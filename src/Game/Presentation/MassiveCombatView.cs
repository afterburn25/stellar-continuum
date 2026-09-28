using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Godot;
using Game.Presentation.Spatial;
using Game.Simulation.Combat.Massive;
using NumericsVector2 = System.Numerics.Vector2;

namespace Game.Presentation;

/// <summary>
/// Full-screen tactical presentation driven exclusively by an observer-filtered snapshot.
/// It owns no battle state and can issue commands only through the supplied callback.
/// </summary>
public sealed partial class MassiveCombatView : Control
{
    private const float MinimumZoom = .18f;
    private const float MaximumZoom = 7f;
    private const float SelectionRadius = 28f;
    private readonly HashSet<long> _selection = new();
    private MassiveCombatSnapshot? _snapshot;
    private int _observerCivilizationId = -1;
    private MassiveCombatFormationPool _formationPool = null!;
    private SystemScene3D _systemBattleScene = null!;
    private SystemSpatialSnapshot? _systemSnapshot;
    private Font _font = null!;
    private Vector2 _cameraCenter;
    private float _zoom = 1;
    private Vector2 _worldCenter;
    private bool _cameraInitialized;
    private bool _panning;
    private bool _boxSelecting;
    private Vector2 _pointerDown;
    private Vector2 _selectionEnd;
    private Vector2 _lastPointer;
    private long? _hoveredFormation;
    private long? _targetingSource;
    private MassiveCombatOrderType _targetingOrder;
    private long _latestEventSequence;
    private double _tacticalSpeed = 1;
    private readonly List<VisualEvent> _visualEvents = new();
    private Label _status = null!;
    private Label _selectionSummary = null!;
    private Label _battleSummary = null!;
    private HBoxContainer _speedRow = null!;
    private Button _tacticalPlay = null!, _tacticalSpeedButton = null!;
    private double _tacticalSelectedSpeed = 1;
    private HFlowContainer _orders = null!;

    public Func<MassiveCombatOrder, MassiveCombatOrderResult>? OrderRequested { get; set; }
    public Action<double>? TacticalSpeedRequested { get; set; }
    public Action<double>? TacticalResumeSpeedRequested { get; set; }
    public Action? MenuRequested { get; set; }
    public IReadOnlyCollection<long> SelectedFormationIds => _selection;
    public int RenderedOrdinaryTokens => _systemSnapshot is not null ? _systemBattleScene.CombatRepresentativeCount :
        _formationPool.Multimesh is { } pool
        ? pool.VisibleInstanceCount < 0 ? pool.InstanceCount : pool.VisibleInstanceCount
        : 0;
    public int RenderedDetailedVessels => _systemBattleScene?.CombatDetailedVesselCount ?? 0;
    public int RenderedCombatEffects => _systemBattleScene?.CombatEffectCount ?? 0;
    public int RenderedSystemBodies => _systemBattleScene?.BodyCount ?? 0;
    public int RenderedSystemStars => _systemBattleScene?.PrimaryStarCount ?? 0;
    public int RenderedDetailedMeshes => _systemBattleScene?.CombatDetailedMeshCount ?? 0;
    public int RenderedWeaponMounts => _systemBattleScene?.CombatWeaponMountCount ?? 0;
    public bool HasDetailedVesselIn(Rect2 logicalBounds) =>
        _systemBattleScene?.HasVisibleDetailedCombatVessel(logicalBounds) == true;
    public Basis CombatCameraBasis => _systemBattleScene?.CameraBasis ?? Basis.Identity;
    public Vector2? GetFormationScreenPosition(long formationId) =>
        Find(formationId) is { } formation ? ToScreen(formation.Position) : null;
    public void FocusObservedPosition(MassivePoint position) =>
        _systemBattleScene?.FocusCombatPosition(new Vector2(position.X, position.Y));

    public void SetTacticalSpeedState(double speed)
    {
        _tacticalSpeed = speed;
        _systemBattleScene?.SetCombatAnimationRunning(speed > 0);
        if (speed > 0) _tacticalSelectedSpeed = speed;
        RefreshTacticalControls();
    }

    public void SetTacticalResumeSpeedState(double speed)
    {
        if (speed > 0) _tacticalSelectedSpeed = speed;
        RefreshTacticalControls();
    }

    public override void _Ready()
    {
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        MouseFilter = MouseFilterEnum.Stop;
        MouseForcePassScrollEvents = false;
        FocusMode = FocusModeEnum.All;
        ClipContents = true;
        _font = ThemeDB.FallbackFont;
        _systemBattleScene = new SystemScene3D { Name = "TacticalSystemScene", MouseFilter = MouseFilterEnum.Ignore,
            ShowBehindParent = true };
        AddChild(_systemBattleScene);
        _formationPool = new MassiveCombatFormationPool { Name = "FormationTokenPool" };
        AddChild(_formationPool);
        BuildInterface();
        Resized += () => { RefreshPool(); QueueRedraw(); };
        VisibilityChanged += () => { if (Visible) GrabFocus(); };
        SetProcess(true);
    }

    public void UpdateSnapshot(MassiveCombatSnapshot? snapshot, int observerCivilizationId,
        SystemSpatialSnapshot? systemSnapshot = null)
    {
        var battleChanged = snapshot is not null && _snapshot is not null && snapshot.BattleId != _snapshot.BattleId;
        var observerChanged = snapshot is not null && _snapshot is not null && observerCivilizationId != _observerCivilizationId;
        var environmentModeChanged = snapshot is not null && _snapshot is not null &&
            (systemSnapshot is null) != (_systemSnapshot is null);
        if (battleChanged || observerChanged)
        {
            _selection.Clear(); _visualEvents.Clear(); _latestEventSequence = 0; _cameraInitialized = false;
            _systemBattleScene.PresentCombat(null, -1, Array.Empty<long>());
        }
        else if (environmentModeChanged)
            _cameraInitialized = false;
        _snapshot = snapshot;
        _systemSnapshot = systemSnapshot;
        _observerCivilizationId = observerCivilizationId;
        Visible = snapshot is not null;
        if (snapshot is null)
        {
            _selection.Clear();
            _visualEvents.Clear();
            _latestEventSequence = 0;
            _cameraInitialized = false;
            _systemBattleScene.Clear();
            if (_formationPool.Multimesh is { } empty) empty.InstanceCount = 0;
            return;
        }

        if (systemSnapshot is not null)
            _systemBattleScene.Present(systemSnapshot);
        _systemBattleScene.Visible = systemSnapshot is not null;

        var visibleIds = snapshot.Formations.Select(formation => formation.FormationId).ToHashSet();
        _selection.RemoveWhere(id => !visibleIds.Contains(id) || !IsOwned(id));
        if (_selection.Count == 0 && snapshot.Formations.FirstOrDefault(IsOwned) is { } firstOwn)
            _selection.Add(firstOwn.FormationId);
        if (systemSnapshot is not null)
            _systemBattleScene.PresentCombat(snapshot, observerCivilizationId, _selection);
        else
            _systemBattleScene.PresentCombat(null, -1, Array.Empty<long>());
        if (!_cameraInitialized) FitEncounter();
        IngestEvents(snapshot.Events);
        RefreshPresentation();
    }

    public override void _Process(double delta)
    {
        if (!Visible || _snapshot is null) return;
        _systemBattleScene.SetCombatAnimationRunning(_tacticalSpeed > 0);
        _systemBattleScene.Advance(delta);
        for (var index = _visualEvents.Count - 1; index >= 0; index--)
        {
            _visualEvents[index] = _visualEvents[index] with { Age = _visualEvents[index].Age + (float)Math.Max(0, delta) };
            if (_visualEvents[index].Age > _visualEvents[index].Lifetime) _visualEvents.RemoveAt(index);
        }
        QueueRedraw();
    }

    public override void _GuiInput(InputEvent input)
    {
        if (_snapshot is null) { AcceptEvent(); return; }
        switch (input)
        {
            case InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.WheelUp } wheelUp:
                ZoomAt(wheelUp.Position, 1.16f); break;
            case InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.WheelDown } wheelDown:
                ZoomAt(wheelDown.Position, 1f / 1.16f); break;
            case InputEventMouseButton { ButtonIndex: MouseButton.Middle } middle:
                _panning = middle.Pressed; _lastPointer = middle.Position; break;
            case InputEventMouseButton { ButtonIndex: MouseButton.Left } left:
                HandleLeftButton(left); break;
            case InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Right } right:
                IssueContextOrder(right.Position); break;
            case InputEventMouseMotion motion:
                HandleMotion(motion); break;
            case InputEventKey { Pressed: true, Echo: false, Keycode: Key.Escape }:
                if (_targetingSource.HasValue) { _targetingSource = null; SetStatus("Targeting cancelled."); }
                else MenuRequested?.Invoke();
                break;
            case InputEventKey { Pressed: true, Echo: false, Keycode: Key.F }:
                FitEncounter(); break;
            case InputEventKey { Pressed: true, Echo: false, Keycode: Key.Space }:
                ToggleTacticalPause(); break;
            case InputEventKey { Pressed: true, Echo: false } speedKey when TacticalSpeedFor(speedKey.Keycode) is { } speed:
                TacticalSpeedRequested?.Invoke(speed); break;
        }
        AcceptEvent();
    }

    public override void _Draw()
    {
        var viewport = Size;
        if (_systemSnapshot is null)
        {
            DrawRect(new Rect2(Vector2.Zero, viewport), new Color(.008f, .016f, .027f), true);
            DrawGrid(viewport);
        }
        if (_snapshot is null) return;
        DrawInterdictionFields();
        DrawFormationGuides();
        if (_systemSnapshot is null)
        {
            DrawWeaponEffects();
            DrawImportantVessels();
        }
        DrawFormationLabels();
        if (_boxSelecting)
        {
            var box = RectFromPoints(_pointerDown, _selectionEnd);
            DrawRect(box, new Color(VisualPalette.Selected, .11f), true);
            DrawRect(box, VisualPalette.Selected, false, 1.5f);
        }
        DrawScale(viewport);
    }

    private void BuildInterface()
    {
        var top = new PanelContainer { Name = "TacticalHeader", MouseFilter = MouseFilterEnum.Stop };
        top.SetAnchorsPreset(LayoutPreset.TopWide); top.OffsetBottom = 74;
        top.AddThemeStyleboxOverride("panel", VisualUi.Surface()); AddChild(top);
        var topRow = new HBoxContainer(); topRow.AddThemeConstantOverride("separation", 12); top.AddChild(topRow);
        var title = VisualUi.Text("TACTICAL COMBAT", 21, VisualPalette.TextPrimary); title.SizeFlagsHorizontal = SizeFlags.ExpandFill; topRow.AddChild(title);
        _battleSummary = VisualUi.Text("", 11, VisualPalette.TextSecondary);
        _battleSummary.HorizontalAlignment = HorizontalAlignment.Right;
        topRow.AddChild(_battleSummary);
        _speedRow = new HBoxContainer { Name = "TacticalSpeedControls" }; topRow.AddChild(_speedRow);
        _tacticalPlay = VisualUi.Button("▶", "Play or pause tactical simulation.", ToggleTacticalPause);
        _tacticalPlay.Name = "TacticalPlaybackButton"; _speedRow.AddChild(_tacticalPlay);
        _tacticalSpeedButton = VisualUi.Button("› 1×", "Select the next tactical speed.", CycleTacticalSpeed);
        _tacticalSpeedButton.Name = "TacticalPlaybackSpeedButton"; _speedRow.AddChild(_tacticalSpeedButton);
        topRow.AddChild(VisualUi.Button("Fit", "Fit every detected formation in the tactical view.", FitEncounter));
        var menu = VisualUi.Button("Menu", "Pause combat and open the campaign menu.",
            () => MenuRequested?.Invoke(), VisualIconLibrary.NavMenu);
        menu.Name = "TacticalCampaignMenu";
        topRow.AddChild(menu);

        var commandPanel = new PanelContainer { Name = "TacticalOrders", MouseFilter = MouseFilterEnum.Stop };
        commandPanel.SetAnchorsPreset(LayoutPreset.BottomWide); commandPanel.OffsetTop = -116;
        commandPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface()); AddChild(commandPanel);
        var commandColumn = new VBoxContainer(); commandPanel.AddChild(commandColumn);
        _selectionSummary = VisualUi.Text("Select an owned formation", 13, VisualPalette.TextSecondary, true);
        commandColumn.AddChild(_selectionSummary);
        _orders = new HFlowContainer(); _orders.AddThemeConstantOverride("h_separation", 7); commandColumn.AddChild(_orders);
        AddOrderButton("Hold", MassiveCombatOrderType.Hold, false);
        AddOrderButton("Defend", MassiveCombatOrderType.Defend, false);
        AddOrderButton("Advance", MassiveCombatOrderType.Advance, true);
        AddOrderButton("Focus fire", MassiveCombatOrderType.FocusFire, true);
        AddOrderButton("Flank left", MassiveCombatOrderType.FlankLeft, true);
        AddOrderButton("Flank right", MassiveCombatOrderType.FlankRight, true);
        AddOrderButton("Intercept", MassiveCombatOrderType.Intercept, true);
        AddOrderButton("Break contact", MassiveCombatOrderType.BreakContact, false);
        AddOrderButton("Retreat", MassiveCombatOrderType.Retreat, false);

        _status = VisualUi.Text("", 12, VisualPalette.TextSecondary, true);
        _status.Name = "TacticalStatus"; _status.SetAnchorsPreset(LayoutPreset.BottomWide);
        _status.OffsetLeft = 18; _status.OffsetRight = -18; _status.OffsetTop = -145; _status.OffsetBottom = -120;
        _status.HorizontalAlignment = HorizontalAlignment.Center; AddChild(_status);
    }

    private void ToggleTacticalPause()
    {
        var paused = _tacticalSpeed <= 0;
        TacticalSpeedRequested?.Invoke(paused ? _tacticalSelectedSpeed : 0);
    }

    private void CycleTacticalSpeed()
    {
        var current = _tacticalSpeed > 0 ? _tacticalSpeed : _tacticalSelectedSpeed;
        _tacticalSelectedSpeed = current switch { .25 => .5, .5 => 1, 1 => 2, 2 => 4, _ => .25 };
        if (_tacticalSpeed > 0) TacticalSpeedRequested?.Invoke(_tacticalSelectedSpeed);
        else TacticalResumeSpeedRequested?.Invoke(_tacticalSelectedSpeed);
        RefreshTacticalControls();
    }

    private void RefreshTacticalControls()
    {
        if (_tacticalPlay is null) return;
        var selected = _tacticalSpeed > 0 ? _tacticalSpeed : _tacticalSelectedSpeed;
        _tacticalPlay.Text = _tacticalSpeed > 0 ? "Ⅱ" : "▶";
        _tacticalSpeedButton.Text = $"› {selected:0.##}×";
        _tacticalPlay.Modulate = _tacticalSpeed > 0 ? Colors.White : VisualUi.Gold;
        _tacticalSpeedButton.Modulate = _tacticalSpeed > 0 ? Colors.White : VisualUi.Gold;
    }

    private void AddOrderButton(string label, MassiveCombatOrderType type, bool needsTarget)
    {
        var button = VisualUi.Button(label, needsTarget ? $"Choose a target or objective for {label.ToLowerInvariant()}." : $"Order selected formations to {label.ToLowerInvariant()}.",
            () => BeginOrder(type, needsTarget));
        button.Name = "TacticalOrder" + type;
        _orders.AddChild(button);
    }

    private void BeginOrder(MassiveCombatOrderType type, bool needsTarget)
    {
        var source = SelectedOwned().FirstOrDefault();
        if (source == 0) { SetStatus("Select one or more friendly formations first.", true); return; }
        if (needsTarget)
        {
            _targetingSource = source;
            _targetingOrder = type;
            SetStatus($"{FriendlyOrderName(type)}: choose a formation or open-space objective.");
            return;
        }
        foreach (var id in SelectedOwned()) Issue(new MassiveCombatOrder(id, type));
    }

    private void IssueContextOrder(Vector2 screen)
    {
        var selected = SelectedOwned().ToArray();
        if (selected.Length == 0) { SetStatus("Select a friendly formation before issuing an order.", true); return; }
        var hit = HitFormation(screen);
        foreach (var source in selected)
        {
            if (hit is { } target && target != source)
                Issue(new MassiveCombatOrder(source, MassiveCombatOrderType.Engage, target));
            else
            {
                if (ToWorld(screen) is not { } objective)
                {
                    SetStatus("That point does not intersect the tactical plane.", true);
                    return;
                }
                Issue(new MassiveCombatOrder(source, MassiveCombatOrderType.Advance, Objective: objective));
            }
        }
    }

    private void CompleteTargetedOrder(Vector2 screen)
    {
        if (_targetingSource is not { } source) return;
        var hit = HitFormation(screen);
        MassiveCombatOrder order;
        if (hit is { } target && target != source)
            order = new MassiveCombatOrder(source, _targetingOrder, target);
        else
        {
            if (ToWorld(screen) is not { } objective)
            {
                SetStatus("That point does not intersect the tactical plane.", true);
                return;
            }
            order = new MassiveCombatOrder(source, _targetingOrder, Objective: objective);
        }
        Issue(order);
        _targetingSource = null;
    }

    private void Issue(MassiveCombatOrder order)
    {
        if (OrderRequested is null) { SetStatus("Tactical command link is unavailable.", true); return; }
        var result = OrderRequested(order);
        SetStatus(result.Message, !result.Accepted);
    }

    private void HandleLeftButton(InputEventMouseButton input)
    {
        if (input.Pressed)
        {
            _pointerDown = _selectionEnd = input.Position;
            _boxSelecting = false;
            return;
        }
        if (_targetingSource.HasValue) { CompleteTargetedOrder(input.Position); return; }
        if (_boxSelecting)
        {
            var box = RectFromPoints(_pointerDown, input.Position);
            if (!input.CtrlPressed && !input.ShiftPressed) _selection.Clear();
            foreach (var formation in _snapshot!.Formations.Where(IsOwned))
                if (box.HasPoint(ToScreen(formation.Position))) _selection.Add(formation.FormationId);
        }
        else
        {
            var hit = HitFormation(input.Position);
            if (!input.CtrlPressed && !input.ShiftPressed) _selection.Clear();
            if (hit is { } id && IsOwned(id))
            {
                if ((input.CtrlPressed || input.ShiftPressed) && _selection.Contains(id)) _selection.Remove(id);
                else _selection.Add(id);
            }
            if (input.DoubleClick && hit is { } focusId && Find(focusId) is { } focus && _systemSnapshot is not null)
                _systemBattleScene.FocusCombatPosition(new Vector2(focus.Position.X, focus.Position.Y));
        }
        _boxSelecting = false;
        RefreshSelectionSummary();
        RefreshPool(); QueueRedraw();
    }

    private void HandleMotion(InputEventMouseMotion motion)
    {
        if (_panning && (motion.ButtonMask & MouseButtonMask.Middle) != 0)
        {
            if (_systemSnapshot is not null)
            {
                if (motion.ShiftPressed) _systemBattleScene.Rotate(motion.Relative);
                else _systemBattleScene.Pan(motion.Relative);
            }
            else _cameraCenter += motion.Relative;
            _lastPointer = motion.Position; RefreshPool(); QueueRedraw(); return;
        }
        if ((motion.ButtonMask & MouseButtonMask.Left) != 0)
        {
            _selectionEnd = motion.Position;
            _boxSelecting |= motion.Position.DistanceTo(_pointerDown) >= 6;
            QueueRedraw(); return;
        }
        _hoveredFormation = HitFormation(motion.Position);
        MouseDefaultCursorShape = _hoveredFormation.HasValue ? CursorShape.PointingHand : CursorShape.Arrow;
        QueueRedraw();
    }

    private void ZoomAt(Vector2 point, float factor)
    {
        if (_systemSnapshot is not null)
        {
            _systemBattleScene.Zoom(factor, point);
            QueueRedraw();
            return;
        }
        var before = ToWorld(point)!.Value;
        _zoom = Math.Clamp(_zoom * factor, MinimumZoom, MaximumZoom);
        var after = ToScreen(before);
        _cameraCenter += point - after;
        RefreshPool(); QueueRedraw();
    }

    private void FitEncounter()
    {
        if (_snapshot?.Formations.Count is not > 0) return;
        var minX = _snapshot.Formations.Min(x => x.Position.X);
        var maxX = _snapshot.Formations.Max(x => x.Position.X);
        var minY = _snapshot.Formations.Min(x => x.Position.Y);
        var maxY = _snapshot.Formations.Max(x => x.Position.Y);
        _worldCenter = new Vector2((minX + maxX) * .5f, (minY + maxY) * .5f);
        var available = new Vector2(Math.Max(320, Size.X - 120), Math.Max(240, Size.Y - 260));
        _zoom = Math.Clamp(Math.Min(available.X / Math.Max(220, maxX - minX), available.Y / Math.Max(180, maxY - minY)), MinimumZoom, MaximumZoom);
        _cameraCenter = new Vector2(Size.X * .5f, 74 + available.Y * .5f);
        _cameraInitialized = true;
        if (_systemSnapshot is not null) _systemBattleScene.FitCombat();
        RefreshPool(); QueueRedraw();
    }

    private void RefreshPresentation()
    {
        RefreshSelectionSummary(); RefreshPool(); QueueRedraw();
    }

    private void RefreshPool()
    {
        if (_snapshot is null || !IsInstanceValid(_formationPool)) return;
        _formationPool.Visible = _systemSnapshot is null;
        if (_systemSnapshot is not null)
        {
            _systemBattleScene.PresentCombat(_snapshot, _observerCivilizationId, _selection);
            return;
        }
        _formationPool.Populate(_snapshot.Formations, ToScreen, FormationColor, _zoom, _selection);
    }

    private void RefreshSelectionSummary()
    {
        if (_selectionSummary is null || _snapshot is null) return;
        var selected = _snapshot.Formations.Where(x => _selection.Contains(x.FormationId)).ToArray();
        _selectionSummary.Text = selected.Length == 0
            ? "Select or drag around friendly formations · right-click to engage or advance · middle-drag to pan · wheel to zoom"
            : $"{selected.Length} formation{(selected.Length == 1 ? "" : "s")} · {selected.Sum(x => x.ShipCountLow):N0}" +
              (selected.All(x => x.IsExact) ? " ships" : $"–{selected.Sum(x => x.ShipCountHigh):N0} estimated ships");
        if (selected.Length == 1 && selected[0].Cohorts.Count > 0)
        {
            var composition = string.Join(" · ", selected[0].Cohorts.Take(3).Select(cohort =>
                $"{cohort.DisplayClass} {cohort.CountLow:N0}" + (cohort.CountLow == cohort.CountHigh ? "" : $"–{cohort.CountHigh:N0}")));
            if (selected[0].Cohorts.Count > 3) composition += $" · +{selected[0].Cohorts.Count - 3} groups";
            _selectionSummary.Text += " · " + composition;
        }
        var friendly = _snapshot.Formations.Where(IsOwned).ToArray();
        var contacts = _snapshot.Formations.Count - friendly.Length;
        _battleSummary.Text = $"T+{_snapshot.SimulatedSeconds:N1}s  ·  {_snapshot.ExactOwnShips:N0} friendly ships  ·  {contacts:N0} detected hostile formation{(contacts == 1 ? "" : "s")}";
    }

    private IEnumerable<long> SelectedOwned() => _selection.Where(IsOwned).OrderBy(id => id);
    private bool IsOwned(long id) => _snapshot?.Formations.Any(x => x.FormationId == id && IsOwned(x)) == true;
    private bool IsOwned(MassiveObservedFormation formation) => formation.CivilizationId == _observerCivilizationId;

    private void IngestEvents(IReadOnlyList<MassiveObservedCombatEvent> events)
    {
        foreach (var value in events.Where(x => x.Sequence > _latestEventSequence).OrderBy(x => x.Sequence))
        {
            _latestEventSequence = Math.Max(_latestEventSequence, value.Sequence);
            if (!value.DetailsKnown || value.Position is not { } position) continue;
            var source = value.ActorFormationId.HasValue ? Find(value.ActorFormationId.Value)?.Position : null;
            var target = value.TargetFormationId.HasValue ? Find(value.TargetFormationId.Value)?.Position : null;
            var start = source ?? position.Vector;
            var end = target ?? position.Vector;
            var lifetime = value.Type switch
            {
                MassiveCombatEventType.BeamVolley => .42f,
                MassiveCombatEventType.KineticVolley => .7f,
                MassiveCombatEventType.MissileSalvo => 2.2f,
                MassiveCombatEventType.MissileIntercepted => .8f,
                MassiveCombatEventType.Damage => .65f,
                _ => .9f,
            };
            _visualEvents.Add(new(value.Type, start, end, 0, lifetime, value.Magnitude ?? 1));
        }
        if (_visualEvents.Count > 192) _visualEvents.RemoveRange(0, _visualEvents.Count - 192);
    }

    private MassiveObservedFormation? Find(long id) => _snapshot?.Formations.FirstOrDefault(x => x.FormationId == id);

    private void DrawGrid(Vector2 viewport)
    {
        var spacing = Math.Clamp(80 * _zoom, 42, 132);
        var offset = new Vector2(PosMod(_cameraCenter.X, spacing), PosMod(_cameraCenter.Y, spacing));
        var color = new Color(.12f, .24f, .31f, .18f);
        for (var x = offset.X; x < viewport.X; x += spacing) DrawLine(new Vector2(x, 74), new Vector2(x, viewport.Y - 116), color);
        for (var y = Math.Max(74, offset.Y); y < viewport.Y - 116; y += spacing) DrawLine(new Vector2(0, y), new Vector2(viewport.X, y), color);
    }

    private void DrawInterdictionFields()
    {
        foreach (var formation in _snapshot!.Formations.Where(x => x.IsInterdicting &&
                         (_systemSnapshot is null || _selection.Contains(x.FormationId) || _hoveredFormation == x.FormationId))
                     .OrderByDescending(x => _selection.Contains(x.FormationId) || _hoveredFormation == x.FormationId)
                     .ThenBy(x => x.FormationId).Take(_systemSnapshot is null ? 32 : 2))
        {
            var center = ToScreen(formation.Position);
            var radius = Math.Clamp(78 * MathF.Sqrt(Math.Max(.25f, _zoom)), 52, 170);
            DrawCircle(center, radius, new Color(FormationColor(formation), .025f));
            DrawArc(center, radius, 0, Mathf.Tau, 64, new Color(FormationColor(formation), .28f), 1.25f);
        }
    }

    private void DrawFormationGuides()
    {
        foreach (var formation in _snapshot!.Formations)
        {
            var center = ToScreen(formation.Position);
            var color = FormationColor(formation);
            if (_selection.Contains(formation.FormationId))
            {
                DrawArc(center, 25, 0, Mathf.Tau, 32, VisualPalette.Selected, 2.5f);
                var future = ToScreen(formation.Position + formation.Velocity * 10);
                DrawDashedLine(center, future, new Color(VisualPalette.Selected, .7f), 2, 7);
            }
            if (_hoveredFormation == formation.FormationId)
                DrawArc(center, 31, 0, Mathf.Tau, 32, new Color(color, .9f), 2);
            if (formation.IsWarpBlocked) DrawArc(center, 35, -.75f, 3.9f, 26, VisualPalette.Danger, 3);
            if (formation.WarpSpoolProgress > 0)
                DrawArc(center, 39, -Mathf.Pi / 2, -Mathf.Pi / 2 + Mathf.Tau * formation.WarpSpoolProgress, 32, VisualPalette.Focus, 3);
        }
    }

    private void DrawImportantVessels()
    {
        if (_zoom < .72f) return;
        var drawn = 0;
        foreach (var formation in _snapshot!.Formations)
        {
            var center = ToScreen(formation.Position);
            if (center.X < -90 || center.Y < 40 || center.X > Size.X + 90 || center.Y > Size.Y - 70) continue;
            var color = FormationColor(formation);
            var visible = Math.Min(formation.ImportantVessels.Count, _zoom > 2 ? 20 : 8);
            for (var index = 0; index < visible && drawn < 320; index++, drawn++)
            {
                var vessel = formation.ImportantVessels[index];
                var angle = index * 2.399963f;
                var radius = 28 + 8 * MathF.Sqrt(index);
                var point = center + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * radius;
                var size = vessel.IsFlagship ? 8f : vessel.IsCarrier || vessel.IsInterdictor ? 6.5f : 5f;
                var vesselColor = vessel.IsCriticallyDamaged ? VisualPalette.Danger : color.Lightened(.18f);
                var diamond = new[] { point + new Vector2(size, 0), point + new Vector2(0, size * .7f), point - new Vector2(size, 0), point - new Vector2(0, size * .7f) };
                DrawColoredPolygon(diamond, vesselColor);
                if (vessel.IsFlagship) DrawArc(point, size + 3, 0, Mathf.Tau, 16, VisualPalette.Caution, 1.5f);
                if (vessel.IsInterdictor) DrawArc(point, size + 5, 0, Mathf.Tau, 16, new Color(color, .55f), 1);
            }
            if (drawn >= 320) break;
        }
    }

    private void DrawWeaponEffects()
    {
        foreach (var effect in _visualEvents)
        {
            var alpha = Math.Clamp(1 - effect.Age / effect.Lifetime, 0, 1);
            var start = ToScreen(effect.Start); var end = ToScreen(effect.End);
            switch (effect.Type)
            {
                case MassiveCombatEventType.BeamVolley:
                    DrawLine(start, end, new Color(.35f, .9f, 1, alpha), 1.5f + Math.Min(3, effect.Magnitude / 60f));
                    DrawCircle(end, 3 + effect.Magnitude / 80f, new Color(.8f, .95f, 1, alpha)); break;
                case MassiveCombatEventType.KineticVolley:
                    DrawDashedLine(start, end, new Color(1, .78f, .35f, alpha), 2, 5); break;
                case MassiveCombatEventType.MissileSalvo:
                    var progress = Mathf.SmoothStep(0, 1, effect.Age / effect.Lifetime);
                    var missile = start.Lerp(end, progress);
                    DrawLine(start.Lerp(missile, .35f), missile, new Color(1, .42f, .18f, alpha), 2.4f);
                    DrawCircle(missile, 3.2f, new Color(1, .88f, .55f, alpha)); break;
                case MassiveCombatEventType.MissileIntercepted:
                    DrawArc(end, 6 + effect.Age * 18, 0, Mathf.Tau, 24, new Color(.35f, .85f, 1, alpha), 2); break;
                case MassiveCombatEventType.Damage:
                    DrawCircle(end, 5 + effect.Age * 22, new Color(1, .22f, .12f, alpha * .18f));
                    DrawArc(end, 5 + effect.Age * 22, 0, Mathf.Tau, 24, new Color(1, .58f, .22f, alpha), 2); break;
            }
        }
    }

    private void DrawFormationLabels()
    {
        var labelBudget = _systemSnapshot is not null ? 8 : _zoom < .5f ? 48 : _zoom < 1.1f ? 120 : 360;
        var labelled = 0;
        var occupied = new List<Rect2>(Math.Min(labelBudget, 64));
        foreach (var formation in _snapshot!.Formations.OrderByDescending(x => _selection.Contains(x.FormationId) || _hoveredFormation == x.FormationId))
        {
            var center = ToScreen(formation.Position);
            if (center.X < -100 || center.Y < 50 || center.X > Size.X + 100 || center.Y > Size.Y - 80) continue;
            var priority = _selection.Contains(formation.FormationId) || _hoveredFormation == formation.FormationId;
            if (!priority && labelled >= labelBudget) continue;
            var labelOffset = IsOwned(formation) ? 19f : -229f;
            var labelBounds = new Rect2(center + new Vector2(labelOffset - 3, -17), new Vector2(220, _hoveredFormation == formation.FormationId ? 48 : 34));
            if (!priority && occupied.Any(existing => existing.Grow(4).Intersects(labelBounds))) continue;
            occupied.Add(labelBounds);
            labelled++;
            var color = FormationColor(formation);
            var count = formation.IsExact ? formation.ShipCountLow.ToString("N0") : $"{formation.ShipCountLow:N0}–{formation.ShipCountHigh:N0}";
            DrawString(_font, center + new Vector2(labelOffset, -5), formation.DisplayName, HorizontalAlignment.Left, 210, 12, VisualPalette.TextPrimary);
            DrawString(_font, center + new Vector2(labelOffset, 11), $"{count} ships · {FormationState(formation)}", HorizontalAlignment.Left, 220, 10, color);
            if (_hoveredFormation == formation.FormationId)
            {
                var strength = formation.StrengthLow.HasValue
                    ? formation.StrengthLow == formation.StrengthHigh ? $"Power {formation.StrengthLow:N0}" : $"Power {formation.StrengthLow:N0}–{formation.StrengthHigh:N0}"
                    : "Power unknown";
                DrawString(_font, center + new Vector2(labelOffset, 27), strength, HorizontalAlignment.Left, 220, 10, VisualPalette.TextSecondary);
            }
        }
    }

    private void DrawScale(Vector2 viewport)
    {
        var kilometres = 100f;
        var pixels = _systemSnapshot is not null ? _systemBattleScene.CombatScalePixels(kilometres) ?? 0 : 110;
        if (_systemSnapshot is not null)
        {
            while (pixels > 180 && kilometres > 12.5f) { kilometres *= .5f; pixels = _systemBattleScene.CombatScalePixels(kilometres) ?? 0; }
            while (pixels < 55 && kilometres < 1600) { kilometres *= 2; pixels = _systemBattleScene.CombatScalePixels(kilometres) ?? 0; }
            if (pixels <= 1) return;
        }
        else kilometres = pixels / Math.Max(_zoom, .0001f);
        var y = viewport.Y - 128;
        DrawLine(new Vector2(18, y), new Vector2(18 + pixels, y), VisualPalette.TextSecondary, 2);
        DrawLine(new Vector2(18, y - 4), new Vector2(18, y + 4), VisualPalette.TextSecondary, 2);
        DrawLine(new Vector2(18 + pixels, y - 4), new Vector2(18 + pixels, y + 4), VisualPalette.TextSecondary, 2);
        DrawString(_font, new Vector2(18, y - 8), $"{kilometres:N0} km", HorizontalAlignment.Left, pixels, 10, VisualPalette.TextSecondary);
    }

    private Color FormationColor(MassiveObservedFormation formation)
    {
        if (IsOwned(formation)) return VisualPalette.Success;
        if (!formation.IsExact && !formation.StrengthLow.HasValue) return VisualPalette.Unknown;
        var hue = ((formation.CivilizationId * 67) % 31) / 310f;
        return new Color(1f, .25f + hue, .22f + hue * .45f);
    }

    private long? HitFormation(Vector2 point)
    {
        if (_snapshot is null) return null;
        return _snapshot.Formations
            .Select(formation => (formation.FormationId, Distance: point.DistanceSquaredTo(ToScreen(formation.Position))))
            .Where(value => value.Distance <= SelectionRadius * SelectionRadius)
            .OrderBy(value => value.Distance).ThenBy(value => value.FormationId)
            .Select(value => (long?)value.FormationId).FirstOrDefault();
    }

    private Vector2 ToScreen(NumericsVector2 value) => _systemSnapshot is not null
        ? _systemBattleScene.ProjectCombatPosition(new Vector2(value.X, value.Y)) ?? new Vector2(-10000, -10000)
        : _cameraCenter + (new Vector2(value.X, value.Y) - _worldCenter) * _zoom;
    private NumericsVector2? ToWorld(Vector2 value)
    {
        if (_systemSnapshot is not null)
            return _systemBattleScene.UnprojectCombatPosition(value) is { } combat
                ? new NumericsVector2(combat.X, combat.Y) : null;
        var point = _worldCenter + (value - _cameraCenter) / Math.Max(_zoom, .0001f);
        return new NumericsVector2(point.X, point.Y);
    }

    private static Rect2 RectFromPoints(Vector2 first, Vector2 second) => new(first.Min(second), (second - first).Abs());
    private static float PosMod(float value, float divisor) => (value % divisor + divisor) % divisor;
    private static string FriendlyOrderName(MassiveCombatOrderType type) => type.ToString().Replace("Cautiously", " cautiously", StringComparison.Ordinal).Replace("Fire", " fire", StringComparison.Ordinal).ToLower(CultureInfo.InvariantCulture);
    private static double? TacticalSpeedFor(Key key) => key switch
    {
        Key.Key1 => .25, Key.Key2 => .5, Key.Key3 => 1, Key.Key4 => 2, Key.Key5 => 4, _ => null,
    };
    private static string FormationState(MassiveObservedFormation value) => value.IsWarpBlocked ? "WARP BLOCKED" : value.WarpSpoolProgress > 0 ? $"WARP {value.WarpSpoolProgress:P0}" : value.Shape.ToString().ToUpperInvariant();
    private void SetStatus(string message, bool error = false)
    {
        _status.Text = message;
        _status.AddThemeColorOverride("font_color", error ? VisualPalette.Danger : VisualPalette.TextSecondary);
    }

    private readonly record struct VisualEvent(MassiveCombatEventType Type, NumericsVector2 Start, NumericsVector2 End, float Age, float Lifetime, int Magnitude);
}
