using System;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Game.Simulation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyWindowLifecycleAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Normal);
        await VerifyMinimizeRestoreAsync("galaxy", () => !_main.UiIsSystemSpatialView && !_main.UiIsSurfaceOpen);

        _main.UiSelectHomeSystem();
        _main.UiOpenSelectedSystem();
        await WaitForCameraAsync();
        await VerifyMinimizeRestoreAsync("system", () => _main.UiIsSystemSpatialView && !_main.UiIsSurfaceOpen);

        var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
        var earth = _main.UiSystemBodies.First(body => body.SurfaceKey == "earth");
        Require(canvas.FocusBody(earth.BodyId), "Lifecycle fixture could not focus owned Earth.");
        await WaitForCameraAsync();
        _main.UiOpenPlanetSurface(earth.BodyId);
        Require(_main.UiIsSurfaceOpen, "Lifecycle fixture could not open the real Earth surface.");
        await VerifyMinimizeRestoreAsync("surface", () => _main.UiIsSurfaceOpen);

        var closeRevision = _main.UiWindowLifecycleRevision;
        _main.Notification((int)Node.NotificationWMCloseRequest);
        await WaitFramesAsync(3);
        Require(_main.UiIsMenuOpen && _main.UiIsPaused && _main.UiWindowLifecycleRevision > closeRevision &&
                _main.UiLastWindowLifecycle == "close-request-menu",
            "A native window close request did not pause behind the campaign menu.");
        await WaitFramesAsync(12);
        Require(IsInstanceValid(_main) && GetTree().Root.GetChildCount() > 0,
            "The healthy runtime exited after its intercepted window close request.");
        Check(true, "window-close-request-opens-menu-without-exiting");
        await SaveViewportAsync("window-lifecycle-close-menu.png");

        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        Require(_main.UiIsSurfaceOpen && _main.UiCurrentSpeed == SimulationClock.SpeedLevel.Normal,
            "Closing the window menu did not restore the surface and its pre-menu playback state.");
        Check(true, "minimize-restore-preserves-galaxy-system-surface-and-input");
    }

    private async Task VerifyMinimizeRestoreAsync(string location, Func<bool> locationStillOpen)
    {
        var window = GetWindow();
        var speed = _main.UiCurrentSpeed;
        var revision = _main.UiWindowLifecycleRevision;
        var frameBefore = Engine.GetProcessFrames();
        window.Mode = Window.ModeEnum.Minimized;
        await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        await Task.Delay(450);
        Require(IsInstanceValid(_main), $"Runtime exited while minimized at {location}.");
        var recovery = Stopwatch.StartNew();
        window.Mode = Window.ModeEnum.Windowed;
        await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
        recovery.Stop();
        await WaitFramesAsync(5);
        Require(locationStillOpen() && _main.UiCurrentSpeed == speed,
            $"Minimize/restore changed the {location} view or its playback state.");
        Require(_main.UiWindowLifecycleRevision > revision,
            $"The {location} minimize/restore cycle emitted no lifecycle diagnostic.");
        Require(Engine.GetProcessFrames() > frameBefore && recovery.Elapsed < TimeSpan.FromSeconds(3),
            $"The {location} restore did not produce a fresh rendered frame promptly ({recovery.Elapsed.TotalMilliseconds:0} ms).");

        // A real visible control must still receive pointer input after restoration.
        var playbackName = _main.UiIsSurfaceOpen ? "SurfacePlaybackButton" : "SimulationPlaybackButton";
        var playback = Descendants(_main).OfType<Button>()
            .First(button => button.Name == playbackName && button.IsVisibleInTree());
        await ClickControlAsync(playback);
        Require(_main.UiCurrentSpeed != speed, $"Playback input missed after restoring {location}.");
        await ClickPositionAsync(ScreenRect(playback).GetCenter(), MouseButton.Right);
        _main.UiResumeAtSpeed(speed);
        await WaitFramesAsync(2);
        GD.Print($"STELLAR_MINIMIZE_RESTORE_PASS location={location} mode={window.Mode} speed={speed} " +
            $"freshFrameMs={recovery.Elapsed.TotalMilliseconds:0}");
    }
}
