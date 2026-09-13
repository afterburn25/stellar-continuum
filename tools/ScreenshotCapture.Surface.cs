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
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true); await WaitForCameraAsync();
        await ClickNamedButtonAsync(_main, "SpatialSurface"); await WaitForRefreshAsync();
        var panel = _main.GetNode<PlanetaryWindow>("PlanetSurfaceLayer/PlanetaryWindow");
        Require(_main.UiCurrentSurface is { PlanetName: "Earth", BuildingCapacity: 32 }, "Earth planetary management did not expose its established Command Center.");
        await ClickNamedButtonAsync(panel, "PlanetarySlot_0");
        await ClickNamedButtonAsync(panel, "PlanetaryBuild_power_generator"); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.Buildings.Any(b => b.SlotIndex == 0 && b.TypeId == "power_generator"), "planetary-slot-click-authorizes-real-construction");
        await SaveViewportAsync("17-surface-placement.png");
        await ClickNamedButtonAsync(panel, "PlanetarySlot_1");
        await ClickNamedButtonAsync(panel, "PlanetaryBuild_science_lab"); await WaitForRefreshAsync();
        await SaveViewportAsync("18-surface-colony.png");
        var expected = _main.UiCurrentSurface!.Buildings.Select(b => (b.Id, b.SlotIndex, b.TypeId)).ToArray();
        await ClickNamedButtonAsync(panel, "PlanetarySave"); await WaitForRefreshAsync();
        Require(HashFile(normalSave) == normalSaveHash, "Planetary development changed the Player save while in Developer mode.");
        await ClickNamedButtonAsync(panel, "PlanetaryBack");
        await ClickButtonAsync(_dock, "Back to Region"); await WaitForCameraAsync();
        normalSaveHash = await ReloadDeveloperThroughPlayerAsync(normalSave, normalSaveHash);
        await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true); await WaitForCameraAsync();
        await ClickNamedButtonAsync(_main, "SpatialSurface"); await WaitForRefreshAsync();
        panel = _main.GetNode<PlanetaryWindow>("PlanetSurfaceLayer/PlanetaryWindow");
        Check(_main.UiCurrentSurface!.Buildings.Select(b => (b.Id, b.SlotIndex, b.TypeId)).SequenceEqual(expected), "planetary-slots-survive-real-campaign-reload");
        await ClickNamedButtonAsync(panel, "PlanetaryBack");
        await OpenSectionAsync("colonies");
        var manage = Descendants(ActivePanel()).OfType<Button>().Where(button => button.Text == "Manage planet").ToArray();
        Require(manage.Length == 3, "Colony list did not expose its planetary management actions.");
        await ClickControlAsync(manage[2]); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface is { PlanetName: "Mars" } && _main.UiCurrentSurface.RequiredHabitatSystems > 0, "mars-planetary-window-uses-real-environment-and-support");
        await SaveViewportAsync("21-mars-surface.png");
        await ClickNamedButtonAsync(panel, "PlanetaryBack");
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
