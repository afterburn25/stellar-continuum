using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public enum ResearchBlockerCode
{
    UnknownNode,
    NonPublicResearch,
    MissingPrerequisite,
    MissingAlternativePrerequisite,
    MissingApplicabilityContext,
    MissingApplicabilityTrait,
    MissingEvidence,
    MissingPressure,
    MissingAlternativePressure,
    MissingCapability,
    MissingAlternativeCapability,
    NodeNotInvestigable,
    AlreadyMature,
    AlreadyActive,
    DirectedProgramCapacity,
    InsufficientFreeLabs,
    BelowMinimumAssignedLabs,
    MissingFacilityCapability,
    MissingAlternativeFacilityCapability,
}

public sealed record ResearchBlocker(
    ResearchBlockerCode Code,
    string? SubjectId,
    double? RequiredValue,
    double? ActualValue,
    string Message);

public sealed record ResearchEligibilityResult(
    bool Allowed,
    IReadOnlyList<ResearchBlocker> Blockers)
{
    public static ResearchEligibilityResult Success { get; } = new(true, Array.Empty<ResearchBlocker>());
}

public sealed class AdaptiveResearchEligibilityEvaluator
{
    private readonly AdaptiveResearchCatalog _catalog;
    private readonly AdaptiveResearchApplicabilityCatalog _applicability;
    private readonly AdaptiveResearchFacilityCatalog _facilities;

    public AdaptiveResearchEligibilityEvaluator(
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchApplicabilityCatalog applicability,
        AdaptiveResearchFacilityCatalog facilities)
    {
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
        _applicability = applicability ?? throw new ArgumentNullException(nameof(applicability));
        _facilities = facilities ?? throw new ArgumentNullException(nameof(facilities));
    }

    /// <summary>
    /// Evaluates hard scientific conditions for a candidate to be Investigable. Labs, directed-program
    /// capacity and stage facilities are intentionally excluded until the player/AI starts the project.
    /// </summary>
    public ResearchEligibilityResult EvaluateScientificEligibility(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        string? targetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!_catalog.Nodes.TryGetValue(nodeId, out var node))
            return Failure(new ResearchBlocker(ResearchBlockerCode.UnknownNode, null, null, null, "Unknown research possibility."));
        if (!node.PublicNormalResearch)
            return Failure(new ResearchBlocker(ResearchBlockerCode.NonPublicResearch, null, null, null, "This possibility is not part of normal public research."));

        var blockers = new List<ResearchBlocker>();

        foreach (var prerequisiteId in node.Prerequisites.AllOf)
        {
            if (!state.HasEstablishedKnowledge(prerequisiteId))
                blockers.Add(new ResearchBlocker(
                    ResearchBlockerCode.MissingPrerequisite,
                    prerequisiteId,
                    null,
                    null,
                    "Additional prerequisite knowledge is required."));
        }

        if (node.Prerequisites.AnyOf.Count > 0 && !node.Prerequisites.AnyOf.Any(state.HasEstablishedKnowledge))
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.MissingAlternativePrerequisite,
                null,
                null,
                null,
                "At least one alternative prerequisite knowledge path is required."));
        }

        foreach (var traitId in node.Applicability.Traits)
        {
            var trait = _applicability.GetTrait(traitId);
            var hasTrait = trait.Scope switch
            {
                ResearchApplicabilityTraitScope.Civilization => state.HasCivilizationTrait(traitId),
                ResearchApplicabilityTraitScope.PopulationOrSpecies =>
                    targetApplicabilityContextId is not null && state.HasApplicabilityTrait(targetApplicabilityContextId, traitId),
                _ => false,
            };
            if (!hasTrait)
            {
                if (trait.Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies && targetApplicabilityContextId is null)
                {
                    blockers.Add(new ResearchBlocker(
                        ResearchBlockerCode.MissingApplicabilityContext,
                        traitId,
                        null,
                        null,
                        "A target population/species research context is required."));
                }
                else
                {
                    blockers.Add(new ResearchBlocker(
                        ResearchBlockerCode.MissingApplicabilityTrait,
                        traitId,
                        null,
                        null,
                        "The target research context is not compatible with this possibility."));
                }
            }
        }

        foreach (var evidenceId in node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence).Distinct(StringComparer.Ordinal))
        {
            if (!state.HasEvidenceType(evidenceId, targetApplicabilityContextId))
                blockers.Add(new ResearchBlocker(
                    ResearchBlockerCode.MissingEvidence,
                    evidenceId,
                    null,
                    null,
                    "Required scientific evidence is not currently available."));
        }

        foreach (var requirement in node.ProjectRequirements.RequiredPressure)
        {
            var actual = state.GetPressure(requirement.Key);
            if (actual + 0.000001 < requirement.Value)
                blockers.Add(new ResearchBlocker(
                    ResearchBlockerCode.MissingPressure,
                    requirement.Key,
                    requirement.Value,
                    actual,
                    "The recognized research need/evidence pressure is below the required level."));
        }

        if (node.ProjectRequirements.RequiredPressureAny.Count > 0 &&
            !node.ProjectRequirements.RequiredPressureAny.Any(pair => state.GetPressure(pair.Key) + 0.000001 >= pair.Value))
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.MissingAlternativePressure,
                null,
                null,
                null,
                "None of the recognized conditions that would justify this program are strong enough yet."));
        }

        foreach (var capabilityId in node.CapabilityRequirements.AllOf)
        {
            if (!HasRequiredCapability(state, capabilityId, targetApplicabilityContextId))
                blockers.Add(new ResearchBlocker(
                    ResearchBlockerCode.MissingCapability,
                    capabilityId,
                    null,
                    null,
                    "A required functional capability is missing."));
        }

        if (node.CapabilityRequirements.AnyOf.Count > 0 &&
            !node.CapabilityRequirements.AnyOf.Any(capabilityId => HasRequiredCapability(state, capabilityId, targetApplicabilityContextId)))
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.MissingAlternativeCapability,
                null,
                null,
                null,
                "At least one alternative functional capability is required."));
        }

        return blockers.Count == 0 ? ResearchEligibilityResult.Success : new ResearchEligibilityResult(false, blockers);
    }

    public ResearchEligibilityResult EvaluateProjectStart(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs,
        string? targetApplicabilityContextId = null)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!_catalog.Nodes.TryGetValue(nodeId, out var node))
            return Failure(new ResearchBlocker(ResearchBlockerCode.UnknownNode, null, null, null, "Unknown research possibility."));

        var blockers = new List<ResearchBlocker>();
        var scientific = EvaluateScientificEligibility(state, nodeId, targetApplicabilityContextId);
        blockers.AddRange(scientific.Blockers);

        if (state.TryGetNodeState(nodeId, out var nodeState))
        {
            if (nodeState.Maturity == ResearchMaturity.Mature || nodeState.CountsAsEstablishedKnowledge)
                blockers.Add(new ResearchBlocker(ResearchBlockerCode.AlreadyMature, nodeId, null, null, "This knowledge is already mature/established."));
            else if (nodeState.Maturity < ResearchMaturity.Investigable)
                blockers.Add(new ResearchBlocker(ResearchBlockerCode.NodeNotInvestigable, nodeId, null, null, "The possibility is recognized but not yet Investigable."));
        }
        else
        {
            blockers.Add(new ResearchBlocker(ResearchBlockerCode.NodeNotInvestigable, null, null, null, "The possibility has not entered the visible research horizon."));
        }

        if (state.ActiveProjects.ContainsKey(nodeId))
            blockers.Add(new ResearchBlocker(ResearchBlockerCode.AlreadyActive, nodeId, null, null, "This research project is already active or paused."));

        var programStage = _catalog.GetDirectedProgramStage(state.DirectedProgramStageId);
        var activeDirectedProjects = state.ActiveProjects.Values.Count(project => !project.Paused);
        if (programStage.DirectedProgramLimit is int limit && activeDirectedProjects >= limit)
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.DirectedProgramCapacity,
                programStage.Id,
                limit,
                activeDirectedProjects,
                "The civilization has no free directed research-program capacity."));
        }

        if (requestedAssignedLabs + 0.000001 < node.ProjectRequirements.MinimumLabs)
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.BelowMinimumAssignedLabs,
                nodeId,
                node.ProjectRequirements.MinimumLabs,
                requestedAssignedLabs,
                "The project needs more assigned Effective Research Labs before it can begin."));
        }

        if (state.FreeEffectiveLabs + 0.000001 < requestedAssignedLabs)
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.InsufficientFreeLabs,
                nodeId,
                requestedAssignedLabs,
                state.FreeEffectiveLabs,
                "Not enough unassigned Effective Research Labs are available."));
        }

        AddFacilityBlockers(state, nodeId, ResearchMaturity.Experimental, blockers);
        return blockers.Count == 0 ? ResearchEligibilityResult.Success : new ResearchEligibilityResult(false, blockers);
    }

    public ResearchEligibilityResult EvaluateStageFacilityEligibility(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        ResearchMaturity stage)
    {
        var blockers = new List<ResearchBlocker>();
        AddFacilityBlockers(state, nodeId, stage, blockers);
        return blockers.Count == 0 ? ResearchEligibilityResult.Success : new ResearchEligibilityResult(false, blockers);
    }

    private bool HasRequiredCapability(
        AdaptiveResearchCivilizationState state,
        string capabilityId,
        string? targetApplicabilityContextId)
    {
        var definition = _catalog.Capabilities[capabilityId];
        return definition.Scope switch
        {
            ResearchCapabilityScope.Civilization => state.HasCapability(capabilityId, null),
            ResearchCapabilityScope.PopulationOrSpecies or ResearchCapabilityScope.ColonyOrInstallation =>
                targetApplicabilityContextId is not null && state.HasCapability(capabilityId, targetApplicabilityContextId),
            _ => false,
        };
    }

    private void AddFacilityBlockers(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        ResearchMaturity stage,
        ICollection<ResearchBlocker> blockers)
    {
        var requirement = _facilities.GetStageRequirement(nodeId, stage);
        if (requirement is null)
            return;

        foreach (var capabilityId in requirement.AllOf)
        {
            if (!state.HasFacilityCapability(capabilityId))
                blockers.Add(new ResearchBlocker(
                    ResearchBlockerCode.MissingFacilityCapability,
                    capabilityId,
                    null,
                    null,
                    "A required specialist research-facility capability is unavailable."));
        }

        if (requirement.AnyOf.Count > 0 && !requirement.AnyOf.Any(state.HasFacilityCapability))
        {
            blockers.Add(new ResearchBlocker(
                ResearchBlockerCode.MissingAlternativeFacilityCapability,
                null,
                null,
                null,
                "At least one acceptable specialist research-facility capability is required."));
        }
    }

    private static ResearchEligibilityResult Failure(ResearchBlocker blocker) =>
        new(false, new[] { blocker });
}
