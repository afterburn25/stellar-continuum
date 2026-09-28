using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Research.Adaptive;

public sealed record AdaptiveResearchNodeSnapshot(
    string NodeId,
    ResearchMaturity Maturity,
    string? Resolution,
    double StageResearchPoints,
    double TotalResearchPoints);

public sealed record AdaptiveResearchEvidenceSnapshot(
    string EvidenceInstanceId,
    string EvidenceTypeId,
    string Provenance,
    double Quality,
    double Confidence,
    string? ContextId);

public sealed record AdaptiveResearchCapabilitySnapshot(string CapabilityId, string? ContextId);

public sealed record AdaptiveResearchProjectSnapshot(
    string NodeId,
    ResearchMaturity Stage,
    string? TargetApplicabilityContextId,
    double AssignedEffectiveLabs,
    double ReadinessEfficiency,
    bool Paused,
    string? PauseReason,
    double StageResearchPoints,
    double TotalResearchPoints);

public sealed record AdaptiveResearchStateSnapshot(
    int SchemaVersion,
    string CatalogId,
    string CivilizationId,
    string DirectedProgramStageId,
    double TotalEffectiveResearchLabs,
    IReadOnlyList<AdaptiveResearchNodeSnapshot> Nodes,
    IReadOnlyDictionary<string, double> Pressures,
    IReadOnlyList<AdaptiveResearchEvidenceSnapshot> Evidence,
    IReadOnlyList<string> CivilizationTraits,
    IReadOnlyDictionary<string, IReadOnlyList<string>> ApplicabilityContexts,
    IReadOnlyList<AdaptiveResearchCapabilitySnapshot> Capabilities,
    IReadOnlyList<string> FacilityCapabilities,
    IReadOnlyList<string> EnabledDeploymentEventIds,
    IReadOnlyList<AdaptiveResearchProjectSnapshot> ActiveProjects);

/// <summary>
/// Standalone versioned research payload. This is not the campaign save format and does not change VERSION.
/// The shared save/integration workstream can embed this DTO at a coordinated save boundary.
/// </summary>
public sealed class AdaptiveResearchSnapshotCodec
{
    public const int CurrentSchemaVersion = 1;

    private readonly AdaptiveResearchRuntime _runtime;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    public AdaptiveResearchSnapshotCodec(AdaptiveResearchRuntime runtime)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
    }

    public AdaptiveResearchStateSnapshot Capture(AdaptiveResearchCivilizationState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        return new AdaptiveResearchStateSnapshot(
            CurrentSchemaVersion,
            _runtime.Catalog.Metadata.CatalogId,
            state.CivilizationId,
            state.DirectedProgramStageId,
            state.TotalEffectiveResearchLabs,
            state.NodeStates.Values
                .OrderBy(value => value.NodeId, StringComparer.Ordinal)
                .Select(value => new AdaptiveResearchNodeSnapshot(
                    value.NodeId,
                    value.Maturity,
                    value.Resolution,
                    value.StageResearchPoints,
                    value.TotalResearchPoints))
                .ToArray(),
            state.Pressures.OrderBy(pair => pair.Key, StringComparer.Ordinal)
                .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal),
            state.EvidenceInstances.Values
                .OrderBy(value => value.EvidenceInstanceId, StringComparer.Ordinal)
                .Select(value => new AdaptiveResearchEvidenceSnapshot(
                    value.EvidenceInstanceId,
                    value.EvidenceTypeId,
                    value.Provenance,
                    value.Quality,
                    value.Confidence,
                    value.ContextId))
                .ToArray(),
            state.CivilizationTraits.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
            state.ApplicabilityContexts.OrderBy(pair => pair.Key, StringComparer.Ordinal)
                .ToDictionary(
                    pair => pair.Key,
                    pair => (IReadOnlyList<string>)pair.Value.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
                    StringComparer.Ordinal),
            state.Capabilities
                .OrderBy(value => value.CapabilityId, StringComparer.Ordinal)
                .ThenBy(value => value.ContextId, StringComparer.Ordinal)
                .Select(value => new AdaptiveResearchCapabilitySnapshot(value.CapabilityId, value.ContextId))
                .ToArray(),
            state.FacilityCapabilities.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
            state.EnabledDeploymentEventIds.OrderBy(value => value, StringComparer.Ordinal).ToArray(),
            state.ActiveProjects.Values
                .OrderBy(value => value.NodeId, StringComparer.Ordinal)
                .Select(value => new AdaptiveResearchProjectSnapshot(
                    value.NodeId,
                    value.Stage,
                    value.TargetApplicabilityContextId,
                    value.AssignedEffectiveLabs,
                    value.ReadinessEfficiency,
                    value.Paused,
                    value.PauseReason,
                    value.StageResearchPoints,
                    value.TotalResearchPoints))
                .ToArray());
    }

    public string Serialize(AdaptiveResearchCivilizationState state) =>
        JsonSerializer.Serialize(Capture(state), _jsonOptions);

    public AdaptiveResearchCivilizationState Deserialize(string json)
    {
        var snapshot = JsonSerializer.Deserialize<AdaptiveResearchStateSnapshot>(json, _jsonOptions)
            ?? throw new InvalidDataException("Adaptive Research snapshot deserialized to null.");
        return Restore(snapshot);
    }

    public AdaptiveResearchCivilizationState Restore(AdaptiveResearchStateSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research snapshot schema {snapshot.SchemaVersion}; expected {CurrentSchemaVersion}.");
        if (!string.Equals(snapshot.CatalogId, _runtime.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research snapshot catalog '{snapshot.CatalogId}' does not match runtime catalog '{_runtime.Catalog.Metadata.CatalogId}'.");
        _runtime.Catalog.GetDirectedProgramStage(snapshot.DirectedProgramStageId);

        var state = new AdaptiveResearchCivilizationState(snapshot.CivilizationId, snapshot.DirectedProgramStageId);
        state.SetTotalEffectiveResearchLabs(snapshot.TotalEffectiveResearchLabs);

        foreach (var pair in snapshot.Pressures)
        {
            if (!_runtime.Catalog.PressureIds.Contains(pair.Key))
                throw new InvalidDataException($"Snapshot references unknown Research Pressure '{pair.Key}'.");
            state.SetPressure(pair.Key, pair.Value);
        }

        foreach (var evidence in snapshot.Evidence)
        {
            if (!_runtime.Catalog.EvidenceTypeIds.Contains(evidence.EvidenceTypeId))
                throw new InvalidDataException($"Snapshot evidence '{evidence.EvidenceInstanceId}' references unknown evidence type '{evidence.EvidenceTypeId}'.");
            ValidateUnitInterval(evidence.Quality, $"evidence {evidence.EvidenceInstanceId} quality");
            ValidateUnitInterval(evidence.Confidence, $"evidence {evidence.EvidenceInstanceId} confidence");
            if (!state.AddEvidence(new ResearchEvidenceInstance(
                    evidence.EvidenceInstanceId,
                    evidence.EvidenceTypeId,
                    evidence.Provenance,
                    evidence.Quality,
                    evidence.Confidence,
                    evidence.ContextId,
                    state.Revision + 1)))
                throw new InvalidDataException($"Duplicate evidence instance '{evidence.EvidenceInstanceId}' in snapshot.");
        }

        foreach (var traitId in snapshot.CivilizationTraits)
        {
            var trait = _runtime.Applicability.GetTrait(traitId);
            if (trait.Scope != ResearchApplicabilityTraitScope.Civilization)
                throw new InvalidDataException($"Population-scoped trait '{traitId}' was stored as a civilization trait.");
            state.AddCivilizationTrait(traitId);
        }

        foreach (var pair in snapshot.ApplicabilityContexts)
        {
            foreach (var traitId in pair.Value)
            {
                var trait = _runtime.Applicability.GetTrait(traitId);
                if (trait.Scope != ResearchApplicabilityTraitScope.PopulationOrSpecies)
                    throw new InvalidDataException($"Civilization-scoped trait '{traitId}' was stored in applicability context '{pair.Key}'.");
            }
            state.SetApplicabilityContextTraits(pair.Key, pair.Value);
        }

        foreach (var capability in snapshot.Capabilities)
        {
            if (!_runtime.Catalog.Capabilities.TryGetValue(capability.CapabilityId, out var definition))
                throw new InvalidDataException($"Snapshot references unknown capability '{capability.CapabilityId}'.");
            if (definition.Scope == ResearchCapabilityScope.Civilization && capability.ContextId is not null)
                throw new InvalidDataException($"Civilization capability '{capability.CapabilityId}' has an invalid target context.");
            if (definition.Scope != ResearchCapabilityScope.Civilization && capability.ContextId is null)
                throw new InvalidDataException($"Scoped capability '{capability.CapabilityId}' is missing its target context.");
            state.AddCapability(capability.CapabilityId, capability.ContextId);
        }

        foreach (var facilityCapabilityId in snapshot.FacilityCapabilities)
        {
            if (!_runtime.Facilities.FacilityCapabilityIds.Contains(facilityCapabilityId))
                throw new InvalidDataException($"Snapshot references unknown facility capability '{facilityCapabilityId}'.");
            state.AddFacilityCapability(facilityCapabilityId);
        }

        foreach (var deploymentEventId in snapshot.EnabledDeploymentEventIds)
        {
            if (!_runtime.Catalog.DeploymentEvents.ContainsKey(deploymentEventId))
                throw new InvalidDataException($"Snapshot references unknown research-enabled deployment event '{deploymentEventId}'.");
            state.AddEnabledDeploymentEvent(deploymentEventId);
        }

        foreach (var node in snapshot.Nodes)
        {
            _runtime.Catalog.GetNode(node.NodeId);
            if (node.Maturity is < ResearchMaturity.Rumored or > ResearchMaturity.Archived)
                throw new InvalidDataException($"Node '{node.NodeId}' has invalid maturity '{node.Maturity}'.");
            if (node.StageResearchPoints < 0.0 || node.TotalResearchPoints < 0.0)
                throw new InvalidDataException($"Node '{node.NodeId}' has negative research progress.");
            state.SetNodeState(new ResearchNodeRuntimeState(
                node.NodeId,
                node.Maturity,
                node.Resolution,
                node.StageResearchPoints,
                node.TotalResearchPoints,
                state.Revision + 1));
        }

        foreach (var project in snapshot.ActiveProjects)
        {
            var definition = _runtime.Catalog.GetNode(project.NodeId);
            if (project.Stage is not (ResearchMaturity.Experimental or ResearchMaturity.Demonstrated or ResearchMaturity.Engineering))
                throw new InvalidDataException($"Project '{project.NodeId}' has invalid active stage '{project.Stage}'.");
            if (!state.TryGetNodeState(project.NodeId, out var nodeState))
                throw new InvalidDataException($"Project '{project.NodeId}' has no visible node state.");
            if (nodeState.Maturity != project.Stage)
                throw new InvalidDataException($"Project '{project.NodeId}' stage '{project.Stage}' does not match node state '{nodeState.Maturity}'.");
            if (project.AssignedEffectiveLabs + 0.000001 < definition.ProjectRequirements.MinimumLabs)
                throw new InvalidDataException($"Project '{project.NodeId}' is below its minimum lab requirement.");
            if (project.ReadinessEfficiency <= 0.0 || double.IsNaN(project.ReadinessEfficiency) || double.IsInfinity(project.ReadinessEfficiency))
                throw new InvalidDataException($"Project '{project.NodeId}' has invalid readiness efficiency.");
            if (project.StageResearchPoints < 0.0 || project.TotalResearchPoints < 0.0)
                throw new InvalidDataException($"Project '{project.NodeId}' has negative research progress.");

            state.SetProject(new ResearchProjectRuntimeState(
                project.NodeId,
                project.Stage,
                project.TargetApplicabilityContextId,
                project.AssignedEffectiveLabs,
                project.ReadinessEfficiency,
                project.Paused,
                project.PauseReason,
                project.StageResearchPoints,
                project.TotalResearchPoints,
                state.Revision + 1));
        }

        if (state.AssignedEffectiveLabs > state.TotalEffectiveResearchLabs + 0.000001)
            throw new InvalidDataException("Active Adaptive Research projects assign more labs than the restored total Effective Research Lab capacity.");

        return state;
    }

    private static void ValidateUnitInterval(double value, string name)
    {
        if (value is < 0.0 or > 1.0 || double.IsNaN(value) || double.IsInfinity(value))
            throw new InvalidDataException($"Invalid {name}: {value}.");
    }
}
