using System;
using System.IO;
using System.Threading.Tasks;
using Godot;
using Game.Diagnostics;

namespace Game.Presentation;

/// <summary>
/// Scene entry point for the integrated early-release runtime. Core owns campaign lifecycle
/// and strategic simulation timing here; all existing drawing, ordinary input and command
/// presentation behavior remains inherited from Main.
/// </summary>
public partial class IntegratedMain : Main
{
    private bool _runtimeReady;
    private bool _startupFailed;
    private bool _startupReported;
    private bool _startupSmokeRequested;
    private bool _startupFailureUiRequested;
    private bool _failureExitRequested;
    private Window.ModeEnum _lastWindowMode;
    private bool _lastWindowFocused;
    public bool UiRuntimeReady => _runtimeReady;
    public bool UiStartupFailed => _startupFailed;

    public override void _Ready()
    {
        // A platform close request is an invitation to show the campaign menu. Only its
        // explicit Exit to Windows command may save and end a healthy campaign.
        GetTree().AutoAcceptQuit = false;
        _lastWindowMode = GetWindow().Mode;
        _lastWindowFocused = GetWindow().HasFocus();
        var arguments = OS.GetCmdlineUserArgs();
        var lateFailureSmokeRequested = Array.IndexOf(arguments, "--stellar-startup-failure-late-smoke") >= 0;
        _startupSmokeRequested = Array.IndexOf(arguments, "--stellar-startup-smoke") >= 0 ||
            Array.IndexOf(arguments, "--stellar-startup-failure-smoke") >= 0 || lateFailureSmokeRequested;
        _startupFailureUiRequested = Array.IndexOf(arguments, "--stellar-startup-failure-ui") >= 0;
        try
        {
            PrepareIntegratedStartupAttempt();
            if (Array.IndexOf(arguments, "--stellar-startup-failure-smoke") >= 0 || _startupFailureUiRequested)
                throw new InvalidOperationException("Requested startup failure smoke.",
                    new InvalidDataException("Deterministic nested startup failure evidence."));
            AddChild(new ResponsiveDisplay { Name = "ResponsiveDisplay" });
            // Injected late-failure smokes must exercise partial initialization without
            // creating, repairing, or rotating the user's campaign files before failing.
            RunIntegratedCampaignReady(suppressStartupPersistence: lateFailureSmokeRequested);
            if (lateFailureSmokeRequested)
                throw new InvalidOperationException("Requested late startup failure smoke.",
                    new InvalidDataException("Deterministic late initialization failure evidence."));
            InitializeSpatialPresentation();
            InitializeSurfacePresentation();
            InitializeDeveloperTools();
            InitializeVoicePresentation();
            InitializeMassiveCombatPresentation();
            _runtimeReady = true;
        }
        catch (Exception exception)
        {
            HandleInitializationFailure(exception);
        }
    }

    public override void _Process(double delta)
    {
        if (!_runtimeReady)
            return;
        ObserveWindowLifecycleState();
        if (!RunMassiveCombatFrame(delta))
            RunIntegratedSimulationFrame(delta);
        RefreshSpatialPresentation(delta);
        RefreshSurfacePresentation();
        RefreshVoicePresentation(delta);
        RefreshDiplomacyWorkspaceEvents(delta);
        RefreshMassiveCombatPresentation(delta);
        if (_runtimeReady && !_startupReported)
        {
            // Prove that the actual scene entry point initialized its campaign and ran a frame.
            // CI also rejects engine errors before or after this marker, including child scripts.
            _startupReported = true;
            GD.Print("STELLAR_RUNTIME_READY IntegratedMain");
        }
        // The source-startup smoke follows the same threaded asset lifecycle as play.
        // Retrieve every requested resource before headless renderer teardown so in-flight
        // textures cannot survive the tree that requested them.
        if (_startupSmokeRequested &&
            GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.HasCompletedStartupLoading == true)
            HandleIntegratedCloseRequest();
    }

    public override void _PhysicsProcess(double delta)
    {
        if (!_runtimeReady)
            return;
        _ = delta;
        RefreshIntegratedShipbuildingPresentation();
    }

    public override void _Input(InputEvent @event)
    {
        if (!_runtimeReady || UiIsMassiveCombatPresentationOpen)
            return;
        if (ShouldBlockGameplayInput())
            return;

        if (@event is InputEventKey key && key.Pressed && !key.Echo)
        {
            if (key.Keycode == Key.N)
            {
                UiNewCampaign();
                GetViewport().SetInputAsHandled();
                return;
            }

            if (key.Keycode == Key.F6)
            {
                SaveIntegratedCampaign();
                GetViewport().SetInputAsHandled();
                return;
            }
        }

        // Pointer commands must wait until GUI controls have had the opportunity to consume them.
        if (@event is not InputEventMouse)
            base._Input(@event);
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (!_runtimeReady)
            return;
        if (ShouldBlockGameplayInput())
            return;

        if (UiIsMassiveCombatActive && @event is InputEventKey { Pressed: true, Echo: false } tacticalKey)
        {
            var handled = true;
            switch (tacticalKey.Keycode)
            {
                case Key.Space: UiSetPaused(!UiIsPaused); break;
                case Key.Key1: UiSetTacticalSpeed(.25); break;
                case Key.Key2: UiSetTacticalSpeed(.5); break;
                case Key.Key3: UiSetTacticalSpeed(1); break;
                case Key.Key4: UiSetTacticalSpeed(2); break;
                case Key.Key5: UiSetTacticalSpeed(4); break;
                default: handled = false; break;
            }
            if (handled)
            {
                GetViewport().SetInputAsHandled();
                return;
            }
        }

        // Record pointer commands only after GUI consumption, including rejected orders.
        // This read-only diagnostic lets runtime checks detect invisible click-through.
        if (@event is InputEventMouseButton { Pressed: true } pointer &&
            pointer.ButtonIndex is MouseButton.Left or MouseButton.Right)
            UiPointerCommandRevision++;

        if (HandleSpatialPresentationInput(@event) ||
            (UiIsSystemSpatialView && @event is InputEventMouse))
        {
            GetViewport().SetInputAsHandled();
            return;
        }

        if (@event is InputEventMouse)
        {
            // Main's science-order shortcut is historically in _Input. Route it after GUI just
            // like scout/colony orders, so neither panels nor the system canvas can be clicked through.
            base._Input(@event);
            if (GetViewport().IsInputHandled())
                return;
        }

        base._UnhandledInput(@event);
    }

    public override void _Notification(int what)
    {
        if (what == NotificationWMCloseRequest)
        {
            if (_startupFailed)
            {
                RequestFailureExit();
                return;
            }
            HandleIntegratedWindowCloseRequest();
            return;
        }

        if (what == (int)NotificationWMWindowFocusIn || what == (int)NotificationWMWindowFocusOut)
        {
            var focused = what == (int)NotificationWMWindowFocusIn;
            if (focused != _lastWindowFocused)
            {
                _lastWindowFocused = focused;
                RecordWindowLifecycle(focused ? "focus-in" : "focus-out");
            }
        }
        else if (what == NotificationWMSizeChanged)
        {
            var mode = GetWindow().Mode;
            if (mode != _lastWindowMode)
            {
                _lastWindowMode = mode;
                RecordWindowLifecycle(mode == Window.ModeEnum.Minimized ? "minimized" : "window-mode-" + mode);
            }
        }

        base._Notification(what);
    }

    private void RecordWindowLifecycle(string transition)
    {
        UiWindowLifecycleRevision++;
        UiLastWindowLifecycle = transition;
        var message = $"transition={transition} mode={GetWindow().Mode} focused={GetWindow().HasFocus()} " +
            $"menu={UiIsMenuOpen} paused={UiIsPaused}";
        GD.Print("STELLAR_WINDOW_LIFECYCLE " + message);
        try { SupportLogger.Log("window-lifecycle", message); }
        catch (Exception exception) { GD.PushWarning("Window lifecycle diagnostic could not be persisted: " + exception.Message); }
    }

    private void ObserveWindowLifecycleState()
    {
        var mode = GetWindow().Mode;
        if (mode == _lastWindowMode) return;
        _lastWindowMode = mode;
        RecordWindowLifecycle(mode == Window.ModeEnum.Minimized ? "minimized" : "window-mode-" + mode);
    }

    private void HandleIntegratedWindowCloseRequest()
    {
        RecordWindowLifecycle("close-request-menu");
        if (!_runtimeReady || UiIsMenuOpen) return;
        UiOpenMenu();
    }

    private void HandleInitializationFailure(Exception exception)
    {
        _runtimeReady = false;
        _startupFailed = true;
        string diagnostic;
        try { diagnostic = BuildIntegratedStartupFailureDiagnostic(exception); }
        catch (Exception diagnosticFailure)
        {
            diagnostic = "Integrated campaign initialization failed and diagnostic context could not be resolved." +
                System.Environment.NewLine + exception + System.Environment.NewLine + "Diagnostic failure: " + diagnosticFailure;
        }
        try { SupportLogger.Log("startup-fatal", diagnostic); }
        catch (Exception loggingFailure)
        {
            GD.PushError("Startup diagnostic logging also failed: " + loggingFailure);
        }
        GD.PushError(diagnostic);

        if (_startupSmokeRequested)
        {
            RequestFailureExit();
            return;
        }

        try
        {
            GetTree().Paused = true;
            ShowInitializationFailure();
        }
        catch (Exception presentationFailure)
        {
            GD.PushError("Startup failure presentation also failed: " + presentationFailure);
            RequestFailureExit();
        }
    }

    private void RequestFailureExit()
    {
        if (_failureExitRequested) return;
        _failureExitRequested = true;
        try { _ = ShutdownAfterStartupFailureAsync(); }
        catch (Exception cleanupStartFailure)
        {
            GD.PushError("Startup audio cleanup could not start: " + cleanupStartFailure);
            GetTree().Quit(1);
        }
    }

    private async Task ShutdownAfterStartupFailureAsync()
    {
        try { await AudioDirector.ShutdownAndQuitAsync(GetTree(), 1); }
        catch (Exception cleanupFailure)
        {
            GD.PushError("Startup audio cleanup failed: " + cleanupFailure);
            GetTree().Quit(1);
        }
    }

    private void ShowInitializationFailure()
    {
        // Early failures can happen before the normal responsive presentation is attached.
        // It must keep processing while the broken campaign tree is paused so resizing the
        // recovery screen retains the same readable 720p contract as the ordinary UI.
        var responsive = GetNodeOrNull<ResponsiveDisplay>("ResponsiveDisplay");
        if (responsive is null)
        {
            responsive = new ResponsiveDisplay { Name = "ResponsiveDisplay", ProcessMode = ProcessModeEnum.Always };
            AddChild(responsive);
        }
        else responsive.ProcessMode = ProcessModeEnum.Always;
        var layer = new CanvasLayer { Name = "StartupFailure", Layer = 1000, ProcessMode = ProcessModeEnum.WhenPaused };
        var backdrop = new ColorRect { Color = new Color("07121f") };
        backdrop.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        layer.AddChild(backdrop);
        var center = new CenterContainer();
        center.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        backdrop.AddChild(center);
        var panel = new PanelContainer { CustomMinimumSize = new Vector2(520, 250) };
        panel.AddThemeStyleboxOverride("panel", new StyleBoxFlat
        {
            BgColor = new Color("10263a"), BorderColor = new Color("4d7897"),
            BorderWidthLeft = 1, BorderWidthTop = 1, BorderWidthRight = 1, BorderWidthBottom = 1,
            CornerRadiusTopLeft = 8, CornerRadiusTopRight = 8, CornerRadiusBottomLeft = 8, CornerRadiusBottomRight = 8,
            ContentMarginLeft = 32, ContentMarginTop = 28, ContentMarginRight = 32, ContentMarginBottom = 28,
        });
        center.AddChild(panel);
        var content = new VBoxContainer(); content.AddThemeConstantOverride("separation", 16); panel.AddChild(content);
        var title = new Label { Text = "CAMPAIGN COULD NOT START", HorizontalAlignment = HorizontalAlignment.Center };
        title.AddThemeFontSizeOverride("font_size", 22); title.AddThemeColorOverride("font_color", new Color("efc778"));
        content.AddChild(title);
        var message = new Label
        {
            Text = "Stellar Continuum stopped before opening the campaign. Check the support log for details, then start again.",
            AutowrapMode = TextServer.AutowrapMode.WordSmart, HorizontalAlignment = HorizontalAlignment.Center,
            CustomMinimumSize = new Vector2(450, 80),
        };
        message.AddThemeFontSizeOverride("font_size", 14); message.AddThemeColorOverride("font_color", new Color("dbe8f0"));
        content.AddChild(message);
        var exit = new Button { Name = "ExitAfterStartupFailure", Text = "Exit safely", CustomMinimumSize = new Vector2(180, 44) };
        exit.Pressed += RequestFailureExit;
        content.AddChild(exit);
        AddChild(layer);
        exit.GrabFocus();
        exit.CallDeferred(Control.MethodName.GrabFocus);
    }
}
