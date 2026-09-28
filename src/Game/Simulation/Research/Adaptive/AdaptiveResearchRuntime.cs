using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public enum AdaptiveResearchRuntimeEventType
{
    NodeBecameInvestigable,
    ProjectStarted,
    ProjectPaused,
    ProjectResumed,
    ProjectLabsChanged,
    ProjectReadinessChanged,
    StageAdvanced,
    HypothesisResolutionRequired,
    HypothesisDisproven,
    TechnologyMatured,
    CapabilityGranted,
    CivilizationTraitGranted,
    DirectedProgramStageChanged,
    DeploymentEventUnlocked,
}

public sealed record AdaptiveResearchRuntimeEvent(
    AdaptiveResearchRuntimeEventType Type,
    string CivilizationId,
    string? NodeId,
    string? SubjectId,
    string Message);

public sealed record AdaptiveResearchCommandResult(
    bool Accepted,
    string Message,
    IReadOnlyList<AdaptiveResearchRuntimeEvent> Events,
    IReadOnlyList<ResearchBlocker> Blockers)
{
    public static AdaptiveResearchCommandResult Rejected(string message, IReadOnlyList<ResearchBlocker>? blockers = null) =>
        new(false, message, Array.Empty<AdaptiveResearchRuntimeEvent>(), blockers ?? Array.Empty<ResearchBlocker>());
}

/// <summary>
/// Isolated plain-C# Adaptive Research runtime kernel. It does not replace the legacy prototype ResearchSimulation.
/// All candidate wakeups are index/event driven; only explicitly supplied basic-science candidates may be reviewed in batches.
/// </summary>
public sealed class AdaptiveResearchRuntime
{
    public AdaptiveResearchRuntime(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchApplicabilityCatalog applicability,
        AdaptiveResearchFacilityCatalog facilities,
        AdaptiveResearchProgressPolicy progressPolicy)
    {
        Catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        Applicability = applicability ?? throw new ArgumentNullException(nameof(applicability));
        Facilities = facilities ?? throw new ArgumentNullException(nameof(facilities));
        ProgressPolicy = progressPolicy ?? throw new ArgumentNullException(nameof(progressPolicy));
        Eligibility = new AdaptiveResearchEligibilityEvaluator(Catalog, Applicability, Facilities);
        ViewBuilder = new AdaptiveResearchViewBuilder(Catalog, Eligibility, ProgressPolicy);
    }

    public AdaptiveResearchCatalog Catalog { get; }
    public AdaptiveResearchApplicabilityCatalog Applicability { get; }
    public AdaptiveResearchFacilityCatalog Facilities { get; }
    public AdaptiveResearchProgressPolicy ProgressPolicy { get; }
    public AdaptiveResearchEligibilityEvaluator Eligibility { get; }
    public AdaptiveResearchViewBuilder ViewBuilder { get; }

    public static AdaptiveResearchRuntime LoadFromDirectory(string rootPath)
    {
        var catalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(rootPath);
        return new AdaptiveResearchRuntime(
            catalog,
            AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(rootPath, catalog),
            AdaptiveResearchFacilityCatalog.LoadFromDirectory(rootPath, catalog),
            AdaptiveResearchProgressPolicy.LoadFromDirectory(rootPath, catalog));
    }

    public AdaptiveResearchCivilizationState CreateCivilizationState(string civilizationId) =>
        new(civilizationId, Catalog.Metadata.StartingDirectedProgramStageId);

    public AdaptiveResearchView BuildView(
        AdaptiveResearchCivilizationState state,
        string? defaultTargetApplicabilityContextId = null) =>
        ViewBuilder.Build(state, defaultTargetApplicabilityContextId);

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> SetTotalEffectiveResearchLabs(
        AdaptiveResearchCivilizationState state,
        double totalLabs)
    {
        state.SetTotalEffectiveResearchLabs(totalLabs);
        return Array.Empty<AdaptiveResearchRuntimeEvent>();
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> SetPressure(
        AdaptiveResearchCivilizationState state,
        string pressureId,
        double value,
        string? targetApplicabilityContextId = null)
    {
        if (!Catalog.PressureIds.Contains(pressureId))
            throw new ArgumentException($"Unknown Research Pressure '{pressureId}'.", nameof(pressureId));
        if (!state.SetPressure(pressureId, value))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();
        return WakeIndexedCandidates(state, Catalog.NodesByPressure, pressureId, targetApplicabilityContextId);
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> AddEvidence(
        AdaptiveResearchCivilizationState state,
        string evidenceInstanceId,
        string evidenceTypeId,
        string provenance,
        double quality,
        double confidence,
        string? contextId = null)
    {
        if (!Catalog.EvidenceTypeIds.Contains(evidenceTypeId))
            throw new ArgumentException($"Unknown evidence type '{evidenceTypeId}'.", nameof(evidenceTypeId));
        if (quality is < 0.0 or > 1.0 || double.IsNaN(quality))
            throw new ArgumentOutOfRangeException(nameof(quality));
        if (confidence is < 0.0 or > 1.0 || double.IsNaN(confidence))
            throw new ArgumentOutOfRangeException(nameof(confidence));

        var evidence = new ResearchEvidenceInstance(
            evidenceInstanceId,
            evidenceTypeId,
            provenance,
            quality,
            confidence,
            contextId,
            state.Revision + 1);
        if (!state.AddEvidence(evidence))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();
        return WakeIndexedCandidates(state, Catalog.NodesByEvidence, evidenceTypeId, contextId);
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> AddCivilizationTrait(
        AdaptiveResearchCivilizationState state,
        string traitId)
    {
        var definition = Applicability.GetTrait(traitId);
        if (definition.Scope != ResearchApplicabilityTraitScope.Civilization)
            throw new ArgumentException($"Trait '{traitId}' must be attached to a population/species applicability context.", nameof(traitId));
        if (!state.AddCivilizationTrait(traitId))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();
        return WakeIndexedCandidates(state, Catalog.NodesByTrait, traitId, null);
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> SetApplicabilityContextTraits(
        AdaptiveResearchCivilizationState state,
        string contextId,
        IEnumerable<string> traitIds)
    {
        var traits = traitIds.Distinct(StringComparer.Ordinal).ToArray();
        foreach (var traitId in traits)
        {
            var definition = Applicability.GetTrait(traitId);
            if (definition.Scope != ResearchApplicabilityTraitScope.PopulationOrSpecies)
                throw new ArgumentException($"Civilization-scoped trait '{traitId}' cannot be placed on applicability context '{contextId}'.", nameof(traitIds));
        }

        if (!state.SetApplicabilityContextTraits(contextId, traits))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();

        var candidates = traits
            .Where(Catalog.NodesByTrait.ContainsKey)
            .SelectMany(traitId => Catalog.NodesByTrait[traitId])
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        return MaterializeEligibleCandidates(state, candidates, contextId);
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> AddCapability(
        AdaptiveResearchCivilizationState state,
        string capabilityId,
        string? contextId = null)
    {
        if (!Catalog.Capabilities.TryGetValue(capabilityId, out var definition))
            throw new ArgumentException($"Unknown cross-lineage capability '{capabilityId}'.", nameof(capabilityId));
        ValidateCapabilityContext(definition, contextId);
        var events = new List<AdaptiveResearchRuntimeEvent>();
        GrantCapabilityWithImplications(state, capabilityId, contextId, null, events);
        return events;
    }

    public void AddFacilityCapability(AdaptiveResearchCivilizationState state, string facilityCapabilityId)
    {
        if (!Facilities.FacilityCapabilityIds.Contains(facilityCapabilityId))
            throw new ArgumentException($"Unknown research facility capability '{facilityCapabilityId}'.", nameof(facilityCapabilityId));
        state.AddFacilityCapability(facilityCapabilityId);
    }

    public void RemoveFacilityCapability(AdaptiveResearchCivilizationState state, string facilityCapabilityId) =>
        state.RemoveFacilityCapability(facilityCapabilityId);

    /// <summary>
    /// Low-frequency basic-science review. Caller supplies a bounded batch selected by its scheduler/index policy;
    /// this method deliberately does not scan the full catalog.
    /// </summary>
    public IReadOnlyList<AdaptiveResearchRuntimeEvent> ReviewBasicScienceCandidates(
        AdaptiveResearchCivilizationState state,
        IEnumerable<string> boundedCandidateNodeIds,
        string? targetApplicabilityContextId = null) =>
        MaterializeEligibleCandidates(state, boundedCandidateNodeIds, targetApplicabilityContextId);

    public AdaptiveResearchCommandResult StartDirectedResearch(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs,
        double readinessScore = 60.0,
        string? targetApplicabilityContextId = null)
    {
        var eligibility = Eligibility.EvaluateProjectStart(state, nodeId, requestedAssignedLabs, targetApplicabilityContextId);
        if (!eligibility.Allowed)
            return AdaptiveResearchCommandResult.Rejected("Research project cannot start under current conditions.", eligibility.Blockers);

        var readiness = ProgressPolicy.GetReadinessEfficiency(readinessScore);
        var nodeState = state.NodeStates[nodeId];
        var project = new ResearchProjectRuntimeState(
            nodeId,
            ResearchMaturity.Experimental,
            targetApplicabilityContextId,
            requestedAssignedLabs,
            readiness,
            false,
            null,
            0.0,
            nodeState.TotalResearchPoints,
            state.Revision + 1);
        state.SetProject(project);
        state.SetNodeState(nodeState with
        {
            Maturity = ResearchMaturity.Experimental,
            StageResearchPoints = 0.0,
            Revision = state.Revision + 1,
        });

        var runtimeEvent = new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.ProjectStarted,
            state.CivilizationId,
            nodeId,
            null,
            $"Directed research started: {Catalog.GetNode(nodeId).Name}.");
        return new AdaptiveResearchCommandResult(true, runtimeEvent.Message, new[] { runtimeEvent }, Array.Empty<ResearchBlocker>());
    }

    public AdaptiveResearchCommandResult PauseDirectedResearch(AdaptiveResearchCivilizationState state, string nodeId)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return AdaptiveResearchCommandResult.Rejected("No active research project exists for that node.");
        if (project.Paused)
            return new AdaptiveResearchCommandResult(true, "Research project is already paused.", Array.Empty<AdaptiveResearchRuntimeEvent>(), Array.Empty<ResearchBlocker>());

        state.SetProject(project with { Paused = true, PauseReason = "paused_by_order" });
        var runtimeEvent = new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.ProjectPaused,
            state.CivilizationId,
            nodeId,
            null,
            "Research project paused; accumulated scientific work is preserved.");
        return new AdaptiveResearchCommandResult(true, runtimeEvent.Message, new[] { runtimeEvent }, Array.Empty<ResearchBlocker>());
    }

    public AdaptiveResearchCommandResult ResumeDirectedResearch(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs,
        double readinessScore)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project) || !project.Paused)
            return AdaptiveResearchCommandResult.Rejected("The project is not currently paused.");

        var blockers = new List<ResearchBlocker>();
        blockers.AddRange(Eligibility.EvaluateScientificEligibility(state, nodeId, project.TargetApplicabilityContextId).Blockers);
        blockers.AddRange(Eligibility.EvaluateStageFacilityEligibility(state, nodeId, project.Stage).Blockers);

        var definition = Catalog.GetNode(nodeId);
        if (requestedAssignedLabs + 0.000001 < definition.ProjectRequirements.MinimumLabs)
            blockers.Add(new ResearchBlocker(ResearchBlockerCode.BelowMinimumAssignedLabs, nodeId, definition.ProjectRequirements.MinimumLabs, requestedAssignedLabs, "The project needs more assigned Effective Research Labs."));
        if (state.FreeEffectiveLabs + 0.000001 < requestedAssignedLabs)
            blockers.Add(new ResearchBlocker(ResearchBlockerCode.InsufficientFreeLabs, nodeId, requestedAssignedLabs, state.FreeEffectiveLabs, "Not enough free Effective Research Labs are available."));

        var stage = Catalog.GetDirectedProgramStage(state.DirectedProgramStageId);
        var activeCount = state.ActiveProjects.Values.Count(active => !active.Paused);
        if (stage.DirectedProgramLimit is int limit && activeCount >= limit)
            blockers.Add(new ResearchBlocker(ResearchBlockerCode.DirectedProgramCapacity, stage.Id, limit, activeCount, "No directed research-program capacity is free."));

        if (blockers.Count > 0)
            return AdaptiveResearchCommandResult.Rejected("Research project cannot resume under current conditions.", blockers);

        state.SetProject(project with
        {
            Paused = false,
            PauseReason = null,
            AssignedEffectiveLabs = requestedAssignedLabs,
            ReadinessEfficiency = ProgressPolicy.GetReadinessEfficiency(readinessScore),
        });
        var runtimeEvent = new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.ProjectResumed,
            state.CivilizationId,
            nodeId,
            null,
            "Research project resumed.");
        return new AdaptiveResearchCommandResult(true, runtimeEvent.Message, new[] { runtimeEvent }, Array.Empty<ResearchBlocker>());
    }

    public AdaptiveResearchCommandResult ReallocateResearchLabs(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return AdaptiveResearchCommandResult.Rejected("No research project exists for that node.");
        var definition = Catalog.GetNode(nodeId);
        if (requestedAssignedLabs + 0.000001 < definition.ProjectRequirements.MinimumLabs)
            return AdaptiveResearchCommandResult.Rejected("Allocation is below the project's minimum lab requirement.");

        var capacityAvailable = state.FreeEffectiveLabs + (project.Paused ? 0.0 : project.AssignedEffectiveLabs);
        if (capacityAvailable + 0.000001 < requestedAssignedLabs)
            return AdaptiveResearchCommandResult.Rejected("Not enough Effective Research Labs are available for that allocation.");

        state.SetProject(project with { AssignedEffectiveLabs = requestedAssignedLabs });
        var runtimeEvent = new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.ProjectLabsChanged,
            state.CivilizationId,
            nodeId,
            null,
            $"Research lab allocation changed to {requestedAssignedLabs:0.##} effective labs.");
        return new AdaptiveResearchCommandResult(true, runtimeEvent.Message, new[] { runtimeEvent }, Array.Empty<ResearchBlocker>());
    }

    public AdaptiveResearchCommandResult SetProjectReadiness(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double readinessScore)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return AdaptiveResearchCommandResult.Rejected("No research project exists for that node.");
        var efficiency = ProgressPolicy.GetReadinessEfficiency(readinessScore);
        state.SetProject(project with { ReadinessEfficiency = efficiency });
        var runtimeEvent = new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.ProjectReadinessChanged,
            state.CivilizationId,
            nodeId,
            null,
            "Research readiness was recalculated from current competence/facility/evidence/tacit inputs.");
        return new AdaptiveResearchCommandResult(true, runtimeEvent.Message, new[] { runtimeEvent }, Array.Empty<ResearchBlocker>());
    }

    public IReadOnlyList<AdaptiveResearchRuntimeEvent> AdvanceProjects(
        AdaptiveResearchCivilizationState state,
        double elapsedYears)
    {
        if (elapsedYears < 0.0 || double.IsNaN(elapsedYears) || double.IsInfinity(elapsedYears))
            throw new ArgumentOutOfRangeException(nameof(elapsedYears));
        if (elapsedYears <= 0.0)
            return Array.Empty<AdaptiveResearchRuntimeEvent>();

        var events = new List<AdaptiveResearchRuntimeEvent>();
        foreach (var nodeId in state.ActiveProjects.Keys.ToArray())
        {
            if (!state.ActiveProjects.TryGetValue(nodeId, out var project) || project.Paused)
                continue;

            var scientific = Eligibility.EvaluateScientificEligibility(state, nodeId, project.TargetApplicabilityContextId);
            var facility = Eligibility.EvaluateStageFacilityEligibility(state, nodeId, project.Stage);
            if (!scientific.Allowed || !facility.Allowed)
            {
                var reason = scientific.Blockers.Concat(facility.Blockers).FirstOrDefault()?.Message ?? "research requirements changed";
                state.SetProject(project with { Paused = true, PauseReason = reason });
                events.Add(new AdaptiveResearchRuntimeEvent(
                    AdaptiveResearchRuntimeEventType.ProjectPaused,
                    state.CivilizationId,
                    nodeId,
                    null,
                    $"Research paused: {reason}"));
                continue;
            }

            var node = Catalog.GetNode(nodeId);
            var scaledLabs = Catalog.LabScaling.ScaleAssignedLabs(project.AssignedEffectiveLabs, node.ProjectRequirements.RecommendedLabs);
            var availableRp = scaledLabs * Catalog.Metadata.BaseRpPerEffectiveLabPerYear * project.ReadinessEfficiency * elapsedYears;
            AdvanceOneProject(state, node, project, availableRp, events);
        }
        return events;
    }

    public AdaptiveResearchCommandResult ResolveHypothesis(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        bool supported)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project) || !project.Paused ||
            !string.Equals(project.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal))
            return AdaptiveResearchCommandResult.Rejected("No hypothesis is awaiting scientific resolution for that node.");
        var node = Catalog.GetNode(nodeId);
        if (!node.IsHypothesis)
            return AdaptiveResearchCommandResult.Rejected("That node is not a scientific hypothesis.");

        var events = new List<AdaptiveResearchRuntimeEvent>();
        if (!supported)
        {
            state.RemoveProject(nodeId);
            state.SetNodeState(new ResearchNodeRuntimeState(
                nodeId,
                ResearchMaturity.Archived,
                "disproven",
                0.0,
                project.TotalResearchPoints,
                state.Revision + 1));
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.HypothesisDisproven,
                state.CivilizationId,
                nodeId,
                null,
                $"{node.Name} was disproven; accumulated negative knowledge is preserved."));
        }
        else
        {
            var next = project with
            {
                Stage = ResearchMaturity.Demonstrated,
                StageResearchPoints = 0.0,
                Paused = false,
                PauseReason = null,
            };
            state.SetProject(next);
            state.SetNodeState(new ResearchNodeRuntimeState(
                nodeId,
                ResearchMaturity.Demonstrated,
                null,
                0.0,
                project.TotalResearchPoints,
                state.Revision + 1));
            ApplyStageGrant(state, node, ResearchMaturity.Demonstrated, project.TargetApplicabilityContextId, events);
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.StageAdvanced,
                state.CivilizationId,
                nodeId,
                null,
                $"Evidence supports {node.Name}; the principle is Demonstrated."));
        }

        return new AdaptiveResearchCommandResult(true, events[^1].Message, events, Array.Empty<ResearchBlocker>());
    }

    private void AdvanceOneProject(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        ResearchProjectRuntimeState initialProject,
        double availableRp,
        ICollection<AdaptiveResearchRuntimeEvent> events)
    {
        var project = initialProject;
        while (availableRp > 0.000001 && state.ActiveProjects.ContainsKey(node.Id) && !project.Paused)
        {
            var stageWork = ProgressPolicy.GetStageWork(node, project.Stage);
            var remainingStage = Math.Max(0.0, stageWork - project.StageResearchPoints);
            var spend = Math.Min(availableRp, remainingStage);
            project = project with
            {
                StageResearchPoints = project.StageResearchPoints + spend,
                TotalResearchPoints = project.TotalResearchPoints + spend,
            };
            availableRp -= spend;
            state.SetProject(project);
            state.SetNodeState(new ResearchNodeRuntimeState(
                node.Id,
                project.Stage,
                null,
                project.StageResearchPoints,
                project.TotalResearchPoints,
                state.Revision + 1));

            if (project.StageResearchPoints + 0.000001 < stageWork)
                break;

            if (project.Stage == ResearchMaturity.Experimental && node.IsHypothesis)
            {
                project = project with { Paused = true, PauseReason = "hypothesis_resolution_required" };
                state.SetProject(project);
                events.Add(new AdaptiveResearchRuntimeEvent(
                    AdaptiveResearchRuntimeEventType.HypothesisResolutionRequired,
                    state.CivilizationId,
                    node.Id,
                    null,
                    $"{node.Name} reached its experimental evidence boundary and requires scientific resolution."));
                break;
            }

            if (project.Stage == ResearchMaturity.Engineering)
            {
                MatureTechnology(state, node, project, events);
                break;
            }

            var nextStage = project.Stage == ResearchMaturity.Experimental
                ? ResearchMaturity.Demonstrated
                : ResearchMaturity.Engineering;
            var nextFacility = Eligibility.EvaluateStageFacilityEligibility(state, node.Id, nextStage);
            if (!nextFacility.Allowed)
            {
                project = project with { Paused = true, PauseReason = nextFacility.Blockers[0].Message };
                state.SetProject(project);
                events.Add(new AdaptiveResearchRuntimeEvent(
                    AdaptiveResearchRuntimeEventType.ProjectPaused,
                    state.CivilizationId,
                    node.Id,
                    nextFacility.Blockers[0].SubjectId,
                    $"Research paused before {nextStage}: {nextFacility.Blockers[0].Message}"));
                break;
            }

            project = project with { Stage = nextStage, StageResearchPoints = 0.0 };
            state.SetProject(project);
            state.SetNodeState(new ResearchNodeRuntimeState(
                node.Id,
                nextStage,
                null,
                0.0,
                project.TotalResearchPoints,
                state.Revision + 1));
            ApplyStageGrant(state, node, nextStage, project.TargetApplicabilityContextId, events);
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.StageAdvanced,
                state.CivilizationId,
                node.Id,
                null,
                $"{node.Name} advanced to {nextStage}."));
        }
    }

    private void MatureTechnology(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        ResearchProjectRuntimeState project,
        ICollection<AdaptiveResearchRuntimeEvent> events)
    {
        state.RemoveProject(node.Id);
        state.SetNodeState(new ResearchNodeRuntimeState(
            node.Id,
            ResearchMaturity.Mature,
            null,
            0.0,
            node.ProjectRequirements.BaseResearchPoints,
            state.Revision + 1));

        foreach (var capabilityId in node.DeclaredCapabilities.Where(Catalog.Capabilities.ContainsKey))
            GrantCapabilityWithImplications(state, capabilityId, ContextForCapability(capabilityId, project.TargetApplicabilityContextId), node.Id, events);
        ApplyStageGrant(state, node, ResearchMaturity.Mature, project.TargetApplicabilityContextId, events);

        events.Add(new AdaptiveResearchRuntimeEvent(
            AdaptiveResearchRuntimeEventType.TechnologyMatured,
            state.CivilizationId,
            node.Id,
            null,
            $"{node.Name} reached Mature scientific/engineering knowledge."));

        if (Catalog.ChildrenByPrerequisite.TryGetValue(node.Id, out var children))
            foreach (var candidateEvent in MaterializeEligibleCandidates(state, children, project.TargetApplicabilityContextId))
                events.Add(candidateEvent);
    }

    private void ApplyStageGrant(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchNodeDefinition node,
        ResearchMaturity stage,
        string? targetContextId,
        ICollection<AdaptiveResearchRuntimeEvent> events)
    {
        var grants = stage switch
        {
            ResearchMaturity.Demonstrated => Catalog.DemonstratedGrants,
            ResearchMaturity.Mature => Catalog.MatureGrants,
            _ => null,
        };
        if (grants is null || !grants.TryGetValue(node.Id, out var grant))
            return;

        foreach (var capabilityId in grant.CapabilityIds)
            GrantCapabilityWithImplications(state, capabilityId, ContextForCapability(capabilityId, targetContextId), node.Id, events);

        foreach (var traitId in grant.CivilizationTraitIds)
        {
            var trait = Applicability.GetTrait(traitId);
            var changed = trait.Scope switch
            {
                ResearchApplicabilityTraitScope.Civilization => state.AddCivilizationTrait(traitId),
                ResearchApplicabilityTraitScope.PopulationOrSpecies when targetContextId is not null => state.AddApplicabilityTrait(targetContextId, traitId),
                _ => false,
            };
            if (!changed)
                continue;
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.CivilizationTraitGranted,
                state.CivilizationId,
                node.Id,
                traitId,
                $"Research established applicability trait/capability context '{traitId}'."));
            if (Catalog.NodesByTrait.TryGetValue(traitId, out var candidates))
                foreach (var candidateEvent in MaterializeEligibleCandidates(state, candidates, targetContextId))
                    events.Add(candidateEvent);
        }

        if (grant.ResearchCapacityStageId is not null &&
            !string.Equals(state.DirectedProgramStageId, grant.ResearchCapacityStageId, StringComparison.Ordinal))
        {
            state.SetDirectedProgramStage(grant.ResearchCapacityStageId);
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.DirectedProgramStageChanged,
                state.CivilizationId,
                node.Id,
                grant.ResearchCapacityStageId,
                $"Directed research coordination advanced to '{grant.ResearchCapacityStageId}'."));
        }

        foreach (var deploymentEventId in grant.EnabledDeploymentEventIds)
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.DeploymentEventUnlocked,
                state.CivilizationId,
                node.Id,
                deploymentEventId,
                $"Research now permits deployment event '{deploymentEventId}', subject to real deployment by the owning subsystem."));
    }

    private void GrantCapabilityWithImplications(
        AdaptiveResearchCivilizationState state,
        string capabilityId,
        string? contextId,
        string? sourceNodeId,
        ICollection<AdaptiveResearchRuntimeEvent> events)
    {
        var pending = new Queue<(string Id, string? Context)>();
        var visited = new HashSet<ResearchCapabilityKey>();
        pending.Enqueue((capabilityId, contextId));

        while (pending.Count > 0)
        {
            var current = pending.Dequeue();
            var key = new ResearchCapabilityKey(current.Id, current.Context);
            if (!visited.Add(key))
                continue;
            var definition = Catalog.Capabilities[current.Id];
            ValidateCapabilityContext(definition, current.Context);
            if (state.AddCapability(current.Id, current.Context))
            {
                events.Add(new AdaptiveResearchRuntimeEvent(
                    AdaptiveResearchRuntimeEventType.CapabilityGranted,
                    state.CivilizationId,
                    sourceNodeId,
                    current.Id,
                    $"Functional capability gained: {definition.Name}."));
                if (Catalog.NodesByCapabilityRequirement.TryGetValue(current.Id, out var candidates))
                    foreach (var candidateEvent in MaterializeEligibleCandidates(state, candidates, current.Context))
                        events.Add(candidateEvent);
            }

            foreach (var implication in Catalog.CapabilityImplications.Where(implication => string.Equals(implication.FromCapabilityId, current.Id, StringComparison.Ordinal)))
            {
                var impliedContext = implication.PreserveTargetContext ? current.Context : null;
                pending.Enqueue((implication.ToCapabilityId, impliedContext));
            }
        }
    }

    private string? ContextForCapability(string capabilityId, string? targetContextId)
    {
        var scope = Catalog.Capabilities[capabilityId].Scope;
        return scope == ResearchCapabilityScope.Civilization ? null : targetContextId;
    }

    private void ValidateCapabilityContext(ResearchCapabilityDefinition definition, string? contextId)
    {
        if (definition.Scope == ResearchCapabilityScope.Civilization && contextId is not null)
            throw new ArgumentException($"Civilization capability '{definition.Id}' cannot be granted to target context '{contextId}'.");
        if (definition.Scope != ResearchCapabilityScope.Civilization && contextId is null)
            throw new ArgumentException($"Scoped capability '{definition.Id}' requires a target population/installation context.");
    }

    private IReadOnlyList<AdaptiveResearchRuntimeEvent> WakeIndexedCandidates(
        AdaptiveResearchCivilizationState state,
        IReadOnlyDictionary<string, IReadOnlyList<string>> index,
        string key,
        string? targetApplicabilityContextId)
    {
        if (!index.TryGetValue(key, out var candidates))
            return Array.Empty<AdaptiveResearchRuntimeEvent>();
        return MaterializeEligibleCandidates(state, candidates, targetApplicabilityContextId);
    }

    private IReadOnlyList<AdaptiveResearchRuntimeEvent> MaterializeEligibleCandidates(
        AdaptiveResearchCivilizationState state,
        IEnumerable<string> candidateNodeIds,
        string? targetApplicabilityContextId)
    {
        var events = new List<AdaptiveResearchRuntimeEvent>();
        foreach (var nodeId in candidateNodeIds.Distinct(StringComparer.Ordinal))
        {
            if (state.TryGetNodeState(nodeId, out var existing) && existing.Maturity >= ResearchMaturity.Investigable)
                continue;
            var eligibility = Eligibility.EvaluateScientificEligibility(state, nodeId, targetApplicabilityContextId);
            if (!eligibility.Allowed)
                continue;

            var totalRp = existing?.TotalResearchPoints ?? 0.0;
            state.SetNodeState(new ResearchNodeRuntimeState(
                nodeId,
                ResearchMaturity.Investigable,
                null,
                0.0,
                totalRp,
                state.Revision + 1));
            events.Add(new AdaptiveResearchRuntimeEvent(
                AdaptiveResearchRuntimeEventType.NodeBecameInvestigable,
                state.CivilizationId,
                nodeId,
                null,
                $"New research program is Investigable: {Catalog.GetNode(nodeId).Name}."));
        }
        return events;
    }
}
