using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Presentation.Spatial;
using Game.Simulation.Knowledge;

namespace Game.Presentation;

public sealed record SpatialCameraSnapshot(string Level, float Zoom, Vector2 Pan, float TargetZoom,
    Vector2 TargetPan, int? FocusedBodyId, bool IsTransitioning);
public sealed record SpatialCatalogEntry(int SystemId, SystemSurveyLevel SurveyLevel);

public partial class Main
{
    private readonly SmoothSpatialCamera _regionalCamera = new();
    private object? _regionalCameraCampaign;
    private Vector2 _regionalViewportSize;
    private bool _regionalCameraReady;
    private object? _overviewFrameCampaign;
    private Vector2 _overviewFrameViewport;
    private SystemSpatialViewport _overviewFrame;
    private float _systemViewBlend;
    private bool _leavingSystem;
    private HBoxContainer? _spatialBreadcrumbs;
    private Button? _galaxyCrumb;
    private Button? _regionCrumb;
    private Button? _systemCrumb;
    private Button? _planetCrumb;
    private Button? _surfaceCrumb;

    public float UiMapZoom => _zoom;
    public Vector2 UiMapOriginScreen => GetViewportRect().Size * 0.5f + _pan;
    public float UiOverviewBlend
    {
        get
        {
            if (!UsesFullGalaxyMap) return SpatialNavigationLayout.GalaxyOverviewBlend(_zoom);
            var fit = GalaxyOverviewFrame().Scale;
            return SpatialNavigationLayout.PopulationOverviewBlend(_zoom, fit);
        }
    }
    public float UiSystemViewBlend => _systemViewBlend;
    public bool UiIsStarFocused => _systemSpatialCanvas?.IsStarFocused == true;
    public int? UiFocusedPlanetBodyId => _systemSpatialCanvas?.FocusedBodyId;
    public event Action<int>? PlanetSurfaceRequested;
    public Func<int, bool>? PlanetSurfaceAvailable { get; set; }
    public IReadOnlyList<SystemSpatialBodyMarker> UiSystemBodies =>
        _systemSpatialCanvas?.VisibleBodies ?? Array.Empty<SystemSpatialBodyMarker>();
    public IReadOnlyList<SystemSpatialInfrastructureMarker> UiSystemInfrastructure =>
        _systemSpatialCanvas?.VisibleInfrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>();
    public Vector2? UiGetInfrastructureScreenPosition(string projectId) =>
        _systemSpatialCanvas?.GetInfrastructureScreenPosition(projectId);
    public int UiCachedPlanetMaterialCount => _systemSpatialCanvas?.CachedSurfaceCount ?? 0;
    public IReadOnlyList<SpatialCatalogEntry> UiSpatialCatalog => _galaxy?.Systems
        .Select(system => new SpatialCatalogEntry(system.Id,
            _galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, system.Id))).ToArray()
        ?? Array.Empty<SpatialCatalogEntry>();
    public SpatialCameraSnapshot UiCameraSnapshot
    {
        get
        {
            var system = UiIsSystemSpatialView;
            if (system)
            {
                if (_systemSpatialCanvas!.IsDetailedFocus)
                {
                    var scene = _systemSpatialCanvas.Scene;
                    return new(UiSpatialScale.ToString(), scene.FitDistance / Math.Max(.01f, scene.Distance),
                        new(scene.CameraTarget.X, scene.CameraTarget.Z), scene.FitDistance / Math.Max(.01f, scene.TargetDistance),
                        new(scene.TargetCameraTarget.X, scene.TargetCameraTarget.Z), UiFocusedPlanetBodyId,
                        scene.IsMoving || _systemViewBlend < 1 || _leavingSystem);
                }
                var overview = _systemSpatialCanvas.Camera;
                return new(UiSpatialScale.ToString(), overview.Scale, new(overview.OriginX, overview.OriginY), overview.TargetScale,
                    new(overview.TargetOriginX, overview.TargetOriginY), null,
                    overview.IsMoving || _systemViewBlend < 1 || _leavingSystem);
            }
            var camera = _regionalCamera;
            return new(UiSpatialScale.ToString(), camera.Scale, new(camera.OriginX, camera.OriginY), camera.TargetScale,
                new(camera.TargetOriginX, camera.TargetOriginY), UiFocusedPlanetBodyId,
                camera.IsMoving || (system && (_systemViewBlend < 1 || _leavingSystem)));
        }
    }

    private void InitializeSpatialNavigation()
    {
        if (_spatialBreadcrumbs is not null) return;
        var layer = new CanvasLayer { Name = "SpatialNavigation", Layer = 4 };
        _spatialBreadcrumbs = new HBoxContainer { Position = new Vector2(118, 78) };
        VisualUi.ContainPointerInput(_spatialBreadcrumbs);
        _spatialBreadcrumbs.AddThemeConstantOverride("separation", 5);
        Button Crumb(string name, string text, Action action)
        {
            var button = new Button { Name = name, Text = text, CustomMinimumSize = new Vector2(0, 28), MouseFilter = Control.MouseFilterEnum.Stop };
            button.AddThemeFontSizeOverride("font_size", 12);
            AudioDirector.Bind(button);
            button.Pressed += action;
            _spatialBreadcrumbs.AddChild(button);
            return button;
        }
        Crumb("SpatialBack", "‹ Back", UiNavigateBack);
        _galaxyCrumb = Crumb("SpatialOverview", UiOverviewName, UiShowGalaxyOverview);
        _regionCrumb = Crumb("SpatialRegion", "Stellar region", UiShowStellarRegion);
        _systemCrumb = Crumb("SpatialSystem", "System", UiOpenSelectedSystem);
        _planetCrumb = Crumb("SpatialPlanet", "Planet", () => _systemSpatialCanvas?.FocusSelectedBody());
        _surfaceCrumb = Crumb("SpatialSurface", "Surface", () =>
        {
            if (UiFocusedPlanetBodyId is int bodyId && PlanetSurfaceAvailable?.Invoke(bodyId) == true)
                PlanetSurfaceRequested?.Invoke(bodyId);
        });
        layer.AddChild(_spatialBreadcrumbs);
        AddChild(layer);
        SynchronizeRegionalCamera();
    }

    private void RefreshSpatialNavigation(double delta)
    {
        if (!_regionalCameraReady || !ReferenceEquals(_regionalCameraCampaign, _galaxy))
            SynchronizeRegionalCamera();
        var size = GetViewportRect().Size;
        if (size != _regionalViewportSize)
        {
            var shift = (size - _regionalViewportSize) * 0.5f;
            _regionalCamera.Translate(shift.X, shift.Y);
            _regionalViewportSize = size;
            _pan = new Vector2(_regionalCamera.OriginX, _regionalCamera.OriginY) - size * 0.5f;
        }
        if (!(UiIsMenuOpen || UiIsDeveloperToolsOpen) && _regionalCamera.Advance(delta))
        {
            _zoom = _regionalCamera.Scale;
            _pan = new Vector2(_regionalCamera.OriginX, _regionalCamera.OriginY) - size * 0.5f;
            QueueRedraw();
        }
        if (!(UiIsMenuOpen || UiIsDeveloperToolsOpen) && _systemSpatialState.IsOpen && _systemSpatialCanvas is not null)
        {
            var goal = _leavingSystem ? 0f : 1f;
            _systemViewBlend = Mathf.MoveToward(_systemViewBlend, goal, (float)Math.Clamp(delta, 0, 0.1) * 5.5f);
            _systemSpatialCanvas.Modulate = new Color(1, 1, 1, _systemViewBlend);
            if (_leavingSystem && _systemViewBlend <= 0)
                ReturnToStellarView(announce: false);
        }
        if (_spatialBreadcrumbs is null) return;
        _spatialBreadcrumbs.Visible = !(UiIsMenuOpen || UiIsDeveloperToolsOpen);
        _galaxyCrumb!.Text = UiOverviewName;
        _galaxyCrumb.Disabled = !UiIsSystemSpatialView && UiOverviewBlend > 0.9f;
        _regionCrumb!.Disabled = !UiIsSystemSpatialView && UiOverviewBlend < 0.1f;
        _systemCrumb!.Visible = _selectedSystemId >= 0;
        _systemCrumb.Disabled = UiIsSystemSpatialView && !_systemSpatialCanvas!.IsPlanetFocused;
        _systemCrumb.Text = UiIsSystemSpatialView ? _systemSpatialCanvas!.SystemName : "Open system";
        _planetCrumb!.Visible = _systemSpatialCanvas?.SelectedBodyId.HasValue == true && UiIsSystemSpatialView;
        _planetCrumb.Disabled = _systemSpatialCanvas?.IsPlanetFocused == true;
        _planetCrumb.Text = _systemSpatialCanvas?.SelectedBodyId is int selected ? _systemSpatialCanvas.GetBodyLabel(selected) ?? "Planet" : "Planet";
        _surfaceCrumb!.Visible = UiFocusedPlanetBodyId.HasValue;
        _surfaceCrumb.Disabled = PlanetSurfaceRequested is null || UiFocusedPlanetBodyId is not int surfaceBody ||
            PlanetSurfaceAvailable?.Invoke(surfaceBody) != true;
    }

    private void SynchronizeRegionalCamera()
    {
        _regionalViewportSize = GetViewportRect().Size;
        var origin = _regionalViewportSize * 0.5f + _pan;
        _regionalCamera.Snap(_zoom, origin.X, origin.Y);
        _regionalCameraCampaign = _galaxy;
        _regionalCameraReady = true;
    }

    private void PanRegionalCamera(Vector2 motion)
    {
        if (!_regionalCameraReady) SynchronizeRegionalCamera();
        _regionalCamera.Pan(motion.X, motion.Y);
        _pan = new Vector2(_regionalCamera.OriginX, _regionalCamera.OriginY) - GetViewportRect().Size * 0.5f;
    }

    private Vector2 SpatialZoomButtonAnchor()
    {
        if (UiIsSystemSpatialView)
            return _systemSpatialCanvas?.SelectedBodyId is int bodyId
                ? _systemSpatialCanvas.GetBodyScreenPosition(bodyId) ?? GetViewportRect().Size * 0.5f
                : GetViewportRect().Size * 0.5f;
        return _selectedSystemId >= 0 ? UiGetCatalogScreenPosition(_selectedSystemId) ?? UiMapOriginScreen : UiMapOriginScreen;
    }

    private void ZoomSpatialAt(float factor, Vector2 anchor)
    {
        if (UiIsMenuOpen || UiIsDeveloperToolsOpen) return;
        if (UiIsSystemSpatialView)
        {
            _systemSpatialCanvas?.ZoomAt(factor, anchor);
            return;
        }
        if (!_regionalCameraReady) SynchronizeRegionalCamera();
        // Regional zoom remains free and cursor-anchored. A system handoff is deliberate:
        // the pointer must be on a reconnoitred catalogue star at close approach, rather than
        // using an unrelated lingering selection as an implicit destination.
        if (factor > 1 && _regionalCamera.TargetScale * factor >= RegionalSystemEntryZoom &&
            TryEnterSystemFromRegionalCloseApproach(anchor))
            return;
        var overview = GalaxyOverviewFrame();
        if (factor < 1 && _regionalCamera.TargetScale * factor <= overview.Scale)
        {
            _regionalCamera.SetTarget(overview.Scale, overview.CenterX, overview.CenterY);
            return;
        }
        _regionalCamera.ZoomAt(factor, anchor.X, anchor.Y, overview.Scale, RegionalMaximumZoom);
    }

    private bool TryEnterSystemFromRegionalCloseApproach(Vector2 anchor)
    {
        var hovered = FindNearestCatalogSystem(anchor, 0);
        if (hovered is null || _galaxy is null)
            return false;
        if (_galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, hovered.Id) < SystemSurveyLevel.PartiallySurveyed)
            return false;
        _selectedSystemId = hovered.Id;
        UiClearFleetSelection();
        EnterSelectedSystemView(starFocusedEntry: true);
        return true;
    }

    public Vector2 UiSystemCameraAngles => _systemSpatialCanvas is null ? Vector2.Zero : new(_systemSpatialCanvas.Scene.Yaw, _systemSpatialCanvas.Scene.Pitch);
    public int UiSystemMeshBodyCount => _systemSpatialCanvas?.Scene.BodyCount ?? 0;

    private SystemSpatialViewport GalaxyOverviewFrame()
    {
        var size = GetViewportRect().Size;
        if (ReferenceEquals(_overviewFrameCampaign, _galaxy) && _overviewFrameViewport == size)
            return _overviewFrame;
        Rect2 bounds;
        if (UsesSolarNeighborhoodMap || UsesFullGalaxyMap)
        {
            // Camera and dust use one square world frame. The catalogue coordinates remain
            // measured light-year projections; this only fits their visual backdrop.
            bounds = GalaxyArtworkWorldFrame();
        }
        else
        {
            var world = SpatialNavigationLayout.GalaxyWorldFrame;
            bounds = new Rect2(world.Left, world.Top, world.Width, world.Height);
            foreach (var system in _galaxy.Systems)
                bounds = bounds.Expand(new Vector2(system.Position.X, system.Position.Y));
            bounds = bounds.Grow(60);
        }
        var usable = new Rect2(112, 170, Math.Max(1, size.X - 412), Math.Max(1, size.Y - 202));
        var scale = Math.Min(SpatialNavigationLayout.OverviewBlendFullScale,
            Math.Min(usable.Size.X / bounds.Size.X, usable.Size.Y / bounds.Size.Y));
        var origin = usable.GetCenter() - bounds.GetCenter() * scale;
        _overviewFrameCampaign = _galaxy;
        _overviewFrameViewport = size;
        return _overviewFrame = new(origin.X, origin.Y, scale);
    }

    public void UiShowGalaxyOverview()
    {
        if (UiIsMenuOpen || UiIsDeveloperToolsOpen) return;
        ReturnToStellarView(announce: false);
        if (!_regionalCameraReady) SynchronizeRegionalCamera();
        var size = GetViewportRect().Size;
        var frame = GalaxyOverviewFrame();
        _regionalCamera.SetTarget(frame.Scale, frame.CenterX, frame.CenterY);
        _panning = false;
    }

    public void UiShowStellarRegion()
    {
        if (UiIsMenuOpen || UiIsDeveloperToolsOpen) return;
        if (UiIsSystemSpatialView) { BeginReturnToRegion(); return; }
        if (!_regionalCameraReady) SynchronizeRegionalCamera();
        var size = GetViewportRect().Size;
        var system = _galaxy?.Systems.FirstOrDefault(item => item.Id == _selectedSystemId)
            ?? _galaxy?.Systems.FirstOrDefault(item => item.Id == PlayerCivilization.HomeSystemId);
        var position = system?.Position ?? System.Numerics.Vector2.Zero;
        _regionalCamera.SetTarget(SpatialNavigationLayout.StellarRegionScale,
            size.X * .5 - (double)position.X * UiCatalogVisualCoordinateScale * SpatialNavigationLayout.StellarRegionScale,
            size.Y * .5 - (double)position.Y * UiCatalogVisualCoordinateScale * SpatialNavigationLayout.StellarRegionScale);
        _panning = false;
    }

    public void UiNavigateBack()
    {
        if (UiIsMenuOpen || UiIsDeveloperToolsOpen) return;
        if (_systemSpatialCanvas?.IsDetailedFocus == true)
            _systemSpatialCanvas.ExitDetailedFocus();
        else if (UiIsSystemSpatialView)
            BeginReturnToRegion();
        else
            UiShowGalaxyOverview();
    }

    private void BeginReturnToRegion()
    {
        if (!_systemSpatialState.IsOpen) return;
        _leavingSystem = true;
        _panning = false;
    }
}
