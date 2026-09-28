using System;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Campaign;
using Game.Diagnostics;
using Game.Persistence;
using Game.Simulation.Models;
using Game.Simulation.Time;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Territory;

namespace Game.Presentation;

/// <summary>
/// Godot-facing adapter for the plain-C# campaign lifecycle. Presentation reset/logging stays
/// here; deterministic generation, load recovery and persistence dispatch stay in CampaignSessionService.
/// </summary>
public partial class Main
{
    private readonly CampaignSessionService _campaignSessionService = new();
    private CampaignAutosaveScheduler _autosaveScheduler = new();
    private bool _preserveRecoveredBackupOnNextSave;
    private PendingScheduledAutosave? _pendingScheduledAutosave;
    private long? _integratedStartupSeed;
    public ulong UiCampaignApplicationRevision { get; private set; }

    private sealed record PendingScheduledAutosave(
        Task<CampaignSaveWriteMetrics> WriteTask,
        double SimulationDays,
        string Path,
        ulong SessionRevision,
        bool PreserveExistingBackup,
        CampaignSaveCaptureMetrics CaptureMetrics);
    public long UiCampaignSeed => _galaxy.Seed;
    public string UiHomePlanetSampleIdentity
    {
        get
        {
            var homeSystemId = PlayerCivilization.HomeSystemId;
            var body = _galaxy.PlanetaryBodies.FirstOrDefault(candidate => candidate.SystemId == homeSystemId);
            return body is null ? string.Empty : $"{body.Id}:{body.Name}";
        }
    }

    protected void RunIntegratedCampaignReady(bool suppressStartupPersistence = false)
    {
        GetTree().AutoAcceptQuit = false;
        PrepareIntegratedStartupAttempt();
        var fallbackSeed = _integratedStartupSeed!.Value;
        _font = ThemeDB.FallbackFont;
        SupportLogger.Initialize();

        var initialSettings = Game.Simulation.Generation.GalaxyGenerationMetadata.FullGalaxy500(
            fallbackSeed.ToString(System.Globalization.CultureInfo.InvariantCulture), fallbackSeed).ToSettings();
        var bootstrap = _campaignSessionService.LoadOrCreate(AutosavePath, fallbackSeed, initialSettings);
        _integratedStartupSeed = bootstrap.Galaxy.Seed;
        ApplyIntegratedCampaign(bootstrap);

        switch (bootstrap.Source)
        {
            case CampaignBootstrapSource.LoadedSave:
                SetStatus($"Loaded autosave from {bootstrap.SavedAtUtc?.LocalDateTime:g}");
                SupportLogger.Log(
                    "save",
                    $"Loaded autosave seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(_clock.SimulationDays)} stage={PlayerCivilization.DevelopmentStage} format={CampaignStatePersistenceService.CurrentFormatVersion}");
                break;

            case CampaignBootstrapSource.RecoveredFromBackup:
                if (!string.IsNullOrWhiteSpace(bootstrap.LoadFailure))
                    SupportLogger.Log("save-recovery", bootstrap.LoadFailure);
                SupportLogger.Log(
                    "save-recovery",
                    $"Recovered backup autosave seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(_clock.SimulationDays)} savedAt={bootstrap.SavedAtUtc?.LocalDateTime:g} format={CampaignStatePersistenceService.CurrentFormatVersion}");
                // Replace a missing/corrupt primary promptly, without retrying on every frame.
                // The first successful repair save preserves the known-good .bak rather than
                // rotating a corrupt primary over it; normal rotation resumes after repair.
                _autosaveScheduler.MarkFailure(_clock.SimulationDays);
                SetStatus("Primary autosave was unavailable; recovered the previous backup. A fresh autosave is scheduled after 1 simulation day.", 8.0);
                break;

            case CampaignBootstrapSource.RecoveredFromInvalidSave:
                SupportLogger.Log("save-error", bootstrap.LoadFailure ?? "Unknown autosave load failure.");
                LogIntegratedCampaignStartup("recovery");
                if (!suppressStartupPersistence && TryPersistIntegratedCampaign(
                    logCategory: "save-recovery-checkpoint",
                    showSuccessStatus: false,
                    failureStatus: "Recovered campaign checkpoint failed; retry scheduled after 1 simulation day. See logs."))
                {
                    SetStatus("Autosave and backup could not be loaded; generated and checkpointed a new 2050 campaign.");
                }
                break;

            default:
                LogIntegratedCampaignStartup("startup");
                if (!suppressStartupPersistence)
                    TryPersistIntegratedCampaign(
                        logCategory: "save-initial",
                        showSuccessStatus: false,
                        failureStatus: "Initial campaign checkpoint failed; retry scheduled after 1 simulation day. See logs.");
                break;
        }

        QueueRedraw();
    }

    protected string BuildIntegratedStartupFailureDiagnostic(Exception exception) =>
        StartupInitializationFailure.BuildDiagnostic(
            exception,
            _integratedStartupSeed,
            ResolveStartupPath(() => AutosavePath),
            ResolveStartupPath(() => DeveloperSavePath),
            UiIsDeveloperMode ? "Developer" : "Player");

    protected void PrepareIntegratedStartupAttempt() =>
        _integratedStartupSeed ??= DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

    private static string ResolveStartupPath(Func<string> resolve)
    {
        try { return resolve(); }
        catch (Exception exception) { return $"unavailable ({exception.GetType().Name}: {exception.Message})"; }
    }

    protected void CreateIntegratedNewCampaign(
        string? enteredSeed = null,
        string playerSpeciesId = Game.Simulation.Species.SpeciesCatalog.TerranBaselineId)
    {
        var seedText = string.IsNullOrWhiteSpace(enteredSeed)
            ? Game.Simulation.Generation.CampaignSeed.CreateRandomNumericText()
            : enteredSeed.Trim();
        var bootstrap = _campaignSessionService.CreateNew(seedText, playerSpeciesId);
        CommitIntegratedNewCampaign(bootstrap, seedText);
    }

    public Task<CampaignBootstrapResult> UiPrepareNewCampaignAsync(string enteredSeed, string playerSpeciesId,
        Action<Game.Simulation.Generation.GalaxyGenerationProgress> progress) =>
        Task.Run(() => _campaignSessionService.CreateNew(enteredSeed, playerSpeciesId, progress));

    /// <summary>
    /// Prepares an explicitly configured Player campaign off the UI thread. The metadata record is
    /// captured by the menu before confirmation, so a later menu edit cannot affect this run.
    /// </summary>
    public Task<CampaignBootstrapResult> UiPrepareNewCampaignAsync(
        Game.Simulation.Generation.GalaxyGenerationMetadata metadata,
        Action<Game.Simulation.Generation.GalaxyGenerationProgress> progress) =>
        Task.Run(() => _campaignSessionService.CreateNew(metadata, progress));

    public bool UiCommitPreparedNewCampaign(CampaignBootstrapResult bootstrap, string enteredSeed)
    {
        _ = CommitIntegratedNewCampaign(bootstrap, enteredSeed.Trim());
        return true;
    }

    private bool CommitIntegratedNewCampaign(CampaignBootstrapResult bootstrap, string seedText)
    {
        ApplyIntegratedCampaign(bootstrap);
        _clock.SetSpeed(Game.Simulation.SimulationClock.SpeedLevel.Normal);
        LogIntegratedCampaignStartup("startup");

        var checkpointSaved = TryPersistIntegratedCampaign(
            logCategory: "save-new-game",
            showSuccessStatus: false,
            failureStatus: "New campaign checkpoint failed; retry scheduled after 1 simulation day. See logs.");
        if (checkpointSaved)
        {
            SetStatus($"Generated a new {_galaxy.Systems.Count:N0}-system campaign beginning January 1, 2050. Seed: {seedText}");
        }

        QueueRedraw();
        return checkpointSaved;
    }

    protected void SaveIntegratedCampaign()
    {
        TryPersistIntegratedCampaign(
            logCategory: "save",
            showSuccessStatus: true,
            failureStatus: "Autosave failed. See logs.");
        QueueRedraw();
    }

    protected void RunIntegratedScheduledAutosave()
    {
        if (_galaxy is null)
            return;

        CompletePendingScheduledAutosave(waitForCompletion: false);
        if (_pendingScheduledAutosave is not null)
            return;

        var simulationDays = _clock.SimulationDays;
        if (!_autosaveScheduler.IsDue(simulationDays))
            return;

        var preserveExistingBackup = _preserveRecoveredBackupOnNextSave;
        var path = CurrentCampaignSavePath;
        try
        {
            PreparedCampaignSave prepared;
            Func<CampaignSaveWriteMetrics> write;
            if (UiIsDeveloperMode)
            {
                prepared = _developerPersistence.PrepareSave(
                    path, _galaxy, simulationDays, _diplomacyState, _adaptiveResearch!);
                write = () => _developerPersistence.WritePrepared(path, prepared, preserveExistingBackup);
            }
            else
            {
                prepared = _campaignSessionService.PrepareSave(
                    _galaxy, _diplomacyState, _adaptiveResearch!, simulationDays);
                write = () => _campaignSessionService.WritePreparedSave(path, prepared, preserveExistingBackup);
            }

            var writeTask = Task.Run(write);
            _pendingScheduledAutosave = new PendingScheduledAutosave(
                writeTask, simulationDays, path, UiCampaignApplicationRevision,
                preserveExistingBackup, prepared.CaptureMetrics);
        }
        catch (Exception ex)
        {
            ReportPersistenceFailure(ex, simulationDays,
                "Autosave failed; retry scheduled after 1 simulation day. See logs.");
        }
    }

    protected void HandleIntegratedCloseRequest()
    {
        if (_integratedExitRequested) return;
        _integratedExitRequested = true;
        if (_galaxy is not null)
        {
            if (!TryPersistIntegratedCampaign(
                logCategory: "save-exit",
                showSuccessStatus: false,
                failureStatus: "Exit cancelled because saving failed. Your campaign is still open; retry Save or export a support bundle."))
            {
                _integratedExitRequested = false;
                return;
            }
        }

        GD.Print($"STELLAR_EXIT_TO_WINDOWS_SAVE_CONFIRMED path={CurrentCampaignSavePath}");
        UiVoice?.Stop();
        _ = AudioDirector.ShutdownAndQuitAsync(GetTree());
    }

    private bool TryPersistIntegratedCampaign(
        string logCategory,
        bool showSuccessStatus,
        string failureStatus)
    {
        DrainPendingScheduledAutosave();
        var simulationDays = _clock.SimulationDays;
        var preserveRecoveredBackup = _preserveRecoveredBackupOnNextSave;
        var saveStarted = System.Diagnostics.Stopwatch.GetTimestamp();
        try
        {
            if (UiIsDeveloperMode)
            {
                _developerPersistence.Save(CurrentCampaignSavePath, _galaxy, simulationDays,
                    _diplomacyState, _adaptiveResearch!, preserveRecoveredBackup);
            }
            else if (preserveRecoveredBackup)
            {
                _campaignSessionService.SavePreservingBackup(
                    CurrentCampaignSavePath,
                    _galaxy,
                    _diplomacyState,
                    _adaptiveResearch!,
                    simulationDays);
            }
            else
            {
                _campaignSessionService.Save(CurrentCampaignSavePath, _galaxy, _diplomacyState, _adaptiveResearch!, simulationDays);
            }

            _preserveRecoveredBackupOnNextSave = false;
            _autosaveScheduler.MarkSuccess(simulationDays);
            GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.ClearSaveFailure();
            SupportLogger.Log(
                logCategory,
                $"Autosaved seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(simulationDays)} format={CampaignStatePersistenceService.CurrentFormatVersion} nextAutoDay={_autosaveScheduler.NextDueDay:0.###} preservedRecoveredBackup={preserveRecoveredBackup} saveMs={System.Diagnostics.Stopwatch.GetElapsedTime(saveStarted).TotalMilliseconds:0.00}");

            if (showSuccessStatus)
                SetStatus("Autosave complete.");
            return true;
        }
        catch (Exception ex)
        {
            ReportPersistenceFailure(ex, simulationDays, failureStatus);
            return false;
        }
    }

    private void DrainPendingScheduledAutosave() => CompletePendingScheduledAutosave(waitForCompletion: true);

    private void CompletePendingScheduledAutosave(bool waitForCompletion)
    {
        var pending = _pendingScheduledAutosave;
        if (pending is null || (!waitForCompletion && !pending.WriteTask.IsCompleted))
            return;

        try
        {
            var writeMetrics = pending.WriteTask.GetAwaiter().GetResult();
            if (pending.SessionRevision != UiCampaignApplicationRevision ||
                !string.Equals(pending.Path, CurrentCampaignSavePath, StringComparison.Ordinal))
                throw new InvalidOperationException("A scheduled autosave completed after its campaign session was replaced.");

            _preserveRecoveredBackupOnNextSave = false;
            _autosaveScheduler.MarkSuccess(_clock.SimulationDays);
            GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.ClearSaveFailure();
            SupportLogger.Log(
                "autosave",
                $"Autosaved seed={_galaxy.Seed} date={CampaignCalendar.FormatDate(pending.SimulationDays)} format={CampaignStatePersistenceService.CurrentFormatVersion} nextAutoDay={_autosaveScheduler.NextDueDay:0.###} preservedRecoveredBackup={pending.PreserveExistingBackup} " +
                $"captureMs={pending.CaptureMetrics.TotalMilliseconds:0.00} diplomacyMs={pending.CaptureMetrics.DiplomacyMilliseconds:0.00} galaxyValidationMs={pending.CaptureMetrics.GalaxyValidationMilliseconds:0.00} galaxyDtoMs={pending.CaptureMetrics.GalaxyDtoMilliseconds:0.00} adaptiveMs={pending.CaptureMetrics.AdaptiveResearchMilliseconds:0.00} " +
                $"jsonMs={writeMetrics.JsonMilliseconds:0.00} atomicWriteMs={writeMetrics.AtomicWriteMilliseconds:0.00}");
        }
        catch (Exception ex)
        {
            ReportPersistenceFailure(ex, _clock.SimulationDays,
                "Autosave failed; retry scheduled after 1 simulation day. See logs.");
        }
        finally
        {
            _pendingScheduledAutosave = null;
        }
    }

    private void ReportPersistenceFailure(Exception exception, double simulationDays, string failureStatus)
    {
        // Failed repair saves retain the flag so later manual/exit/retry saves still protect
        // the known-good backup instead of rotating a bad primary over it.
        _autosaveScheduler.MarkFailure(simulationDays);
        SupportLogger.Log("save-error", exception.ToString());
        SetStatus(failureStatus, 8.0);
        GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.ShowSaveFailure(failureStatus);
    }

    private void ApplyIntegratedCampaign(CampaignBootstrapResult bootstrap)
    {
        DrainPendingScheduledAutosave();
        _galaxy = bootstrap.Galaxy;
        _diplomacyState = bootstrap.Diplomacy;
        _adaptiveResearch = bootstrap.AdaptiveResearch;
        AdaptiveResearchCampaignProgression.SynchronizeDevelopmentStages(_galaxy, _adaptiveResearch);
        // Territory is a derived campaign cache. Initialize it once at the bootstrap boundary;
        // map and inspection readers deliberately only Peek at this snapshot.
        TerritorialRuntime.Initialize(_galaxy);
        if (_diplomacyState is not null) TerritorialDiplomacyBridge.Bind(_galaxy, _diplomacyState);
        _clock.Restore(bootstrap.SimulationDays);
        ResetMassiveCombatHostForCampaign();
        _autosaveScheduler = UiIsDeveloperMode ? PlayableDemoScenario.CreateAutosaveScheduler() : new CampaignAutosaveScheduler();
        _autosaveScheduler.Reset(_clock.SimulationDays);
        _preserveRecoveredBackupOnNextSave = bootstrap.Source == CampaignBootstrapSource.RecoveredFromBackup;
        RebuildIntegratedCoreSimulation();
        ResetIntegratedCampaignPresentation();
        UiCampaignApplicationRevision++;
        _voiceOpening = bootstrap.Source is not (CampaignBootstrapSource.LoadedSave or CampaignBootstrapSource.RecoveredFromBackup);
    }

    private void ResetIntegratedCampaignPresentation()
    {
        UiTerritoryMapVisible = true;
        _playerNotifications.Clear();
        _returnConfirmation = null;
        ResetVoicePresentation();
        GetNodeOrNull<DeveloperToolsLayer>("DeveloperToolsLayer")?.Close();
        UiReturnToOrbit();
        ReturnToStellarView(announce: false);
        _selectedSystemId = -1;
        _researchCandidateIndex = 0;
        _constructionCandidateIndex = 0;
        _shipDesignCandidateIndex = 0;
        _pan = Godot.Vector2.Zero;
        // A replacement campaign must open at the playable regional scale. Keeping the
        // previous overview threshold here left the First Light guide hidden and made a
        // new campaign appear to resume an unrelated camera state.
        _zoom = Spatial.SpatialNavigationLayout.StellarRegionScale;
        var home = _galaxy.Systems.First(system => system.Id == PlayerCivilization.HomeSystemId);
        _pan = -new Godot.Vector2(home.Position.X, home.Position.Y) * (_zoom * UiCatalogVisualCoordinateScale);
        SynchronizeRegionalCamera();
        var midpoint = GetViewportRect().Size * .5f;
        _regionalCamera.Snap(_zoom,
            midpoint.X - (double)home.Position.X * UiCatalogVisualCoordinateScale * _zoom,
            midpoint.Y - (double)home.Position.Y * UiCatalogVisualCoordinateScale * _zoom);

        foreach (var marker in _scienceFleetMarkers.Values)
            marker.QueueFree();
        _scienceFleetMarkers.Clear();
    }

    private void LogIntegratedCampaignStartup(string category)
    {
        SupportLogger.Log(
            category,
            $"Generated 2050 campaign seed={_galaxy.Seed} systems={_galaxy.Systems.Count} prewarp={_galaxy.Civilizations.Count(c => c.DevelopmentStage == CivilizationDevelopmentStage.PreWarp)} ancient={_galaxy.Civilizations.Count(c => c.IsSeededAncient)} player={PlayerCivilization.Name}");
    }
}
