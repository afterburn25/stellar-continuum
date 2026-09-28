using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Preferred foreign-contact research coordinator. It delegates factual package/assessment changes
/// to the milestone-16 foreign runtime, then materializes only bounded existing public research
/// candidates as Rumored/Hypothesized/Investigable according to evidence and normal eligibility.
/// </summary>
public sealed class AdaptiveResearchForeignDiscoveryRuntime
{
    private readonly AdaptiveResearchAuthority _authority;
    private readonly AdaptiveResearchForeignTechnologyRuntime _foreignTechnology;
    private readonly AdaptiveResearchForeignDiscoveryCatalog _catalog;

    public AdaptiveResearchForeignDiscoveryRuntime(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchForeignTechnologyRuntime foreignTechnology,
        AdaptiveResearchForeignDiscoveryCatalog catalog)
    {
        _authority = authority ?? throw new ArgumentNullException(nameof(authority));
        _foreignTechnology = foreignTechnology ?? throw new ArgumentNullException(nameof(foreignTechnology));
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
    }

    public (ForeignTechnologyAssessmentRuntimeState Assessment, IReadOnlyList<ForeignResearchAwarenessEvent> AwarenessEvents) Observe(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        string sourceLineageReference,
        double confidence,
        double year,
        IEnumerable<string>? knownConstraintIds = null,
        string? targetApplicabilityContextId = null)
    {
        var assessment = _foreignTechnology.Observe(
            state,
            foreignTechnologyReference,
            sourceLineageReference,
            confidence,
            year,
            knownConstraintIds);
        return (assessment, Reevaluate(state, foreignTechnologyReference, targetApplicabilityContextId));
    }

    public (ForeignTechnologyAssessmentRuntimeState Assessment, IReadOnlyList<ForeignResearchAwarenessEvent> AwarenessEvents) AcquirePackage(
        AdaptiveResearchCivilizationState state,
        ForeignTechnologyPackageInput input)
    {
        var assessment = _foreignTechnology.AcquirePackage(state, input);
        return (assessment, Reevaluate(state, input.ForeignTechnologyReference, input.TargetApplicabilityContextId));
    }

    public (ForeignTechnologyAssessmentRuntimeState Assessment, IReadOnlyList<ForeignResearchAwarenessEvent> AwarenessEvents) RecordAnalysisResult(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignUnderstandingState targetUnderstanding,
        double confidence,
        double year,
        IEnumerable<string>? newlyKnownConstraintIds = null,
        string? targetApplicabilityContextId = null)
    {
        var assessment = _foreignTechnology.RecordAnalysisResult(
            state,
            foreignTechnologyReference,
            targetUnderstanding,
            confidence,
            year,
            newlyKnownConstraintIds);
        return (assessment, Reevaluate(state, foreignTechnologyReference, targetApplicabilityContextId));
    }

    public (ForeignTechnologyAssessmentRuntimeState Assessment, IReadOnlyList<ForeignResearchAwarenessEvent> AwarenessEvents) RecordAdaptationResult(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignAdaptationState adaptation,
        double confidence,
        double year,
        string? targetApplicabilityContextId = null)
    {
        var assessment = _foreignTechnology.RecordAdaptationResult(
            state,
            foreignTechnologyReference,
            adaptation,
            confidence,
            year);
        return (assessment, Reevaluate(state, foreignTechnologyReference, targetApplicabilityContextId));
    }

    public IReadOnlyList<ForeignResearchAwarenessEvent> Reevaluate(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        string? targetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        var foreign = _foreignTechnology.GetState(state);
        if (!foreign.Assessments.TryGetValue(foreignTechnologyReference, out var assessment))
            throw new KeyNotFoundException($"Foreign technology '{foreignTechnologyReference}' has not been legitimately observed/acquired.");

        var desired = new Dictionary<string, (ResearchMaturity State, string Reason)>(StringComparer.Ordinal);
        var evidenceAwareness = assessment.Understanding >= ForeignUnderstandingState.Characterized
            ? _catalog.CharacterizedEvidenceState
            : _catalog.ObservedEvidenceState;

        foreach (var evidenceRef in assessment.EvidenceRefs)
        {
            if (!state.EvidenceInstances.TryGetValue(evidenceRef, out var evidence))
                continue;
            if (!_authority.Catalog.NodesByEvidence.TryGetValue(evidence.EvidenceTypeId, out var candidates))
                continue;
            foreach (var nodeId in candidates)
            {
                var node = _authority.Catalog.GetNode(nodeId);
                if (!AllowsDirectForeignAwareness(node))
                    continue;
                MergeDesired(desired, nodeId, evidenceAwareness, $"Legitimate foreign evidence '{evidence.EvidenceTypeId}' supports scientific awareness.");
            }
        }

        foreach (var rule in _catalog.MethodRules)
        {
            var currentRank = rule.TriggerAxis switch
            {
                ForeignDiscoveryTriggerAxis.Understanding => (int)assessment.Understanding,
                ForeignDiscoveryTriggerAxis.Adaptation => (int)assessment.Adaptation,
                _ => 0,
            };
            if (currentRank < rule.MinimumAxisRank)
                continue;
            foreach (var nodeId in rule.NodeIds)
                MergeDesired(desired, nodeId, rule.AwarenessState, $"Foreign {rule.TriggerAxis.ToString().ToLowerInvariant()} now supports a cross-lineage research method hypothesis.");
        }

        var events = new List<ForeignResearchAwarenessEvent>();
        foreach (var pair in desired.OrderBy(pair => _authority.Catalog.GetNode(pair.Key).GraphDepth).ThenBy(pair => pair.Key, StringComparer.Ordinal))
        {
            var node = _authority.Catalog.GetNode(pair.Key);
            if (!node.PublicNormalResearch)
                continue;

            var scientific = _authority.Kernel.Eligibility.EvaluateScientificEligibility(
                state,
                node.Id,
                targetApplicabilityContextId);
            var target = scientific.Allowed ? ResearchMaturity.Investigable : pair.Value.State;
            if (target > ResearchMaturity.Investigable)
                throw new InvalidOperationException("Foreign discovery may not directly materialize Experimental-or-higher native research state.");

            state.TryGetNodeState(node.Id, out var existing);
            if (existing is not null)
            {
                if (existing.Maturity is ResearchMaturity.Experimental or ResearchMaturity.Demonstrated or ResearchMaturity.Engineering or ResearchMaturity.Mature)
                    continue;
                if (existing.CountsAsEstablishedKnowledge)
                    continue;
                if (existing.Maturity == ResearchMaturity.Archived &&
                    !string.Equals(existing.Resolution, "disproven", StringComparison.OrdinalIgnoreCase))
                    continue;
                if (existing.Maturity != ResearchMaturity.Archived && existing.Maturity >= target)
                    continue;
            }

            var previous = existing?.Maturity;
            var resolution = target < ResearchMaturity.Investigable
                ? $"foreign_awareness:{foreignTechnologyReference}"
                : null;
            state.SetNodeState(new ResearchNodeRuntimeState(
                node.Id,
                target,
                resolution,
                0.0,
                existing?.TotalResearchPoints ?? 0.0,
                state.Revision + 1));
            events.Add(new ForeignResearchAwarenessEvent(
                foreignTechnologyReference,
                node.Id,
                previous,
                target,
                scientific.Allowed
                    ? "Foreign contact supplied awareness/evidence and all normal native scientific requirements are now satisfied."
                    : pair.Value.Reason));
        }

        return events;
    }

    private static bool AllowsDirectForeignAwareness(AdaptiveResearchNodeDefinition node) =>
        node.AwarenessSources.Contains("foreign_contact", StringComparer.Ordinal) ||
        node.AwarenessSources.Contains("observation", StringComparer.Ordinal);

    private static void MergeDesired(
        IDictionary<string, (ResearchMaturity State, string Reason)> desired,
        string nodeId,
        ResearchMaturity state,
        string reason)
    {
        if (desired.TryGetValue(nodeId, out var existing) && existing.State >= state)
            return;
        desired[nodeId] = (state, reason);
    }
}
