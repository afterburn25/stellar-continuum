using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task<string> VerifySurfaceJourneyAsync(string normalSave, string normalSaveHash)
    {
        await CloseDrawerAsync();
        var surface = await LandOnEarthAsync();
        var initial = _main.UiCurrentSurface ?? throw new InvalidOperationException("Surface has no owned-colony snapshot.");
        Check(initial.BodyId == 3 && initial.PlanetName == "Earth" && initial.Buildings.Count == 0 &&
            _main.UiIsSurfaceOpen, "earth-surface-opens-from-real-breadcrumb");
        Check(initial.SurfaceVisualClass == "temperate" && initial.RequiredHabitatSystems == 0 &&
            surface.SurfaceVisualClass == "temperate" && surface.SettlementVisualParts > 20 &&
            surface.AmbientShuttleCount >= 2 && surface.HighRiseCount >= 6 && surface.DistrictRingRoadCount == 2 &&
            surface.DistrictPlazaCount == 6,
            "surface-world-palette-from-environment");
        foreach (var button in Descendants(surface).OfType<Button>().Where(button => button.IsVisibleInTree()))
        {
            await RevealControlAsync(button);
            AssertInsideViewport(button, "surface " + button.Name);
        }
        var buildPalette = ScreenRect(surface.GetNode<Control>("SurfaceBuildPalette"));
        Check(buildPalette.Size.X <= 360 && buildPalette.Position.X >= 900 &&
            !Descendants(surface).OfType<GridContainer>().Any(g => g.IsVisibleInTree()),
            "surface-build-palette-collapses-by-default");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuildPaletteToggle"));
        await WaitForRefreshAsync();
        buildPalette = ScreenRect(surface.GetNode<Control>("SurfaceBuildPalette"));
        Check(buildPalette.Size.X <= 360 && buildPalette.Position.X >= 900 && buildPalette.End.Y <= 720,
            "surface-build-palette-preserves-world-view");
        Check(true, "surface-controls-fit-1280x720");
        var surfacePlayback = SurfaceButton(surface, "SurfacePlaybackButton");
        var surfacePlaybackSpeed = SurfaceButton(surface, "SurfacePlaybackSpeedButton");
        Check(surfacePlayback.IsVisibleInTree() && surfacePlaybackSpeed.IsVisibleInTree() && Descendants(surface).OfType<Label>()
                .Any(label => label.Name == "SurfacePlaybackState" && label.IsVisibleInTree()),
            "surface-compact-playback-visible");
        await ToggleSurfacePlaybackAsync(surface);
        Require(_main.UiIsPaused, "Surface Pause did not stop the real campaign.");
        var revision = _main.UiPointerCommandRevision;
        var camera = surface.CameraPosition;
        await DragAsync(new Vector2(350, 330), new Vector2(388, 344), MouseButton.Left);
        Require(surface.CameraPosition.DistanceTo(camera) > 1, "Surface left drag did not move its 3D camera.");
        var beforeZoom = surface.CameraPosition;
        await ClickPositionAsync(new Vector2(350, 330), MouseButton.WheelUp);
        Require(surface.CameraPosition.DistanceTo(beforeZoom) > 1, "Surface wheel positive control did not zoom.");
        await ClickPositionAsync(new Vector2(350, 330), MouseButton.WheelDown);
        Require(surface.CameraPosition.DistanceTo(beforeZoom) < 0.1f, "Surface reciprocal wheel lost its prior camera distance.");
        var hudPoint = ScreenRect(surface.GetNode<Control>("SurfaceHeader")).Position + new Vector2(4, 4);
        await ClickPositionAsync(hudPoint, MouseButton.WheelUp);
        Check(surface.CameraPosition.DistanceTo(beforeZoom) < 0.1f && _main.UiPointerCommandRevision == revision,
            "surface-camera-input-and-hud-shielding");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceCenterHub"));

        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuild_science_lab"));
        var labGround = await FindValidSurfacePointAsync(surface);
        Check(surface.SelectedBuildingType == "science_lab" && surface.HasGroundPreview && surface.PlacementErrorText is null,
            "surface-valid-free-placement-preview");
        await SaveViewportAsync("17-surface-placement.png");
        var industryBefore = _main.UiCurrentSurface!.Industry;
        await ClickPositionAsync(labGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        var afterLab = _main.UiCurrentSurface!;
        var lab = afterLab.Buildings.Single();
        Check(lab.TypeId == "science_lab" && Math.Abs(lab.X - labGround.X) < 0.1f && Math.Abs(lab.Z - labGround.Z) < 0.1f &&
            lab.Progress == 0 && !lab.Complete && !lab.Powered && afterLab.Industry == industryBefore,
            "surface-real-ground-click-places-unfunded-site");
        await ClickPositionAsync(labGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.Buildings.Count == 1 && _main.UiCurrentSurface.Industry == industryBefore &&
            surface.PlacementErrorText?.Contains("overlap", StringComparison.OrdinalIgnoreCase) == true,
            "surface-collision-rejected-without-charge");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceCancel"));
        var creditsBeforeCancel = _main.UiCurrentSurface!.Credits;
        await ClickPositionAsync(labGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        var remove = SurfaceButton(surface, "SurfaceRemove");
        Require(remove.IsVisibleInTree() && remove.Text == "Cancel site",
            "Clicking the unfinished 3D lab did not expose its cancellation action.");
        await ClickControlAsync(remove);
        await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.Buildings.Count == 0 &&
            Math.Abs(_main.UiCurrentSurface.Credits - (creditsBeforeCancel + 20)) < 0.001,
            "surface-building-selection-and-cancellation");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuild_science_lab"));
        labGround = await FindValidSurfacePointAsync(surface);
        await ClickPositionAsync(labGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuild_power_generator"));
        await ClickControlAsync(SurfaceButton(surface, "SurfaceRotate"));
        var generatorGround = await FindValidSurfacePointAsync(surface);
        await ClickPositionAsync(generatorGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        var placed = _main.UiCurrentSurface!;
        Require(placed.Buildings.Count == 2 && placed.Buildings.Any(building => building.TypeId == "power_generator" &&
            Math.Abs(building.X - generatorGround.X) < 0.1f && Math.Abs(building.Z - generatorGround.Z) < 0.1f &&
            building.RotationDegrees == 15) && placed.Buildings.All(building => building.Progress == 0) &&
            placed.Industry == industryBefore && _main.UiPointerCommandRevision == revision,
            "The rotated generator placement leaked input, snapped coordinates, or advanced while paused.");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuild_trade_hub"));
        var tradeGround = await FindValidSurfacePointAsync(surface);
        await ClickPositionAsync(tradeGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        placed = _main.UiCurrentSurface!;
        Check(placed.Buildings.Count == 3 && placed.Buildings.Any(building => building.TypeId == "trade_hub" &&
            Math.Abs(building.X - tradeGround.X) < 0.1f && Math.Abs(building.Z - tradeGround.Z) < 0.1f),
            "surface-trade-hub-placed-through-real-palette");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceCancel"));
        await ClickPositionAsync(tradeGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        await ClickControlAsync(SurfaceButton(surface, "SurfaceRemove"));
        await WaitForRefreshAsync();
        placed = _main.UiCurrentSurface!;
        Require(placed.Buildings.Count == 2 && placed.Buildings.All(building => building.TypeId != "trade_hub"),
            "The temporary trade-hub placement could not be cancelled through its visible surface action.");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceSave"));
        Check(File.Exists(ProjectSettings.GlobalizePath("user://saves/developer-autosave.json")) &&
            HashFile(normalSave) == normalSaveHash, "surface-save-keeps-normal-campaign-separate");
        // Establish an observable partial state at 1x before any running-speed control
        // exercise. On slow hosted frames, starting at 2x or 8x can legitimately advance
        // both sites from zero to complete between two presentation samples.
        await SetSurfacePlaybackSpeedAsync(surface, SimulationClock.SpeedLevel.Normal);
        var progressSampleStarted = Time.GetTicksMsec();
        while (!_main.UiCurrentSurface!.Buildings.Any(building => building.Progress is > 0 and < 1))
        {
            Require(!_main.UiCurrentSurface.Buildings.All(building => building.Complete),
                "Ordinary construction completed before exposing any intermediate progress at 1x.");
            Require(Time.GetTicksMsec() - progressSampleStarted < 30000,
                "Ordinary 1x construction did not expose bounded intermediate progress.");
            await ToSignal(GetTree().CreateTimer(.1), SceneTreeTimer.SignalName.Timeout);
        }
        var sawIncompleteProgress = true;
        await ToggleSurfacePlaybackAsync(surface);
        Require(_main.UiIsPaused, "Surface Pause did not stop construction after the paced progress sample.");
        // Exercise pause/resume from a real non-normal Player speed before watching
        // ordinary 8x construction. This preserves the player's selected speed instead
        // of assuming every pause begins and ends at Normal.
        await SetSurfacePlaybackSpeedAsync(surface, SimulationClock.SpeedLevel.Fast);
        var expectedResumeSpeed = _main.UiCurrentSpeed;
        Require(!_main.UiIsPaused && expectedResumeSpeed == SimulationClock.SpeedLevel.Fast,
            $"The visible surface 2x control did not select a running ordinary speed: {expectedResumeSpeed}.");
        await ToggleSurfacePlaybackAsync(surface);
        Require(_main.UiIsPaused, "Surface Pause did not pause after selecting ordinary 2x.");
        Require(Descendants(surface).OfType<Label>().Single(label => label.Name == "SurfacePlaybackState").Text == "PAUSED",
            "Paused surface playback did not display its stopped state.");
        await ToggleSurfacePlaybackAsync(surface);
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == expectedResumeSpeed,
            $"Surface Pause did not restore the selected ordinary speed: expected {expectedResumeSpeed}, actual {_main.UiCurrentSpeed}.");
        await SetSurfacePlaybackSpeedAsync(surface, SimulationClock.SpeedLevel.Maximum);
        Require(_main.UiCurrentSpeed == SimulationClock.SpeedLevel.Maximum,
            "The visible surface 8x control did not select ordinary maximum speed.");
        var started = Time.GetTicksMsec();
        var clock = (SimulationClock)typeof(Main).GetField("_clock",
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(_main)!;
        var startedDay = clock.SimulationDays;
        var nextProgressLog = started + 20000;
        while (true)
        {
            var current = _main.UiCurrentSurface ?? throw new InvalidOperationException("Surface closed during ordinary construction.");
            Require(current.Buildings.Count == 2, "Ordinary construction lost or duplicated a placed site.");
            if (current.Buildings.All(building => building.Complete && building.Powered)) break;
            if (Time.GetTicksMsec() >= nextProgressLog)
            {
                GD.Print("STELLAR_SURFACE_PROGRESS " + System.Text.Json.JsonSerializer.Serialize(new
                {
                    realElapsedSeconds = (Time.GetTicksMsec() - started) / 1000.0,
                    simulationDays = clock.SimulationDays, advancedDays = clock.SimulationDays - startedDay,
                    effectiveMultiplier = clock.EffectiveMultiplier, backlogDays = clock.BacklogDays,
                    materials = current.Industry, materialProductionPerDay = _main.UiDashboard.IndustryPerDay,
                    funding = current.BaseOperationsFundingFraction, orders = _main.UiConstructionOrders,
                    buildings = current.Buildings.Select(b => new { b.Id, b.TypeId, b.Progress, b.Complete,
                        b.Powered, b.Staffed, b.RemainingConstructionMaterials }),
                }));
                nextProgressLog = Time.GetTicksMsec() + 20000;
            }
            Require(Time.GetTicksMsec() - started < 90000,
                $"Ordinary surface construction failed to complete: advanced {clock.SimulationDays - startedDay:0.0} days, " +
                $"{current.Industry:0.0} stored materials, {_main.UiDashboard.IndustryPerDay:0.00}/day production; see STELLAR_SURFACE_PROGRESS.");
            await ToSignal(GetTree().CreateTimer(0.25), SceneTreeTimer.SignalName.Timeout);
        }
        while (_main.UiCurrentSurface!.Industry < 320)
        {
            Require(Time.GetTicksMsec() - started < 120000,
                "Ordinary industry production did not fund the surface upgrade within the bounded rendering run.");
            await ToSignal(GetTree().CreateTimer(0.25), SceneTreeTimer.SignalName.Timeout);
        }
        await ToggleSurfacePlaybackAsync(surface);
        var labPoint = surface.GetSurfaceScreenPosition(labGround.X, labGround.Z)
            ?? throw new InvalidOperationException("Completed lab was outside the surface camera.");
        await ClickPositionAsync(labPoint, MouseButton.Left);
        await WaitForRefreshAsync();
        var upgrade = SurfaceButton(surface, "SurfaceUpgrade");
        Require(upgrade.IsVisibleInTree() && !upgrade.Disabled,
            "Selecting the completed lab did not expose an affordable upgrade action.");
        await ClickControlAsync(upgrade);
        await WaitForRefreshAsync();
        Require(_main.UiCurrentSurface!.Buildings.Single(building => building.Id == lab.Id) is
            { TypeId: "science_lab", UpgradeDaysRemaining: > 0 }, "Upgrade granted output immediately instead of starting timed work.");
        await SetSurfacePlaybackSpeedAsync(surface, SimulationClock.SpeedLevel.Maximum);
        started = Time.GetTicksMsec();
        while (_main.UiCurrentSurface!.Buildings.Single(building => building.Id == lab.Id).UpgradeDaysRemaining > 0)
        {
            Require(Time.GetTicksMsec() - started < 30000, "Timed lab upgrade failed to finish under ordinary simulation.");
            await ToSignal(GetTree().CreateTimer(.25), SceneTreeTimer.SignalName.Timeout);
        }
        await ToggleSurfacePlaybackAsync(surface);
        // The surface HUD samples the read model every 150 ms. Allow that normal
        // refresh before comparing its text with the just-completed upgrade.
        await WaitForRefreshAsync();
        var complete = _main.UiCurrentSurface!;
        Check(complete.Buildings.Single(building => building.Id == lab.Id).TypeId == "advanced_science_lab" &&
            !SurfaceButton(surface, "SurfaceUpgrade").IsVisibleInTree(),
            "surface-building-upgrade-through-real-selection");
        var allCompleteAndPowered = complete.Buildings.All(building => building.Complete && building.Powered && building.Progress == 1);
        var placementPreserved = complete.Buildings.All(building => placed.Buildings.Any(old => old.Id == building.Id &&
            old.X == building.X && old.Z == building.Z && old.RotationDegrees == building.RotationDegrees));
        Require(sawIncompleteProgress && _main.UiIsPaused && complete.PowerSupply >= complete.PowerDemand &&
                allCompleteAndPowered && placementPreserved,
            $"Ordinary surface completion mismatch: sampledPartial={sawIncompleteProgress}, paused={_main.UiIsPaused}, " +
            $"power={complete.PowerSupply:0.###}/{complete.PowerDemand:0.###}, completePowered={allCompleteAndPowered}, " +
            $"placementPreserved={placementPreserved}, buildings=[{string.Join("; ", complete.Buildings.Select(building =>
                $"{building.Id}:{building.TypeId}:progress={building.Progress:R}:complete={building.Complete}:powered={building.Powered}"))}].");
        Check(true, "surface-ordinary-progress-completes-powered-buildings");
        var production = Descendants(surface).OfType<Label>().Single(label => label.Name == "SurfaceProduction");
        Check(complete.SciencePerDay == 2.5 && complete.IndustryPerDay == 0 && complete.CreditsPerDay == 0 &&
            complete.UpkeepCreditsPerDay == .10 &&
            complete.SpecializationName == "Research district" && complete.SpecializationDescription.Contains("1/3", StringComparison.Ordinal) &&
            production.IsVisibleInTree() && production.Text.Contains("+2.5 labs", StringComparison.Ordinal) &&
            production.Text.Contains(complete.Currency.FormatRate(0), StringComparison.Ordinal) &&
            production.Text.Contains(complete.Currency.FormatRate(-complete.UpkeepCreditsPerDay), StringComparison.Ordinal),
            "surface-output-visible-and-authoritative");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceCenterHub"));
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuildPaletteToggle"));
        await WaitForRefreshAsync();
        await SaveViewportAsync("18-surface-colony.png");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceSave"));
        Require(HashFile(normalSave) == normalSaveHash, "Completed surface save changed the normal campaign.");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBack"));
        Check(!_main.UiIsSurfaceOpen && ObserveCamera().Level == "PlanetFocus" && _main.UiSelectedBodyId == 3 &&
            _main.UiPointerCommandRevision == revision, "surface-back-restores-orbit-without-map-input");
        await ClickButtonAsync(_dock, "Back to Region");
        await WaitForCameraAsync();
        await OpenSectionAsync("menu");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        normalSaveHash = await ReloadDeveloperThroughPlayerAsync(normalSave, normalSaveHash);
        await CloseDrawerAsync();
        surface = await LandOnEarthAsync();
        var reloaded = _main.UiCurrentSurface!;
        Check(reloaded.Buildings.Count == complete.Buildings.Count && reloaded.Buildings.All(building =>
            complete.Buildings.Any(old => old.Id == building.Id && old.TypeId == building.TypeId && old.X == building.X &&
                old.Z == building.Z && old.RotationDegrees == building.RotationDegrees && building.Complete && building.Powered)) &&
            HashFile(normalSave) == normalSaveHash, "surface-real-save-reload-retains-buildings");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBack"));
        await OpenSectionAsync("colonies");
        var mars = _main.UiOwnedColonies.Single(world => world.PlanetName == "Mars");
        var landButtons = Descendants(ActivePanel()).OfType<Button>().Where(button => button.Text == "Land").ToArray();
        Require(landButtons.Length == 3, "The colony page did not expose each starting surface destination.");
        await ClickControlAsync(landButtons[2]);
        await WaitForRefreshAsync();
        var marsSurface = _main.UiCurrentSurface ?? throw new InvalidOperationException("Mars surface did not open.");
        Check(_main.UiIsSurfaceOpen && marsSurface.ColonyId == mars.ColonyId && marsSurface.PlanetName == "Mars" &&
            marsSurface.RequiredHabitatSystems > 0 && marsSurface.SurfaceVisualClass == "rocky" &&
            surface.SurfaceVisualClass == "rocky" && surface.SettlementVisualParts == 0 && surface.HighRiseCount == 0,
            "mars-small-settlement-opens-without-invented-city");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuildPaletteToggle"));
        await WaitForRefreshAsync();
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBuild_habitat_complex"));
        var habitatGround = await FindValidSurfacePointAsync(surface);
        await ClickPositionAsync(habitatGround.Screen, MouseButton.Left);
        await WaitForRefreshAsync();
        marsSurface = _main.UiCurrentSurface!;
        var marsOverview = _main.UiOwnedColonies.Single(world => world.PlanetName == "Mars");
        Check(marsSurface.Buildings.Any(building => building.TypeId == "habitat_complex") &&
            marsOverview.BuildingCount == 1 &&
            Math.Abs(marsOverview.HabitatSupportReduction - marsSurface.HabitatSupportReduction) < 0.001 &&
            marsOverview.HabitatSupportCreditsPerDay <= marsOverview.GrossHabitatSupportCreditsPerDay,
            "mars-habitat-placed-through-real-build-menu");
        await SaveViewportAsync("21-mars-surface.png");
        await ClickControlAsync(SurfaceButton(surface, "SurfaceBack"));
        return normalSaveHash;
    }

    private async Task<PlanetSurfaceView> LandOnEarthAsync()
    {
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialSurface"));
        await WaitForRefreshAsync();
        Require(_main.UiIsSurfaceOpen, "The visible Surface breadcrumb did not open owned Earth terrain.");
        return _main.GetNode<PlanetSurfaceView>("PlanetSurfaceLayer/PlanetSurfaceView");
    }

    private static Button SurfaceButton(PlanetSurfaceView surface, string name) =>
        Descendants(surface).OfType<Button>().Single(button => button.Name == name);

    private async Task ToggleSurfacePlaybackAsync(PlanetSurfaceView surface) =>
        await ClickControlAsync(SurfaceButton(surface, "SurfacePlaybackButton"));

    private async Task SetSurfacePlaybackSpeedAsync(PlanetSurfaceView surface, SimulationClock.SpeedLevel target)
    {
        for (var attempt = 0; attempt < 7; attempt++)
        {
            if (!_main.UiIsPaused && _main.UiCurrentSpeed == target) return;
            if (_main.UiIsPaused && _main.UiResumeSpeed == target)
                await ClickControlAsync(SurfaceButton(surface, "SurfacePlaybackButton"));
            else
                await ClickControlAsync(SurfaceButton(surface, "SurfacePlaybackSpeedButton"));
            await WaitForRefreshAsync();
        }
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == target, $"Surface playback could not select {target} through its visible cycle.");
    }

    private async Task<(float X, float Z, Vector2 Screen)> FindValidSurfacePointAsync(PlanetSurfaceView surface)
    {
        // Candidate points use fractional metres; the actual terrain ray decides the placement.
        var clearGround = new Rect2(70, 165, 815, 480);
        var existing = _main.UiCurrentSurface!.Buildings;
        var candidates = from x in Enumerable.Range(-4, 9)
                         from z in Enumerable.Range(-4, 9)
                         let px = x * 25.3f + 0.375f
                         let pz = z * 25.3f + 0.125f
                         let screen = surface.GetSurfaceScreenPosition(px, pz)
                         where px * px + pz * pz > 55 * 55 && screen.HasValue && clearGround.HasPoint(screen.Value)
                         where existing.All(building => new Vector2(building.X - px, building.Z - pz).Length() > 70)
                         orderby screen.GetValueOrDefault().DistanceTo(new Vector2(640, 340))
                         select (X: px, Z: pz, Screen: screen.GetValueOrDefault());
        foreach (var candidate in candidates)
        {
            InjectPointerEvent(new InputEventMouseMotion { Position = candidate.Screen, GlobalPosition = candidate.Screen });
            _mouseActions++;
            GD.Print($"STELLAR_MOUSE_INPUT SurfacePreview {candidate.Screen.X:0.0},{candidate.Screen.Y:0.0}");
            await WaitFramesAsync(3);
            if (surface.HasGroundPreview && surface.PlacementErrorText is null) return candidate;
        }
        throw new InvalidOperationException("No visible valid free ground placement was reachable through the real camera.");
    }
}
