using System;
using System.Collections.Generic;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Authoritative visible research maturity. Unknown is represented by absence from sparse civilization state.
/// </summary>
public enum ResearchMaturity
{
    Rumored = 1,
    Hypothesized = 2,
    Investigable = 3,
    Experimental = 4,
    Demonstrated = 5,
    Engineering = 6,
    Mature = 7,
    Archived = 8,
}

public enum ResearchCapabilityScope
{
    Civilization,
    PopulationOrSpecies,
    ColonyOrInstallation,
}

public sealed record ResearchNodePrerequisites(
    IReadOnlyList<string> AllOf,
    IReadOnlyList<string> AnyOf);

public sealed record ResearchCapabilityRequirements(
    IReadOnlyList<string> AllOf,
    IReadOnlyList<string> AnyOf,
    string Context);

public sealed record ResearchApplicabilityRequirements(
    IReadOnlyList<string> Traits,
    IReadOnlyList<string> EvidenceTypes);

public sealed record ResearchProjectRequirements(
    double BaseResearchPoints,
    int MinimumLabs,
    int RecommendedLabs,
    IReadOnlyDictionary<string, double> RequiredPressure,
    IReadOnlyDictionary<string, double> RequiredPressureAny,
    IReadOnlyList<string> RequiredEvidence);

public sealed record AdaptiveResearchNodeDefinition(
    string Id,
    string Name,
    string DomainId,
    string Complexity,
    int GraphDepth,
    string SolutionFamily,
    IReadOnlyList<string> KnowledgeFields,
    IReadOnlyList<string> AwarenessSources,
    IReadOnlyList<string> PressureAffinities,
    ResearchNodePrerequisites Prerequisites,
    ResearchApplicabilityRequirements Applicability,
    ResearchCapabilityRequirements CapabilityRequirements,
    IReadOnlyList<string> DeclaredCapabilities,
    bool IsHypothesis,
    bool PublicNormalResearch,
    ResearchProjectRequirements ProjectRequirements);

public sealed record ResearchCapabilityDefinition(
    string Id,
    string Name,
    ResearchCapabilityScope Scope);

public sealed record ResearchCapabilityImplication(
    string FromCapabilityId,
    string ToCapabilityId,
    bool PreserveTargetContext);

public sealed record ResearchLabScaling(
    double AtOrBelowRecommendedEfficiency,
    double AboveRecommendedToTwiceEfficiency,
    double AboveTwiceRecommendedEfficiency)
{
    public double ScaleAssignedLabs(double assignedLabs, double recommendedLabs)
    {
        if (assignedLabs <= 0.0 || recommendedLabs <= 0.0)
            return 0.0;

        var first = Math.Min(assignedLabs, recommendedLabs);
        var result = first * AtOrBelowRecommendedEfficiency;

        if (assignedLabs > recommendedLabs)
        {
            var second = Math.Min(assignedLabs - recommendedLabs, recommendedLabs);
            result += second * AboveRecommendedToTwiceEfficiency;
        }

        if (assignedLabs > 2.0 * recommendedLabs)
            result += (assignedLabs - (2.0 * recommendedLabs)) * AboveTwiceRecommendedEfficiency;

        return result;
    }
}

public sealed record DirectedResearchProgramStage(
    string Id,
    int? DirectedProgramLimit,
    bool LabCapacityOnly,
    string? RequiredTechnologyId);

public sealed record ResearchMaturityGrant(
    IReadOnlyList<string> CapabilityIds,
    IReadOnlyList<string> CivilizationTraitIds,
    string? ResearchCapacityStageId,
    IReadOnlyList<string> EnabledDeploymentEventIds);

public sealed record ResearchDeploymentEventDefinition(
    string Id,
    IReadOnlyList<string> RequiresAnyMatureTechnologyIds,
    IReadOnlyList<string> CivilizationTraitIds);

public sealed record AdaptiveResearchCatalogMetadata(
    int SchemaVersion,
    string CatalogId,
    int DeclaredNodeCount,
    int DomainCount,
    int PressureCount,
    int TraitCount,
    int EvidenceTypeCount,
    int KnowledgeFieldCount,
    int CrossLineageCapabilityCount,
    double BaseRpPerEffectiveLabPerYear,
    string StartingDirectedProgramStageId);
