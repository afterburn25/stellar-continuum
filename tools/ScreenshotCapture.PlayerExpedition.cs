using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Game.Campaign;
using Game.Presentation;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

/// <summary>
/// Focused native evidence for the complete ordinary-player opening. This deliberately uses no
/// Developer actions: every order below is a visible control click or a map pointer command.
/// </summary>
public partial class ScreenshotCapture
{
    private const string FirstWarpCheckpointSha256 = "4ccf0892744279a8704c5cf90b78704d027f511e67d13687f629bddf1d15110d";
    private Stopwatch? _playerExpeditionStopwatch;
    private object? _playerAuthorizationSaveEvidence;
    private object? _playerSettlementEvidence;
    private object? _playerReloadEvidence;
    private double _playerAuthorizationSimulationDays;
    private static readonly string[] OpeningConstruction =
    {
        "research_network", "industrial_automation", "orbital_launch_complex", "orbital_shipyard", "warp_test_facility",
    };

    private async Task VerifyPlayerExpeditionAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        _playerExpeditionStopwatch = Stopwatch.StartNew();
        try
        {
            await StartFreshOrdinarySandboxAsync(menu, dialog);
            await SelectMaximumPlayerSpeedAsync();
            Require(_main.UiCurrentSpeed == Game.Simulation.SimulationClock.SpeedLevel.Maximum && !_main.UiIsDeveloperMode,
                "Ordinary Player expedition did not begin at the visible 8× speed setting.");
            Require(_main.UiDashboard.TotalSystemCount == 500, "Configured Sandbox did not generate the ordinary 500-system campaign.");

            await StartOpeningResearchForEvidenceAsync();
            await SaveViewportAsync("player-expedition-01-opening-research.png");
            await ProgressToWarpAndShipOrdersAsync();
            await SaveFirstWarpCheckpointAsync();
            await SaveViewportAsync("player-expedition-02-first-warp-shipyard.png");
            await CompleteSurveyAndSettlementAsync();
            await VerifyPlayerExpeditionSaveReloadAsync(menu, dialog);
            await OpenSectionAsync("colonies");
            Require(Descendants(ActivePanel()).Any(node => node.Name.ToString().StartsWith("OwnedColony_", StringComparison.Ordinal)),
                "Reloaded colony evidence did not display the owned-world cards.");
            await SaveViewportAsync("player-expedition-04-reloaded-colony.png");
            WritePlayerExpeditionEvidenceManifest();
        }
        finally { _playerExpeditionStopwatch = null; }
    }

    private async Task VerifyPlayerExpeditionControlsAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        _playerExpeditionStopwatch = Stopwatch.StartNew();
        try
        {
            await StartFreshOrdinarySandboxAsync(menu, dialog);
            await SelectMaximumPlayerSpeedAsync();
            await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
            var pausedDays = _main.UiSimulationDays;
            await SelectMaximumPlayerSpeedAsync();
            Check(!_main.UiIsPaused &&
                  _main.UiCurrentSpeed == Game.Simulation.SimulationClock.SpeedLevel.Maximum &&
                  _main.UiSimulationDays > pausedDays,
                "player-expedition-paused-maximum-speed-reselection-resumes-clock");
            await OpenSectionAsync("research");
            var deadline = Stopwatch.StartNew();
            while (deadline.Elapsed < TimeSpan.FromMinutes(2))
            {
                var startable = _main.UiResearchHorizon.FirstOrDefault(node => node.CanStart);
                if (startable is not null)
                {
                    await ClickControlAsync(await SelectResearchProgramThroughSearchAsync(startable.Id));
                    await VerifyOpeningResearchControlsAsync(startable.Id);
                    await SaveViewportAsync("player-expedition-controls.png");
                    WritePlayerExpeditionEvidenceManifest();
                    return;
                }
                await WaitForRefreshAsync();
            }
            throw new InvalidOperationException("Ordinary Player research controls did not expose a legal startable program within two active minutes.");
        }
        finally { _playerExpeditionStopwatch = null; }
    }

    private async Task VerifyPlayerExpeditionResumeAsync(MainMenuLayer menu)
    {
        _playerExpeditionStopwatch = Stopwatch.StartNew();
        try
        {
            var savePath = ProjectSettings.GlobalizePath("user://saves/autosave.json");
            Require(File.Exists(savePath), "Player expedition resume requires an isolated preserved autosave.");
            var inputHash = HashFile(savePath);
            var expectedHash = System.Environment.GetEnvironmentVariable("STELLAR_PLAYER_EXPEDITION_RESUME_SHA256");
            Require(!string.IsNullOrWhiteSpace(expectedHash) &&
                    string.Equals(inputHash, expectedHash, StringComparison.OrdinalIgnoreCase),
                "Player expedition resume input did not match its declared preserved-save SHA-256.");
            await ClickNamedButtonAsync(menu, "ResumeCampaign");
            await WaitFramesAsync(4);
            GD.Print($"STELLAR_PLAYER_RESUME_STATE developer={_main.UiIsDeveloperMode} tools={_main.UiDeveloperToolsUsed} " +
                     $"systems={_main.UiDashboard.TotalSystemCount} days={_main.UiSimulationDays:R} " +
                     $"warpResearch={string.Join(',', _main.UiResearchHorizon.Where(node => node.Id.Contains("warp", StringComparison.Ordinal)).Select(node => $"{node.Id}:{node.State}"))}");
            Require(!_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed && _main.UiDashboard.TotalSystemCount == 500 &&
                    _main.UiSimulationDays > 5_000 &&
                    _main.UiResearchHorizon.Any(node =>
                        node.Id == "warp_field_control" && node.State == "MATURE"),
                "Preserved Player expedition save did not retain its ordinary campaign and completed opening prefix.");
            Check(true, "player-expedition-resumed-from-preserved-ordinary-save");
            await SelectMaximumPlayerSpeedAsync();
            await ProgressToWarpAndShipOrdersAsync();
            await SaveFirstWarpCheckpointAsync();
            await SaveViewportAsync("player-expedition-02-first-warp-shipyard.png");
            await CompleteSurveyAndSettlementAsync();
            await VerifyPlayerExpeditionSaveReloadAsync(menu, FindNode<ConfirmationDialog>(menu)
                ?? throw new InvalidOperationException("Campaign confirmation dialog did not instantiate."));
            await OpenSectionAsync("colonies");
            Require(Descendants(ActivePanel()).Any(node => node.Name.ToString().StartsWith("OwnedColony_", StringComparison.Ordinal)),
                "Reloaded colony evidence did not display the owned-world cards.");
            await SaveViewportAsync("player-expedition-04-reloaded-colony.png");
            File.WriteAllText(Path.Combine(_outputDirectory, "player-expedition-resume-receipt.json"),
                JsonSerializer.Serialize(new
                {
                    schema = "player-expedition-preserved-prefix-resume-v1",
                    sourceRevision = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_SHA") ?? "unknown",
                    inputSaveSha256 = inputHash,
                    inputSimulationDaysMinimum = 5_000,
                    completed = true,
                    checks = _checks.ToArray(),
                }, new JsonSerializerOptions { WriteIndented = true }));
        }
        finally { _playerExpeditionStopwatch = null; }
    }

    private async Task VerifyPlayerExpeditionCheckpointAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        var checkpoint = System.Environment.GetEnvironmentVariable("STELLAR_PLAYER_EXPEDITION_CHECKPOINT");
        if (string.IsNullOrWhiteSpace(checkpoint) || !File.Exists(checkpoint))
            throw new InvalidOperationException(
                "Focused survey recovery requires STELLAR_PLAYER_EXPEDITION_CHECKPOINT to name the reviewed first-warp save.");
        Require(HashFile(checkpoint) == FirstWarpCheckpointSha256,
            "Focused survey recovery received a different first-warp checkpoint.");
        var playerSave = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        Directory.CreateDirectory(Path.GetDirectoryName(playerSave)!);
        File.Copy(checkpoint, playerSave, overwrite: true);

        _playerExpeditionStopwatch = Stopwatch.StartNew();
        try
        {
            await WaitForRefreshAsync();
            await LoadCurrentCampaignThroughMenuAsync(menu, dialog, captureConfirmation: true);
            Require(!_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed && _main.UiOwnedFleets.Count(fleet =>
                        fleet.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony) == 3,
                "Reviewed checkpoint did not load its three ordinary physical expedition ships.");
            await SelectMaximumPlayerSpeedAsync();
            await CompleteSurveyAndSettlementAsync();
            await SaveViewportAsync("player-expedition-checkpoint-route-recovery.png");
            GD.Print("STELLAR_PLAYER_EXPEDITION_CHECKPOINT_ROUTE_RECOVERY_COMPLETE");
        }
        finally { _playerExpeditionStopwatch = null; }
    }

    private async Task StartFreshOrdinarySandboxAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        Require(_main.UiIsMenuOpen && !_main.UiIsDeveloperMode, "Focused Player journey requires the normal startup menu.");
        await ClickNamedButtonAsync(menu, "NewPlayerCampaign");
        await ClickNamedButtonAsync(menu, "SandboxCampaignOption");
        var seed = Descendants(menu).OfType<LineEdit>().Single(input => input.Name == "SandboxSeed");
        await ReplaceSeedThroughKeyboardAsync(seed, "20260908");
        await ClickNamedButtonAsync(menu, "StartConfiguredSandbox");
        Require(dialog.Visible, "Fresh Player Sandbox skipped its confirmation.");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForCampaignLoadingAsync();
        Require(!_main.UiIsMenuOpen && !_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed,
            "Fresh Sandbox did not enter ordinary Player mode.");
        Check(true, "player-expedition-fresh-ordinary-sandbox");
    }

    private async Task StartOpeningResearchForEvidenceAsync()
    {
        await OpenSectionAsync("research");
        var opening = EarlyCampaignResearchPlan.WarpCapabilityPath
            .Select(id => _main.UiResearchHorizon.FirstOrDefault(node => node.Id == id))
            .FirstOrDefault(node => node is { CanStart: true })
            ?? throw new InvalidOperationException("Ordinary Player opening exposed no legal research program.");
        await ClickControlAsync(await SelectResearchProgramThroughSearchAsync(opening.Id));
        Check(true, "player-expedition-research-" + opening.Id);
        await VerifyOpeningResearchControlsAsync(opening.Id);
    }

    private async Task SelectMaximumPlayerSpeedAsync()
    {
        await SetPlaybackSpeedAsync(Game.Simulation.SimulationClock.SpeedLevel.Maximum);
        Require(!_main.UiIsPaused &&
                _main.UiCurrentSpeed == Game.Simulation.SimulationClock.SpeedLevel.Maximum,
            "Visible compact playback did not select the Player 8× speed.");
        await WaitForRefreshAsync();
    }

    private async Task ProgressToWarpAndShipOrdersAsync()
    {
        var startedConstruction = new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
        var startedShips = new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
        var researchIndex = 0;
        while (WithinPlayerExpeditionBudget())
        {
            if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "research") await OpenSectionAsync("research");
            while (researchIndex < EarlyCampaignResearchPlan.WarpCapabilityPath.Count &&
                   _main.UiResearchHorizon.Any(node => node.Id == EarlyCampaignResearchPlan.WarpCapabilityPath[researchIndex] && node.State == "MATURE"))
                researchIndex++;
            if (researchIndex < EarlyCampaignResearchPlan.WarpCapabilityPath.Count)
            {
                var id = EarlyCampaignResearchPlan.WarpCapabilityPath[researchIndex];
                var node = _main.UiResearchHorizon.SingleOrDefault(value => value.Id == id);
                if (node is { CanStart: true })
                {
                    await ClickExpeditionChoiceWhenVisibleAsync("ResearchNode_" + id);
                    Check(true, "player-expedition-research-" + id);
                    if (researchIndex == 0) await VerifyOpeningResearchControlsAsync(id);
                }
            }

            if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "industry") await OpenSectionAsync("industry");
            if (_main.UiConstructionOrders.Count == 0)
            {
                var next = OpeningConstruction.FirstOrDefault(id => !startedConstruction.Contains(id) &&
                    Descendants(ActivePanel()).OfType<Button>().Any(button => button.Name == "Choose" + id && !button.Disabled));
                if (next is not null)
                {
                    await ClickNamedButtonAsync(ActivePanel(), "Choose" + next);
                    startedConstruction.Add(next);
                    Check(true, "player-expedition-construction-" + next);
                }
            }

            if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "ships") await OpenSectionAsync("ships");
            foreach (var id in new[] { "warp_scout", "science_vessel", "colony_ship" })
            {
                if (startedShips.Contains(id) || _main.UiOwnedFleets.Any(fleet => fleet.DesignId == id) ||
                    _main.UiShipyardOrders.Any(order => order.DesignId == id))
                    continue;
                var choice = _main.UiShipChoices.SingleOrDefault(value => value.Id == id && !value.IsCancellation);
                if (choice is { CanAfford: true })
                {
                    await ClickExpeditionChoiceWhenVisibleAsync("Choose" + id);
                    startedShips.Add(id);
                    Check(true, "player-expedition-build-" + id);
                }
            }

            if (_main.UiShipChoices.Any(choice => !choice.IsCancellation && choice.Id == "warp_scout") &&
                _main.UiResearchHorizon.FirstOrDefault(node => node.CanPause) is { } activeResearch)
            {
                if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "research") await OpenSectionAsync("research");
                await ClickExpeditionChoiceWhenVisibleAsync("ResearchNode_" + activeResearch.Id);
                Check(true, "player-expedition-pauses-active-research-for-shipbuilding-capital");
            }

            if (_main.UiDashboard.DemoStep >= 1 && new[] { FleetRole.Scout, FleetRole.Science, FleetRole.Colony }
                .All(role => _main.UiOwnedFleets.Any(fleet => fleet.Role == role)))
            {
                Check(true, "player-expedition-first-warp-completed");
                return;
            }
            await WaitForRefreshAsync();
        }
        throw new InvalidOperationException("Ordinary Player opening did not reach physical scout, science, and colony ships within the shared 22-minute journey budget.");
    }

    private async Task SaveFirstWarpCheckpointAsync()
    {
        var resume = !_main.UiIsPaused;
        if (resume) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        Require(!_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed &&
                new[] { FleetRole.Scout, FleetRole.Science, FleetRole.Colony }
                    .All(role => _main.UiOwnedFleets.Any(fleet => fleet.Role == role)),
            "The first-warp diagnostic checkpoint requires the three physically completed Player ships.");
        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var source = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        Require(File.Exists(source), "The first-warp Player checkpoint was not written through the visible Save control.");
        File.Copy(source, Path.Combine(_outputDirectory, "player-expedition-first-warp-save.json"), overwrite: true);
        Check(true, "player-expedition-first-warp-gui-checkpoint-saved");
        await OpenSectionAsync("ships");
        if (resume) await SelectMaximumPlayerSpeedAsync();
    }

    private async Task ClickExpeditionChoiceWhenVisibleAsync(string name)
    {
        // The authoritative horizon can advance between the panel's scheduled refreshes.
        // Wait for its real enabled control; never issue the command through the read model.
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromSeconds(5) && WithinPlayerExpeditionBudget())
        {
            var notificationCenter = _main.GetNode<Control>("PlayerControls/NotificationCenter");
            if (notificationCenter.IsVisibleInTree())
            {
                await ClickNamedButtonAsync(notificationCenter, "NotificationClose");
                Require(!notificationCenter.Visible, "Recent Events remained above the Player choice after its real Close action.");
                Check(true, "player-expedition-closes-recent-events-before-covered-choice");
            }
            if (name.StartsWith("ResearchNode_", StringComparison.Ordinal) && ActivePanel() is ResearchWorkspaceView)
            {
                var researchId = name["ResearchNode_".Length..];
                if (_main.UiResearchHorizon.Any(node => node.Id == researchId && (node.CanStart || node.CanPause || node.CanResume)))
                {
                    var action = await SelectResearchProgramThroughSearchAsync(researchId);
                    if (!action.Disabled) { await ClickControlAsync(action); return; }
                }
            }
            var button = Descendants(ActivePanel()).OfType<Button>()
                .SingleOrDefault(control => control.Name == name && control.IsVisibleInTree() && !control.Disabled);
            if (button is not null)
            {
                await ClickControlAsync(button);
                return;
            }
            await WaitForRefreshAsync();
        }
        throw new InvalidOperationException($"Player choice '{name}' did not render enabled in '{_sidebar.ActiveSection}' after its canonical unlock. status='{_main.UiStatusMessage}'.");
    }

    private async Task CompleteSurveyAndSettlementAsync()
    {
        if (_sidebar.IsDrawerOpen) await CloseDrawerAsync();
        await ClickButtonAsync(_dock, "Home");
        var scout = _main.UiOwnedFleets.Single(fleet => fleet.Role == FleetRole.Scout);
        var science = _main.UiOwnedFleets.Single(fleet => fleet.Role == FleetRole.Science);
        var refuelingSystemId = scout.CurrentSystemId
            ?? throw new InvalidOperationException("The new scout did not begin at an owned refueling colony.");
        Require(science.CurrentSystemId == refuelingSystemId,
            "New survey ships did not begin together at their owned refueling colony.");
        ColonyOpportunityUiState opportunity = FindFundedSettlementOpportunity();
        for (var survey = 0; survey < 8 && !opportunity.CanOrder; survey++)
        {
            await ClickButtonAsync(_dock, "Home");
            scout = _main.UiOwnedFleets.Single(fleet => fleet.FleetId == scout.FleetId);
            science = _main.UiOwnedFleets.Single(fleet => fleet.FleetId == science.FleetId);
            Require(scout.CurrentSystemId == refuelingSystemId && science.CurrentSystemId == refuelingSystemId,
                "Survey ships did not finish their visible return to the owned refueling colony.");
            Require(Math.Abs(scout.FuelRemainingLightYears - scout.FuelCapacityLightYears) < .000001 &&
                    Math.Abs(science.FuelRemainingLightYears - science.FuelCapacityLightYears) < .000001,
                "Survey ships did not receive full-service colony refueling before target selection.");
            var scoutTarget = FindRoundTripPublicStar(scout, science, requireUnknown: true);
            await SelectFleetByMarkerAsync(scout.FleetId);
            await ClickPositionAsync(await BringPublicStarIntoMapBoundsAsync(scoutTarget), MouseButton.Right);
            await RequireRouteStartedAsync(scout.FleetId, scoutTarget, "scout right-click");
            Check(true, "player-expedition-scout-right-click-order-" + (survey + 1));
            await WaitForSurveyLevelAsync(scoutTarget, "PartiallySurveyed", "scout reconnaissance");

            await ClickButtonAsync(_dock, "Home");
            science = _main.UiOwnedFleets.Single(fleet => fleet.FleetId == science.FleetId);
            await SelectFleetByMarkerAsync(science.FleetId);
            await ClickPositionAsync(await BringPublicStarIntoMapBoundsAsync(scoutTarget), MouseButton.Right);
            await RequireRouteStartedAsync(science.FleetId, scoutTarget, "science right-click");
            Check(true, "player-expedition-science-right-click-order-" + (survey + 1));
            await WaitForSurveyLevelAsync(scoutTarget, "FullySurveyed", "science detailed survey");
            opportunity = FindFundedSettlementOpportunity();
            if (!opportunity.CanOrder)
            {
                await ReturnSurveyFleetForRefuelingAsync(scout.FleetId, refuelingSystemId, "scout");
                await ReturnSurveyFleetForRefuelingAsync(science.FleetId, refuelingSystemId, "science vessel");
                Check(true, "player-expedition-survey-pair-returns-to-owned-refueling-colony-" + (survey + 1));
            }
        }

        Require(opportunity is { FleetId: not null, SystemId: not null, PlanetaryBodyId: not null, CanOrder: true },
            "The ordinary colonies panel did not expose a funded, fully surveyed settlement opportunity after eight public survey attempts.");
        var colonyFleetId = opportunity.FleetId!.Value;
        var settlementSystemId = opportunity.SystemId!.Value;
        var settlementBodyId = opportunity.PlanetaryBodyId!.Value;
        var colonyCountBefore = _main.UiOwnedColonies.Length;
        await OpenSectionAsync("colonies");
        var previousSite = RequireButton(ActivePanel(), "← Site");
        while (!previousSite.Disabled)
        {
            await ClickControlAsync(previousSite);
            previousSite = RequireButton(ActivePanel(), "← Site");
        }
        for (var site = 0; site < opportunity.SiteIndex; site++)
            await ClickButtonAsync(ActivePanel(), "Site →");
        var visibleOpportunity = _main.GetUiColonyOpportunityState(opportunity.FleetIndex, opportunity.SiteIndex);
        Require(visibleOpportunity.CanOrder && visibleOpportunity.FleetId == colonyFleetId &&
                visibleOpportunity.SystemId == settlementSystemId && visibleOpportunity.PlanetaryBodyId == settlementBodyId,
            "Visible Colony Sites paging did not reach the selected canonical opportunity.");
        await ClickControlAsync(RequireButton(ActivePanel(), "Select ship on map"));
        Require(_main.UiSelectedFleetId == colonyFleetId, "Colony Sites did not select its real populated colony ship.");
        await SelectFleetByMarkerAsync(colonyFleetId);
        await ClickPositionAsync(await BringPublicStarIntoMapBoundsAsync(settlementSystemId), MouseButton.Right);
        await RequireRouteStartedAsync(colonyFleetId, settlementSystemId, "colony transit right-click");
        Check(true, "player-expedition-colony-transit-right-click-order");
        await WaitForFleetAtSelectedSystemAsync(colonyFleetId, settlementSystemId);
        await ClickPositionAsync(await BringPublicStarIntoMapBoundsAsync(settlementSystemId), MouseButton.Left);
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        var localColonyShip = Descendants(_main).OfType<Button>()
            .Single(button => button.Name == "SystemFleet" + colonyFleetId);
        await ClickControlAsync(localColonyShip);
        Require(_main.UiSelectedFleetId == colonyFleetId,
            "The visible local system fleet control did not select the arrived colony ship.");
        var bodyPoint = _main.UiGetBodyScreenPosition(settlementBodyId)
            ?? throw new InvalidOperationException("Surveyed settlement world has no visible system-view body marker.");
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickPositionAsync(bodyPoint, MouseButton.Right);
        await WaitForRefreshAsync();
        var authorized = _main.UiOwnedFleets.Single(fleet => fleet.FleetId == colonyFleetId);
        Check(authorized.CurrentSystemId == settlementSystemId &&
              authorized.DestinationPlanetaryBodyId == settlementBodyId && authorized.SettlementBodyId is null &&
              authorized.EmbarkedPopulationMillions > 0 && !_main.UiOwnedColonies.Any(colony => colony.BodyId == settlementBodyId),
            "player-expedition-colony-body-right-click-authorizes-settlement");
        await SavePlayerSettlementAuthorizationAsync(colonyFleetId, settlementBodyId, authorized.EmbarkedPopulationMillions);
        await WaitForColonyAndPauseAsync(colonyCountBefore, settlementBodyId, colonyFleetId);
        var founded = _main.UiOwnedColonies.Single(colony => colony.BodyId == settlementBodyId);
        var colonyShipConsumed = !_main.UiOwnedFleets.Any(fleet => fleet.FleetId == colonyFleetId);
        GD.Print($"STELLAR_PLAYER_SETTLEMENT_STATE colonies={_main.UiOwnedColonies.Length}/{colonyCountBefore + 1} " +
                 $"body={founded.BodyId}/{settlementBodyId} population={founded.PopulationMillions:R} " +
                 $"authorized={authorized.EmbarkedPopulationMillions:R} consumed={colonyShipConsumed} paused={_main.UiIsPaused}");
        Check(_main.UiOwnedColonies.Length == colonyCountBefore + 1 &&
              Math.Abs(founded.PopulationMillions - authorized.EmbarkedPopulationMillions) <=
              authorized.EmbarkedPopulationMillions * .001 &&
              colonyShipConsumed && _main.UiIsPaused,
            "player-expedition-exact-body-founded-and-colony-ship-consumed");
        _playerSettlementEvidence = new { fleet_id = colonyFleetId, body_id = settlementBodyId,
            colonies_before = colonyCountBefore, colonies_after = _main.UiOwnedColonies.Length,
            authorized_population_millions = authorized.EmbarkedPopulationMillions,
            observed_population_millions = founded.PopulationMillions,
            authorization_simulation_days = _playerAuthorizationSimulationDays,
            observed_simulation_days = _main.UiSimulationDays,
            colony_ship_consumed = colonyShipConsumed, observation_paused = _main.UiIsPaused };
        var establishmentDays = _main.UiSimulationDays - _playerAuthorizationSimulationDays;
        Check(establishmentDays is >= 29.0 and <= 35.0,
            "player-expedition-settlement-observes-canonical-timer");
        Check(true, "player-expedition-settlement-timed-and-complete");
    }

    private ColonyOpportunityUiState FindFundedSettlementOpportunity()
    {
        var first = _main.GetUiColonyOpportunityState(0, 0);
        for (var site = 0; site < first.SiteCount; site++)
        {
            var candidate = _main.GetUiColonyOpportunityState(first.FleetIndex, site);
            if (candidate.CanOrder) return candidate;
        }
        return first;
    }

    private async Task VerifyOpeningResearchControlsAsync(string researchId)
    {
        // Allow the legitimate Start -> Pause action rebuild before measuring live updates.
        await WaitForRefreshAsync();
        var control = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Name == "ResearchNode_" + researchId);
        var instance = control.GetInstanceId();
        var opening = _main.UiResearchHorizon.Single(node => node.Id == researchId);
        var progressBefore = opening.Progress;
        var detailBefore = opening.Detail;
        Require(opening.CanPause, "Opening research did not enter its active, pausable state.");
        await WaitForRefreshAsync();
        var refreshed = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Name == "ResearchNode_" + researchId);
        var progressAfter = _main.UiResearchHorizon.Single(node => node.Id == researchId).Progress;
        GD.Print($"STELLAR_RESEARCH_CONTROL id={researchId} sameInstance={refreshed.GetInstanceId() == instance} focus={refreshed.HasFocus()} progress={progressBefore:R}->{progressAfter:R} tooltipCurrent={refreshed.TooltipText.Contains(_main.UiResearchHorizon.Single(node => node.Id == researchId).Detail, StringComparison.Ordinal)} before='{detailBefore}' after='{_main.UiResearchHorizon.Single(node => node.Id == researchId).Detail}'");
        Check(refreshed.GetInstanceId() == instance && refreshed.HasFocus() && progressAfter > progressBefore &&
              refreshed.TooltipText.Contains(_main.UiResearchHorizon.Single(node => node.Id == researchId).Detail, StringComparison.Ordinal),
            "player-expedition-active-research-control-retains-focus-and-refreshes-progress");

        await ClickControlAsync(refreshed);
        await WaitForRefreshAsync();
        var paused = _main.UiResearchHorizon.Single(node => node.Id == researchId);
        var pausedProgress = paused.Progress;
        Check(paused.CanResume && !paused.CanPause && Math.Abs(_main.UiCreditFlow.ResearchOperationsPerDay) < .000001,
            "player-expedition-research-pointer-pause-halts-canonical-spend");
        await WaitForRefreshAsync();
        Check(Math.Abs(_main.UiResearchHorizon.Single(node => node.Id == researchId).Progress - pausedProgress) < .000001,
            "player-expedition-paused-research-progress-remains-stable");

        var resume = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Name == "ResearchNode_" + researchId);
        await ClickControlAsync(resume);
        await WaitForRefreshAsync();
        var resumed = _main.UiResearchHorizon.Single(node => node.Id == researchId);
        Check(resumed.CanPause && !resumed.CanResume && Math.Abs(_main.UiCreditFlow.ResearchOperationsPerDay) > .000001 &&
              resumed.Progress > pausedProgress,
            "player-expedition-research-pointer-resume-restores-canonical-progress-and-spend");
    }

    private async Task SelectFleetByMarkerAsync(int fleetId)
    {
        var point = await BringFleetMarkerIntoMapBoundsAsync(fleetId);
        await ClickPositionAsync(point, MouseButton.Left);
        await WaitForRefreshAsync();
        Require(_main.UiSelectedFleetId == fleetId, "Visible fleet-marker click did not select the requested owned ship.");
    }

    private int FindRoundTripPublicStar(
        UiOwnedFleetSnapshot scout,
        UiOwnedFleetSnapshot science,
        bool requireUnknown)
    {
        var mapBounds = new Rect2(100, 150, 780, 470);
        var publicCandidates = _main.UiSpatialCatalog
            .Where(entry => !requireUnknown || entry.SurveyLevel.ToString() == "Unknown")
            .Select(entry => new
            {
                entry.SystemId,
                ScoutReach = _main.UiGetFleetRouteAssessment(scout.FleetId, entry.SystemId),
                ScienceReach = _main.UiGetFleetRouteAssessment(science.FleetId, entry.SystemId),
            })
            .ToArray();
        var bothReachable = publicCandidates.Where(entry => entry.ScoutReach.ReachSupported && entry.ScienceReach.ReachSupported).ToArray();
        var candidate = bothReachable
            .Where(entry =>
                            entry.ScoutReach.DistanceLy > .001 && entry.ScienceReach.DistanceLy > .001 &&
                            entry.ScoutReach.DistanceLy * 2 <= scout.FuelRemainingLightYears + .000001 &&
                            entry.ScienceReach.DistanceLy * 2 <= science.FuelRemainingLightYears + .000001)
            .OrderBy(entry => Math.Max(entry.ScoutReach.DistanceLy, entry.ScienceReach.DistanceLy))
            .ThenBy(entry => entry.SystemId)
            .FirstOrDefault();
        if (candidate is null)
            throw new InvalidOperationException(
                $"No public stellar target preserves the current roundtrip reserve for both survey ships. " +
                $"public={publicCandidates.Length}, bothReachable={bothReachable.Length}, roundtripSafe=0, " +
                $"scoutFuel={scout.FuelRemainingLightYears:0.#}/{scout.FuelCapacityLightYears:0.#}, " +
                $"scienceFuel={science.FuelRemainingLightYears:0.#}/{science.FuelCapacityLightYears:0.#}.");
        return candidate.SystemId;
    }

    private static readonly Rect2 RegionalMapBounds = new(100, 150, 780, 470);

    private async Task<Vector2> BringPublicStarIntoMapBoundsAsync(int systemId) =>
        await BringPointIntoMapBoundsAsync(() => _main.UiGetCatalogScreenPosition(systemId), $"Public catalog target {systemId}");

    private async Task<Vector2> BringFleetMarkerIntoMapBoundsAsync(int fleetId) =>
        await BringPointIntoMapBoundsAsync(() => _main.UiGetFleetScreenPosition(fleetId), $"Player ship marker {fleetId}");

    private async Task<Vector2> BringPointIntoMapBoundsAsync(Func<Vector2?> pointReader, string description)
    {
        var center = RegionalMapBounds.GetCenter();
        for (var attempt = 0; attempt < 6; attempt++)
        {
            var point = pointReader();
            if (point.HasValue && RegionalMapBounds.HasPoint(point.Value)) return point.Value;
            if (!point.HasValue)
                throw new InvalidOperationException($"{description} has no screen coordinate while preparing real map navigation.");
            var delta = (center - point.Value).LimitLength(360);
            var end = (center + delta).Clamp(RegionalMapBounds.Position + new Vector2(12, 12), RegionalMapBounds.End - new Vector2(12, 12));
            await DragAsync(center, end, MouseButton.Middle);
            await WaitFramesAsync(2);
        }
        throw new InvalidOperationException($"{description} remained off-screen after 6 real middle-mouse pans.");
    }

    private async Task ReturnSurveyFleetForRefuelingAsync(int fleetId, int refuelingSystemId, string fleetName)
    {
        await ClickButtonAsync(_dock, "Home");
        var fleet = _main.UiOwnedFleets.Single(item => item.FleetId == fleetId);
        var returnReach = _main.UiGetFleetRouteAssessment(fleetId, refuelingSystemId);
        Require(returnReach.ReachSupported,
            $"The {fleetName} cannot complete its reserved return to the owned refueling colony: {returnReach.Reason}");
        var outboundFuelUsed = fleet.FuelCapacityLightYears - fleet.FuelRemainingLightYears;
        Require(returnReach.DistanceLy > .001 && outboundFuelUsed > .001 &&
                Math.Abs(outboundFuelUsed - returnReach.DistanceLy) < .001,
            $"The {fleetName} return did not expose exact nonzero outbound fuel use: " +
            $"used={outboundFuelUsed:0.###}, returnDistance={returnReach.DistanceLy:0.###}.");
        await SelectFleetByMarkerAsync(fleetId);
        await ClickPositionAsync(await BringPublicStarIntoMapBoundsAsync(refuelingSystemId), MouseButton.Right);
        await RequireRouteStartedAsync(fleetId, refuelingSystemId, fleetName + " refueling return right-click");
        await WaitForFleetAtSelectedSystemAsync(fleetId, refuelingSystemId);
        fleet = _main.UiOwnedFleets.Single(item => item.FleetId == fleetId);
        Require(Math.Abs(fleet.FuelRemainingLightYears - fleet.FuelCapacityLightYears) < .000001,
            $"The {fleetName} arrived at the owned colony without receiving canonical full refueling service.");
    }

    private async Task WaitForSurveyLevelAsync(int systemId, string level, string phase)
    {
        await WaitForPlayerConditionAsync(() => _main.UiSpatialCatalog.Any(entry => entry.SystemId == systemId && entry.SurveyLevel.ToString() == level),
            phase + " did not complete");
    }

    private async Task WaitForFleetAtSelectedSystemAsync(int fleetId, int systemId)
    {
        await WaitForPlayerConditionAsync(() => _main.UiOwnedFleets.Any(fleet => fleet.FleetId == fleetId &&
            fleet.CurrentSystemId == systemId && fleet.DestinationSystemId is null && fleet.RemainingRouteLegs == 0 &&
            fleet.RemainingRouteDistanceLightYears < .0001), "Fleet did not finish its canonical route to the selected system");
    }

    private async Task WaitForColonyAndPauseAsync(int colonyCountBefore, int bodyId, int fleetId)
    {
        while (WithinPlayerExpeditionBudget())
        {
            if (_main.UiOwnedColonies.Length > colonyCountBefore &&
                _main.UiOwnedColonies.Any(colony => colony.BodyId == bodyId && colony.PopulationMillions > 0) &&
                !_main.UiOwnedFleets.Any(fleet => fleet.FleetId == fleetId))
            {
                if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
                return;
            }
            await WaitFramesAsync(1);
        }
        throw new InvalidOperationException("Timed settlement did not found the authorized ordinary Player colony before the shared journey deadline.");
    }

    private async Task SavePlayerSettlementAuthorizationAsync(int fleetId, int bodyId, double embarkedPopulation)
    {
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var path = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        var saved = FindConstructionGalaxy(JsonNode.Parse(File.ReadAllText(path)))
            ?? throw new InvalidOperationException("Ordinary Player authorization save has no galaxy payload.");
        var fleet = saved["Fleets"]!.AsArray().Single(item => item!["Id"]!.GetValue<int>() == fleetId)!.AsObject();
        Check(fleet["DestinationPlanetaryBodyId"]!.GetValue<int>() == bodyId &&
              fleet["SettlementBodyId"] is null && fleet["SettlementDaysCompleted"]!.GetValue<double>() == 0 &&
              Math.Abs(fleet["EmbarkedPopulationMillions"]!.GetValue<double>() - embarkedPopulation) < .000001,
            "player-expedition-authorization-save-preserves-ship-id-target-and-embarked-people");
        var evidenceFile = "player-expedition-authorization-save.json";
        var evidencePath = Path.Combine(_outputDirectory, evidenceFile);
        File.Copy(path, evidencePath, overwrite: true);
        await CloseDrawerAsync();
        Require(_main.UiIsPaused, "Settlement authorization evidence resumed the clock before capture.");
        _playerAuthorizationSimulationDays = _main.UiSimulationDays;
        _playerAuthorizationSaveEvidence = new { fleet_id = fleetId, body_id = bodyId,
            embarked_population_millions = embarkedPopulation, simulation_days = _main.UiSimulationDays,
            file = evidenceFile, save_bytes = new FileInfo(evidencePath).Length,
            save_sha256 = HashFile(evidencePath), captured_while_paused = true };
        await SaveViewportAsync("player-expedition-03-settlement-authorized.png");
        await SelectMaximumPlayerSpeedAsync();
    }

    private async Task WaitForPlayerConditionAsync(Func<bool> predicate, string failure)
    {
        while (WithinPlayerExpeditionBudget())
        {
            if (predicate()) return;
            await WaitForRefreshAsync();
        }
        throw new InvalidOperationException(failure + $" before the shared journey deadline. status='{_main.UiStatusMessage}', date='{_main.UiDashboard.Date}', speed='{_main.UiSpeedLabel}'.");
    }

    private bool WithinPlayerExpeditionBudget() => _playerExpeditionStopwatch?.Elapsed < TimeSpan.FromMinutes(22);

    private async Task RequireRouteStartedAsync(int fleetId, int destinationSystemId, string action)
    {
        await WaitFramesAsync(1);
        var fleet = _main.UiOwnedFleets.SingleOrDefault(item => item.FleetId == fleetId);
        var traveling = fleet?.DestinationSystemId == destinationSystemId &&
                        (fleet.RemainingRouteLegs > 0 || fleet.RemainingRouteDistanceLightYears > .0001);
        var alreadyArrived = fleet?.CurrentSystemId == destinationSystemId && fleet.DestinationSystemId is null &&
                             fleet.RemainingRouteLegs == 0 && fleet.RemainingRouteDistanceLightYears < .0001;
        Require(fleet is not null && (traveling || alreadyArrived),
            $"{action} was not accepted: fleet={fleetId}, expectedDestination={destinationSystemId}, actualDestination={fleet?.DestinationSystemId}, route={fleet?.RemainingRouteLegs}/{fleet?.RemainingRouteDistanceLightYears:0.###}, status='{_main.UiStatusMessage}'.");
    }

    private async Task VerifyPlayerExpeditionSaveReloadAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        var coloniesBefore = _main.UiOwnedColonies.Select(colony => (colony.ColonyId, colony.BodyId, colony.PopulationMillions)).OrderBy(value => value.ColonyId).ToArray();
        var shipsBefore = _main.UiOwnedFleets.Select(fleet => (fleet.FleetId, fleet.Role, fleet.DesignId)).OrderBy(value => value.FleetId).ToArray();
        var savedDay = _main.UiSimulationDays;
        var savedSeed = _main.UiCampaignSeed;
        var applicationRevisionBefore = _main.UiCampaignApplicationRevision;
        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        await SelectMaximumPlayerSpeedAsync();
        await WaitForPlayerConditionAsync(() => _main.UiSimulationDays > savedDay + .01,
            "Visible post-save play did not create an unsaved time difference for reload proof");
        var unsavedAdvancedDay = _main.UiSimulationDays;
        await OpenCampaignMenuAsync();
        await LoadCurrentCampaignThroughMenuAsync(menu, dialog);
        var coloniesAfter = _main.UiOwnedColonies.Select(colony => (colony.ColonyId, colony.BodyId, colony.PopulationMillions)).OrderBy(value => value.ColonyId).ToArray();
        var shipsAfter = _main.UiOwnedFleets.Select(fleet => (fleet.FleetId, fleet.Role, fleet.DesignId)).OrderBy(value => value.FleetId).ToArray();
        var restoredDay = _main.UiSimulationDays;
        var applicationRevisionAfter = _main.UiCampaignApplicationRevision;
        Check(coloniesBefore.SequenceEqual(coloniesAfter) && shipsBefore.SequenceEqual(shipsAfter) && coloniesAfter.Length > 1 &&
              _main.UiCampaignSeed == savedSeed && unsavedAdvancedDay > savedDay &&
              Math.Abs(restoredDay - savedDay) < .000001 && applicationRevisionAfter > applicationRevisionBefore && _main.UiIsPaused,
            "player-expedition-real-load-restores-saved-day-people-ships-and-campaign-instance");
        _playerReloadEvidence = new
        {
            seed = savedSeed,
            saved_simulation_days = savedDay,
            unsaved_advanced_simulation_days = unsavedAdvancedDay,
            restored_simulation_days = restoredDay,
            application_revision_before = applicationRevisionBefore,
            application_revision_after = applicationRevisionAfter,
            restored_paused = _main.UiIsPaused,
        };
    }

    private async Task LoadCurrentCampaignThroughMenuAsync(MainMenuLayer menu, ConfirmationDialog dialog, bool captureConfirmation = false)
    {
        Require(_main.UiIsMenuOpen, "Saved-campaign Load requires the blocking campaign menu.");
        // Player and Developer saves have separate visible menu controls. Both routes
        // converge on the same confirmation and disk-backed UiLoadCurrentCampaign action.
        await ClickNamedButtonAsync(menu, _main.UiIsDeveloperMode ? "ModeDeveloper" : "LoadCampaign");
        Require(dialog.Visible, "Saved-campaign Load did not request confirmation before discarding unsaved changes.");
        Require(menu.UiCampaignConfirmationVisible && menu.UiCampaignConfirmationTitle == "LOAD SAVED CAMPAIGN?" &&
                menu.UiCampaignConfirmationAcceptText == "LOAD CAMPAIGN" &&
                menu.UiCampaignConfirmationCancelText == "KEEP CURRENT CAMPAIGN",
            $"Saved-campaign Load showed the wrong rendered confirmation: visible={menu.UiCampaignConfirmationVisible}, title='{menu.UiCampaignConfirmationTitle}', accept='{menu.UiCampaignConfirmationAcceptText}', cancel='{menu.UiCampaignConfirmationCancelText}'.");
        if (captureConfirmation) await SaveViewportAsync("player-expedition-load-confirmation.png");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForCampaignLoadingAsync();
        Require(!_main.UiIsMenuOpen && _main.UiIsPaused,
            "Saved-campaign Load did not return to the replaced campaign in a paused state.");
    }

    private void WritePlayerExpeditionEvidenceManifest()
    {
        var evidence = new
        {
            schema_version = 3,
            git_sha = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_SHA") ?? "unknown",
            seed = "20260908",
            system_count = _main.UiDashboard.TotalSystemCount,
            player_mode = !_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed,
            simulation_days = _main.UiSimulationDays,
            campaign_date = _main.UiDashboard.Date,
            elapsed_wall_seconds = _playerExpeditionStopwatch?.Elapsed.TotalSeconds ?? 0,
            scope = "focused ordinary Player Sandbox opening; no Developer mode, grants, direct state mutation, finish-orders, or hidden-knowledge commands",
            input_mode = "Input.ParseInputEvent",
            mouse_actions = _mouseActions,
            authorization_save = _playerAuthorizationSaveEvidence,
            settlement = _playerSettlementEvidence,
            reload = _playerReloadEvidence,
            captures = _captureRecords,
            checks = _checks,
        };
        File.WriteAllText(Path.Combine(_outputDirectory, "player-expedition-manifest.json"),
            JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
    }
}
