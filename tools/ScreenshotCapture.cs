using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;
using Game.Campaign;
using Game.Presentation;
using Game.Presentation.Audio.Voice;
using Game.Simulation;
using Godot;

namespace Game.Tools;

/// <summary>CI-only real-input acceptance driver for the actual integrated game scene.</summary>
public partial class ScreenshotCapture : Node
{
    private const string PanelPath = "CampaignSidebar/DetailDrawer/Body/DetailScroll/Panels";
    private string _outputDirectory = string.Empty;
    private readonly List<string> _captures = new();
    private readonly List<object> _captureRecords = new();
    private readonly List<string> _checks = new();
    private int _mouseActions;
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    private Control _drawer = null!;
    private Control _dock = null!;
    private Vector2? _visiblePointerHold;
    private bool UsesVisibleViewportInput =>
        System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_VISIBLE") == "1";
    private string CaptureInputMode => UsesVisibleViewportInput
        ? "Viewport.PushInput (visible)" : "Input.ParseInputEvent";

    private void InjectPointerEvent(InputEvent @event)
    {
        if (UsesVisibleViewportInput)
            GetViewport().PushInput(@event, false);
        else
            Input.ParseInputEvent(@event);
    }

    private void FlushPointerEvents()
    {
        if (!UsesVisibleViewportInput) Input.FlushBufferedEvents();
    }

    private void HoldVisiblePointer(Vector2? nativePoint) => _visiblePointerHold = nativePoint;

    public override void _Process(double delta)
    {
        _ = delta;
        if (UsesVisibleViewportInput && _visiblePointerHold is { } point)
            GetViewport().PushInput(new InputEventMouseMotion { Position = point, GlobalPosition = point }, false);
    }

    public override async void _Ready()
    {
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "startup-failure-ui")
            ProcessMode = ProcessModeEnum.Always;
        try
        {
            await CaptureSuiteAsync();
            if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "exit-to-windows")
                throw new InvalidOperationException("Exit to Windows returned without completing the real save-and-quit flow.");
            GD.Print("STELLAR_SCREENSHOT_CAPTURE_COMPLETE");
            _main.UiVoice?.Stop();
            await AudioDirector.ShutdownAndQuitAsync(GetTree(), 0);
        }
        catch (Exception exception)
        {
            GD.PushError($"Screenshot capture failed: {exception}");
            try { await SaveViewportAsync("failure.png", 0, 0); }
            catch (Exception captureError) { GD.Print($"Failure image unavailable: {captureError.Message}"); }
            _main?.UiVoice?.Stop();
            await AudioDirector.ShutdownAndQuitAsync(GetTree(), 1);
        }
    }

    private async Task CaptureSuiteAsync()
    {
        var focus = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS");
        _outputDirectory = System.Environment.GetEnvironmentVariable("STELLAR_SCREENSHOT_DIR")
            ?? ProjectSettings.GlobalizePath("user://screenshots");
        Directory.CreateDirectory(_outputDirectory);
        var packedMain = GD.Load<PackedScene>("res://scenes/Main.tscn")
            ?? throw new InvalidOperationException("Could not load real Main.tscn.");
        var startupSavePath = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        var startupSaveLength = focus == "startup-failure-ui" && File.Exists(startupSavePath)
            ? new FileInfo(startupSavePath).Length : 0;
        var startupSaveHash = startupSaveLength > 0 ? HashFile(startupSavePath) : string.Empty;
        var instantiated = packedMain.Instantiate();
        AddChild(instantiated);
        _main = instantiated as Main
            ?? throw new InvalidOperationException("Main.tscn did not instantiate its real C# entry point.");
        // Capture jobs alone need windowed client pixels for repeatable resize evidence.
        if (focus is not "startup-fullscreen" and not "exit-to-windows")
        {
            var captureWindow = GetWindow();
            var visibleCapture = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_VISIBLE") == "1";
            captureWindow.Mode = Window.ModeEnum.Windowed;
            captureWindow.Borderless = false;
            await WaitFramesAsync(2);
            captureWindow.Position = visibleCapture ? new Vector2I(100, 100) : new Vector2I(5000, 5000);
            captureWindow.Size = new Vector2I(1280, 720);
            await WaitFramesAsync(2);
        }
        if (focus == "startup-failure-ui")
        {
            await VerifyStartupFailureUiAsync(startupSavePath, startupSaveHash, startupSaveLength);
            return;
        }
        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        _drawer = _main.GetNode<Control>("CampaignSidebar/DetailDrawer");
        _dock = _main.GetNode<Control>("PlayerControls/EmpireOverview");
        _ = _main.GetNode<ExplorationMissionPanel>("ExplorationMissionPanel");
        _ = _main.GetNode<RelationsPanel>("RelationsPanel");
        _ = _main.GetNode<LogisticsNetworkPanel>("LogisticsNetworkPanel");
        _ = _main.GetNode<SystemInspectionPanel>("SystemInspectionPanel");
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        var dialog = FindNode<ConfirmationDialog>(menu)
            ?? throw new InvalidOperationException("Campaign confirmation dialog did not instantiate.");
        await WaitForStartupLoadingAsync(menu, captureEvidence: focus is "loading-splash" or "loading-contexts");
        await WaitFramesAsync(30);
        if (focus == "loading-splash")
        {
            GD.Print("STELLAR_FOCUSED_LOADING_SPLASH_COMPLETE");
            return;
        }
        if (focus == "loading-contexts")
        {
            await VerifyLoadingContextsAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_LOADING_CONTEXTS_COMPLETE");
            return;
        }
        if (focus == "player-expedition-resume")
        {
            await VerifyPlayerExpeditionResumeAsync(menu);
            GD.Print("STELLAR_FOCUSED_PLAYER_EXPEDITION_RESUME_COMPLETE");
            return;
        }
        if (focus == "startup-fullscreen")
        {
            GD.Print($"STELLAR_FULLSCREEN_STARTUP mode={GetWindow().Mode} borderless={GetWindow().Borderless}");
            Require(GetWindow().Mode is Window.ModeEnum.Fullscreen or Window.ModeEnum.ExclusiveFullscreen,
                "production-startup-is-fullscreen-without-a-title-bar");
            Check(true, "startup-fullscreen-no-title-bar");
            return;
        }
        if (focus == "settings-navigation")
        {
            await VerifySettingsNavigationAsync(menu);
            GD.Print("STELLAR_FOCUSED_SETTINGS_NAVIGATION_COMPLETE");
            return;
        }
        if (focus == "exit-to-windows")
        {
            await ClickNamedButtonAsync(menu, "ExitToWindows");
            await ToSignal(GetTree().CreateTimer(8), SceneTreeTimer.SignalName.Timeout);
            throw new InvalidOperationException("Exit to Windows did not close after its save request.");
        }
        if (focus == "performance")
        {
            await VerifyCampaignPerformanceAsync(menu);
            GD.Print("STELLAR_FOCUSED_PERFORMANCE_REVIEW_COMPLETE");
            return;
        }
        if (focus == "massive-combat-menu")
        {
            await VerifyMassiveCombatMenuLifecycleAsync(menu);
            GD.Print("STELLAR_FOCUSED_MASSIVE_COMBAT_MENU_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "voice")
        {
            await VerifyVoiceRuntimeAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_VOICE_REVIEW_COMPLETE");
            return; // Focused runtime evidence is intentionally separate from the release manifest.
        }
        if (focus == "voice-tutorial")
        {
            await VerifyVoiceTutorialAsync(menu);
            GD.Print("STELLAR_FOCUSED_VOICE_TUTORIAL_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "playback")
        {
            await VerifyCompactPlaybackAsync(menu);
            GD.Print("STELLAR_FOCUSED_PLAYBACK_REVIEW_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "responsive")
        {
            await ClickNamedButtonAsync(menu, "ResumeCampaign");
            await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
            await VerifyResponsiveResolutionsAsync();
            GD.Print("STELLAR_FOCUSED_RESPONSIVE_REVIEW_COMPLETE");
            return; // Deliberately no full-suite manifest: this cannot satisfy the release gate.
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "video")
        {
            await VerifyVideoSettingsAsync(menu);
            GD.Print("STELLAR_FOCUSED_VIDEO_REVIEW_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "immersive")
        {
            await VerifyImmersiveVisualsAsync();
            GD.Print("STELLAR_FOCUSED_IMMERSIVE_REVIEW_COMPLETE");
            return;
        }
        if (focus == "regional-map")
        {
            await VerifyRegionalMapVisualsAsync(menu);
            GD.Print("STELLAR_FOCUSED_REGIONAL_MAP_COMPLETE");
            return;
        }
        if (focus == "nearby-catalog")
        {
            await VerifyStarLightEdgesAsync();
            await VerifyNearbyCatalogAsync(menu);
            GD.Print("STELLAR_FOCUSED_NEARBY_CATALOG_COMPLETE");
            return;
        }
        if (focus == "star-edges")
        {
            await VerifyStarLightEdgesAsync();
            return;
        }
        if (focus == "species-selector")
        {
            await VerifySpeciesSelectorAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_SPECIES_SELECTOR_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") is "map-stars" or "map-stars-final" or "system-scale")
        {
            await VerifyMapStarVisualsAsync(observeFullSolarCycle: focus == "map-stars");
            GD.Print("STELLAR_FOCUSED_MAP_STARS_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "map-evidence")
        {
            await VerifyMapEvidenceAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_MAP_EVIDENCE_COMPLETE");
            return;
        }
        if (focus == "planet-limbs")
        {
            await VerifyPlanetLimbDiagnosticsAsync();
            GD.Print("STELLAR_FOCUSED_PLANET_LIMBS_COMPLETE");
            return;
        }
        if (focus == "metric-inspector")
        {
            await VerifyMetricPlanetInspectorAsync(menu);
            GD.Print("STELLAR_FOCUSED_METRIC_INSPECTOR_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "camera")
        {
            await VerifyFocusedCameraJourneyAsync(menu);
            GD.Print("STELLAR_FOCUSED_CAMERA_REVIEW_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "construction")
        {
            await ClickNamedButtonAsync(menu, "ResumeCampaign");
            if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
            await OpenSectionAsync("industry");
            await ClickNamedButtonAsync(ActivePanel(), "Chooseresearch_network");
            await WaitForRefreshAsync();
            await VerifyConstructionRecoveryAsync(captureEvidence: true);
            GD.Print("STELLAR_FOCUSED_CONSTRUCTION_REVIEW_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "production")
        {
            await VerifyFreshConstructionRecoveryAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_PRODUCTION_REVIEW_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "project-card-stability")
        {
            await VerifyProjectCardStabilityAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_PROJECT_CARD_STABILITY_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "civilian-recovery")
        {
            await VerifyCivilianRecoveryControlsAsync(menu, dialog);
            // The civilian recovery flow exercises the original compact drawer path. Keep
            // the active-caption proof beside it so both 720p and 1080p captures verify
            // that settled subtitle wrapping leaves an operable drawer.
            await VerifyResponsiveResolutionsAsync();
            GD.Print("STELLAR_FOCUSED_CIVILIAN_RECOVERY_COMPLETE");
            return;
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "player-expedition")
        {
            await VerifyPlayerExpeditionAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_PLAYER_EXPEDITION_COMPLETE");
            return; // Long ordinary-player evidence is intentionally outside the release screenshot manifest.
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "player-expedition-checkpoint")
        {
            await VerifyPlayerExpeditionCheckpointAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_PLAYER_EXPEDITION_CHECKPOINT_COMPLETE");
            return; // Reviewed save setup is a focused recovery check, never fresh-opening evidence.
        }
        if (System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_FOCUS") == "player-expedition-controls")
        {
            await VerifyPlayerExpeditionControlsAsync(menu, dialog);
            GD.Print("STELLAR_FOCUSED_PLAYER_EXPEDITION_CONTROLS_COMPLETE");
            return;
        }
        if (focus == "fleet-orders")
        {
            await ClickNamedButtonAsync(menu, "OpenDevelopment");
            await ClickNamedButtonAsync(menu, "ModeDeveloper");
            await WaitForCampaignLoadingAsync();
            await VerifyShipMouseOrdersAsync();
            GD.Print("STELLAR_FOCUSED_FLEET_ORDERS_COMPLETE");
            return;
        }
        if (focus == "window-lifecycle")
        {
            await VerifyWindowLifecycleAsync(menu);
            GD.Print("STELLAR_FOCUSED_WINDOW_LIFECYCLE_COMPLETE");
            return;
        }
        Require(GetViewport().GetVisibleRect().Size == new Vector2(1280, 720),
            "The minimum-layout acceptance run must render at 1280x720.");
        Check(_main.GetNodeOrNull<Control>("PlayerControls/MapToolbar") is null,
            "bottom-command-toolbar-removed");
        Check(_main.UiIsMenuOpen && _main.UiIsPaused && !_main.UiIsDeveloperMode, "normal-startup-menu-paused");
        Require(menu.HasLoadingPresentation, "The campaign menu did not load the cinematic splash artwork.");
        Check(!_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed &&
            Descendants(menu).OfType<Button>().Single(button => button.Name == "DeveloperTools").Disabled,
            "player-mode-tools-unavailable");
        foreach (var button in Descendants(menu).OfType<Button>().Where(button => button.IsVisibleInTree()))
            AssertInsideViewport(button, "mode menu " + button.Name);
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        foreach (var control in Descendants(menu).OfType<Control>().Where(control => control.IsVisibleInTree() && (control is Button || control is LineEdit)))
            AssertInsideViewport(control, "development menu " + control.Name);
        await ClickNamedButtonAsync(menu, "CloseDevelopment");
        Check(true, "mode-menu-controls-fit-1280x720");
        CheckHomeIdentity("normal-human-earth-sol-start");
        await AssertMenuBlocksGameplayAsync(dialog, firstMenu: true);
        await SaveViewportAsync("01-main-menu.png");

        await OpenSettingsCategoryAsync(menu, "SettingsAudio");
        var audio = AudioDirector.Instance;
        var volumeSliders = Descendants(menu).OfType<HSlider>().Where(slider => slider.IsVisibleInTree()).ToArray();
        Check(menu.IsAudioSettingsVisible && audio is { HasRequiredAudio: true, IsMenuContext: true } &&
            volumeSliders.Length == 3 && volumeSliders.All(slider => slider.Value is >= 0 and <= 100),
            "audio-settings-and-original-score-present");
        await VerifyMusicRuntimeAsync(audio!);
        await SaveViewportAsync("01b-audio-settings.png");
        await ClickNamedButtonAsync(menu, "AudioSettingsDone");
        await ClickNamedButtonAsync(menu, "SettingsBack");

        await ClickNamedButtonAsync(menu, "NewPlayerCampaign");
        var story = Descendants(menu).OfType<Button>().Single(button => button.Name == "StoryCampaignOption");
        var sandbox = Descendants(menu).OfType<Button>().Single(button => button.Name == "SandboxCampaignOption");
        var gameTypeArtwork = Descendants(menu).OfType<TextureRect>()
            .Where(texture => texture.Name.ToString().EndsWith("CampaignOptionArtwork", StringComparison.Ordinal)).ToArray();
        Check(menu.IsNewGameSelectionVisible && story.Disabled && !sandbox.Disabled &&
            gameTypeArtwork.Length == 2 && gameTypeArtwork.All(texture => texture.Texture is { } image &&
                image.GetWidth() >= 1280 && image.GetHeight() >= 720) &&
            Descendants(menu).OfType<Label>().Any(label => label.Text == "COMING SOON"),
            "new-game-choice-presents-locked-story-and-sandbox");
        await SaveViewportAsync("01a-new-game-options.png");
        await ClickNamedButtonAsync(menu, "SandboxCampaignOption");
        Require(menu.IsSandboxSetupVisible, "Sandbox option did not open the setup page.");
        var setupControlNames = new HashSet<string>(StringComparer.Ordinal)
            { "SandboxSetupBack", "SandboxSeed", "SandboxSpecies", "StartConfiguredSandbox" };
        foreach (var control in Descendants(menu).OfType<Control>().Where(control => control.IsVisibleInTree() &&
                     setupControlNames.Contains(control.Name.ToString())))
            AssertInsideViewport(control, "sandbox setup " + control.Name);
        Check(true, "sandbox-setup-fits-and-precedes-confirmation");
        await ClickNamedButtonAsync(menu, "StartConfiguredSandbox");
        Require(dialog.Visible && dialog.DialogText.StartsWith("Generate a fresh 500-system", StringComparison.Ordinal),
            "Sandbox did not open the protected new-campaign confirmation.");
        await PressKeyAsync(Key.Escape);
        await WaitForRefreshAsync();
        Require(!dialog.Visible && menu.IsSandboxSetupVisible,
            "Canceling Sandbox confirmation did not return to its setup page.");
        await ClickNamedButtonAsync(menu, "SandboxSetupBack");
        Require(menu.IsNewGameSelectionVisible, "Sandbox setup Back did not restore the game-type choices.");
        await ClickNamedButtonAsync(menu, "NewGameBack");
        Require(!menu.IsNewGameSelectionVisible, "New-game Back did not restore the campaign menu.");

        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        Check(!_main.UiIsMenuOpen && !_main.UiIsPaused, "continue-resumes-normal-campaign");
        Check(!_sidebar.IsDrawerOpen && !_drawer.Visible && VisiblePanelCount() == 0, "navigation-default-closed");
        Check(VisualIconLibrary.Research.GetWidth() >= 96 && VisualIconLibrary.Construction.GetWidth() >= 96 &&
            VisualIconLibrary.NavShips.GetWidth() >= 96 && VisualIconLibrary.NavGalaxy.GetWidth() >= 96,
            "project-icons-crisp");
        foreach (var iconButton in Descendants(_dock).Concat(Descendants(_main.GetNode("PlayerControls/ResourceBar")))
                     .OfType<Button>().Where(button => string.IsNullOrEmpty(button.Text)))
            AssertIconAffordance(iconButton);
        // Freeze presentation probes so state comparisons cannot fail because a production tick ran.
        await PressKeyAsync(Key.Space);
        Require(_main.UiIsPaused, "Space did not pause the ordinary campaign.");
        await ClickButtonAsync(_dock, "Home");
        Require(_main.UiSelectedSystemId >= 0, "Home did not select a public catalog star.");
        await WaitForRefreshAsync();
        await SaveViewportAsync("02-region-map.png");
        await VerifyCameraJourneyAsync();

        foreach (var section in new[] { "economy", "research", "industry", "ships", "explore", "colonies",
                                       "inspection", "logistics", "relations", "menu" })
        {
            await OpenSectionAsync(section);
            CheckExclusive(section);
            if (section is "research" or "industry" or "ships")
            {
                var scrollBounds = ScreenRect(_main.GetNode<Control>("CampaignSidebar/DetailDrawer/Body/DetailScroll"));
                var actions = Descendants(ActivePanel()).OfType<Button>().ToArray();
                // Construction now also lists locked future infrastructure with its exact
                // prerequisites. All actionable opening choices must fit immediately;
                // locked reference cards are checked for scroll reachability below.
                if (section == "industry") actions = actions.Where(button => !button.Disabled).ToArray();
                if (section == "research")
                {
                    var primary = actions.FirstOrDefault(button => button.Name.ToString().StartsWith("ResearchNode_", StringComparison.Ordinal));
                    Require(primary is not null, "Fresh campaign did not expose an actionable research possibility.");
                    var primaryAction = primary!;
                    Require(primaryAction.IsVisibleInTree() && Encloses(GetViewport().GetVisibleRect(), ScreenRect(primaryAction)),
                        $"Highest-priority research action does not fit the workspace on first open: {primaryAction.Name}.");
                    actions = Array.Empty<Button>();
                }
                foreach (var action in actions)
                    Require(action.IsVisibleInTree() && Encloses(scrollBounds, ScreenRect(action)),
                        $"Primary {section} action requires scrolling on first open: {action.Name}.");
            }
            await AssertSectionControlsReachableAsync();
            if (section == "research")
            {
                // Reachability walks the complete page. Restore the real first-open position
                // so visual evidence shows the summary and highest-priority programs.
                _main.GetNode<ScrollContainer>("CampaignSidebar/DetailDrawer/Body/DetailScroll").ScrollVertical = 0;
                await WaitForRefreshAsync();
                await SaveViewportAsync("03-research-card.png");
            }
            if (section == "industry")
            {
                _main.GetNode<ScrollContainer>("CampaignSidebar/DetailDrawer/Body/DetailScroll").ScrollVertical = 0;
                await WaitForRefreshAsync();
                await SaveViewportAsync("04-industry-card.png");
            }
            if (section == "economy")
            {
                var flow = _main.UiCreditFlow;
                Require(Math.Abs(flow.NetCreditsPerDay - _main.UiDashboard.CreditsPerDay) < 0.0001,
                    "Economy page net does not match the authoritative dashboard throughput.");
                Require(Descendants(ActivePanel()).Any(node => node.Name == "EconomyCostBreakdown") &&
                    Descendants(ActivePanel()).OfType<Label>().Single(label => label.Name == "EconomyFlow_orbital")
                        .Text == _main.UiFormatMoneyRate(-flow.OrbitalMaintenancePerDay),
                    "Economy page omitted the explicit orbital-maintenance line.");
                foreach (var labelName in new[] { "EconomyReserves", "EconomyNetFlow", "EconomyGrossIncome", "EconomyOperatingCosts",
                             "EconomyFlow_colony", "EconomyFlow_trade", "EconomyFlow_administration", "EconomyFlow_population",
                             "EconomyFlow_habitat", "EconomyFlow_fleet", "EconomyFlow_orbital", "EconomyFlow_surface", "EconomyFlow_research" })
                    Require(Descendants(ActivePanel()).OfType<Label>().Single(label => label.Name == labelName).IsVisibleInTree(),
                        $"Economy page metric is not visible: {labelName}.");
                Check(true, "economy-page-reconciles-live-cash-flow");
                await SaveViewportAsync("20-economy.png");
            }
            if (section == "relations")
            {
                var relationsWorkspace = (DiplomacyWorkspaceView)ActivePanel();
                Check(relationsWorkspace.Model?.Contacts.Count == 0 &&
                    new[] { "ContactSearch", "ContactFilter", "SelectedContact", "RelationshipPanel", "DiplomacyTabs" }
                        .All(name => Descendants(relationsWorkspace).OfType<Control>().Any(control => control.Name == name && control.IsVisibleInTree())) &&
                    Descendants(relationsWorkspace).OfType<Label>().Any(label => label.Text == "THE UNDISCOVERED") &&
                    !Descendants(relationsWorkspace).OfType<TextureRect>().Single(texture => texture.Name == "CivilizationPortrait").Visible,
                    "relations-page-uses-visual-contact-state");
                await SaveViewportAsync("05-relations.png");
            }
            if (section == "explore")
            {
                var missionCards = Descendants(ActivePanel()).OfType<Control>()
                    .Single(node => node.Name == "MissionCards");
                Check(missionCards.IsVisibleInTree() &&
                    Descendants(missionCards).Any(node => node.Name == "NoActiveMissions") &&
                    Descendants(missionCards).OfType<Label>().Any(label => label.Text == "DEEP SPACE AWAITS"),
                    "exploration-page-uses-visual-mission-state");
            }
            if (section == "logistics")
            {
                var logisticsNodes = Descendants(ActivePanel()).Count(node =>
                    node.Name.ToString().StartsWith("LogisticsNode_", StringComparison.Ordinal));
                Check(Descendants(ActivePanel()).Any(node => node.Name == "LogisticsMetrics") &&
                    new[] { "LogisticsSupply", "LogisticsDemand", "LogisticsDelivered", "LogisticsShortfall" }
                        .All(name => Descendants(ActivePanel()).OfType<Label>().Any(label => label.Name == name && label.IsVisibleInTree())) &&
                    Descendants(ActivePanel()).OfType<Label>().Any(label => label.Name == "LogisticsGuidance" &&
                        label.IsVisibleInTree() && label.Text.Contains('·')) &&
                    logisticsNodes >= 3,
                    "logistics-page-uses-visual-network-state");
            }
            if (section == "inspection")
            {
                var facts = Descendants(ActivePanel()).OfType<GridContainer>()
                    .Single(node => node.Name == "InspectionFacts");
                Check(Descendants(ActivePanel()).Any(node => node.Name == "InspectionSurveyProgress") &&
                    Descendants(ActivePanel()).Any(node => node.Name == "InspectionColonyCard") &&
                    facts.GetChildCount() >= 5 &&
                    Descendants(ActivePanel()).OfType<Label>().Any(label => label.Text == "EARTH"),
                    "inspection-page-uses-visual-intelligence-state");
            }
            if (section == "colonies")
            {
                var startingWorlds = _main.UiOwnedColonies;
                Check(startingWorlds.Length == 3 &&
                    startingWorlds.Select(world => world.ColonyName).SequenceEqual(new[] { "Earth", "Luna", "Mars" }) &&
                    startingWorlds.All(world => world.SystemName == "Sol" && world.CanLand && world.PopulationMillions > 0) &&
                    startingWorlds.Where(world => world.PlanetName is "Moon" or "Mars").All(world =>
                        world.AdministrationCreditsPerDay > 0 && world.HabitatSupportCreditsPerDay > 0 &&
                        world.GrossHabitatSupportCreditsPerDay >= world.HabitatSupportCreditsPerDay &&
                        world.HabitatNeeds.Contains("required", StringComparison.Ordinal)),
                    "human-sol-starting-settlements-visible");
                var land = Descendants(ActivePanel()).OfType<Button>().First(button => button.Text == "Land");
                await ClickControlAsync(land);
                Check(_main.UiIsSurfaceOpen && !_sidebar.IsDrawerOpen,
                    "owned-colony-land-opens-surface");
                _main.UiReturnToOrbit();
                await WaitForRefreshAsync();
                Require(!_main.UiIsSurfaceOpen, "Direct colony surface probe did not return to orbit.");
            }
            if (section == "menu")
            {
                var portraits = new[]
                {
                    CivilizationArtworkLibrary.TerranBaseline,
                    CivilizationArtworkLibrary.PelagicHighPressure,
                    CivilizationArtworkLibrary.CompactHighGravity,
                    CivilizationArtworkLibrary.CryogenicHydrocarbon,
                    CivilizationArtworkLibrary.PlanetaryGovernor,
                    CivilizationArtworkLibrary.ChiefScientist,
                    CivilizationArtworkLibrary.FleetCommander,
                }.Select(VisualIconLibrary.Get).ToArray();
                Check(portraits.All(texture => texture.GetWidth() >= 1200 && texture.GetHeight() >= 1200) &&
                    Descendants(ActivePanel()).OfType<TextureRect>().Count(texture =>
                        texture.Name.ToString().StartsWith("LeaderPortrait_", StringComparison.Ordinal)) == 3 &&
                    Descendants(ActivePanel()).Any(node => node.Name == "PlayerSpeciesPortrait") &&
                    Descendants(ActivePanel()).OfType<Label>().Single(label => label.Name == "CampaignCivilizationName").Text == "HUMAN COMMONWEALTH" &&
                    Descendants(ActivePanel()).OfType<Label>().Single(label => label.Name == "CampaignSpeciesName").Text == "TERRAN BASELINE",
                    "civilization-portraits-load-in-real-runtime");
                _main.GetNode<ScrollContainer>("CampaignSidebar/DetailDrawer/Body/DetailScroll").ScrollVertical = 0;
                await WaitFramesAsync(3);
                await SaveViewportAsync("12-menu-drawer.png");
            }
            await CloseDrawerAsync();
        }
        Check(true, "drawer-close-returns-map");
        AssertInsideViewport(_main.GetNode<Control>("CampaignSidebar/NavigationRail"), "navigation rail");
        var navigationScroll = _main.GetNode<ScrollContainer>("CampaignSidebar/NavigationRail/NavigationScroll");
        foreach (var destination in Descendants(navigationScroll).OfType<Button>())
            Require(Encloses(ScreenRect(navigationScroll), ScreenRect(destination)),
                $"Navigation clipped: {destination.Text}, rect={ScreenRect(destination)}, minimum={destination.GetCombinedMinimumSize()}, viewport={ScreenRect(navigationScroll)}, scroll={navigationScroll.ScrollVertical}.");
        Require(navigationScroll.ScrollVertical == 0, $"Navigation requires scrolling at 1280x720: {navigationScroll.ScrollVertical}.");
        AssertInsideViewport(_main.GetNode<Control>("PlayerControls/ResourceBar"), "resource bar");
        AssertInsideViewport(_dock, "action dock");
        if (_main.UiIsSystemSpatialView)
        {
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            await WaitForRefreshAsync();
        }
        Require(!_main.UiIsSystemSpatialView,
            "First Light guide proof did not return from the intentionally clear orbital view.");
        var playerMilestones = _main.GetNode<Control>("DemoProgressPanel/DemoMilestones");
        Check(playerMilestones.IsVisibleInTree() && _main.UiDemoObjective is not null &&
            Descendants(playerMilestones).OfType<Button>().Count() == 4,
            "player-first-colony-guide-is-visible-and-actionable");
        await ClickButtonAsync(playerMilestones, "Guide");
        var expeditionSpeed = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "ExpeditionSpeed");
        Require(expeditionSpeed.IsVisibleInTree() && !expeditionSpeed.Disabled,
            "Player expedition guide did not expose its 8× fast-forward control.");
        Require(expeditionSpeed.Text.Contains("8×", StringComparison.Ordinal),
            "Player expedition fast-forward control did not disclose its ordinary 8× pace.");
        await ClickControlAsync(expeditionSpeed);
        Check(_main.UiCurrentSpeed == SimulationClock.SpeedLevel.Maximum &&
            _main.UiRequestedSpeedMultiplier == 8 && !_main.UiIsPaused,
            "guided-expedition-pacing-visible");
        await ClickNamedButtonAsync(_main.GetNode("PlayerControls"), "SimulationPlaybackButton");
        Require(_main.UiIsPaused, "Expedition pace probe did not return the campaign to its paused acceptance state.");
        await WaitForRefreshAsync();
        Require(expeditionSpeed.IsVisibleInTree() && expeditionSpeed.Text == "Continue at 8×",
            "Paused ordinary 8× expedition did not offer its visible Continue control.");
        await CloseDrawerAsync();
        Check(true, "controls-fit-1280x720");
        Check(true, "icon-only-controls-visible");
        await VerifyPointerShieldingAsync();

        await OpenSectionAsync("menu");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        await ClickNamedButtonAsync(menu, "NewDeveloperCampaign");
        Require(dialog.Visible && _main.UiIsMenuOpen && !_main.UiIsDeveloperMode, "Developer confirmation was skipped.");
        var normalBeforeCancel = _main.UiDashboard;
        var dialogBounds = new Rect2((Vector2)dialog.Position, (Vector2)dialog.Size);
        Check(dialog.DialogAutowrap && Encloses(GetViewport().GetVisibleRect(), dialogBounds),
            "developer-confirmation-wraps-inside-viewport");
        await SaveViewportAsync("06-demo-confirmation.png");
        await ClickControlAsync(dialog.GetCancelButton());
        Check(!dialog.Visible && _main.UiIsMenuOpen && !_main.UiIsDeveloperMode && _main.UiIsPaused &&
            Equals(normalBeforeCancel, _main.UiDashboard), "cancel-developer-preserves-player-campaign");
        await ClickNamedButtonAsync(menu, "NewDeveloperCampaign");
        var confirmationAccept = dialog.GetOkButton();
        var confirmationPoint = ScreenRect(confirmationAccept).GetCenter();
        var firstConfirmationClick = ClickPositionAsync(confirmationPoint, MouseButton.Left);
        var duplicateConfirmationClick = ClickPositionAsync(confirmationPoint, MouseButton.Left);
        await Task.WhenAll(firstConfirmationClick, duplicateConfirmationClick);
        await WaitForCampaignLoadingAsync();
        await WaitForRefreshAsync();
        Check(!dialog.Visible && !_main.UiIsMenuOpen && _main.UiIsDeveloperMode && menu.LoadingPresentationShownCount == 1 &&
            _main.UiCurrentSpeed == SimulationClock.SpeedLevel.Demo, "confirm-starts-developer-at-24x");
        Check(menu.LoadingPresentationShownCount == 1,
            "duplicate-campaign-confirmation-starts-one-loading-transition");
        Check(menu.HasLoadingPresentation && menu.LoadingPresentationShownCount == 1,
            "cinematic-splash-loading-present");
        CheckHomeIdentity("developer-human-earth-sol-start");
        Check(_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed && _main.UiDashboard.FleetCount == 0 &&
            !_main.UiDashboard.Research.IsActive && !_main.UiDashboard.Construction.IsActive,
            "developer-opening-tools-unused");
        var normalSave = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        Require(File.Exists(normalSave), "Player campaign was not checkpointed before the Developer switch.");
        var normalSaveHash = HashFile(normalSave);
        await CloseDrawerAsync();
        // DemoProgressPanel refreshes on its own bounded cadence after a campaign switch.
        // Wait for that real layout refresh before resolving and clicking the Guide control.
        await WaitForRefreshAsync();
        var milestones = _main.GetNode<Control>("DemoProgressPanel/DemoMilestones");
        await ClickButtonAsync(milestones, "Guide");
        Require(_sidebar.ActiveSection == "demo" && VisiblePanelCount() == 1, "Guide did not open alone.");
        Check(ActivePanel().IsVisibleInTree() && !string.IsNullOrWhiteSpace(_main.UiDemoObjective?.Objective),
            "developer-guidance-visible-with-objective");
        await SaveViewportAsync("07-demo-guidance.png");

        // Use normal player buttons to start the first projects; no technology or resource injection.
        await OpenSectionAsync("research");
        var visibleResearch = _main.UiResearchHorizon.Where(node => node.CanStart).ToArray();
        var workspace = (ResearchWorkspaceView)ActivePanel();
        var lockedResearch = Descendants(workspace).OfType<Button>()
            .Where(button => button.Name.ToString().StartsWith("ResearchLocked_", StringComparison.Ordinal)).ToArray();
        Check(visibleResearch.Length >= 2 && workspace.GraphControlCount >= visibleResearch.Length && lockedResearch.Length > 0 &&
            lockedResearch.All(button => button.Text == "????\nLOCKED" &&
                button.TooltipText == "Locked research. Advance known prerequisite branches to reveal it." &&
                button.Name.ToString().StartsWith("ResearchLocked_research-", StringComparison.Ordinal)),
            "research-horizon-hides-unknown-possibilities");
        Check(_main.UiResearchHorizonEdges.Count > 0 &&
            Descendants(workspace).Any(node => node.Name == "ResearchGraph") &&
            Descendants(workspace).Any(node => node.Name == "ResearchInspector"),
            "research-horizon-has-graphical-node-identities");
        var selectedResearch = visibleResearch[0];
        await ClickControlAsync(await SelectResearchProgramThroughSearchAsync(selectedResearch.Id));
        Check(_main.UiDashboard.Research.IsActive, "research-card-starts-project");
        await OpenSectionAsync("industry");
        await ClickControlAsync(Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "Chooseresearch_network"));
        Check(_main.UiDashboard.Construction.IsActive, "industry-card-starts-project");
        // Freeze the notification snapshot before its layout refresh. At 24x, ordinary
        // milestone events can legitimately arrive between clearing unread notifications
        // and inspecting the center, which would make this acceptance check nondeterministic.
        var resumeAfterNotificationInspection = !_main.UiIsPaused;
        if (resumeAfterNotificationInspection)
            await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await WaitForRefreshAsync();
        var notificationToggle = Descendants(_main.GetNode("PlayerControls")).OfType<Button>()
            .Single(button => button.Name == "NotificationToggle");
        Require(notificationToggle.Text == "2", $"Expected two unread project events, got {notificationToggle.Text}.");
        await ClickControlAsync(notificationToggle);
        var notificationCenter = _main.GetNode<Control>("PlayerControls/NotificationCenter");
        var notificationLabels = Descendants(notificationCenter).OfType<Label>().Select(label => label.Text).ToArray();
        Check(notificationCenter.IsVisibleInTree() && notificationToggle.Text == "0" &&
            notificationLabels.Contains("RESEARCH") && notificationLabels.Contains("CONSTRUCTION") &&
            notificationLabels.Any(text => text.Contains(selectedResearch.Title, StringComparison.Ordinal)) &&
            notificationLabels.Any(text => text.Contains("Research Network", StringComparison.Ordinal)),
            "notification-center-retains-player-orders");
        var actionEffects = _main.GetNode<ActionFeedbackEffects>("PlayerControls/ActionFeedbackEffects");
        Check(actionEffects.TriggerCount >= 2 && !string.IsNullOrWhiteSpace(actionEffects.LastCategory),
            "accepted-actions-trigger-visual-feedback");
        AssertInsideViewport(notificationCenter, "notification center");
        await ClickNamedButtonAsync(notificationCenter, "NotificationClose");
        Require(!notificationCenter.Visible, "Notification close control did not dismiss the center.");
        if (resumeAfterNotificationInspection)
            await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenSectionAsync("ships");
        var earlyShipButtons = Descendants(ActivePanel()).OfType<Button>().ToArray();
        Check(_main.UiIsDeveloperMode && _main.UiDashboard.FleetCount == 0 &&
            _main.UiDashboard.Shipyard.Title == "Shipyard locked" &&
            _main.UiDashboard.Shipyard.Detail.Contains("Spacecraft Construction", StringComparison.Ordinal) &&
            _main.UiDashboard.Shipyard.Detail.Contains("Experimental Interstellar Transit", StringComparison.Ordinal) &&
            _main.UiDashboard.Shipyard.Detail.Contains("Orbital Shipyard", StringComparison.Ordinal) &&
            earlyShipButtons.All(button => button.Text is not "Next Ship" and not "Build / Queue Ship"),
            "early-game-shipyard-locks-cleanly");
        await SaveViewportAsync("08-ships-card.png");
        await OpenSectionAsync("colonies");
        Require(_sidebar.ActiveSection == "colonies" && ActivePanel().Name == "Exploration",
            "Colonies must use the ordinary settlement panel.");
        await SaveViewportAsync("09-colonies.png");
        await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home");
        var homeId = _main.UiSelectedSystemId;
        Check(homeId >= 0 && !_main.UiIsSystemSpatialView, "home-selects-known-star");
        await SaveViewportAsync("11-region-map-demo.png");
        await ClickButtonAsync(_dock, "Open System");
        Check(_main.UiIsSystemSpatialView && _main.UiSelectedSystemId == homeId, "open-system-enters-home-orbits");
        await WaitForRefreshAsync();
        var feedback = _main.GetNode<Control>("PlayerControls/CommandFeedback");
        await WaitForCameraAsync();
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickPositionAsync(_main.UiGetBodyScreenPosition(3)!.Value, MouseButton.Right);
        await WaitForRefreshAsync();
        Check(feedback.IsVisibleInTree() && _main.UiStatusMessage.Contains("Select a ship", StringComparison.Ordinal),
            "command-feedback-visible-over-system-view");
        AssertInsideViewport(feedback, "command feedback");
        await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickButtonAsync(_dock, "Home");
        await WaitForCameraAsync();
        await WaitForRefreshAsync();
        Require(!_main.UiIsSystemSpatialView && playerMilestones.IsVisibleInTree(),
            "Developer guide proof did not return to the galaxy context where the objective strip is available.");
        await ClickButtonAsync(playerMilestones, "Guide");
        await WaitForRefreshAsync();
        await ClickNamedButtonAsync(ActivePanel(), "DeveloperResumeSpeed");
        await CloseDrawerAsync();
        await WaitForRefreshAsync();
        Require(_main.UiCurrentSpeed == SimulationClock.SpeedLevel.Demo, "Speed selector did not restore Developer speed.");
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        await WaitForRefreshAsync();
        Require(_main.UiIsSystemSpatialView && _main.UiSelectedSystemId == homeId,
            "Developer guide proof did not return to the home orbital view.");
        var worldNames = new[] { "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Moon" };
        var systemMapBounds = new Rect2(112, 146, 1152, 438);
        for (var index = 0; index < worldNames.Length; index++)
        {
            var bodyId = index + 1;
            var point = _main.UiGetBodyScreenPosition(bodyId);
            Require(_main.UiGetBodyLabel(bodyId) == worldNames[index] && point.HasValue &&
                systemMapBounds.HasPoint(point.Value), $"Canonical Sol world not visible in its map area: {worldNames[index]}.");
        }
        Check(true, "sol-catalog-worlds-visible");
        await SaveViewportAsync("10-system-planets.png");
        var earthPoint = _main.UiGetBodyScreenPosition(3)
            ?? throw new InvalidOperationException("Earth's rendered position is unavailable.");
        var pointerRevision = _main.UiPointerCommandRevision;
        await ClickPositionAsync(earthPoint, MouseButton.Left);
        Check(_main.UiSelectedBodyId == 3 && _main.UiGetBodyLabel(3) == "Earth" &&
            _main.UiIsSystemSpatialView && _main.UiPointerCommandRevision == pointerRevision,
            "earth-selected-by-mouse");
        await WaitForRefreshAsync();
        await SaveViewportAsync("13-earth-selected.png");
        await ClickButtonAsync(_dock, "Back to Region");
        Check(!_main.UiIsSystemSpatialView && _main.UiSelectedSystemId == homeId, "back-to-region-preserves-selection");
        await AssertStartupArtworkHiddenWhileAsync(menu, async () =>
        {
            await OpenSectionAsync("explore");
            await WaitForRefreshAsync();
            await CloseDrawerAsync();
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
        }, "gameplay refresh and navigation");
        Check(true, "startup-artwork-stays-hidden-during-gameplay-refresh-and-navigation");

        await OpenSectionAsync("menu");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        await AssertMenuBlocksGameplayAsync(dialog, firstMenu: false);
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        Check(_main.UiIsDeveloperMode && !_main.UiIsMenuOpen &&
            _main.UiCurrentSpeed == SimulationClock.SpeedLevel.Demo, "resume-restores-developer-speed");
        await AssertStartupArtworkHiddenWhileAsync(menu, async () =>
        {
            await ClickButtonAsync(ActivePanel(), "Save");
            await WaitForRefreshAsync();
        }, "manual save");
        Check(true, "startup-artwork-stays-hidden-during-manual-save");
        var demoSave = ProjectSettings.GlobalizePath("user://saves/developer-autosave.json");
        Check(File.Exists(demoSave) && normalSaveHash == HashFile(normalSave), "player-save-unchanged-by-developer");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        normalSaveHash = await ReloadDeveloperThroughPlayerAsync(normalSave, normalSaveHash);
        Check(_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed,
            "mode-roundtrip-preserves-independent-campaigns");
        Require(_main.UiIsDeveloperMode && !_main.UiIsMenuOpen && normalSaveHash == HashFile(normalSave),
            "Reloading Developer mode changed the Player save or failed to resume.");
        CheckHomeIdentity("developer-sol-identity-survives-reload");
        normalSaveHash = await VerifySurfaceJourneyAsync(normalSave, normalSaveHash);
        await VerifyDeveloperToolsAsync(normalSave, normalSaveHash);
        await VerifyShipMouseOrdersAsync();
        await VerifyResponsiveResolutionsAsync();
        await VerifyLocalSkySceneryAsync();
        // Cancellation deliberately scraps materials. Isolate this destructive journey
        // after the existing colony progression checks instead of starving their fixture.
        await VerifyFreshConstructionRecoveryAsync(menu, dialog);
        // The fullscreen research interaction contract was originally exercised only by
        // its focused stability fixture. Run the same real controls in this final isolated
        // Developer state, while retaining the release manifest's exact 35 captures.
        await VerifyResearchCardLayoutAsync(captureEvidence: false);
        await VerifyScheduledDeveloperAutosaveAsync(menu, demoSave);
        WriteManifest();
    }

    private async Task VerifyScheduledDeveloperAutosaveAsync(MainMenuLayer menu, string demoSave)
    {
        Require(_main.UiIsDeveloperMode, "Scheduled Developer autosave proof lost its isolated campaign.");
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var manualSaveHash = HashFile(demoSave);
        await CloseDrawerAsync();

        await SetPlaybackSpeedAsync(SimulationClock.SpeedLevel.Demo);
        var autosaveStartDay = _main.UiSimulationDays;
        var developerAutosaveSchedule = PlayableDemoScenario.CreateAutosaveScheduler();
        developerAutosaveSchedule.Reset(autosaveStartDay);
        var autosaveDueDay = developerAutosaveSchedule.NextDueDay;
        var loadingCount = menu.LoadingPresentationShownCount;
        var autosaveArtworkAppeared = false;
        for (var frame = 0; frame < 18_000 &&
             (_main.UiSimulationDays < autosaveDueDay || HashFile(demoSave) == manualSaveHash); frame++)
        {
            await WaitFramesAsync(1);
            autosaveArtworkAppeared |= menu.IsStartupArtworkVisible || menu.IsLoadingCampaign ||
                menu.LoadingPresentationShownCount != loadingCount;
        }

        var saveChanged = HashFile(demoSave) != manualSaveHash;
        Require(_main.UiSimulationDays >= autosaveDueDay && saveChanged,
            $"Developer scheduled autosave incomplete: start={autosaveStartDay:0.###}, " +
            $"due={autosaveDueDay:0.###}, observed={_main.UiSimulationDays:0.###}, " +
            $"saveChanged={saveChanged}, speed={_main.UiCurrentSpeed}, paused={_main.UiIsPaused}, " +
            $"menuOpen={_main.UiIsMenuOpen}.");
        Check(!autosaveArtworkAppeared && !menu.IsStartupArtworkVisible &&
            menu.LoadingPresentationShownCount == loadingCount,
            "startup-artwork-stays-hidden-during-scheduled-autosave");
    }

    private async Task AssertStartupArtworkHiddenWhileAsync(
        MainMenuLayer menu,
        Func<Task> action,
        string activity)
    {
        var loadingCount = menu.LoadingPresentationShownCount;
        var artworkAppeared = menu.IsStartupArtworkVisible || menu.IsLoadingCampaign;
        var pending = action();
        while (!pending.IsCompleted)
        {
            await WaitFramesAsync(1);
            artworkAppeared |= menu.IsStartupArtworkVisible || menu.IsLoadingCampaign ||
                menu.LoadingPresentationShownCount != loadingCount;
        }
        await pending;
        Require(!artworkAppeared && !menu.IsStartupArtworkVisible && !menu.IsLoadingCampaign &&
                menu.LoadingPresentationShownCount == loadingCount,
            $"Startup artwork became visible during ordinary {activity}.");
    }

    private async Task VerifyMusicRuntimeAsync(AudioDirector audio)
    {
        var originalSettings = audio.Settings;
        var originalContext = audio.IsMenuContext;
        var players = Descendants(audio).OfType<AudioStreamPlayer>().ToArray();
        var music = players.SingleOrDefault(player => player.Name == "Music");
        try
        {
            Require(music?.Stream is AudioStreamMP3 { Loop: true },
                "main-score-imported-as-looping-mp3");
            Require(players.Count(player => player.Stream is AudioStreamMP3) == 1 &&
                    players.Count(player => player.Name == "Music") == 1 &&
                    music?.MaxPolyphony == 1,
                "one-active-primary-music-player");
            var primary = music ?? throw new InvalidOperationException("Primary music player was not created.");
            Require(primary.Playing, "main-score-is-playing");
            var length = primary.Stream!.GetLength();
            Require(length > .2, "main-score-length-is-readable");
            primary.Seek((float)Math.Max(0, length - .04));
            await WaitFramesAsync(8);
            var wrappedPosition = primary.GetPlaybackPosition();
            Require(primary.Playing && wrappedPosition < length - .04,
                "main-score-loops-from-end-to-start");
            var before = wrappedPosition;
            audio.SetMenuContext(false);
            Require(!audio.IsMenuContext && primary.Playing && primary.GetPlaybackPosition() >= before,
                "game-context-keeps-main-score-playing");
            audio.SetMenuContext(true);
            Require(audio.IsMenuContext && primary.Playing && primary.GetPlaybackPosition() >= before,
                "menu-context-keeps-main-score-playing");
            audio.SetVolumes(.72f, .48f, .81f);
            var normalDb = primary.VolumeDb;
            audio.SetVoiceDucking(true);
            audio._Process(.5);
            Require(primary.VolumeDb < normalDb, "voice-ducking-lowers-main-score");
            audio.SetVoiceDucking(false);
            audio._Process(1);
            Require(Math.Abs(primary.VolumeDb - normalDb) < .05,
                "voice-ducking-restores-main-score-level");
            // Exercise the resource-cache/managed-wrapper lifetime boundary seen in the
            // long Linux capture, while cycling away from each previous sound stream.
            var effects = players.Single(player => player.Name == "SoundEffects");
            Action[] sounds = [AudioDirector.PlayHover, AudioDirector.PlayConfirm,
                () => AudioDirector.PlayEvent("research"), () => AudioDirector.PlayEvent("construction"),
                () => AudioDirector.PlayEvent("ships"), () => AudioDirector.PlayEvent("combat")];
            for (var cycle = 0; cycle < 4; cycle++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                await WaitFramesAsync(5);
                foreach (var sound in sounds)
                {
                    sound();
                    Require(GodotObject.IsInstanceValid(effects.Stream) && effects.Stream!.GetLength() > 0,
                        "Sound resource became invalid after garbage collection.");
                    await WaitFramesAsync(2);
                }
            }
            Check(audio.HasRequiredAudio && primary.Playing, "audio-streams-survive-repeated-garbage-collection");
        }
        finally
        {
            audio.SetVoiceDucking(false);
            audio.SetVolumes(originalSettings.Master, originalSettings.Music, originalSettings.Sfx);
            audio.SetMenuContext(originalContext);
        }
    }

    private void CheckHomeIdentity(string check)
    {
        var identity = _main.UiHomeIdentity;
        Check(identity.SpeciesId == "terran_baseline" && identity.SystemName == "Sol" &&
            identity.ColonyName == "Earth" && identity.BodyId == 3 && identity.CatalogPresetId == "sol-v1", check);
    }

    private void AssertIconAffordance(Button button)
    {
        Require(button.Icon is not null && button.Size.X >= 36 && button.Size.Y >= 36,
            $"Icon-only control has collapsed to a blank affordance: {button.GetPath()} {button.Size}.");
        AssertInsideViewport(button, "icon control " + button.GetPath());
    }

    private async Task AssertMenuBlocksGameplayAsync(ConfirmationDialog dialog, bool firstMenu)
    {
        var state = _main.UiDashboard;
        var selection = _main.UiSelectedSystemId;
        var revision = _main.UiPointerCommandRevision;
        var section = _sidebar.ActiveSection;
        var catalog0 = _main.UiGetCatalogScreenPosition(0);
        var catalog1 = _main.UiGetCatalogScreenPosition(1);
        var camera = ObserveCamera();
        var save = ProjectSettings.GlobalizePath(_main.UiIsDeveloperMode ?
            "user://saves/developer-autosave.json" : "user://saves/autosave.json");
        var saveHash = File.Exists(save) ? HashFile(save) : null;
        // Space legitimately activates a focused Resume button. Exercise gameplay-key
        // shielding with the real seed field focused, then restore its text through keys.
        var seed = Descendants(_main.GetNode("MainMenuLayer")).OfType<LineEdit>()
            .Single(input => input.Name == "DeveloperSeed");
        var openedDevelopment = !seed.IsVisibleInTree();
        if (openedDevelopment) await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "OpenDevelopment");
        var seedText = seed.Text;
        await ClickPositionAsync(ScreenRect(seed).GetCenter(), MouseButton.Left);
        Require(seed.HasFocus(), "The mode seed field did not receive real mouse focus.");
        foreach (var key in new[] { Key.N, Key.Space, Key.Key1, Key.Key2, Key.Key3, Key.Key4,
                                   Key.T, Key.R, Key.C, Key.B, Key.V, Key.Y, Key.F6 })
            await PressKeyAsync(key);
        Require(_main.UiIsMenuOpen && _main.UiIsPaused && !dialog.Visible &&
            Equals(state, _main.UiDashboard) && _main.UiSelectedSystemId == selection &&
            saveHash == (File.Exists(save) ? HashFile(save) : null), "Gameplay keyboard command escaped the menu.");
        if (firstMenu) Check(true, "menu-blocks-gameplay-keyboard");
        await ReplaceSeedThroughKeyboardAsync(seed, seedText);

        await ClickPositionAsync(ScreenRect(NavButton("research")).GetCenter(), MouseButton.Left);
        await ClickPositionAsync(ScreenRect(NavButton("home")).GetCenter(), MouseButton.Left);
        var mapPoint = new Vector2(220, 380);
        await ClickPositionAsync(mapPoint, MouseButton.Right);
        await ClickPositionAsync(mapPoint, MouseButton.Right, ctrl: true);
        await ClickPositionAsync(mapPoint, MouseButton.Right, shift: true);
        await ClickPositionAsync(mapPoint, MouseButton.WheelUp);
        Require(Equals(camera, ObserveCamera()), "Wheel zoom escaped the open menu.");
        await ClickPositionAsync(mapPoint, MouseButton.WheelDown);
        await DragAsync(mapPoint, mapPoint + new Vector2(40, 15));
        Require(_main.UiIsMenuOpen && _main.UiIsPaused && !dialog.Visible &&
            Equals(state, _main.UiDashboard) && _main.UiSelectedSystemId == selection &&
            _main.UiPointerCommandRevision == revision && _sidebar.ActiveSection == section &&
            _main.UiGetCatalogScreenPosition(0) == catalog0 && _main.UiGetCatalogScreenPosition(1) == catalog1 &&
            Equals(camera, ObserveCamera()),
            "Gameplay pointer command or hidden navigation escaped the menu.");
        Check(true, firstMenu ? "menu-blocks-gameplay-pointer" : "menu-preserves-developer-state");
        if (openedDevelopment) await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "CloseDevelopment");
    }

    private async Task VerifyPointerShieldingAsync()
    {
        await OpenSectionAsync("research");
        // Use the drawer's opaque padding below the first-colony guide, so the probe
        // cannot activate either a project button or the persistent map guidance.
        var coveredPoint = ScreenRect(_drawer).Position + new Vector2(4, 118);
        await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home");
        var home = _main.UiSelectedSystemId;
        var homePoint = _main.UiGetCatalogScreenPosition(home)
            ?? throw new InvalidOperationException("Home catalog position unavailable.");
        await DragAsync(homePoint, coveredPoint);
        var movedHome = _main.UiGetCatalogScreenPosition(home);
        Require(movedHome.HasValue && movedHome.Value.DistanceTo(coveredPoint) < 2,
            "Real middle-drag did not place the home star beneath the drawer probe.");
        var revision = _main.UiPointerCommandRevision;
        await ClickPositionAsync(coveredPoint, MouseButton.Left);
        Check(_main.UiSelectedSystemId == home && _main.UiPointerCommandRevision > revision,
            "map-selection-positive-control");
        foreach (var modifiers in new[] { (false, false), (true, false), (false, true) })
        {
            revision = _main.UiPointerCommandRevision;
            await ClickPositionAsync(coveredPoint, MouseButton.Right, modifiers.Item1, modifiers.Item2);
            Require(_main.UiPointerCommandRevision > revision, "Uncovered map order positive control did not reach gameplay.");
        }
        Check(true, "map-order-positive-control");
        await OpenSectionAsync("research");
        revision = _main.UiPointerCommandRevision;
        await ClickPositionAsync(coveredPoint, MouseButton.Left);
        await ClickPositionAsync(coveredPoint, MouseButton.Left, doubleClick: true);
        Check(_main.UiSelectedSystemId == home && _main.UiPointerCommandRevision == revision &&
            !_main.UiIsSystemSpatialView && _sidebar.ActiveSection == "research", "drawer-blocks-map-selection");
        foreach (var modifiers in new[] { (false, false), (true, false), (false, true) })
            await ClickPositionAsync(coveredPoint, MouseButton.Right, modifiers.Item1, modifiers.Item2);
        Check(_main.UiSelectedSystemId == home && _main.UiPointerCommandRevision == revision,
            "drawer-blocks-map-orders");
        await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home");

        revision = _main.UiPointerCommandRevision;
        var railPoint = ScreenRect(NavButton("research")).GetCenter();
        await ClickPositionAsync(railPoint, MouseButton.Right);
        await ClickPositionAsync(railPoint, MouseButton.Right, ctrl: true);
        Check(_main.UiPointerCommandRevision == revision && !_sidebar.IsDrawerOpen, "rail-blocks-map-input");
        var dockPoint = ScreenRect(_dock).Position + new Vector2(4, 4);
        await ClickPositionAsync(dockPoint, MouseButton.Left);
        await ClickPositionAsync(dockPoint, MouseButton.Right);
        await ClickPositionAsync(dockPoint, MouseButton.Right, ctrl: true);
        Check(_main.UiPointerCommandRevision == revision, "dock-blocks-map-input");
    }

    private async Task OpenSectionAsync(string section)
    {
        var workspace = _main.GetNodeOrNull<ResearchWorkspaceView>("PlayerControls/ResearchWorkspace");
        if (workspace?.IsVisibleInTree() == true && section != "research")
            await CloseDrawerAsync();
        Require(_sidebar.ActiveSection != section, $"Capture tried to toggle off the already-open {section} drawer.");
        var revision = _main.UiPointerCommandRevision;
        if (section == "inspection") await ClickButtonAsync(_dock, "Inspect");
        else await ClickControlAsync(NavButton(section));
        // Opening a page schedules its live data refresh and nested container layout.
        // Wait for that first refresh before inspecting or targeting its controls.
        await WaitForRefreshAsync();
        Require(_sidebar.ActiveSection == section && _sidebar.IsDrawerOpen &&
            _main.UiPointerCommandRevision == revision, $"Real mouse did not open {section} without selecting the map.");
    }

    private void CheckExclusive(string section)
    {
        if (section == "relations")
        {
            Check(_sidebar.ActiveSection == "relations" && !_drawer.Visible && VisiblePanelCount() == 0 &&
                ActivePanel().IsVisibleInTree(), "drawer-relations-exclusive");
            AssertInsideViewport(ActivePanel(), "diplomacy workspace");
            return;
        }
        var expected = section switch
        {
            "explore" or "colonies" => "Exploration", "industry" => "Construction", "inspection" => "Inspection",
            "research" => "ResearchWorkspace",
            _ => char.ToUpperInvariant(section[0]) + section[1..],
        };
        Check(_sidebar.IsDrawerOpen && _drawer.IsVisibleInTree() && VisiblePanelCount() == 1 &&
            ActivePanel().Name == expected, $"drawer-{section}-exclusive");
        AssertInsideViewport(_drawer, section + " drawer");
        AssertIconAffordance(_main.GetNode<Button>("CampaignSidebar/DetailDrawer/Body/Header/DrawerClose"));
    }

    private int VisiblePanelCount() => _main.GetNode(PanelPath).GetChildren()
        .OfType<Control>().Count(control => control.IsVisibleInTree());

    private Control ActivePanel()
    {
        if (_sidebar.ActiveSection == "relations")
            return _main.GetNode<RelationsPanel>("RelationsPanel").Workspace;
        var workspace = _main.GetNodeOrNull<ResearchWorkspaceView>("PlayerControls/ResearchWorkspace");
        return workspace?.IsVisibleInTree() == true
            ? workspace
            : _main.GetNode(PanelPath).GetChildren().OfType<Control>().Single(control => control.IsVisibleInTree());
    }

    private async Task<Button> SelectResearchProgramThroughSearchAsync(string researchId)
    {
        var workspace = ActivePanel() as ResearchWorkspaceView
            ?? throw new InvalidOperationException("Research workspace is not the active operations page.");
        var detail = _main.UiResearchHorizon.Single(node => node.Id == researchId);
        var search = Descendants(workspace).OfType<LineEdit>().Single(line => line.Name == "ResearchSearch");
        await ReplaceLineEditThroughKeyboardAsync(search, detail.Title);
        var graph = Descendants(workspace).OfType<Button>().Single(button =>
            button.Name == "ResearchGraphNode_" + detail.GraphKey[9..]);
        Require(graph.IsVisibleInTree(), $"Search did not reveal known research '{detail.Title}'.");
        Require(workspace.SelectedCommandId == researchId,
            $"Exact title search did not select and center known research '{detail.Title}'.");
        await ClickControlAsync(graph);
        Require(workspace.SelectedCommandId == researchId, $"Graph node did not select known research '{detail.Title}'.");
        return Descendants(workspace).OfType<Button>().Single(button => button.Name == "ResearchNode_" + researchId);
    }

    private async Task ReplaceLineEditThroughKeyboardAsync(LineEdit field, string text)
    {
        await ClickPositionAsync(ScreenRect(field).GetCenter(), MouseButton.Left);
        Input.ParseInputEvent(new InputEventKey { Keycode = Key.A, PhysicalKeycode = Key.A, CtrlPressed = true, Pressed = true });
        await WaitFramesAsync(1);
        Input.ParseInputEvent(new InputEventKey { Keycode = Key.A, PhysicalKeycode = Key.A, CtrlPressed = true, Pressed = false });
        await PressKeyAsync(Key.Backspace);
        foreach (var character in text)
        {
            // Text input is Unicode. Casting punctuation directly to Key maps values such as '-'
            // to unrelated physical-key enum members and makes this real-input fixture lie.
            Input.ParseInputEvent(new InputEventKey { Unicode = character, Pressed = true });
            await WaitFramesAsync(1);
            Input.ParseInputEvent(new InputEventKey { Unicode = character, Pressed = false });
        }
        await WaitFramesAsync(2);
        Require(field.Text == text, $"Real keyboard editing did not enter '{text}'.");
    }

    private Button NavButton(string section)
    {
        var suffix = section switch
        {
            "explore" => "Explore",
            "industry" => "Construction",
            _ => char.ToUpperInvariant(section[0]) + section[1..],
        };
        return _main.GetNode<Button>("CampaignSidebar/NavigationRail/NavigationScroll/Items/Nav" + suffix);
    }

    private async Task CloseDrawerAsync()
    {
        if (!_sidebar.IsDrawerOpen) return;
        if (_sidebar.ActiveSection == "relations")
        {
            await ClickNamedButtonAsync(ActivePanel(), "DiplomacyClose");
            Require(!_sidebar.IsDrawerOpen && !_main.GetNode<RelationsPanel>("RelationsPanel").IsOpen,
                "Diplomacy Close did not restore the map.");
            return;
        }
        var workspace = _main.GetNodeOrNull<ResearchWorkspaceView>("PlayerControls/ResearchWorkspace");
        var close = workspace?.IsVisibleInTree() == true
            ? Descendants(workspace).OfType<Button>().Single(button => button.Name == "ResearchWorkspaceClose")
            : _main.GetNode<Button>("CampaignSidebar/DetailDrawer/Body/Header/DrawerClose");
        await ClickControlAsync(close);
        Require(!_sidebar.IsDrawerOpen && !_drawer.Visible && VisiblePanelCount() == 0,
            "Drawer Close did not restore the map.");
    }

    private async Task AssertSectionControlsReachableAsync()
    {
        if (ActivePanel() is ResearchWorkspaceView workspace)
        {
            AssertInsideViewport(workspace, "research workspace");
            var close = Descendants(workspace).OfType<Button>().Single(button => button.Name == "ResearchWorkspaceClose");
            AssertInsideViewport(close, "research workspace Close");
            foreach (var action in Descendants(workspace).OfType<Button>()
                         .Where(button => button.Name.ToString().StartsWith("ResearchNode_", StringComparison.Ordinal) && button.IsVisibleInTree()))
                AssertInsideViewport(action, action.Text);
            return;
        }
        foreach (var button in Descendants(ActivePanel()).OfType<Button>().Where(button => button.IsVisibleInTree()))
        {
            await RevealControlAsync(button);
            AssertInsideViewport(button, button.Text);
            if (_sidebar.ActiveSection == "relations")
            {
                for (Node? ancestor = button.GetParent(); ancestor is not null; ancestor = ancestor.GetParent())
                    if (ancestor is ScrollContainer scroll)
                        Require(Encloses(ScreenRect(scroll), ScreenRect(button)),
                            $"Diplomacy button cannot fit its scroll viewport: {button.Text}.");
                continue;
            }
            Require(Encloses(ScreenRect(_main.GetNode<Control>("CampaignSidebar/DetailDrawer/Body/DetailScroll")),
                ScreenRect(button)), $"Drawer button cannot fit its scroll viewport: {button.Text}.");
        }
        if (_sidebar.ActiveSection != "relations")
            AssertInsideViewport(_main.GetNode<Button>("CampaignSidebar/DetailDrawer/Body/Header/DrawerClose"), "drawer Close");
    }

    private async Task ClickButtonAsync(Node root, string text)
    {
        if (root == _dock)
        {
            if (text == "Back to Region" && !_main.UiIsSystemSpatialView && _main.UiOverviewBlend < .1f) return;
            var name = text switch { "Home" => "NavHome", "Open System" => "SpatialSystem",
                "Back to Region" => "SpatialRegion", "Inspect" => "NavInspect", _ => "" };
            if (name.Length > 0) { await ClickNamedButtonAsync(_main, name); await WaitForCameraAsync(); return; }
        }
        await ClickControlAsync(RequireButton(root, text));
    }

    private async Task ClickNamedButtonAsync(Node root, string name)
    {
        var button = Descendants(root).OfType<Button>().Single(button => button.Name == name);
        await ClickControlAsync(button);
    }

    private async Task OpenSettingsCategoryAsync(MainMenuLayer menu, string category)
    {
        var settings = Descendants(menu).OfType<Control>().Single(control => control.Name == "SettingsPanel");
        if (!settings.IsVisibleInTree()) await ClickNamedButtonAsync(menu, "Settings");
        await ClickNamedButtonAsync(menu, category);
    }

    private async Task VerifySettingsNavigationAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "Settings");
        var panel = Descendants(menu).OfType<Control>().Single(control => control.Name == "SettingsPanel");
        Require(panel.IsVisibleInTree(), "settings-hub-opens-from-main-menu");
        await ClickNamedButtonAsync(menu, "SettingsAudio");
        Require(menu.IsAudioSettingsVisible, "settings-audio-category-opens");
        await PressKeyAsync(Key.Escape);
        Require(panel.IsVisibleInTree(), "settings-escape-returns-from-audio-category");
        await ClickNamedButtonAsync(menu, "SettingsVideo");
        var video = Descendants(menu).OfType<Control>().Single(control => control.Name == "VideoSettingsPanel");
        Require(video.IsVisibleInTree(), "settings-video-category-opens");
        await PressKeyAsync(Key.Escape);
        Require(panel.IsVisibleInTree(), "settings-escape-returns-from-video-category");
        await ClickNamedButtonAsync(menu, "SettingsVoice");
        var voice = _main.UiVoice ?? throw new InvalidOperationException("Main did not create the voice settings controller.");
        var voicePanel = Descendants(voice).OfType<Control>().Single(control => control.Name == "VoiceSettings");
        Require(voicePanel.IsVisibleInTree(), "settings-voice-category-opens");
        await ClickNamedButtonAsync(voicePanel, "VoiceSettingsClose");
        Require(panel.IsVisibleInTree(), "settings-voice-close-returns-to-settings-hub");
        await ClickNamedButtonAsync(menu, "SettingsBack");
        Require(!panel.Visible, "settings-back-returns-to-main-menu");
        Check(true, "settings-main-menu-navigation-and-escape");
    }

    private async Task SetPlaybackSpeedAsync(SimulationClock.SpeedLevel target)
    {
        var playback = Descendants(_main).OfType<Button>().Single(button => button.Name == "SimulationPlaybackSpeedButton");
        for (var attempt = 0; attempt < 7; attempt++)
        {
            if (!_main.UiIsPaused && _main.UiCurrentSpeed == target) return;
            if (_main.UiIsPaused && _main.UiResumeSpeed == target)
            {
                await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
                await WaitForRefreshAsync();
                Require(!_main.UiIsPaused && _main.UiCurrentSpeed == target,
                    $"Visible compact playback did not resume {target}.");
                return;
            }
            await ClickControlAsync(playback);
            await WaitForRefreshAsync();
        }
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == target,
            $"Visible compact playback could not select {target}.");
    }

    private async Task VerifyCompactPlaybackAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        var button = Descendants(_main).OfType<Button>().Single(control => control.Name == "SimulationPlaybackButton");
        var speedButton = Descendants(_main).OfType<Button>().Single(control => control.Name == "SimulationPlaybackSpeedButton");
        var label = Descendants(_main).OfType<Label>().Single(control => control.Name == "SimulationPlaybackState");
        Require(button.IsVisibleInTree() && speedButton.IsVisibleInTree() && label.IsVisibleInTree() &&
            button.TooltipText.Contains("pause", StringComparison.OrdinalIgnoreCase) &&
            speedButton.TooltipText.Contains("speed", StringComparison.OrdinalIgnoreCase),
            "compact-playback-controls-are-separate-and-readable");
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickControlAsync(speedButton);
        await WaitForRefreshAsync();
        Require(_main.UiIsPaused && _main.UiResumeSpeed == SimulationClock.SpeedLevel.Fast &&
                label.Text == "PAUSED" && speedButton.Text.EndsWith("2×", StringComparison.Ordinal) && button.Text == "▶",
            "compact-playback-speed-selection-stays-paused");
        await ClickControlAsync(button);
        await WaitForRefreshAsync();
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == SimulationClock.SpeedLevel.Fast && label.Text == "2×",
            "compact-playback-toggle-resumes-selected-speed");
        await ClickControlAsync(speedButton);
        await WaitForRefreshAsync();
        Require(_main.UiCurrentSpeed == SimulationClock.SpeedLevel.VeryFast && label.Text == "3×",
            "compact-playback-speed-cycle-changes-running-rate");
        await ClickControlAsync(button);
        await WaitForRefreshAsync();
        Require(_main.UiIsPaused && label.Text == "PAUSED", "compact-playback-toggle-pauses");
        await ClickControlAsync(button);
        await WaitForRefreshAsync();
        Require(!_main.UiIsPaused && _main.UiCurrentSpeed == SimulationClock.SpeedLevel.VeryFast,
            "compact-playback-toggle-resumes-remembered-speed");
        Require(PlaybackControl.NextSpeed(SimulationClock.SpeedLevel.Maximum, false) == SimulationClock.SpeedLevel.Normal &&
            PlaybackControl.NextSpeed(SimulationClock.SpeedLevel.Maximum, true) == SimulationClock.SpeedLevel.Demo,
            "compact-playback-player-gates-24x-and-developer-allows-it");
        Check(true, "compact-playback-cycle-pause-resume-and-developer-gate");
    }

    private async Task ClickControlAsync(Button button)
    {
        await RevealControlAsync(button);
        Require(button.IsVisibleInTree() && !button.Disabled, $"Button hidden or disabled: {button.Text}.");
        AssertInsideViewport(button, button.Text);
        var path = button.GetPath().ToString();
        var rect = ScreenRect(button);
        var down = false;
        var up = false;
        var activated = false;
        void OnDown() => down = true;
        void OnUp() => up = true;
        void OnPressed() => activated = true;
        button.ButtonDown += OnDown;
        button.ButtonUp += OnUp;
        button.Pressed += OnPressed;
        try
        {
            for (var attempt = 1; attempt <= 3 && !activated; attempt++)
            {
                down = false;
                up = false;
                await ClickPositionAsync(rect.GetCenter(), MouseButton.Left);
                if (activated) break;
                // A person can move the real desktop pointer while watching a visible
                // capture. If neither edge reached the intended control, re-inject the
                // same real GUI input without warping or capturing their OS cursor.
                // A partial press is not retried because that could duplicate an action.
                if (down || up) break;
                GD.Print($"STELLAR_MOUSE_INPUT_RETRY {path} attempt={attempt} mouse={GetViewport().GetMousePosition()}");
            }
            Require(activated,
                $"Mouse did not activate {path}: target={rect}, down={down}, up={up}, mouse={GetViewport().GetMousePosition()}.");
        }
        finally
        {
            if (GodotObject.IsInstanceValid(button))
            {
                button.ButtonDown -= OnDown;
                button.ButtonUp -= OnUp;
                button.Pressed -= OnPressed;
            }
        }
    }

    private async Task ClickPositionAsync(Vector2 point, MouseButton button, bool ctrl = false,
        bool shift = false, bool doubleClick = false)
    {
        Require(GetViewport().GetVisibleRect().HasPoint(point), $"Mouse target is outside viewport: {point}.");
        // Input.ParseInputEvent enters through the window. Native mouse coordinates are
        // transformed back into logical canvas coordinates by Godot at high DPI.
        // Recompute the native point after frame boundaries: a deferred responsive-layout
        // refresh can replace the viewport transform between press and release. Reasserting
        // the pointer immediately before release also prevents an unrelated Windows cursor
        // sample from canceling a valid synthetic press in a hidden acceptance window.
        Input.UseAccumulatedInput = false; // Deterministic synthetic acceptance input; native pointer probe remains separate.
        var logicalPoint = point;
        Vector2 NativePoint() => GetViewport().GetFinalTransform() * logicalPoint;
        var nativePoint = NativePoint();
        InjectPointerEvent(new InputEventMouseMotion { Position = nativePoint, GlobalPosition = nativePoint });
        FlushPointerEvents();
        var mask = button switch
        {
            MouseButton.Left => MouseButtonMask.Left, MouseButton.Right => MouseButtonMask.Right,
            MouseButton.Middle => MouseButtonMask.Middle, _ => (MouseButtonMask)0,
        };
        nativePoint = NativePoint();
        // A real desktop mouse sample can replace the earlier synthetic hover while
        // the frame is awaited. Deliver this click's hover immediately before down.
        InjectPointerEvent(new InputEventMouseMotion { Position = nativePoint, GlobalPosition = nativePoint });
        FlushPointerEvents();
        InjectPointerEvent(new InputEventMouseButton
        {
            Position = nativePoint, GlobalPosition = nativePoint, ButtonIndex = button, ButtonMask = mask,
            Pressed = true, CtrlPressed = ctrl, ShiftPressed = shift, DoubleClick = doubleClick,
        });
        FlushPointerEvents();
        nativePoint = NativePoint();
        InjectPointerEvent(new InputEventMouseMotion { Position = nativePoint, GlobalPosition = nativePoint, ButtonMask = mask });
        FlushPointerEvents();
        InjectPointerEvent(new InputEventMouseButton
        {
            Position = nativePoint, GlobalPosition = nativePoint, ButtonIndex = button, ButtonMask = 0,
            Pressed = false, CtrlPressed = ctrl, ShiftPressed = shift,
        });
        _mouseActions++;
        GD.Print($"STELLAR_MOUSE_INPUT {button} {nativePoint.X:0.0},{nativePoint.Y:0.0} ctrl={ctrl} shift={shift} double={doubleClick}");
        await WaitFramesAsync(3);
    }

    private async Task DragAsync(Vector2 from, Vector2 to, MouseButton button = MouseButton.Middle)
    {
        // Match ClickPositionAsync: ParseInputEvent receives native window
        // coordinates, while all camera/projected marker points are logical
        // viewport coordinates. This matters on the high-DPI capture target.
        from = GetViewport().GetFinalTransform() * from;
        to = GetViewport().GetFinalTransform() * to;
        var mask = button switch
        {
            MouseButton.Left => MouseButtonMask.Left, MouseButton.Right => MouseButtonMask.Right,
            MouseButton.Middle => MouseButtonMask.Middle, _ => (MouseButtonMask)0,
        };
        InjectPointerEvent(new InputEventMouseMotion { Position = from, GlobalPosition = from });
        FlushPointerEvents();
        InjectPointerEvent(new InputEventMouseButton
        {
            Position = from, GlobalPosition = from, ButtonIndex = button,
            ButtonMask = mask, Pressed = true,
        });
        FlushPointerEvents();
        var midpoint = (from + to) * .5f;
        InjectPointerEvent(new InputEventMouseMotion
        {
            Position = midpoint, GlobalPosition = midpoint, Relative = midpoint - from, ButtonMask = mask,
        });
        FlushPointerEvents();
        InjectPointerEvent(new InputEventMouseMotion
        {
            Position = to, GlobalPosition = to, Relative = to - midpoint, ButtonMask = mask,
        });
        FlushPointerEvents();
        InjectPointerEvent(new InputEventMouseButton
        {
            Position = to, GlobalPosition = to, ButtonIndex = button, Pressed = false,
        });
        FlushPointerEvents();
        _mouseActions++;
        GD.Print($"STELLAR_MOUSE_INPUT {button}Drag {from.X:0.0},{from.Y:0.0} to {to.X:0.0},{to.Y:0.0}");
        await WaitFramesAsync(3);
    }

    private async Task RevealControlAsync(Control control)
    {
        // Structural card reconciliation can restore a prior scroll position on the next
        // deferred frame. Converge on two fully visible frames before mouse-down so that
        // deferred layout cannot undo the first EnsureControlVisible request.
        var scrolls = new List<ScrollContainer>();
        for (Node? ancestor = control.GetParent(); ancestor is not null; ancestor = ancestor.GetParent())
            if (ancestor is ScrollContainer scroll) scrolls.Add(scroll);
        var visibleFrames = 0;
        for (var attempt = 0; attempt < 8 && visibleFrames < 2; attempt++)
        {
            Require(GodotObject.IsInstanceValid(control) && control.IsInsideTree(),
                "The control was removed before its visible pointer action began.");
            foreach (var scroll in scrolls)
                scroll.EnsureControlVisible(control);
            await WaitFramesAsync(1);
            var bounds = ScreenRect(control);
            var fullyVisible = Encloses(GetViewport().GetVisibleRect(), bounds) &&
                               scrolls.All(scroll => Encloses(ScreenRect(scroll), bounds));
            visibleFrames = fullyVisible ? visibleFrames + 1 : 0;
        }
        Require(visibleFrames == 2,
            $"Control could not settle fully through its scroll chain: control={ScreenRect(control)}, " +
            $"scrolls=[{string.Join(", ", scrolls.Select(scroll => ScreenRect(scroll).ToString()))}].");
    }

    private Rect2 ScreenRect(Control control)
    {
        var rect = control.GetGlobalRect();
        if (control.GetViewport() is Window window && window != GetWindow())
            rect.Position += (Vector2)window.Position; // Embedded confirmation uses its own viewport.
        return rect;
    }

    private void AssertInsideViewport(Control control, string label) =>
        Require(Encloses(GetViewport().GetVisibleRect(), ScreenRect(control)),
            $"{label} is clipped by the viewport: {ScreenRect(control)}.");

    private static bool Encloses(Rect2 outer, Rect2 inner) =>
        inner.Position.X >= outer.Position.X - 1 && inner.Position.Y >= outer.Position.Y - 1 &&
        inner.End.X <= outer.End.X + 1 && inner.End.Y <= outer.End.Y + 1;

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private void Check(bool condition, string name)
    {
        Require(condition, $"Visual interaction check failed: {name}.");
        Require(!_checks.Contains(name), $"Duplicate check: {name}.");
        _checks.Add(name);
        GD.Print($"STELLAR_UI_CHECK_PASS {name}");
    }

    private static IEnumerable<Node> Descendants(Node root)
    {
        foreach (Node child in root.GetChildren())
        {
            yield return child;
            foreach (var descendant in Descendants(child)) yield return descendant;
        }
    }

    private static Button RequireButton(Node root, string text) => Descendants(root).OfType<Button>()
        .FirstOrDefault(button => button.IsVisibleInTree() && string.Equals(button.Text, text, StringComparison.Ordinal))
        ?? throw new InvalidOperationException($"Visible button not found: {text}.");

    private static T? FindNode<T>(Node node) where T : Node =>
        node as T ?? Descendants(node).OfType<T>().FirstOrDefault();

    private async Task PressKeyAsync(Key key)
    {
        Input.ParseInputEvent(new InputEventKey { Keycode = key, PhysicalKeycode = key, Pressed = true });
        await WaitFramesAsync(1);
        Input.ParseInputEvent(new InputEventKey { Keycode = key, PhysicalKeycode = key, Pressed = false });
        await WaitFramesAsync(1);
    }

    private async Task WaitForRefreshAsync()
    {
        await ToSignal(GetTree().CreateTimer(0.7), SceneTreeTimer.SignalName.Timeout);
        await WaitFramesAsync(3);
    }

    private async Task WaitFramesAsync(int count)
    {
        for (var frame = 0; frame < count; frame++)
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
    }

    private void AssertCaptionDoesNotCover(Control control, string checkName)
    {
        var caption = _main.UiVoice ?? throw new InvalidOperationException("Main did not create UiVoice.");
        var sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        if (!caption.UiCaptionVisible) throw new InvalidOperationException($"{checkName}: active caption was not visible.");
        if (!sidebar.IsDrawerOpen) throw new InvalidOperationException($"{checkName}: campaign drawer was not open.");
        Check(!caption.UiCaptionBounds.Intersects(sidebar.UiDrawerBounds) &&
              ScreenRect(control).End.Y <= caption.UiCaptionBounds.Position.Y + 1,
            checkName);
    }

    private async Task<VoiceSettings> BeginCaptionLayoutProbeAsync(string key)
    {
        var voice = _main.UiVoice ?? throw new InvalidOperationException("Main did not create UiVoice.");
        var previous = voice.Settings;
        voice.Stop();
        voice.ApplySettings(previous with { EnableVoices = false, Subtitles = true, SpeakerLabels = true });
        voice.Speak(new SpeechRequest("human_female_narrator",
            "Caption layout check. Ship costs and cancellation actions remain visible above this caption.")
        {
            DedupeKey = key,
            AllowSynthesis = false,
            SpeakerName = "Narrator",
            SpeakerRole = VoiceSpeakerRole.Narrator,
            SubtitleText = "Caption layout check. Ship costs and cancellation actions remain visible above this caption.",
        });
        await WaitUntilAsync(() => voice.UiCaptionVisible && voice.ActiveSubtitle.Length > 0, 5,
            "Caption layout probe did not become visible.");
        await WaitFramesAsync(3);
        return previous;
    }

    private async Task SaveViewportAsync(string fileName, int width = 1280, int height = 720)
    {
        await WaitFramesAsync(3);
        using var image = GetViewport().GetTexture().GetImage();
        if (width == 0 && image is not null) { width = image.GetWidth(); height = image.GetHeight(); }
        var window = GetWindow();
        var textureSize = image is null ? "unavailable" : $"{image.GetWidth()}x{image.GetHeight()}";
        Require(image is not null && image.GetWidth() == width && image.GetHeight() == height,
            $"Viewport image unavailable or wrong size for {fileName}: requested={width}x{height}, texture={textureSize}, " +
            $"window={window.Size}, content-scale-size={window.ContentScaleSize}, content-scale-mode={window.ContentScaleMode}, " +
            $"visible={GetViewport().GetVisibleRect().Size}.");
        var path = Path.Combine(_outputDirectory, fileName);
        if (image!.SavePng(path) != Error.Ok) throw new IOException($"Could not save {fileName}.");
        var bytes = new FileInfo(path).Length;
        Require(bytes >= 4096, $"{fileName} is unexpectedly small.");
        _captures.Add(fileName);
        _captureRecords.Add(new { file = fileName, width = image.GetWidth(), height = image.GetHeight(),
            bytes, sha256 = HashFile(path) });
        GD.Print($"STELLAR_SCREENSHOT_CAPTURED {fileName} {image.GetWidth()}x{image.GetHeight()} {bytes} bytes");
    }

    private static string HashFile(string path) => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();

    private void WriteManifest()
    {
        var sha = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_SHA") ?? "unknown";
        var manifest = new { schema_version = 2, git_sha = sha, build = _main.UiBuildLabel,
            input_mode = CaptureInputMode, mouse_actions = _mouseActions,
            captures = _captureRecords, checks = _checks };
        File.WriteAllText(Path.Combine(_outputDirectory, "capture-manifest.json"),
            JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));
        File.WriteAllText(Path.Combine(_outputDirectory, "manifest.txt"),
            $"Stellar Continuum graphical navigation capture\nBuild: {_main.UiBuildLabel}\nGit SHA: {sha}\n" +
            $"Scene: real res://scenes/Main.tscn\nMouse actions: {_mouseActions} via {CaptureInputMode}\n" +
            $"Screenshots: {string.Join(", ", _captures)}\nPassed checks: {string.Join(", ", _checks)}\n" +
            "Scope: real mouse/keyboard routing, 1280x720 layout, camera/resize inverse picking, free 3D surface placement, ordinary construction, Player/Developer save isolation and explicit tool provenance. " +
            "Does not certify long-campaign progression or the Windows GPU renderer.\n");
    }
}
