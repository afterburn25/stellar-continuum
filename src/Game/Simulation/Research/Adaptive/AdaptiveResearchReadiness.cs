using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchReadinessBreakdown(
    string NodeId,
    ResearchMaturity Stage,
    string? TargetApplicabilityContextId,
    double FieldCompetenceScore,
    double FacilityReadinessScore,
    double? EvidenceReadinessScore,
    double? TacitExpertiseScore,
    double OverallReadinessScore,
    double RpEfficiency,
    IReadOnlyList<string> Explanations);

/// <summary>
/// Authoritative Project Readiness calculation. Pressure is deliberately absent: Pressure controls
/// recognition/availability where configured and never doubles as a research-speed bonus.
/// </summary>
public sealed class AdaptiveResearchReadinessCalculator
{
    private readonly AdaptiveResearchCatalog _catalog;
    private readonly AdaptiveResearchFacilityCatalog _facilities;
    private readonly AdaptiveResearchExpertiseCatalog _expertiseCatalog;
    private readonly AdaptiveResearchProgressPolicy _progressPolicy;

    public AdaptiveResearchReadinessCalculator(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchFacilityCatalog facilities,
        AdaptiveResearchExpertiseCatalog expertiseCatalog,
        AdaptiveResearchProgressPolicy progressPolicy)
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _facilities = facilities ?? throw new ArgumentNullException(nameof(facilities));
        _expertiseCatalog = expertiseCatalog ?? throw new ArgumentNullException(nameof(expertiseCatalog));
        _progressPolicy = progressPolicy ?? throw new ArgumentNullException(nameof(progressPolicy));
    }

    public ResearchReadinessBreakdown Calculate(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchExpertiseState expertise,
        string nodeId,
        ResearchMaturity stage,
        double assignedEffectiveLabs,
        string? targetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(expertise);
        if (assignedEffectiveLabs <= 0 || double.IsNaN(assignedEffectiveLabs) || double.IsInfinity(assignedEffectiveLabs))
            throw new ArgumentOutOfRangeException(nameof(assignedEffectiveLabs));
        if (!_expertiseCatalog.StageWeights.TryGetValue(stage, out var stageWeights))
            throw new ArgumentException($"Readiness is only defined for directed research stages, not '{stage}'.", nameof(stage));

        var node = _catalog.GetNode(nodeId);
        var fieldScore = CalculateFieldCompetence(expertise, node, stageWeights);
        var facilityScore = CalculateFacilityReadiness(expertise, node, stage, assignedEffectiveLabs, targetApplicabilityContextId);
        var evidenceScore = CalculateEvidenceReadiness(state, node, targetApplicabilityContextId);
        var tacitScore = CalculateTacitReadiness(expertise, node, stage, stageWeights, targetApplicabilityContextId);

        var weights = _expertiseCatalog.ReadinessWeights;
        var weightedTotal = (fieldScore * weights.FieldCompetence) + (facilityScore * weights.FacilityReadiness);
        var appliedWeight = weights.FieldCompetence + weights.FacilityReadiness;

        if (evidenceScore is double evidence)
        {
            weightedTotal += evidence * weights.EvidenceReadiness;
            appliedWeight += weights.EvidenceReadiness;
        }

        if (tacitScore is double tacit)
        {
            weightedTotal += tacit * weights.TacitExpertise;
            appliedWeight += weights.TacitExpertise;
        }

        var readiness = appliedWeight <= 0 ? 0.0 : Math.Clamp(weightedTotal / appliedWeight, 0.0, 100.0);
        var efficiency = _progressPolicy.GetReadinessEfficiency(readiness);
        var explanations = new List<string>
        {
            $"Field competence readiness {fieldScore:0.#}/100.",
            $"Facility readiness {facilityScore:0.#}/100 for {assignedEffectiveLabs:0.##} assigned Effective Research Labs.",
        };
        if (evidenceScore is double evidenceValue)
            explanations.Add($"Required evidence readiness {evidenceValue:0.#}/100.");
        if (tacitScore is double tacitValue)
            explanations.Add($"Relevant tacit expertise readiness {tacitValue:0.#}/100.");
        explanations.Add($"Overall Project Readiness {readiness:0.#}/100 -> RP efficiency {efficiency:0.##}x.");

        return new ResearchReadinessBreakdown(
            nodeId,
            stage,
            targetApplicabilityContextId,
            fieldScore,
            facilityScore,
            evidenceScore,
            tacitScore,
            readiness,
            efficiency,
            explanations);
    }

    private double CalculateFieldCompetence(
        AdaptiveResearchExpertiseState expertise,
        AdaptiveResearchNodeDefinition node,
        ResearchStageCompetenceWeights weights)
    {
        if (node.KnowledgeFields.Count == 0)
            return 0.0;

        var perField = new List<double>(node.KnowledgeFields.Count);
        foreach (var fieldId in node.KnowledgeFields)
        {
            var field = expertise.GetField(fieldId).Current;
            var score = (field.Theoretical * weights.Theoretical) +
                        (field.Experimental * weights.Experimental) +
                        (field.Engineering * weights.Engineering);
            perField.Add(Math.Clamp(score, 0.0, 100.0));
        }

        var weakest = perField.Min();
        var mean = perField.Average();
        return Math.Clamp((0.60 * weakest) + (0.40 * mean), 0.0, 100.0);
    }

    private double CalculateFacilityReadiness(
        AdaptiveResearchExpertiseState expertise,
        AdaptiveResearchNodeDefinition node,
        ResearchMaturity stage,
        double assignedLabs,
        string? targetContextId)
    {
        var policy = _expertiseCatalog.RuntimePolicy;
        var matchingUnits = 0.0;
        var stageRequirement = _facilities.GetStageRequirement(node.Id, stage);

        foreach (var institution in expertise.Institutions.Values)
        {
            if (!institution.IsActive)
                continue;
            if (targetContextId is not null && institution.ContextId is not null &&
                !string.Equals(institution.ContextId, targetContextId, StringComparison.Ordinal))
                continue;

            var definition = _expertiseCatalog.Institutions[institution.InstitutionArchetypeId];
            var units = definition.EffectiveLabUnits * institution.ActiveCount;
            double factor;
            if (definition.SpecializedFieldIds.Count == 0)
                factor = policy.GeneralLabMatchingFactor;
            else if (definition.SpecializedFieldIds.Any(node.KnowledgeFields.Contains))
                factor = policy.SpecializedMatchingFactor;
            else
                factor = policy.NonmatchingSpecialistFactor;

            if (stageRequirement is not null &&
                definition.FacilityCapabilityIds.Any(capability =>
                    stageRequirement.AllOf.Contains(capability) || stageRequirement.AnyOf.Contains(capability)))
                factor = Math.Max(factor, policy.SpecializedMatchingFactor);

            matchingUnits += units * factor;
        }

        return 100.0 * Math.Min(1.0, matchingUnits / Math.Max(assignedLabs, 1.0));
    }

    private double? CalculateEvidenceReadiness(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        string? targetContextId)
    {
        var requiredTypes = node.Applicability.EvidenceTypes
            .Concat(node.ProjectRequirements.RequiredEvidence)
            .Distinct(StringComparer.Ordinal)
            .ToHashSet(StringComparer.Ordinal);
        if (requiredTypes.Count == 0)
            return null;

        var relevant = state.EvidenceInstances.Values
            .Where(evidence => requiredTypes.Contains(evidence.EvidenceTypeId))
            .Where(evidence => targetContextId is null || evidence.ContextId is null ||
                string.Equals(evidence.ContextId, targetContextId, StringComparison.Ordinal))
            .Select(evidence => 100.0 * evidence.Quality * evidence.Confidence)
            .ToArray();
        return relevant.Length == 0 ? 0.0 : Math.Clamp(relevant.Average(), 0.0, 100.0);
    }

    private double? CalculateTacitReadiness(
        AdaptiveResearchExpertiseState expertise,
        AdaptiveResearchNodeDefinition node,
        ResearchMaturity stage,
        ResearchStageCompetenceWeights stageWeights,
        string? targetContextId)
    {
        var relevant = new List<double>();
        foreach (var asset in expertise.TacitAssets.Values)
        {
            if (targetContextId is not null && asset.ContextId is not null &&
                !string.Equals(asset.ContextId, targetContextId, StringComparison.Ordinal))
                continue;
            if (!IsTacitAssetRelevant(asset, node, stage))
                continue;

            var type = _expertiseCatalog.TacitAssetTypes[asset.AssetTypeId];
            if (!type.SupportedComponents.Any(component => StageComponentWeight(stageWeights, component) > 0.0))
                continue;

            var assimilation = _expertiseCatalog.RuntimePolicy.TacitAssimilationFactors[asset.AssimilationStage];
            var score = asset.Depth * assimilation * asset.Availability * asset.TranslationContextQuality * asset.TrainingContinuity;
            relevant.Add(Math.Clamp(score, 0.0, 100.0));
        }

        var tacitIsMaterial = node.KnowledgeFields.Contains("xenoscience", StringComparer.Ordinal) || relevant.Count > 0;
        if (!tacitIsMaterial)
            return null;
        if (relevant.Count == 0)
            return 0.0;

        var best = relevant.Max();
        var mean = relevant.Average();
        var policy = _expertiseCatalog.RuntimePolicy;
        return Math.Clamp((policy.TacitBestWeight * best) + (policy.TacitMeanWeight * mean), 0.0, 100.0);
    }

    private bool IsTacitAssetRelevant(
        ResearchTacitAssetRuntimeState asset,
        AdaptiveResearchNodeDefinition node,
        ResearchMaturity stage)
    {
        return asset.ScopeKind switch
        {
            ResearchTacitScopeKind.KnowledgeField => node.KnowledgeFields.Contains(asset.ScopeRef, StringComparer.Ordinal),
            ResearchTacitScopeKind.TechnologyNode => string.Equals(asset.ScopeRef, node.Id, StringComparison.Ordinal),
            ResearchTacitScopeKind.SolutionFamily => string.Equals(asset.ScopeRef, node.SolutionFamily, StringComparison.Ordinal),
            ResearchTacitScopeKind.ForeignLineage => node.KnowledgeFields.Contains("xenoscience", StringComparer.Ordinal),
            ResearchTacitScopeKind.FacilityOrProcess => FacilityOrProcessMatches(asset.ScopeRef, node, stage),
            _ => false,
        };
    }

    private bool FacilityOrProcessMatches(string scopeRef, AdaptiveResearchNodeDefinition node, ResearchMaturity stage)
    {
        if (string.Equals(scopeRef, node.Id, StringComparison.Ordinal))
            return true;
        var requirement = _facilities.GetStageRequirement(node.Id, stage);
        return requirement is not null &&
               (requirement.AllOf.Contains(scopeRef, StringComparer.Ordinal) ||
                requirement.AnyOf.Contains(scopeRef, StringComparer.Ordinal));
    }

    private static double StageComponentWeight(ResearchStageCompetenceWeights weights, ResearchCompetenceComponent component) =>
        component switch
        {
            ResearchCompetenceComponent.Theoretical => weights.Theoretical,
            ResearchCompetenceComponent.Experimental => weights.Experimental,
            ResearchCompetenceComponent.Engineering => weights.Engineering,
            _ => 0.0,
        };
}
