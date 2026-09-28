using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchNodeRuntimeState(
    string NodeId,
    ResearchMaturity Maturity,
    string? Resolution,
    double StageResearchPoints,
    double TotalResearchPoints,
    long Revision)
{
    public bool CountsAsEstablishedKnowledge =>
        Maturity == ResearchMaturity.Mature ||
        (Maturity == ResearchMaturity.Archived &&
         (string.Equals(Resolution, "mature_history", StringComparison.OrdinalIgnoreCase) ||
          string.Equals(Resolution, "superseded", StringComparison.OrdinalIgnoreCase)));
}

public sealed record ResearchEvidenceInstance(
    string EvidenceInstanceId,
    string EvidenceTypeId,
    string Provenance,
    double Quality,
    double Confidence,
    string? ContextId,
    long Revision);

public readonly record struct ResearchCapabilityKey(string CapabilityId, string? ContextId);

public sealed record ResearchProjectRuntimeState(
    string NodeId,
    ResearchMaturity Stage,
    string? TargetApplicabilityContextId,
    double AssignedEffectiveLabs,
    double ReadinessEfficiency,
    bool Paused,
    string? PauseReason,
    double StageResearchPoints,
    double TotalResearchPoints,
    long Revision);

/// <summary>
/// Compact authoritative research state for one civilization.
/// Unknown nodes and zero/default values are absent. Static catalog definitions are never copied here.
/// Mutation is internal to AdaptiveResearchRuntime so consumers must use events/commands/queries.
/// </summary>
public sealed class AdaptiveResearchCivilizationState
{
    private readonly Dictionary<string, ResearchNodeRuntimeState> _nodeStates = new(StringComparer.Ordinal);
    private readonly Dictionary<string, double> _pressures = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ResearchEvidenceInstance> _evidenceByInstanceId = new(StringComparer.Ordinal);
    private readonly Dictionary<string, HashSet<string>> _evidenceInstancesByType = new(StringComparer.Ordinal);
    private readonly HashSet<string> _civilizationTraits = new(StringComparer.Ordinal);
    private readonly Dictionary<string, HashSet<string>> _applicabilityTraitsByContext = new(StringComparer.Ordinal);
    private readonly HashSet<ResearchCapabilityKey> _capabilities = new();
    private readonly HashSet<string> _facilityCapabilities = new(StringComparer.Ordinal);
    private readonly HashSet<string> _enabledDeploymentEventIds = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ResearchProjectRuntimeState> _activeProjects = new(StringComparer.Ordinal);

    public AdaptiveResearchCivilizationState(string civilizationId, string startingDirectedProgramStageId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(civilizationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(startingDirectedProgramStageId);
        CivilizationId = civilizationId;
        DirectedProgramStageId = startingDirectedProgramStageId;
    }

    public string CivilizationId { get; }
    public long Revision { get; private set; }
    public long MaterializedViewRevision { get; private set; }
    public string DirectedProgramStageId { get; private set; }
    public double TotalEffectiveResearchLabs { get; private set; }

    /// <summary>
    /// Sparse competence/institution/tacit sidecar. Static field/institution definitions remain shared in the runtime catalog.
    /// </summary>
    public AdaptiveResearchExpertiseState Expertise { get; } = new();

    public IReadOnlyDictionary<string, ResearchNodeRuntimeState> NodeStates =>
        new ReadOnlyDictionary<string, ResearchNodeRuntimeState>(_nodeStates);

    public IReadOnlyDictionary<string, double> Pressures =>
        new ReadOnlyDictionary<string, double>(_pressures);

    public IReadOnlyDictionary<string, ResearchEvidenceInstance> EvidenceInstances =>
        new ReadOnlyDictionary<string, ResearchEvidenceInstance>(_evidenceByInstanceId);

    public IReadOnlyCollection<string> CivilizationTraits => _civilizationTraits;
    public IReadOnlyCollection<string> Traits => _civilizationTraits;
    public IReadOnlyCollection<ResearchCapabilityKey> Capabilities => _capabilities;
    public IReadOnlyCollection<string> FacilityCapabilities => _facilityCapabilities;
    public IReadOnlyCollection<string> EnabledDeploymentEventIds => _enabledDeploymentEventIds;

    public IReadOnlyDictionary<string, IReadOnlyCollection<string>> ApplicabilityContexts =>
        _applicabilityTraitsByContext.ToDictionary(
            pair => pair.Key,
            pair => (IReadOnlyCollection<string>)pair.Value.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
            StringComparer.Ordinal);

    public IReadOnlyDictionary<string, ResearchProjectRuntimeState> ActiveProjects =>
        new ReadOnlyDictionary<string, ResearchProjectRuntimeState>(_activeProjects);

    public double AssignedEffectiveLabs => _activeProjects.Values.Where(project => !project.Paused).Sum(project => project.AssignedEffectiveLabs);
    public double FreeEffectiveLabs => Math.Max(0.0, TotalEffectiveResearchLabs - AssignedEffectiveLabs);

    public bool TryGetNodeState(string nodeId, out ResearchNodeRuntimeState state) =>
        _nodeStates.TryGetValue(nodeId, out state!);

    public bool HasEstablishedKnowledge(string nodeId) =>
        _nodeStates.TryGetValue(nodeId, out var state) && state.CountsAsEstablishedKnowledge;

    public bool TryGetPressure(string pressureId, out double value) => _pressures.TryGetValue(pressureId, out value);
    public double GetPressure(string pressureId) => _pressures.TryGetValue(pressureId, out var value) ? value : 0.0;

    public bool HasEvidenceType(string evidenceTypeId) =>
        _evidenceInstancesByType.TryGetValue(evidenceTypeId, out var instances) && instances.Count > 0;

    public bool HasEvidenceType(string evidenceTypeId, string? contextId)
    {
        if (!_evidenceInstancesByType.TryGetValue(evidenceTypeId, out var instances))
            return false;
        if (contextId is null)
            return instances.Count > 0;
        return instances.Any(id =>
        {
            var evidence = _evidenceByInstanceId[id];
            return evidence.ContextId is null || string.Equals(evidence.ContextId, contextId, StringComparison.Ordinal);
        });
    }

    public bool HasCivilizationTrait(string traitId) => _civilizationTraits.Contains(traitId);
    public bool HasTrait(string traitId) => HasCivilizationTrait(traitId);

    public bool HasApplicabilityTrait(string contextId, string traitId) =>
        _applicabilityTraitsByContext.TryGetValue(contextId, out var traits) && traits.Contains(traitId);

    public IReadOnlyCollection<string> GetApplicabilityTraits(string contextId) =>
        _applicabilityTraitsByContext.TryGetValue(contextId, out var traits)
            ? traits
            : Array.Empty<string>();

    public bool HasFacilityCapability(string facilityCapabilityId) => _facilityCapabilities.Contains(facilityCapabilityId);

    public bool HasCapability(string capabilityId, string? contextId = null) =>
        _capabilities.Contains(new ResearchCapabilityKey(capabilityId, contextId));

    public bool IsDeploymentEventEnabled(string deploymentEventId) => _enabledDeploymentEventIds.Contains(deploymentEventId);

    internal void SetTotalEffectiveResearchLabs(double value)
    {
        if (value < 0.0 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(nameof(value));
        if (Math.Abs(TotalEffectiveResearchLabs - value) < 0.0000001)
            return;
        TotalEffectiveResearchLabs = value;
        Touch();
    }

    internal bool SetPressure(string pressureId, double value)
    {
        value = Math.Clamp(value, 0.0, 100.0);
        var had = _pressures.TryGetValue(pressureId, out var oldValue);
        if (value <= 0.0)
        {
            if (!had)
                return false;
            _pressures.Remove(pressureId);
            Touch();
            return true;
        }
        if (had && Math.Abs(oldValue - value) < 0.0000001)
            return false;
        _pressures[pressureId] = value;
        Touch();
        return true;
    }

    internal bool AddEvidence(ResearchEvidenceInstance evidence)
    {
        if (_evidenceByInstanceId.ContainsKey(evidence.EvidenceInstanceId))
            return false;
        _evidenceByInstanceId.Add(evidence.EvidenceInstanceId, evidence);
        if (!_evidenceInstancesByType.TryGetValue(evidence.EvidenceTypeId, out var instances))
        {
            instances = new HashSet<string>(StringComparer.Ordinal);
            _evidenceInstancesByType.Add(evidence.EvidenceTypeId, instances);
        }
        instances.Add(evidence.EvidenceInstanceId);
        Touch();
        return true;
    }

    internal bool RemoveEvidence(string evidenceInstanceId)
    {
        if (!_evidenceByInstanceId.Remove(evidenceInstanceId, out var evidence))
            return false;
        if (_evidenceInstancesByType.TryGetValue(evidence.EvidenceTypeId, out var instances))
        {
            instances.Remove(evidenceInstanceId);
            if (instances.Count == 0)
                _evidenceInstancesByType.Remove(evidence.EvidenceTypeId);
        }
        Touch();
        return true;
    }

    internal bool AddCivilizationTrait(string traitId)
    {
        if (!_civilizationTraits.Add(traitId))
            return false;
        Touch();
        return true;
    }

    internal bool AddTrait(string traitId) => AddCivilizationTrait(traitId);

    internal bool RemoveCivilizationTrait(string traitId)
    {
        if (!_civilizationTraits.Remove(traitId))
            return false;
        Touch();
        return true;
    }

    internal bool RemoveTrait(string traitId) => RemoveCivilizationTrait(traitId);

    internal bool SetApplicabilityContextTraits(string contextId, IEnumerable<string> traitIds)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(contextId);
        var replacement = traitIds.ToHashSet(StringComparer.Ordinal);
        if (_applicabilityTraitsByContext.TryGetValue(contextId, out var existing) && existing.SetEquals(replacement))
            return false;
        if (replacement.Count == 0)
            _applicabilityTraitsByContext.Remove(contextId);
        else
            _applicabilityTraitsByContext[contextId] = replacement;
        Touch();
        return true;
    }

    internal bool AddApplicabilityTrait(string contextId, string traitId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(contextId);
        if (!_applicabilityTraitsByContext.TryGetValue(contextId, out var traits))
        {
            traits = new HashSet<string>(StringComparer.Ordinal);
            _applicabilityTraitsByContext.Add(contextId, traits);
        }
        if (!traits.Add(traitId))
            return false;
        Touch();
        return true;
    }

    internal bool RemoveApplicabilityTrait(string contextId, string traitId)
    {
        if (!_applicabilityTraitsByContext.TryGetValue(contextId, out var traits) || !traits.Remove(traitId))
            return false;
        if (traits.Count == 0)
            _applicabilityTraitsByContext.Remove(contextId);
        Touch();
        return true;
    }

    internal bool AddCapability(string capabilityId, string? contextId)
    {
        if (!_capabilities.Add(new ResearchCapabilityKey(capabilityId, contextId)))
            return false;
        Touch();
        return true;
    }

    internal bool RemoveCapability(string capabilityId, string? contextId)
    {
        if (!_capabilities.Remove(new ResearchCapabilityKey(capabilityId, contextId)))
            return false;
        Touch();
        return true;
    }

    internal bool AddFacilityCapability(string capabilityId)
    {
        if (!_facilityCapabilities.Add(capabilityId))
            return false;
        Touch();
        return true;
    }

    internal bool RemoveFacilityCapability(string capabilityId)
    {
        if (!_facilityCapabilities.Remove(capabilityId))
            return false;
        Touch();
        return true;
    }

    internal bool AddEnabledDeploymentEvent(string deploymentEventId)
    {
        if (!_enabledDeploymentEventIds.Add(deploymentEventId))
            return false;
        Touch();
        return true;
    }

    internal void SetDirectedProgramStage(string stageId)
    {
        if (string.Equals(DirectedProgramStageId, stageId, StringComparison.Ordinal))
            return;
        DirectedProgramStageId = stageId;
        Touch();
    }

    internal void SetNodeState(ResearchNodeRuntimeState state)
    {
        _nodeStates[state.NodeId] = state with { Revision = Revision + 1 };
        Touch();
    }

    internal bool RemoveNodeState(string nodeId)
    {
        if (!_nodeStates.Remove(nodeId))
            return false;
        Touch();
        return true;
    }

    internal void SetProject(ResearchProjectRuntimeState project)
    {
        _activeProjects[project.NodeId] = project with { Revision = Revision + 1 };
        Touch();
    }

    internal bool RemoveProject(string nodeId)
    {
        if (!_activeProjects.Remove(nodeId))
            return false;
        Touch();
        return true;
    }

    internal void MarkViewDirty() => MaterializedViewRevision++;

    private void Touch()
    {
        Revision++;
        MaterializedViewRevision++;
    }
}
