using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Presentation;
using Game.Simulation.Models;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyShipMouseOrdersAsync()
    {
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        if (!_main.UiOwnedFleets.Any(f => f.Role == FleetRole.Scout))
        {
            await OpenCampaignMenuAsync();
            await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "OpenDevelopment");
            await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "DeveloperTools");
            await ClickNamedButtonAsync(_main, "DeveloperCommand_finish_orders");
            await ClickNamedButtonAsync(_main, "DeveloperToolsClose");
        }
        var inheritedResumeSpeed = _main.UiResumeSpeed;
        var inheritedLabel = Descendants(_main).OfType<Label>().Single(control => control.Name == "SimulationPlaybackState");
        Require(inheritedLabel.Text == "PAUSED" && inheritedLabel.TooltipText.Contains(inheritedResumeSpeed == Game.Simulation.SimulationClock.SpeedLevel.Demo ? "24×" : ((int)inheritedResumeSpeed == 4 ? "8×" : $"{(int)inheritedResumeSpeed}×")),
            $"Paused compact playback did not disclose remembered {inheritedResumeSpeed} speed.");
        await SelectNormalPlayerSpeedAsync();
        Require(_main.UiIsPaused && _main.UiResumeSpeed == Game.Simulation.SimulationClock.SpeedLevel.Normal,
            $"Travel fixture did not retain visible 1x as its resume speed (paused={_main.UiIsPaused}, resume={_main.UiResumeSpeed}).");
        GD.Print($"STELLAR_FLEET_TIMING_SETUP inheritedResumeSpeed={inheritedResumeSpeed} controlledResumeSpeed={_main.UiResumeSpeed} day={FormatTiming(_main.UiSimulationDays)}");
        if (_sidebar.IsDrawerOpen) await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        var ship = _main.UiOwnedFleets.First(f => f.Role == FleetRole.Scout);
        var point = _main.UiGetFleetScreenPosition(ship.FleetId)!.Value;
        await ClickPositionAsync(point, MouseButton.Left); await WaitForRefreshAsync();
        Require(_main.UiSelectedFleetId == ship.FleetId, "Clicking a ship icon did not select the exact ship.");
        Require(Descendants(_main).OfType<Label>().Single(l => l.Name == "SelectedShipName").Text == ship.Name,
            "Selected ship stats did not identify the clicked vessel.");
        var home = _main.UiSelectedSystemId;
        var homePoint = StarPoint(home);
        var target = _main.UiSpatialCatalog.Where(s => s.SystemId != home)
            .Select(s => new { s.SystemId, Point = StarPoint(s.SystemId) })
            .Where(s => new Rect2(100, 150, 780, 470).HasPoint(s.Point) && s.Point.DistanceTo(homePoint) > 70 &&
                s.Point.DistanceTo(homePoint) < ship.MaximumLegRangeLightYears * .85)
            .OrderBy(s => s.Point.DistanceTo(homePoint)).First();
        await ClickPositionAsync(target.Point, MouseButton.Right); await WaitForRefreshAsync();
        var ordered = _main.UiOwnedFleets.Single(f => f.FleetId == ship.FleetId);
        Require(_main.UiSelectedFleetId == ship.FleetId && ordered.RemainingRouteDistanceLightYears > 0 &&
            _main.UiGetFleetScreenPosition(ship.FleetId)!.Value.DistanceTo(point) < .1f,
            "Right-click failed to order the selected vessel or teleported it while paused.");
        AssertVisibleMetricRouteFeedback();
        var orderedDay = _main.UiSimulationDays;
        var orderedFuel = ordered.FuelRemainingLightYears;
        var orderedDistance = ordered.RemainingRouteDistanceLightYears;
        GD.Print($"STELLAR_FLEET_TIMING_ORDERED day={FormatTiming(orderedDay)} speed={_main.UiCurrentSpeed} resumeSpeed={_main.UiResumeSpeed} routeLy={FormatTiming(orderedDistance)} fuelLy={FormatTiming(orderedFuel)}");
        await SaveViewportAsync("28-selected-ship-route.png");
        var wall = Stopwatch.StartNew();
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        Require(_main.UiCurrentSpeed == Game.Simulation.SimulationClock.SpeedLevel.Normal,
            $"Resume restored {_main.UiCurrentSpeed} instead of the visibly selected Normal speed.");
        UiOwnedFleetSnapshot? intermediate = null;
        var last = ordered;
        var lastPoint = point;
        var observedFrames = 0;
        for (var frame = 1; frame <= 120; frame++)
        {
            await WaitFramesAsync(1);
            var current = _main.UiOwnedFleets.Single(f => f.FleetId == ship.FleetId);
            var currentPoint = _main.UiGetFleetScreenPosition(ship.FleetId)!.Value;
            observedFrames = frame;
            last = current;
            lastPoint = currentPoint;
            if (current.RemainingRouteDistanceLightYears < orderedDistance &&
                current.RemainingRouteDistanceLightYears > 0 && current.FuelRemainingLightYears < orderedFuel &&
                currentPoint.DistanceTo(point) > .1f)
            {
                intermediate = current;
                break;
            }
        }
        var observedDay = _main.UiSimulationDays;
        GD.Print($"STELLAR_FLEET_TIMING_OBSERVED frames={observedFrames} wallMs={wall.Elapsed.TotalMilliseconds.ToString("0.0", CultureInfo.InvariantCulture)} speed={_main.UiCurrentSpeed} dayDelta={FormatTiming(observedDay - orderedDay)} routeBeforeLy={FormatTiming(orderedDistance)} routeAfterLy={FormatTiming(last.RemainingRouteDistanceLightYears)} fuelBeforeLy={FormatTiming(orderedFuel)} fuelAfterLy={FormatTiming(last.FuelRemainingLightYears)} screenMove={FormatTiming(lastPoint.DistanceTo(point))} label=\"{_main.UiSpeedLabel}\"");
        Require(intermediate is not null,
            $"Travel produced no positive unfinished movement within {observedFrames} frames at {_main.UiCurrentSpeed}; day delta {FormatTiming(observedDay - orderedDay)}, remaining route {_main.UiOwnedFleets.Single(f => f.FleetId == ship.FleetId).RemainingRouteDistanceLightYears:0.000} ly.");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await WaitForRefreshAsync();
        Require(_main.UiIsPaused, "Visible Pause control did not stop the observed route.");
        Check(true, "ship-icon-selection-right-click-and-timed-travel");
        await ClickNamedButtonAsync(_main, "CloseShipInspector");
        await ClickButtonAsync(_dock, "Home"); await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        await ClickPositionAsync(_main.UiGetInfrastructureScreenPosition("orbital_shipyard")!.Value, MouseButton.Left);
        await WaitForRefreshAsync();
        Require(_main.UiSelectedOrbitalConstruction is { Id: "orbital_shipyard", State: "Operational" } &&
            Descendants(_main).OfType<Game.Presentation.Spatial.SystemScene3D>()
                .Single(scene => scene.Name.ToString() == "SystemScene3D").InfrastructureCount >= 3 &&
            Descendants(_main).OfType<Node3D>().Count(node => node.Name.ToString().StartsWith("Infrastructure_", StringComparison.Ordinal) &&
                node.GetChildren().OfType<MeshInstance3D>().Any()) >= 3,
            "Completed orbital infrastructure did not produce selectable 3D models.");
        await SaveViewportAsync("29-orbital-shipyard.png");
        await ClickNamedButtonAsync(_main, "CloseOrbitalInspector");
        await ClickButtonAsync(_dock, "Back to Region"); await WaitForCameraAsync();
    }

    private async Task SelectNormalPlayerSpeedAsync()
    {
        await SetPlaybackSpeedAsync(Game.Simulation.SimulationClock.SpeedLevel.Normal);
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == Game.Simulation.SimulationClock.SpeedLevel.Normal,
            $"Visible speed selection did not start ordinary 1x simulation (paused={_main.UiIsPaused}, speed={_main.UiCurrentSpeed}).");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await WaitForRefreshAsync();
        Require(Descendants(_main).OfType<Label>().Single(control => control.Name == "SimulationPlaybackState").Text == "PAUSED",
            "Paused compact playback did not display its stopped state.");
    }

    private static string FormatTiming(double value) => value.ToString("0.000", CultureInfo.InvariantCulture);

    private void AssertVisibleMetricRouteFeedback()
    {
        var feedback = _main.GetNode<Control>("PlayerControls/CommandFeedback");
        var label = Descendants(feedback).OfType<Label>().Single();
        Require(_main.UiStatusMessage.Contains("Route:", StringComparison.Ordinal) &&
                _main.UiStatusMessage.Contains("km", StringComparison.Ordinal) &&
                _main.UiStatusMessage.Contains("ly", StringComparison.Ordinal),
            $"Visible route feedback lost its metric primary distance: {_main.UiStatusMessage}");
        Require(feedback.IsVisibleInTree() && label.IsVisibleInTree() &&
                label.Text == _main.UiStatusMessage &&
                label.GetCombinedMinimumSize().Y <= feedback.Size.Y &&
                Encloses(ScreenRect(feedback), ScreenRect(label)),
            $"Metric route feedback did not fit the visible 720p command strip: " +
            $"labelMinimum={label.GetCombinedMinimumSize()} labelBounds={ScreenRect(label)} panelBounds={ScreenRect(feedback)}");
        AssertInsideViewport(feedback, "metric route feedback");
        Check(true, "metric-route-feedback-visible-at-720p");
    }
}
