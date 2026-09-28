using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;

namespace Game.Simulation.Research.Adaptive;

public enum AdaptiveResearchOutcomeEventType
{
    OutcomeResolved,
    ProjectSetback,
    PartialSuccess,
    HypothesisRefined,
    HypothesisDisproven,
    AnomalousResult,
    HazardReported,
    SideDiscoveryMaterialized,
}

public sealed record AdaptiveResearchOutcomeEvent(
    AdaptiveResearchOutcomeEventType Type,
    string CivilizationId,
    string NodeId,
    string? SubjectId,
    string Message);

public sealed record PlannedResearchOutcome(
    string NodeId,
    string CheckpointId,
    int AttemptIndex,
    ResearchUncertaintyProfile Profile,
    ResearchOutcomeKind Outcome,
    double DeterministicRoll,
    string? PlannedSideDiscoveryNodeId,
    string Explanation);

public sealed record ResearchOutcomeApplicationResult(
    bool Accepted,
    PlannedResearchOutcome? Resolution,
    IReadOnlyList<AdaptiveResearchRuntimeEvent> ResearchEvents,
    IReadOnlyList<AdaptiveResearchOutcomeEvent> OutcomeEvents,
    string Message)
{
    public static ResearchOutcomeApplicationResult Rejected(string message) =>
        new(false, null, Array.Empty<AdaptiveResearchRuntimeEvent>(), Array.Empty<AdaptiveResearchOutcomeEvent>(), message);
}

/// <summary>
/// Deterministic/replayable uncertainty resolution for ordinary public research. This layer does not
/// replace RP progression: it resolves explicit experimental checkpoints, records bounded history,
/// and materializes only pre-indexed related side discoveries.
/// </summary>
public sealed class AdaptiveResearchOutcomeRuntime
{
    private readonly AdaptiveResearchAuthority _authority;
    private readonly AdaptiveResearchPressureRuntime? _pressure;
    private readonly AdaptiveResearchOutcomeCatalog _catalog;
    private readonly ConditionalWeakTable<AdaptiveResearchCivilizationState, AdaptiveResearchOutcomeState> _states = new();

    public AdaptiveResearchOutcomeRuntime(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchOutcomeCatalog catalog,
        AdaptiveResearchPressureRuntime? pressure = null)
    {
        _authority = authority ?? throw new ArgumentNullException(nameof(authority));
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _pressure = pressure;
    }

    public AdaptiveResearchOutcomeCatalog Catalog => _catalog;
    public AdaptiveResearchOutcomeState GetState(AdaptiveResearchCivilizationState state) =>
        _states.GetValue(state ?? throw new ArgumentNullException(nameof(state)), _ => new AdaptiveResearchOutcomeState());

    public PlannedResearchOutcome PlanOutcome(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string campaignSeed,
        string checkpointId,
        string? targetApplicabilityContextId = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(campaignSeed);
        ArgumentException.ThrowIfNullOrWhiteSpace(checkpointId);
        var node = _authority.Catalog.GetNode(nodeId);
        var profile = _catalog.GetProfile(node);
        var summary = GetState(state).GetSummary(nodeId);
        var attemptIndex = summary.Attempts;
        var weights = AdjustWeightsForReadiness(state, node, profile, targetApplicabilityContextId);
        var roll = DeterministicUnit(campaignSeed, state.CivilizationId, nodeId, checkpointId, attemptIndex, "outcome");
        var outcome = Select(weights, roll);
        var side = outcome == ResearchOutcomeKind.SideDiscovery
            ? SelectSideDiscovery(state, nodeId, campaignSeed, checkpointId, attemptIndex)
            : null;
        var explanation = $"{node.Name} resolved {checkpointId} as {AdaptiveResearchOutcomeCatalog.OutcomeId(outcome)} under profile {profile}; exact deterministic roll remains an internal replay value.";
        return new PlannedResearchOutcome(nodeId, checkpointId, attemptIndex, profile, outcome, roll, side, explanation);
    }

    public ResearchOutcomeApplicationResult ResolvePendingHypothesis(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string campaignSeed,
        double currentYear,
        string? targetApplicabilityContextId = null)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project) || !project.Paused ||
            !string.Equals(project.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal))
            return ResearchOutcomeApplicationResult.Rejected("No hypothesis is awaiting experimental resolution for that node.");
        if (!_authority.Catalog.GetNode(nodeId).IsHypothesis)
            return ResearchOutcomeApplicationResult.Rejected("The pending project is not a scientific hypothesis.");

        var resolution = PlanOutcome(state, nodeId, campaignSeed, "experimental_boundary", targetApplicabilityContextId ?? project.TargetApplicabilityContextId);
        return ApplyOutcome(state, project, resolution, currentYear);
    }

    /// <summary>
    /// Resolves a real explicit prototype/engineering checkpoint supplied by the research scheduler or owning test system.
    /// Routine established projects need not invoke this; the default runtime remains deterministic unless an actual
    /// uncertainty checkpoint is requested.
    /// </summary>
    public ResearchOutcomeApplicationResult ResolveActiveCheckpoint(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string campaignSeed,
        string checkpointId,
        double currentYear)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return ResearchOutcomeApplicationResult.Rejected("No active project exists for that outcome checkpoint.");
        if (_authority.Catalog.GetNode(nodeId).IsHypothesis && project.Paused &&
            string.Equals(project.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal))
            return ResolvePendingHypothesis(state, nodeId, campaignSeed, currentYear, project.TargetApplicabilityContextId);

        var resolution = PlanOutcome(state, nodeId, campaignSeed, checkpointId, project.TargetApplicabilityContextId);
        if (resolution.Outcome is ResearchOutcomeKind.HypothesisSupported or ResearchOutcomeKind.HypothesisDisproven)
            return ResearchOutcomeApplicationResult.Rejected("A non-hypothesis checkpoint cannot resolve fundamental hypothesis truth.");
        return ApplyOutcome(state, project, resolution, currentYear);
    }

    private ResearchOutcomeApplicationResult ApplyOutcome(
        AdaptiveResearchCivilizationState state,
        ResearchProjectRuntimeState project,
        PlannedResearchOutcome resolution,
        double currentYear)
    {
        var researchEvents = new List<AdaptiveResearchRuntimeEvent>();
        var outcomeEvents = new List<AdaptiveResearchOutcomeEvent>();
        var node = _authority.Catalog.GetNode(project.NodeId);
        string? sideDiscoveryNodeId = null;

        switch (resolution.Outcome)
        {
            case ResearchOutcomeKind.Progress:
                break;

            case ResearchOutcomeKind.Setback:
                ApplySetback(state, node, project);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.ProjectSetback, state.CivilizationId, node.Id, null,
                    "A failed test created additional engineering work; total scientific work remains recorded."));
                break;

            case ResearchOutcomeKind.PartialSuccess:
                ApplyPartialSuccess(state, node, project);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.PartialSuccess, state.CivilizationId, node.Id, null,
                    "A limited/reduced-performance result worked and reduced remaining work without granting Mature technology."));
                break;

            case ResearchOutcomeKind.HypothesisSupported:
            {
                var result = _authority.ResolveHypothesis(state, node.Id, supported: true);
                if (!result.Accepted)
                    return ResearchOutcomeApplicationResult.Rejected(result.Message);
                researchEvents.AddRange(result.Events);
                break;
            }

            case ResearchOutcomeKind.HypothesisDisproven:
            {
                var result = _authority.ResolveHypothesis(state, node.Id, supported: false);
                if (!result.Accepted)
                    return ResearchOutcomeApplicationResult.Rejected(result.Message);
                researchEvents.AddRange(result.Events);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.HypothesisDisproven, state.CivilizationId, node.Id, null,
                    "The tested hypothesis was disproven and archived; negative knowledge and scientific practice are retained."));
                break;
            }

            case ResearchOutcomeKind.HypothesisRefined:
                ContinueRefinedHypothesis(state, node, project);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.HypothesisRefined, state.CivilizationId, node.Id, null,
                    "The hypothesis survived only in revised form; most effective experimental progress was preserved for another test cycle."));
                break;

            case ResearchOutcomeKind.AnomalousResult:
                ContinueRefinedHypothesis(state, node, project);
                if (_pressure is not null)
                    _pressure.ReportEventSignal(state, "experiment_violates_current_model", 1.0, project.TargetApplicabilityContextId);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.AnomalousResult, state.CivilizationId, node.Id, null,
                    "A reproducible anomaly did not validate the expected model; it created a legitimate new fundamental-science signal."));
                break;

            case ResearchOutcomeKind.HazardIncident:
                ApplySetback(state, node, project);
                outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
                    AdaptiveResearchOutcomeEventType.HazardReported, state.CivilizationId, node.Id, null,
                    "A research hazard occurred. Adaptive Research reports the fact; physical damage/casualties are resolved by the owning subsystem."));
                break;

            case ResearchOutcomeKind.SideDiscovery:
                sideDiscoveryNodeId = MaterializeSideDiscovery(state, resolution, project.TargetApplicabilityContextId, outcomeEvents);
                if (node.IsHypothesis && project.Paused)
                    ContinueRefinedHypothesis(state, node, project);
                break;

            default:
                throw new ArgumentOutOfRangeException();
        }

        ApplyOutcomeCompetence(state, node, resolution.Outcome, currentYear);
        var finalResolution = resolution with { PlannedSideDiscoveryNodeId = sideDiscoveryNodeId ?? resolution.PlannedSideDiscoveryNodeId };
        GetState(state).Record(
            node.Id,
            resolution.CheckpointId,
            resolution.AttemptIndex,
            resolution.Outcome,
            sideDiscoveryNodeId,
            currentYear,
            resolution.Explanation,
            _catalog.Policy.MaxRecentOutcomeRecords,
            _catalog.Policy.MaxPerNodeRecentRecords);
        outcomeEvents.Add(new AdaptiveResearchOutcomeEvent(
            AdaptiveResearchOutcomeEventType.OutcomeResolved, state.CivilizationId, node.Id, sideDiscoveryNodeId,
            $"Research outcome resolved: {AdaptiveResearchOutcomeCatalog.OutcomeId(resolution.Outcome)}."));
        return new ResearchOutcomeApplicationResult(true, finalResolution, researchEvents, outcomeEvents, outcomeEvents[^1].Message);
    }

    private void ApplySetback(AdaptiveResearchCivilizationState state, AdaptiveResearchNodeDefinition node, ResearchProjectRuntimeState project)
    {
        if (!state.ActiveProjects.TryGetValue(node.Id, out var current))
            return;
        var stageWork = _authority.ProgressPolicy.GetStageWork(node, current.Stage);
        var loss = stageWork * _catalog.Policy.SetbackStageProgressLossFraction;
        var updatedProgress = Math.Max(0.0, current.StageResearchPoints - loss);
        state.SetProject(current with { StageResearchPoints = updatedProgress });
        if (state.TryGetNodeState(node.Id, out var nodeState))
            state.SetNodeState(nodeState with { StageResearchPoints = updatedProgress });
    }

    private void ApplyPartialSuccess(AdaptiveResearchCivilizationState state, AdaptiveResearchNodeDefinition node, ResearchProjectRuntimeState project)
    {
        if (!state.ActiveProjects.TryGetValue(node.Id, out var current))
            return;
        var stageWork = _authority.ProgressPolicy.GetStageWork(node, current.Stage);
        var credit = stageWork * _catalog.Policy.PartialSuccessStageProgressCreditFraction;
        var updatedProgress = Math.Min(Math.Max(0.0, stageWork - 0.000001), current.StageResearchPoints + credit);
        state.SetProject(current with { StageResearchPoints = updatedProgress });
        if (state.TryGetNodeState(node.Id, out var nodeState))
            state.SetNodeState(nodeState with { StageResearchPoints = updatedProgress });
    }

    private void ContinueRefinedHypothesis(AdaptiveResearchCivilizationState state, AdaptiveResearchNodeDefinition node, ResearchProjectRuntimeState project)
    {
        if (!state.ActiveProjects.TryGetValue(node.Id, out var current))
            return;
        var stageWork = _authority.ProgressPolicy.GetStageWork(node, ResearchMaturity.Experimental);
        var preserved = stageWork * _catalog.Policy.RefinedHypothesisStageProgressPreservedFraction;
        var updated = current with
        {
            Stage = ResearchMaturity.Experimental,
            StageResearchPoints = preserved,
            Paused = false,
            PauseReason = null,
        };
        state.SetProject(updated);
        state.SetNodeState(new ResearchNodeRuntimeState(
            node.Id, ResearchMaturity.Experimental, "refined_hypothesis", preserved,
            current.TotalResearchPoints, state.Revision + 1));
    }

    private string? MaterializeSideDiscovery(
        AdaptiveResearchCivilizationState state,
        PlannedResearchOutcome resolution,
        string? targetContextId,
        ICollection<AdaptiveResearchOutcomeEvent> events)
    {
        var candidateId = resolution.PlannedSideDiscoveryNodeId;
        if (candidateId is null || state.NodeStates.ContainsKey(candidateId))
            return null;
        var candidate = _authority.Catalog.GetNode(candidateId);
        var eligibility = _authority.Kernel.Eligibility.EvaluateScientificEligibility(state, candidateId, targetContextId);
        var maturity = eligibility.Allowed ? ResearchMaturity.Investigable : ResearchMaturity.Hypothesized;
        state.SetNodeState(new ResearchNodeRuntimeState(candidateId, maturity, "side_discovery", 0.0, 0.0, state.Revision + 1));
        events.Add(new AdaptiveResearchOutcomeEvent(
            AdaptiveResearchOutcomeEventType.SideDiscoveryMaterialized,
            state.CivilizationId,
            resolution.NodeId,
            candidateId,
            eligibility.Allowed
                ? $"Unexpected work made {candidate.Name} immediately Investigable because its normal requirements were already met."
                : $"Unexpected work exposed {candidate.Name} as a related hypothesis; normal requirements still govern researchability."));
        return candidateId;
    }

    private void ApplyOutcomeCompetence(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        ResearchOutcomeKind outcome,
        double currentYear)
    {
        if (!_catalog.Policy.CompetenceGains.TryGetValue(outcome, out var raw) || node.KnowledgeFields.Count == 0)
            return;
        var divisor = node.KnowledgeFields.Count;
        foreach (var fieldId in node.KnowledgeFields)
        {
            var current = state.Expertise.GetField(fieldId).Current;
            _authority.Expertise.SeedFieldCompetence(
                state,
                fieldId,
                new ResearchCompetenceVector(
                    Math.Min(100.0, current.Theoretical + (raw.Theoretical / divisor)),
                    Math.Min(100.0, current.Experimental + (raw.Experimental / divisor)),
                    Math.Min(100.0, current.Engineering + (raw.Engineering / divisor))),
                currentYear);
        }
    }

    private IReadOnlyDictionary<ResearchOutcomeKind, double> AdjustWeightsForReadiness(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        ResearchUncertaintyProfile profile,
        string? targetContextId)
    {
        var weights = _catalog.Policy.BaseWeights[profile].ToDictionary(pair => pair.Key, pair => pair.Value);
        if (!state.ActiveProjects.TryGetValue(node.Id, out var project))
            return weights;
        var readiness = _authority.GetProjectReadiness(state, node.Id, project.Stage, project.AssignedEffectiveLabs, targetContextId ?? project.TargetApplicabilityContextId);
        var reductionFraction = (readiness.OverallReadinessScore / 100.0) * _catalog.Policy.HighReadinessRiskReductionMaxFraction;
        var removed = 0.0;
        foreach (var risky in new[] { ResearchOutcomeKind.Setback, ResearchOutcomeKind.HazardIncident, ResearchOutcomeKind.AnomalousResult })
        {
            if (!weights.TryGetValue(risky, out var weight))
                continue;
            var delta = weight * reductionFraction;
            weights[risky] = weight - delta;
            removed += delta;
        }
        if (removed > 0)
        {
            var preferred = new[] { ResearchOutcomeKind.Progress, ResearchOutcomeKind.PartialSuccess, ResearchOutcomeKind.HypothesisRefined }
                .FirstOrDefault(weights.ContainsKey);
            if (weights.ContainsKey(preferred))
                weights[preferred] += removed;
        }
        return weights;
    }

    private string? SelectSideDiscovery(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string campaignSeed,
        string checkpointId,
        int attemptIndex)
    {
        if (!_catalog.SideDiscoveryCandidatesByNode.TryGetValue(nodeId, out var candidates) || candidates.Count == 0)
            return null;
        var start = (int)Math.Floor(DeterministicUnit(campaignSeed, state.CivilizationId, nodeId, checkpointId, attemptIndex, "side") * candidates.Count);
        if (start >= candidates.Count)
            start = candidates.Count - 1;
        for (var offset = 0; offset < candidates.Count; offset++)
        {
            var candidate = candidates[(start + offset) % candidates.Count];
            if (!state.NodeStates.ContainsKey(candidate))
                return candidate;
        }
        return null;
    }

    private static ResearchOutcomeKind Select(IReadOnlyDictionary<ResearchOutcomeKind, double> weights, double unit)
    {
        var ordered = weights.OrderBy(pair => pair.Key).ToArray();
        var total = ordered.Sum(pair => pair.Value);
        var target = unit * total;
        var cursor = 0.0;
        foreach (var pair in ordered)
        {
            cursor += pair.Value;
            if (target <= cursor)
                return pair.Key;
        }
        return ordered[^1].Key;
    }

    private double DeterministicUnit(
        string campaignSeed,
        string civilizationId,
        string nodeId,
        string checkpointId,
        int attemptIndex,
        string stream)
    {
        var key = string.Join("|", _authority.Catalog.Metadata.CatalogId, campaignSeed, civilizationId, nodeId, checkpointId, attemptIndex, stream);
        var hash = SHA256.HashData(Encoding.UTF8.GetBytes(key));
        var raw = BinaryPrimitives.ReadUInt64BigEndian(hash.AsSpan(0, 8));
        return raw / ((double)ulong.MaxValue + 1.0);
    }
}
