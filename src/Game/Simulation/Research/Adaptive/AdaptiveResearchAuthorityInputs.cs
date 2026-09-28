using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public static class AdaptiveResearchAuthorityInputs
{
    public static AdaptiveResearchView BuildView(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state) =>
        authority.Kernel.BuildView(state);

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> SetPressure(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        string pressureId,
        double value,
        string? targetApplicabilityContextId = null) =>
        authority.Kernel.SetPressure(state, pressureId, value, targetApplicabilityContextId);

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> AddEvidence(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        string evidenceInstanceId,
        string evidenceTypeId,
        string provenance,
        double quality,
        double confidence,
        string? contextId = null)
    {
        var events = authority.Kernel.AddEvidence(
            state,
            evidenceInstanceId,
            evidenceTypeId,
            provenance,
            quality,
            confidence,
            contextId);
        RecalculateAllActiveReadiness(authority, state);
        return events;
    }

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> AddCivilizationTrait(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        string traitId) =>
        authority.Kernel.AddCivilizationTrait(state, traitId);

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> SetApplicabilityContextTraits(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        string contextId,
        IEnumerable<string> traitIds) =>
        authority.Kernel.SetApplicabilityContextTraits(state, contextId, traitIds);

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> AddCapability(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        string capabilityId,
        string? contextId = null) =>
        authority.Kernel.AddCapability(state, capabilityId, contextId);

    public static IReadOnlyList<AdaptiveResearchRuntimeEvent> ReviewBasicScienceCandidates(
        this AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state,
        IEnumerable<string> boundedCandidateNodeIds,
        string? targetApplicabilityContextId = null) =>
        authority.Kernel.ReviewBasicScienceCandidates(state, boundedCandidateNodeIds, targetApplicabilityContextId);

    private static void RecalculateAllActiveReadiness(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchCivilizationState state)
    {
        foreach (var project in state.ActiveProjects.Values.ToArray())
        {
            var breakdown = authority.GetProjectReadiness(
                state,
                project.NodeId,
                project.Stage,
                project.AssignedEffectiveLabs,
                project.TargetApplicabilityContextId);
            if (Math.Abs(project.ReadinessEfficiency - breakdown.RpEfficiency) < 0.000001)
                continue;
            state.SetProject(project with { ReadinessEfficiency = breakdown.RpEfficiency });
        }
    }
}
