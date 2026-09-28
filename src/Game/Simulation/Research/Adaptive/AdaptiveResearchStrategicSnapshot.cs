using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Research.Adaptive;

public sealed record AdaptiveResearchPressureSupportSnapshot(
    IReadOnlyDictionary<string, double> MetricSignals,
    IReadOnlyList<string> ActivePressureIds);

public sealed record AdaptiveResearchAgendaSnapshot(
    IReadOnlyDictionary<string, string> DomainPriorities,
    IReadOnlyDictionary<string, string> FieldPriorities,
    IReadOnlyDictionary<string, string> ProblemPriorities,
    IReadOnlyDictionary<string, string> CapabilityPriorities,
    ResearchAgendaOrientationState Orientations,
    IReadOnlyDictionary<string, double> CultureAxes,
    double? LastMajorReviewYear,
    string PolicyProvenance);

public sealed record AdaptiveResearchStateSnapshotV3(
    int SchemaVersion,
    string CatalogId,
    AdaptiveResearchStateSnapshotV2 Research,
    AdaptiveResearchPressureSupportSnapshot PressureSupport,
    AdaptiveResearchAgendaSnapshot Agenda);

/// <summary>
/// Preferred standalone strategic Adaptive Research snapshot. V3 layers sparse causal support and
/// agenda/culture state over the validated v2 expertise envelope; it is still not the campaign save format.
/// </summary>
public sealed class AdaptiveResearchStrategicSnapshotCodec
{
    public const int CurrentSchemaVersion = 3;

    private readonly AdaptiveResearchStrategicRuntime _runtime;
    private readonly AdaptiveResearchSnapshotV2Codec _v2;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    public AdaptiveResearchStrategicSnapshotCodec(AdaptiveResearchStrategicRuntime runtime)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _v2 = new AdaptiveResearchSnapshotV2Codec(runtime.Authority);
    }

    public AdaptiveResearchStateSnapshotV3 Capture(AdaptiveResearchCivilizationState state)
    {
        var pressure = _runtime.Pressure.GetSupportState(state);
        var agenda = _runtime.Agenda.GetState(state);
        var activePressureIds = pressure.ActivePressureIds
            .Concat(state.Pressures.Where(pair => pair.Value > 0).Select(pair => pair.Key))
            .Distinct(StringComparer.Ordinal)
            .OrderBy(value => value, StringComparer.Ordinal)
            .ToArray();

        return new AdaptiveResearchStateSnapshotV3(
            CurrentSchemaVersion,
            _runtime.Authority.Catalog.Metadata.CatalogId,
            _v2.Capture(state),
            new AdaptiveResearchPressureSupportSnapshot(
                pressure.MetricSignals.OrderBy(pair => pair.Key, StringComparer.Ordinal)
                    .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                activePressureIds),
            new AdaptiveResearchAgendaSnapshot(
                agenda.DomainPriorities.OrderBy(pair => pair.Key, StringComparer.Ordinal).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                agenda.FieldPriorities.OrderBy(pair => pair.Key, StringComparer.Ordinal).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                agenda.ProblemPriorities.OrderBy(pair => pair.Key, StringComparer.Ordinal).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                agenda.CapabilityPriorities.OrderBy(pair => pair.Key, StringComparer.Ordinal).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                agenda.Orientations,
                agenda.CultureAxes.OrderBy(pair => pair.Key, StringComparer.Ordinal).ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
                double.IsFinite(agenda.LastMajorReviewYear) ? agenda.LastMajorReviewYear : null,
                agenda.PolicyProvenance));
    }

    public string Serialize(AdaptiveResearchCivilizationState state) =>
        JsonSerializer.Serialize(Capture(state), _jsonOptions);

    public AdaptiveResearchCivilizationState Deserialize(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("schemaVersion", out var schemaElement))
            throw new InvalidDataException("Adaptive Research snapshot is missing schemaVersion.");
        var schemaVersion = schemaElement.GetInt32();
        if (schemaVersion is 1 or 2)
        {
            var state = _v2.Deserialize(json);
            ActivatePersistedPressures(state);
            return state;
        }
        if (schemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research strategic snapshot schema {schemaVersion}.");

        var snapshot = JsonSerializer.Deserialize<AdaptiveResearchStateSnapshotV3>(json, _jsonOptions)
            ?? throw new InvalidDataException("Adaptive Research v3 snapshot deserialized to null.");
        return Restore(snapshot);
    }

    public AdaptiveResearchCivilizationState Restore(AdaptiveResearchStateSnapshotV3 snapshot)
    {
        if (snapshot.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research strategic snapshot schema {snapshot.SchemaVersion}.");
        if (!string.Equals(snapshot.CatalogId, _runtime.Authority.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research snapshot catalog '{snapshot.CatalogId}' does not match runtime catalog '{_runtime.Authority.Catalog.Metadata.CatalogId}'.");

        var state = _v2.Restore(snapshot.Research);
        var pressure = _runtime.Pressure.GetSupportState(state);
        foreach (var pair in snapshot.PressureSupport.MetricSignals)
        {
            if (!_runtime.PressureCatalog.IsKnownMetricSignal(pair.Key))
                throw new InvalidDataException($"Strategic snapshot references unknown Pressure metric signal '{pair.Key}'.");
            if (pair.Value <= 0 || pair.Value > 1 || double.IsNaN(pair.Value) || double.IsInfinity(pair.Value))
                throw new InvalidDataException($"Strategic snapshot has invalid metric signal value {pair.Value} for '{pair.Key}'.");
            pressure.SetMetricSignal(pair.Key, pair.Value);
        }
        foreach (var pressureId in snapshot.PressureSupport.ActivePressureIds)
        {
            if (!_runtime.PressureCatalog.Rules.ContainsKey(pressureId))
                throw new InvalidDataException($"Strategic snapshot references unknown activated Pressure '{pressureId}'.");
            pressure.ActivatePressure(pressureId);
        }
        ActivatePersistedPressures(state);

        RestoreAgenda(state, snapshot.Agenda);
        return state;
    }

    private void RestoreAgenda(AdaptiveResearchCivilizationState state, AdaptiveResearchAgendaSnapshot snapshot)
    {
        foreach (var pair in snapshot.DomainPriorities)
            _runtime.Agenda.SetDomainPriority(state, pair.Key, pair.Value);
        foreach (var pair in snapshot.FieldPriorities)
            _runtime.Agenda.SetFieldPriority(state, pair.Key, pair.Value);
        foreach (var pair in snapshot.ProblemPriorities)
            _runtime.Agenda.SetProblemPriority(state, pair.Key, pair.Value);
        foreach (var pair in snapshot.CapabilityPriorities)
            _runtime.Agenda.SetCapabilityPriority(state, pair.Key, pair.Value);
        _runtime.Agenda.SetOrientations(state, snapshot.Orientations);
        foreach (var pair in snapshot.CultureAxes)
            _runtime.Agenda.SetScientificCultureAxis(state, pair.Key, pair.Value);

        if (snapshot.LastMajorReviewYear is double reviewYear)
            _runtime.Agenda.GetState(state).MarkReviewed(reviewYear, snapshot.PolicyProvenance);
    }

    private void ActivatePersistedPressures(AdaptiveResearchCivilizationState state)
    {
        var support = _runtime.Pressure.GetSupportState(state);
        foreach (var pair in state.Pressures)
            if (pair.Value > 0 && _runtime.PressureCatalog.Rules.ContainsKey(pair.Key))
                support.ActivatePressure(pair.Key);
    }
}
