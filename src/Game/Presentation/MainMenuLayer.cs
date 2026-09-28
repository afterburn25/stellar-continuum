using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Diagnostics;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Generation;
using Game.Simulation.Species;

namespace Game.Presentation;

/// <summary>Campaign mode selection. Main owns switching, checkpoints and commands.</summary>
public partial class MainMenuLayer : CanvasLayer
{
    private const string StartupLoadingArtworkPath = "res://assets/visual/loading/stellar-loading-splash.png";
    private const string GenerationLoadingArtworkPath = "res://assets/visual/loading/stellar-galaxy-generation.png";
    private const string SaveLoadingArtworkPath = "res://assets/visual/loading/stellar-save-loading.png";
    private static readonly string[] LoadingTips =
    [
        "Tip: Space pauses or resumes time.",
        "Tip: Use the navigation rail to open research, construction, fleets, and relations.",
        "Tip: Survey nearby systems before sending civilian expeditions.",
        "Tip: Save before changing a major plan.",
        "Tip: Select a fleet to review its orders and current readiness.",
    ];
    private static readonly string[] CampaignLoadAssetPaths =
    [
        "res://assets/visual/space/campaign-galaxy-four-arm-v1.png",
        "res://assets/visual/space/deep-field-v2.png",
        "res://assets/visual/shaders/system_sky.gdshader",
        "res://assets/visual/shaders/stellar_photosphere.gdshader",
        "res://assets/visual/ships/deep-space-science-vessel.jpg",
        "res://assets/visual/sol/earth.jpg",
    ];

    private Main _main = null!;
    private Control _overlay = null!;
    private PanelContainer _campaignModes = null!;
    private Control _newGameSelection = null!;
    private Control _sandboxSetup = null!;
    private LineEdit _sandboxSeed = null!;
    private Label _sandboxSeedResolved = null!;
    private Label _sandboxSummary = null!;
    private OptionButton _sandboxSize = null!, _sandboxRivals = null!, _sandboxAncients = null!;
    private OptionButton _sandboxHabitables = null!, _sandboxAnomalies = null!;
    private Button _copySandboxSetup = null!, _startConfiguredSandbox = null!;
    private readonly Dictionary<string, Button> _sandboxSpeciesChoices = new(StringComparer.Ordinal);
    private string _selectedSandboxSpeciesId = SpeciesCatalog.TerranBaselineId;
    private TextureRect _sandboxSpeciesPortrait = null!;
    private Label _sandboxSpeciesTitle = null!, _sandboxSpeciesStats = null!, _sandboxSpeciesBio = null!;
    private Label _sandboxSpeciesPhysiology = null!, _sandboxSpeciesTraits = null!;
    private Control _loading = null!;
    private Control _audioSettings = null!;
    private Control _videoSettings = null!;
    private Control _settings = null!;
    private Button _settingsAudio = null!;
    private OptionButton _videoResolution = null!, _videoMode = null!, _videoVsync = null!, _videoFrameCap = null!, _videoMsaa = null!, _videoRenderScale = null!;
    private readonly VideoSettingsService _videoService = new();
    private Control _videoRollback = null!;
    private Label _videoRollbackText = null!, _videoError = null!;
    private Button _videoKeep = null!;
    private VideoSettingsService.Settings _videoPrevious;
    private double _videoRollbackSeconds;
    private double _refreshStatePollSeconds;
    private int _refreshStateScreen = int.MinValue;
    private bool _refreshStateFocused;
    private Window.ModeEnum _refreshStateWindowMode = (Window.ModeEnum)(-1);
    private VideoSettingsService.Settings? _refreshStateSettings;
    private bool _videoHasUncommittedChange;
    private Control _development = null!;
    private HSlider _masterVolume = null!, _musicVolume = null!, _sfxVolume = null!;
    private Label _loadingTitle = null!, _loadingStatus = null!, _loadingPercentage = null!, _loadingTip = null!;
    private TextureRect _loadingArtwork = null!;
    private ProgressBar _loadingProgress = null!;
    private ConfirmationDialog _confirmation = null!;
    private Label _confirmationTitle = null!, _confirmationBody = null!;
    private Button _confirmationAccept = null!, _confirmationCancel = null!;
    private Label _saveError = null!;
    private Label _mode = null!;
    private Button _player = null!, _developer = null!, _tools = null!;
    private Button _resume = null!, _load = null!;
    private LineEdit _seed = null!;
    private Action? _confirmedStart;
    private Func<bool>? _confirmedLoad;
    private Func<Action<GalaxyGenerationProgress>, Task<CampaignBootstrapResult>>? _confirmedGeneration;
    private Func<CampaignBootstrapResult, bool>? _confirmedGenerationCommit;
    private int _lastLoadingTip = -1;
    private SimulationClock.SpeedLevel _resumeSpeed = SimulationClock.SpeedLevel.Normal;
    private double _refresh;
    private bool _loadingTransitionActive;
    private bool _loadingLifetimeEnded;
    private readonly Dictionary<string, Resource> _campaignLoadAssets = new(StringComparer.Ordinal);
    public bool IsBlockingGameplay => _loadingTransitionActive || (_overlay?.IsVisibleInTree() ?? false) ||
        (_loading?.IsVisibleInTree() ?? false) || (_confirmation?.Visible ?? false);
    public bool HasLoadingPresentation => _loading is not null &&
        _loading.GetNodeOrNull<TextureRect>("SplashArtwork")?.Texture is { } texture &&
        texture.GetWidth() >= 1280 && texture.GetHeight() >= 720;
    public int LoadingPresentationShownCount { get; private set; }
    public int StartupLoadingPresentationShownCount { get; private set; }
    public bool HasCompletedStartupLoading { get; private set; }
    public double LastLoadingDurationSeconds { get; private set; }
    public int RetainedCampaignLoadAssetCount => _campaignLoadAssets.Count;
    public double UiLoadingProgress => _loadingProgress?.Value ?? 0;
    public string UiLoadingTitle => _loadingTitle?.Text ?? string.Empty;
    public string UiLoadingStatus => _loadingStatus?.Text ?? string.Empty;
    public string UiLoadingTip => _loadingTip?.Text ?? string.Empty;
    public string UiLoadingArtworkPath => _loadingArtwork?.Texture?.ResourcePath ?? string.Empty;
    public bool IsLoadingCampaign => _loading?.IsVisibleInTree() ?? false;
    public bool IsStartupArtworkVisible => (_overlay?.IsVisibleInTree() ?? false) || IsLoadingCampaign;
    public bool IsNewGameSelectionVisible => _newGameSelection?.IsVisibleInTree() ?? false;
    public bool IsSandboxSetupVisible => _sandboxSetup?.IsVisibleInTree() ?? false;
    public bool IsAudioSettingsVisible => _audioSettings?.IsVisibleInTree() ?? false;
    public bool UiCampaignConfirmationVisible => _confirmation?.Visible ?? false;
    public string UiCampaignConfirmationTitle => _confirmationTitle?.Text ?? string.Empty;
    public string UiCampaignConfirmationAcceptText => _confirmationAccept?.Text ?? string.Empty;
    public string UiCampaignConfirmationCancelText => _confirmationCancel?.Text ?? string.Empty;

    public override void _Ready()
    {
        _main = GetParent() as Main ?? throw new InvalidOperationException("MainMenuLayer must be a child of Main.");
        _videoService.LoadAndApply();
        AudioDirector.Instance?.SetMenuContext(true);
        _overlay = new ColorRect { Color = VisualPalette.Canvas };
        VisualUi.ContainPointerInput(_overlay);
        _overlay.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var backdrop = new MainMenuBackdrop();
        backdrop.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        _overlay.AddChild(backdrop);
        _campaignModes = new PanelContainer { Name = "CampaignModes" };
        _campaignModes.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.LeftWide);
        _campaignModes.OffsetLeft = 56; _campaignModes.OffsetRight = 510;
        _campaignModes.OffsetTop = 68; _campaignModes.OffsetBottom = -44;
        _campaignModes.AddThemeStyleboxOverride("panel", new StyleBoxEmpty());
        _overlay.AddChild(_campaignModes);
        var content = new VBoxContainer(); content.AddThemeConstantOverride("separation", 8); _campaignModes.AddChild(content);
        content.AddChild(VisualUi.Text("STELLAR", 48));
        content.AddChild(VisualUi.Text("C O N T I N U U M", 23));
        content.AddChild(VisualUi.Text("THE FIRST LIGHT OF AN INTERSTELLAR AGE", 11, VisualUi.Gold));
        content.AddChild(new Control { CustomMinimumSize = new(0, 30) });
        _resume = AddMenuButton(content, "ResumeCampaign", "Continue", "Return to your campaign.", ContinueCampaign);
        _load = AddMenuButton(content, "LoadCampaign", "Load saved campaign", "Discard unsaved changes and reload this Player campaign from its save slot.", RequestLoadCampaign);
        AddMenuButton(content, "NewPlayerCampaign", "New Game", "Begin a new Player campaign.", RequestNewCampaign);
        _player = AddMenuButton(content, "ModePlayer", "Player campaign", "Open your separate Player campaign.", SwitchToPlayer);
        _load.Visible = !_main.UiIsDeveloperMode;
        _player.Visible = _main.UiIsDeveloperMode;
        AddMenuButton(content, "Settings", "Settings", "Audio, video, controls, voice and accessibility settings.", ShowSettings);
        AddMenuButton(content, "OpenDevelopment", "Development", "Switch to your separate Developer world and tools.", () =>
        {
            _campaignModes.Hide(); _development.Show(); _developer.GrabFocus();
        });
        AddMenuButton(content, "ExitToWindows", "Exit to Windows", "Save this campaign and return to Windows.", _main.UiQuit);
        content.AddChild(new Control { SizeFlagsVertical = Control.SizeFlags.ExpandFill });
        _mode = VisualUi.Text("PLAYER MODE", 11, VisualUi.Accent);
        _mode.Name = "CampaignModeLabel"; content.AddChild(_mode);
        content.AddChild(VisualUi.Text(_main.UiBuildLabel, 10, VisualUi.Muted));
        _saveError = VisualUi.Text("", 13, new Color("efac92"), true);
        _saveError.Name = "CampaignMenuError"; _saveError.Visible = false; content.AddChild(_saveError);
        BuildDevelopmentMenu();
        BuildNewGameSelection();
        BuildSandboxSetup();
        BuildAudioSettings();
        BuildVideoSettings();
        BuildSettingsMenu();
        AddChild(_overlay);
        BuildLoadingPresentation();
        _confirmation = new ConfirmationDialog
        {
            // The stock ConfirmationDialog is a native-looking grey window. Keep the
            // real ConfirmationDialog contract for input/tests, but make it an
            // in-game surface with no OS title bar.
            Title = "Start a new campaign?", DialogAutowrap = true,
            Borderless = true, Unresizable = true,
            // AcceptDialog enables wrap_controls in its native constructor. That
            // mode re-fits this custom content window from child minimums and was
            // expanding the modal to the full 720px viewport.
            WrapControls = false,
        };
        StyleCampaignConfirmation();
        ConfigureCampaignConfirmation(loading: false);
        _confirmation.Confirmed += ConfirmCampaignAction;
        _confirmation.Canceled += ClearConfirmedCampaignAction;
        AddChild(_confirmation);
        GetViewport().GuiFocusChanged += KeepMenuFocus;
        GetWindow().FocusEntered += RefreshRateFocusChanged;
        GetWindow().FocusExited += RefreshRateFocusChanged;
        _resume.GrabFocus();
        _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Paused);
        _main.UiPauseMassiveCombatForMenu();
        BeginStartupLoading();
    }

    public override void _ExitTree()
    {
        _loadingLifetimeEnded = true;
        _videoService.Dispose();
        GetWindow().FocusEntered -= RefreshRateFocusChanged;
        GetWindow().FocusExited -= RefreshRateFocusChanged;
        GetViewport().GuiFocusChanged -= KeepMenuFocus;
    }

    private void RefreshRateFocusChanged() => PollRefreshRateLifecycle(force: true);

    public override void _Notification(int what)
    {
        if (!_loadingLifetimeEnded && IsNodeReady() &&
            (what == (int)NotificationWMPositionChanged || what == (int)NotificationWMSizeChanged))
            PollRefreshRateLifecycle(force: true);
    }

    private void PollRefreshRateLifecycle(bool force = false)
    {
        var window = GetWindow();
        var screen = window.CurrentScreen;
        var focused = window.HasFocus();
        var mode = window.Mode;
        var settings = VideoSettingsService.Current;
        var changed = screen != _refreshStateScreen || focused != _refreshStateFocused ||
            mode != _refreshStateWindowMode || _refreshStateSettings != settings;

        _refreshStateScreen = screen;
        _refreshStateFocused = focused;
        _refreshStateWindowMode = mode;
        _refreshStateSettings = settings;
        if (force || changed || _videoService.RefreshRestorePending)
            _videoService.PollWindowState(window);
    }

    private void KeepMenuFocus(Control focus)
    {
        if (focus is not null && IsBlockingGameplay && !_confirmation.Visible && !_overlay.IsAncestorOf(focus))
            _resume.GrabFocus();
    }

    public override void _Process(double delta)
    {
        _refreshStatePollSeconds += delta;
        if (_refreshStatePollSeconds >= 1.0)
        {
            _refreshStatePollSeconds = 0.0;
            PollRefreshRateLifecycle();
        }
        if (_videoSettings?.Visible == true && !string.IsNullOrEmpty(_videoService.RefreshRateError))
        {
            _videoError.Text = _videoService.RefreshRateError;
            _videoError.Show();
        }
        if (_videoRollback?.Visible == true)
        {
            _videoRollbackSeconds -= delta;
            _videoRollbackText.Text = $"Keep these display settings? Reverting in {Math.Max(0, (int)Math.Ceiling(_videoRollbackSeconds))} seconds.";
            if (_videoRollbackSeconds <= 0) RevertVideoSettings();
        }
        _refresh += delta;
        if (_refresh < .2 || !IsBlockingGameplay) return;
        _refresh = 0;
        _mode.Text = _main.UiModeLabel.ToUpperInvariant() + (_main.UiIsDeveloperMode ? (_main.UiDeveloperToolsUsed ? " · TOOLS USED" : " · TOOLS UNUSED") : "");
        _mode.Modulate = _main.UiIsDeveloperMode ? VisualUi.Gold : VisualUi.Accent;
        _load.Visible = !_main.UiIsDeveloperMode;
        _load.Disabled = !_main.UiHasPlayerSave;
        _player.Visible = _main.UiIsDeveloperMode;
        _player.Text = _main.UiIsDeveloperMode ? (_main.UiHasPlayerSave ? "Resume Player" : "Start Player") : "Player active";
        _developer.Text = _main.UiIsDeveloperMode ? "Load Developer save" : (_main.UiHasDeveloperSave ? "Resume Developer" : "Start Developer");
        _developer.Disabled = _main.UiIsDeveloperMode && !_main.UiHasDeveloperSave;
        _tools.Disabled = !_main.UiIsDeveloperMode;
    }

    private void ContinueCampaign()
    {
        if (_loadingTransitionActive) return;
        _newGameSelection.Hide();
        _sandboxSetup.Hide();
        _development.Hide();
        _campaignModes.Show();
        _audioSettings.Hide();
        _videoSettings.Hide();
        _settings.Hide();
        _overlay.Hide();
        AudioDirector.Instance?.SetMenuContext(false);
        _main.UiResumeAtSpeed(_resumeSpeed);
        _main.UiResumeMassiveCombatAfterMenu();
    }
    public void ShowMenu()
    {
        if (_loadingTransitionActive || _overlay.IsVisibleInTree()) return;
        _main.GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.Close();
        _resumeSpeed = _main.UiCurrentSpeed;
        _main.UiPauseMassiveCombatForMenu();
        _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Paused);
        _newGameSelection.Hide();
        _sandboxSetup.Hide();
        _audioSettings.Hide();
        _videoSettings.Hide();
        _settings.Hide();
        _development.Hide();
        _campaignModes.Show();
        _overlay.Show();
        AudioDirector.Instance?.SetMenuContext(true);
        _resume.GrabFocus();
    }
    public void ShowSaveFailure(string message) { _saveError.Text = message; _saveError.Show(); }
    public void ClearSaveFailure() => _saveError.Hide();
    public void RequestNewCampaign()
    {
        ShowMenu();
        _campaignModes.Hide();
        _sandboxSetup.Hide();
        _newGameSelection.Show();
        _newGameSelection.GetNode<Button>("NewGamePanel/Body/Choices/SandboxCampaignOption").GrabFocus();
    }
    private void RequestSandboxCampaign()
    {
        _newGameSelection.Hide();
        _sandboxSetup.Show();
        if (string.IsNullOrWhiteSpace(_sandboxSeed.Text)) RandomizeSandboxSeed();
        _sandboxSeed.GrabFocus();
    }
    private void RequestDeveloperCampaign()
    {
        if (!long.TryParse(_seed.Text.Trim(), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var seed))
        { ShowSaveFailure("Enter a whole-number seed from −9223372036854775808 to 9223372036854775807."); _seed.GrabFocus(); return; }
        ShowMenu(); _confirmedStart = null;
        _confirmedGeneration = progress => _main.UiPrepareDeveloperCampaignAsync(seed, progress);
        _confirmedGenerationCommit = _main.UiCommitPreparedDeveloperCampaign;
        _confirmedLoad = null;
        ConfigureCampaignConfirmation(loading: false);
        SetCampaignConfirmationText($"Start a fresh Developer campaign with seed {seed}? The current campaign will be saved first. The previous Developer save is kept as its backup; Player saves stay separate. Tools run only when you choose them.");
        ShowCampaignConfirmation(new(620, 260));
    }
    private async void ConfirmCampaignAction()
    {
        var load = _confirmedLoad; _confirmedLoad = null;
        if (load is not null)
        {
            _confirmedStart = null;
            await RunSavedCampaignLoadingAsync();
            return;
        }
        var prepare = _confirmedGeneration; _confirmedGeneration = null;
        var commit = _confirmedGenerationCommit; _confirmedGenerationCommit = null;
        if (prepare is not null && commit is not null)
        {
            _confirmedStart = null;
            if (!_main.UiCheckpointBeforeCampaignSwitch()) return;
            await RunGenerationLoadingAsync(prepare, commit);
            return;
        }
        var start = _confirmedStart; _confirmedStart = null;
        if (start is null || !_main.UiCheckpointBeforeCampaignSwitch()) return;
        await RunLoadingAsync("Generating a new 100-star campaign", () => { start(); return true; });
    }
    private void ClearConfirmedCampaignAction()
    {
        _confirmedStart = null; _confirmedLoad = null;
        _confirmedGeneration = null; _confirmedGenerationCommit = null;
    }
    private void RequestLoadCampaign()
    {
        ShowMenu();
        _development.Hide();
        _newGameSelection.Hide();
        _sandboxSetup.Hide();
        _campaignModes.Show();
        _confirmedStart = null;
        _confirmedLoad = _main.UiLoadCurrentCampaign;
        _confirmedGeneration = null; _confirmedGenerationCommit = null;
        ConfigureCampaignConfirmation(loading: true);
        SetCampaignConfirmationText($"Load the saved {_main.UiModeLabel} campaign? Unsaved changes in the current campaign will be discarded. Loading does not overwrite the save or the other mode's campaign.");
        ShowCampaignConfirmation(new(640, 250));
    }
    private async void SwitchToPlayer()
    {
        if (!_main.UiIsDeveloperMode) { ContinueCampaign(); return; }
        await RunLoadingAsync("Loading the Player campaign", _main.UiSwitchToPlayerMode);
    }
    private async void SwitchToDeveloper()
    {
        if (_main.UiIsDeveloperMode) { RequestLoadCampaign(); return; }
        await RunLoadingAsync("Loading the Developer campaign", _main.UiSwitchToDeveloperMode);
    }
    private void OpenTools()
    {
        if (!_main.UiIsDeveloperMode) return;
        ContinueCampaign(); _main.UiOpenDeveloperTools();
    }
    public override void _Input(InputEvent input)
    {
        if (!IsBlockingGameplay || !input.IsActionPressed("ui_cancel")) return;
        if (_loadingTransitionActive)
        {
            GetViewport().SetInputAsHandled();
            return;
        }
        if (_videoRollback.Visible) RevertVideoSettings();
        else if (_confirmation.Visible) { _confirmation.Hide(); ClearConfirmedCampaignAction(); }
        else if (_newGameSelection.Visible)
        {
            _newGameSelection.Hide();
            _campaignModes.Show();
            _resume.GrabFocus();
        }
        else if (_sandboxSetup.Visible)
        {
            _sandboxSetup.Hide();
            _newGameSelection.Show();
            _newGameSelection.GetNode<Button>("NewGamePanel/Body/Choices/SandboxCampaignOption").GrabFocus();
        }
        else if (_audioSettings.Visible)
        {
            _audioSettings.Hide();
            _settings.Show();
        }
        else if (_videoSettings.Visible) CancelVideoSettings();
        else if (_settings.Visible) { _settings.Hide(); _campaignModes.Show(); _resume.GrabFocus(); }
        else if (_development.Visible) CloseDevelopmentMenu();
        else ContinueCampaign();
        GetViewport().SetInputAsHandled();
    }

    private void BuildSandboxSetup()
    {
        _sandboxSetup = new MarginContainer { Name = "SandboxSetup", Visible = false };
        _sandboxSetup.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        _sandboxSetup.AddThemeConstantOverride("margin_left", 18);
        _sandboxSetup.AddThemeConstantOverride("margin_top", 18);
        _sandboxSetup.AddThemeConstantOverride("margin_right", 18);
        _sandboxSetup.AddThemeConstantOverride("margin_bottom", 18);
        var center = new CenterContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, SizeFlagsVertical = Control.SizeFlags.ExpandFill };
        _sandboxSetup.AddChild(center);
        var panel = new PanelContainer
        {
            Name = "SandboxSetupPanel", CustomMinimumSize = new Vector2(1000, 0),
            SizeFlagsVertical = Control.SizeFlags.ExpandFill,
        };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 20)); center.AddChild(panel);
        void FitSandboxPanel() => panel.CustomMinimumSize = new Vector2(
            Math.Min(1120, Math.Max(0, _overlay.Size.X - 36)),
            Math.Min(840, Math.Max(0, _overlay.Size.Y - 36)));
        _overlay.Resized += FitSandboxPanel;
        FitSandboxPanel();
        var layout = new VBoxContainer { Name = "SandboxSetupLayout", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, SizeFlagsVertical = Control.SizeFlags.ExpandFill };
        layout.AddThemeConstantOverride("separation", 7); panel.AddChild(layout);
        var heading = new HBoxContainer(); layout.AddChild(heading);
        var title = VisualUi.Text("CONFIGURE SANDBOX", 26); title.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill; heading.AddChild(title);
        var back = VisualUi.Button("Back", "Return to game type selection.", () =>
        {
            _sandboxSetup.Hide(); _newGameSelection.Show();
            _newGameSelection.GetNode<Button>("NewGamePanel/Body/Choices/SandboxCampaignOption").GrabFocus();
        }, VisualIconLibrary.NavBack);
        back.Name = "SandboxSetupBack"; heading.AddChild(back);
        layout.AddChild(VisualUi.Text("Build a reproducible galaxy with the rules and scale you choose.", 13, VisualUi.Muted));
        var setupScroll = new ScrollContainer
        {
            Name = "SandboxSetupScroll", VerticalScrollMode = ScrollContainer.ScrollMode.Auto,
            HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            SizeFlagsVertical = Control.SizeFlags.ExpandFill,
        };
        layout.AddChild(setupScroll);
        var body = new VBoxContainer { Name = "Body", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        body.AddThemeConstantOverride("separation", 7); setupScroll.AddChild(body);

        var speciesPanel = new PanelContainer { Name = "SandboxSpeciesSelection" };
        speciesPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface(true, 8)); body.AddChild(speciesPanel);
        var speciesRow = new HBoxContainer(); speciesRow.AddThemeConstantOverride("separation", 10); speciesPanel.AddChild(speciesRow);
        var speciesChoices = new VBoxContainer { Name = "SandboxSpeciesChoices", CustomMinimumSize = new Vector2(270, 0) };
        speciesChoices.AddThemeConstantOverride("separation", 3);
        speciesChoices.AddChild(VisualUi.Text("PLAYABLE SPECIES", 12, VisualUi.Gold));
        foreach (var species in SpeciesCatalog.All)
        {
            var choice = new Button
            {
                Name = $"SandboxSpecies_{species.Id}", Text = species.DisplayName,
                Icon = VisualIconLibrary.Get(CivilizationArtworkLibrary.PathForSpecies(species.Id)),
                ExpandIcon = true, CustomMinimumSize = new Vector2(270, 48),
                TooltipText = $"Select {species.DisplayName}.",
            };
            choice.AddThemeConstantOverride("icon_max_width", 48);
            choice.AddThemeFontSizeOverride("font_size", 13);
            AudioDirector.Bind(choice);
            choice.Pressed += () => SelectSandboxSpecies(species.Id);
            _sandboxSpeciesChoices.Add(species.Id, choice);
            speciesChoices.AddChild(choice);
        }
        speciesRow.AddChild(speciesChoices);

        var detail = new VBoxContainer { Name = "SandboxSpeciesDetails", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        detail.AddThemeConstantOverride("separation", 3);
        var identity = new HBoxContainer(); identity.AddThemeConstantOverride("separation", 10); detail.AddChild(identity);
        _sandboxSpeciesPortrait = new TextureRect
        {
            Name = "SandboxSpeciesPortrait", CustomMinimumSize = new Vector2(120, 120),
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered,
        };
        identity.AddChild(_sandboxSpeciesPortrait);
        var identityText = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill }; identity.AddChild(identityText);
        _sandboxSpeciesTitle = VisualUi.Text("", 15, VisualUi.Accent); _sandboxSpeciesTitle.Name = "SandboxSpeciesTitle"; identityText.AddChild(_sandboxSpeciesTitle);
        _sandboxSpeciesBio = VisualUi.Text("", 11, VisualUi.Muted, true); _sandboxSpeciesBio.Name = "SandboxSpeciesBio"; identityText.AddChild(_sandboxSpeciesBio);
        var detailScroll = new ScrollContainer { Name = "SandboxSpeciesDetailScroll", VerticalScrollMode = ScrollContainer.ScrollMode.Auto, SizeFlagsVertical = Control.SizeFlags.ExpandFill };
        detail.AddChild(detailScroll);
        var detailBody = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        detailBody.AddThemeConstantOverride("separation", 2); detailScroll.AddChild(detailBody);
        detailBody.AddChild(VisualUi.Text("HOMEWORLD CONDITIONS", 11, VisualUi.Gold));
        _sandboxSpeciesStats = VisualUi.Text("", 11, VisualUi.PrimaryText, true); _sandboxSpeciesStats.Name = "SandboxSpeciesStats"; detailBody.AddChild(_sandboxSpeciesStats);
        detailBody.AddChild(VisualUi.Text("PHYSIOLOGY", 11, VisualUi.Gold));
        _sandboxSpeciesPhysiology = VisualUi.Text("", 11, VisualUi.PrimaryText, true); _sandboxSpeciesPhysiology.Name = "SandboxSpeciesPhysiology"; detailBody.AddChild(_sandboxSpeciesPhysiology);
        detailBody.AddChild(VisualUi.Text("TRAITS", 11, VisualUi.Gold));
        _sandboxSpeciesTraits = VisualUi.Text("", 11, VisualUi.PrimaryText, true); _sandboxSpeciesTraits.Name = "SandboxSpeciesTraits"; detailBody.AddChild(_sandboxSpeciesTraits);
        speciesRow.AddChild(detail);

        var seedPanel = new PanelContainer(); seedPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface(true, 12)); body.AddChild(seedPanel);
        var seedBody = new VBoxContainer(); seedBody.AddThemeConstantOverride("separation", 3); seedPanel.AddChild(seedBody);
        seedBody.AddChild(VisualUi.Text("GALAXY SEED", 13, VisualUi.Gold));
        _sandboxSeed = new LineEdit { Name = "SandboxSeed", PlaceholderText = "Number or memorable text", MaxLength = 80, CustomMinimumSize = new Vector2(0, 28) };
        _sandboxSeed.TextChanged += _ => RefreshSandboxSetup(); seedBody.AddChild(_sandboxSeed);
        _sandboxSeedResolved = VisualUi.Text("", 11, VisualUi.Muted); _sandboxSeedResolved.Name = "ResolvedSeed"; seedBody.AddChild(_sandboxSeedResolved);
        var seedActions = new HBoxContainer(); seedActions.AddThemeConstantOverride("separation", 8); seedBody.AddChild(seedActions);
        CompactButton(seedActions, "RandomizeSandboxSeed", "Randomize", "Generate a fresh seed.", RandomizeSandboxSeed, VisualIconLibrary.NavGalaxy);
        _copySandboxSetup = CompactButton(seedActions, "CopySandboxSetup", "Copy setup", "Copy every selected option and the seed to the clipboard.", CopySandboxSetup, VisualIconLibrary.Save);
        CompactButton(seedActions, "RestoreSandboxDefaults", "Restore defaults", "Restore the recommended setup and generate a fresh seed.", RestoreSandboxDefaults, VisualIconLibrary.NavHome);

        var settingsPanel = new PanelContainer();
        settingsPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface(true, 9));
        body.AddChild(settingsPanel);
        var settings = new VBoxContainer(); settings.AddThemeConstantOverride("separation", 3); settingsPanel.AddChild(settings);
        settings.AddChild(VisualUi.Text("GALAXY CONDITIONS", 13, VisualUi.Gold));
        settings.AddChild(VisualUi.Text("Every size follows the same realistic stellar and planetary generation rules. These choices shape the campaign before generation begins.", 11, VisualUi.Muted, true));
        var optionRows = new HBoxContainer { Name = "SandboxGalaxyOptions" };
        optionRows.AddThemeConstantOverride("separation", 10); settings.AddChild(optionRows);
        var leftOptions = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        leftOptions.AddThemeConstantOverride("separation", 3); optionRows.AddChild(leftOptions);
        var rightOptions = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        rightOptions.AddThemeConstantOverride("separation", 3); optionRows.AddChild(rightOptions);
        _sandboxSize = AddSandboxOption(leftOptions, "SandboxGalaxySize", "GALAXY SIZE", "More systems enlarge exploration and increase generation time.",
            ("Small · 250 systems", "250"), ("Medium · 500 systems", "500"), ("Large · 1,000 systems", "1000"), ("Huge · 2,500 systems", "2500"));
        _sandboxRivals = AddSandboxOption(leftOptions, "SandboxRivalEmpires", "RIVAL EMPIRES", "More rivals create more early borders, diplomacy, and competition.",
            ("None", "0"), ("Sparse · 3", "3"), ("Standard · 5", "5"), ("Crowded · 8", "8"), ("Packed · 12", "12"));
        _sandboxAncients = AddSandboxOption(leftOptions, "SandboxAncientEmpires", "ANCIENT EMPIRES", "Ancient empires add old powers and high-risk discoveries.",
            ("None", "None"), ("Rare", "Rare"), ("Standard", "Standard"));
        _sandboxHabitables = AddSandboxOption(rightOptions, "SandboxHabitableWorlds", "HABITABLE WORLDS", "More habitable worlds create more viable colony destinations.",
            ("Rare", "Rare"), ("Uncommon", "Uncommon"), ("Common", "Common"));
        _sandboxAnomalies = AddSandboxOption(rightOptions, "SandboxAnomalyFrequency", "ANOMALY FREQUENCY", "More anomalies add more discoveries and exploration decisions.",
            ("Low", "Low"), ("Standard", "Standard"), ("High", "High"));
        SetSandboxOptionDefaults();
        var footer = new VBoxContainer { Name = "SandboxSetupFooter" };
        footer.AddThemeConstantOverride("separation", 4); layout.AddChild(footer);
        _sandboxSummary = VisualUi.Text("", 12, VisualUi.Accent, true);
        _sandboxSummary.Name = "SandboxSummary";
        _sandboxSummary.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        footer.AddChild(_sandboxSummary);
        _startConfiguredSandbox = AddButton(footer, "StartConfiguredSandbox", "Generate campaign", "Create this reproducible Player campaign.", StartConfiguredSandbox, VisualIconLibrary.NavGalaxy);
        _overlay.AddChild(_sandboxSetup);
        SelectSandboxSpecies(_selectedSandboxSpeciesId, refresh: false);
        RandomizeSandboxSeed();
    }

    private void RandomizeSandboxSeed()
    {
        _sandboxSeed.Text = CampaignSeed.CreateRandomNumericText();
        RefreshSandboxSetup();
    }

    private void RestoreSandboxDefaults()
    {
        SelectSandboxSpecies(SpeciesCatalog.TerranBaselineId, refresh: false);
        SetSandboxOptionDefaults();
        RandomizeSandboxSeed();
    }

    private OptionButton AddSandboxOption(Container parent, string name, string title, string help,
        params (string Label, string Value)[] choices)
    {
        var row = new HBoxContainer { Name = name + "Row" };
        row.AddThemeConstantOverride("separation", 7); parent.AddChild(row);
        var label = VisualUi.Text(title, 11, VisualUi.Gold);
        label.TooltipText = help;
        label.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        row.AddChild(label);
        var option = new OptionButton { Name = name, TooltipText = help, CustomMinimumSize = new Vector2(172, 28) };
        foreach (var choice in choices)
        {
            option.AddItem(choice.Label);
            option.SetItemMetadata(option.ItemCount - 1, choice.Value);
        }
        option.ItemSelected += _ => RefreshSandboxSetup();
        row.AddChild(option);
        return option;
    }

    private void SetSandboxOptionDefaults()
    {
        _sandboxSize.Select(1);
        _sandboxRivals.Select(2);
        _sandboxAncients.Select(1);
        _sandboxHabitables.Select(1);
        _sandboxAnomalies.Select(1);
    }

    private GalaxyGenerationMetadata BuildSandboxMetadata(string enteredSeed, long internalSeed)
    {
        var metadata = GalaxyGenerationMetadata.FullGalaxy500(
            enteredSeed, internalSeed, SelectedSandboxSpeciesId(), SelectedSandboxSize()) with
        {
            OtherCivilizations = SelectedSandboxInt(_sandboxRivals),
            AncientCivilizations = SelectedSandboxText(_sandboxAncients),
            HabitableWorlds = SelectedSandboxText(_sandboxHabitables),
            AnomalyFrequency = SelectedSandboxText(_sandboxAnomalies),
        };
        return metadata;
    }

    private int SelectedSandboxSize() => SelectedSandboxInt(_sandboxSize);

    private static int SelectedSandboxInt(OptionButton option) => int.Parse(
        SelectedSandboxText(option).Split(' ', StringSplitOptions.RemoveEmptyEntries)[0], CultureInfo.InvariantCulture);

    private static string SelectedSandboxText(OptionButton option) => option.GetItemMetadata(option.Selected).AsString();

    private static string SandboxSetupSummary(GalaxyGenerationMetadata metadata) =>
        $"{metadata.SystemCount:N0} systems · {metadata.OtherCivilizations} rival empires · {metadata.AncientCivilizations} ancient empires · " +
        $"{metadata.HabitableWorlds} habitable worlds · {metadata.AnomalyFrequency} anomalies. " +
        "Borders and exploration respond to these choices.";

    private void RefreshSandboxSetup()
    {
        try
        {
            var entered = _sandboxSeed.Text.Trim();
            var internalSeed = CampaignSeed.Parse(entered);
            var metadata = BuildSandboxMetadata(entered, internalSeed);
            _sandboxSeedResolved.Text = "Seed ready · use Copy setup to share this galaxy configuration.";
            _sandboxSummary.Text = SandboxSetupSummary(metadata);
            _copySandboxSetup.Disabled = false;
            _startConfiguredSandbox.Disabled = false;
            _saveError.Hide();
        }
        catch (ArgumentException ex)
        {
            _sandboxSeedResolved.Text = ex.Message;
            _sandboxSummary.Text = "Enter a seed to preview this campaign setup.";
            _copySandboxSetup.Disabled = true;
            _startConfiguredSandbox.Disabled = true;
        }
    }

    private void CopySandboxSetup()
    {
        try
        {
            var entered = _sandboxSeed.Text.Trim();
            var metadata = BuildSandboxMetadata(entered, CampaignSeed.Parse(entered));
            DisplayServer.ClipboardSet($"Stellar Continuum Sandbox | Seed: {entered} | Size: {metadata.SystemCount} systems | Rivals: {metadata.OtherCivilizations} | Ancient empires: {metadata.AncientCivilizations} | Habitable worlds: {metadata.HabitableWorlds} | Anomalies: {metadata.AnomalyFrequency} | Species: {SpeciesCatalog.Get(metadata.PlayerSpeciesId!).DisplayName}");
            _sandboxSeedResolved.Text = "Seed and galaxy options copied.";
        }
        catch (ArgumentException) { RefreshSandboxSetup(); _sandboxSeed.GrabFocus(); }
    }

    private void StartConfiguredSandbox()
    {
        var entered = _sandboxSeed.Text.Trim();
        GalaxyGenerationMetadata metadata;
        try { metadata = BuildSandboxMetadata(entered, CampaignSeed.Parse(entered)); }
        catch (ArgumentException ex) { _sandboxSeedResolved.Text = ex.Message; _sandboxSeed.GrabFocus(); return; }
        var species = SpeciesCatalog.Get(metadata.PlayerSpeciesId!);
        _confirmedStart = null;
        // Capture the immutable metadata now. Subsequent setup edits cannot alter a confirmed generation.
        _confirmedGeneration = progress => _main.UiPrepareNewCampaignAsync(metadata, progress);
        _confirmedGenerationCommit = bootstrap => _main.UiCommitPreparedNewCampaign(bootstrap, entered);
        _confirmedLoad = null;
        ConfigureCampaignConfirmation(loading: false);
        SetCampaignConfirmationText($"Generate a fresh {metadata.SystemCount:N0}-system campaign for {species.DisplayName} with seed '{entered}'? {metadata.OtherCivilizations} rival empires, {metadata.AncientCivilizations.ToLowerInvariant()} ancient empires, {metadata.HabitableWorlds.ToLowerInvariant()} habitable worlds, and {metadata.AnomalyFrequency.ToLowerInvariant()} anomalies. The current Player campaign will be checkpointed first.");
        ShowCampaignConfirmation(new(650, 250));
    }

    private void StyleCampaignConfirmation()
    {
        _confirmation.Name = "CampaignConfirmation";
        _confirmation.AddThemeStyleboxOverride("panel", CinematicArt.Frame("panel", 22));
        _confirmation.AddThemeColorOverride("font_color", new Color("e8eee9"));
        _confirmation.AddThemeColorOverride("font_outline_color", new Color(0, 0, 0, .55f));
        _confirmation.AddThemeConstantOverride("outline_size", 2);
        _confirmation.AddThemeFontSizeOverride("font_size", 15);

        // AcceptDialog positions each direct content child into the same content
        // rectangle; its built-in message label is an internal direct child of the
        // Window, not a VBox. Hide that renderer and mirror DialogText into our own
        // VBox so heading, rule, and body receive real vertical layout.
        var bodyLabel = _confirmation.GetLabel();
        bodyLabel.Visible = false;
        // AcceptDialog measures every direct Control child, even an invisible
        // label. Exclude this required internal renderer so its autowrapped
        // DialogText cannot inflate the modal to a full-height window.
        bodyLabel.SetAsTopLevel(true);
        bodyLabel.CustomMinimumSize = Vector2.Zero;
        var body = new VBoxContainer
        {
            Name = "CampaignConfirmationContent",
            CustomMinimumSize = new Vector2(520, 120),
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill,
            MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        body.AddThemeConstantOverride("separation", 10);
        _confirmation.AddChild(body);
        var heading = VisualUi.Text("START A NEW CAMPAIGN?", 23, VisualUi.Gold);
        heading.Name = "CampaignConfirmationTitle";
        heading.MouseFilter = Control.MouseFilterEnum.Ignore;
        body.AddChild(heading);

        var rule = new ColorRect
        {
            Name = "CampaignConfirmationRule", Color = new Color("587d73"),
            CustomMinimumSize = new Vector2(0, 1), MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        body.AddChild(rule);

        _confirmationBody = VisualUi.Text("", 15, new Color("c2cfca"), wrap: false);
        _confirmationBody.Name = "CampaignConfirmationBody";
        _confirmationBody.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        _confirmationBody.CustomMinimumSize = new Vector2(0, 72);
        body.AddChild(_confirmationBody);

        _confirmationTitle = heading;
        _confirmationAccept = _confirmation.GetOkButton();
        _confirmationCancel = _confirmation.GetCancelButton();
        StyleConfirmationButton(_confirmationAccept, highlighted: true);
        StyleConfirmationButton(_confirmationCancel, highlighted: false);
    }

    private void ConfigureCampaignConfirmation(bool loading)
    {
        _confirmation.Title = loading ? "Load saved campaign?" : "Start a new campaign?";
        _confirmationTitle.Text = loading ? "LOAD SAVED CAMPAIGN?" : "START A NEW CAMPAIGN?";
        _confirmationAccept.Text = loading ? "LOAD CAMPAIGN" : "START CAMPAIGN";
        _confirmationAccept.TooltipText = loading
            ? "Load the saved campaign and discard unsaved changes."
            : "Save the current campaign, then begin a fresh campaign.";
        _confirmationCancel.Text = loading ? "KEEP CURRENT CAMPAIGN" : "CANCEL";
        _confirmationCancel.TooltipText = loading
            ? "Keep the current campaign and return to the menu."
            : "Keep the current campaign and return to setup.";
    }

    private static void StyleConfirmationButton(Button button, bool highlighted)
    {
        button.CustomMinimumSize = new Vector2(180, 42);
        button.FocusMode = Control.FocusModeEnum.All;
        button.AddThemeFontSizeOverride("font_size", 13);
        button.AddThemeColorOverride("font_color", highlighted ? VisualUi.Gold : new Color("d0d9d5"));
        button.AddThemeColorOverride("font_hover_color", Colors.White);
        button.AddThemeStyleboxOverride("normal", CinematicArt.Frame(highlighted ? "button-hover" : "button", 9));
        button.AddThemeStyleboxOverride("hover", CinematicArt.Frame("button-hover", 9));
        button.AddThemeStyleboxOverride("pressed", CinematicArt.Frame("button-pressed", 9));
        button.AddThemeStyleboxOverride("focus", CinematicArt.Frame("button-hover", 9));
    }

    private void ShowCampaignConfirmation(Vector2I size)
    {
        _confirmation.Size = size;
        _confirmation.PopupCentered(size);
        // Popup layout can apply the Window minimum one more time; restore the
        // deliberate cinematic footprint after it has become visible.
        _confirmation.Size = size;
        // Starting a campaign is destructive. Make the reversible action the
        // default keyboard focus, while Enter still explicitly confirms.
        CallDeferred(nameof(FocusSafeCampaignCancel));
    }

    private void FocusSafeCampaignCancel()
    {
        if (_confirmation.Visible) _confirmation.GetCancelButton().GrabFocus();
    }

    private void SetCampaignConfirmationText(string text)
    {
        // Preserve the public/native DialogText contract for callers and probes;
        // the dedicated body label is the correctly laid-out visual renderer.
        _confirmation.DialogText = text;
        if (_confirmationBody is null) return;
        // Wrap using measured glyph widths before popup layout. Godot's first-layout
        // autowrap minimum can otherwise treat this label as zero pixels wide.
        var font = ThemeDB.FallbackFont;
        var lines = new System.Collections.Generic.List<string>();
        var line = string.Empty;
        foreach (var word in text.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            var candidate = line.Length == 0 ? word : line + " " + word;
            if (line.Length > 0 && font.GetStringSize(candidate, fontSize: 15).X > 560)
            { lines.Add(line); line = word; }
            else line = candidate;
        }
        if (line.Length > 0) lines.Add(line);
        _confirmationBody.Text = string.Join("\n", lines);
    }

    private string SelectedSandboxSpeciesId()
    {
        return _selectedSandboxSpeciesId;
    }

    private void SelectSandboxSpecies(string speciesId, bool refresh = true)
    {
        var species = SpeciesCatalog.Get(speciesId);
        _selectedSandboxSpeciesId = species.Id;
        _sandboxSpeciesPortrait.Texture = VisualIconLibrary.Get(CivilizationArtworkLibrary.PathForSpecies(species.Id));
        _sandboxSpeciesTitle.Text = species.DisplayName.ToUpperInvariant();
        _sandboxSpeciesBio.Text = SpeciesBiography(species);
        _sandboxSpeciesStats.Text =
            $"Comfortable: {Band(species.Environment.GravityG, "g", 2)} · {Band(species.Environment.TemperatureKelvin, "K", 0)} · {Band(species.Environment.PressureKPa, "kPa", 0)}\n" +
            $"Atmosphere: {Words(species.Environment.PreferredAtmosphere)} · Solvent: {Words(species.Environment.BiologicalSolvent)}";
        _sandboxSpeciesPhysiology.Text =
            $"Adult mass {species.Physiology.TypicalAdultMassKg:0} kg · Maturity {species.Physiology.MaturityAgeYears:0} years · Lifespan {species.Physiology.BaselineLifespanYears:0} years";
        var traits = $"Metabolic demand {species.Physiology.BaselineMetabolicDemand:0.##}× Terran baseline · " +
            $"Radiation tolerance {species.Physiology.RadiationTolerance * 100:0}/100\n" +
            $"Structural robustness {species.Physiology.MusculoskeletalRobustness * 100:0}/100";
        _sandboxSpeciesTraits.Text = species.Environment.RequiresImmersion
            ? traits + "\nRequires an immersed workspace."
            : traits;
        foreach (var pair in _sandboxSpeciesChoices)
        {
            var selected = pair.Key == species.Id;
            pair.Value.Text = (selected ? "✓ " : string.Empty) + SpeciesCatalog.Get(pair.Key).DisplayName;
            pair.Value.Modulate = Colors.White;
            pair.Value.AddThemeColorOverride("font_color", selected ? VisualUi.Accent : VisualUi.PrimaryText);
        }
        if (refresh) RefreshSandboxSetup();
    }

    private static string Band(ToleranceBand band, string unit, int decimals)
    {
        var format = decimals == 0 ? "0" : "0.##";
        return $"{(band.Preferred - band.ComfortableDeviation).ToString(format)}–{(band.Preferred + band.ComfortableDeviation).ToString(format)} {unit}";
    }

    private static string SpeciesBiography(SpeciesDefinition species) => species.Id switch
    {
        SpeciesCatalog.TerranBaselineId => "An oxygen-breathing, water-based people shaped for open terrestrial worlds.",
        SpeciesCatalog.PelagicHighPressureId => "Aquatic, water-based people whose free-swimming lives depend on pressure and buoyancy.",
        SpeciesCatalog.CompactHighGravityId => "Dense, water-based terrestrial people adapted to high gravity and oxygen-rich air.",
        SpeciesCatalog.CryogenicHydrocarbonId => "Hydrocarbon-based terrestrial people whose slow lives suit cold, reducing worlds.",
        _ => $"A {Words(species.Biochemistry).ToLowerInvariant()} species adapted to {Words(species.HabitatMode).ToLowerInvariant()} habitats.",
    };

    private static string Words<T>(T value) where T : Enum
    {
        var source = value.ToString();
        var result = new System.Text.StringBuilder(source.Length + 8);
        for (var index = 0; index < source.Length; index++)
        {
            if (index > 0 && char.IsUpper(source[index])) result.Append(' ');
            result.Append(source[index]);
        }
        return result.ToString();
    }

    private void BuildNewGameSelection()
    {
        _newGameSelection = new CenterContainer { Name = "NewGameSelection", Visible = false };
        _newGameSelection.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var panel = new PanelContainer { Name = "NewGamePanel", CustomMinimumSize = new Vector2(760, 0) };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 20));
        _newGameSelection.AddChild(panel);
        var body = new VBoxContainer { Name = "Body" };
        body.AddThemeConstantOverride("separation", 12); panel.AddChild(body);
        var heading = new HBoxContainer(); body.AddChild(heading);
        var title = VisualUi.Text("CHOOSE YOUR GAME", 26); title.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        heading.AddChild(title);
        var back = VisualUi.Button("Back", "Return to campaign options.", () =>
        {
            _newGameSelection.Hide(); _campaignModes.Show(); _resume.GrabFocus();
        }, VisualIconLibrary.NavBack);
        back.Name = "NewGameBack"; heading.AddChild(back);
        body.AddChild(VisualUi.Text("Select how your civilization's journey will begin.", 13, VisualUi.Muted));
        var choices = new HBoxContainer { Name = "Choices" };
        choices.AddThemeConstantOverride("separation", 14); body.AddChild(choices);
        choices.AddChild(GameTypeCard("StoryCampaignOption", "STORY CAMPAIGN",
            "A guided narrative with authored characters, conflicts and discoveries.",
            "res://assets/visual/loading/stellar-continuum-splash.png", enabled: false, action: null));
        choices.AddChild(GameTypeCard("SandboxCampaignOption", "SANDBOX",
            "Set the galaxy scale, rivals, ancient empires, worlds, anomalies, and your people.",
            "res://assets/visual/space/campaign-galaxy-four-arm-v1.png", enabled: true, RequestSandboxCampaign));
        _overlay.AddChild(_newGameSelection);
    }

    private static Button GameTypeCard(string name, string title, string description,
        string artworkPath, bool enabled, Action? action)
    {
        var button = new Button
        {
            Name = name, Disabled = !enabled, ClipContents = true,
            CustomMinimumSize = new Vector2(350, 310),
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill,
            FocusMode = enabled ? Control.FocusModeEnum.All : Control.FocusModeEnum.None,
            TooltipText = enabled ? $"Begin {title}." : "Story Campaign is coming soon.",
        };
        if (action is not null) button.Pressed += action;
        AudioDirector.Bind(button);
        foreach (var state in new[] { "normal", "hover", "pressed", "disabled", "focus" })
        {
            var style = VisualUi.Surface(highlighted: enabled && state is "hover" or "focus", margin: 10);
            style.BgColor = enabled ? new Color(.015f, .035f, .055f, .98f) : new Color(.025f, .028f, .032f, .98f);
            button.AddThemeStyleboxOverride(state, style);
        }
        var column = new VBoxContainer { MouseFilter = Control.MouseFilterEnum.Ignore };
        column.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        column.OffsetLeft = 10; column.OffsetRight = -10; column.OffsetTop = 10; column.OffsetBottom = -10;
        column.AddThemeConstantOverride("separation", 9); button.AddChild(column);
        var artFrame = new Control { CustomMinimumSize = new Vector2(0, 190), MouseFilter = Control.MouseFilterEnum.Ignore };
        artFrame.ClipContents = true; column.AddChild(artFrame);
        var art = new TextureRect
        {
            Name = name + "Artwork", Texture = GD.Load<Texture2D>(artworkPath),
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
            MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        art.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); artFrame.AddChild(art);
        if (!enabled)
        {
            var veil = new ColorRect { Color = new Color(.05f, .055f, .06f, .72f), MouseFilter = Control.MouseFilterEnum.Ignore };
            veil.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); artFrame.AddChild(veil);
            var comingSoon = new CenterContainer { MouseFilter = Control.MouseFilterEnum.Ignore };
            comingSoon.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); artFrame.AddChild(comingSoon);
            var banner = new PanelContainer { MouseFilter = Control.MouseFilterEnum.Ignore };
            banner.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 10)); comingSoon.AddChild(banner);
            var label = VisualUi.Text("COMING SOON", 18, VisualUi.Gold); label.MouseFilter = Control.MouseFilterEnum.Ignore;
            banner.AddChild(label);
        }
        var modeTitle = VisualUi.Text(title, 20, enabled ? Colors.White : VisualUi.Muted);
        modeTitle.MouseFilter = Control.MouseFilterEnum.Ignore; column.AddChild(modeTitle);
        var detail = VisualUi.Text(description, 12, VisualUi.Muted, true);
        detail.MouseFilter = Control.MouseFilterEnum.Ignore; column.AddChild(detail);
        if (enabled)
        {
            var call = VisualUi.Text("START SANDBOX  →", 12, VisualUi.Gold);
            call.MouseFilter = Control.MouseFilterEnum.Ignore; column.AddChild(call);
        }
        return button;
    }
    private void BuildDevelopmentMenu()
    {
        _development = new CenterContainer { Name = "DevelopmentPanel", Visible = false };
        _development.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var panel = new PanelContainer { CustomMinimumSize = new(480, 0) };
        panel.AddThemeStyleboxOverride("panel", CinematicArt.Frame(margin: 22)); _development.AddChild(panel);
        var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 12); panel.AddChild(body);
        body.AddChild(VisualUi.Text("DEVELOPMENT", 26));
        body.AddChild(VisualUi.Text("Test in a separate world. Player saves stay separate.", 13, VisualUi.Muted));
        _developer = AddButton(body, "ModeDeveloper", "Open Developer", "Open your separate Developer campaign.", SwitchToDeveloper, VisualIconLibrary.Construction);
        body.AddChild(VisualUi.Text("WORLD SEED", 11, VisualUi.Muted));
        _seed = new LineEdit { Name = "DeveloperSeed", Text = "20260908", MaxLength = 20, CustomMinimumSize = new(0, 36) };
        body.AddChild(_seed);
        AddButton(body, "NewDeveloperCampaign", "New Developer campaign", "Create a separate Developer world after confirmation.", RequestDeveloperCampaign, VisualIconLibrary.NavHome);
        _tools = AddButton(body, "DeveloperTools", "Developer tools", "Open tools for the active Developer campaign.", OpenTools, VisualIconLibrary.Construction);
        AddButton(body, "CloseDevelopment", "Back", "Return to the main menu.", CloseDevelopmentMenu, VisualIconLibrary.NavBack);
        _overlay.AddChild(_development);
    }

    private void CloseDevelopmentMenu()
    {
        _development.Hide(); _campaignModes.Show(); _resume.GrabFocus();
    }

    private static Button AddMenuButton(Container parent, string name, string text, string tooltip, Action action)
    {
        var button = VisualUi.Button(text, tooltip, action);
        button.Name = name; button.Alignment = HorizontalAlignment.Left;
        button.CustomMinimumSize = new(0, 43); button.AddThemeFontSizeOverride("font_size", 20);
        button.AddThemeStyleboxOverride("normal", new StyleBoxEmpty { ContentMarginLeft = 8 });
        var focus = VisualUi.Surface(true, 8);
        focus.BgColor = new Color(0,0,0,0); focus.BorderColor = new Color("8fac9c");
        focus.BorderWidthTop = focus.BorderWidthRight = focus.BorderWidthBottom = 0;
        focus.BorderWidthLeft = 2; focus.ShadowSize = 0;
        button.AddThemeStyleboxOverride("focus", focus);
        button.AddThemeColorOverride("font_color", new Color("cbd6d3"));
        button.AddThemeColorOverride("font_hover_color", new Color("f4ddab"));
        parent.AddChild(button); return button;
    }

    private static Button AddButton(Container parent, string name, string text, string tooltip, Action action, Texture2D icon)
    {
        var button = VisualUi.Button(text, tooltip, action, icon);
        button.Name = name; button.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        button.CustomMinimumSize = new(0, 40); parent.AddChild(button); return button;
    }

    private static Button CompactButton(Container parent, string name, string text, string tooltip, Action action, Texture2D icon)
    {
        var button = VisualUi.Button(text, tooltip, action, icon);
        button.Name = name; button.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        button.CustomMinimumSize = new(0, 32); parent.AddChild(button); return button;
    }

    private void BuildLoadingPresentation()
    {
        _loading = new Control { Name = "CampaignLoading", Visible = false };
        _loading.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        VisualUi.ContainPointerInput(_loading);
        _loadingArtwork = new TextureRect
        {
            Name = "SplashArtwork",
            Texture = LoadLoadingArtwork(StartupLoadingArtworkPath),
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
            MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        _loadingArtwork.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        _loading.AddChild(_loadingArtwork);
        var veil = new ColorRect { Color = new Color(0.003f, .008f, .018f, .12f), MouseFilter = Control.MouseFilterEnum.Ignore };
        veil.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        _loading.AddChild(veil);
        var margin = new MarginContainer();
        margin.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.BottomWide);
        margin.AnchorLeft = .31f; margin.AnchorRight = .69f;
        margin.OffsetLeft = 0; margin.OffsetRight = 0; margin.OffsetTop = -205; margin.OffsetBottom = -35;
        _loading.AddChild(margin);
        var column = new VBoxContainer(); column.AddThemeConstantOverride("separation", 8); margin.AddChild(column);
        _loadingTitle = VisualUi.Text("L O A D I N G   G A M E . . .", 18, Colors.White);
        _loadingTitle.Name = "LoadingTitle";
        _loadingTitle.HorizontalAlignment = HorizontalAlignment.Center;
        column.AddChild(_loadingTitle);
        var progressRow = new HBoxContainer(); progressRow.AddThemeConstantOverride("separation", 12); column.AddChild(progressRow);
        _loadingStatus = VisualUi.Text("Preparing campaign", 15, VisualUi.Accent);
        _loadingStatus.Name = "LoadingStatus";
        _loadingStatus.HorizontalAlignment = HorizontalAlignment.Center;
        _loadingProgress = new ProgressBar
        {
            Name = "LoadingProgress", MinValue = 0, MaxValue = 100, Value = 0,
            ShowPercentage = false, CustomMinimumSize = new Vector2(0, 24),
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill,
        };
        var trough = new StyleBoxFlat
        {
            BgColor = new Color("031526"), BorderColor = new Color("42c8ff"),
            BorderWidthLeft = 2, BorderWidthTop = 2, BorderWidthRight = 2, BorderWidthBottom = 2,
            CornerRadiusTopLeft = 8, CornerRadiusTopRight = 8, CornerRadiusBottomLeft = 8, CornerRadiusBottomRight = 8,
            ShadowColor = new Color("24bfff80"), ShadowSize = 5,
        };
        var fill = new StyleBoxFlat
        {
            BgColor = new Color("4bd5ff"),
            CornerRadiusTopLeft = 6, CornerRadiusTopRight = 6, CornerRadiusBottomLeft = 6, CornerRadiusBottomRight = 6,
        };
        _loadingProgress.AddThemeStyleboxOverride("background", trough);
        _loadingProgress.AddThemeStyleboxOverride("fill", fill);
        progressRow.AddChild(_loadingProgress);
        _loadingPercentage = VisualUi.Text("0%", 16, new Color("8ce6ff"));
        _loadingPercentage.Name = "LoadingPercent";
        _loadingPercentage.CustomMinimumSize = new Vector2(58, 0);
        _loadingPercentage.VerticalAlignment = VerticalAlignment.Center;
        progressRow.AddChild(_loadingPercentage);
        column.AddChild(_loadingStatus);
        _loadingTip = VisualUi.Text("", 12, new Color("c4d7df"), true);
        _loadingTip.Name = "LoadingTip";
        _loadingTip.HorizontalAlignment = HorizontalAlignment.Center;
        _loadingTip.CustomMinimumSize = new Vector2(0, 36);
        column.AddChild(_loadingTip);
        AddChild(_loading);
    }

    private static Texture2D LoadLoadingArtwork(string path) => GD.Load<Texture2D>(path) ??
        throw new InvalidOperationException($"Required loading splash is missing: {path}");

    private void ConfigureLoadingPresentation(string artworkPath, string title)
    {
        _loadingArtwork.Texture = LoadLoadingArtwork(artworkPath);
        _loadingTitle.Text = title;
        var candidate = _lastLoadingTip < 0
            ? Random.Shared.Next(LoadingTips.Length)
            : Random.Shared.Next(LoadingTips.Length - 1);
        if (_lastLoadingTip >= 0 && candidate >= _lastLoadingTip) candidate++;
        _lastLoadingTip = candidate;
        _loadingTip.Text = LoadingTips[candidate];
    }

    private void BuildAudioSettings()
    {
        _audioSettings = new CenterContainer { Name = "AudioSettingsPanel", Visible = false };
        _audioSettings.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var panel = new PanelContainer { CustomMinimumSize = new Vector2(520, 0) };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 22));
        _audioSettings.AddChild(panel);
        var content = new VBoxContainer(); content.AddThemeConstantOverride("separation", 14); panel.AddChild(content);
        content.AddChild(VisualUi.Text("AUDIO", 28, Colors.White));
        content.AddChild(VisualUi.Text("Balance the score and interface feedback for your play space.", 13, VisualUi.Muted, true));
        var settings = AudioDirector.Instance?.Settings ?? new AudioSettings();
        _masterVolume = AddVolumeRow(content, "MASTER", settings.Master);
        _musicVolume = AddVolumeRow(content, "MUSIC", settings.Music);
        _sfxVolume = AddVolumeRow(content, "SOUND EFFECTS", settings.Sfx);
        foreach (var slider in new[] { _masterVolume, _musicVolume, _sfxVolume })
            slider.ValueChanged += _ => ApplyAudioSettings();
        var actions = VisualUi.Actions(content);
        var done = VisualUi.Button("Done", "Save audio settings and return.", CloseAudioSettings, VisualIconLibrary.NavBack);
        done.Name = "AudioSettingsDone"; actions.AddChild(done);
        var defaults = VisualUi.Button("Restore defaults", "Restore the recommended audio mix.", () =>
        {
            _masterVolume.Value = 78; _musicVolume.Value = 64; _sfxVolume.Value = 82;
            ApplyAudioSettings();
        }, VisualIconLibrary.NavHome);
        defaults.Name = "AudioSettingsDefaults"; actions.AddChild(defaults);
        _overlay.AddChild(_audioSettings);
    }

    private static HSlider AddVolumeRow(Container parent, string label, float value)
    {
        var row = new HBoxContainer(); row.AddThemeConstantOverride("separation", 14); parent.AddChild(row);
        var name = VisualUi.Text(label, 12, VisualUi.Gold); name.CustomMinimumSize = new Vector2(130, 0); row.AddChild(name);
        var slider = new HSlider { MinValue = 0, MaxValue = 100, Step = 1, Value = value * 100,
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, CustomMinimumSize = new Vector2(300, 34) };
        row.AddChild(slider);
        return slider;
    }

    private void ShowAudioSettings()
    {
        _settings.Hide(); _campaignModes.Hide(); _newGameSelection.Hide(); _sandboxSetup.Hide(); _audioSettings.Show();
        _masterVolume.GrabFocus();
    }

    private void BuildSettingsMenu()
    {
        _settings = new CenterContainer { Name = "SettingsPanel", Visible = false };
        _settings.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var panel = new PanelContainer { CustomMinimumSize = new Vector2(480, 0) };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 22)); _settings.AddChild(panel);
        var content = new VBoxContainer(); content.AddThemeConstantOverride("separation", 10); panel.AddChild(content);
        content.AddChild(VisualUi.Text("SETTINGS", 28));
        content.AddChild(VisualUi.Text("Choose a category. Changes remain available while your campaign is paused.", 13, VisualUi.Muted, true));
        _settingsAudio = VisualUi.Button("Audio", "Adjust master, music and effects levels.", ShowAudioSettings, VisualIconLibrary.Info);
        _settingsAudio.Name = "SettingsAudio"; content.AddChild(_settingsAudio);
        var video = VisualUi.Button("Video", "Configure fullscreen display and rendering quality.", ShowVideoSettings, VisualIconLibrary.Info);
        video.Name = "SettingsVideo"; content.AddChild(video);
        var voice = VisualUi.Button("Voice & subtitles", "Configure offline dialogue and accessibility.", () => _main.UiVoice?.ShowVoiceSettings(), VisualIconLibrary.Info);
        voice.Name = "SettingsVoice"; content.AddChild(voice);
        content.AddChild(VisualUi.Text("CONTROLS", 12, VisualUi.Gold));
        content.AddChild(VisualUi.Text("Space pauses or resumes. 1–4 select standard simulation rates. Right-click the playback control pauses immediately.", 12, VisualUi.Muted, true));
        var actions = VisualUi.Actions(content);
        var back = VisualUi.Button("Back", "Return to the main menu.", () => { _settings.Hide(); _campaignModes.Show(); _resume.GrabFocus(); }, VisualIconLibrary.NavBack);
        back.Name = "SettingsBack"; actions.AddChild(back);
        _overlay.AddChild(_settings);
    }

    private void ShowSettings()
    {
        _campaignModes.Hide();
        _settings.Show();
        _settingsAudio.GrabFocus();
    }

    private void CloseAudioSettings()
    {
        ApplyAudioSettings(); _audioSettings.Hide(); _settings.Show();
    }

    private void BuildVideoSettings()
    {
        _videoSettings = new CenterContainer { Name = "VideoSettingsPanel", Visible = false };
        _videoSettings.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var panel = new PanelContainer { CustomMinimumSize = new Vector2(600, 0) };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 22)); _videoSettings.AddChild(panel);
        var content = new VBoxContainer(); content.AddThemeConstantOverride("separation", 10); panel.AddChild(content);
        content.AddChild(VisualUi.Text("VIDEO", 28));
        content.AddChild(VisualUi.Text("Detected display modes and renderer controls for this computer.", 13, VisualUi.Muted, true));
        content.AddChild(VisualUi.Text($"{_videoService.AdapterName}  ·  {_videoService.RendererName}", 11, VisualUi.Accent, true));
        _videoResolution = AddVideoOption(content, "RESOLUTION", _videoService.Modes.Select(mode => mode.ToString()));
        _videoMode = AddVideoOption(content, "DISPLAY", new[] { "Fullscreen", "Exclusive fullscreen" });
        _videoMode.ItemSelected += _ => UpdateVideoResolutionAvailability();
        content.AddChild(VisualUi.Text("Stellar Continuum always fills your display. 3D resolution adjusts rendering quality.", 12, VisualUi.Muted, true));
        _videoVsync = AddVideoOption(content, "V-SYNC", new[] { "Off", "On", "Adaptive" });
        _videoFrameCap = AddVideoOption(content, "FRAME CAP", new[]
        {
            $"Automatic · current {_videoService.ActiveMonitorRefreshHz} Hz",
            "60 FPS", "120 FPS", "144 FPS", "Unlimited",
        });
        content.AddChild(VisualUi.Text("Automatic temporarily selects the highest progressive refresh supported at your desktop resolution while the game is focused.", 11, VisualUi.Muted, true));
        _videoMsaa = AddVideoOption(content, "MSAA", new[] { "Off", "2×", "4×", "8×" });
        _videoRenderScale = AddVideoOption(content, "3D RESOLUTION", new[] { "75% · Performance", "100% · Native", "125% · Quality" });
        var nvidiaPanel = _videoService.FindNvidiaControlPanel();
        if (nvidiaPanel is not null)
        {
            var nvidia = VisualUi.Button("Open NVIDIA Control Panel", "Open NVIDIA's installed control panel. Stellar Continuum does not change global driver settings.", () =>
            {
                try { VideoSettingsService.OpenNvidiaControlPanel(nvidiaPanel); }
                catch (Exception error)
                {
                    _videoError.Text = $"NVIDIA Control Panel could not open: {error.Message}";
                    _videoError.Show();
                }
            });
            nvidia.Name = "OpenNvidiaControlPanel"; content.AddChild(nvidia);
        }
        _videoError = VisualUi.Text("", 12, new Color("efac92"), true); _videoError.Visible = false; content.AddChild(_videoError);
        var actions = VisualUi.Actions(content);
        var apply = VisualUi.Button("Apply", "Preview these settings for 15 seconds.", ApplyVideoSettings, VisualIconLibrary.Save);
        apply.Name = "VideoSettingsDone"; actions.AddChild(apply);
        var cancel = VisualUi.Button("Cancel", "Return without saving changes.", CancelVideoSettings, VisualIconLibrary.NavBack);
        cancel.Name = "VideoSettingsCancel"; actions.AddChild(cancel);
        _overlay.AddChild(_videoSettings);

        _videoRollback = new Control { Name = "VideoSettingsConfirmation", Visible = false };
        _videoRollback.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        VisualUi.ContainPointerInput(_videoRollback);
        var shade = new ColorRect { Color = new Color(0, 0, 0, .72f), MouseFilter = Control.MouseFilterEnum.Stop };
        shade.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); _videoRollback.AddChild(shade);
        var center = new CenterContainer(); center.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); _videoRollback.AddChild(center);
        var confirmPanel = new PanelContainer { CustomMinimumSize = new Vector2(510, 0) };
        confirmPanel.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 22)); center.AddChild(confirmPanel);
        var confirmBody = new VBoxContainer(); confirmBody.AddThemeConstantOverride("separation", 14); confirmPanel.AddChild(confirmBody);
        confirmBody.AddChild(VisualUi.Text("CONFIRM DISPLAY", 22, Colors.White));
        _videoRollbackText = VisualUi.Text("", 13, VisualUi.Muted, true); confirmBody.AddChild(_videoRollbackText);
        var confirmActions = VisualUi.Actions(confirmBody);
        _videoKeep = VisualUi.Button("Keep", "Keep and save these display settings.", KeepVideoSettings, VisualIconLibrary.Save);
        _videoKeep.Name = "KeepVideoSettings"; confirmActions.AddChild(_videoKeep);
        var revert = VisualUi.Button("Revert", "Restore the previous display settings.", RevertVideoSettings, VisualIconLibrary.NavBack);
        revert.Name = "RevertVideoSettings"; confirmActions.AddChild(revert);
        _overlay.AddChild(_videoRollback);
    }
    private static OptionButton AddVideoOption(Container parent, string label, IEnumerable<string> values)
    { var row=new HBoxContainer(); parent.AddChild(row); var name=VisualUi.Text(label,12,VisualUi.Gold); name.CustomMinimumSize=new Vector2(130,0); row.AddChild(name); var option=new OptionButton { SizeFlagsHorizontal=Control.SizeFlags.ExpandFill }; foreach(var value in values) option.AddItem(value); row.AddChild(option); return option; }
    private void ShowVideoSettings()
    {
        SyncVideoControls(VideoSettingsService.Current);
        if (string.IsNullOrEmpty(_videoService.RefreshRateError))
            _videoError.Hide();
        else
        {
            _videoError.Text = _videoService.RefreshRateError;
            _videoError.Show();
        }
        _settings.Hide(); _videoSettings.Show(); _videoResolution.GrabFocus();
    }

    private void UpdateVideoResolutionAvailability()
    {
        _videoResolution.Disabled = true;
        _videoResolution.TooltipText = "Fullscreen uses your desktop resolution. Use 3D resolution to adjust rendering quality.";
    }

    private void ApplyVideoSettings()
    {
        var resolution = _videoService.Modes[Math.Clamp(_videoResolution.Selected, 0, _videoService.Modes.Count - 1)];
        var displayMode = _videoMode.Selected switch
        {
            1 => VideoSettingsService.DisplayMode.Fullscreen,
            _ => VideoSettingsService.DisplayMode.Borderless,
        };
        var vsync = _videoVsync.Selected switch
        {
            1 => DisplayServer.VSyncMode.Enabled,
            2 => DisplayServer.VSyncMode.Adaptive,
            _ => DisplayServer.VSyncMode.Disabled,
        };
        var msaa = _videoMsaa.Selected switch
        {
            1 => Viewport.Msaa.Msaa2X,
            2 => Viewport.Msaa.Msaa4X,
            3 => Viewport.Msaa.Msaa8X,
            _ => Viewport.Msaa.Disabled,
        };
        var renderScale = _videoRenderScale.Selected switch { 0 => .75f, 2 => 1.25f, _ => 1f };
        var frameCap = _videoFrameCap.Selected switch
        {
            1 => VideoSettingsService.FrameCap.Fps60,
            2 => VideoSettingsService.FrameCap.Fps120,
            3 => VideoSettingsService.FrameCap.Fps144,
            4 => VideoSettingsService.FrameCap.Unlimited,
            _ => VideoSettingsService.FrameCap.Automatic,
        };
        var previous = _videoService.ApplyPreview(new(resolution, displayMode, vsync, msaa, renderScale, frameCap));
        if (!string.IsNullOrEmpty(_videoService.RefreshRateError))
        {
            _videoError.Text = _videoService.RefreshRateError;
            _videoError.Show();
        }
        if (!_videoHasUncommittedChange) _videoPrevious = previous;
        _videoHasUncommittedChange = true;
        _videoRollbackSeconds = 15;
        _videoRollback.Show();
        _videoKeep.GrabFocus();
    }

    private void KeepVideoSettings()
    {
        var error = _videoService.SaveCurrent();
        _videoRollback.Hide();
        if (!string.IsNullOrEmpty(error)) { _videoError.Text = error; _videoError.Show(); return; }
        _videoHasUncommittedChange = false;
        _videoSettings.Hide(); _settings.Show();
    }

    private void RevertVideoSettings()
    {
        _videoService.Revert(_videoPrevious);
        _videoHasUncommittedChange = false;
        SyncVideoControls(_videoPrevious);
        _videoRollback.Hide(); _videoResolution.GrabFocus();
    }

    private void CancelVideoSettings()
    {
        if (_videoHasUncommittedChange) RevertVideoSettings();
        _videoSettings.Hide(); _settings.Show();
    }

    private void SyncVideoControls(VideoSettingsService.Settings settings)
    {
        _videoFrameCap.SetItemText(0, $"Automatic · current {_videoService.ActiveMonitorRefreshHz} Hz");
        var resolutionIndex = 0;
        for (var index = 0; index < _videoService.Modes.Count; index++)
            if (_videoService.Modes[index] == settings.Resolution) { resolutionIndex = index; break; }
        _videoResolution.Select(resolutionIndex);
        _videoMode.Select(settings.DisplayMode == VideoSettingsService.DisplayMode.Fullscreen ? 1 : 0);
        _videoVsync.Select(settings.VSync switch { DisplayServer.VSyncMode.Enabled => 1, DisplayServer.VSyncMode.Adaptive => 2, _ => 0 });
        _videoFrameCap.Select(settings.FrameCap switch
        {
            VideoSettingsService.FrameCap.Fps60 => 1,
            VideoSettingsService.FrameCap.Fps120 => 2,
            VideoSettingsService.FrameCap.Fps144 => 3,
            VideoSettingsService.FrameCap.Unlimited => 4,
            _ => 0,
        });
        _videoMsaa.Select(settings.Msaa switch { Viewport.Msaa.Msaa2X => 1, Viewport.Msaa.Msaa4X => 2, Viewport.Msaa.Msaa8X => 3, _ => 0 });
        _videoRenderScale.Select(settings.RenderScale switch { .75f => 0, 1.25f => 2, _ => 1 });
        UpdateVideoResolutionAvailability();
    }

    private void ApplyAudioSettings() => AudioDirector.Instance?.SetVolumes(
        (float)_masterVolume.Value / 100f, (float)_musicVolume.Value / 100f, (float)_sfxVolume.Value / 100f);

    private async Task RunLoadingAsync(string status, Func<bool> action)
    {
        // Button callbacks are async void at the Godot signal boundary. Own one transition
        // explicitly so queued keyboard/pointer activation cannot start a second timed splash
        // after the first operation has already returned to gameplay.
        if (_loadingTransitionActive || !_overlay.IsVisibleInTree()) return;
        ConfigureLoadingPresentation(SaveLoadingArtworkPath, "LOADING SAVED GAME...");
        _loadingTransitionActive = true;
        _loadingStatus.Text = "Preparing game assets";
        _loadingProgress.Value = 0;
        _overlay.Hide();
        _loading.Show();
        LoadingPresentationShownCount++;
        var timeline = new CampaignLoadingTimeline();
        var elapsed = Stopwatch.StartNew();
        var previousSeconds = 0.0;
        try
        {
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            if (_loadingLifetimeEnded) return;
            BeginCampaignAssetLoading();
            var assetFraction = CampaignAssetLoadFraction();
            while (assetFraction < 1)
            {
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, assetFraction,
                    operationStarted: false, operationReady: false);
                _loadingStatus.Text = $"Loading game assets · {assetFraction:P0}";
                await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded) return;
                assetFraction = CampaignAssetLoadFraction();
            }

            _loadingStatus.Text = status;
            AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, 1,
                operationStarted: true, operationReady: false);
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            if (_loadingLifetimeEnded) return;
            if (!action())
            {
                timeline.Advance(0, elapsed.Elapsed.TotalSeconds, 1, true, false, failed: true);
                _loadingProgress.Value = timeline.DisplayedPercent;
                _loadingPercentage.Text = $"{timeline.DisplayedPercent:0}%";
                RestoreMenuAfterLoadingFailure();
                return;
            }
            // Loading a campaign can take long enough for the next rendered frame to carry a
            // large delta. Pause the newly loaded clock while the ready state is presented so
            // that opening a save never advances its world before the player regains control.
            var readySpeed = _main.UiCurrentSpeed;
            _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Paused);
            _loadingStatus.Text = "Finalizing campaign";
            while (!timeline.CanDismiss)
            {
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, 1,
                    operationStarted: true, operationReady: true);
                if (!timeline.CanDismiss)
                    await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded) return;
            }
            _loadingStatus.Text = "Campaign ready";
            _loadingProgress.Value = 100;
            _loadingPercentage.Text = "100%";
            LastLoadingDurationSeconds = elapsed.Elapsed.TotalSeconds;
            await ToSignal(GetTree().CreateTimer(0.18), SceneTreeTimer.SignalName.Timeout);
            if (_loadingLifetimeEnded) return;
            _loading.Hide();
            AudioDirector.Instance?.SetMenuContext(false);
            _main.UiResumeAtSpeed(readySpeed);
        }
        catch (Exception exception)
        {
            var message = $"Campaign transition failed: {exception.GetType().Name}: {exception.Message}";
            try { SupportLogger.Log("campaign-transition-error", exception.ToString()); }
            catch (Exception loggingFailure) { GD.PushError("Campaign transition logging failed: " + loggingFailure); }
            ShowSaveFailure(message);
            RestoreMenuAfterLoadingFailure();
        }
        finally
        {
            if (!_loadingLifetimeEnded)
            {
                _loading.Hide();
                _loadingTransitionActive = false;
            }
        }
    }

    private async Task RunGenerationLoadingAsync(
        Func<Action<GalaxyGenerationProgress>, Task<CampaignBootstrapResult>> prepare,
        Func<CampaignBootstrapResult, bool> commit)
    {
        await RunPreparedCampaignAsync<GalaxyGenerationProgress>(
            GenerationLoadingArtworkPath,
            "GENERATING NEW GALAXY...",
            "Planning galaxy generation",
            prepare,
            update => (update.Fraction, update.Status),
            commit,
            "Galaxy ready");
    }

    private async Task RunSavedCampaignLoadingAsync()
    {
        var developer = _main.UiIsDeveloperMode;
        await RunPreparedCampaignAsync<CampaignRestorationProgress>(
            SaveLoadingArtworkPath,
            "LOADING SAVED GAME...",
            "Locating saved campaign",
            progress => _main.UiPrepareCurrentCampaignAsync(progress),
            update => (update.Fraction, update.Status),
            bootstrap => _main.UiCommitPreparedCurrentCampaign(bootstrap, developer),
            "Saved campaign ready",
            exception => _main.UiHandlePreparedCurrentCampaignFailure(exception, developer));
    }

    private sealed class ProgressMailbox<T> where T : class
    {
        private readonly object _sync = new();
        private T? _latest;
        public void Publish(T value) { lock (_sync) _latest = value; }
        public T? Read() { lock (_sync) return _latest; }
    }

    private async Task RunPreparedCampaignAsync<TProgress>(
        string artworkPath,
        string title,
        string initialStatus,
        Func<Action<TProgress>, Task<CampaignBootstrapResult>> prepare,
        Func<TProgress, (double Fraction, string Status)> describe,
        Func<CampaignBootstrapResult, bool> commit,
        string readyStatus,
        Action<Exception>? handlePreparationFailure = null) where TProgress : class
    {
        if (_loadingTransitionActive || !_overlay.IsVisibleInTree()) return;
        _loadingTransitionActive = true;
        ConfigureLoadingPresentation(artworkPath, title);
        _loadingStatus.Text = "Preparing game assets";
        _loadingProgress.Value = 0;
        _loadingPercentage.Text = "0%";
        _overlay.Hide(); _loading.Show(); LoadingPresentationShownCount++;
        var timeline = new CampaignLoadingTimeline();
        var elapsed = Stopwatch.StartNew();
        var previousSeconds = 0.0;
        try
        {
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            if (_loadingLifetimeEnded) return;
            BeginCampaignAssetLoading();
            var assetFraction = CampaignAssetLoadFraction();
            while (assetFraction < 1)
            {
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, assetFraction, false, false);
                _loadingStatus.Text = $"Loading game assets · {assetFraction:P0}";
                await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded) return;
                assetFraction = CampaignAssetLoadFraction();
            }

            var mailbox = new ProgressMailbox<TProgress>();
            _loadingStatus.Text = initialStatus;
            var preparation = prepare(mailbox.Publish);
            while (!preparation.IsCompleted)
            {
                var update = mailbox.Read();
                var operation = update is null ? (0.0, initialStatus) : describe(update);
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, 1, true, false, operation.Item1);
                _loadingStatus.Text = operation.Item2;
                await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded)
                {
                    ObserveBackgroundFailure(preparation);
                    return;
                }
            }

            if (_loadingLifetimeEnded)
            {
                ObserveBackgroundFailure(preparation);
                return;
            }
            var bootstrap = await preparation;
            if (!commit(bootstrap))
            {
                timeline.Advance(0, elapsed.Elapsed.TotalSeconds, 1, true, false, failed: true, operationFraction: .98);
                RestoreMenuAfterLoadingFailure();
                return;
            }
            _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Paused);
            _loadingStatus.Text = "Finalizing campaign";
            while (!timeline.CanDismiss)
            {
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, 1, true, true, 1);
                if (!timeline.CanDismiss) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded) return;
            }
            _loadingStatus.Text = readyStatus;
            _loadingProgress.Value = 100; _loadingPercentage.Text = "100%";
            LastLoadingDurationSeconds = elapsed.Elapsed.TotalSeconds;
            await ToSignal(GetTree().CreateTimer(.18), SceneTreeTimer.SignalName.Timeout);
            if (_loadingLifetimeEnded) return;
            _loading.Hide(); AudioDirector.Instance?.SetMenuContext(false);
            // Explicit loads remain paused. A newly generated campaign resumes at its
            // ordinary rate once its atomic main-thread commit is visible.
            if (typeof(TProgress) == typeof(GalaxyGenerationProgress))
                _main.UiResumeAtSpeed(_main.UiIsDeveloperMode ? SimulationClock.SpeedLevel.Demo : SimulationClock.SpeedLevel.Normal);
        }
        catch (Exception exception)
        {
            if (handlePreparationFailure is not null)
            {
                handlePreparationFailure(exception);
                RestoreMenuAfterLoadingFailure();
                return;
            }
            var message = $"Campaign transition failed: {exception.GetType().Name}: {exception.Message}";
            try { SupportLogger.Log("campaign-transition-error", exception.ToString()); }
            catch (Exception loggingFailure) { GD.PushError("Campaign transition logging failed: " + loggingFailure); }
            ShowSaveFailure(message); RestoreMenuAfterLoadingFailure();
        }
        finally
        {
            if (!_loadingLifetimeEnded) { _loading.Hide(); _loadingTransitionActive = false; }
        }
    }

    private static void ObserveBackgroundFailure(Task operation)
    {
        _ = operation.ContinueWith(
            completed => _ = completed.Exception,
            System.Threading.CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private void BeginStartupLoading()
    {
        ConfigureLoadingPresentation(StartupLoadingArtworkPath, "L O A D I N G   G A M E . . .");
        _loadingTransitionActive = true;
        _overlay.Hide();
        _loadingStatus.Text = "Loading game assets";
        _loadingProgress.Value = 0;
        _loadingPercentage.Text = "0%";
        _loading.Show();
        StartupLoadingPresentationShownCount++;
        _ = RunStartupLoadingAsync();
    }

    private async Task RunStartupLoadingAsync()
    {
        var timeline = new CampaignLoadingTimeline();
        var elapsed = Stopwatch.StartNew();
        var previousSeconds = 0.0;
        try
        {
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            if (_loadingLifetimeEnded) return;
            if ((_main as IntegratedMain)?.UiStartupFailed == true) return;
            BeginCampaignAssetLoading();
            while (!timeline.CanDismiss)
            {
                if ((_main as IntegratedMain)?.UiStartupFailed == true) return;
                var assetFraction = CampaignAssetLoadFraction();
                var mainReady = (_main as IntegratedMain)?.UiRuntimeReady ?? _main.IsNodeReady();
                var ready = mainReady && assetFraction >= 1;
                AdvanceLoadingTimeline(timeline, elapsed, ref previousSeconds, assetFraction,
                    operationStarted: mainReady, operationReady: ready);
                _loadingStatus.Text = !mainReady ? "Preparing game systems" : assetFraction < 1
                    ? $"Loading game assets · {assetFraction:P0}"
                    : "Preparing main menu";
                if (!timeline.CanDismiss)
                    await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
                if (_loadingLifetimeEnded) return;
            }
            _loadingStatus.Text = "Game ready";
            _loadingProgress.Value = 100;
            _loadingPercentage.Text = "100%";
            LastLoadingDurationSeconds = elapsed.Elapsed.TotalSeconds;
            HasCompletedStartupLoading = true;
            await ToSignal(GetTree().CreateTimer(0.18), SceneTreeTimer.SignalName.Timeout);
        }
        catch (Exception exception)
        {
            var message = $"Startup loading failed: {exception.GetType().Name}: {exception.Message}";
            try { SupportLogger.Log("startup-loading-error", exception.ToString()); }
            catch (Exception loggingFailure) { GD.PushError("Startup loading error logging failed: " + loggingFailure); }
            ShowSaveFailure(message);
        }
        finally
        {
            if (!_loadingLifetimeEnded)
            {
                _loading.Hide();
                _loadingTransitionActive = false;
                if ((_main as IntegratedMain)?.UiStartupFailed != true)
                {
                    _campaignModes.Show();
                    _overlay.Show();
                    AudioDirector.Instance?.SetMenuContext(true);
                    if (HasCompletedStartupLoading) AudioDirector.Instance?.CompleteStartupLoading();
                    _resume.GrabFocus();
                }
            }
        }
    }

    private void BeginCampaignAssetLoading()
    {
        foreach (var path in CampaignLoadAssetPaths)
        {
            if (_campaignLoadAssets.ContainsKey(path)) continue;
            var error = ResourceLoader.LoadThreadedRequest(path);
            if (error != Error.Ok)
                throw new InvalidOperationException($"Could not begin loading required game asset '{path}' ({error}).");
        }
    }

    private double CampaignAssetLoadFraction()
    {
        var loaded = 0;
        foreach (var path in CampaignLoadAssetPaths)
        {
            if (_campaignLoadAssets.ContainsKey(path))
            {
                loaded++;
                continue;
            }
            switch (ResourceLoader.LoadThreadedGetStatus(path))
            {
                case ResourceLoader.ThreadLoadStatus.Loaded:
                    _campaignLoadAssets[path] = ResourceLoader.LoadThreadedGet(path) ??
                        throw new InvalidOperationException($"Required game asset '{path}' completed without a resource.");
                    loaded++;
                    break;
                case ResourceLoader.ThreadLoadStatus.Failed:
                case ResourceLoader.ThreadLoadStatus.InvalidResource:
                    throw new InvalidOperationException($"Required game asset '{path}' failed to load.");
            }
        }
        return (double)loaded / CampaignLoadAssetPaths.Length;
    }

    private void AdvanceLoadingTimeline(CampaignLoadingTimeline timeline, Stopwatch elapsed,
        ref double previousSeconds, double assetFraction, bool operationStarted, bool operationReady,
        double operationFraction = 0)
    {
        var currentSeconds = elapsed.Elapsed.TotalSeconds;
        timeline.Advance(Math.Max(0, currentSeconds - previousSeconds), currentSeconds,
            assetFraction, operationStarted, operationReady, operationFraction: operationFraction);
        previousSeconds = currentSeconds;
        _loadingProgress.Value = timeline.DisplayedPercent;
        _loadingPercentage.Text = $"{timeline.DisplayedPercent:0}%";
    }

    private void RestoreMenuAfterLoadingFailure()
    {
        _newGameSelection.Hide();
        _sandboxSetup.Hide();
        _audioSettings.Hide();
        _videoSettings.Hide();
        _settings.Hide();
        _development.Hide();
        _campaignModes.Show();
        _overlay.Show();
        _resume.GrabFocus();
    }
}

/// <summary>Compact spoiler-free view of the real nearby-star catalogue used by play.</summary>
public sealed partial class SandboxGalaxyPreview : Control
{
    public SandboxGalaxyPreview()
    {
        ClipContents = true;
        MouseFilter = MouseFilterEnum.Ignore;
    }

    public void SetSeed(long seed)
    {
        _ = seed; // The nearby catalogue is fixed; the seed only changes generated game content.
        QueueRedraw();
    }

    public override void _Draw()
    {
        var size = Size;
        DrawRect(new Rect2(Vector2.Zero, size), new Color(.006f, .012f, .026f));
        var catalog = NearbyStarCatalog.Stars;
        foreach (var star in catalog)
        {
            var point = Project(star, catalog, size);
            if (star.HygId == 0)
            {
                DrawCircle(point, 4.2f, VisualPalette.WithAlpha(VisualUi.Gold, .18f));
                DrawCircle(point, 1.4f, VisualUi.Gold);
                continue;
            }
            DrawCircle(point, .72f, VisualPalette.WithAlpha(PreviewSpectralColor(star.SpectralType), .74f));
        }
        DrawRect(new Rect2(Vector2.Zero, size), VisualPalette.WithAlpha(VisualPalette.Keyline, .62f), false, 1);
    }

    private static Color PreviewSpectralColor(string spectralType) => spectralType.Trim().ToUpperInvariant() switch
    {
        var value when value.StartsWith('O') || value.StartsWith('B') => new Color("89b7ff"),
        var value when value.StartsWith('A') || value.StartsWith('F') => new Color("d7e5ff"),
        var value when value.StartsWith('G') => new Color("ffe1a1"),
        var value when value.StartsWith('K') => new Color("ffc17c"),
        _ => new Color("ef8b7d"),
    };

    private static Vector2 Project(NearbyCatalogStar star, IReadOnlyList<NearbyCatalogStar> catalog, Vector2 size)
    {
        var minX = catalog.Min(value => value.XLightYears);
        var maxX = catalog.Max(value => value.XLightYears);
        var minY = catalog.Min(value => value.YLightYears);
        var maxY = catalog.Max(value => value.YLightYears);
        var scale = Math.Min(size.X * .88f / (float)(maxX - minX), size.Y * .76f / (float)(maxY - minY));
        var centerX = (minX + maxX) * .5;
        var centerY = (minY + maxY) * .5;
        return size * .5f + new Vector2((float)(star.XLightYears - centerX), (float)(star.YLightYears - centerY)) * scale;
    }
}
