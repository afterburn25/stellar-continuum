using System;
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>Fullscreen, observer-safe research graph. Topology changes rebuild controls;
/// progress, funding, filters and camera transforms update the existing graph in place.</summary>
public partial class ResearchWorkspaceView : PanelContainer
{
    private static readonly (string Name, string[] Domains)[] Tabs =
    {
        ("ALL RESEARCH", Array.Empty<string>()),
        ("PHYSICS", new[] { "foundations", "research_infrastructure" }),
        ("ENGINEERING", new[] { "propulsion", "space_industry", "planetary", "military", "logistics", "stellar_engineering" }),
        ("ENERGY", new[] { "energy" }),
        ("COMPUTING", new[] { "computing", "sensors_comms", "synthetic_systems", "cybernetics" }),
        ("MATERIALS", new[] { "materials" }),
        ("BIOLOGY & MEDICINE", new[] { "life_medicine", "biotechnology", "biosphere_agriculture", "alternative_biochemistry" }),
        ("SOCIETY & ECONOMY", new[] { "social_admin", "economic_trade" }),
        ("XENOSCIENCE", new[] { "xenoscience" }),
    };

    private static readonly Vector2 NodeSize = new(190, 82);
    private readonly Dictionary<string, WorkspaceNode> _nodes = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Button> _buttons = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Vector2> _layout = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Button> _tabButtons = new(StringComparer.Ordinal);
    private readonly ResearchGraphCanvas _graph = new();
    private readonly Label _inspectorTitle = VisualUi.Text("SELECT A PROGRAM", 20, Colors.White, wrap: true);
    private readonly Label _inspectorState = VisualUi.Text("Choose a node to inspect its known details.", 11, VisualUi.Muted, wrap: true);
    private readonly Label _inspectorBody = VisualUi.Text(string.Empty, 12, VisualUi.Muted, wrap: true);
    private readonly ProgressBar _inspectorProgress = new() { MinValue = 0, MaxValue = 100, ShowPercentage = false, CustomMinimumSize = new Vector2(0, 7) };
    private readonly Button _inspectorAction;
    private readonly Label _empty = VisualUi.Text("No known research matches this view.", 14, VisualUi.Muted, wrap: true);
    private readonly LineEdit _search = new() { Name = "ResearchSearch", PlaceholderText = "Search known name, field, purpose, or benefit" };
    private IReadOnlyList<UiResearchHorizonEdge> _edges = Array.Empty<UiResearchHorizonEdge>();
    private string _tab = "ALL RESEARCH";
    private string _signature = string.Empty;
    private string? _selectedKey;
    private Vector2 _pan = new(44, 36);
    private float _zoom = 1f;

    public event Action<string>? Start;
    public event Action<string>? Pause;
    public event Action<string>? Resume;
    public event Action? CloseRequested;

    public int GraphControlCount => _buttons.Count;
    public float GraphZoom => _zoom;
    public Vector2 GraphPan => _pan;
    public string? SelectedCommandId => _selectedKey is not null && _nodes.TryGetValue(_selectedKey, out var node) ? node.Detail?.Id : null;

    public ResearchWorkspaceView()
    {
        Name = "ResearchWorkspace";
        Visible = false;
        VisualUi.ContainPointerInput(this);
        FocusMode = FocusModeEnum.All;
        ZIndex = 80;
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        OffsetTop = 72;
        AddThemeStyleboxOverride("panel", VisualUi.OperationSurface(10));
        _inspectorTitle.Name = "ResearchInspectorTitle";
        _inspectorState.Name = "ResearchInspectorState";
        _inspectorBody.Name = "ResearchInspectorBody";
        _inspectorProgress.Name = "ResearchInspectorProgress";

        var margin = new MarginContainer();
        margin.AddThemeConstantOverride("margin_left", 12);
        margin.AddThemeConstantOverride("margin_right", 12);
        margin.AddThemeConstantOverride("margin_top", 8);
        margin.AddThemeConstantOverride("margin_bottom", 12);
        AddChild(margin);
        var root = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ExpandFill };
        root.AddThemeConstantOverride("separation", 8);
        margin.AddChild(root);

        var header = new HBoxContainer();
        header.AddThemeConstantOverride("separation", 10);
        var heading = VisualUi.Heading("RESEARCH NETWORK", 22);
        heading.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        header.AddChild(heading);
        _search.CustomMinimumSize = new Vector2(330, 42);
        _search.TextChanged += _ => ApplyFilter(centerFirst: true);
        _search.TextSubmitted += _ => SelectFirstVisible();
        header.AddChild(_search);
        var close = VisualUi.Button("CLOSE", "Close research and return to the map.", RequestClose, VisualIconLibrary.NavClose);
        close.Name = "ResearchWorkspaceClose";
        header.AddChild(close);
        root.AddChild(header);

        var tabs = new FlowContainer { Name = "ResearchCategoryTabs" };
        tabs.AddThemeConstantOverride("h_separation", 5);
        tabs.AddThemeConstantOverride("v_separation", 5);
        root.AddChild(tabs);
        foreach (var tab in Tabs)
        {
            var name = tab.Name;
            var button = VisualUi.Button(name, $"Show the {name.ToLowerInvariant()} branch.", () => SelectTab(name));
            button.Name = "ResearchTab_" + name.Replace(' ', '_').Replace('&', '_');
            button.ToggleMode = true;
            button.ButtonPressed = name == _tab;
            tabs.AddChild(button);
            _tabButtons[name] = button;
        }

        var body = new HSplitContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ExpandFill };
        root.AddChild(body);
        _graph.Name = "ResearchGraph";
        _graph.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        _graph.SizeFlagsVertical = SizeFlags.ExpandFill;
        _graph.CustomMinimumSize = new Vector2(520, 380);
        _graph.TransformChanged += OnGraphTransform;
        body.AddChild(_graph);
        _empty.Position = new Vector2(28, 28);
        _empty.MouseFilter = MouseFilterEnum.Ignore;
        _graph.AddChild(_empty);

        var inspectorPanel = new PanelContainer { Name = "ResearchInspector", CustomMinimumSize = new Vector2(350, 0) };
        inspectorPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 12));
        var inspector = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ExpandFill };
        inspector.AddThemeConstantOverride("separation", 9);
        inspectorPanel.AddChild(inspector);
        inspector.AddChild(VisualUi.Text("PROGRAM INSPECTOR", 10, VisualUi.Accent));
        inspector.AddChild(_inspectorTitle);
        inspector.AddChild(_inspectorState);
        inspector.AddChild(_inspectorProgress);
        var inspectorScroll = new ScrollContainer
        {
            Name = "ResearchInspectorScroll", HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            SizeFlagsHorizontal = SizeFlags.ExpandFill,
            SizeFlagsVertical = SizeFlags.ExpandFill,
        };
        _inspectorBody.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        inspectorScroll.AddChild(_inspectorBody);
        inspector.AddChild(inspectorScroll);
        _inspectorAction = VisualUi.Button(string.Empty, "Select an available program first.", InvokeSelectedAction, VisualIconLibrary.Research);
        _inspectorAction.Name = "ResearchWorkspaceAction";
        _inspectorAction.Visible = false;
        inspector.AddChild(_inspectorAction);
        body.AddChild(inspectorPanel);

        Resized += () =>
        {
            inspectorPanel.CustomMinimumSize = new Vector2(Size.X < 1120 ? 285 : 350, 0);
            ApplyTransform();
        };
    }

    public override void _Ready()
    {
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        OffsetTop = 72;
        SetProcessUnhandledKeyInput(true);
    }

    public override void _UnhandledKeyInput(InputEvent @event)
    {
        if (!Visible || @event is not InputEventKey { Pressed: true, Echo: false, Keycode: Key.Escape }) return;
        GetViewport().SetInputAsHandled();
        RequestClose();
    }

    public void Open()
    {
        Visible = true;
        MoveToFront();
        GrabFocus();
        if (_selectedKey is null) SelectDefault();
    }

    public void UpdateWorkspace(
        IReadOnlyList<UiResearchHorizonNode> visibleNodes,
        IReadOnlyList<UiResearchPreviewNode> lockedNodes,
        IReadOnlyList<UiResearchHorizonEdge> edges)
    {
        var next = new Dictionary<string, WorkspaceNode>(StringComparer.Ordinal);
        foreach (var locked in lockedNodes)
            next[locked.GraphKey] = new(locked.GraphKey, locked.DomainId, locked.GraphDepth, null);
        foreach (var detail in visibleNodes.Where(IsDetailed))
            next[detail.GraphKey] = new(detail.GraphKey, detail.DomainId, detail.GraphDepth, detail);

        var signature = string.Join('|', next.Values.OrderBy(node => node.Key, StringComparer.Ordinal)
            .Select(node => $"{node.Key}:{node.Domain}:{node.Depth}:{node.Detail is null}")) + "/" +
            string.Join('|', edges.OrderBy(edge => edge.FromId, StringComparer.Ordinal).ThenBy(edge => edge.ToId, StringComparer.Ordinal)
                .Select(edge => $"{edge.FromId}>{edge.ToId}:{edge.Relationship}"));
        _nodes.Clear();
        foreach (var pair in next) _nodes[pair.Key] = pair.Value;
        _edges = edges;
        if (signature != _signature)
        {
            _signature = signature;
            RebuildGraph();
        }
        else
        {
            RefreshNodes();
            RefreshInspector();
        }
    }

    private static bool IsDetailed(UiResearchHorizonNode node) =>
        node.State is "INVESTIGABLE" or "EXPERIMENTAL" or "DEMONSTRATED" or "ENGINEERING" or "MATURE" or "ARCHIVED" or "ACTIVE PROGRAM";

    private void RebuildGraph()
    {
        var previousSelection = _selectedKey;
        foreach (var button in _buttons.Values)
        {
            _graph.RemoveChild(button);
            button.QueueFree();
        }
        _buttons.Clear();
        _layout.Clear();

        var orderedDomains = Tabs.Skip(1).SelectMany(tab => tab.Domains).Distinct(StringComparer.Ordinal)
            .Concat(_nodes.Values.Select(node => node.Domain)).Distinct(StringComparer.Ordinal).ToArray();
        var domainStarts = new Dictionary<string, float>(StringComparer.Ordinal);
        var nextDomainX = 34f;
        foreach (var domain in orderedDomains)
        {
            domainStarts[domain] = nextDomainX;
            var widestDepth = Math.Max(1, _nodes.Values.Where(node => node.Domain == domain)
                .GroupBy(node => node.Depth).Select(group => group.Count()).DefaultIfEmpty(1).Max());
            nextDomainX += widestDepth * 202 + 34;
        }
        var slots = new Dictionary<(string Domain, int Depth), int>();
        foreach (var node in _nodes.Values.OrderBy(node => node.Depth).ThenBy(node => Array.IndexOf(orderedDomains, node.Domain)).ThenBy(node => node.Key, StringComparer.Ordinal))
        {
            var slotKey = (node.Domain, node.Depth);
            var slot = slots.GetValueOrDefault(slotKey);
            slots[slotKey] = slot + 1;
            var world = new Vector2(domainStarts[node.Domain] + slot * 202, 34 + node.Depth * 138);
            _layout[node.Key] = world;
            var button = VisualUi.Button(string.Empty, string.Empty, () => SelectNode(node.Key),
                node.Detail is null ? VisualIconLibrary.ResearchLocked : VisualIconLibrary.Research);
            button.Name = node.Detail is null ? "ResearchLocked_" + node.Key : "ResearchGraphNode_" + node.GraphSafeCommandSuffix;
            button.CustomMinimumSize = NodeSize;
            button.Size = NodeSize;
            button.ClipText = true;
            button.ToggleMode = true;
            _graph.AddChild(button);
            _buttons[node.Key] = button;
        }
        _graph.SetGraph(_buttons, _edges);
        RefreshNodes();
        ApplyFilter(centerFirst: false);
        if (previousSelection is not null && _nodes.ContainsKey(previousSelection)) SelectNode(previousSelection, center: false);
        else SelectDefault(center: true);
    }

    private void RefreshNodes()
    {
        foreach (var pair in _buttons)
        {
            var node = _nodes[pair.Key];
            var button = pair.Value;
            if (node.Detail is null)
            {
                button.Text = "????\nLOCKED";
                button.TooltipText = "Locked research. Advance known prerequisite branches to reveal it.";
                button.Modulate = DomainColor(node.Domain).Lerp(new Color(.58f, .64f, .72f, .86f), .64f);
            }
            else
            {
                button.Text = $"{GraphTitle(node.Detail.Title)}\n{NodeState(node.Detail)}";
                button.TooltipText = $"Select {node.Detail.Title}.\n{node.Detail.WhatItDoes}";
                var stateColor = node.Detail.State == "MATURE" ? new Color("9ce6bd") :
                    node.Detail.State == "ACTIVE PROGRAM" ? new Color("8fdcff") : Colors.White;
                button.Modulate = stateColor.Lerp(DomainColor(node.Domain), .24f);
            }
        }
    }

    private static string GraphTitle(string title)
    {
        const int width = 21;
        var words = title.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (words.Length == 0) return string.Empty;
        var first = words[0].Length <= width ? words[0] : words[0][..(width - 3)] + "...";
        var index = 1;
        while (index < words.Length && first.Length + 1 + words[index].Length <= width)
            first += " " + words[index++];
        if (index == words.Length) return first;
        var second = words[index].Length <= width ? words[index] : words[index][..(width - 3)] + "...";
        index++;
        while (index < words.Length && second.Length + 1 + words[index].Length <= width)
            second += " " + words[index++];
        if (index < words.Length)
            second = second.Length <= width - 3 ? second + "..." : second[..(width - 3)].TrimEnd() + "...";
        return first + "\n" + second;
    }

    private void SelectTab(string name)
    {
        _tab = name;
        foreach (var pair in _tabButtons) pair.Value.ButtonPressed = pair.Key == name;
        ApplyFilter(centerFirst: true);
    }

    private void ApplyFilter(bool centerFirst)
    {
        var domains = Tabs.First(tab => tab.Name == _tab).Domains;
        var query = _search.Text.Trim();
        foreach (var pair in _buttons)
        {
            var node = _nodes[pair.Key];
            var inTab = domains.Length == 0 || domains.Contains(node.Domain, StringComparer.Ordinal);
            var matches = query.Length == 0 || node.Detail is not null && SearchText(node.Detail).Contains(query, StringComparison.OrdinalIgnoreCase);
            pair.Value.Visible = inTab && matches;
        }
        _empty.Visible = !_buttons.Values.Any(button => button.Visible);
        ApplyTransform();
        if (centerFirst) SelectFirstVisible();
    }

    private void SelectFirstVisible()
    {
        var query = _search.Text.Trim();
        var first = _buttons.Where(pair => pair.Value.Visible && _nodes[pair.Key].Detail is not null)
            .OrderBy(pair => SearchRank(_nodes[pair.Key].Detail!, query))
            .ThenBy(pair => _nodes[pair.Key].Depth)
            .ThenBy(pair => pair.Key, StringComparer.Ordinal)
            .FirstOrDefault();
        if (!string.IsNullOrEmpty(first.Key)) SelectNode(first.Key);
    }

    private static int SearchRank(UiResearchHorizonNode node, string query)
    {
        if (query.Length == 0) return 0;
        if (node.Title.Equals(query, StringComparison.OrdinalIgnoreCase)) return 0;
        if (node.Title.StartsWith(query, StringComparison.OrdinalIgnoreCase)) return 1;
        if (node.Title.Contains(query, StringComparison.OrdinalIgnoreCase)) return 2;
        return 3;
    }

    private void SelectDefault(bool center = false)
    {
        var preferred = _nodes.Values.Where(node => node.Detail is not null)
            .OrderBy(node => node.Detail!.State == "ACTIVE PROGRAM" ? 0 : node.Detail.CanStart ? 1 : node.Detail.State == "MATURE" ? 2 : 3)
            .ThenBy(node => node.Depth).ThenBy(node => node.Key, StringComparer.Ordinal).FirstOrDefault();
        if (preferred is not null) SelectNode(preferred.Key, center);
        else RefreshInspector();
    }

    private void SelectNode(string key, bool center = true)
    {
        if (!_nodes.ContainsKey(key)) return;
        _selectedKey = key;
        foreach (var pair in _buttons) pair.Value.ButtonPressed = pair.Key == key;
        RefreshInspector();
        if (center) CenterNode(key);
    }

    private void CenterNode(string key)
    {
        if (!_layout.TryGetValue(key, out var world) || _graph.Size.X < 2) return;
        _pan = _graph.Size * .5f - (world + NodeSize * .5f) * _zoom;
        ApplyTransform();
    }

    private void RefreshInspector()
    {
        if (_selectedKey is null || !_nodes.TryGetValue(_selectedKey, out var node))
        {
            _inspectorTitle.Text = "SELECT A PROGRAM";
            _inspectorState.Text = "Choose a node to inspect its known details.";
            _inspectorBody.Text = string.Empty;
            _inspectorProgress.Visible = false;
            _inspectorAction.Visible = false;
            _inspectorAction.Name = "ResearchWorkspaceAction";
            return;
        }
        if (node.Detail is null)
        {
            _inspectorTitle.Text = "????";
            _inspectorState.Text = "LOCKED";
            _inspectorBody.Text = "This branch has not been revealed. Advance known research to discover what becomes possible.";
            _inspectorProgress.Visible = false;
            _inspectorAction.Visible = false;
            _inspectorAction.Name = "ResearchWorkspaceAction";
            return;
        }
        var detail = node.Detail;
        _inspectorTitle.Text = detail.Title;
        _inspectorState.Text = $"{DisplayDomain(detail.DomainId)}  ·  {NodeState(detail)}";
        _inspectorBody.Text = $"WHAT IT DOES\n{detail.WhatItDoes}\n\nBENEFITS / UNLOCKS\n{detail.Benefits}\n\nCOST & TIME\n{detail.CostAndTime}\n\nREQUIREMENTS / STATUS\n{detail.RequirementsStatus}";
        _inspectorProgress.Visible = detail.State is "MATURE" or "ACTIVE PROGRAM" or "EXPERIMENTAL" or "DEMONSTRATED" or "ENGINEERING";
        _inspectorProgress.Value = Math.Clamp(detail.Progress, 0, 1) * 100;
        var canOfferBegin = detail.State == "INVESTIGABLE";
        var pausedProgram = detail.State == "ACTIVE PROGRAM" && !detail.CanPause &&
            (detail.CanResume || detail.Detail.Contains("paused", StringComparison.OrdinalIgnoreCase));
        _inspectorAction.Text = detail.CanStart || canOfferBegin ? "BEGIN RESEARCH" : detail.CanPause ? "PAUSE PROGRAM" : pausedProgram ? "RESUME PROGRAM" : string.Empty;
        _inspectorAction.Name = "ResearchNode_" + detail.Id;
        _inspectorAction.Visible = detail.CanStart || canOfferBegin || detail.CanPause || pausedProgram;
        _inspectorAction.Disabled = !detail.CanStart && !detail.CanPause && !detail.CanResume;
        _inspectorAction.TooltipText = detail.Detail;
    }

    private void InvokeSelectedAction()
    {
        if (_selectedKey is null || !_nodes.TryGetValue(_selectedKey, out var node) || node.Detail is not { } detail) return;
        if (detail.CanStart) Start?.Invoke(detail.Id);
        else if (detail.CanPause) Pause?.Invoke(detail.Id);
        else if (detail.CanResume) Resume?.Invoke(detail.Id);
    }

    private void OnGraphTransform(Vector2 pan, float zoom)
    {
        _pan = pan;
        _zoom = zoom;
        ApplyTransform();
    }

    private void ApplyTransform()
    {
        foreach (var pair in _buttons)
        {
            pair.Value.Position = _pan + _layout[pair.Key] * _zoom;
            pair.Value.Scale = Vector2.One * _zoom;
        }
        _graph.Camera = new(_pan, _zoom);
        _graph.QueueRedraw();
    }

    private void RequestClose()
    {
        Visible = false;
        CloseRequested?.Invoke();
    }

    private static string NodeState(UiResearchHorizonNode node) => node.CanStart ? "AVAILABLE" : node.State;
    private static string SearchText(UiResearchHorizonNode node) => $"{node.Title} {DisplayDomain(node.DomainId)} {node.WhatItDoes} {node.Benefits}";
    private static string DisplayDomain(string domain) => domain.Replace('_', ' ').ToUpperInvariant();
    private static Color DomainColor(string domain) => GroupIndex(domain) switch
    {
        0 => new Color("84b8ff"), 1 => new Color("e2aa58"), 2 => new Color("f4cf63"),
        3 => new Color("8d8cff"), 4 => new Color("b9a6ff"), 5 => new Color("73d69e"),
        6 => new Color("d484b8"), _ => new Color("62d0cc"),
    };
    private static int GroupIndex(string domain)
    {
        for (var index = 1; index < Tabs.Length; index++)
            if (Tabs[index].Domains.Contains(domain, StringComparer.Ordinal)) return index - 1;
        return Tabs.Length - 2;
    }

    private sealed record WorkspaceNode(string Key, string Domain, int Depth, UiResearchHorizonNode? Detail)
    {
        public string GraphSafeCommandSuffix => Key.StartsWith("research-", StringComparison.Ordinal) ? Key[9..] : Key;
    }
}

internal partial class ResearchGraphCanvas : Control
{
    private IReadOnlyDictionary<string, Button> _buttons = new Dictionary<string, Button>();
    private IReadOnlyList<UiResearchHorizonEdge> _edges = Array.Empty<UiResearchHorizonEdge>();
    private bool _pressed;
    private bool _dragging;
    private Vector2 _pressPoint;
    public (Vector2 Pan, float Zoom) Camera { get; set; } = (new Vector2(44, 36), 1f);
    public event Action<Vector2, float>? TransformChanged;

    public ResearchGraphCanvas()
    {
        ClipContents = true;
        MouseFilter = MouseFilterEnum.Stop;
        FocusMode = FocusModeEnum.All;
    }

    public void SetGraph(IReadOnlyDictionary<string, Button> buttons, IReadOnlyList<UiResearchHorizonEdge> edges)
    {
        _buttons = buttons;
        _edges = edges;
        QueueRedraw();
    }

    public override void _GuiInput(InputEvent @event)
    {
        if (@event is InputEventMouseButton { ButtonIndex: MouseButton.Left } button)
        {
            _pressed = button.Pressed;
            _dragging = false;
            _pressPoint = button.Position;
            AcceptEvent();
            return;
        }
        if (@event is InputEventMouseMotion motion && _pressed)
        {
            if (!_dragging && motion.Position.DistanceTo(_pressPoint) < 5) return;
            _dragging = true;
            TransformChanged?.Invoke(Camera.Pan + motion.Relative, Camera.Zoom);
            AcceptEvent();
            return;
        }
        if (@event is InputEventMouseButton { Pressed: true } wheel && wheel.ButtonIndex is MouseButton.WheelUp or MouseButton.WheelDown)
        {
            var next = Math.Clamp(Camera.Zoom * (wheel.ButtonIndex == MouseButton.WheelUp ? 1.12f : 1f / 1.12f), .58f, 1.55f);
            var world = (wheel.Position - Camera.Pan) / Camera.Zoom;
            TransformChanged?.Invoke(wheel.Position - world * next, next);
            AcceptEvent();
        }
    }

    public override void _Draw()
    {
        DrawRect(new Rect2(Vector2.Zero, Size), new Color("050b12"));
        var visibleGraph = new Rect2(Vector2.Zero, Size).Grow(8);
        foreach (var edge in _edges)
        {
            if (!_buttons.TryGetValue(edge.FromId, out var from) || !_buttons.TryGetValue(edge.ToId, out var to) || !from.Visible || !to.Visible) continue;
            var fromRect = new Rect2(from.Position, from.Size * from.Scale);
            var toRect = new Rect2(to.Position, to.Size * to.Scale);
            // Do not draw long chords between nodes that are both or partly offscreen. Those
            // links used to form bright parallel bands across every filtered branch view.
            if (!visibleGraph.Intersects(fromRect) || !visibleGraph.Intersects(toRect)) continue;
            var start = from.Position + from.Size * from.Scale * new Vector2(.5f, 1f);
            var end = to.Position + to.Size * to.Scale * new Vector2(.5f, 0f);
            var middle = (start.Y + end.Y) * .5f;
            var selectedBranch = from.ButtonPressed || to.ButtonPressed;
            var color = edge.Relationship == "known_alternative"
                ? new Color("b596e8", selectedBranch ? .72f : .18f)
                : new Color("58bfd4", selectedBranch ? .68f : .15f);
            var width = selectedBranch ? 2.4f : 1.1f;
            var controlA = new Vector2(start.X, middle);
            var controlB = new Vector2(end.X, middle);
            var previous = start;
            for (var step = 1; step <= 14; step++)
            {
                var t = step / 14f;
                var inverse = 1f - t;
                var point = inverse * inverse * inverse * start + 3f * inverse * inverse * t * controlA +
                    3f * inverse * t * t * controlB + t * t * t * end;
                DrawLine(previous, point, color, width, true);
                previous = point;
            }
            DrawCircle(end, 3.2f, color);
        }
    }
}
