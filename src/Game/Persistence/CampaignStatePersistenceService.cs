using System;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Diagnostics;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;

namespace Game.Persistence;

public sealed class PreparedCampaignSave
{
    internal PreparedCampaignSave(object payload, PreparedCampaignKind kind, CampaignSaveCaptureMetrics metrics)
    { Payload = payload; Kind = kind; CaptureMetrics = metrics; }
    internal object Payload { get; }
    internal PreparedCampaignKind Kind { get; }
    public CampaignSaveCaptureMetrics CaptureMetrics { get; }
}

internal enum PreparedCampaignKind { Player, Developer }

public sealed record CampaignSaveCaptureMetrics(
    double DiplomacyMilliseconds,
    double GalaxyValidationMilliseconds,
    double GalaxyDtoMilliseconds,
    double AdaptiveResearchMilliseconds)
{
    public double TotalMilliseconds => DiplomacyMilliseconds + GalaxyValidationMilliseconds +
        GalaxyDtoMilliseconds + AdaptiveResearchMilliseconds;
}

public sealed record CampaignSaveWriteMetrics(double JsonMilliseconds, double AtomicWriteMilliseconds)
{
    public double TotalMilliseconds => JsonMilliseconds + AtomicWriteMilliseconds;
}

internal sealed class DeveloperSaveEnvelope
{
    public int DeveloperFormatVersion { get; set; }
    public string Mode { get; set; } = string.Empty;
    public bool ToolsUsed { get; set; }
    public CampaignSaveEnvelope Campaign { get; set; } = new();
}

public interface ICampaignSaveWriter
{
    void WriteAtomically(string path, string json, bool preserveExistingBackup);
}

internal sealed class AtomicCampaignSaveWriter : ICampaignSaveWriter
{
    public void WriteAtomically(string path, string json, bool preserveExistingBackup)
    {
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory))
            Directory.CreateDirectory(directory);

        var tempPath = path + $".{Guid.NewGuid():N}.tmp";
        try
        {
            using (var stream = new FileStream(tempPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true);
                writer.Write(json);
                writer.Flush();
                stream.Flush(flushToDisk: true);
            }
            if (File.Exists(path))
                File.Replace(tempPath, path, preserveExistingBackup ? null : path + ".bak", ignoreMetadataErrors: true);
            else
                File.Move(tempPath, path);
        }
        finally
        {
            DeleteIfPresent(tempPath);
        }
    }

    private static void DeleteIfPresent(string path)
    {
        if (File.Exists(path))
            File.Delete(path);
    }
}

public sealed record CampaignRestorationProgress(double Fraction, string Status)
{
    public CampaignRestorationProgress Validate()
    {
        if (!double.IsFinite(Fraction) || Fraction < 0 || Fraction >= 1)
            throw new ArgumentOutOfRangeException(nameof(Fraction));
        if (string.IsNullOrWhiteSpace(Status)) throw new ArgumentException("A restoration status is required.", nameof(Status));
        return this;
    }
}

/// <summary>
/// Authoritative campaign-level persistence boundary.
///
/// Save format v17 wraps the authoritative v16 galaxy payload with Diplomacy and bounded
/// Adaptive Research snapshots. Older v9/v11/v13/v15 campaigns retain their historical
/// procedural, preset, surface, and Adaptive Research migration semantics.
/// </summary>
public sealed class CampaignStatePersistenceService
{
    public const int LegacyFormatVersion = 9;
    public const int PresetFormatVersion = 11;
    public const int SurfaceFormatVersion = 13;
    public const int AdaptiveFormatVersion = 15;
    public const int CurrentFormatVersion = 17;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
    };

    private readonly CampaignSaveService _galaxyPersistence;
    private readonly AdaptiveResearchStrategicRuntime _adaptiveResearchRuntime;
    private readonly AdaptiveResearchCampaignFactory _adaptiveResearchFactory;
    private readonly AdaptiveResearchCampaignSnapshotCodec _adaptiveResearchCodec;
    private readonly ICampaignSaveWriter _saveWriter;

    public CampaignStatePersistenceService(
        CampaignSaveService? galaxyPersistence = null,
        AdaptiveResearchStrategicRuntime? adaptiveResearchRuntime = null,
        ICampaignSaveWriter? saveWriter = null)
    {
        _galaxyPersistence = galaxyPersistence ?? new CampaignSaveService();
        _adaptiveResearchRuntime = adaptiveResearchRuntime ?? AdaptiveResearchStrategicRuntime.LoadFromDirectory(
            AdaptiveResearchDataLocator.FindDataRoot());
        _adaptiveResearchFactory = new AdaptiveResearchCampaignFactory(_adaptiveResearchRuntime);
        _adaptiveResearchCodec = new AdaptiveResearchCampaignSnapshotCodec(_adaptiveResearchRuntime);
        _saveWriter = saveWriter ?? new AtomicCampaignSaveWriter();
    }

    public AdaptiveResearchCampaignState CreateAdaptiveResearchState(GalaxyState galaxy) =>
        _adaptiveResearchFactory.Create(galaxy);

    public void Save(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy) =>
        Save(path, galaxy, simulationDays, diplomacy, _adaptiveResearchFactory.Create(galaxy));

    public void Save(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch) =>
        WritePrepared(path, PrepareSave(galaxy, simulationDays, diplomacy, adaptiveResearch), preserveExistingBackup: false);

    /// <summary>
    /// Atomically replaces the primary campaign file without rotating the existing .bak file.
    /// This is reserved for the first successful repair save after startup recovered from that
    /// known-good backup. Ordinary saves must continue through Save so backup rotation resumes.
    /// </summary>
    public void SavePreservingBackup(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy) =>
        SavePreservingBackup(path, galaxy, simulationDays, diplomacy, _adaptiveResearchFactory.Create(galaxy));

    public void SavePreservingBackup(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch) =>
        WritePrepared(path, PrepareSave(galaxy, simulationDays, diplomacy, adaptiveResearch), preserveExistingBackup: true);

    internal void SaveDeveloperPayload(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy) =>
        SaveDeveloperPayload(path, galaxy, simulationDays, diplomacy, _adaptiveResearchFactory.Create(galaxy));

    internal void SaveDeveloperPayload(
        string path,
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch) =>
        WritePreparedDeveloper(path, PrepareDeveloperPayload(galaxy, simulationDays, diplomacy, adaptiveResearch), preserveExistingBackup: false);

    public PreparedCampaignSave PrepareSave(
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch) =>
        PrepareCore(galaxy, simulationDays, diplomacy, adaptiveResearch, developerPayload: false);

    internal PreparedCampaignSave PrepareDeveloperPayload(
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch) =>
        PrepareCore(galaxy, simulationDays, diplomacy, adaptiveResearch, developerPayload: true);

    public CampaignSaveWriteMetrics WritePrepared(string path, PreparedCampaignSave prepared, bool preserveExistingBackup = false)
        => WritePreparedCore(path, prepared, PreparedCampaignKind.Player, preserveExistingBackup);

    internal CampaignSaveWriteMetrics WritePreparedDeveloper(string path, PreparedCampaignSave prepared,
        bool preserveExistingBackup = false)
        => WritePreparedCore(path, prepared, PreparedCampaignKind.Developer, preserveExistingBackup);

    private CampaignSaveWriteMetrics WritePreparedCore(string path, PreparedCampaignSave prepared,
        PreparedCampaignKind expectedKind, bool preserveExistingBackup)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new ArgumentException("A save path is required.", nameof(path));
        ArgumentNullException.ThrowIfNull(prepared);
        if (prepared.Kind != expectedKind)
            throw new InvalidOperationException(expectedKind == PreparedCampaignKind.Player
                ? "A Developer prepared payload cannot be written through Player campaign persistence."
                : "A Player prepared payload cannot be written through Developer campaign persistence.");
        var jsonStarted = Stopwatch.GetTimestamp();
        var json = JsonSerializer.Serialize(prepared.Payload, prepared.Payload.GetType(), JsonOptions);
        var jsonMilliseconds = Stopwatch.GetElapsedTime(jsonStarted).TotalMilliseconds;
        var writeStarted = Stopwatch.GetTimestamp();
        _saveWriter.WriteAtomically(path, json, preserveExistingBackup);
        return new CampaignSaveWriteMetrics(jsonMilliseconds,
            Stopwatch.GetElapsedTime(writeStarted).TotalMilliseconds);
    }

    private PreparedCampaignSave PrepareCore(
        GalaxyState galaxy,
        double simulationDays,
        DiplomacyState diplomacy,
        AdaptiveResearchCampaignState adaptiveResearch,
        bool developerPayload)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(diplomacy);
        ArgumentNullException.ThrowIfNull(adaptiveResearch);
        if ((galaxy.DeveloperSession is not null) != developerPayload)
            throw new InvalidOperationException(developerPayload
                ? "Developer payload serialization requires explicit Developer session provenance."
                : "A Developer campaign cannot be written as a Player save. Use Developer campaign persistence.");
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays), "Simulation time must be finite and non-negative.");

        var diplomacyStarted = Stopwatch.GetTimestamp();
        var snapshot = diplomacy.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(snapshot);
        DiplomacyCampaignReferenceValidator.Validate(galaxy, snapshot);
        var diplomacyMilliseconds = Stopwatch.GetElapsedTime(diplomacyStarted).TotalMilliseconds;

        var detachedGalaxy = _galaxyPersistence.CaptureDetachedEnvelope(galaxy, simulationDays, developerPayload);
        var root = detachedGalaxy.Envelope;
        var galaxyFormat = root.FormatVersion;
        if (galaxyFormat != CampaignSaveService.CurrentFormatVersion)
            throw new InvalidDataException(
                $"Expected galaxy payload format {CampaignSaveService.CurrentFormatVersion}, got {galaxyFormat}.");

        var adaptiveStarted = Stopwatch.GetTimestamp();
        var adaptiveSnapshot = new AdaptiveResearchCampaignSnapshotCodec(adaptiveResearch.Runtime).Capture(adaptiveResearch);
        var adaptiveMilliseconds = Stopwatch.GetElapsedTime(adaptiveStarted).TotalMilliseconds;
        root.FormatVersion = CurrentFormatVersion;
        root.GalaxyFormatVersion = galaxyFormat;
        root.Diplomacy = snapshot;
        root.AdaptiveResearch = adaptiveSnapshot;
        return new PreparedCampaignSave(root, developerPayload ? PreparedCampaignKind.Developer : PreparedCampaignKind.Player,
            new CampaignSaveCaptureMetrics(diplomacyMilliseconds, detachedGalaxy.ValidationMilliseconds,
                detachedGalaxy.DtoCaptureMilliseconds, adaptiveMilliseconds));
    }

    public LoadedCampaignState Load(string path, Action<CampaignRestorationProgress>? progress = null)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new ArgumentException("A save path is required.", nameof(path));

        void Report(double fraction, string status) => progress?.Invoke(new CampaignRestorationProgress(fraction, status).Validate());
        Report(.04, "Reading saved campaign");
        var json = File.ReadAllText(path);
        Report(.16, "Decoding saved campaign");
        var root = JsonNode.Parse(json)?.AsObject()
            ?? throw new InvalidDataException("Save file did not contain a campaign JSON object.");
        if (root.ContainsKey("DeveloperFormatVersion"))
            throw new InvalidDataException("Developer campaign envelopes cannot be opened as Player saves.");
        var formatVersion = root["FormatVersion"]?.GetValue<int>()
            ?? throw new InvalidDataException("Save file did not declare FormatVersion.");

        if (formatVersion < 1 || formatVersion > CurrentFormatVersion)
        {
            throw new InvalidDataException(
                $"Unsupported campaign save format {formatVersion}; maximum supported is {CurrentFormatVersion}.");
        }

        if (formatVersion <= CampaignSaveService.LegacyFormatVersion ||
            formatVersion is CampaignSaveService.PresetFormatVersion or CampaignSaveService.SurfaceFormatVersion or CampaignSaveService.CurrentFormatVersion)
        {
            // Legacy saves did not persist political state. Do not infer contacts, trust, claims,
            // treaties or wars from omniscient galaxy data during migration.
            Report(.35, "Restoring galaxy state");
            var legacy = _galaxyPersistence.Load(path);
            Report(.82, "Rebuilding campaign systems");
            var adaptive = _adaptiveResearchFactory.Create(legacy.Galaxy);
            Report(.97, "Validating restored campaign");
            return new LoadedCampaignState(
                legacy.Galaxy,
                legacy.SimulationDays,
                legacy.GameVersion,
                legacy.SavedAtUtc,
                new DiplomacyState(),
                adaptive);
        }

        if (formatVersion != LegacyFormatVersion && formatVersion != PresetFormatVersion &&
            formatVersion != SurfaceFormatVersion && formatVersion != AdaptiveFormatVersion &&
            formatVersion != CurrentFormatVersion)
            throw new InvalidDataException($"No migration path is defined for campaign save format {formatVersion}.");

        Report(.25, "Restoring diplomacy");
        var diplomacyNode = root["Diplomacy"]
            ?? throw new InvalidDataException($"Format v{formatVersion} save is missing the authoritative Diplomacy snapshot.");

        DiplomacyStateSnapshot snapshot;
        try
        {
            snapshot = diplomacyNode.Deserialize<DiplomacyStateSnapshot>(JsonOptions)
                ?? throw new InvalidDataException($"Format v{formatVersion} Diplomacy snapshot was empty.");
            DiplomacySnapshotInvariantValidator.Validate(snapshot);
        }
        catch (DiplomacySnapshotValidationException ex)
        {
            throw new InvalidDataException($"Format v{formatVersion} Diplomacy snapshot failed strict invariant validation.", ex);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Format v{formatVersion} Diplomacy snapshot could not be decoded.", ex);
        }

        // v9 wraps procedural v8; v11 wraps preset-aware v10; v13 wraps surface-construction v12.
        // v15 records its historical inner galaxy version; v17 always records v16.
        // Normalize to the matching galaxy version so neither path silently reinterprets the
        // other catalog. Species/body/Combat validation remains in CampaignSaveService.
        Report(.42, "Restoring galaxy state");
        var normalized = (JsonObject)root.DeepClone();
        normalized["FormatVersion"] = formatVersion is AdaptiveFormatVersion or CurrentFormatVersion
            ? ReadGalaxyFormatVersion(root, formatVersion)
            : formatVersion - 1;
        normalized.Remove("Diplomacy");
        normalized.Remove("AdaptiveResearch");
        normalized.Remove("GalaxyFormatVersion");

        var normalizedPath = path + $".{Guid.NewGuid():N}.v8load";
        try
        {
            File.WriteAllText(normalizedPath, normalized.ToJsonString(JsonOptions));
            var galaxy = _galaxyPersistence.Load(normalizedPath);
            Report(.72, "Validating campaign references");
            DiplomacyCampaignReferenceValidator.Validate(galaxy.Galaxy, snapshot);
            Report(.82, "Restoring research progress");
            var adaptiveResearch = formatVersion is AdaptiveFormatVersion or CurrentFormatVersion
                ? RestoreAdaptiveResearch(root, galaxy.Galaxy, formatVersion)
                : _adaptiveResearchFactory.Create(galaxy.Galaxy);
            Report(.97, "Finalizing restored campaign");
            return new LoadedCampaignState(
                galaxy.Galaxy,
                galaxy.SimulationDays,
                galaxy.GameVersion,
                galaxy.SavedAtUtc,
                DiplomacyState.Restore(snapshot),
                adaptiveResearch);
        }
        finally
        {
            DeleteIfPresent(normalizedPath);
            DeleteIfPresent(normalizedPath + ".tmp");
            DeleteIfPresent(normalizedPath + ".bak");
        }
    }

    private static int ReadGalaxyFormatVersion(JsonObject root, int campaignFormatVersion)
    {
        var version = root["GalaxyFormatVersion"]?.GetValue<int>()
            ?? throw new InvalidDataException($"Format v{campaignFormatVersion} save is missing GalaxyFormatVersion.");
        var supported = campaignFormatVersion == CurrentFormatVersion
            ? version == CampaignSaveService.CurrentFormatVersion
            : version is CampaignSaveService.LegacyFormatVersion or CampaignSaveService.PresetFormatVersion or CampaignSaveService.SurfaceFormatVersion;
        if (!supported)
            throw new InvalidDataException($"Format v{campaignFormatVersion} save references unsupported galaxy format {version}.");
        return version;
    }

    private AdaptiveResearchCampaignState RestoreAdaptiveResearch(
        JsonObject root,
        GalaxyState galaxy,
        int formatVersion)
    {
        var node = root["AdaptiveResearch"]
            ?? throw new InvalidDataException($"Format v{formatVersion} save is missing Adaptive Research state.");
        try
        {
            var snapshot = node.Deserialize<AdaptiveResearchCampaignSnapshot>(JsonOptions)
                ?? throw new InvalidDataException($"Format v{formatVersion} Adaptive Research state was empty.");
            return _adaptiveResearchCodec.Restore(galaxy, snapshot);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Format v{formatVersion} Adaptive Research state could not be decoded.", ex);
        }
    }

    private static void DeleteIfPresent(string path)
    {
        if (File.Exists(path))
            File.Delete(path);
    }
}

public sealed record LoadedCampaignState(
    GalaxyState Galaxy,
    double SimulationDays,
    string GameVersion,
    DateTimeOffset SavedAtUtc,
    DiplomacyState Diplomacy,
    AdaptiveResearchCampaignState AdaptiveResearch);
