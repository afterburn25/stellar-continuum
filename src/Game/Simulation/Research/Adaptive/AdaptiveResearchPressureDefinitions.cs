using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchPressureRuleDefinition(
    string Id,
    IReadOnlyList<string> MetricSignalIds,
    IReadOnlyList<string> EventSignalIds,
    double DecayPerYear,
    double MemoryFloor);

public sealed record ResearchPressureRuntimePolicy(
    double StrongestSignalWeight,
    double MeanSignalWeight,
    double RiseTowardTargetPerYear,
    double EventPulseBasePoints,
    double IntendedReviewIntervalYears,
    double DormantReviewIntervalYears);

/// <summary>
/// Immutable causal pressure rules and signal indexes. These indexes map simulation facts to
/// pressure IDs; they never map directly to technology nodes.
/// </summary>
public sealed class AdaptiveResearchPressureCatalog
{
    private AdaptiveResearchPressureCatalog(
        IReadOnlyDictionary<string, ResearchPressureRuleDefinition> rules,
        IReadOnlyDictionary<string, IReadOnlyList<string>> pressuresByMetricSignal,
        IReadOnlyDictionary<string, IReadOnlyList<string>> pressuresByEventSignal,
        ResearchPressureRuntimePolicy runtimePolicy)
    {
        Rules = rules;
        PressuresByMetricSignal = pressuresByMetricSignal;
        PressuresByEventSignal = pressuresByEventSignal;
        RuntimePolicy = runtimePolicy;
    }

    public IReadOnlyDictionary<string, ResearchPressureRuleDefinition> Rules { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> PressuresByMetricSignal { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> PressuresByEventSignal { get; }
    public ResearchPressureRuntimePolicy RuntimePolicy { get; }

    public bool IsKnownMetricSignal(string signalId) => PressuresByMetricSignal.ContainsKey(signalId);
    public bool IsKnownEventSignal(string signalId) => PressuresByEventSignal.ContainsKey(signalId);

    public static AdaptiveResearchPressureCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        var root = Path.GetFullPath(rootPath);
        using var dynamicsDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "pressure_dynamics.json")));
        ValidateCatalogId(dynamicsDoc.RootElement, catalog.Metadata.CatalogId, "pressure_dynamics.json");

        var rules = new Dictionary<string, ResearchPressureRuleDefinition>(StringComparer.Ordinal);
        var metricIndex = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal);
        var eventIndex = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal);
        foreach (var property in dynamicsDoc.RootElement.GetProperty("rules").EnumerateObject())
        {
            if (!catalog.PressureIds.Contains(property.Name))
                throw new InvalidDataException($"Pressure dynamics defines unknown pressure '{property.Name}'.");
            var metricSignals = StringArray(property.Value, "metric_signals");
            var eventSignals = StringArray(property.Value, "event_signals");
            var decay = property.Value.GetProperty("decay_per_year").GetDouble();
            var memoryFloor = property.Value.GetProperty("memory_floor").GetDouble();
            if (decay < 0 || memoryFloor < 0 || memoryFloor > 100 || double.IsNaN(decay) || double.IsInfinity(decay))
                throw new InvalidDataException($"Pressure '{property.Name}' has invalid decay/memory-floor values.");

            rules.Add(property.Name, new ResearchPressureRuleDefinition(
                property.Name,
                metricSignals,
                eventSignals,
                decay,
                memoryFloor));
            foreach (var signal in metricSignals)
                AddIndex(metricIndex, signal, property.Name);
            foreach (var signal in eventSignals)
                AddIndex(eventIndex, signal, property.Name);
        }

        if (rules.Count != catalog.PressureIds.Count || catalog.PressureIds.Any(id => !rules.ContainsKey(id)))
            throw new InvalidDataException("Pressure dynamics rules do not cover the full public Research Pressure catalog.");

        using var policyDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_pressure_runtime_policy.json")));
        ValidateCatalogId(policyDoc.RootElement, catalog.Metadata.CatalogId, "research_pressure_runtime_policy.json");
        var metricTarget = policyDoc.RootElement.GetProperty("metric_target");
        var response = policyDoc.RootElement.GetProperty("response");
        var maintenance = policyDoc.RootElement.GetProperty("maintenance");
        var policy = new ResearchPressureRuntimePolicy(
            0.75,
            0.25,
            response.GetProperty("rise_toward_metric_target_per_year").GetDouble(),
            response.GetProperty("event_pulse_base_points").GetDouble(),
            maintenance.GetProperty("intended_review_interval_years").GetDouble(),
            maintenance.GetProperty("coarse_dormant_review_interval_years").GetDouble());

        if (policy.RiseTowardTargetPerYear <= 0 || policy.EventPulseBasePoints <= 0 ||
            policy.IntendedReviewIntervalYears <= 0 || policy.DormantReviewIntervalYears <= 0 ||
            Math.Abs((policy.StrongestSignalWeight + policy.MeanSignalWeight) - 1.0) > 0.000001)
            throw new InvalidDataException("Invalid Research Pressure runtime policy.");
        _ = metricTarget;

        return new AdaptiveResearchPressureCatalog(
            new ReadOnlyDictionary<string, ResearchPressureRuleDefinition>(rules),
            FreezeIndex(metricIndex),
            FreezeIndex(eventIndex),
            policy);
    }

    private static void AddIndex(Dictionary<string, HashSet<string>> index, string signalId, string pressureId)
    {
        if (!index.TryGetValue(signalId, out var values))
        {
            values = new HashSet<string>(StringComparer.Ordinal);
            index.Add(signalId, values);
        }
        values.Add(pressureId);
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<string>> FreezeIndex(
        Dictionary<string, HashSet<string>> source) =>
        new ReadOnlyDictionary<string, IReadOnlyList<string>>(
            source.ToDictionary(
                pair => pair.Key,
                pair => (IReadOnlyList<string>)pair.Value.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
                StringComparer.Ordinal));

    private static IReadOnlyList<string> StringArray(JsonElement element, string name) =>
        element.TryGetProperty(name, out var property)
            ? property.EnumerateArray().Select(value => value.GetString() ?? throw new InvalidDataException($"{name} contains null.")).ToArray()
            : Array.Empty<string>();

    private static void ValidateCatalogId(JsonElement root, string expected, string fileName)
    {
        var actual = root.GetProperty("catalog_id").GetString();
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{fileName} catalog_id '{actual}' does not match '{expected}'.");
    }
}
