using System;
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>Renders only the civilization's legitimate research horizon. Unknown technology
/// never enters this presentation model, so the view can later consume Adaptive Research nodes.</summary>
public partial class ResearchHorizonView : VBoxContainer
{
    private readonly Dictionary<string, NodeControls> _nodes = new(StringComparer.Ordinal);
    private string _signature = string.Empty;

    public ResearchHorizonView()
    {
        AddThemeConstantOverride("separation", 8);
    }

    public void UpdateNodes(
        IReadOnlyList<UiResearchHorizonNode> nodes,
        Action<string> start,
        Action<string> pause,
        Action<string> resume)
    {
        // Funding/runway detail changes every simulation update. It must not replace the
        // controls a player is reading or focused on. Identity, ordering and available
        // command are the only reasons to rebuild a node.
        var signature = string.Join('|', nodes.Select(node =>
            $"{node.Id}:{node.State}:{node.CanStart}:{node.CanPause}:{node.CanResume}"));
        if (signature != _signature)
        {
            var focusName = GetViewport().GuiGetFocusOwner()?.Name;
            var scroll = FindScrollAncestor();
            var scrollPosition = scroll?.ScrollVertical ?? 0;
            _signature = signature;
            _nodes.Clear();
            foreach (var child in GetChildren())
            {
                RemoveChild(child);
                child.QueueFree();
            }
            AddChild(VisualUi.Text("VISIBLE RESEARCH HORIZON", 11, VisualUi.Accent));
            AddChild(VisualUi.Text("Established knowledge and possibilities your scientists can investigate now.",
                11, VisualUi.Muted, wrap: true));
            var summary = new HBoxContainer { Name = "ResearchSummary" };
            summary.AddThemeConstantOverride("separation", 8);
            summary.AddChild(StatusChip($"{nodes.Count(node => node.State == "ACTIVE PROGRAM")} ACTIVE", VisualUi.Accent));
            summary.AddChild(StatusChip($"{nodes.Count(node => node.CanStart)} AVAILABLE", VisualUi.Gold));
            summary.AddChild(StatusChip($"{nodes.Count(node => node.State == "MATURE")} MATURE", new Color("8fd7b0")));
            AddChild(summary);
            var flow = new ResponsiveGrid
            {
                Name = "ResearchNodes", Columns = 1, ReferenceColumns = 2, CompactColumns = 1,
                SizeFlagsHorizontal = SizeFlags.ExpandFill,
            };
            flow.AddThemeConstantOverride("h_separation", 10);
            flow.AddThemeConstantOverride("v_separation", 10);
            AddChild(flow);
            foreach (var node in nodes)
                flow.AddChild(BuildNode(node, start, pause, resume));

            if (!string.IsNullOrEmpty(focusName) && FindChild(focusName, true, false) is Control focus)
                focus.CallDeferred(Control.MethodName.GrabFocus);
            if (scroll is not null)
                scroll.SetDeferred(ScrollContainer.PropertyName.ScrollVertical, scrollPosition);
        }

        foreach (var node in nodes)
            if (_nodes.TryGetValue(node.Id, out var controls))
                RefreshNode(controls, node);
    }

    private Control BuildNode(
        UiResearchHorizonNode node,
        Action<string> start,
        Action<string> pause,
        Action<string> resume)
    {
        var hasAction = node.CanStart || node.CanPause || node.CanResume;
        var stateColor = StateColor(node.State);
        var card = new PanelContainer
        {
            Name = "ResearchCard_" + node.Id,
            CustomMinimumSize = new Vector2(280, 0),
            SizeFlagsHorizontal = SizeFlags.ExpandFill,
            MouseFilter = MouseFilterEnum.Ignore,
        };
        card.AddThemeStyleboxOverride("panel", VisualUi.OperationSurface(margin: 9));
        var content = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore };
        content.AddThemeConstantOverride("separation", 7);
        card.AddChild(content);
        var body = new HBoxContainer { MouseFilter = MouseFilterEnum.Ignore };
        body.AddThemeConstantOverride("separation", 9);
        content.AddChild(body);
        body.AddChild(new ResearchNodeSigil(node.Id, node.Detail, stateColor));
        var copy = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore, SizeFlagsHorizontal = SizeFlags.ExpandFill };
        copy.AddThemeConstantOverride("separation", 2);
        body.AddChild(copy);
        var header = new HBoxContainer { MouseFilter = MouseFilterEnum.Ignore };
        header.AddChild(VisualUi.Icon(VisualIconLibrary.Research, 20));
        var title = VisualUi.Text(node.Title, 15, Colors.White, wrap: true);
        title.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        header.AddChild(title);
        var state = VisualUi.Text(node.State, 10, stateColor);
        header.AddChild(state);
        copy.AddChild(header);
        var detail = VisualUi.Text(SectionText(node), 11, VisualUi.Muted, wrap: true);
        detail.Name = "ResearchDetail";
        copy.AddChild(detail);
        var progress = new ProgressBar { MinValue = 0, MaxValue = 100, ShowPercentage = false, CustomMinimumSize = new Vector2(0, 5) };
        progress.Value = node.Progress * 100;
        progress.Visible = node.State is "MATURE" or "ACTIVE PROGRAM";
        copy.AddChild(progress);
        var action = VisualUi.Button(ActionLabel(node),
            node.CanStart ? $"Start {node.Title}.\n{node.Detail}" :
            node.CanPause ? $"Pause {node.Title} and stop its operating cost.\n{node.Detail}" :
            node.CanResume ? $"Resume {node.Title}.\n{node.Detail}" : node.Detail,
            () =>
            {
                if (node.CanStart) start(node.Id);
                else if (node.CanPause) pause(node.Id);
                else if (node.CanResume) resume(node.Id);
            }, VisualIconLibrary.Research);
        action.Name = "ResearchNode_" + node.Id;
        action.Disabled = !hasAction;
        action.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        action.TooltipText = node.CanStart ? $"Start {node.Title}.\n{node.Detail}" :
            node.CanPause ? $"Pause {node.Title} and stop its operating cost.\n{node.Detail}" :
            node.CanResume ? $"Resume {node.Title}.\n{node.Detail}" : node.Detail;
        content.AddChild(action);
        _nodes[node.Id] = new NodeControls(card, action, title, state, detail, progress);
        return card;
    }

    private void RefreshNode(NodeControls controls, UiResearchHorizonNode node)
    {
        controls.Title.Text = node.Title;
        controls.State.Text = node.State;
        controls.Detail.Text = SectionText(node);
        controls.Progress.Value = Math.Clamp(node.Progress, 0, 1) * 100;
        controls.Progress.Visible = node.State is "MATURE" or "ACTIVE PROGRAM";
        controls.Action.Text = ActionLabel(node);
        controls.Action.Visible = !string.IsNullOrEmpty(controls.Action.Text);
        controls.Action.Disabled = !(node.CanStart || node.CanPause || node.CanResume);
        controls.Action.TooltipText = node.CanStart ? $"Start {node.Title}.\n{node.Detail}" :
            node.CanPause ? $"Pause {node.Title} and stop its operating cost.\n{node.Detail}" :
            node.CanResume ? $"Resume {node.Title}.\n{node.Detail}" : node.Detail;
    }

    private ScrollContainer? FindScrollAncestor()
    {
        for (Node? current = GetParent(); current is not null; current = current.GetParent())
            if (current is ScrollContainer scroll) return scroll;
        return null;
    }

    private static Color StateColor(string state) => state == "MATURE" ? new Color("64d6a5") :
        state == "ACTIVE PROGRAM" ? VisualUi.Accent : VisualUi.Gold;

    private static string ActionLabel(UiResearchHorizonNode node) => node.CanStart ? "BEGIN RESEARCH  →" :
        node.CanPause ? "PAUSE PROGRAM" : node.CanResume ? "RESUME PROGRAM  →" : string.Empty;

    private static string SectionText(UiResearchHorizonNode node)
    {
        if (string.IsNullOrWhiteSpace(node.WhatItDoes)) return node.Detail;
        return $"WHAT IT DOES\n{node.WhatItDoes}\n\n" +
               $"BENEFITS / UNLOCKS\n{node.Benefits}\n\n" +
               $"COST & TIME\n{node.CostAndTime}\n\n" +
               $"REQUIREMENTS / STATUS\n{node.RequirementsStatus}";
    }

    private sealed record NodeControls(PanelContainer Card, Button Action, Label Title, Label State, Label Detail,
        ProgressBar Progress);

    private static PanelContainer StatusChip(string text, Color color)
    {
        var chip = new PanelContainer();
        var surface = VisualUi.Surface(margin: 7);
        surface.BgColor = new Color(color.R * .08f, color.G * .08f, color.B * .08f, .94f);
        surface.BorderColor = new Color(color.R, color.G, color.B, .38f);
        chip.AddThemeStyleboxOverride("panel", surface);
        chip.AddChild(VisualUi.Text(text, 10, color));
        return chip;
    }
}

/// <summary>A deterministic vector emblem for a visible research node. It gives each program
/// a recognizable map-like identity without importing art or exposing hidden graph data.</summary>
public partial class ResearchNodeSigil : Control
{
    private readonly int _seed;
    private readonly Color _color;

    public ResearchNodeSigil(string id, string detail, Color stateColor)
    {
        Name = "ResearchSigil_" + id;
        CustomMinimumSize = new Vector2(50, 76);
        MouseFilter = MouseFilterEnum.Ignore;
        _seed = id.Aggregate(17, (value, character) => unchecked(value * 31 + character));
        var domain = detail.Split('·', 2)[0].Trim();
        var domainColor = domain switch
        {
            "Computing" => new Color("8d8cff"),
            "Sensors Comms" => new Color("52d7ea"),
            "Space Industry" => new Color("e2aa58"),
            "Life Medicine" => new Color("73d69e"),
            _ => new Color("b9a6ff"),
        };
        _color = domainColor.Lerp(stateColor, .28f);
    }

    public override void _Draw()
    {
        var center = Size * .5f;
        DrawCircle(center, 28, new Color(_color.R, _color.G, _color.B, .07f));
        DrawCircle(center, 22, new Color(.008f, .025f, .04f, .9f));
        DrawArc(center, 22, -.9f, 4.7f, 40, new Color(_color.R, _color.G, _color.B, .72f), 1.4f, true);
        DrawArc(center, 15, .8f, 5.7f, 32, new Color(_color.R, _color.G, _color.B, .35f), 1, true);
        var spokes = 3 + Math.Abs(_seed % 3);
        for (var index = 0; index < spokes; index++)
        {
            var angle = (_seed % 29) * .07f + index * MathF.Tau / spokes;
            var inner = center + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * 5;
            var outer = center + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * (13 + index % 2 * 5);
            DrawLine(inner, outer, new Color(_color.R, _color.G, _color.B, .5f), 1, true);
            DrawCircle(outer, 2.2f, _color);
        }
        DrawCircle(center, 5.2f, _color);
        DrawCircle(center, 1.8f, Colors.White);
    }
}
