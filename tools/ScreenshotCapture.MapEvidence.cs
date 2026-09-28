using System;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    // Explicitly Developer-only visual receipt. Reveal affects this disposable Developer
    // campaign alone; all system transitions below use the same visible map controls as play.
    private async Task VerifyMapEvidenceAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        await ClickNamedButtonAsync(menu, "NewDeveloperCampaign");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForCampaignLoadingAsync();
        Require(_main.UiIsDeveloperMode, "map evidence must be explicitly Developer-labelled");
        _main.UiRunDeveloperCommand("reveal_galaxy");
        await WaitForRefreshAsync();
        var galaxy = (GalaxyState)typeof(Main).GetField("_galaxy", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(_main)!;
        var binary = galaxy.Systems.FirstOrDefault(system => system.SecondaryStellarClass.HasValue && !system.TertiaryStellarClass.HasValue);
        var triple = galaxy.Systems.FirstOrDefault(system => system.TertiaryStellarClass.HasValue);
        Require(binary is not null && triple is not null, "generated Developer galaxy has no persisted binary/triple stellar fixtures");
        await CaptureCompanionSystemAsync(binary!.Id, "map-evidence-01-developer-binary.png", true);
        await CaptureCompanionSystemAsync(triple!.Id, "map-evidence-02-developer-triple.png", false);

        // Build a real scout through the visible Developer shipyard route, then use a normal
        // right-click order. The local phase remains simulation-owned and is never fabricated.
        await RunVisibleDeveloperCommandAsync(menu, "DeveloperCommand_unlock_technology");
        await RunVisibleDeveloperCommandAsync(menu, "DeveloperCommand_grant_resources");
        var scoutId = await BuildVisibleCivilianShipAsync(menu, "warp_scout", FleetRole.Scout);
        await SelectNormalPlayerSpeedAsync();
        await ClickNamedButtonAsync(_main, "DrawerClose");
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        var idleFleet = Descendants(_main).OfType<Button>().Single(button => button.Name == "SystemFleet" + scoutId);
        await ClickControlAsync(idleFleet); await WaitForCameraAsync();
        var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
        Require(!canvas.IsDetailedFocus && _main.UiSelectedFleetId == scoutId && canvas.GetStarScreenPosition().HasValue,
            "single-clicking an idle scout did not preserve the 2D orbital order view and select its stats");
        await ClickPositionAsync(ScreenRect(idleFleet).GetCenter(), MouseButton.Left, doubleClick: true); await WaitForCameraAsync();
        Require(canvas.IsFleetFocused, "double-clicking an idle scout did not enter local vessel focus at chart origin");
        await SaveViewportAsync("map-evidence-03a-developer-idle-scout-close.png", 0, 0);
        await ClickNamedButtonAsync(_main, "SpatialBack"); await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        _main.UiSelectOwnedFleet(scoutId, center: true); await WaitForCameraAsync();
        var origin = _main.UiSelectedSystemId;
        var target = _main.UiSpatialCatalog.Where(item => item.SystemId != origin)
            .Select(item => new { item.SystemId, Point = StarPoint(item.SystemId) })
            .OrderBy(item => item.Point.DistanceTo(StarPoint(origin))).First();
        await ClickPositionAsync(target.Point, MouseButton.Right); await WaitForRefreshAsync();
        Require(_main.UiIsPaused, "moving-scout fixture must issue its order while visibly paused");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        for (var frame = 0; frame < 90 && Fleet(galaxy, scoutId).TransitPhase != FleetTransitPhase.LocalDeparture; frame++)
            await WaitFramesAsync(1);
        Require(Fleet(galaxy, scoutId).TransitPhase == FleetTransitPhase.LocalDeparture, "real scout did not enter timed local departure");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickButtonAsync(_dock, "Home"); await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        Require(_main.UiIsSystemSpatialView, "moving scout did not remain in its actual local system");
        var localFleet = Descendants(_main).OfType<Button>().Single(button => button.Name == "SystemFleet" + scoutId);
        await ClickControlAsync(localFleet); await WaitForCameraAsync();
        Require(!canvas.IsDetailedFocus && _main.UiSelectedFleetId == scoutId && canvas.GetStarScreenPosition().HasValue,
            "single-clicking the moving scout did not preserve the 2D orbital order view and select its stats");
        await ClickPositionAsync(ScreenRect(localFleet).GetCenter(), MouseButton.Left, doubleClick: true); await WaitForCameraAsync();
        Require(canvas.IsFleetFocused, "double-clicking the actual moving scout did not enter local vessel focus");
        await SaveViewportAsync("map-evidence-03-developer-moving-scout-close.png", 0, 0);
        await WheelAsync(true, new Vector2(620, 390));
        await DragAsync(new Vector2(620, 390), new Vector2(690, 350), MouseButton.Middle);
        await DragAsync(new Vector2(620, 390), new Vector2(585, 420), MouseButton.Left);
        Require(canvas.IsFleetFocused, "zoom/rotate/pan unexpectedly left local vessel focus");
        await ClickNamedButtonAsync(_main, "SpatialBack"); await WaitForCameraAsync();
        Require(!canvas.IsDetailedFocus && _main.UiIsSystemSpatialView,
            "Back from local vessel focus did not restore the same orbital overview");
    }

    private async Task CaptureCompanionSystemAsync(int systemId, string file, bool starClose)
    {
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        await ClickNamedButtonAsync(_main, "SpatialOverview"); await WaitForCameraAsync();
        await ClickPositionAsync(StarPoint(systemId), MouseButton.Left); await WaitForCameraAsync();
        Require(_main.UiSelectedSystemId == systemId, "Developer companion fixture did not select its persisted system");
        await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
        if (!starClose)
        {
            await SaveViewportAsync(file, 0, 0);
            return;
        }
        await ClickPositionAsync(canvas.GetStarScreenPosition()!.Value, MouseButton.Left, doubleClick: true);
        await WaitForCameraAsync();
        Require(canvas.IsStarFocused, "double-clicking the primary did not enter native stellar focus");
        await SaveViewportAsync(file, 0, 0);
        await WaitFramesAsync(120);
        await SaveViewportAsync("map-evidence-01b-developer-binary-flare-growth.png", 0, 0);
        await WaitFramesAsync(180);
        await SaveViewportAsync("map-evidence-01c-developer-binary-flare-fade.png", 0, 0);
        await ClickNamedButtonAsync(_main, "SpatialBack"); await WaitForCameraAsync();
        Require(!canvas.IsDetailedFocus && _main.UiIsSystemSpatialView,
            "Back from stellar focus did not restore the same orbital overview");
    }

    private static FleetState Fleet(GalaxyState galaxy, int id) => galaxy.Fleets.Single(fleet => fleet.Id == id);
}
