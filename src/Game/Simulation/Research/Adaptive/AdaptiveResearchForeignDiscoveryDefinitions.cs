using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public enum ForeignDiscoveryTriggerAxis
{
    Understanding,
    Adaptation,
}

public sealed record ForeignResearchMethodCandidateRule(
    ForeignDiscoveryTriggerAxis TriggerAxis,
    int MinimumAxisRank,
    IReadOnlyList<string> NodeIds,
    ResearchMaturity AwarenessState);

public sealed record ForeignResearchAwarenessEvent(
    string ForeignTechnologyReference,
    string NodeId,
    ResearchMaturity? PreviousState,
    ResearchMaturity NewState,
    string Reason);

/// <summary>
/// Immutable bounded mapping between foreign assessment progress and existing public research-method
/// nodes. Evidence-specific candidates continue to use the catalog's evidence index.
/// </summary>
public sealed class AdaptiveResearchForeignDiscoveryCatalog
{
    private AdaptiveResearchForeignDiscoveryCatalog(
        ResearchMaturity observedEvidenceState,
        ResearchMaturity characterizedEvidenceState,
        IReadOnlyList<ForeignResearchMethodCandidateRule> methodRules)
    {
        ObservedEvidenceState = observedEvidenceState;
        CharacterizedEvidenceState = characterizedEvidenceState;
        MethodRules = methodRules;
    }

    public ResearchMaturity ObservedEvidenceState { get; }
    public ResearchMaturity CharacterizedEvidenceState { get; }
    public IReadOnlyList<ForeignResearchMethodCandidateRule> MethodRules { get; }

    public static AdaptiveResearchForeignDiscoveryCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog)
    {
        var path = Path.Combine(Path.GetFullPath(rootPath), "foreign_research_materialization_policy.json");
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        var catalogId = RequiredString(root, "catalog_id", "foreign_research_materialization_policy.json");
        if (!string.Equals(catalogId, catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"foreign_research_materialization_policy.json catalog_id '{catalogId}' does not match '{catalog.Metadata.CatalogId}'.");

        var evidence = root.GetProperty("evidence_index_rules");
        var observed = ParseAwarenessState(RequiredString(evidence, "observed_minimum_state", "evidence_index_rules"));
        var characterized = ParseAwarenessState(RequiredString(evidence, "characterized_or_better_minimum_state", "evidence_index_rules"));
        if (observed != ResearchMaturity.Rumored || characterized != ResearchMaturity.Hypothesized)
            throw new InvalidDataException("Foreign evidence seed states must remain Rumored / Hypothesized for the current runtime contract.");

        var rules = new List<ForeignResearchMethodCandidateRule>();
        foreach (var element in root.GetProperty("cross_lineage_method_candidates").EnumerateArray())
        {
            var axis = RequiredString(element, "trigger_axis", "cross_lineage_method_candidates") switch
            {
                "understanding" => ForeignDiscoveryTriggerAxis.Understanding,
                "adaptation" => ForeignDiscoveryTriggerAxis.Adaptation,
                var unknown => throw new InvalidDataException($"Unknown foreign discovery trigger axis '{unknown}'."),
            };
            var minimumStateId = RequiredString(element, "minimum_state", "cross_lineage_method_candidates");
            var minimumRank = axis switch
            {
                ForeignDiscoveryTriggerAxis.Understanding => (int)AdaptiveResearchForeignTechnologyCatalog.ParseUnderstanding(minimumStateId),
                ForeignDiscoveryTriggerAxis.Adaptation => (int)ParseAdaptation(minimumStateId),
                _ => 0,
            };
            var awareness = ParseAwarenessState(RequiredString(element, "awareness_state", "cross_lineage_method_candidates"));
            if (awareness is not (ResearchMaturity.Rumored or ResearchMaturity.Hypothesized))
                throw new InvalidDataException("Foreign method candidate awareness may only be Rumored or Hypothesized.");
            var nodeIds = element.GetProperty("node_ids").EnumerateArray()
                .Select(value => value.GetString() ?? throw new InvalidDataException("Foreign materialization node id cannot be null."))
                .Distinct(StringComparer.Ordinal)
                .ToArray();
            foreach (var nodeId in nodeIds)
            {
                if (!catalog.Nodes.TryGetValue(nodeId, out var node))
                    throw new InvalidDataException($"Foreign materialization policy references unknown node '{nodeId}'.");
                if (!node.PublicNormalResearch)
                    throw new InvalidDataException($"Foreign materialization policy cannot expose non-public node '{nodeId}'.");
            }
            rules.Add(new ForeignResearchMethodCandidateRule(axis, minimumRank, nodeIds, awareness));
        }

        return new AdaptiveResearchForeignDiscoveryCatalog(observed, characterized, rules.AsReadOnly());
    }

    public static ForeignAdaptationState ParseAdaptation(string id) => id switch
    {
        "none" => ForeignAdaptationState.None,
        "conceptual_inspiration" => ForeignAdaptationState.ConceptualInspiration,
        "interface_adaptation" => ForeignAdaptationState.InterfaceAdaptation,
        "native_derivative" => ForeignAdaptationState.NativeDerivative,
        "hybrid_lineage" => ForeignAdaptationState.HybridLineage,
        _ => throw new InvalidDataException($"Unknown foreign adaptation state '{id}'."),
    };

    private static ResearchMaturity ParseAwarenessState(string id) => id switch
    {
        "rumored" => ResearchMaturity.Rumored,
        "hypothesized" => ResearchMaturity.Hypothesized,
        "investigable" => ResearchMaturity.Investigable,
        _ => throw new InvalidDataException($"Unsupported foreign awareness state '{id}'."),
    };

    private static string RequiredString(JsonElement element, string name, string source) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(value.GetString())
            ? value.GetString()!
            : throw new InvalidDataException($"{source} is missing non-empty string '{name}'.");
}
