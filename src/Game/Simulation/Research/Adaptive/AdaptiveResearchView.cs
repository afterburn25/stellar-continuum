using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record DirectedResearchCapacityView(
    string StageId,
    int? MaximumDirectedPrograms,
    bool LabCapacityOnly,
    int ActiveProgramCount,
    double FreeEffectiveLabs);

public sealed record AdaptiveResearchNodeView(
    string NodeId,
    string DisplayName,
    string DomainId,
    string SolutionFamily,
    ResearchMaturity State,
    bool IsHypothesis,
    IReadOnlyList<string> KnownCapabilities,
    IReadOnlyList<ResearchBlocker> Blockers,
    int? MinimumLabs,
    int? RecommendedLabs,
    double? AssignedLabs,
    string? TargetApplicabilityContextId);

public sealed record AdaptiveResearchEdgeView(
    string FromVisibleNodeId,
    string ToVisibleNodeId,
    string Relationship);

public sealed record AdaptiveResearchProjectView(
    string NodeId,
    ResearchMaturity Stage,
    double StageProgress,
    double TotalProgress,
    double AssignedEffectiveLabs,
    string ReadinessBand,
    bool Paused,
    string? PauseReason,
    IReadOnlyList<ResearchBlocker> CurrentBlockers,
    string? TargetApplicabilityContextId);

public sealed record AdaptiveResearchPressureView(
    string PressureId,
    double Value,
    IReadOnlyList<string> VisibleHardGateTargetNodeIds);

public sealed record AdaptiveResearchView(
    long Revision,
    string CivilizationId,
    DirectedResearchCapacityView DirectedProgramCapacity,
    IReadOnlyList<AdaptiveResearchNodeView> VisibleNodes,
    IReadOnlyList<AdaptiveResearchEdgeView> VisibleEdges,
    IReadOnlyList<AdaptiveResearchProjectView> ActiveProjects,
    IReadOnlyList<AdaptiveResearchPressureView> RecognizedPressures);

/// <summary>
/// Builds the read-only player/AI explanation projection strictly from already-visible civilization state.
/// It never enumerates Unknown nodes to create placeholders, hidden edges or hidden pressure targets.
/// </summary>
public sealed class AdaptiveResearchViewBuilder
{
    private readonly AdaptiveResearchCatalog _catalog;
    private readonly AdaptiveResearchEligibilityEvaluator _eligibility;
    private readonly AdaptiveResearchProgressPolicy _progressPolicy;

    public AdaptiveResearchViewBuilder(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchEligibilityEvaluator eligibility,
        AdaptiveResearchProgressPolicy progressPolicy)
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _eligibility = eligibility ?? throw new ArgumentNullException(nameof(eligibility));
        _progressPolicy = progressPolicy ?? throw new ArgumentNullException(nameof(progressPolicy));
    }

    public AdaptiveResearchView Build(
        AdaptiveResearchCivilizationState state,
        string? defaultTargetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        var visibleIds = state.NodeStates.Keys.ToHashSet(StringComparer.Ordinal);
        var nodes = state.NodeStates.Values
            .OrderBy(node => _catalog.GetNode(node.NodeId).GraphDepth)
            .ThenBy(node => _catalog.GetNode(node.NodeId).DomainId, StringComparer.Ordinal)
            .ThenBy(node => node.NodeId, StringComparer.Ordinal)
            .Select(node => BuildNodeView(state, node, visibleIds, defaultTargetApplicabilityContextId))
            .ToArray();

        var edges = BuildVisibleEdges(visibleIds);
        var projects = state.ActiveProjects.Values
            .OrderBy(project => project.NodeId, StringComparer.Ordinal)
            .Select(project => BuildProjectView(state, project, visibleIds))
            .ToArray();
        var pressures = BuildRecognizedPressures(state, visibleIds);

        var directed = _catalog.GetDirectedProgramStage(state.DirectedProgramStageId);
        var capacityView = new DirectedResearchCapacityView(
            directed.Id,
            directed.DirectedProgramLimit,
            directed.LabCapacityOnly,
            state.ActiveProjects.Values.Count(project => !project.Paused),
            state.FreeEffectiveLabs);

        return new AdaptiveResearchView(
            state.MaterializedViewRevision,
            state.CivilizationId,
            capacityView,
            nodes,
            edges,
            projects,
            pressures);
    }

    private AdaptiveResearchNodeView BuildNodeView(
        AdaptiveResearchCivilizationState state,
        ResearchNodeRuntimeState nodeState,
        IReadOnlySet<string> visibleIds,
        string? defaultTargetApplicabilityContextId)
    {
        var definition = _catalog.GetNode(nodeState.NodeId);
        state.ActiveProjects.TryGetValue(nodeState.NodeId, out var activeProject);
        var targetContext = activeProject?.TargetApplicabilityContextId ?? defaultTargetApplicabilityContextId;

        IReadOnlyList<ResearchBlocker> blockers;
        if (activeProject is not null)
        {
            blockers = CurrentProjectBlockers(state, activeProject, visibleIds);
        }
        else if (nodeState.Maturity >= ResearchMaturity.Investigable &&
                 nodeState.Maturity < ResearchMaturity.Mature)
        {
            blockers = SanitizeBlockers(
                _eligibility.EvaluateProjectStart(
                    state,
                    nodeState.NodeId,
                    definition.ProjectRequirements.MinimumLabs,
                    targetContext).Blockers,
                visibleIds);
        }
        else if (nodeState.Maturity < ResearchMaturity.Investigable)
        {
            blockers = SanitizeBlockers(
                _eligibility.EvaluateScientificEligibility(state, nodeState.NodeId, targetContext).Blockers,
                visibleIds);
        }
        else
        {
            blockers = Array.Empty<ResearchBlocker>();
        }

        var knownCapabilities = nodeState.Maturity >= ResearchMaturity.Demonstrated
            ? definition.DeclaredCapabilities.ToArray()
            : Array.Empty<string>();

        return new AdaptiveResearchNodeView(
            definition.Id,
            definition.Name,
            definition.DomainId,
            definition.SolutionFamily,
            nodeState.Maturity,
            definition.IsHypothesis,
            knownCapabilities,
            blockers,
            nodeState.Maturity >= ResearchMaturity.Investigable ? definition.ProjectRequirements.MinimumLabs : null,
            nodeState.Maturity >= ResearchMaturity.Investigable ? definition.ProjectRequirements.RecommendedLabs : null,
            activeProject?.AssignedEffectiveLabs,
            targetContext);
    }

    private AdaptiveResearchProjectView BuildProjectView(
        AdaptiveResearchCivilizationState state,
        ResearchProjectRuntimeState project,
        IReadOnlySet<string> visibleIds)
    {
        var definition = _catalog.GetNode(project.NodeId);
        var stageWork = _progressPolicy.GetStageWork(definition, project.Stage);
        var stageProgress = stageWork <= 0.0 ? 0.0 : Math.Clamp(project.StageResearchPoints / stageWork, 0.0, 1.0);
        var totalProgress = definition.ProjectRequirements.BaseResearchPoints <= 0.0
            ? 0.0
            : Math.Clamp(project.TotalResearchPoints / definition.ProjectRequirements.BaseResearchPoints, 0.0, 1.0);

        return new AdaptiveResearchProjectView(
            project.NodeId,
            project.Stage,
            stageProgress,
            totalProgress,
            project.AssignedEffectiveLabs,
            ReadinessBand(project.ReadinessEfficiency),
            project.Paused,
            project.PauseReason,
            CurrentProjectBlockers(state, project, visibleIds),
            project.TargetApplicabilityContextId);
    }

    private IReadOnlyList<ResearchBlocker> CurrentProjectBlockers(
        AdaptiveResearchCivilizationState state,
        ResearchProjectRuntimeState project,
        IReadOnlySet<string> visibleIds)
    {
        var blockers = new List<ResearchBlocker>();
        blockers.AddRange(_eligibility.EvaluateScientificEligibility(
            state,
            project.NodeId,
            project.TargetApplicabilityContextId).Blockers);
        blockers.AddRange(_eligibility.EvaluateStageFacilityEligibility(state, project.NodeId, project.Stage).Blockers);
        return SanitizeBlockers(blockers, visibleIds);
    }

    private IReadOnlyList<AdaptiveResearchEdgeView> BuildVisibleEdges(IReadOnlySet<string> visibleIds)
    {
        var edges = new List<AdaptiveResearchEdgeView>();
        foreach (var targetId in visibleIds)
        {
            var node = _catalog.GetNode(targetId);
            foreach (var prerequisite in node.Prerequisites.AllOf)
                if (visibleIds.Contains(prerequisite))
                    edges.Add(new AdaptiveResearchEdgeView(prerequisite, targetId, "known_prerequisite"));
            foreach (var prerequisite in node.Prerequisites.AnyOf)
                if (visibleIds.Contains(prerequisite))
                    edges.Add(new AdaptiveResearchEdgeView(prerequisite, targetId, "known_alternative"));
        }
        return edges
            .OrderBy(edge => edge.FromVisibleNodeId, StringComparer.Ordinal)
            .ThenBy(edge => edge.ToVisibleNodeId, StringComparer.Ordinal)
            .ToArray();
    }

    private IReadOnlyList<AdaptiveResearchPressureView> BuildRecognizedPressures(
        AdaptiveResearchCivilizationState state,
        IReadOnlySet<string> visibleIds)
    {
        var relevant = new HashSet<string>(StringComparer.Ordinal);
        var hardTargets = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal);

        foreach (var nodeId in visibleIds)
        {
            var node = _catalog.GetNode(nodeId);
            foreach (var pressureId in node.PressureAffinities)
                relevant.Add(pressureId);
            foreach (var pressureId in node.ProjectRequirements.RequiredPressure.Keys.Concat(node.ProjectRequirements.RequiredPressureAny.Keys))
            {
                relevant.Add(pressureId);
                if (!hardTargets.TryGetValue(pressureId, out var targets))
                {
                    targets = new HashSet<string>(StringComparer.Ordinal);
                    hardTargets.Add(pressureId, targets);
                }
                targets.Add(nodeId);
            }
        }

        return relevant
            .Where(pressureId => state.TryGetPressure(pressureId, out _))
            .OrderBy(pressureId => pressureId, StringComparer.Ordinal)
            .Select(pressureId => new AdaptiveResearchPressureView(
                pressureId,
                state.GetPressure(pressureId),
                hardTargets.TryGetValue(pressureId, out var targets)
                    ? targets.OrderBy(id => id, StringComparer.Ordinal).ToArray()
                    : Array.Empty<string>()))
            .ToArray();
    }

    private static IReadOnlyList<ResearchBlocker> SanitizeBlockers(
        IEnumerable<ResearchBlocker> blockers,
        IReadOnlySet<string> visibleIds) =>
        blockers.Select(blocker =>
        {
            if (blocker.Code is ResearchBlockerCode.MissingPrerequisite &&
                blocker.SubjectId is not null && !visibleIds.Contains(blocker.SubjectId))
            {
                return blocker with { SubjectId = null, Message = "Additional prerequisite knowledge is required." };
            }
            return blocker;
        }).ToArray();

    private static string ReadinessBand(double efficiency)
    {
        if (efficiency <= 0.35 + 0.000001) return "poor";
        if (efficiency <= 0.55 + 0.000001) return "limited";
        if (efficiency <= 0.75 + 0.000001) return "adequate";
        if (efficiency <= 1.00 + 0.000001) return "strong";
        return "exceptional";
    }
}
