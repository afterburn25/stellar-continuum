using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchAgendaPriorityDefinition(string Id, int Rank, double Score);
public sealed record ResearchScientificCultureAxisDefinition(string Id, string Name);

public sealed record ResearchAgendaRuntimePolicy(
    string DefaultPriorityId,
    double DefaultBasicVsAppliedOrientation,
    double DefaultCompetencePreservation,
    double DefaultPortfolioDiversity,
    double DefaultForeignScienceEngagement,
    double DefaultCultureAxis,
    double BlockedProjectScoreMultiplier,
    double AlreadyCoveredSolutionValue,
    double NovelSolutionValue,
    double TimeToEffectHalfValueYears,
    double LabCostHalfValueFraction,
    double MinimumAdequacyConfidence,
    double LowRelevantPressureMaximum,
    double HighAdequacyMinimum,
    double ComplacencyDeprioritizeThreshold,
    double ImportantChallengeThreshold,
    double StrategicChallengeThreshold,
    double CriticalChallengeThreshold);

/// <summary>
/// Immutable Research Agenda / scientific-culture / fair-AI planning definitions.
/// </summary>
public sealed class AdaptiveResearchAgendaCatalog
{
    private AdaptiveResearchAgendaCatalog(
        IReadOnlyDictionary<string, ResearchAgendaPriorityDefinition> priorities,
        IReadOnlyDictionary<string, ResearchScientificCultureAxisDefinition> cultureAxes,
        IReadOnlyList<string> utilityComponentIds,
        int shortlistBound,
        ResearchAgendaRuntimePolicy runtimePolicy)
    {
        Priorities = priorities;
        CultureAxes = cultureAxes;
        UtilityComponentIds = utilityComponentIds;
        ShortlistBound = shortlistBound;
        RuntimePolicy = runtimePolicy;
    }

    public IReadOnlyDictionary<string, ResearchAgendaPriorityDefinition> Priorities { get; }
    public IReadOnlyDictionary<string, ResearchScientificCultureAxisDefinition> CultureAxes { get; }
    public IReadOnlyList<string> UtilityComponentIds { get; }
    public int ShortlistBound { get; }
    public ResearchAgendaRuntimePolicy RuntimePolicy { get; }

    public ResearchAgendaPriorityDefinition GetPriority(string id) =>
        Priorities.TryGetValue(id, out var value)
            ? value
            : throw new KeyNotFoundException($"Unknown research agenda priority '{id}'.");

    public static AdaptiveResearchAgendaCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchExpertiseCatalog expertiseCatalog)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentNullException.ThrowIfNull(expertiseCatalog);
        var root = Path.GetFullPath(rootPath);

        using var agendaDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_agenda_model.json")));
        ValidateCatalogId(agendaDoc.RootElement, catalog.Metadata.CatalogId, "research_agenda_model.json");
        var priorities = new Dictionary<string, ResearchAgendaPriorityDefinition>(StringComparer.Ordinal);
        foreach (var element in agendaDoc.RootElement.GetProperty("priority_levels").EnumerateArray())
        {
            var id = RequiredString(element, "id", "research_agenda_model.json");
            var rank = element.GetProperty("rank").GetInt32();
            if (!priorities.TryAdd(id, new ResearchAgendaPriorityDefinition(id, rank, 0)))
                throw new InvalidDataException($"Duplicate research agenda priority '{id}'.");
        }
        if (priorities.Count != 5 || priorities.Values.Select(value => value.Rank).OrderBy(value => value).SequenceEqual(new[] { 0, 1, 2, 3, 4 }) is false)
            throw new InvalidDataException("Research agenda priority levels must contain ranks 0..4 exactly once.");

        using var cultureDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "scientific_culture_model.json")));
        ValidateCatalogId(cultureDoc.RootElement, catalog.Metadata.CatalogId, "scientific_culture_model.json");
        var axes = new Dictionary<string, ResearchScientificCultureAxisDefinition>(StringComparer.Ordinal);
        foreach (var element in cultureDoc.RootElement.GetProperty("axes").EnumerateArray())
        {
            var id = RequiredString(element, "id", "scientific_culture_model.json");
            var range = element.GetProperty("range").EnumerateArray().Select(value => value.GetDouble()).ToArray();
            if (range.Length != 2 || Math.Abs(range[0]) > 0.000001 || Math.Abs(range[1] - 100.0) > 0.000001)
                throw new InvalidDataException($"Scientific culture axis '{id}' must use range 0..100.");
            axes.Add(id, new ResearchScientificCultureAxisDefinition(id, RequiredString(element, "name", id)));
        }
        if (axes.Count != 12)
            throw new InvalidDataException($"Expected 12 scientific-culture axes, found {axes.Count}.");

        using var aiDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_ai_planning_contract.json")));
        ValidateCatalogId(aiDoc.RootElement, catalog.Metadata.CatalogId, "research_ai_planning_contract.json");
        var components = aiDoc.RootElement.GetProperty("visible_candidate_utility_components")
            .EnumerateArray().Select(element => RequiredString(element, "id", "research_ai_planning_contract.json")).ToArray();
        var shortlistBound = aiDoc.RootElement.GetProperty("shortlist_policy").GetProperty("bounded_candidate_count").GetInt32();
        if (shortlistBound <= 0 || shortlistBound > 64)
            throw new InvalidDataException($"Invalid visible research shortlist bound {shortlistBound}.");

        using var policyDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "research_agenda_runtime_policy.json")));
        ValidateCatalogId(policyDoc.RootElement, catalog.Metadata.CatalogId, "research_agenda_runtime_policy.json");
        var defaults = policyDoc.RootElement.GetProperty("defaults");
        var scoreRoot = policyDoc.RootElement.GetProperty("priority_scores");
        foreach (var id in priorities.Keys.ToArray())
        {
            if (!scoreRoot.TryGetProperty(id, out var scoreElement))
                throw new InvalidDataException($"Agenda runtime policy is missing score for priority '{id}'.");
            priorities[id] = priorities[id] with { Score = scoreElement.GetDouble() };
        }

        var ranking = policyDoc.RootElement.GetProperty("visible_candidate_ranking");
        var adequacy = policyDoc.RootElement.GetProperty("perceived_adequacy_review");
        var policy = new ResearchAgendaRuntimePolicy(
            RequiredString(defaults, "priority_level", "research_agenda_runtime_policy.json"),
            defaults.GetProperty("basic_vs_applied_orientation").GetDouble(),
            defaults.GetProperty("competence_preservation_policy").GetDouble(),
            defaults.GetProperty("portfolio_diversity_policy").GetDouble(),
            defaults.GetProperty("foreign_science_engagement").GetDouble(),
            defaults.GetProperty("scientific_culture_axis").GetDouble(),
            ranking.GetProperty("blocked_project_score_multiplier").GetDouble(),
            ranking.GetProperty("already_covered_solution_value").GetDouble(),
            ranking.GetProperty("novel_solution_value").GetDouble(),
            ranking.GetProperty("time_to_effect_half_value_years").GetDouble(),
            ranking.GetProperty("lab_cost_half_value_fraction_of_total_capacity").GetDouble(),
            adequacy.GetProperty("minimum_confidence_for_policy_change").GetDouble(),
            adequacy.GetProperty("low_relevant_pressure_max").GetDouble(),
            adequacy.GetProperty("high_adequacy_min").GetDouble(),
            adequacy.GetProperty("deprioritize_threshold").GetDouble(),
            adequacy.GetProperty("important_challenge_threshold").GetDouble(),
            adequacy.GetProperty("strategic_challenge_threshold").GetDouble(),
            adequacy.GetProperty("critical_challenge_threshold").GetDouble());

        if (!priorities.ContainsKey(policy.DefaultPriorityId))
            throw new InvalidDataException($"Unknown default agenda priority '{policy.DefaultPriorityId}'.");
        foreach (var value in new[]
                 {
                     policy.DefaultBasicVsAppliedOrientation,
                     policy.DefaultCompetencePreservation,
                     policy.DefaultPortfolioDiversity,
                     policy.DefaultForeignScienceEngagement,
                     policy.DefaultCultureAxis,
                 })
            if (value < 0 || value > 100)
                throw new InvalidDataException("Research agenda defaults must remain in 0..100.");

        var domains = catalog.Nodes.Values.Select(node => node.DomainId).ToHashSet(StringComparer.Ordinal);
        if (domains.Count != catalog.Metadata.DomainCount)
            throw new InvalidDataException("Agenda domain discovery does not match catalog metadata.");
        if (expertiseCatalog.Fields.Count != catalog.Metadata.KnowledgeFieldCount)
            throw new InvalidDataException("Agenda knowledge-field catalog mismatch.");

        return new AdaptiveResearchAgendaCatalog(
            new ReadOnlyDictionary<string, ResearchAgendaPriorityDefinition>(priorities),
            new ReadOnlyDictionary<string, ResearchScientificCultureAxisDefinition>(axes),
            components,
            shortlistBound,
            policy);
    }

    private static string RequiredString(JsonElement element, string name, string source) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(value.GetString())
            ? value.GetString()!
            : throw new InvalidDataException($"{source} is missing non-empty string '{name}'.");

    private static void ValidateCatalogId(JsonElement root, string expected, string source)
    {
        var actual = RequiredString(root, "catalog_id", source);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{source} catalog_id '{actual}' does not match '{expected}'.");
    }
}
