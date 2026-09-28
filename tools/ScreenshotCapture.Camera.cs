using System;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Presentation;
using Game.Presentation.Spatial;
using Game.Simulation.Knowledge;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private bool _sawSmoothCameraTransition;

    private sealed record CameraObservation(string Level, float Zoom, Vector2 Pan,
        float TargetZoom, Vector2 TargetPan, int? FocusedBodyId, bool IsTransitioning,
        Vector3 Position, Vector3 Forward);

    private CameraObservation ObserveCamera()
    {
        var camera = _main.UiCameraSnapshot;
        Require(float.IsFinite(camera.Zoom) && camera.Zoom > 0 &&
            float.IsFinite(camera.Pan.X) && float.IsFinite(camera.Pan.Y) &&
            float.IsFinite(camera.TargetZoom) && camera.TargetZoom > 0 &&
            float.IsFinite(camera.TargetPan.X) && float.IsFinite(camera.TargetPan.Y),
            "The displayed camera contains a non-finite transform.");
        // A hidden system viewport retains its last 3D camera pose by design. It is
        // not part of the active regional camera, so regional restoration checks
        // must compare the regional zoom/pan and projected stars only.
        var spatialCanvas = _main.UiIsSystemSpatialView &&
            (camera.FocusedBodyId.HasValue || _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas").IsStarFocused)
            ? _main.GetNodeOrNull<Control>("SystemSpatialCanvas") : null;
        var perspectiveCamera = spatialCanvas is null ? null : Descendants(spatialCanvas).OfType<Camera3D>().FirstOrDefault();
        var position = perspectiveCamera?.GlobalPosition ?? Vector3.Zero;
        var forward = perspectiveCamera is null ? Vector3.Zero : -perspectiveCamera.GlobalTransform.Basis.Z;
        return new(camera.Level, camera.Zoom, camera.Pan, camera.TargetZoom, camera.TargetPan,
            camera.FocusedBodyId, camera.IsTransitioning, position, forward);
    }

    private async Task WaitForCameraAsync()
    {
        var started = Time.GetTicksMsec();
        var previous = ObserveCamera();
        while (true)
        {
            await WaitFramesAsync(1);
            var current = ObserveCamera();
            if (previous.Level == current.Level && previous.IsTransitioning && current.IsTransitioning &&
                (Math.Abs(previous.Zoom - current.Zoom) > 0.00001f || previous.Pan.DistanceTo(current.Pan) > 0.05f))
                _sawSmoothCameraTransition = true;
            if (!current.IsTransitioning)
            {
                Require(Math.Abs(current.Zoom - current.TargetZoom) < 0.0001f &&
                    current.Pan.DistanceTo(current.TargetPan) < 0.1f,
                    "Camera reported settled before reaching its requested transform.");
                return;
            }
            Require(Time.GetTicksMsec() - started < 4000, $"Camera did not settle within four seconds: {current}.");
            previous = current;
        }
    }

    private static bool SameCamera(CameraObservation expected, CameraObservation actual) =>
        expected.Level == actual.Level && expected.FocusedBodyId == actual.FocusedBodyId &&
        Math.Abs(expected.Zoom - actual.Zoom) <= Math.Max(0.0001f, expected.Zoom * 0.001f) &&
        expected.Pan.DistanceTo(actual.Pan) < 1.0f &&
        (expected.Position == Vector3.Zero || expected.Position.DistanceTo(actual.Position) < .08f) &&
        (expected.Forward == Vector3.Zero || expected.Forward.DistanceTo(actual.Forward) < .002f) &&
        !actual.IsTransitioning;

    private static bool SameZoomRoute(CameraObservation before, CameraObservation after,
        Vector2 anchorBefore, Vector2 anchorAfter, Vector2 otherBefore, Vector2 otherAfter) =>
        before.Level == after.Level && after.Zoom > before.Zoom && anchorBefore.DistanceTo(anchorAfter) < 1 &&
        Math.Abs(otherAfter.DistanceTo(anchorAfter) / otherBefore.DistanceTo(anchorBefore) - after.Zoom / before.Zoom) < 0.005f;

    private Vector2 StarPoint(int id) => _main.UiGetCatalogScreenPosition(id)
        ?? throw new InvalidOperationException($"Catalog star {id} has no rendered position.");

    private Vector2 BodyPoint(int id) => _main.UiGetBodyScreenPosition(id)
        ?? throw new InvalidOperationException($"Known body {id} has no rendered position.");

    private Button ZoomButton(bool inward) => Descendants(_main).OfType<Button>().Single(button =>
        button.Name == (inward ? "MapZoomIn" : "MapZoomOut"));

    private async Task WheelAsync(bool inward, Vector2 point)
    {
        await ClickPositionAsync(point, inward ? MouseButton.WheelUp : MouseButton.WheelDown);
        await WaitForCameraAsync();
    }

    private async Task ClickZoomAsync(bool inward)
    {
        await ClickControlAsync(ZoomButton(inward));
        await WaitForCameraAsync();
    }

    private async Task VerifyCameraJourneyAsync()
    {
        Require(_main.UiIsPaused && !_sidebar.IsDrawerOpen && !_main.UiIsSystemSpatialView,
            "Camera acceptance must start on the paused, unobstructed normal map.");
        await WaitForCameraAsync();
        var home = _main.UiSelectedSystemId;
        Check(_main.UiGalaxyDeepFieldOpacity == 0 && _main.UiRegionalBackdropOpacity > .99f &&
            _main.UiRegionalBackdropStarCount is >= 300 and <= 500 &&
            _main.UiCatalogStarRadius(home) >= 12 && _main.UiCatalogStarCoreRadius(home) <= 4,
            "regional-map-enlarged-stars-and-local-sky");
        var baseline = ObserveCamera();
        var homeBefore = StarPoint(home);
        var comparisonId = PublicCatalogIds().First(id => id != home);
        var comparisonBefore = StarPoint(comparisonId);

        await WheelAsync(true, StarPoint(home));
        var wheelCamera = ObserveCamera();
        var wheelComparison = StarPoint(comparisonId);
        Require(wheelCamera.Zoom > baseline.Zoom && !SameCamera(baseline, wheelCamera),
            "Uncovered regional wheel positive control did not zoom.");
        await WheelAsync(false, StarPoint(home));
        Require(SameCamera(baseline, ObserveCamera()), "Opposite regional wheel did not restore its transform.");
        await ClickZoomAsync(true);
        Check(SameZoomRoute(baseline, wheelCamera, homeBefore, StarPoint(home), comparisonBefore, wheelComparison) &&
            SameZoomRoute(baseline, ObserveCamera(), homeBefore, StarPoint(home), comparisonBefore, StarPoint(comparisonId)),
            "regional-wheel-button-zoom-parity");
        await ClickZoomAsync(false);

        var overviewSteps = 0;
        while (ObserveCamera().Level != "GalaxyOverview" || !FullGalaxyArtworkFits() || !PublicCatalogFits())
        {
            // A full 50,000 ly disk needs substantially more real wheel steps than the
            // compact nearby profile. Keep this bounded so gesture convergence remains tested.
            Require(overviewSteps++ < 48, "The full public galaxy catalog could not fit in the overview within the bounded zoom route.");
            var previous = ObserveCamera();
            await WheelAsync(false, StarPoint(home));
            Require(ObserveCamera().Zoom < previous.Zoom, "Overview zoom stopped before the public catalog fitted.");
        }
        Check(overviewSteps > 0 && !_main.UiIsSystemSpatialView, "galaxy-overview-reachable-by-wheel");
        Check(PublicCatalogFits() && FullGalaxyArtworkFits(), "galaxy-overview-shows-public-catalog");
        Check(_main.UiHasDeepField, "galaxy-overview-shows-distant-galaxy-field");
        Check(_main.UiGalaxyDeepFieldOpacity is >= .44f and <= .50f,
            "galaxy-overview-keeps-distant-galaxies-visibly-exposed");
        Check(_main.GetNode<Control>("DemoProgressPanel/DemoMilestones").IsVisibleInTree(),
            "first-colony-guide-remains-available-at-galaxy-scale");
        if (_main.UndisclosedCoreScreenPosition is { } projectedCore)
        {
            Require(_main.UiGalacticCore is null && _main.UiGalacticCoreScreenPosition is null &&
                    _main.UiGalacticCoreScreenRadius == 0 && GetViewport().GetVisibleRect().HasPoint(projectedCore),
                "an unexplored galactic core leaked through an observer-safe map API");
            var visibleCoreDisclosure = Descendants(_main).OfType<Control>().Where(control => control.IsVisibleInTree())
                .Select(control => control is Label label ? label.Text : control.TooltipText)
                .Any(text => text.Contains("supermassive", StringComparison.OrdinalIgnoreCase) ||
                             text.Contains("black hole", StringComparison.OrdinalIgnoreCase) ||
                             text.Contains("galactic core", StringComparison.OrdinalIgnoreCase));
            Require(!visibleCoreDisclosure, "visible map text or a tooltip disclosed the unexplored galactic core");
            var selectedBeforeCoreClick = _main.UiSelectedSystemId;
            var fleetBeforeCoreClick = _main.UiSelectedFleetId;
            var pointerRevisionBeforeCoreClick = _main.UiPointerCommandRevision;
            var statusBeforeCoreClick = _main.UiStatusMessage;
            await ClickPositionAsync(projectedCore, MouseButton.Left);
            await ClickPositionAsync(projectedCore, MouseButton.Right);
            Check(_main.UiSelectedSystemId == selectedBeforeCoreClick &&
                  _main.UiSelectedFleetId == fleetBeforeCoreClick &&
                  _main.UiPointerCommandRevision == pointerRevisionBeforeCoreClick + 2 &&
                  _main.UiStatusMessage == statusBeforeCoreClick,
                "undiscovered-galactic-core-remains-secret-and-noninteractive");
        }
        else
        {
            Require(_main.UiGalacticCore is null && _main.UiGalacticCoreScreenPosition is null &&
                    _main.UiGalacticCoreScreenRadius == 0,
                "the nearby-star profile invented a galactic-core map object");
        }
        await SaveViewportAsync("14-galaxy-overview.png");
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialRegion"));
        await WaitForCameraAsync();
        Check(SameCamera(baseline, ObserveCamera()) && StarPoint(home).DistanceTo(homeBefore) < 1 &&
            StarPoint(comparisonId).DistanceTo(comparisonBefore) < 1, "galaxy-region-zoom-roundtrip-restores");

        await WheelAsync(true, StarPoint(home));
        await WheelAsync(true, StarPoint(home));
        Require(ObserveCamera().Level == "StellarRegion" && ObserveCamera().Zoom > baseline.Zoom,
            "Zoomed regional view crossed into an unrelated camera level.");
        await SaveViewportAsync("15-zoomed-region.png");
        var dragStart = StarPoint(home);
        var dragEnd = dragStart + new Vector2(42, -27);
        await DragAsync(dragStart, dragEnd, MouseButton.Left);
        await WaitForCameraAsync();
        Require(StarPoint(home).DistanceTo(dragEnd) < 1, "Region camera did not follow a real left drag.");
        await SelectDifferentStarAsync(home);
        await ClickPositionAsync(StarPoint(home), MouseButton.Left);
        Check(_main.UiSelectedSystemId == home, "regional-pan-inverse-hit");
        await VerifyDrawerWheelShieldingAsync(home);
        await VerifyResizedStarPickingAsync(home);

        var regionReturn = ObserveCamera();
        var regionReturnHome = StarPoint(home);
        var regionReturnOther = StarPoint(comparisonId);
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        Require(ObserveCamera().Level == "StarSystem" && _main.UiIsSystemSpatialView,
            "The ordinary Open System button did not enter orbital space.");
        Check(_main.UiGalaxyDeepFieldOpacity == 0,
            "system-and-planet-space-hide-distant-galaxy-backdrop");
        Check(_main.UiSystemBodies.Any(body => body.BodyId == 3 && body.Label == "Earth" &&
                body.MassEarth is > .99 and < 1.01 && body.GravityG is > .99 and < 1.01) &&
            _main.UiSystemBodies.Any(body => body.Label == "Moon" && body.ParentBodyId == 3),
            "system-inspection-exposes-surveyed-world-stats-and-moons");
        var infrastructure = _main.UiSystemInfrastructure;
        Check(infrastructure.Count == 3 &&
            infrastructure.Any(item => item.ProjectId == "orbital_launch_complex" &&
                item.State == SystemSpatialInfrastructureState.Available) &&
            infrastructure.Any(item => item.ProjectId == "orbital_shipyard" &&
                item.State == SystemSpatialInfrastructureState.Locked) &&
            infrastructure.Any(item => item.ProjectId == "asteroid_resource_network" &&
                item.State == SystemSpatialInfrastructureState.Locked),
            "home-orbit-shows-infrastructure-plan");
        await SaveViewportAsync("15b-system-overview.png");
        var asteroidPoint = _main.UiGetInfrastructureScreenPosition("asteroid_resource_network")
            ?? throw new InvalidOperationException("Asteroid Resource Network marker has no screen position.");
        await ClickPositionAsync(asteroidPoint, MouseButton.Left);
        await WaitForRefreshAsync();
        Check(_main.UiSelectedOrbitalConstruction is { CanBuild: false, LockReason: { } lockReason } &&
            lockReason.Contains("Orbital Industry", StringComparison.Ordinal) && lockReason.Contains("Orbital Launch Complex", StringComparison.Ordinal),
            "locked-orbital-infrastructure-explains-requirements");
        await ClickNamedButtonAsync(_main, "CloseOrbitalInspector");
        var launchPoint = _main.UiGetInfrastructureScreenPosition("orbital_launch_complex")
            ?? throw new InvalidOperationException("Launch Complex orbital marker has no screen position.");
        await ClickPositionAsync(launchPoint, MouseButton.Left);
        await WaitForRefreshAsync();
        Check(_main.UiSelectedOrbitalConstruction is { Id: "orbital_launch_complex", CanBuild: true, DaysRemaining: > 0 },
            "orbital-infrastructure-opens-industry");
        Require(Descendants(_main).OfType<Button>().Single(b => b.Name == "BuildOrbitalStructure").IsVisibleInTree(),
            "The orbital site did not expose its contextual construction action.");
        await ClickNamedButtonAsync(_main, "CloseOrbitalInspector");
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left);
        Require(_main.UiSelectedBodyId == 3, "Earth was not selected for the system zoom anchor.");
        await VerifyPlanetInspectorAsync();
        var systemBefore = ObserveCamera();
        var systemEarthBefore = BodyPoint(3);
        var systemMarsBefore = BodyPoint(4);
        await WheelAsync(true, BodyPoint(3));
        var systemWheel = ObserveCamera();
        var systemEarthWheel = BodyPoint(3);
        var systemMarsWheel = BodyPoint(4);
        Require(systemWheel.Zoom > systemBefore.Zoom, "Uncovered system wheel positive control did not zoom.");
        await WheelAsync(false, BodyPoint(3));
        Require(SameCamera(systemBefore, ObserveCamera()), "Opposite system wheel did not restore its transform.");
        await ClickZoomAsync(true);
        Check(SameZoomRoute(systemBefore, systemWheel, systemEarthBefore, systemEarthWheel,
                systemMarsBefore, systemMarsWheel) &&
            ObserveCamera().Level == "StarSystem" && ObserveCamera().Zoom > systemBefore.Zoom,
            "system-wheel-button-zoom-parity");
        await ClickZoomAsync(false);

        await OpenSectionAsync("research");
        var shieldCamera = ObserveCamera();
        var shieldEarth = BodyPoint(3);
        foreach (var inward in new[] { true, false })
        {
            await WheelAsync(inward, ScreenRect(_drawer).Position + new Vector2(4, 74));
            Require(SameCamera(shieldCamera, ObserveCamera()) && BodyPoint(3).DistanceTo(shieldEarth) < 0.1f,
                "A wheel event zoomed orbital space through the research drawer.");
        }
        await CloseDrawerAsync();
        Check(true, "drawer-blocks-camera-wheel");

        var panBefore = ObserveCamera();
        var earthBeforePan = BodyPoint(3);
        await DragAsync(new Vector2(460, 500), new Vector2(492, 476), MouseButton.Left);
        await WaitForCameraAsync();
        var panAfter = ObserveCamera();
        Require(panAfter.TargetPan.DistanceTo(panBefore.TargetPan) > .01f &&
            BodyPoint(3).DistanceTo(earthBeforePan) > 1,
            "System overview camera did not follow the real left drag.");
        var revision = _main.UiPointerCommandRevision;
        await ClickPositionAsync(BodyPoint(4), MouseButton.Left);
        Require(_main.UiSelectedBodyId == 4, "Mars inverse-hit positive control did not change selection.");
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left);
        Check(_main.UiSelectedBodyId == 3 && _main.UiPointerCommandRevision == revision, "system-pan-inverse-hit");
        await VerifyResizedBodyPickingAsync();
        var focusReturn = ObserveCamera();
        var focusReturnEarth = BodyPoint(3);
        var focusReturnMars = BodyPoint(4);
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Check(ObserveCamera().Level == "PlanetFocus" && ObserveCamera().FocusedBodyId == 3 &&
            _main.UiSelectedBodyId == 3 && _main.UiPointerCommandRevision == revision, "planet-focus-by-real-double-click");
        await SaveViewportAsync("16-earth-focus.png");
        await VerifyFocusedMenuShieldingAsync();
        var spatialBack = Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialBack");
        await ClickControlAsync(spatialBack);
        await WaitForCameraAsync();
        Check(SameCamera(focusReturn, ObserveCamera()) && BodyPoint(3).DistanceTo(focusReturnEarth) < 1 &&
            BodyPoint(4).DistanceTo(focusReturnMars) < 1, "planet-focus-back-restores-system-camera");
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialPlanet"));
        await WaitForCameraAsync();
        Require(ObserveCamera().Level == "PlanetFocus" && ObserveCamera().FocusedBodyId == 3,
            "The visible planet breadcrumb did not share double-click's focus route.");
        // Focused perspective zoom has a long approach range; one wheel notch
        // no longer crosses the focus-exit boundary. Exercise the real route
        // until the camera exits focus, then verify the saved orbital pose.
        for (var step = 0; ObserveCamera().Level == "PlanetFocus"; step++)
        {
            Require(step < 24, "Wheel out never exited focused planetary orbit.");
            await WheelAsync(false, BodyPoint(3));
        }
        Check(SameCamera(focusReturn, ObserveCamera()) && BodyPoint(3).DistanceTo(focusReturnEarth) < 1,
            "planet-wheel-button-route-parity");
        for (var step = 0; step < 8; step++)
            await WheelAsync(true, BodyPoint(3));
        Check(ObserveCamera().Level == "StarSystem" && ObserveCamera().FocusedBodyId is null &&
            ObserveCamera().Zoom > focusReturn.Zoom * 4, "system-wheel-keeps-optional-planet-focus");
        await WheelAsync(false, BodyPoint(3));
        await WaitForCameraAsync();
        Require(_main.UiCachedPlanetMaterialCount > 0 &&
            _main.UiSystemBodies.Any(body => body.BodyId == 3 && body.SurfaceKey == "earth" && body.HasDetailedEnvironment),
            "Known Earth material positive control is missing from the observer-safe presentation.");
        await ClickButtonAsync(_dock, "Back to Region");
        await WaitForCameraAsync();
        Check(SameCamera(regionReturn, ObserveCamera()) && StarPoint(home).DistanceTo(regionReturnHome) < 1 &&
            StarPoint(comparisonId).DistanceTo(regionReturnOther) < 1, "system-back-restores-region-camera");

        // Wheel navigation must cross the same entry boundary, then restore the last regional camera.
        CameraObservation beforeEntry = ObserveCamera();
        for (var step = 0; !_main.UiIsSystemSpatialView; step++)
        {
            Require(step < 20, "Wheel zoom never entered the selected known star's system.");
            beforeEntry = ObserveCamera();
            await WheelAsync(true, StarPoint(home));
        }
        Require(_main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas").IsStarFocused,
            "Close regional wheel approach did not retain a detailed stellar view.");
        await ClickButtonAsync(_dock, "Back to Region");
        await WaitForCameraAsync();
        Check(SameCamera(beforeEntry, ObserveCamera()), "wheel-enters-system-and-restores-region");
        await VerifyUnknownEntryPrivacyAsync(home);
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        Check(_sawSmoothCameraTransition, "camera-transitions-settle-smoothly");
        Require(SameCamera(baseline, ObserveCamera()) && _main.UiSelectedSystemId == home && _main.UiIsPaused,
            "Camera probes failed to restore the ordinary campaign for the existing acceptance checks.");
    }

    private async Task VerifyFocusedCameraJourneyAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        if (!_main.UiIsPaused) await PressKeyAsync(Key.Space);
        await ClickButtonAsync(_dock, "Home");
        await WaitForRefreshAsync();
        await VerifyCameraJourneyAsync();
    }

    private int[] PublicCatalogIds() => _main.UiSpatialCatalog.Select(system => system.SystemId).ToArray();

    private bool FullGalaxyArtworkFits() => Encloses(GetViewport().GetVisibleRect(), _main.UiGalaxyArtworkScreenRect);

    private bool PublicCatalogFits()
    {
        var mapBounds = new Rect2(112, 170, GetViewport().GetVisibleRect().Size.X - 412,
            GetViewport().GetVisibleRect().Size.Y - 202);
        var ids = PublicCatalogIds();
        return ids.Length > 1 && ids.All(id => mapBounds.HasPoint(StarPoint(id)));
    }

    private async Task SelectDifferentStarAsync(int target)
    {
        var mapBounds = new Rect2(125, 152, GetViewport().GetVisibleRect().Size.X - 445,
            GetViewport().GetVisibleRect().Size.Y - 300);
        // The persistent first-colony guide is interactive at every map scale.
        // Pick a genuinely exposed star, rather than clicking through a guide button.
        var buttonBounds = Descendants(_main).OfType<Button>()
            .Where(button => button.IsVisibleInTree())
            .Select(button => ScreenRect(button).Grow(8)).ToArray();
        var other = PublicCatalogIds().Where(id => id != target).Select(id => (Id: id, Point: StarPoint(id)))
            .First(candidate => mapBounds.HasPoint(candidate.Point) &&
                candidate.Point.DistanceTo(StarPoint(target)) > 35 &&
                !buttonBounds.Any(bounds => bounds.HasPoint(candidate.Point)));
        await ClickPositionAsync(other.Point, MouseButton.Left);
        Require(_main.UiSelectedSystemId == other.Id, "Alternate catalog star positive control did not change selection.");
    }

    private async Task VerifyDrawerWheelShieldingAsync(int home)
    {
        await OpenSectionAsync("research");
        await WaitForCameraAsync();
        var workspace = ActivePanel() as ResearchWorkspaceView
            ?? throw new InvalidOperationException("Research navigation did not open the fullscreen workspace.");
        var camera = ObserveCamera();
        var homePoint = StarPoint(home);
        var tab = Descendants(workspace).OfType<Button>().Single(button => button.Name == "ResearchTab_ENGINEERING");
        var inspector = Descendants(workspace).OfType<Control>().Single(control => control.Name == "ResearchInspector");
        foreach (var covered in new[] { ScreenRect(tab).GetCenter(), ScreenRect(inspector).GetCenter() })
        {
            foreach (var inward in new[] { true, false })
            {
                await WheelAsync(inward, covered);
                Require(SameCamera(camera, ObserveCamera()) && StarPoint(home).DistanceTo(homePoint) < 0.1f,
                    "A wheel event zoomed the map through the research workspace.");
            }
        }
        var graph = Descendants(workspace).OfType<Control>().Single(control => control.Name == "ResearchGraph");
        var graphZoom = workspace.GraphZoom;
        await WheelAsync(true, ScreenRect(graph).GetCenter());
        Require(workspace.GraphZoom > graphZoom && SameCamera(camera, ObserveCamera()) &&
                StarPoint(home).DistanceTo(homePoint) < 0.1f,
            "Research graph wheel did not stay inside the graph camera.");
        Check(true, "research-workspace-wheel-shields-tabs-graph-and-inspector");
        await CloseDrawerAsync();
    }

    private async Task VerifyFocusedMenuShieldingAsync()
    {
        var camera = ObserveCamera();
        await OpenSectionAsync("menu");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        Require(_main.UiIsMenuOpen, "Focused-planet menu did not open through its ordinary controls.");
        foreach (var button in new[] { MouseButton.WheelUp, MouseButton.WheelDown })
        {
            await ClickPositionAsync(new Vector2(220, 380), button);
            Require(Equals(camera, ObserveCamera()), "The menu allowed a focused-planet wheel gesture.");
        }
        await DragAsync(new Vector2(220, 380), new Vector2(250, 410));
        Check(Equals(camera, ObserveCamera()) && _main.UiSelectedBodyId == 3 && _main.UiIsMenuOpen,
            "focused-menu-blocks-camera");
        await ClickNamedButtonAsync(_main.GetNode<Godot.CanvasLayer>("MainMenuLayer"), "ResumeCampaign");
        if (!_main.UiIsPaused) await PressKeyAsync(Key.Space);
        await CloseDrawerAsync();
        Require(SameCamera(camera, ObserveCamera()) && _main.UiIsPaused,
            "Continue changed the focused camera or could not restore the paused acceptance campaign.");
    }

    private async Task AtLargerViewportAsync(Func<Task> verify)
    {
        var window = GetWindow();
        var previousSize = window.Size;
        var previousContentScale = window.ContentScaleSize;
        try
        {
            window.ContentScaleSize = new Vector2I(1600, 900);
            window.Size = new Vector2I(1600, 900);
            await WaitFramesAsync(12);
            await WaitForCameraAsync();
            Require(GetViewport().GetVisibleRect().Size == new Vector2(1600, 900),
                "Resize probe did not change the logical drawing and picking viewport.");
            await verify();
        }
        finally
        {
            window.ContentScaleSize = previousContentScale;
            window.Size = previousSize;
            await WaitFramesAsync(12);
            await WaitForCameraAsync();
        }
        Require(GetViewport().GetVisibleRect().Size == new Vector2(1280, 720),
            "Resize probe did not restore the minimum supported viewport.");
    }

    private async Task VerifyResizedStarPickingAsync(int home)
    {
        var previousPoint = StarPoint(home);
        await AtLargerViewportAsync(async () =>
        {
            await SelectDifferentStarAsync(home);
            await ClickPositionAsync(StarPoint(home), MouseButton.Left);
            Check(_main.UiSelectedSystemId == home && !_main.UiIsSystemSpatialView, "resize-preserves-star-hit");
        });
        Require(StarPoint(home).DistanceTo(previousPoint) < 1, "Resizing back shifted the regional selection's camera.");
    }

    private async Task VerifyResizedBodyPickingAsync()
    {
        var previousPoint = BodyPoint(3);
        var revision = _main.UiPointerCommandRevision;
        await AtLargerViewportAsync(async () =>
        {
            await ClickPositionAsync(BodyPoint(4), MouseButton.Left);
            Require(_main.UiSelectedBodyId == 4, "Resized Mars positive control did not change selection.");
            await ClickPositionAsync(BodyPoint(3), MouseButton.Left);
            Check(_main.UiSelectedBodyId == 3 && _main.UiPointerCommandRevision == revision, "resize-preserves-body-hit");
        });
        Require(BodyPoint(3).DistanceTo(previousPoint) < 1, "Resizing back shifted the system selection's camera.");
        var railScroll = _main.GetNode<ScrollContainer>("CampaignSidebar/NavigationRail/NavigationScroll");
        Require(railScroll.ScrollVertical == 0,
            $"Navigation rail retained a {railScroll.ScrollVertical}px vertical scroll at the minimum viewport.");
        foreach (var button in Descendants(railScroll).OfType<Button>())
            Require(Encloses(ScreenRect(railScroll), ScreenRect(button)),
                $"Navigation button {button.Name} escaped the minimum rail: rail={ScreenRect(railScroll)}, button={ScreenRect(button)}.");
        Require(Encloses(GetViewport().GetVisibleRect(), ScreenRect(_dock)),
            $"Action dock escaped the restored minimum viewport: dock={ScreenRect(_dock)}.");
        Check(true, "resize-restores-minimum-layout");
    }

    private async Task VerifyUnknownEntryPrivacyAsync(int home, string checkSuffix = "")
    {
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        var mapBounds = new Rect2(125, 170, 825, 402);
        var catalog = _main.UiSpatialCatalog;
        var unknown = catalog.First(system => system.SystemId != home && system.SurveyLevel == SystemSurveyLevel.Unknown &&
            mapBounds.HasPoint(StarPoint(system.SystemId)) && catalog.Where(other => other.SystemId != system.SystemId)
                .All(other => StarPoint(other.SystemId).DistanceTo(StarPoint(system.SystemId)) > 18));
        await ClickPositionAsync(StarPoint(unknown.SystemId), MouseButton.Left);
        Require(_main.UiSelectedSystemId == unknown.SystemId, "Unknown-star pointer selection failed.");
        Require(_main.UiSelectedSystemIntelligence.Facts.Length == 1 &&
                _main.UiSelectedSystemIntelligence.Facts[0].Label == "DISTANCE FROM HOMEWORLD",
            "An unknown star must expose only its distance reference, not private surveyed facts.");
        Require(_main.UiSelectedSystemIntelligence.Name == "UNKNOWN" &&
                _main.UiSelectedSystemIntelligence.SurveyStatus == "Unknown" &&
                _main.UiSelectedSystemInspection.StartsWith("Unknown\nStatus: Unknown\n", StringComparison.Ordinal) &&
                _main.UiDashboard.SelectedSystemName == "Unknown",
            "Unknown-star map, inspector, header, or status text exposed a synthetic catalogue identity.");
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        Require(!_main.UiIsSystemSpatialView, "Open System exposed an unreconnoitred system.");
        await ClickPositionAsync(StarPoint(unknown.SystemId), MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Require(!_main.UiIsSystemSpatialView, "Double-click exposed an unreconnoitred system.");
        for (var step = 0; step < 40; step++)
        {
            var before = ObserveCamera();
            await WheelAsync(true, StarPoint(unknown.SystemId));
            Require(!_main.UiIsSystemSpatialView, "Wheel entry exposed an unreconnoitred system.");
            if (Math.Abs(before.Zoom - ObserveCamera().Zoom) < 0.00001f) break;
            Require(step < 39, "Unknown-star zoom failed to reach its safe camera limit.");
        }
        Check(_main.UiSelectedSystemId == unknown.SystemId && !_main.UiIsSystemSpatialView &&
            ObserveCamera().FocusedBodyId is null, "unknown-system-entry-preserves-privacy" + checkSuffix);
        Check(_main.UiSystemBodies.Count == 0 && _main.UiCachedPlanetMaterialCount == 0 && _main.UiSelectedBodyId is null &&
            _main.UiGetBodyLabel(3) is null && _main.UiGetBodyScreenPosition(3) is null,
            "unknown-body-materials-redacted" + checkSuffix);
    }
}
