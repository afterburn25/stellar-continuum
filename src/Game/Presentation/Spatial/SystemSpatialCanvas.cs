using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>
/// Observer-safe orbital presentation. Surface textures are deterministic cosmetic illustrations
/// of confirmed broad classes, cached on snapshot changes; they are not new simulation facts.
/// </summary>
public partial class SystemSpatialCanvas : Control
{
    // Rendering conversion only: authoritative local transit stays normalized at .82.
    // This maps that gate outside the final orbital ring with a visible body/label margin.
    internal const float ChartRenderRadiusFactor = 1.65f;
    private static readonly Color CanvasColor = new(0.012f, 0.025f, 0.044f);
    private static readonly Color KeylineColor = new(0.22f, 0.36f, 0.48f);
    private static readonly Color PrimaryTextColor = new(0.90f, 0.95f, 0.98f);
    private static readonly Color SecondaryTextColor = new(0.66f, 0.76f, 0.84f);
    private static readonly Color MutedTextColor = new(0.40f, 0.52f, 0.62f);
    private static readonly Color SelectedColor = new(0.35f, 0.81f, 0.98f);
    private static readonly Color UnknownColor = new(0.55f, 0.59f, 0.66f);
    private static readonly Color ResourceColor = new(0.91f, 0.71f, 0.36f);
    private static readonly Color AnomalyColor = new(0.69f, 0.56f, 1.0f);
    private static readonly Color ActivityColor = new(0.37f, 0.82f, 0.75f);

    private SystemSpatialSnapshot? _snapshot;
    private IReadOnlyDictionary<int, SystemSpatialBodyMarker> _bodiesById = new Dictionary<int, SystemSpatialBodyMarker>();
    private readonly Dictionary<int, (SystemSpatialBodyMarker Marker, ImageTexture Texture)> _surfaces = new();
    private readonly Dictionary<int, FocusedPlanetView> _orbitalDiscs = new();
    private readonly Dictionary<string, OrbitalStructureView> _orbitalStructures = new();
    private SystemScene3D _scene = null!;
    private TextureRect? _stellarDisc;
    private SystemSkyBackdrop? _sky;
    public int? BackgroundSystemId => _sky?.SystemId;
    private Font _font = null!;
    private Vector2 _lastViewportSize;
    private int? _hoveredBodyId;
    private int? _hoveredLaneDestinationId;
    private int? _selectedBodyId;
    private float _drawOpacity = 1;

    public event Action? ReturnRequested;
    public event Action<int>? BodyOrderRequested;
    public event Action<int>? DescentRequested;
    public Func<bool>? IsObjectInspectorOpen { get; set; }
    public event Action<string>? InfrastructureRequested;

    public int? SelectedBodyId => _selectedBodyId;
    public int? HoveredBodyId => IsPlanetFocused ? _focusedBodyId : _hoveredBodyId;
    internal int? HoveredLaneDestinationId => _hoveredLaneDestinationId;
    public IReadOnlyList<SystemSpatialInfrastructureMarker> VisibleInfrastructure =>
        _snapshot?.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>();
    public string? GetBodyLabel(int bodyId) => _bodiesById.TryGetValue(bodyId, out var body) ? body.Label : null;
    internal SystemSpatialBodyMarker? GetBodyMarker(int bodyId) =>
        _bodiesById.TryGetValue(bodyId, out var body) ? body : null;
    internal SystemScene3D Scene => _scene;
    public Vector2? GetBodyScreenPosition(int bodyId)
    {
        if (_snapshot is null || !_bodiesById.TryGetValue(bodyId, out var body)) return null;
        if (IsPlanetFocused) return _scene.ProjectBody(bodyId);
        var layout = CurrentViewport;
        return ToScreen(body, new Vector2(layout.CenterX, layout.CenterY), layout.Scale);
    }
    public Vector2? GetInfrastructureScreenPosition(string projectId)
    {
        if (_snapshot?.Infrastructure is not { } infrastructure) return null;
        if (IsPlanetFocused) return _scene.ProjectInfrastructure(projectId);
        var index = infrastructure.ToList().FindIndex(item => item.ProjectId == projectId);
        if (index < 0) return null;
        var layout = CurrentViewport;
        return InfrastructurePosition(new Vector2(layout.CenterX, layout.CenterY), layout.Scale, index);
    }

    public override void _Ready()
    {
        _font = ThemeDB.FallbackFont;
        MouseFilter = MouseFilterEnum.Stop;
        FocusMode = FocusModeEnum.None;
        MouseExited += ClearHover;
        _scene = new SystemScene3D { Name = "SystemScene3D", ZIndex = -1, Visible = false };
        AddChild(_scene);
        ResizeToViewport();
    }

    public override void _Process(double delta)
    {
        if (GetViewportRect().Size != _lastViewportSize)
        {
            var shift = (GetViewportRect().Size - _lastViewportSize) * 0.5f;
            if (_cameraReady)
                _camera.Translate(shift.X, shift.Y);
            ResizeToViewport();
            QueueRedraw();
        }
        if (_snapshot is null) return;
        EnsureOrbitalCamera();
        if (IsNavigationBlocked?.Invoke() == true)
        {
            _systemPanning = false;
            _leftPanCandidate = false;
            _leftPanMoved = false;
            _rotating = false;
            return;
        }
        if (IsDetailedFocus)
        {
            _scene.Advance(delta);
            if (!_descentRequested && _focusedBodyId is int focused &&
                _scene.FocusAltitudeRatio < .12f && CanOpenSurface?.Invoke(focused) == true)
            {
                _descentRequested = true;
                DescentRequested?.Invoke(focused);
            }
        }
        else if (_camera.Advance(delta)) QueueRedraw();
        UpdatePlanetInspector();
        UpdateLocalFleets();
    }

    public override void _GuiInput(InputEvent @event)
    {
        if (IsNavigationBlocked?.Invoke() == true) { AcceptEvent(); return; }
        if (_snapshot is not null)
        {
            var layout = CurrentViewport;
            if (@event is InputEventMouseMotion motion)
            {
                if (IsDetailedFocus)
                {
                    _rotating &= (motion.ButtonMask & MouseButtonMask.Middle) != 0;
                    _leftPanCandidate &= (motion.ButtonMask & MouseButtonMask.Left) != 0;
                    if (_rotating) _scene.Rotate(motion.Relative);
                    else if (_leftPanCandidate)
                    {
                        _leftPanMoved |= motion.Position.DistanceTo(_leftPanStart) >= 5;
                        if (_leftPanMoved) _scene.Pan(motion.Relative);
                    }
                    else _hoveredBodyId = IsPlanetFocused ? _scene.HitBody(motion.Position) : null;
                    MouseDefaultCursorShape = _hoveredBodyId.HasValue ? CursorShape.PointingHand : CursorShape.Arrow;
                    QueueRedraw();
                    AcceptEvent();
                    return;
                }
                // Release can occur over another control or while restoring the window.
                // Never keep panning after the native mouse button is no longer held.
                _leftPanCandidate &= (motion.ButtonMask & MouseButtonMask.Left) != 0;
                _systemPanning &= (motion.ButtonMask & MouseButtonMask.Middle) != 0;
                if (_leftPanCandidate && !IsDetailedFocus)
                {
                    if (!_leftPanMoved && motion.Position.DistanceTo(_leftPanStart) >= 5)
                        _leftPanMoved = true;
                    if (_leftPanMoved)
                    {
                        _camera.Pan(motion.Relative.X, motion.Relative.Y);
                        QueueRedraw();
                        AcceptEvent();
                        return;
                    }
                }
                if (_systemPanning && !IsDetailedFocus)
                {
                    _camera.Pan(motion.Relative.X, motion.Relative.Y);
                    QueueRedraw();
                    AcceptEvent();
                    return;
                }
                var laneHit = HitLane(motion.Position);
                var laneHover = laneHit?.DestinationSystemId;
                var hovered = laneHover.HasValue ? null : layout.HitBody(_snapshot, motion.Position.X, motion.Position.Y);
                if (hovered != _hoveredBodyId || laneHover != _hoveredLaneDestinationId)
                {
                    _hoveredBodyId = hovered;
                    _hoveredLaneDestinationId = laneHover;
                    TooltipText = laneHit is null ? string.Empty : laneHit.IsKnown ? laneHit.Label : "????";
                    MouseDefaultCursorShape = hovered.HasValue || laneHover.HasValue ? CursorShape.PointingHand : CursorShape.Arrow;
                    QueueRedraw();
                }
            }
            if (@event is InputEventMouseButton gesture)
            {
                if (gesture.ButtonIndex == MouseButton.Middle)
                {
                    _systemPanning = gesture.Pressed && !IsDetailedFocus;
                    _rotating = gesture.Pressed && IsDetailedFocus;
                }
                if (gesture.ButtonIndex == MouseButton.Left && !gesture.DoubleClick)
                {
                    if (gesture.Pressed)
                    {
                        _leftPanCandidate = true;
                        _leftPanMoved = false;
                        _leftPanStart = gesture.Position;
                    }
                    else if (!gesture.Pressed && _leftPanCandidate)
                    {
                        if (!_leftPanMoved) HandleLeftClick(gesture.Position, false);
                        _leftPanCandidate = false;
                        _leftPanMoved = false;
                    }
                }
                if (gesture.Pressed && gesture.ButtonIndex is MouseButton.WheelUp or MouseButton.WheelDown)
                    ZoomAt(gesture.ButtonIndex == MouseButton.WheelUp ? 1.22f : 1f / 1.22f, gesture.Position);
            }
            if (@event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Right } orderMouse)
            {
                var bodyId = IsPlanetFocused ? _scene.HitBody(orderMouse.Position) : IsDetailedFocus ? null : CurrentViewport.HitBody(_snapshot, orderMouse.Position.X, orderMouse.Position.Y);
                if (bodyId.HasValue) BodyOrderRequested?.Invoke(bodyId.Value);
            }
            if (@event is InputEventMouseButton mouse && mouse.Pressed && mouse.ButtonIndex == MouseButton.Left && mouse.DoubleClick)
            {
                _leftPanCandidate = false;
                _leftPanMoved = false;
                HandleLeftClick(mouse.Position, true);
            }
        }
        // The canvas owns system-space pointer input. Higher CanvasLayer controls retain their
        // events; hidden regional-map fleet orders must never fire through the orbital view.
        AcceptEvent();
    }

    private void HandleLeftClick(Vector2 position, bool doubleClick)
    {
        if (_snapshot is null) return;
        if (!IsDetailedFocus && HitLane(position) is { } lane)
        {
            LaneSelected?.Invoke(lane.DestinationSystemId);
            return;
        }
        if (!IsDetailedFocus && HitInfrastructure(position) is { } infrastructure)
        {
            InfrastructureRequested?.Invoke(infrastructure.ProjectId);
            return;
        }
        var layout = CurrentViewport;
        var hit = IsPlanetFocused ? _scene.HitBody(position) : IsDetailedFocus ? null : layout.HitBody(_snapshot, position.X, position.Y);
        if (IsDetailedFocus)
        {
            if (hit is int id)
            {
                _selectedBodyId = id;
                if (doubleClick && id != _focusedBodyId) FocusBody(id);
            }
            else if (_scene.HitInfrastructure(position) is string facility)
                InfrastructureRequested?.Invoke(facility);
            return;
        }
        _selectedBodyId = hit;
        QueueRedraw();
        if (!doubleClick) return;
        if (hit.HasValue) FocusSelectedBody();
        else if (layout.HitsStar(position.X, position.Y)) FocusStar();
        else if (!layout.HitsCelestialObject(_snapshot, position.X, position.Y)) ReturnRequested?.Invoke();
    }

    public void SetSnapshot(SystemSpatialSnapshot? snapshot)
    {
        if (snapshot?.SystemId != _snapshot?.SystemId || snapshot is null)
        {
            ResetSpatialCamera();
            _selectedBodyId = null;
            _hoveredBodyId = null;
            ClearSurfaces();
            foreach (var sprite in _orbitalDiscs.Values) sprite.QueueFree();
            _orbitalDiscs.Clear();
            foreach (var structure in _orbitalStructures.Values) structure.QueueFree();
            _orbitalStructures.Clear();
        }
        _snapshot = snapshot;
        _bodiesById = snapshot?.Bodies.ToDictionary(marker => marker.BodyId)
            ?? new Dictionary<int, SystemSpatialBodyMarker>();
        foreach (var staleId in _surfaces.Keys.Where(id => !_bodiesById.ContainsKey(id)).ToArray())
        {
            _surfaces[staleId].Texture.Dispose();
            _surfaces.Remove(staleId);
        }
        if (snapshot is not null)
        {
            foreach (var body in snapshot.Bodies)
            {
                if (_surfaces.TryGetValue(body.BodyId, out var existing) && existing.Marker == body)
                    continue;
                if (_surfaces.Remove(body.BodyId, out existing))
                    existing.Texture.Dispose();
                if (body.HasDetailedEnvironment && body.VisualClass is not SystemSpatialBodyVisualClass.UnknownPlanet and not SystemSpatialBodyVisualClass.UnknownMoon)
                    _surfaces.Add(body.BodyId, (body, CreateSurface(body)));
            }
        }
        if (_selectedBodyId.HasValue && !_bodiesById.ContainsKey(_selectedBodyId.Value))
            _selectedBodyId = null;
        if (_hoveredBodyId.HasValue && !_bodiesById.ContainsKey(_hoveredBodyId.Value))
            _hoveredBodyId = null;
        if (snapshot is not null) _scene.Present(snapshot);
        else _scene.Clear();
        if (_focusedBodyId is int focused)
        {
            if (!_bodiesById.ContainsKey(focused)) ResetSpatialCamera();
        }
        _scene.Visible = snapshot is not null && IsDetailedFocus;
        Visible = snapshot is not null;
        QueueRedraw();
    }

    public override void _ExitTree() => ClearSurfaces();

    public override void _Draw()
    {
        foreach (var structure in _orbitalStructures.Values) structure.Visible = false;
        if (_snapshot is null)
            return;
        var viewport = Size;
        DrawSpace(viewport);
        if (_sky is not null) _sky.Visible = !IsPlanetFocused;
        foreach (var sprite in _orbitalDiscs.Values) sprite.Visible = false;
        if (_stellarDisc is not null) _stellarDisc.Visible = false;
        var layout = CurrentViewport;
        var center = new Vector2(layout.CenterX, layout.CenterY);
        DrawHeader(_snapshot);
        if (IsPlanetFocused)
        {
            DrawFocusedWorldFacts();
            return;
        }
        if (IsFleetFocused)
        {
            DrawString(_font, new Vector2(124, 267), "FLEET LOCAL SPACE · WHEEL DOWN TO RETURN", HorizontalAlignment.Left, -1, 11, SelectedColor);
            return;
        }
        if (IsStarFocused)
        {
            DrawString(_font, new Vector2(124, 267), "STELLAR PHOTOSPHERE · WHEEL DOWN TO RETURN", HorizontalAlignment.Left, -1, 11, SelectedColor);
            return;
        }
        _drawOpacity = OrbitalContextOpacity;
        if (_drawOpacity > 0.001f)
        {
            DrawOrbits(_snapshot, center, layout.Scale);
            DrawStar(_snapshot, center, layout.Scale);
            DrawStellarCompanions(_snapshot, center, layout.Scale);
            DrawSystemBoundary(_snapshot, center, layout.Scale);
            DrawLocalLanes(_snapshot, center, layout.Scale);
            DrawInfrastructure(_snapshot, center, layout.Scale);
            // Retain the orbital context as the selected GPU disc approaches; restore it
            // along the same camera path on Back rather than switching whole layers at once.
            foreach (var body in _snapshot.Bodies)
                if (body.Kind != PlanetaryBodyKind.Moon)
                    DrawBody(body, center, layout);
            foreach (var body in _snapshot.Bodies)
                if (body.Kind == PlanetaryBodyKind.Moon && layout.IsBodyVisible(_snapshot, body))
                    DrawBody(body, center, layout);
        }
        _drawOpacity = 1;
        if (!IsPlanetFocused) DrawSelectionCaption(viewport);
    }

    private void DrawSpace(Vector2 size)
    {
        if (_sky is null)
        {
            // Both the native scene and this backdrop live behind the canvas annotations.
            // Keep the opaque sky one layer below the SubViewport presenter so it cannot
            // cover close planets or ships merely because it was created later.
            _sky = new SystemSkyBackdrop { Name = "LocalSystemSky", ShowBehindParent = true, ZIndex = -2 };
            AddChild(_sky);
        }
        _sky.Size = size;
        _sky.SetSystem(_snapshot!.SystemId);
    }

    private void DrawHeader(SystemSpatialSnapshot snapshot)
    {
        DrawRect(new Rect2(112.0f, 172.0f, 266.0f, 70.0f), WithAlpha(CanvasColor, .82f));
        DrawRect(new Rect2(112.0f, 172.0f, 266.0f, 70.0f), WithAlpha(KeylineColor, .52f), false, 1.0f);
        DrawLine(new Vector2(124.0f, 187.0f), new Vector2(148.0f, 187.0f), SelectedColor, 2.0f, true);
        DrawString(_font, new Vector2(158.0f, 192.0f), IsPlanetFocused ? "PLANET FOCUS" : IsFleetFocused ? "VESSEL FOCUS" : IsStarFocused ? "STELLAR FOCUS" : "ORBITAL SYSTEM", HorizontalAlignment.Left, -1, 10, SelectedColor);
        var title = IsPlanetFocused && _focusedBodyId is int focusedId && _bodiesById.TryGetValue(focusedId, out var focusedBody)
            ? focusedBody.Label
            : snapshot.CatalogName;
        DrawString(_font, new Vector2(124.0f, 218.0f), title, HorizontalAlignment.Left, -1, 24, PrimaryTextColor);
        var complete = snapshot.SurveyLevel == SystemSurveyLevel.FullySurveyed;
        DrawString(_font, new Vector2(124.0f, 235.0f), complete ? "SURVEY COMPLETE" : $"RECONNAISSANCE  ·  SURVEY {snapshot.SurveyProgress:P0}",
            HorizontalAlignment.Left, -1, 11, complete ? ActivityColor : UnknownColor);
    }

    private void DrawOrbits(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        foreach (var body in snapshot.Bodies)
        {
            var highlighted = body.BodyId == _selectedBodyId || body.BodyId == _hoveredBodyId;
            if (body.Kind != PlanetaryBodyKind.Moon)
            {
                var orbitColor = highlighted ? SelectedColor : KeylineColor;
                if (body.OrbitalEccentricity > 0 || body.OrbitalInclinationDegrees != 0)
                {
                    var points = new Vector2[193];
                    for (var i = 0; i < points.Length; i++)
                    {
                        var point = SystemOrbitGeometry.Point(body.OrbitRadius, body.OrbitalEccentricity,
                            body.OrbitalInclinationDegrees, i * Mathf.Tau / (points.Length - 1));
                        points[i] = center + new Vector2(point.X, point.Y) * scale;
                    }
                    DrawPolyline(points, WithAlpha(orbitColor, highlighted ? .55f : .32f),
                        highlighted ? 1.35f : .85f, true);
                    continue;
                }
                // A restrained inner trace breaks up uniform wire rings and gives the 2D
                // orbital chart depth without adding objects or obscuring pointer targets.
                DrawArc(center, body.OrbitRadius * scale, -2.26f, -.62f, 32,
                    WithAlpha(orbitColor, highlighted ? .22f : .075f), highlighted ? 2.2f : 1.55f, true);
                DrawCircle(center, body.OrbitRadius * scale, WithAlpha(orbitColor, highlighted ? 0.55f : 0.24f), false,
                    highlighted ? 1.35f : 0.85f, true);
                // A short periapsis tick supplies hierarchy without turning every orbit into a grid.
                var tick = Vector2.FromAngle(MathF.Atan2(body.OffsetY, body.OffsetX));
                var tickAt = center + tick * body.OrbitRadius * scale;
                DrawLine(tickAt - tick * 3.5f, tickAt + tick * 3.5f, WithAlpha(orbitColor, highlighted ? .75f : .34f), 1.0f, true);
                continue;
            }
            if (!new SystemSpatialViewport(center.X, center.Y, scale).IsBodyVisible(snapshot, body) || body.ParentBodyId is not int parentId || !_bodiesById.TryGetValue(parentId, out var parent))
                continue;
            DrawCircle(ToScreen(parent, center, scale), body.OrbitRadius * scale,
                WithAlpha(highlighted ? SelectedColor : KeylineColor, highlighted ? 0.48f : 0.16f), false, 0.7f, true);
        }
    }

    private void DrawStar(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        var radius = SystemCelestialScale.StarScreenRadius(scale);
        if (snapshot.StarArchetype is null)
        {
            // Reconnaissance establishes an orbital center, never an undiscovered stellar class.
            DrawCircle(center, radius + 8.0f, WithAlpha(UnknownColor, 0.045f));
            DrawCircle(center, radius, Fade(new Color(0.07f, 0.09f, 0.12f)));
            DrawCircle(center, radius, WithAlpha(UnknownColor, 0.78f), false, 1.5f, true);
            DrawArc(center, radius + 5.0f, -0.7f, 0.7f, 20, WithAlpha(UnknownColor, 0.40f), 1.0f, true);
            DrawString(_font, center + new Vector2(-4.0f, 5.0f), "?", HorizontalAlignment.Left, -1, 15, Fade(UnknownColor));
            return;
        }
        var isBlackHole = snapshot.StellarClass.HasValue
            ? snapshot.StellarClass == StellarPrimaryClass.BlackHole
            : snapshot.StarArchetype == StarArchetype.BlackHole;
        if (isBlackHole)
        {
            for (var glow = 9; glow > 0; glow--)
                DrawCircle(center, radius + glow * 2.2f, Fade(new Color(0.58f, 0.67f, 0.85f, 0.025f)));
            DrawCircle(center, radius + 2.0f, Fade(new Color(0.74f, 0.79f, 0.94f)));
            DrawCircle(center, radius, Fade(new Color(0.003f, 0.006f, 0.014f)));
            DrawArc(center, radius + 8.0f, -0.3f, 2.7f, 48, Fade(new Color(0.73f, 0.79f, 0.92f, 0.55f)), 2.0f, true);
            return;
        }
        var archetype = snapshot.StarArchetype.Value;
        var profile = snapshot.StellarClass switch
        {
            StellarPrimaryClass.MRedDwarf => (new Color("e96550"), .72f),
            StellarPrimaryClass.KOrangeDwarf => (new Color("ff9850"), .86f),
            StellarPrimaryClass.GYellowDwarf => (new Color("ffc66d"), 1.0f),
            StellarPrimaryClass.FYellowWhiteDwarf => (new Color("fff0c8"), 1.08f),
            StellarPrimaryClass.AWhiteStar => (new Color("e4f1ff"), 1.18f),
            StellarPrimaryClass.HotBlueStar => (new Color("84b8ff"), 1.44f),
            StellarPrimaryClass.Giant => (new Color("ff6e50"), 1.72f),
            StellarPrimaryClass.WhiteDwarf => (new Color("d4ebff"), .55f),
            StellarPrimaryClass.NeutronStar => (new Color("79d4ff"), .42f),
            StellarPrimaryClass.Pulsar => (new Color("67dcff"), .40f),
            StellarPrimaryClass.Protostar => (new Color("ffae61"), 1.36f),
            _ => archetype switch
            {
                StarArchetype.ResourceRich => (new Color("ff9c48"), .88f),
                StarArchetype.HabitableRich => (new Color("ffd982"), 1.04f),
                StarArchetype.BarrenFrontier => (new Color("e65f4e"), .72f),
                StarArchetype.Nebula => (new Color("8ecbff"), 1.08f),
                StarArchetype.NeutronPulsar => (new Color("8bd6ff"), .48f),
                StarArchetype.AncientRuin => (new Color("f0d39b"), .92f),
                StarArchetype.Dangerous => (new Color("ff5847"), 1.38f),
                StarArchetype.Legendary => (new Color("b8d9ff"), 1.62f),
                _ => (new Color("ffc66d"), 1.0f),
            },
        };
        var color = profile.Item1;
        radius *= profile.Item2;
        DrawTextureRect(CinematicArt.Glow, new Rect2(center - Vector2.One * radius * 4.8f,
            Vector2.One * radius * 9.6f), false, WithAlpha(color, .22f));
        if (archetype == StarArchetype.Nebula)
        {
            DrawCircle(center + new Vector2(-radius * .7f, radius * .18f), radius * 2.15f,
                WithAlpha(new Color("8957c7"), .055f));
            DrawCircle(center + new Vector2(radius * .65f, -radius * .25f), radius * 1.75f,
                WithAlpha(new Color("3d89b8"), .05f));
        }
        _stellarDisc ??= CreateStellarDisc();
        _stellarDisc.Visible = true;
        _stellarDisc.Material = CelestialBodyMaterials.GetStarMaterial(color);
        var extent = radius * CelestialBodyMaterials.StarExtentMultiplier;
        _stellarDisc.Position = center - Vector2.One * extent;
        _stellarDisc.Size = Vector2.One * extent * 2;
        _stellarDisc.Modulate = Fade(Colors.White);
        var isNeutron = snapshot.StellarClass.HasValue
            ? snapshot.StellarClass is StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar
            : archetype == StarArchetype.NeutronPulsar;
        if (isNeutron)
        {
            DrawLine(center + new Vector2(-radius * 4.8f, radius * 1.15f),
                center + new Vector2(radius * 4.8f, -radius * 1.15f), WithAlpha(color, .32f), 7, true);
            DrawLine(center + new Vector2(-radius * 6.4f, radius * 1.55f),
                center + new Vector2(radius * 6.4f, -radius * 1.55f), WithAlpha(color, .72f), 1.3f, true);
        }
        else if (archetype == StarArchetype.Dangerous)
        {
            for (var flare = 0; flare < 4; flare++)
                DrawArc(center, radius + 5 + flare * 3, -.8f + flare * 1.37f,
                    .25f + flare * 1.37f, 20, WithAlpha(new Color("ffb15b"), .62f), 2, true);
        }
        else if (archetype == StarArchetype.Legendary)
        {
            DrawLine(center + new Vector2(-radius * 2.8f, 0), center + new Vector2(radius * 2.8f, 0), WithAlpha(color, .24f), 2, true);
            DrawLine(center + new Vector2(0, -radius * 2.8f), center + new Vector2(0, radius * 2.8f), WithAlpha(color, .24f), 2, true);
        }
        else if (archetype == StarArchetype.AncientRuin)
        {
            DrawArc(center, radius + 11, -.4f, 4.6f, 36, WithAlpha(new Color("d8b06a"), .48f), 1.2f, true);
            for (var fragment = 0; fragment < 3; fragment++)
            {
                var position = center + Vector2.FromAngle(.5f + fragment * 1.7f) * (radius + 11);
                DrawRect(new Rect2(position - Vector2.One * 2, Vector2.One * 4), WithAlpha(new Color("e7d4b0"), .74f), true);
            }
        }
    }
    public Vector2? GetLaneScreenPosition(int destinationSystemId)
    {
        if (_snapshot is null || IsPlanetFocused) return null;
        var layout = CurrentViewport;
        return BuildLaneMarkerGeometries(new Vector2(layout.CenterX, layout.CenterY), layout.Scale)
            .FirstOrDefault(item => item.Lane.DestinationSystemId == destinationSystemId)?.Center;
    }
    internal Rect2? GetLaneMarkerBounds(int destinationSystemId)
    {
        if (_snapshot is null || IsPlanetFocused) return null;
        var layout = CurrentViewport;
        return BuildLaneMarkerGeometries(new Vector2(layout.CenterX, layout.CenterY), layout.Scale)
            .FirstOrDefault(item => item.Lane.DestinationSystemId == destinationSystemId)?.Bounds;
    }
    internal Vector2? GetLaneBodyColorSamplePosition(int destinationSystemId)
    {
        if (_snapshot is null || IsPlanetFocused) return null;
        var layout = CurrentViewport;
        return BuildLaneMarkerGeometries(new Vector2(layout.CenterX, layout.CenterY), layout.Scale)
            .FirstOrDefault(item => item.Lane.DestinationSystemId == destinationSystemId)?.ColorSample;
    }
    internal float SystemBoundaryScreenRadius => _snapshot is null ? 0f : BoundaryRadius(_snapshot, CurrentViewport.Scale);
    internal float? GetLaneMarkerBoundaryClearance(int destinationSystemId)
    {
        if (_snapshot is null || IsPlanetFocused) return null;
        var layout = CurrentViewport;
        var center = new Vector2(layout.CenterX, layout.CenterY);
        var marker = BuildLaneMarkerGeometries(center, layout.Scale)
            .FirstOrDefault(item => item.Lane.DestinationSystemId == destinationSystemId);
        if (marker is null) return null;
        var gate = (marker.BaseA + marker.BaseB) * .5f;
        var bodyClearance = gate.DistanceTo(center) - BoundaryRadius(_snapshot, layout.Scale);
        var labelClearance = marker.LabelCenter.DistanceTo(center) - marker.LabelHalfHeight -
            BoundaryRadius(_snapshot, layout.Scale);
        return MathF.Min(bodyClearance, labelClearance);
    }

    internal Vector2? GetLaneMarkerBodySize(int destinationSystemId)
    {
        if (_snapshot is null || IsPlanetFocused) return null;
        var layout = CurrentViewport;
        var marker = BuildLaneMarkerGeometries(new Vector2(layout.CenterX, layout.CenterY), layout.Scale)
            .FirstOrDefault(item => item.Lane.DestinationSystemId == destinationSystemId);
        return marker is null ? null : new Vector2(marker.BaseA.DistanceTo(marker.BaseB),
            ((marker.BaseA + marker.BaseB) * .5f).DistanceTo(marker.Apex));
    }
    public Vector2? GetStarScreenPosition() => _snapshot is null || IsPlanetFocused
        ? null : new Vector2(CurrentViewport.CenterX, CurrentViewport.CenterY);

    // Companion stars are shown only when the observer-safe snapshot contains persisted
    // detailed stellar classes. Their fixed offsets are schematic inner-system geometry;
    // selection remains on the one authoritative system, never on invented bodies.
    private void DrawStellarCompanions(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        if (!snapshot.StellarClass.HasValue || !snapshot.SecondaryStellarClass.HasValue) return;
        DrawStellarCompanion(center + new Vector2(850, -390) * Math.Max(.09f, scale),
            CompanionColor(snapshot.SecondaryStellarClass.Value), "B", Math.Max(12f, 196f * scale));
        if (snapshot.TertiaryStellarClass.HasValue)
            DrawStellarCompanion(center + new Vector2(-820, 480) * Math.Max(.09f, scale),
                CompanionColor(snapshot.TertiaryStellarClass.Value), "C", Math.Max(11f, 168f * scale));
    }

    private void DrawStellarCompanion(Vector2 position, Color color, string label, float radius)
    {
        DrawTextureRect(CinematicArt.Glow, new Rect2(position - Vector2.One * 24, Vector2.One * 48), false,
            WithAlpha(color, .34f));
        DrawCircle(position, radius, WithAlpha(color, .88f));
        DrawCircle(position, 2.4f, WithAlpha(new Color(1, .975f, .91f), .96f));
        DrawLine(position - new Vector2(13, 0), position + new Vector2(13, 0), WithAlpha(color, .42f), .75f, true);
        DrawString(_font, position + new Vector2(9, -8), label, HorizontalAlignment.Left, -1, 9, Fade(PrimaryTextColor));
    }

    private static Color CompanionColor(StellarPrimaryClass stellarClass) => stellarClass switch
    {
        StellarPrimaryClass.MRedDwarf => new Color("e96550"), StellarPrimaryClass.KOrangeDwarf => new Color("ff9850"),
        StellarPrimaryClass.GYellowDwarf => new Color("ffc66d"), StellarPrimaryClass.FYellowWhiteDwarf => new Color("fff0c8"),
        StellarPrimaryClass.AWhiteStar => new Color("e4f1ff"), StellarPrimaryClass.HotBlueStar => new Color("84b8ff"),
        StellarPrimaryClass.Giant => new Color("ff6e50"), StellarPrimaryClass.WhiteDwarf => new Color("d4ebff"),
        StellarPrimaryClass.NeutronStar => new Color("79d4ff"), StellarPrimaryClass.Pulsar => new Color("67dcff"),
        StellarPrimaryClass.Protostar => new Color("ffae61"),
        _ => new Color("d5d9d6"),
    };

    private void DrawInfrastructure(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        if (snapshot.Infrastructure is not { Count: > 0 } infrastructure) return;
        for (var index = 0; index < infrastructure.Count; index++)
        {
            var marker = infrastructure[index];
            var position = InfrastructurePosition(center, scale, index);
            var color = marker.State switch
            {
                SystemSpatialInfrastructureState.Complete => ActivityColor,
                SystemSpatialInfrastructureState.Active => SelectedColor,
                SystemSpatialInfrastructureState.Available => ResourceColor,
                _ => UnknownColor,
            };
            DrawLine(center + (position - center).Normalized() * 30.0f, position, WithAlpha(color, 0.24f), 1.0f, true);
            if (marker.State is SystemSpatialInfrastructureState.Complete or SystemSpatialInfrastructureState.Active)
            {
                if (!_orbitalStructures.TryGetValue(marker.ProjectId, out var structure))
                {
                    structure = new OrbitalStructureView { Name = "OrbitalStructure_" + marker.ProjectId, ZIndex = 12 };
                    AddChild(structure); _orbitalStructures.Add(marker.ProjectId, structure);
                }
                structure.Visible = true;
                var diameter = Math.Clamp(70 * scale, 44, 110);
                structure.Position = position - Vector2.One * diameter * .5f;
                structure.Size = Vector2.One * diameter;
                structure.Present(marker.ProjectId, marker.Progress);
            }
            else if (marker.ProjectId == "orbital_shipyard") DrawShipyard(position, color);
            else if (marker.ProjectId == "asteroid_resource_network") DrawResourceNetwork(position, color);
            else DrawLaunchComplex(position, color);
            if (marker.State == SystemSpatialInfrastructureState.Active)
                DrawArc(position, 13.0f, -MathF.PI / 2, -MathF.PI / 2 + MathF.Tau * (float)marker.Progress,
                    28, WithAlpha(color, 0.95f), 2.0f, true);
            var state = marker.State.ToString().ToUpperInvariant();
            var label = marker.Label.Replace("Orbital ", string.Empty, StringComparison.OrdinalIgnoreCase).ToUpperInvariant();
            DrawString(_font, position + new Vector2(17, -1), label, HorizontalAlignment.Left, -1, 9, Fade(PrimaryTextColor));
            DrawString(_font, position + new Vector2(17, 11), state, HorizontalAlignment.Left, -1, 8, Fade(color));
        }
    }

    private SystemSpatialInfrastructureMarker? HitInfrastructure(Vector2 point)
    {
        if (_snapshot?.Infrastructure is not { } infrastructure) return null;
        var layout = CurrentViewport;
        var center = new Vector2(layout.CenterX, layout.CenterY);
        for (var index = 0; index < infrastructure.Count; index++)
            if (point.DistanceTo(InfrastructurePosition(center, layout.Scale, index)) <= 25.0f)
                return infrastructure[index];
        return null;
    }

    private Vector2 InfrastructurePosition(Vector2 center, float scale, int index)
    {
        var marker = _snapshot?.Infrastructure?[index];
        if (marker?.HostBodyId is int hostId && _bodiesById.TryGetValue(hostId, out var host))
        {
            var hostPosition = ToScreen(host, center, scale);
            var hostRadius = CurrentViewport.BodyRadius(host);
            var bearing = marker.ProjectId == "orbital_shipyard" ? -1.8f : .9f;
            return hostPosition + new Vector2(MathF.Cos(bearing), MathF.Sin(bearing)) * (hostRadius + 48);
        }
        var radius = Math.Clamp(180.0f * scale, 100.0f, 320.0f);
        var angle = -0.78f + index * 1.18f;
        return center + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * radius;
    }

    private void DrawLaunchComplex(Vector2 position, Color color)
    {
        DrawCircle(position, 9.0f, Fade(new Color(0.03f, 0.07f, 0.11f)));
        DrawArc(position, 9.0f, 0, MathF.Tau, 24, WithAlpha(color, 0.85f), 1.2f, true);
        DrawLine(position + new Vector2(-5, 5), position + new Vector2(0, -7), Fade(color), 1.5f, true);
        DrawLine(position + new Vector2(0, -7), position + new Vector2(5, 5), Fade(color), 1.5f, true);
        DrawLine(position + new Vector2(-6, 5), position + new Vector2(6, 5), Fade(color), 1.5f, true);
    }

    private void DrawShipyard(Vector2 position, Color color)
    {
        DrawRect(new Rect2(position - new Vector2(9, 6), new Vector2(18, 12)), Fade(new Color(0.03f, 0.07f, 0.11f)), true);
        DrawRect(new Rect2(position - new Vector2(9, 6), new Vector2(18, 12)), WithAlpha(color, 0.85f), false, 1.2f);
        DrawLine(position + new Vector2(-4, -9), position + new Vector2(-4, 9), Fade(color), 1.5f, true);
        DrawLine(position + new Vector2(4, -9), position + new Vector2(4, 9), Fade(color), 1.5f, true);
        DrawLine(position + new Vector2(-8, 0), position + new Vector2(8, 0), Fade(color), 1.2f, true);
    }

    private void DrawResourceNetwork(Vector2 position, Color color)
    {
        DrawCircle(position, 3.5f, Fade(color));
        for (var index = 0; index < 3; index++)
        {
            var angle = index * MathF.Tau / 3.0f - 0.4f;
            var asteroid = position + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * 9.0f;
            DrawLine(position, asteroid, WithAlpha(color, 0.65f), 1.1f, true);
            DrawCircle(asteroid, index == 0 ? 3.0f : 2.2f, Fade(new Color(color.R * 0.75f, color.G * 0.75f, color.B * 0.75f)));
        }
    }

    private TextureRect CreateStellarDisc()
    {
        var sprite = new TextureRect { Name = "StellarPhotosphere", Texture = CelestialBodyMaterials.WhiteTexture,
            MouseFilter = MouseFilterEnum.Ignore, ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize };
        AddChild(sprite);
        return sprite;
    }

    private void DrawBody(SystemSpatialBodyMarker body, Vector2 center, SystemSpatialViewport layout)
    {
        var position = ToScreen(body, center, layout.Scale);
        var radius = layout.BodyRadius(body);
        var selected = body.BodyId == _selectedBodyId;
        var hovered = body.BodyId == _hoveredBodyId;
        var known = _surfaces.TryGetValue(body.BodyId, out var surface);
        if (known)
        {
            DrawCircle(position, radius + Math.Min(4f, radius * .2f), WithAlpha(ResolveBodyColor(body.VisualClass), selected ? .15f : .055f));
            if (!_orbitalDiscs.TryGetValue(body.BodyId, out var sprite))
            {
                sprite = new FocusedPlanetView { Name = "OrbitalBody" + body.BodyId };
                AddChild(sprite);
                _orbitalDiscs.Add(body.BodyId, sprite);
            }
            sprite.SetBody(body);
            sprite.SetDiscRect(new Rect2(position - Vector2.One * radius, Vector2.One * radius * 2));
            sprite.Modulate = Fade(Colors.White);
            sprite.Visible = true;
        }
        else
        {
            DrawCircle(position, radius, Fade(new Color(0.065f, 0.087f, 0.115f)));
            DrawCircle(position, radius, WithAlpha(UnknownColor, 0.72f), false, 1.1f, true);
            if (radius > 2f)
                DrawArc(position, radius - 2.0f, 2.8f, 4.6f, 16, WithAlpha(UnknownColor, 0.27f), 1.0f, true);
        }

        if (selected || hovered)
        {
            var color = WithAlpha(SelectedColor, selected ? 1.0f : 0.62f);
            for (var side = 0; side < 4; side++)
                DrawArc(position, radius + 6.0f, side * MathF.PI * 0.5f + 0.18f, side * MathF.PI * 0.5f + 1.18f, 12, color, selected ? 1.8f : 1.2f, true);
            if (selected)
                DrawCircle(position, radius + 10.0f, WithAlpha(SelectedColor, .16f), false, .8f, true);
        }
        DrawSignatures(body, position, radius);
        if (body.Kind != PlanetaryBodyKind.Moon || hovered || selected)
        {
            var textPosition = position + new Vector2(radius + 8.0f, -radius - 2.0f);
            var labelSize = selected || hovered ? 13 : 11;
            if (body.Kind != PlanetaryBodyKind.Moon && body.OrbitIndex == 0 && body.OffsetX < 0)
                textPosition.X = position.X - radius - 8 - _font.GetStringSize(body.Label, HorizontalAlignment.Left, -1, labelSize).X;
            DrawString(_font, textPosition + Vector2.One, body.Label, HorizontalAlignment.Left, -1, labelSize, Fade(CanvasColor));
            DrawString(_font, textPosition, body.Label, HorizontalAlignment.Left, -1, labelSize,
                Fade(selected || hovered ? PrimaryTextColor : SecondaryTextColor));
        }
    }

    private void DrawSelectionCaption(Vector2 viewport)
    {
        var id = _hoveredBodyId ?? _selectedBodyId;
        if (id is int bodyId && _bodiesById.TryGetValue(bodyId, out var body))
        {
            var className = body.VisualClass switch
            {
                SystemSpatialBodyVisualClass.UnknownPlanet => "Planet · environment unconfirmed",
                SystemSpatialBodyVisualClass.UnknownMoon => "Moon · environment unconfirmed",
                SystemSpatialBodyVisualClass.HotRocky => "Hot rocky world",
                SystemSpatialBodyVisualClass.GasGiant => "Gas giant",
                SystemSpatialBodyVisualClass.IceGiant => "Ice giant",
                _ => body.VisualClass.ToString(),
            };
            DrawString(_font, new Vector2(112.0f, viewport.Y - 154.0f), body.Label, HorizontalAlignment.Left, -1, 16, PrimaryTextColor);
            DrawString(_font, new Vector2(112.0f, viewport.Y - 135.0f), className, HorizontalAlignment.Left, -1, 11, SecondaryTextColor);
        }
        else
            DrawString(_font, new Vector2(112.0f, viewport.Y - 135.0f), "Select a world to inspect  ·  Orbital distances shown schematically",
                HorizontalAlignment.Left, -1, 11, MutedTextColor);
    }

    private void DrawFocusedWorldFacts()
    {
        if (_focusedBodyId is not int id || !_bodiesById.TryGetValue(id, out var body)) return;
        var panel = new Rect2(112, 252, 266, 142);
        DrawRect(panel, WithAlpha(CanvasColor, .86f));
        DrawRect(panel, WithAlpha(KeylineColor, .52f), false, 1);
        DrawString(_font, panel.Position + new Vector2(12, 20), "PLANETARY PROFILE", HorizontalAlignment.Left, -1, 10, SelectedColor);
        var moons = _bodiesById.Values.Count(candidate => candidate.ParentBodyId == body.BodyId);
        var kind = body.Kind == PlanetaryBodyKind.Moon ? "Natural satellite" : moons == 1 ? "Planet · 1 moon" : $"Planet · {moons} moons";
        DrawString(_font, panel.Position + new Vector2(12, 42), kind, HorizontalAlignment.Left, 242, 12, PrimaryTextColor);
        var scale = MetricFormat.PhysicalScaleSummary(body.RadiusEarth, body.MassEarth,
            body.HasDetailedEnvironment);
        DrawString(_font, panel.Position + new Vector2(12, 64), scale, HorizontalAlignment.Left, 242, 11, SecondaryTextColor);
        var climate = MetricFormat.PhysicalEnvironmentSummary(body.GravityG, body.TemperatureKelvin,
            body.PressureKPa, body.HasDetailedEnvironment);
        DrawString(_font, panel.Position + new Vector2(12, 86), climate, HorizontalAlignment.Left, 242, 11, SecondaryTextColor);
        var atmosphere = body.Atmosphere switch
        {
            PlanetaryAtmosphereRegime.OxygenNitrogen => "Oxygen–nitrogen atmosphere",
            PlanetaryAtmosphereRegime.OxygenRich => "Oxygen-rich atmosphere",
            PlanetaryAtmosphereRegime.CarbonDioxideRich => "Carbon-dioxide-rich atmosphere",
            PlanetaryAtmosphereRegime.Vacuum => "Airless / vacuum",
            PlanetaryAtmosphereRegime.Reducing => "Reducing atmosphere",
            PlanetaryAtmosphereRegime.Inert => "Inert atmosphere",
            PlanetaryAtmosphereRegime.Other => "Unusual atmosphere",
            _ => "Atmosphere unconfirmed"
        };
        DrawString(_font, panel.Position + new Vector2(12, 108), atmosphere, HorizontalAlignment.Left, 242, 11, SecondaryTextColor);
        var settlement = body.SurfaceKey == "earth" ? "HUMAN HOMEWORLD · SURFACE AVAILABLE" :
            body.HasDetailedEnvironment ? "SURVEYED WORLD" : "ENVIRONMENT UNCONFIRMED";
        DrawString(_font, panel.Position + new Vector2(12, 130), settlement, HorizontalAlignment.Left, 242, 10,
            body.SurfaceKey == "earth" ? ActivityColor : MutedTextColor);
    }

    private void DrawSelectedWorldPortrait()
    {
        if (_selectedBodyId is not int id || !_bodiesById.TryGetValue(id, out var body)) return;
        var center = new Vector2(250, 296);
        const float radius = 52;
        if (_surfaces.TryGetValue(id, out var surface))
        {
            DrawCircle(center, radius + 7, WithAlpha(SelectedColor, 0.04f));
            if (body.SurfaceKey == "saturn") DrawSaturnRings(center, radius, front: false);
            DrawTextureRect(surface.Texture, new Rect2(center - Vector2.One * radius, Vector2.One * radius * 2), false);
            if (body.SurfaceKey == "saturn") DrawSaturnRings(center, radius, front: true);
        }
        else
        {
            DrawCircle(center, radius, CanvasColor);
            DrawCircle(center, radius, UnknownColor, false, 1.4f, true);
        }
        DrawString(_font, new Vector2(160, 367), body.Label, HorizontalAlignment.Left, 230, 22, PrimaryTextColor);
        var caption = body.SurfaceKey == "earth" ? "HUMAN HOMEWORLD" :
            body.SurfaceKey is not null ? "SOL SYSTEM" : body.HasDetailedEnvironment ? "SURVEYED WORLD" : "UNCONFIRMED ENVIRONMENT";
        DrawString(_font, new Vector2(160, 388), caption, HorizontalAlignment.Left, 230, 10, SelectedColor);
        var scale = MetricFormat.PhysicalScaleSummary(body.RadiusEarth, body.MassEarth,
            body.HasDetailedEnvironment);
        DrawString(_font, new Vector2(160, 409), scale, HorizontalAlignment.Left, 230, 10, SecondaryTextColor);
        var moonCount = _bodiesById.Values.Count(candidate => candidate.ParentBodyId == body.BodyId);
        var family = body.ParentBodyId is int parentId && _bodiesById.TryGetValue(parentId, out var parent)
            ? $"MOON OF {parent.Label.ToUpperInvariant()}"
            : moonCount == 1 ? "1 NATURAL SATELLITE" : $"{moonCount} NATURAL SATELLITES";
        DrawString(_font, new Vector2(160, 427), family, HorizontalAlignment.Left, 230, 10, MutedTextColor);
        if (body.HasDetailedEnvironment)
        {
            var atmosphere = body.Atmosphere?.ToString().Replace("Rich", " rich", StringComparison.Ordinal) ?? "Unknown";
            DrawString(_font, new Vector2(160, 445), $"{MetricFormat.Temperature(body.TemperatureKelvin, true)}  ·  {MetricFormat.Pressure(body.PressureKPa, true)}  ·  {atmosphere}",
                HorizontalAlignment.Left, 230, 10, SecondaryTextColor);
        }
    }

    private void DrawSaturnRings(Vector2 center, float radius, bool front)
    {
        const float tilt = -0.36f;
        for (var band = 0; band < 16; band++)
        {
            if (band is 9 or 10) continue; // Visible Cassini division in the schematic ring plane.
            var distance = radius * (1.25f + band * 0.065f);
            var points = new Vector2[49];
            for (var point = 0; point < points.Length; point++)
            {
                var angle = (front ? 0 : MathF.PI) + point / 48f * MathF.PI;
                points[point] = center + new Vector2(MathF.Cos(angle) * distance, MathF.Sin(angle) * distance * 0.33f).Rotated(tilt);
            }
            DrawPolyline(points, Fade(new Color(0.76f, 0.70f, 0.55f, front ? 0.72f : 0.44f)), Math.Max(0.65f, radius * 0.055f), true);
        }
    }

    private void DrawSignatures(SystemSpatialBodyMarker body, Vector2 position, float radius)
    {
        var anchor = position + new Vector2(radius + 7.0f, radius + 4.0f);
        var index = 0;
        if (body.PositiveResourceSignature)
        {
            var point = anchor + new Vector2(index++ * 11.0f, 0.0f);
            DrawLine(point + new Vector2(0, -3), point + new Vector2(3, 0), Fade(ResourceColor), 1.3f, true);
            DrawLine(point + new Vector2(3, 0), point + new Vector2(0, 3), Fade(ResourceColor), 1.3f, true);
            DrawLine(point + new Vector2(0, 3), point + new Vector2(-3, 0), Fade(ResourceColor), 1.3f, true);
            DrawLine(point + new Vector2(-3, 0), point + new Vector2(0, -3), Fade(ResourceColor), 1.3f, true);
        }
        if (body.PositiveAnomalySignature)
        {
            var point = anchor + new Vector2(index++ * 11.0f, 0.0f);
            DrawLine(point + new Vector2(0, -3), point + new Vector2(3, 3), Fade(AnomalyColor), 1.3f, true);
            DrawLine(point + new Vector2(3, 3), point + new Vector2(-3, 3), Fade(AnomalyColor), 1.3f, true);
            DrawLine(point + new Vector2(-3, 3), point + new Vector2(0, -3), Fade(AnomalyColor), 1.3f, true);
        }
        if (body.PositiveActivitySignature)
            DrawRect(new Rect2(anchor + new Vector2(index * 11.0f - 3.0f, -3.0f), Vector2.One * 6.0f), Fade(ActivityColor), false, 1.2f);
    }

    private static ImageTexture CreateSurface(SystemSpatialBodyMarker body)
    {
        var resolution = body.SurfaceKey is null ? 96 : 256;
        using var image = Image.CreateEmpty(resolution, resolution, false, Image.Format.Rgba8);
        using var source = SolBodyMaterials.LoadColorSource(body.SurfaceKey);
        var baseColor = ResolveBodyColor(body.VisualClass);
        var towardStar = new Vector2(-body.OffsetX, -body.OffsetY).Normalized();
        var phase = (body.BodyId & 255) * 0.137f;
        for (var y = 0; y < resolution; y++)
        {
            for (var x = 0; x < resolution; x++)
            {
                var nx = ((x + 0.5f) / resolution - 0.5f) * 2.0f;
                var ny = ((y + 0.5f) / resolution - 0.5f) * 2.0f;
                var radial = nx * nx + ny * ny;
                if (radial > 1.0f)
                    continue;
                var nz = MathF.Sqrt(1.0f - radial);
                var light = Math.Max(0.0f, nx * towardStar.X * 0.85f + ny * towardStar.Y * 0.85f + nz * 0.53f);
                var color = baseColor;
                // Decorative class-level material, never a map of authoritative surface features.
                var detail = MathF.Sin(nx * 9.0f + phase) * MathF.Sin(ny * 8.0f - phase) + MathF.Sin((nx + ny) * 17.0f + phase) * 0.28f;
                if (source is not null)
                    color = SolBodyMaterials.Sample(source, body.SurfaceKey!, nx, ny, nz);
                else if (body.VisualClass is SystemSpatialBodyVisualClass.GasGiant or SystemSpatialBodyVisualClass.IceGiant)
                {
                    var band = MathF.Sin(ny * 27.0f + MathF.Sin(nx * 6.0f + phase) * 0.55f);
                    color = baseColor.Lerp(new Color(0.92f, 0.82f, 0.64f), Math.Max(0.0f, band) * 0.27f);
                    color = color.Darkened(Math.Max(0.0f, -band) * 0.25f);
                }
                else if (body.VisualClass == SystemSpatialBodyVisualClass.Oceanic)
                {
                    color = baseColor.Lightened(Math.Max(0.0f, detail) * 0.18f);
                    var cloud = MathF.Sin(ny * 19.0f + MathF.Sin(nx * 10.0f + phase) * 2.0f);
                    color = color.Lerp(new Color(0.78f, 0.89f, 0.94f), Math.Max(0.0f, cloud - 0.50f) * 0.50f);
                }
                else
                    color = baseColor.Lightened(detail * 0.09f);
                // Disc photographs already contain their observed illumination.
                var illumination = source is not null && body.SurfaceKey is ("earth" or "mercury" or "venus" or "uranus" or "moon")
                    ? 0.90f + nz * 0.10f : 0.18f + light * 0.87f;
                color = new Color(color.R * illumination, color.G * illumination, color.B * illumination,
                    Math.Clamp((1.0f - radial) * resolution * 0.55f, 0.0f, 1.0f));
                image.SetPixel(x, y, color);
            }
        }
        return ImageTexture.CreateFromImage(image);
    }

    private static Color ResolveBodyColor(SystemSpatialBodyVisualClass visualClass) => visualClass switch
    {
        SystemSpatialBodyVisualClass.Rocky => new Color(0.64f, 0.57f, 0.47f),
        SystemSpatialBodyVisualClass.Oceanic => new Color(0.16f, 0.48f, 0.75f),
        SystemSpatialBodyVisualClass.Frozen => new Color(0.69f, 0.84f, 0.89f),
        SystemSpatialBodyVisualClass.HotRocky => new Color(0.82f, 0.40f, 0.23f),
        SystemSpatialBodyVisualClass.GasGiant => new Color(0.80f, 0.63f, 0.43f),
        SystemSpatialBodyVisualClass.IceGiant => new Color(0.31f, 0.69f, 0.82f),
        SystemSpatialBodyVisualClass.Moon => new Color(0.64f, 0.65f, 0.67f),
        _ => UnknownColor,
    };

    private void ClearHover()
    {
        _hoveredBodyId = null;
        _hoveredLaneDestinationId = null;
        TooltipText = string.Empty;
        MouseDefaultCursorShape = CursorShape.Arrow;
        QueueRedraw();
    }

    private void ClearSurfaces()
    {
        foreach (var surface in _surfaces.Values)
            surface.Texture.Dispose();
        _surfaces.Clear();
    }

    private void ResizeToViewport()
    {
        _lastViewportSize = GetViewportRect().Size;
        Position = Vector2.Zero;
        Size = _lastViewportSize;
    }

    private static Vector2 ToScreen(SystemSpatialBodyMarker marker, Vector2 center, float scale) =>
        center + new Vector2(marker.OffsetX, marker.OffsetY) * scale;

    private Vector2 LanePosition(LocalLaneMarker lane, Vector2 center, float scale)
    {
        // Match FleetLocalTransit.GateTowards exactly: the rendering scale merely maps its
        // normalized chart unit to this system's schematic radius. No visual spreading or
        // screen clamp may change a real lane bearing or its warp-in/out location.
        return center + lane.Direction.Normalized() * (FleetLocalTransit.GateRadius * _snapshot!.DesignRadius * ChartRenderRadiusFactor * scale);
    }

    private LocalLaneMarker? HitLane(Vector2 position)
    {
        if (_snapshot is null) return null;
        var layout = CurrentViewport;
        var center = new Vector2(layout.CenterX, layout.CenterY);
        return BuildLaneMarkerGeometries(center, layout.Scale)
            .Where(marker => marker.Bounds.HasPoint(position))
            .Where(marker => PointInTriangle(position, marker.BaseA, marker.BaseB, marker.Apex))
            .OrderBy(marker => position.DistanceTo(marker.Center))
            .ThenBy(marker => marker.Lane.DestinationSystemId)
            .Select(marker => marker.Lane).FirstOrDefault();
    }

    private IReadOnlyList<LaneMarkerGeometryData> BuildLaneMarkerGeometries(Vector2 center, float scale)
    {
        if (_snapshot is null) return Array.Empty<LaneMarkerGeometryData>();
        var placed = new List<LaneMarkerGeometryData>();
        foreach (var lane in (GetLocalLanes?.Invoke() ?? Array.Empty<LocalLaneMarker>())
                     .OrderBy(item => item.DestinationSystemId))
        {
            var transitGate = LanePosition(lane, center, scale);
            var gate = center + lane.Direction.Normalized() * Math.Max(
                transitGate.DistanceTo(center), BoundaryRadius(_snapshot, scale) + 12f);
            var stagger = 0f;
            var marker = CreateLaneMarkerGeometry(lane, gate, stagger);
            // Nearby catalog lanes can have almost identical bearings. Move only the decorative
            // marker outward along its own exact ray until its complete label/body bounds clear.
            // Each step passes an entire blocker. The number of prior markers bounds
            // the work, including exactly coincident bearings and long system names.
            for (var attempt = 0; attempt <= placed.Count; attempt++)
            {
                var blockers = placed.Where(other => other.Bounds.Grow(6f).Intersects(marker.Bounds)).ToArray();
                if (blockers.Length == 0) break;
                stagger += blockers.Max(other => other.Bounds.Size.Length()) + marker.Bounds.Size.Length() + 12f;
                marker = CreateLaneMarkerGeometry(lane, gate, stagger);
            }
            placed.Add(marker);
        }
        return placed;
    }

    private LaneMarkerGeometryData CreateLaneMarkerGeometry(LocalLaneMarker lane, Vector2 gate, float radialStagger)
    {
        var direction = lane.Direction.Normalized();
        var normal = new Vector2(-direction.Y, direction.X);
        const int fontSize = 11;
        var label = FitLaneLabel(lane.IsKnown ? lane.Label : "????", fontSize, maximumWidth: 96f);
        var labelWidth = _font.GetStringSize(label, HorizontalAlignment.Left, -1, fontSize).X;
        var labelHalfHeight = _font.GetHeight(fontSize) * .5f;
        // Keep the actual transit gate at `gate`. The decorative glyph shifts outward on that
        // exact ray so its name can sit behind, and outside, the wide base without crossing the
        // orbital delimiter or pretending the ship's warp anchor moved.
        var visualBase = gate + direction * (labelHalfHeight * 2f + 4f + radialStagger);
        const float baseHalfWidth = 20f;
        var baseA = visualBase + normal * baseHalfWidth;
        var baseB = visualBase - normal * baseHalfWidth;
        var apex = visualBase + direction * 34f;
        var labelRotation = normal.Angle();
        if (MathF.Cos(labelRotation) < 0f) labelRotation += MathF.PI;
        // Sample the center of the tapered nose beyond the label's radial extent. Keeping
        // this on the lane axis avoids both white text pixels and the antialiased edge.
        var sample = visualBase + direction * 22f;
        var labelCenter = visualBase - direction * (labelHalfHeight + 2f);
        var labelCorners = new[]
        {
            labelCenter + normal * (labelWidth * .5f) + direction * labelHalfHeight,
            labelCenter + normal * (labelWidth * .5f) - direction * labelHalfHeight,
            labelCenter - normal * (labelWidth * .5f) + direction * labelHalfHeight,
            labelCenter - normal * (labelWidth * .5f) - direction * labelHalfHeight,
        };
        var points = labelCorners.Append(baseA).Append(baseB).Append(apex).ToArray();
        var minimum = new Vector2(points.Min(point => point.X), points.Min(point => point.Y));
        var maximum = new Vector2(points.Max(point => point.X), points.Max(point => point.Y));
        return new(lane, visualBase + direction * 12f, baseA, baseB, apex, new Rect2(minimum, maximum - minimum),
            labelCenter, labelHalfHeight, labelRotation, label, labelWidth, sample);
    }

    private float BoundaryRadius(SystemSpatialSnapshot snapshot, float scale)
    {
        var extent = snapshot.DesignRadius * scale;
        var layout = new SystemSpatialViewport(0, 0, scale);
        foreach (var body in snapshot.Bodies)
        {
            var ringFactor = body.SurfaceKey == "saturn" ? 2.8f : 1f;
            extent = Math.Max(extent, new Vector2(body.OffsetX, body.OffsetY).Length() * scale + layout.BodyRadius(body) * ringFactor);
        }
        return extent + Math.Max(36f, extent * .15f);
    }

    private string FitLaneLabel(string label, int fontSize, float maximumWidth)
    {
        if (_font.GetStringSize(label, HorizontalAlignment.Left, -1, fontSize).X <= maximumWidth) return label;
        var shortened = label;
        while (shortened.Length > 1 &&
            _font.GetStringSize(shortened + "…", HorizontalAlignment.Left, -1, fontSize).X > maximumWidth)
            shortened = shortened[..^1];
        return shortened + "…";
    }

    private static bool PointInTriangle(Vector2 point, Vector2 a, Vector2 b, Vector2 c)
    {
        static float Side(Vector2 p1, Vector2 p2, Vector2 p3) =>
            (p1.X - p3.X) * (p2.Y - p3.Y) - (p2.X - p3.X) * (p1.Y - p3.Y);
        var d1 = Side(point, a, b); var d2 = Side(point, b, c); var d3 = Side(point, c, a);
        return !(d1 < 0 || d2 < 0 || d3 < 0) || !(d1 > 0 || d2 > 0 || d3 > 0);
    }

    private void DrawLocalLanes(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        var markers = BuildLaneMarkerGeometries(center, scale);
        // A hovered marker paints last so nearby catalog bearings cannot hide its orange body.
        foreach (var marker in markers.OrderBy(item => item.Lane.DestinationSystemId == _hoveredLaneDestinationId ? 1 : 0))
        {
            // Deep free zoom moves gates far beyond the viewport. Cull before
            // triangulation, where small offscreen shapes lose coordinate precision.
            if (!new Rect2(Vector2.Zero, Size).Grow(8f).Intersects(marker.Bounds)) continue;
            var lane = marker.Lane;
            // Gates share the exact simulated chart bearing. Forest green establishes a
            // consistent travel affordance; only the glyph shifts outward on the same ray.
            var hovered = _hoveredLaneDestinationId == lane.DestinationSystemId;
            var coreColor = hovered ? new Color("f39a32") : new Color("05250f");
            var borderColor = hovered ? new Color("ffd28a") : lane.IsKnown ? new Color("1a552b") : new Color("123d20");
            var triangle = new Vector2[] { marker.BaseA, marker.Apex, marker.BaseB };
            DrawColoredPolygon(triangle.Select(point => point + new Vector2(3f, 4f)).ToArray(), WithAlpha(new Color("020a06"), .78f));
            DrawColoredPolygon(triangle, WithAlpha(coreColor, 1f));
            DrawPolyline(new Vector2[] { marker.BaseA, marker.Apex, marker.BaseB, marker.BaseA }, WithAlpha(borderColor, 1f), 2.2f, true);
            DrawSetTransform(marker.LabelCenter, marker.LabelRotation, Vector2.One);
            DrawString(_font, new Vector2(-marker.LabelWidth * .5f, 4f), marker.Label,
                HorizontalAlignment.Center, marker.LabelWidth, 11, Colors.White);
            DrawSetTransform(Vector2.Zero, 0f, Vector2.One);
        }
    }

    private void DrawSystemBoundary(SystemSpatialSnapshot snapshot, Vector2 center, float scale)
    {
        var radius = BoundaryRadius(snapshot, scale);
        const int segments = 96;
        for (var index = 0; index < segments; index += 2)
        {
            var start = Mathf.Tau * index / segments;
            var end = Mathf.Tau * (index + 1) / segments;
            DrawArc(center, radius, start, end, 3, WithAlpha(new Color("6ba878"), .55f), 1.2f, true);
        }
    }

    private sealed record LaneMarkerGeometryData(LocalLaneMarker Lane, Vector2 Center, Vector2 BaseA, Vector2 BaseB,
        Vector2 Apex, Rect2 Bounds, Vector2 LabelCenter, float LabelHalfHeight, float LabelRotation, string Label,
        float LabelWidth, Vector2 ColorSample);
    private Color WithAlpha(Color color, float alpha) => new(color.R, color.G, color.B, alpha * _drawOpacity);
    private Color Fade(Color color) => new(color.R, color.G, color.B, color.A * _drawOpacity);
}
