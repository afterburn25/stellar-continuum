using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Research.Adaptive;

public sealed record AdaptiveResearchOutcomeSnapshot(
    IReadOnlyList<ResearchOutcomeNodeSummary> Summaries,
    IReadOnlyList<ResearchOutcomeHistoryRecord> RecentRecords);

public sealed record AdaptiveResearchStateSnapshotV5(
    int SchemaVersion,
    string CatalogId,
    AdaptiveResearchStateSnapshotV4 Research,
    AdaptiveResearchOutcomeSnapshot Outcomes);

/// <summary>
/// Preferred standalone Adaptive Research snapshot after deterministic research outcomes.
/// V5 persists compact outcome summaries/attempt indexes so save/load cannot reroll resolved experiments.
/// </summary>
public sealed class AdaptiveResearchOutcomeSnapshotCodec
{
    public const int CurrentSchemaVersion = 5;

    private readonly AdaptiveResearchStrategicRuntime _runtime;
    private readonly AdaptiveResearchForeignTechnologySnapshotCodec _v4;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    public AdaptiveResearchOutcomeSnapshotCodec(AdaptiveResearchStrategicRuntime runtime)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _v4 = new AdaptiveResearchForeignTechnologySnapshotCodec(runtime);
    }

    public AdaptiveResearchStateSnapshotV5 Capture(AdaptiveResearchCivilizationState state)
    {
        var outcomes = _runtime.Outcomes.GetState(state);
        return new AdaptiveResearchStateSnapshotV5(
            CurrentSchemaVersion,
            _runtime.Authority.Catalog.Metadata.CatalogId,
            _v4.Capture(state),
            new AdaptiveResearchOutcomeSnapshot(
                outcomes.Summaries.Values.OrderBy(value => value.NodeId, StringComparer.Ordinal).ToArray(),
                outcomes.RecentRecords.OrderBy(value => value.Sequence).ToArray()));
    }

    public string Serialize(AdaptiveResearchCivilizationState state) =>
        JsonSerializer.Serialize(Capture(state), _jsonOptions);

    public AdaptiveResearchCivilizationState Deserialize(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("schemaVersion", out var schemaElement))
            throw new InvalidDataException("Adaptive Research snapshot is missing schemaVersion.");
        var schemaVersion = schemaElement.GetInt32();
        if (schemaVersion is >= 1 and <= 4)
            return _v4.Deserialize(json);
        if (schemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research outcome snapshot schema {schemaVersion}.");

        var snapshot = JsonSerializer.Deserialize<AdaptiveResearchStateSnapshotV5>(json, _jsonOptions)
            ?? throw new InvalidDataException("Adaptive Research v5 snapshot deserialized to null.");
        return Restore(snapshot);
    }

    public AdaptiveResearchCivilizationState Restore(AdaptiveResearchStateSnapshotV5 snapshot)
    {
        if (snapshot.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research outcome snapshot schema {snapshot.SchemaVersion}.");
        if (!string.Equals(snapshot.CatalogId, _runtime.Authority.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research snapshot catalog '{snapshot.CatalogId}' does not match runtime catalog '{_runtime.Authority.Catalog.Metadata.CatalogId}'.");

        var state = _v4.Restore(snapshot.Research);
        var outcomeState = _runtime.Outcomes.GetState(state);
        foreach (var summary in snapshot.Outcomes.Summaries)
        {
            ValidateSummary(summary);
            outcomeState.RestoreSummary(summary);
        }

        long previousSequence = 0;
        foreach (var record in snapshot.Outcomes.RecentRecords.OrderBy(value => value.Sequence))
        {
            if (record.Sequence <= previousSequence)
                throw new InvalidDataException("Outcome history sequence must be strictly increasing.");
            previousSequence = record.Sequence;
            if (!_runtime.Authority.Catalog.Nodes.ContainsKey(record.NodeId))
                throw new InvalidDataException($"Outcome history references unknown node '{record.NodeId}'.");
            if (record.AttemptIndex < 0 || double.IsNaN(record.Year) || double.IsInfinity(record.Year))
                throw new InvalidDataException($"Outcome history for '{record.NodeId}' has invalid attempt/year data.");
            if (record.SideDiscoveryNodeId is not null && !_runtime.Authority.Catalog.Nodes.ContainsKey(record.SideDiscoveryNodeId))
                throw new InvalidDataException($"Outcome history references unknown side-discovery node '{record.SideDiscoveryNodeId}'.");
            outcomeState.RestoreRecord(record);
        }

        if (outcomeState.RecentRecords.Count > _runtime.OutcomeCatalog.Policy.MaxRecentOutcomeRecords)
            throw new InvalidDataException("Outcome snapshot exceeds configured bounded recent-history capacity.");
        return state;
    }

    private void ValidateSummary(ResearchOutcomeNodeSummary summary)
    {
        if (!_runtime.Authority.Catalog.Nodes.ContainsKey(summary.NodeId))
            throw new InvalidDataException($"Outcome summary references unknown node '{summary.NodeId}'.");
        if (summary.Attempts < 0 || summary.Setbacks < 0 || summary.PartialSuccesses < 0 ||
            summary.Refinements < 0 || summary.Disproofs < 0 || summary.Anomalies < 0 ||
            summary.Hazards < 0 || summary.SideDiscoveries < 0)
            throw new InvalidDataException($"Outcome summary for '{summary.NodeId}' contains negative counters.");
        if (summary.LastOutcomeId is not null)
            _ = AdaptiveResearchOutcomeCatalog.ParseOutcome(summary.LastOutcomeId);
        if (!double.IsNegativeInfinity(summary.LastOutcomeYear) &&
            (double.IsNaN(summary.LastOutcomeYear) || double.IsInfinity(summary.LastOutcomeYear)))
            throw new InvalidDataException($"Outcome summary for '{summary.NodeId}' has invalid last-outcome year.");
    }
}
