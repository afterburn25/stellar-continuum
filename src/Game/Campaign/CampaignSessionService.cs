using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Game.Persistence;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;

namespace Game.Campaign;

public enum CampaignBootstrapSource
{
    NewCampaign,
    LoadedSave,
    RecoveredFromBackup,
    RecoveredFromInvalidSave,
}

public sealed record CampaignBootstrapResult(
    GalaxyState Galaxy,
    DiplomacyState Diplomacy,
    AdaptiveResearchCampaignState AdaptiveResearch,
    double SimulationDays,
    CampaignBootstrapSource Source,
    string GameVersion,
    DateTimeOffset? SavedAtUtc,
    string? LoadFailure)
{
    public long Seed => Galaxy.Seed;
    public bool WasLoaded => Source is CampaignBootstrapSource.LoadedSave or CampaignBootstrapSource.RecoveredFromBackup;
    public bool RecoveredFromBackup => Source == CampaignBootstrapSource.RecoveredFromBackup;
    public bool RecoveredFromInvalidSave => Source == CampaignBootstrapSource.RecoveredFromInvalidSave;
}

/// <summary>
/// Plain-C# campaign lifecycle boundary. It composes deterministic generation and versioned
/// campaign persistence without depending on Godot. Galaxy state remains owned by its proven
/// serializer while the campaign wrapper owns Diplomacy and Adaptive Research beside it.
/// </summary>
public sealed class CampaignSessionService
{
    private readonly GalaxyGenerator _generator;
    private readonly CampaignStatePersistenceService _saveService;

    public CampaignSessionService(
        GalaxyGenerator? generator = null,
        CampaignStatePersistenceService? saveService = null)
    {
        _generator = generator ?? new GalaxyGenerator();
        _saveService = saveService ?? new CampaignStatePersistenceService();
    }

    public CampaignBootstrapResult CreateNew(long seed, GalaxyGenerationSettings? settings = null,
        Action<GalaxyGenerationProgress>? progress = null)
    {
        settings ??= new GalaxyGenerationSettings();
        var enteredSeed = seed.ToString(System.Globalization.CultureInfo.InvariantCulture);
        var localCatalog = settings.GalaxyShape == GalaxyShape.SolarNeighborhood;
        var fullGalaxy = settings.GalaxyShape == GalaxyShape.FullGalaxy;
        var metadata = localCatalog
            ? GalaxyGenerationMetadata.MilkyWay500(enteredSeed, seed, settings.PlayerSpeciesId)
            : fullGalaxy
            ? GalaxyGenerationMetadata.FullGalaxy500(enteredSeed, seed, settings.PlayerSpeciesId, settings.SystemCount)
            : GalaxyGenerationMetadata.Standard100(enteredSeed, seed, settings.PlayerSpeciesId);
        var galaxy = _generator.Generate(seed, settings, progress);
        galaxy.GenerationMetadata = metadata with
        {
            SystemCount = settings.SystemCount,
            GalaxyShape = localCatalog ? "Solar neighborhood" : fullGalaxy ? "Full galaxy" :
                settings.GalaxyShape == GalaxyShape.BarredSpiral ? "Barred spiral" : "Legacy disk",
            ArtProfileVersion = localCatalog ? "hyg-local-500-v1" : fullGalaxy ? "milky-way-full-500-v1" :
                settings.GalaxyShape == GalaxyShape.BarredSpiral ? "milky-way-barred-v1" : "legacy-static-v1",
            OtherCivilizations = Math.Max(0, settings.PreWarpCivilizationCount - 1),
            AncientCivilizations = settings.AncientCivilizationCount == 0 ? "None" :
                settings.AncientCivilizationCount == 1 ? "Rare" : "Standard",
            GalacticCore = galaxy.GalacticCore,
        };
        return new CampaignBootstrapResult(
            galaxy,
            new DiplomacyState(),
            _saveService.CreateAdaptiveResearchState(galaxy),
            0.0,
            CampaignBootstrapSource.NewCampaign,
            global::Game.GameVersion.Current,
            null,
            null);
    }

    public CampaignBootstrapResult CreateNew(
        string enteredSeed,
        string playerSpeciesId = Game.Simulation.Species.SpeciesCatalog.TerranBaselineId,
        Action<GalaxyGenerationProgress>? progress = null,
        int systemCount = FullGalaxyStellarPopulation.DefaultSystemCount)
    {
        var internalSeed = CampaignSeed.Parse(enteredSeed);
        var metadata = GalaxyGenerationMetadata.FullGalaxy500(enteredSeed.Trim(), internalSeed, playerSpeciesId, systemCount);
        return CreateNew(metadata, progress);
    }

    public CampaignBootstrapResult CreateNew(
        GalaxyGenerationMetadata metadata,
        Action<GalaxyGenerationProgress>? progress = null)
    {
        ValidateFullGalaxyOptions(metadata);
        var galaxy = _generator.Generate(metadata.InternalSeed, metadata.ToSettings(), progress);
        galaxy.GenerationMetadata = metadata;
        return new CampaignBootstrapResult(
            galaxy,
            new DiplomacyState(),
            _saveService.CreateAdaptiveResearchState(galaxy),
            0.0,
            CampaignBootstrapSource.NewCampaign,
            global::Game.GameVersion.Current,
            null,
            null);
    }

    private static void ValidateFullGalaxyOptions(GalaxyGenerationMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        if (metadata.GeneratorVersion != GalaxyGenerationMetadata.FullGalaxyGeneratorVersion ||
            metadata.GalaxyShape != "Full galaxy" ||
            metadata.InternalSeed != CampaignSeed.Parse(metadata.EnteredSeed) ||
            !FullGalaxyStellarPopulation.AllowedSystemCounts.Contains(metadata.SystemCount) ||
            metadata.OtherCivilizations is not (0 or 3 or 5 or 8 or 12) ||
            metadata.AncientCivilizations is not ("None" or "Rare" or "Standard") ||
            metadata.HabitableWorlds is not ("Rare" or "Uncommon" or "Common") ||
            metadata.AnomalyFrequency is not ("Low" or "Standard" or "High") ||
            metadata.GalacticCore != GalacticCoreMetadata.CreateFullGalaxy(metadata.SystemCount))
            throw new ArgumentException("Full-galaxy generation metadata contains unsupported or inconsistent options.", nameof(metadata));
    }

    /// <summary>Explicit compatibility path for the original all-nearby 500-star profile.</summary>
    public CampaignBootstrapResult CreateNearbyCatalog(
        string enteredSeed,
        string playerSpeciesId = Game.Simulation.Species.SpeciesCatalog.TerranBaselineId,
        Action<GalaxyGenerationProgress>? progress = null)
    {
        var internalSeed = CampaignSeed.Parse(enteredSeed);
        var metadata = GalaxyGenerationMetadata.MilkyWay500(enteredSeed.Trim(), internalSeed, playerSpeciesId);
        var galaxy = _generator.Generate(internalSeed, metadata.ToSettings(), progress);
        galaxy.GenerationMetadata = metadata;
        return new CampaignBootstrapResult(galaxy, new DiplomacyState(),
            _saveService.CreateAdaptiveResearchState(galaxy), 0.0, CampaignBootstrapSource.NewCampaign,
            global::Game.GameVersion.Current, null, null);
    }

    public CampaignBootstrapResult LoadOrCreate(
        string savePath,
        long fallbackSeed,
        GalaxyGenerationSettings? fallbackSettings = null)
    {
        if (string.IsNullOrWhiteSpace(savePath))
            throw new ArgumentException("A save path is required.", nameof(savePath));

        var backupPath = savePath + ".bak";
        if (File.Exists(savePath))
        {
            try
            {
                return Load(savePath, CampaignBootstrapSource.LoadedSave, loadFailure: null);
            }
            catch (Exception primaryFailure)
            {
                if (File.Exists(backupPath))
                {
                    try
                    {
                        return Load(
                            backupPath,
                            CampaignBootstrapSource.RecoveredFromBackup,
                            $"Primary autosave failed and the previous backup was recovered.\n{primaryFailure}");
                    }
                    catch (Exception backupFailure)
                    {
                        return RecoverNewCampaign(
                            fallbackSeed,
                            fallbackSettings,
                            $"Primary autosave failed:\n{primaryFailure}\nBackup autosave also failed:\n{backupFailure}");
                    }
                }

                return RecoverNewCampaign(
                    fallbackSeed,
                    fallbackSettings,
                    $"Primary autosave failed and no backup was available.\n{primaryFailure}");
            }
        }

        if (File.Exists(backupPath))
        {
            try
            {
                return Load(
                    backupPath,
                    CampaignBootstrapSource.RecoveredFromBackup,
                    "Primary autosave was missing; the previous backup was recovered.");
            }
            catch (Exception backupFailure)
            {
                return RecoverNewCampaign(
                    fallbackSeed,
                    fallbackSettings,
                    $"Primary autosave was missing and the backup autosave failed:\n{backupFailure}");
            }
        }

        return CreateNew(fallbackSeed, fallbackSettings);
    }

    /// <summary>Loads an existing primary or backup without generating or writing a replacement.</summary>
    public CampaignBootstrapResult LoadExisting(string savePath, Action<CampaignRestorationProgress>? progress = null)
    {
        if (string.IsNullOrWhiteSpace(savePath))
            throw new ArgumentException("A save path is required.", nameof(savePath));

        var failures = new List<string>();
        var latest = 0.0;
        void Report(CampaignRestorationProgress update)
        {
            latest = Math.Max(latest, update.Validate().Fraction);
            progress?.Invoke(update with { Fraction = latest });
        }
        var backupPath = savePath + ".bak";
        if (File.Exists(savePath))
        {
            try { return Load(savePath, CampaignBootstrapSource.LoadedSave, loadFailure: null, Report); }
            catch (Exception failure) { failures.Add($"Primary autosave failed:\n{failure}"); }
        }
        else failures.Add("Primary autosave was missing.");

        if (File.Exists(backupPath))
        {
            try
            {
                Report(new(Math.Max(latest, .12), "Primary save unavailable; restoring backup"));
                return Load(backupPath, CampaignBootstrapSource.RecoveredFromBackup,
                    string.Join("\n", failures) + "\nThe previous backup was recovered.", Report);
            }
            catch (Exception failure) { failures.Add($"Backup autosave also failed:\n{failure}"); }
        }
        else failures.Add("No backup autosave was available.");

        throw new InvalidDataException(string.Join("\n", failures));
    }

    /// <summary>
    /// Compatibility overload for callers that have not yet acquired a campaign Diplomacy owner.
    /// It deliberately persists an empty state rather than inferring political knowledge.
    /// </summary>
    public void Save(string savePath, GalaxyState galaxy, double simulationDays) =>
        Save(savePath, galaxy, new DiplomacyState(), simulationDays);

    public void Save(
        string savePath,
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        double simulationDays) =>
        SaveCore(savePath, galaxy, diplomacy, _saveService.CreateAdaptiveResearchState(galaxy), simulationDays, preserveExistingBackup: false);

    public void Save(
        string savePath,
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch,
        double simulationDays) =>
        SaveCore(savePath, galaxy, diplomacy, adaptiveResearch, simulationDays, preserveExistingBackup: false);

    /// <summary>
    /// Repairs/recreates the primary autosave after startup loaded the known-good .bak file.
    /// The existing backup is deliberately preserved for this one write; ordinary Save calls
    /// resume normal primary-to-backup rotation after repair succeeds.
    /// </summary>
    public void SavePreservingBackup(
        string savePath,
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        double simulationDays) =>
        SaveCore(savePath, galaxy, diplomacy, _saveService.CreateAdaptiveResearchState(galaxy), simulationDays, preserveExistingBackup: true);

    public void SavePreservingBackup(
        string savePath,
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch,
        double simulationDays) =>
        SaveCore(savePath, galaxy, diplomacy, adaptiveResearch, simulationDays, preserveExistingBackup: true);

    public PreparedCampaignSave PrepareSave(
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch,
        double simulationDays) =>
        _saveService.PrepareSave(galaxy, simulationDays, diplomacy, adaptiveResearch);

    public CampaignSaveWriteMetrics WritePreparedSave(
        string savePath,
        PreparedCampaignSave prepared,
        bool preserveExistingBackup = false) =>
        _saveService.WritePrepared(savePath, prepared, preserveExistingBackup);

    private void SaveCore(
        string savePath,
        GalaxyState galaxy,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch,
        double simulationDays,
        bool preserveExistingBackup)
    {
        if (string.IsNullOrWhiteSpace(savePath))
            throw new ArgumentException("A save path is required.", nameof(savePath));
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(diplomacy);
        ArgumentNullException.ThrowIfNull(adaptiveResearch);
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays), "Simulation time must be finite and non-negative.");

        if (preserveExistingBackup)
            _saveService.SavePreservingBackup(savePath, galaxy, simulationDays, diplomacy, adaptiveResearch);
        else
            _saveService.Save(savePath, galaxy, simulationDays, diplomacy, adaptiveResearch);
    }

    private CampaignBootstrapResult Load(
        string path,
        CampaignBootstrapSource source,
        string? loadFailure,
        Action<CampaignRestorationProgress>? progress = null)
    {
        var loaded = _saveService.Load(path, progress);
        return new CampaignBootstrapResult(
            loaded.Galaxy,
            loaded.Diplomacy,
            loaded.AdaptiveResearch,
            loaded.SimulationDays,
            source,
            loaded.GameVersion,
            loaded.SavedAtUtc,
            loadFailure);
    }

    private CampaignBootstrapResult RecoverNewCampaign(
        long fallbackSeed,
        GalaxyGenerationSettings? fallbackSettings,
        string loadFailure)
    {
        var recovered = CreateNew(fallbackSeed, fallbackSettings);
        return recovered with
        {
            Source = CampaignBootstrapSource.RecoveredFromInvalidSave,
            LoadFailure = loadFailure,
        };
    }
}
