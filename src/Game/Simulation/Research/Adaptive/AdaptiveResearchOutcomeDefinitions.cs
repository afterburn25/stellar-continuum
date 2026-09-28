using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public enum ResearchUncertaintyProfile
{
    EstablishedExtension,
    FrontierEngineering,
    ScientificHypothesis,
    HazardousForeignOrAnomalous,
}

public enum ResearchOutcomeKind
{
    Progress,
    Setback,
    PartialSuccess,
    HypothesisSupported,
    HypothesisRefined,
    HypothesisDisproven,
    AnomalousResult,
    HazardIncident,
    SideDiscovery,
}

public sealed record ResearchOutcomeCompetenceGain(double Theoretical, double Experimental, double Engineering);

public sealed record AdaptiveResearchOutcomeRuntimePolicy(
    double SetbackStageProgressLossFraction,
    double PartialSuccessStageProgressCreditFraction,
    double RefinedHypothesisStageProgressPreservedFraction,
    double HighReadinessRiskReductionMaxFraction,
    int MaxSideDiscoveryCandidates,
    int MaxMaterializedSideDiscoveries,
    int MinimumSharedKnowledgeFields,
    int SameSolutionFamilyDepthWindow,
    int MaxRecentOutcomeRecords,
    int MaxPerNodeRecentRecords,
    IReadOnlyDictionary<ResearchUncertaintyProfile, IReadOnlyDictionary<ResearchOutcomeKind, double>> BaseWeights,
    IReadOnlyDictionary<ResearchOutcomeKind, ResearchOutcomeCompetenceGain> CompetenceGains,
    IReadOnlySet<string> FrontierEngineeringNodeIds,
    IReadOnlySet<string> HazardousNodeIds);

public sealed class AdaptiveResearchOutcomeCatalog
{
    private AdaptiveResearchOutcomeCatalog(
        AdaptiveResearchCatalog researchCatalog,
        AdaptiveResearchOutcomeRuntimePolicy policy,
        IReadOnlyDictionary<string, IReadOnlyList<string>> sideDiscoveryCandidatesByNode)
    {
        ResearchCatalog = researchCatalog;
        Policy = policy;
        SideDiscoveryCandidatesByNode = sideDiscoveryCandidatesByNode;
    }

    public AdaptiveResearchCatalog ResearchCatalog { get; }
    public AdaptiveResearchOutcomeRuntimePolicy Policy { get; }
    public IReadOnlyDictionary<string, IReadOnlyList<string>> SideDiscoveryCandidatesByNode { get; }

    public ResearchUncertaintyProfile GetProfile(AdaptiveResearchNodeDefinition node)
    {
        if (node.IsHypothesis)
            return ResearchUncertaintyProfile.ScientificHypothesis;
        if (Policy.HazardousNodeIds.Contains(node.Id))
            return ResearchUncertaintyProfile.HazardousForeignOrAnomalous;
        if (Policy.FrontierEngineeringNodeIds.Contains(node.Id))
            return ResearchUncertaintyProfile.FrontierEngineering;
        return ResearchUncertaintyProfile.EstablishedExtension;
    }

    public static AdaptiveResearchOutcomeCatalog LoadFromDirectory(string rootPath, AdaptiveResearchCatalog researchCatalog)
    {
        using var policyDoc = Load(rootPath, "research_outcome_runtime_policy.json");
        using var maturationDoc = Load(rootPath, "maturation_model.json");
        using var indexDoc = Load(rootPath, "index.json");
        ValidateCatalogId(policyDoc.RootElement, researchCatalog.Metadata.CatalogId, "research_outcome_runtime_policy.json");
        ValidateCatalogId(maturationDoc.RootElement, researchCatalog.Metadata.CatalogId, "maturation_model.json");

        var allowedByProfile = ParseMaturationProfiles(maturationDoc.RootElement);
        var baseWeights = ParseWeights(policyDoc.RootElement, allowedByProfile);
        var effects = policyDoc.RootElement.GetProperty("effects");
        var competenceGains = new Dictionary<ResearchOutcomeKind, ResearchOutcomeCompetenceGain>();
        foreach (var property in effects.GetProperty("outcome_competence_gain").EnumerateObject())
        {
            var values = property.Value.EnumerateArray().Select(value => value.GetDouble()).ToArray();
            if (values.Length != 3)
                throw new InvalidDataException($"Outcome competence gain '{property.Name}' must contain three values.");
            competenceGains.Add(ParseOutcome(property.Name), new ResearchOutcomeCompetenceGain(values[0], values[1], values[2]));
        }

        var assignment = policyDoc.RootElement.GetProperty("profile_assignment");
        var frontier = StringArray(assignment.GetProperty("frontier_engineering_node_ids")).ToHashSet(StringComparer.Ordinal);
        var hazardous = StringArray(assignment.GetProperty("hazardous_node_ids")).ToHashSet(StringComparer.Ordinal);
        foreach (var nodeId in frontier.Concat(hazardous))
            if (!researchCatalog.Nodes.ContainsKey(nodeId))
                throw new InvalidDataException($"Outcome policy references unknown node '{nodeId}'.");

        var readiness = policyDoc.RootElement.GetProperty("readiness_effects");
        var side = policyDoc.RootElement.GetProperty("side_discovery");
        var history = policyDoc.RootElement.GetProperty("history");
        var policy = new AdaptiveResearchOutcomeRuntimePolicy(
            RequiredFraction(effects, "setback_stage_progress_loss_fraction"),
            RequiredFraction(effects, "partial_success_stage_progress_credit_fraction"),
            RequiredFraction(effects, "refined_hypothesis_stage_progress_preserved_fraction"),
            RequiredFraction(readiness, "high_readiness_reduces_setback_and_hazard_weight_max_fraction"),
            side.GetProperty("max_candidates_per_resolution").GetInt32(),
            side.GetProperty("max_materialized_nodes_per_resolution").GetInt32(),
            side.GetProperty("minimum_shared_knowledge_fields").GetInt32(),
            side.GetProperty("same_solution_family_graph_depth_window").GetInt32(),
            history.GetProperty("max_recent_outcome_records").GetInt32(),
            history.GetProperty("max_per_node_recent_records").GetInt32(),
            new ReadOnlyDictionary<ResearchUncertaintyProfile, IReadOnlyDictionary<ResearchOutcomeKind, double>>(baseWeights),
            new ReadOnlyDictionary<ResearchOutcomeKind, ResearchOutcomeCompetenceGain>(competenceGains),
            frontier,
            hazardous);

        var alternatives = ParseAlternativeNeighbors(indexDoc.RootElement, researchCatalog);
        var sideIndex = BuildSideDiscoveryIndex(researchCatalog, alternatives, policy);
        return new AdaptiveResearchOutcomeCatalog(researchCatalog, policy, sideIndex);
    }

    private static Dictionary<ResearchUncertaintyProfile, HashSet<ResearchOutcomeKind>> ParseMaturationProfiles(JsonElement root)
    {
        var result = new Dictionary<ResearchUncertaintyProfile, HashSet<ResearchOutcomeKind>>();
        foreach (var property in root.GetProperty("uncertainty_profiles").EnumerateObject())
        {
            var profile = ParseProfile(property.Name);
            result[profile] = property.Value.GetProperty("normal_outcomes").EnumerateArray()
                .Select(value => ParseOutcome(value.GetString() ?? ""))
                .ToHashSet();
        }
        return result;
    }

    private static Dictionary<ResearchUncertaintyProfile, IReadOnlyDictionary<ResearchOutcomeKind, double>> ParseWeights(
        JsonElement root,
        IReadOnlyDictionary<ResearchUncertaintyProfile, HashSet<ResearchOutcomeKind>> allowedByProfile)
    {
        var result = new Dictionary<ResearchUncertaintyProfile, IReadOnlyDictionary<ResearchOutcomeKind, double>>();
        foreach (var property in root.GetProperty("public_seed_weights").EnumerateObject())
        {
            var profile = ParseProfile(property.Name);
            var weights = new Dictionary<ResearchOutcomeKind, double>();
            foreach (var weight in property.Value.EnumerateObject())
            {
                var outcome = ParseOutcome(weight.Name);
                var value = weight.Value.GetDouble();
                if (value <= 0 || double.IsNaN(value) || double.IsInfinity(value))
                    throw new InvalidDataException($"Invalid outcome weight {value} for {property.Name}/{weight.Name}.");
                if (!allowedByProfile.TryGetValue(profile, out var allowed) || !allowed.Contains(outcome))
                    throw new InvalidDataException($"Outcome '{weight.Name}' is not allowed by maturation profile '{property.Name}'.");
                weights.Add(outcome, value);
            }
            result.Add(profile, new ReadOnlyDictionary<ResearchOutcomeKind, double>(weights));
        }
        return result;
    }

    private static Dictionary<string, HashSet<string>> ParseAlternativeNeighbors(JsonElement indexRoot, AdaptiveResearchCatalog catalog)
    {
        var result = catalog.Nodes.Keys.ToDictionary(id => id, _ => new HashSet<string>(StringComparer.Ordinal), StringComparer.Ordinal);
        foreach (var set in indexRoot.GetProperty("alternative_solution_sets").EnumerateArray())
        {
            var nodes = StringArray(set.GetProperty("candidate_nodes")).Where(catalog.Nodes.ContainsKey).ToArray();
            foreach (var nodeId in nodes)
                foreach (var neighbor in nodes)
                    if (!string.Equals(nodeId, neighbor, StringComparison.Ordinal))
                        result[nodeId].Add(neighbor);
        }
        return result;
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<string>> BuildSideDiscoveryIndex(
        AdaptiveResearchCatalog catalog,
        IReadOnlyDictionary<string, HashSet<string>> alternativeNeighbors,
        AdaptiveResearchOutcomeRuntimePolicy policy)
    {
        var result = new Dictionary<string, IReadOnlyList<string>>(StringComparer.Ordinal);
        foreach (var source in catalog.Nodes.Values)
        {
            var ordered = new List<string>();
            var seen = new HashSet<string>(StringComparer.Ordinal) { source.Id };
            void Add(IEnumerable<string> ids)
            {
                foreach (var id in ids)
                {
                    if (ordered.Count >= policy.MaxSideDiscoveryCandidates)
                        return;
                    if (catalog.Nodes.TryGetValue(id, out var candidate) && candidate.PublicNormalResearch && seen.Add(id))
                        ordered.Add(id);
                }
            }

            if (catalog.ChildrenByPrerequisite.TryGetValue(source.Id, out var children))
                Add(children.OrderBy(id => catalog.Nodes[id].GraphDepth).ThenBy(id => id, StringComparer.Ordinal));
            if (alternativeNeighbors.TryGetValue(source.Id, out var alternatives))
                Add(alternatives.OrderBy(id => catalog.Nodes[id].GraphDepth).ThenBy(id => id, StringComparer.Ordinal));
            Add(catalog.Nodes.Values
                .Where(candidate => string.Equals(candidate.SolutionFamily, source.SolutionFamily, StringComparison.Ordinal) &&
                    Math.Abs(candidate.GraphDepth - source.GraphDepth) <= policy.SameSolutionFamilyDepthWindow)
                .OrderBy(candidate => candidate.GraphDepth).ThenBy(candidate => candidate.Id, StringComparer.Ordinal)
                .Select(candidate => candidate.Id));
            Add(catalog.Nodes.Values
                .Where(candidate => candidate.KnowledgeFields.Intersect(source.KnowledgeFields, StringComparer.Ordinal).Count() >= policy.MinimumSharedKnowledgeFields)
                .OrderBy(candidate => Math.Abs(candidate.GraphDepth - source.GraphDepth))
                .ThenBy(candidate => candidate.Id, StringComparer.Ordinal)
                .Select(candidate => candidate.Id));

            result.Add(source.Id, ordered.AsReadOnly());
        }
        return new ReadOnlyDictionary<string, IReadOnlyList<string>>(result);
    }

    public static ResearchOutcomeKind ParseOutcome(string id) => id switch
    {
        "progress" => ResearchOutcomeKind.Progress,
        "setback" => ResearchOutcomeKind.Setback,
        "partial_success" => ResearchOutcomeKind.PartialSuccess,
        "hypothesis_supported" => ResearchOutcomeKind.HypothesisSupported,
        "hypothesis_refined" => ResearchOutcomeKind.HypothesisRefined,
        "hypothesis_disproven" => ResearchOutcomeKind.HypothesisDisproven,
        "anomalous_result" => ResearchOutcomeKind.AnomalousResult,
        "hazard_incident" => ResearchOutcomeKind.HazardIncident,
        "side_discovery" => ResearchOutcomeKind.SideDiscovery,
        _ => throw new InvalidDataException($"Unknown research outcome '{id}'."),
    };

    public static string OutcomeId(ResearchOutcomeKind value) => value switch
    {
        ResearchOutcomeKind.Progress => "progress",
        ResearchOutcomeKind.Setback => "setback",
        ResearchOutcomeKind.PartialSuccess => "partial_success",
        ResearchOutcomeKind.HypothesisSupported => "hypothesis_supported",
        ResearchOutcomeKind.HypothesisRefined => "hypothesis_refined",
        ResearchOutcomeKind.HypothesisDisproven => "hypothesis_disproven",
        ResearchOutcomeKind.AnomalousResult => "anomalous_result",
        ResearchOutcomeKind.HazardIncident => "hazard_incident",
        ResearchOutcomeKind.SideDiscovery => "side_discovery",
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };

    private static ResearchUncertaintyProfile ParseProfile(string id) => id switch
    {
        "established_extension" => ResearchUncertaintyProfile.EstablishedExtension,
        "frontier_engineering" => ResearchUncertaintyProfile.FrontierEngineering,
        "scientific_hypothesis" => ResearchUncertaintyProfile.ScientificHypothesis,
        "hazardous_foreign_or_anomalous" => ResearchUncertaintyProfile.HazardousForeignOrAnomalous,
        _ => throw new InvalidDataException($"Unknown uncertainty profile '{id}'."),
    };

    private static JsonDocument Load(string rootPath, string fileName) => JsonDocument.Parse(File.ReadAllText(Path.Combine(rootPath, fileName)));
    private static void ValidateCatalogId(JsonElement root, string expected, string fileName)
    {
        var value = root.GetProperty("catalog_id").GetString();
        if (!string.Equals(value, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{fileName} catalog_id '{value}' != '{expected}'.");
    }
    private static string[] StringArray(JsonElement element) => element.EnumerateArray().Select(value => value.GetString() ?? throw new InvalidDataException("Expected string array value.")).ToArray();
    private static double RequiredFraction(JsonElement root, string name)
    {
        var value = root.GetProperty(name).GetDouble();
        if (value < 0 || value > 1 || double.IsNaN(value) || double.IsInfinity(value))
            throw new InvalidDataException($"{name} must be within 0..1.");
        return value;
    }
}
