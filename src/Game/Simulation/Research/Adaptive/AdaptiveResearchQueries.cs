using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchStartability(
    string NodeId,
    bool IsVisible,
    bool CanStart,
    int MinimumLabs,
    int RecommendedLabs,
    double RequestedLabs,
    IReadOnlyList<ResearchBlocker> Blockers);

public sealed record AdaptiveResearchVisibleNodeView(
    string NodeId,
    string Name,
    string DomainId,
    string Complexity,
    ResearchMaturity Maturity,
    string? Resolution,
    bool IsActive,
    string? TargetApplicabilityContextId,
    double TotalResearchPoints,
    double BaseResearchPoints,
    int MinimumLabs,
    int RecommendedLabs,
    bool CanStart,
    IReadOnlyList<ResearchBlocker> StartBlockers);

/// <summary>
/// Read-only visible research view. It iterates sparse civilization state only; unknown future
/// possibilities are never enumerated or materialized for UI/AI consumers.
/// </summary>
public sealed class AdaptiveResearchQueryService
{
    private readonly AdaptiveResearchCatalog _catalog;
    private readonly AdaptiveResearchEligibilityEvaluator _eligibility;

    public AdaptiveResearchQueryService(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchEligibilityEvaluator eligibility)
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _eligibility = eligibility ?? throw new ArgumentNullException(nameof(eligibility));
    }

    public IReadOnlyList<AdaptiveResearchVisibleNodeView> GetVisibleNodes(AdaptiveResearchCivilizationState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        var result = new List<AdaptiveResearchVisibleNodeView>(state.NodeStates.Count);

        foreach (var pair in state.NodeStates.OrderBy(pair => pair.Key, StringComparer.Ordinal))
        {
            if (!_catalog.Nodes.TryGetValue(pair.Key, out var definition))
                throw new InvalidOperationException($"Civilization '{state.CivilizationId}' contains unknown research node '{pair.Key}'.");

            state.ActiveProjects.TryGetValue(pair.Key, out var project);
            var targetContext = project?.TargetApplicabilityContextId;
            var requestedLabs = project?.AssignedEffectiveLabs ?? definition.ProjectRequirements.MinimumLabs;
            var startability = pair.Value.Maturity == ResearchMaturity.Investigable && project is null
                ? EvaluateStartability(state, pair.Key, targetContext, requestedLabs)
                : new ResearchStartability(
                    pair.Key,
                    true,
                    false,
                    definition.ProjectRequirements.MinimumLabs,
                    definition.ProjectRequirements.RecommendedLabs,
                    requestedLabs,
                    ProjectStateBlockers(pair.Value, project));

            result.Add(new AdaptiveResearchVisibleNodeView(
                pair.Key,
                definition.Name,
                definition.DomainId,
                definition.Complexity,
                pair.Value.Maturity,
                pair.Value.Resolution,
                project is not null,
                targetContext,
                pair.Value.TotalResearchPoints,
                definition.ProjectRequirements.BaseResearchPoints,
                definition.ProjectRequirements.MinimumLabs,
                definition.ProjectRequirements.RecommendedLabs,
                startability.CanStart,
                startability.Blockers));
        }

        return result;
    }

    public ResearchStartability EvaluateStartability(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string? targetApplicabilityContextId = null,
        double? requestedLabs = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentException.ThrowIfNullOrWhiteSpace(nodeId);

        if (!state.TryGetNodeState(nodeId, out _))
        {
            return new ResearchStartability(
                nodeId,
                false,
                false,
                0,
                0,
                requestedLabs ?? 0.0,
                new[]
                {
                    new ResearchBlocker(
                        ResearchBlockerCode.NodeNotInvestigable,
                        null,
                        null,
                        null,
                        "The possibility is not in the civilization's visible research horizon."),
                });
        }

        var definition = _catalog.GetNode(nodeId);
        var assigned = requestedLabs ?? definition.ProjectRequirements.MinimumLabs;
        var evaluation = _eligibility.EvaluateProjectStart(
            state,
            nodeId,
            assigned,
            targetApplicabilityContextId);

        return new ResearchStartability(
            nodeId,
            true,
            evaluation.Allowed,
            definition.ProjectRequirements.MinimumLabs,
            definition.ProjectRequirements.RecommendedLabs,
            assigned,
            evaluation.Blockers);
    }

    public IReadOnlyList<ResearchBlocker> EvaluateStageBlockers(
        AdaptiveResearchCivilizationState state,
        ResearchProjectRuntimeState project)
    {
        ArgumentNullException.ThrowIfNull(state);
        ArgumentNullException.ThrowIfNull(project);

        var blockers = _eligibility.EvaluateStageFacilityEligibility(state, project.NodeId, project.Stage)
            .Blockers.ToList();
        var minimum = _catalog.GetNode(project.NodeId).ProjectRequirements.MinimumLabs;
        if (!project.Paused && project.AssignedEffectiveLabs + 0.000001 < minimum)
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.BelowMinimumAssignedLabs,
                project.NodeId,
                minimum,
                project.AssignedEffectiveLabs,
                "The active stage has fewer than the minimum assigned Effective Research Labs."));
        }

        return blockers;
    }

    private static IReadOnlyList<ResearchBlocker> ProjectStateBlockers(
        ResearchNodeRuntimeState nodeState,
        ResearchProjectRuntimeState? project)
    {
        if (project is not null)
        {
            return new[]
            {
                new ResearchBlocker(
                    ResearchBlockerCode.AlreadyActive,
                    project.NodeId,
                    null,
                    null,
                    project.Paused ? "This research project is paused." : "This research project is already active."),
            };
        }

        if (nodeState.Maturity is ResearchMaturity.Mature or ResearchMaturity.Archived || nodeState.CountsAsEstablishedKnowledge)
        {
            return new[]
            {
                new ResearchBlocker(
                    ResearchBlockerCode.AlreadyMature,
                    nodeState.NodeId,
                    null,
                    null,
                    "This knowledge is already mature or resolved."),
            };
        }

        return new[]
        {
            new ResearchBlocker(
                ResearchBlockerCode.NodeNotInvestigable,
                nodeState.NodeId,
                null,
                null,
                "The visible possibility is not yet Investigable."),
        };
    }
}
