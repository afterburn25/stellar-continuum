using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Authoritative mutation/service layer for sparse scientific competence, aggregate research institutions,
/// tacit expertise, practice growth and low-frequency competence atrophy.
/// </summary>
public sealed class AdaptiveResearchExpertiseService
{
    private readonly AdaptiveResearchCatalog _catalog;
    private readonly AdaptiveResearchExpertiseCatalog _expertiseCatalog;
    private readonly AdaptiveResearchReadinessCalculator _readiness;

    public AdaptiveResearchExpertiseService(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchExpertiseCatalog expertiseCatalog,
        AdaptiveResearchReadinessCalculator readiness)
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _expertiseCatalog = expertiseCatalog ?? throw new ArgumentNullException(nameof(expertiseCatalog));
        _readiness = readiness ?? throw new ArgumentNullException(nameof(readiness));
    }

    public ResearchReadinessBreakdown CalculateProjectReadiness(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        ResearchMaturity stage,
        double assignedEffectiveLabs,
        string? targetApplicabilityContextId = null) =>
        _readiness.Calculate(state, state.Expertise, nodeId, stage, assignedEffectiveLabs, targetApplicabilityContextId);

    public void SeedFieldCompetence(
        AdaptiveResearchCivilizationState state,
        string fieldId,
        ResearchCompetenceVector value,
        double activityYear = 0.0)
    {
        ValidateField(fieldId);
        var current = state.Expertise.GetField(fieldId);
        var seeded = new ResearchCompetenceVector(
            Math.Max(current.Current.Theoretical, value.Theoretical),
            Math.Max(current.Current.Experimental, value.Experimental),
            Math.Max(current.Current.Engineering, value.Engineering));
        var peak = new ResearchCompetenceVector(
            Math.Max(current.HistoricalPeak.Theoretical, seeded.Theoretical),
            Math.Max(current.HistoricalPeak.Experimental, seeded.Experimental),
            Math.Max(current.HistoricalPeak.Engineering, seeded.Engineering));
        state.Expertise.SetField(new ResearchFieldCompetenceRuntimeState(
            fieldId,
            seeded,
            peak,
            Math.Max(current.LastTheoreticalActivityYear, activityYear),
            Math.Max(current.LastExperimentalActivityYear, activityYear),
            Math.Max(current.LastEngineeringActivityYear, activityYear),
            state.Expertise.Revision + 1));
        state.MarkViewDirty();
    }

    public void SetInstitution(
        AdaptiveResearchCivilizationState state,
        string institutionInstanceId,
        string institutionArchetypeId,
        int totalCount,
        int activeCount,
        string? contextId = null)
    {
        if (!_expertiseCatalog.Institutions.ContainsKey(institutionArchetypeId))
            throw new ArgumentException($"Unknown research institution archetype '{institutionArchetypeId}'.", nameof(institutionArchetypeId));
        state.Expertise.SetInstitution(new ResearchInstitutionRuntimeState(
            institutionInstanceId,
            institutionArchetypeId,
            contextId,
            totalCount,
            activeCount,
            state.Expertise.Revision + 1));
        SynchronizeInstitutionCapacity(state);
        state.MarkViewDirty();
    }

    public bool RemoveInstitution(AdaptiveResearchCivilizationState state, string institutionInstanceId)
    {
        var changed = state.Expertise.RemoveInstitution(institutionInstanceId);
        if (!changed)
            return false;
        SynchronizeInstitutionCapacity(state);
        state.MarkViewDirty();
        return true;
    }

    public void SetTacitAsset(
        AdaptiveResearchCivilizationState state,
        string assetId,
        string assetTypeId,
        ResearchTacitScopeKind scopeKind,
        string scopeRef,
        ResearchTacitAssimilationStage assimilationStage,
        double depth,
        double availability,
        double translationContextQuality,
        double trainingContinuity,
        string provenance,
        string? contextId = null)
    {
        if (!_expertiseCatalog.TacitAssetTypes.ContainsKey(assetTypeId))
            throw new ArgumentException($"Unknown tacit knowledge asset type '{assetTypeId}'.", nameof(assetTypeId));
        ValidateTacitScope(scopeKind, scopeRef);
        state.Expertise.SetTacitAsset(new ResearchTacitAssetRuntimeState(
            assetId,
            assetTypeId,
            scopeKind,
            scopeRef,
            assimilationStage,
            depth,
            availability,
            translationContextQuality,
            trainingContinuity,
            provenance,
            contextId,
            state.Expertise.Revision + 1));
        state.MarkViewDirty();
    }

    public bool RemoveTacitAsset(AdaptiveResearchCivilizationState state, string assetId)
    {
        var changed = state.Expertise.RemoveTacitAsset(assetId);
        if (changed)
            state.MarkViewDirty();
        return changed;
    }

    public void ApplyCompletedStagePractice(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        ResearchMaturity completedStage,
        double activityYear)
    {
        if (!_expertiseCatalog.RuntimePolicy.StagePracticeGain.TryGetValue(completedStage, out var rawGain))
            return;
        var node = _catalog.GetNode(nodeId);
        if (node.KnowledgeFields.Count == 0)
            return;

        var fieldCount = node.KnowledgeFields.Count;
        foreach (var fieldId in node.KnowledgeFields)
        {
            ApplyGain(state, fieldId, Divide(rawGain, fieldCount), activityYear);

            var relatedGain = Multiply(Divide(rawGain, fieldCount), _expertiseCatalog.RuntimePolicy.RelatedFieldTransferFraction);
            foreach (var relatedFieldId in _expertiseCatalog.Fields[fieldId].RelatedFieldIds)
                ApplyGain(state, relatedFieldId, relatedGain, activityYear);
        }
        state.MarkViewDirty();
    }

    /// <summary>
    /// Low-frequency maintenance only. Mature scientific knowledge is never removed; this updates active practice.
    /// </summary>
    public void ApplyCompetenceAtrophy(
        AdaptiveResearchCivilizationState state,
        double currentYear,
        double elapsedYears)
    {
        if (elapsedYears <= 0.0 || double.IsNaN(elapsedYears) || double.IsInfinity(elapsedYears))
            return;

        var policy = _expertiseCatalog.RuntimePolicy;
        foreach (var pair in state.Expertise.FieldCompetence.ToArray())
        {
            var fieldId = pair.Key;
            var field = pair.Value;
            var floor = CalculatePreservationFloor(state, fieldId, field.HistoricalPeak);
            var theoretical = AtrophyComponent(
                field.Current.Theoretical,
                floor.Theoretical,
                currentYear - field.LastTheoreticalActivityYear,
                policy.AtrophyGraceYears,
                policy.AnnualAtrophyRates.Theoretical,
                elapsedYears);
            var experimental = AtrophyComponent(
                field.Current.Experimental,
                floor.Experimental,
                currentYear - field.LastExperimentalActivityYear,
                policy.AtrophyGraceYears,
                policy.AnnualAtrophyRates.Experimental,
                elapsedYears);
            var engineering = AtrophyComponent(
                field.Current.Engineering,
                floor.Engineering,
                currentYear - field.LastEngineeringActivityYear,
                policy.AtrophyGraceYears,
                policy.AnnualAtrophyRates.Engineering,
                elapsedYears);

            var updated = new ResearchCompetenceVector(theoretical, experimental, engineering);
            if (ApproximatelyEqual(updated, field.Current))
                continue;
            state.Expertise.SetField(field with { Current = updated, Revision = state.Expertise.Revision + 1 });
        }
        state.MarkViewDirty();
    }

    private void SynchronizeInstitutionCapacity(AdaptiveResearchCivilizationState state)
    {
        var active = state.Expertise.Institutions.Values.Where(institution => institution.IsActive).ToArray();
        var totalLabs = active.Sum(institution =>
            _expertiseCatalog.Institutions[institution.InstitutionArchetypeId].EffectiveLabUnits * institution.ActiveCount);
        state.SetTotalEffectiveResearchLabs(totalLabs);

        var capabilities = active
            .SelectMany(institution => _expertiseCatalog.Institutions[institution.InstitutionArchetypeId].FacilityCapabilityIds)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        state.SetFacilityCapabilities(capabilities);
    }

    private void ApplyGain(
        AdaptiveResearchCivilizationState state,
        string fieldId,
        ResearchCompetenceVector rawGain,
        double activityYear)
    {
        ValidateField(fieldId);
        var existing = state.Expertise.GetField(fieldId);
        var policy = _expertiseCatalog.RuntimePolicy;
        var current = new ResearchCompetenceVector(
            ApplyDiminishingGain(existing.Current.Theoretical, rawGain.Theoretical, policy.MinimumGainFactor),
            ApplyDiminishingGain(existing.Current.Experimental, rawGain.Experimental, policy.MinimumGainFactor),
            ApplyDiminishingGain(existing.Current.Engineering, rawGain.Engineering, policy.MinimumGainFactor));
        var peak = new ResearchCompetenceVector(
            Math.Max(existing.HistoricalPeak.Theoretical, current.Theoretical),
            Math.Max(existing.HistoricalPeak.Experimental, current.Experimental),
            Math.Max(existing.HistoricalPeak.Engineering, current.Engineering));
        state.Expertise.SetField(new ResearchFieldCompetenceRuntimeState(
            fieldId,
            current,
            peak,
            rawGain.Theoretical > 0 ? activityYear : existing.LastTheoreticalActivityYear,
            rawGain.Experimental > 0 ? activityYear : existing.LastExperimentalActivityYear,
            rawGain.Engineering > 0 ? activityYear : existing.LastEngineeringActivityYear,
            state.Expertise.Revision + 1));
    }

    private ResearchCompetenceVector CalculatePreservationFloor(
        AdaptiveResearchCivilizationState state,
        string fieldId,
        ResearchCompetenceVector historicalPeak)
    {
        var policy = _expertiseCatalog.RuntimePolicy;
        var theoretical = state.NodeStates.Values.Any(nodeState =>
            nodeState.CountsAsEstablishedKnowledge && _catalog.GetNode(nodeState.NodeId).KnowledgeFields.Contains(fieldId, StringComparer.Ordinal))
            ? historicalPeak.Theoretical * policy.EstablishedKnowledgeTheoreticalFloorFraction
            : 0.0;
        var experimental = 0.0;
        var engineering = 0.0;

        foreach (var asset in state.Expertise.TacitAssets.Values)
        {
            var type = _expertiseCatalog.TacitAssetTypes[asset.AssetTypeId];
            if (!TacitScopeSupportsField(asset, fieldId))
                continue;
            var assimilation = policy.TacitAssimilationFactors[asset.AssimilationStage];
            var score = asset.Depth * assimilation * asset.Availability * asset.TranslationContextQuality * asset.TrainingContinuity * policy.PreservationFloorFraction;
            if (type.SupportedComponents.Contains(ResearchCompetenceComponent.Theoretical))
                theoretical = Math.Max(theoretical, score);
            if (type.SupportedComponents.Contains(ResearchCompetenceComponent.Experimental))
                experimental = Math.Max(experimental, score);
            if (type.SupportedComponents.Contains(ResearchCompetenceComponent.Engineering))
                engineering = Math.Max(engineering, score);
        }

        return new ResearchCompetenceVector(
            Math.Min(theoretical, historicalPeak.Theoretical),
            Math.Min(experimental, historicalPeak.Experimental),
            Math.Min(engineering, historicalPeak.Engineering));
    }

    private bool TacitScopeSupportsField(ResearchTacitAssetRuntimeState asset, string fieldId) => asset.ScopeKind switch
    {
        ResearchTacitScopeKind.KnowledgeField => string.Equals(asset.ScopeRef, fieldId, StringComparison.Ordinal),
        ResearchTacitScopeKind.TechnologyNode => _catalog.Nodes.TryGetValue(asset.ScopeRef, out var node) && node.KnowledgeFields.Contains(fieldId, StringComparer.Ordinal),
        ResearchTacitScopeKind.SolutionFamily => _catalog.Nodes.Values.Any(node =>
            string.Equals(node.SolutionFamily, asset.ScopeRef, StringComparison.Ordinal) && node.KnowledgeFields.Contains(fieldId, StringComparer.Ordinal)),
        ResearchTacitScopeKind.ForeignLineage => string.Equals(fieldId, "xenoscience", StringComparison.Ordinal),
        ResearchTacitScopeKind.FacilityOrProcess => _expertiseCatalog.Institutions.Values.Any(institution =>
            (string.Equals(institution.InstitutionArchetypeId, asset.ScopeRef, StringComparison.Ordinal) ||
             institution.FacilityCapabilityIds.Contains(asset.ScopeRef, StringComparer.Ordinal)) &&
            institution.SpecializedFieldIds.Contains(fieldId, StringComparer.Ordinal)),
        _ => false,
    };

    private void ValidateField(string fieldId)
    {
        if (!_expertiseCatalog.Fields.ContainsKey(fieldId))
            throw new ArgumentException($"Unknown research knowledge field '{fieldId}'.", nameof(fieldId));
    }

    private void ValidateTacitScope(ResearchTacitScopeKind scopeKind, string scopeRef)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(scopeRef);
        switch (scopeKind)
        {
            case ResearchTacitScopeKind.KnowledgeField:
                ValidateField(scopeRef);
                break;
            case ResearchTacitScopeKind.TechnologyNode:
                if (!_catalog.Nodes.ContainsKey(scopeRef))
                    throw new ArgumentException($"Unknown research node '{scopeRef}' for tacit asset scope.", nameof(scopeRef));
                break;
            case ResearchTacitScopeKind.SolutionFamily:
                if (!_catalog.Nodes.Values.Any(node => string.Equals(node.SolutionFamily, scopeRef, StringComparison.Ordinal)))
                    throw new ArgumentException($"Unknown research solution family '{scopeRef}' for tacit asset scope.", nameof(scopeRef));
                break;
            case ResearchTacitScopeKind.FacilityOrProcess:
                if (!_expertiseCatalog.Institutions.ContainsKey(scopeRef) &&
                    !_expertiseCatalog.Institutions.Values.Any(institution => institution.FacilityCapabilityIds.Contains(scopeRef, StringComparer.Ordinal)))
                    throw new ArgumentException($"Unknown research facility/process '{scopeRef}' for tacit asset scope.", nameof(scopeRef));
                break;
        }
    }

    private static ResearchCompetenceVector Divide(ResearchCompetenceVector value, double divisor) =>
        new(value.Theoretical / divisor, value.Experimental / divisor, value.Engineering / divisor);

    private static ResearchCompetenceVector Multiply(ResearchCompetenceVector value, double factor) =>
        new(value.Theoretical * factor, value.Experimental * factor, value.Engineering * factor);

    private static double ApplyDiminishingGain(double current, double rawGain, double minimumFactor)
    {
        if (rawGain <= 0.0)
            return current;
        var factor = Math.Max(minimumFactor, 1.0 - (current / 120.0));
        return Math.Clamp(current + (rawGain * factor), 0.0, 100.0);
    }

    private static double AtrophyComponent(
        double current,
        double floor,
        double inactivityYears,
        double graceYears,
        double annualRate,
        double elapsedYears)
    {
        if (inactivityYears <= graceYears || current <= floor)
            return current;
        return Math.Max(floor, current - (annualRate * elapsedYears));
    }

    private static bool ApproximatelyEqual(ResearchCompetenceVector left, ResearchCompetenceVector right) =>
        Math.Abs(left.Theoretical - right.Theoretical) < 0.000001 &&
        Math.Abs(left.Experimental - right.Experimental) < 0.000001 &&
        Math.Abs(left.Engineering - right.Engineering) < 0.000001;
}
