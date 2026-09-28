using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Game.Campaign;
using Game.Diagnostics;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Research.Adaptive;

namespace Game.Presentation;

public sealed record UiDeveloperCommand(string Id, string Title, string Description);
public sealed record UiDeveloperCommandResult(bool Accepted, string Message);

public partial class Main
{
    private readonly DeveloperCampaignSessionService _developerSessions = new();
    private readonly DeveloperCampaignPersistenceService _developerPersistence = new();
    private string DeveloperSavePath => Path.Combine(Path.GetDirectoryName(AutosavePath)!, DeveloperCampaignSessionService.SaveFileName);
    public bool UiIsDeveloperMode => _galaxy?.DeveloperSession is not null;
    public string UiModeLabel => UiIsDeveloperMode ? "Developer mode" : "Player mode";
    public bool UiDeveloperToolsUsed => _galaxy?.DeveloperSession?.ToolsUsed == true;
    public bool UiHasPlayerSave => File.Exists(AutosavePath) || File.Exists(AutosavePath + ".bak");
    public bool UiHasDeveloperSave => File.Exists(DeveloperSavePath) || File.Exists(DeveloperSavePath + ".bak") || UiHasDemoSave;
    public bool UiIsDeveloperToolsOpen => GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.IsOpen == true;
    public IReadOnlyList<UiDeveloperCommand> UiDeveloperCommands { get; } = DeveloperCommandService.Commands
        .Select(command => new UiDeveloperCommand(command.Id, command.Title, command.Description)).ToArray();

    protected void InitializeDeveloperTools()
    {
        if (GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer") is null)
            AddChild(new DeveloperToolsLayer { Name = "DeveloperToolsLayer" });
    }

    public void UiOpenDeveloperTools()
    {
        if (!UiIsDeveloperMode || UiIsMenuOpen) return;
        InitializeDeveloperTools();
        GetNode<DeveloperToolsLayer>("DeveloperToolsLayer").Open();
    }

    public bool UiSwitchToPlayerMode()
    {
        if (!UiIsDeveloperMode) return true;
        if (!UiCheckpointBeforeCampaignSwitch()) return false;
        GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.Close();
        var bootstrap = _campaignSessionService.LoadOrCreate(AutosavePath, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
        ApplyIntegratedCampaign(bootstrap);
        _clock.SetSpeed(SimulationClock.SpeedLevel.Normal);
        CheckpointModeSwitch(bootstrap);
        return true;
    }

    public bool UiSwitchToDeveloperMode()
    {
        if (UiIsDeveloperMode) return true;
        if (!UiCheckpointBeforeCampaignSwitch()) return false;
        var bootstrap = _developerSessions.LoadOrCreate(DeveloperSavePath, PlayableDemoScenario.Seed);
        ApplyIntegratedCampaign(bootstrap);
        _clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        CheckpointModeSwitch(bootstrap);
        return true;
    }

    public bool UiLoadCurrentCampaign()
    {
        DrainPendingScheduledAutosave();
        var developer = UiIsDeveloperMode;
        try
        {
            var bootstrap = developer
                ? _developerSessions.LoadExisting(DeveloperSavePath)
                : _campaignSessionService.LoadExisting(AutosavePath);
            return UiCommitPreparedCurrentCampaign(bootstrap, developer);
        }
        catch (Exception ex) { return HandleCurrentCampaignLoadFailure(ex, developer); }
    }

    public Task<CampaignBootstrapResult> UiPrepareCurrentCampaignAsync(
        Action<CampaignRestorationProgress> progress)
    {
        DrainPendingScheduledAutosave();
        var developer = UiIsDeveloperMode;
        var path = developer ? DeveloperSavePath : AutosavePath;
        return Task.Run(() => developer
            ? _developerSessions.LoadExisting(path, progress)
            : _campaignSessionService.LoadExisting(path, progress));
    }

    public bool UiCommitPreparedCurrentCampaign(CampaignBootstrapResult bootstrap, bool developer)
    {
        var previousGalaxy = _galaxy;
        var previousDiplomacy = _diplomacyState;
        var previousAdaptiveResearch = _adaptiveResearch;
        var previousSimulationDays = _clock.SimulationDays;
        var previousSpeed = _clock.Speed;
        var previousAutosaveScheduler = _autosaveScheduler;
        var previousPreserveRecoveredBackup = _preserveRecoveredBackupOnNextSave;
        var previousVoiceOpening = _voiceOpening;
        var previousApplicationRevision = UiCampaignApplicationRevision;
        var applyStarted = false;
        try
        {
            applyStarted = true;
            ApplyIntegratedCampaign(bootstrap);
        }
        catch (Exception ex)
        {
            try
            {
                if (applyStarted)
                {
                    _galaxy = previousGalaxy;
                    _diplomacyState = previousDiplomacy;
                    _adaptiveResearch = previousAdaptiveResearch;
                    _clock.Restore(previousSimulationDays);
                    _clock.SetSpeed(previousSpeed);
                    _autosaveScheduler = previousAutosaveScheduler;
                    _preserveRecoveredBackupOnNextSave = previousPreserveRecoveredBackup;
                    _voiceOpening = previousVoiceOpening;
                    UiCampaignApplicationRevision = previousApplicationRevision;
                    RebuildIntegratedCoreSimulation();
                    ResetIntegratedCampaignPresentation();
                }
            }
            catch (Exception rollbackFailure)
            {
                SupportLogger.Log("manual-load-rollback-error", rollbackFailure.ToString());
            }
            return HandleCurrentCampaignLoadFailure(ex, developer);
        }

        // Loading always returns paused with a safe ordinary resume speed. A Developer world's
        // remembered 24x rate must never leak through an explicit load or into a Player campaign.
        _clock.SetSpeed(SimulationClock.SpeedLevel.Normal);
        _clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        if (!string.IsNullOrWhiteSpace(bootstrap.LoadFailure))
            SupportLogger.Log("manual-load-recovery", bootstrap.LoadFailure);
        SupportLogger.Log("manual-load",
            $"Loaded {(developer ? "Developer" : "Player")} campaign seed={bootstrap.Seed} day={bootstrap.SimulationDays:0.###} source={bootstrap.Source} without modifying its save slot.");
        SetStatus(bootstrap.RecoveredFromBackup
            ? "Loaded the previous backup. The campaign is paused; save when ready to repair the primary."
            : "Saved campaign loaded and paused.", 10);
        QueueRedraw();
        return true;
    }

    private bool HandleCurrentCampaignLoadFailure(Exception ex, bool developer)
    {
        var mode = developer ? "Developer" : "Player";
        var message = ex is FileNotFoundException
            ? $"No saved {mode} campaign is available. Your current campaign is unchanged."
            : $"Neither the primary nor backup {mode} campaign could be loaded. Your current campaign is unchanged.";
        SupportLogger.Log("manual-load-error", ex.ToString());
        SetStatus(message, 10);
        GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.ShowSaveFailure(message);
        return false;
    }

    public bool UiHandlePreparedCurrentCampaignFailure(Exception exception, bool developer) =>
        HandleCurrentCampaignLoadFailure(exception, developer);

    public void UiCreateDeveloperCampaignConfirmed(long seed)
    {
        GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.Close();
        var bootstrap = _developerSessions.CreateNew(seed);
        ApplyIntegratedCampaign(bootstrap);
        _clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        CheckpointModeSwitch(bootstrap);
    }

    public Task<CampaignBootstrapResult> UiPrepareDeveloperCampaignAsync(long seed,
        Action<Game.Simulation.Generation.GalaxyGenerationProgress> progress) =>
        Task.Run(() => _developerSessions.CreateNew(seed, progress));

    public bool UiCommitPreparedDeveloperCampaign(CampaignBootstrapResult bootstrap)
    {
        GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.Close();
        ApplyIntegratedCampaign(bootstrap);
        _clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        CheckpointModeSwitch(bootstrap);
        return true;
    }

    private void CheckpointModeSwitch(CampaignBootstrapResult bootstrap)
    {
        if (!string.IsNullOrWhiteSpace(bootstrap.LoadFailure)) SupportLogger.Log("mode-load", bootstrap.LoadFailure);
        if (TryPersistIntegratedCampaign("mode-checkpoint", false, "Campaign opened, but its checkpoint failed. Retry Save; your other mode has its own save."))
        {
            var recovery = bootstrap.RecoveredFromBackup ? " Recovered the previous backup." :
                bootstrap.RecoveredFromInvalidSave ? " The save and backup could not be loaded; a fresh campaign was generated." : "";
            SetStatus(UiModeLabel + (UiIsDeveloperMode ? " · 24x. Developer tools run only when you choose them." : " · Ordinary gameplay rules.") + recovery, 10);
        }
        QueueRedraw();
    }

    public UiDeveloperCommandResult UiRunDeveloperCommand(string id)
    {
        if (_galaxy is null) return new(false, "No campaign is open.");
        if (UiIsMenuOpen) return new(false, "Close the campaign menu before running Developer tools.");
        try
        {
            var result = DeveloperCommandService.Execute(_galaxy, id, AdvanceDeveloperDays, _selectedSystemId >= 0 ? _selectedSystemId : null);
            if (!result.Accepted) return new(false, result.Message);
            if (id is "unlock_technology" or "unlock_research" && _adaptiveResearch is not null)
            {
                var adaptive = _adaptiveResearch.GetCivilization(_galaxy.PlayerCivilizationId);
                _adaptiveResearch.Runtime.Authority.AddCapability(adaptive, "orbital_industry");
                _adaptiveResearch.Runtime.Authority.AddCapability(adaptive, "spacecraft_construction");
                _adaptiveResearch.Runtime.Authority.AddCapability(adaptive, "experimental_interstellar_transit");
            }
            QueueRedraw();
            if (!TryPersistIntegratedCampaign("developer-command", false, "Developer changes are active, but saving failed. Retry Save before switching campaigns."))
                return new(true, result.Message + " Saving failed; retry Save before switching.");
            SetStatus(result.Message, 8);
            return new(true, result.Message);
        }
        catch (Exception ex)
        {
            SupportLogger.Log("developer-command-error", ex.ToString());
            return new(false, $"Developer action failed: {ex.GetType().Name}: {ex.Message}. Tools used remains marked; reload a checkpoint if needed. See the support log.");
        }
    }
}
