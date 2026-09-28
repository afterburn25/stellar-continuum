using System;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation.Construction;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyImmersiveVisualsAsync()
    {
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        await ClickNamedButtonAsync(menu, "NewPlayerCampaign");
        await ClickNamedButtonAsync(menu, "SandboxCampaignOption");
        await ClickNamedButtonAsync(menu, "StartConfiguredSandbox");
        await SaveViewportAsync("immersive-01-confirmation.png", 0, 0);
        await PressKeyAsync(Key.Escape);
        await ClickNamedButtonAsync(menu, "SandboxSetupBack");
        await ClickNamedButtonAsync(menu, "NewGameBack");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await ClickControlAsync(Descendants(_main).OfType<Button>().Single(b => b.Name == "SpatialOverview"));
        await WaitForCameraAsync();
        await SaveViewportAsync("immersive-02-galaxy.png", 0, 0);
        if (System.Environment.GetEnvironmentVariable("STELLAR_IMMERSIVE_GALAXY_ONLY") == "1") return;
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        var count = _main.UiSystemMeshBodyCount;
        foreach (var c in Descendants(_main.GetNode("SystemSpatialCanvas")).OfType<Control>())
            if (c is TextureRect || c.Name == "SystemScene3D") GD.Print($"SCENE_LAYOUT {c.Name} {c.Size} visible={c.IsVisibleInTree()}");
        Require(count > 8, "Sol must render planets and their moons as real 3D bodies.");
        await SaveViewportAsync("immersive-03-system.png", 0, 0);
        if (System.Environment.GetEnvironmentVariable("STELLAR_IMMERSIVE_SKY_ONLY") == "1")
        {
            await VerifySurfaceSkyCompanionsAsync();
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_IMMERSIVE_SATURN_ONLY") == "1")
        {
            await ClickPositionAsync(BodyPoint(6), MouseButton.Left, doubleClick: true);
            await WaitForCameraAsync();
            Require(_main.UiFocusedPlanetBodyId == 6, "Saturn did not focus through its projected body point.");
            await SaveViewportAsync("immersive-saturn-focus.png", 0, 0);
            return;
        }
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Require(_main.UiFocusedPlanetBodyId == 3 && _main.UiSystemMeshBodyCount == count,
            "Earth focus must preserve the system's moons and other orbital bodies.");
        var angles = _main.UiSystemCameraAngles;
        await DragAsync(new(640, 420), new(745, 460), MouseButton.Middle);
        await WaitForCameraAsync();
        Require(_main.UiSystemCameraAngles.DistanceTo(angles) > .1f,
            "Middle drag did not rotate the focused perspective camera.");
        await SaveViewportAsync("immersive-04-rotated-system.png", 0, 0);
        await SaveViewportAsync("immersive-05-earth-orbit.png", 0, 0);
        for (var i = 0; !_main.UiIsSurfaceOpen && i < 18; i++)
        {
            await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
            await WaitFramesAsync(65);
        }
        Require(_main.UiIsSurfaceOpen, "Wheel descent never entered Earth's atmosphere.");
        var surface = _main.GetNode<PlanetSurfaceView>("PlanetSurfaceLayer/PlanetSurfaceView");
        Require(surface.IsOrbitalFlight && surface.AltitudeMeters > 500000,
            "Surface transition skipped the continuous globe and upper atmosphere.");
        await SaveViewportAsync("immersive-06-upper-orbit.png", 0, 0);
        var capturedAtmosphere = false;
        for (var i = 0; surface.IsOrbitalFlight && i < 30; i++)
        {
            await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
            await WaitFramesAsync(70);
            if (!capturedAtmosphere && surface.AltitudeMeters < 30000)
            {
                await SaveViewportAsync("immersive-07-atmosphere.png", 0, 0);
                capturedAtmosphere = true;
            }
        }
        Require(!surface.IsOrbitalFlight && surface.AltitudeMeters < 1500, "Descent did not reach the colony terrain.");
        Require(surface.EnvironmentDetailCount >= 20,
            $"Surface infrastructure dressing is unexpectedly sparse ({surface.EnvironmentDetailCount} root parts).");
        Require(surface.SurfaceTrafficCount >= 1,
            "The active colony has no bounded point-to-point ground traffic.");
        Require(surface.HighRiseCount >= 6 && surface.DistrictRingRoadCount == 2,
            "The established capital skyline or its district road network is missing.");
        VerifySurfaceBuildingTiers();
        await DragAsync(new(620, 420), new(620, 335), MouseButton.Middle);
        for (var i = 0; i < 14; i++) await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
        await SaveViewportAsync("immersive-08-colony.png", 0, 0);
        // Walk the view out onto an avenue before descending to eye level. This uses
        // ordinary pan/look gestures and avoids photographing the hub's front wall.
        await DragAsync(new(620, 420), new(360, 420), MouseButton.Left);
        await DragAsync(new(620, 420), new(305, 350), MouseButton.Middle);
        for (var i = 0; i < 12; i++) await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
        await SaveViewportAsync("immersive-09-street.png", 0, 0);
        Require(surface.CameraPosition.Y >= SurfaceConstruction.TerrainHeight(
                surface.CameraPosition.X, surface.CameraPosition.Z) + 2.19f,
            "Street camera passed below its local terrain clearance.");
        Check(true, "premium-surface-city-has-skyline-infrastructure-and-traffic");
        if (System.Environment.GetEnvironmentVariable("STELLAR_IMMERSIVE_SURFACE_ONLY") == "1") return;
        await ClickNamedButtonAsync(surface, "SurfaceBack");
        await WaitForCameraAsync();
        Require(!_main.UiIsSurfaceOpen && _main.UiFocusedPlanetBodyId == 3, "Returning from ground lost focused Earth.");
        await WaitFramesAsync(80);
        Require(!_main.UiIsSurfaceOpen, "Return to orbit immediately retriggered descent.");
        GD.Print($"IMMERSIVE_REVIEW_PASS: {count} bodies, perspective rotation, wheel descent, atmosphere, ground, recovery.");
    }

    private void VerifySurfaceBuildingTiers()
    {
        foreach (var family in new[] { "power_generator", "science_lab", "fabricator", "trade_hub", "habitat_complex" })
        {
            var baseline = SurfaceBuildingVisuals.Create(family);
            var advanced = SurfaceBuildingVisuals.Create("advanced_" + family);
            var baselineParts = Descendants(baseline).OfType<MeshInstance3D>().Count();
            var advancedParts = Descendants(advanced).OfType<MeshInstance3D>().Count();
            Require(baseline.VisualTier == 1 && advanced.VisualTier == 2 && advancedParts > baselineParts,
                $"{family} upgrade has no visible geometry tier ({baselineParts} -> {advancedParts}).");
            baseline.Free();
            advanced.Free();
        }
        Check(true, "surface-module-families-have-distinct-authoritative-upgrade-tiers");
    }

    private async Task VerifySurfaceSkyCompanionsAsync()
    {
        var surface = _main.GetNode<PlanetSurfaceView>("PlanetSurfaceLayer/PlanetSurfaceView");
        await EnterGroundSurfaceAsync(3, surface);
        Require(surface.SkyCompanionCount == 1, "Known Earth surface did not show its one surveyed Moon.");
        var earthMoon = Descendants(surface).OfType<MeshInstance3D>().SingleOrDefault(node => node.Name.ToString().StartsWith("SkyMoon_", StringComparison.Ordinal));
        Require(earthMoon is { Visible: true } && earthMoon.MaterialOverride is ShaderMaterial moonMaterial &&
                moonMaterial.GetShaderParameter("mapped").AsBool(),
            "Earth's sky companion is not the mapped, phase-lit surveyed Moon.");
        await DragAsync(new(620, 420), new(620, 115), MouseButton.Middle);
        await WaitFramesAsync(12);
        var camera = Descendants(surface).OfType<Camera3D>().Single(node => node.Name == "SurfaceCamera");
        var moonPoint = camera.UnprojectPosition(earthMoon!.GlobalPosition);
        Require(!camera.IsPositionBehind(earthMoon.GlobalPosition) && new Rect2(Vector2.Zero, surface.Size).HasPoint(moonPoint),
            $"Earth's Moon is outside the photographed sky after a real look gesture: {moonPoint}.");
        await SaveViewportAsync("immersive-sky-earth-moon.png", 0, 0);
        Check(true, "earth-surface-shows-one-photoreal-phase-lit-moon");

        await ClickNamedButtonAsync(surface, "SurfaceBack");
        await WaitFramesAsync(80);
        Require(surface.SkyCompanionCount == 0, "Closing Earth's surface retained hidden sky companions.");
        await ClickNamedButtonAsync(_main, "SpatialBack");
        await WaitForCameraAsync();
        await EnterGroundSurfaceAsync(4, surface);
        Require(surface.SkyCompanionCount == 0, "Mars surface invented a moon absent from the surveyed orbital markers.");
        Check(true, "mars-surface-does-not-invent-sky-companions");

        await ClickNamedButtonAsync(surface, "SurfaceBack");
        await WaitFramesAsync(80);
        await ClickNamedButtonAsync(_main, "SpatialBack");
        await WaitForCameraAsync();
        await EnterGroundSurfaceAsync(3, surface);
        Require(surface.SkyCompanionCount == 1, "Returning to Earth did not restore its surveyed Moon.");
        await ClickNamedButtonAsync(surface, "SurfaceBack");
        await WaitFramesAsync(20);
        Require(surface.SkyCompanionCount == 0, "Closing the returned Earth surface retained its Moon node.");
        Check(true, "surface-sky-companions-follow-active-world-lifecycle");
    }

    private async Task EnterGroundSurfaceAsync(int bodyId, PlanetSurfaceView surface)
    {
        await ClickPositionAsync(BodyPoint(bodyId), MouseButton.Left, doubleClick: true);
        // The sky lifecycle probe needs the real focus route, but does not require the
        // perspective camera's asymptotic final millimeter before beginning descent.
        await WaitFramesAsync(120);
        Require(_main.UiFocusedPlanetBodyId == bodyId, $"Body {bodyId} did not focus through its projected point.");
        for (var step = 0; !_main.UiIsSurfaceOpen && step < 18; step++)
        {
            await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
            await WaitFramesAsync(65);
        }
        Require(_main.UiIsSurfaceOpen, $"Wheel descent did not open body {bodyId}'s surface.");
        for (var step = 0; surface.IsOrbitalFlight && step < 30; step++)
        {
            await ClickPositionAsync(new(600, 440), MouseButton.WheelUp);
            await WaitFramesAsync(70);
        }
        Require(!surface.IsOrbitalFlight && surface.AltitudeMeters < 1500,
            $"Descent did not reach body {bodyId}'s terrain.");
        await WaitFramesAsync(12);
    }
}
