using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Authoritative integration surface for Adaptive Research. Other workstreams should use this facade
/// instead of calling the milestone-13 compatibility kernel with caller-supplied readiness values.
/// </summary>
public sealed class AdaptiveResearchAuthority
{
    private const double EpsilonYears = 0.0000001;

    private AdaptiveResearchAuthority(
        AdaptiveResearchRuntime kernel,
        AdaptiveResearchExpertiseCatalog expertiseCatalog,
        AdaptiveResearchReadinessCalculator readiness,
        AdaptiveResearchExpertiseService expertise,
        AdaptiveResearchStartingProfileComposer startingProfiles)
    {
        Kernel = kernel;
        ExpertiseCatalog = expertiseCatalog;
        Readiness = readiness;
        Expertise = expertise;
        StartingProfiles = startingProfiles;
    }

    public AdaptiveResearchRuntime Kernel { get; }
    public AdaptiveResearchExpertiseCatalog ExpertiseCatalog { get; }
    public AdaptiveResearchReadinessCalculator Readiness { get; }
    public AdaptiveResearchExpertiseService Expertise { get; }
    public AdaptiveResearchStartingProfileComposer StartingProfiles { get; }

    public AdaptiveResearchCatalog Catalog => Kernel.Catalog;
    public AdaptiveResearchApplicabilityCatalog Applicability => Kernel.Applicability;
    public AdaptiveResearchFacilityCatalog Facilities => Kernel.Facilities;
    public AdaptiveResearchProgressPolicy ProgressPolicy => Kernel.ProgressPolicy;

    public static AdaptiveResearchAuthority LoadFromDirectory(string rootPath)
    {
        var kernel = AdaptiveResearchRuntime.LoadFromDirectory(rootPath);
        var expertiseCatalog = AdaptiveResearchExpertiseCatalog.LoadFromDirectory(
            rootPath,
            kernel.Catalog,
            kernel.Facilities);
        var readiness = new AdaptiveResearchReadinessCalculator(
            kernel.Catalog,
            kernel.Facilities,
            expertiseCatalog,
            kernel.ProgressPolicy);
        var expertise = new AdaptiveResearchExpertiseService(kernel.Catalog, expertiseCatalog, readiness);
        return new AdaptiveResearchAuthority(
            kernel,
            expertiseCatalog,
            readiness,
            expertise,
            new AdaptiveResearchStartingProfileComposer(kernel, rootPath));
    }

    public AdaptiveResearchCivilizationState CreateCivilizationState(string civilizationId) =>
        Kernel.CreateCivilizationState(civilizationId);

    public AdaptiveResearchStartingCompositionResult ComposeReferenceProfile(
        string civilizationId,
        string referenceProfileId,
        string primaryApplicabilityContextId,
        double activityYear = 2050.0)
    {
        var result = StartingProfiles.ComposeReferenceProfile(
            civilizationId,
            referenceProfileId,
            primaryApplicabilityContextId);

        foreach (var pair in result.Deferred.FieldCompetence)
        {
            Expertise.SeedFieldCompetence(
                result.State,
                pair.Key,
                new ResearchCompetenceVector(
                    pair.Value.Theoretical,
                    pair.Value.Experimental,
                    pair.Value.Engineering),
                activityYear);
        }

        var institutionSequence = 0;
        foreach (var seed in result.Deferred.ResearchInstitutions)
        {
            Expertise.SetInstitution(
                result.State,
                $"start:{referenceProfileId}:{seed.InstitutionArchetypeId}:{institutionSequence++}",
                seed.InstitutionArchetypeId,
                seed.Count,
                seed.Count,
                null);
        }

        // Starting tacit seed records intentionally remain deferred unless they contain enough
        // runtime assimilation/depth information to instantiate a real asset. The public seed currently
        // contains none; future profile-schema expansion can add that without inventing hidden defaults.
        RecalculateAllActiveReadiness(result.State);
        return result;
    }

    public ResearchReadinessBreakdown GetProjectReadiness(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        ResearchMaturity stage,
        double assignedEffectiveLabs,
        string? targetApplicabilityContextId = null) =>
        Expertise.CalculateProjectReadiness(
            state,
            nodeId,
            stage,
            assignedEffectiveLabs,
            targetApplicabilityContextId);

    public AdaptiveResearchCommandResult StartDirectedResearch(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs,
        string? targetApplicabilityContextId = null)
    {
        var readiness = GetProjectReadiness(
            state,
            nodeId,
            ResearchMaturity.Experimental,
            requestedAssignedLabs,
            targetApplicabilityContextId);
        return Kernel.StartDirectedResearch(
            state,
            nodeId,
            requestedAssignedLabs,
            readiness.OverallReadinessScore,
            targetApplicabilityContextId);
    }

    public AdaptiveResearchCommandResult PauseDirectedResearch(
        AdaptiveResearchCivilizationState state,
        string nodeId) =>
        Kernel.PauseDirectedResearch(state, nodeId);

    public AdaptiveResearchCommandResult ResumeDirectedResearch(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return AdaptiveResearchCommandResult.Rejected("The project is not currently paused.");
        var readiness = GetProjectReadiness(
            state,
            nodeId,
            project.Stage,
            requestedAssignedLabs,
            project.TargetApplicabilityContextId);
        return Kernel.ResumeDirectedResearch(
            state,
            nodeId,
            requestedAssignedLabs,
            readiness.OverallReadinessScore);
    }

    public AdaptiveResearchCommandResult ReallocateResearchLabs(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        double requestedAssignedLabs)
    {
        var result = Kernel.ReallocateResearchLabs(state, nodeId, requestedAssignedLabs);
        if (result.Accepted)
            RecalculateProjectReadiness(state, nodeId);
        return result;
    }

    public AdaptiveResearchCommandResult ResolveHypothesis(
        AdaptiveResearchCivilizationState state,
        string nodeId,
        bool supported)
    {
        var result = Kernel.ResolveHypothesis(state, nodeId, supported);
        if (result.Accepted && supported && state.ActiveProjects.ContainsKey(nodeId))
            RecalculateProjectReadiness(state, nodeId);
        return result;
    }

    /// <summary>
    /// Advances research in stage-boundary chunks so every completed stage can change competence and
    /// therefore the readiness of the next stage before additional RP is spent.
    /// </summary>
    public IReadOnlyList<AdaptiveResearchRuntimeEvent> AdvanceProjects(
        AdaptiveResearchCivilizationState state,
        double elapsedYears,
        double currentYear)
    {
        if (elapsedYears < 0.0 || double.IsNaN(elapsedYears) || double.IsInfinity(elapsedYears))
            throw new ArgumentOutOfRangeException(nameof(elapsedYears));
        if (elapsedYears <= 0.0)
            return Array.Empty<AdaptiveResearchRuntimeEvent>();

        var events = new List<AdaptiveResearchRuntimeEvent>();
        var remaining = elapsedYears;
        var cursorYear = currentYear - elapsedYears;
        var guard = 0;

        while (remaining > EpsilonYears && guard++ < 4096)
        {
            RecalculateAllActiveReadiness(state);
            var active = state.ActiveProjects.Values.Where(project => !project.Paused).ToArray();
            if (active.Length == 0)
                break;

            var step = remaining;
            foreach (var project in active)
            {
                var node = Catalog.GetNode(project.NodeId);
                var remainingRp = Math.Max(
                    0.0,
                    ProgressPolicy.GetStageWork(node, project.Stage) - project.StageResearchPoints);
                var scaledLabs = Catalog.LabScaling.ScaleAssignedLabs(
                    project.AssignedEffectiveLabs,
                    node.ProjectRequirements.RecommendedLabs);
                var rpPerYear = scaledLabs * Catalog.Metadata.BaseRpPerEffectiveLabPerYear * project.ReadinessEfficiency;
                if (rpPerYear <= 0.0)
                    continue;
                var yearsToBoundary = remainingRp / rpPerYear;
                if (yearsToBoundary < step)
                    step = Math.Max(yearsToBoundary, EpsilonYears);
            }

            var preStages = state.ActiveProjects.ToDictionary(
                pair => pair.Key,
                pair => pair.Value.Stage,
                StringComparer.Ordinal);
            var segmentEvents = Kernel.AdvanceProjects(state, step);
            events.AddRange(segmentEvents);
            cursorYear += step;
            remaining -= step;

            var practiceChanged = false;
            foreach (var pair in preStages)
            {
                var completed = false;
                if (!state.ActiveProjects.TryGetValue(pair.Key, out var currentProject))
                {
                    completed = state.TryGetNodeState(pair.Key, out var nodeState) &&
                        nodeState.Maturity == ResearchMaturity.Mature &&
                        pair.Value == ResearchMaturity.Engineering;
                }
                else if (currentProject.Stage != pair.Value)
                {
                    completed = true;
                }
                else if (currentProject.Paused &&
                         pair.Value == ResearchMaturity.Experimental &&
                         string.Equals(currentProject.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal))
                {
                    completed = true;
                }

                if (!completed)
                    continue;
                Expertise.ApplyCompletedStagePractice(state, pair.Key, pair.Value, cursorYear);
                practiceChanged = true;
            }

            if (practiceChanged || segmentEvents.Count > 0)
                RecalculateAllActiveReadiness(state);

            if (step <= EpsilonYears && segmentEvents.Count == 0)
                break;
        }

        if (guard >= 4096)
            throw new InvalidOperationException("Adaptive Research authority exceeded stage-boundary advancement guard.");
        return events;
    }

    public void SetResearchInstitution(
        AdaptiveResearchCivilizationState state,
        string institutionInstanceId,
        string institutionArchetypeId,
        int totalCount,
        int activeCount,
        string? contextId = null)
    {
        Expertise.SetInstitution(
            state,
            institutionInstanceId,
            institutionArchetypeId,
            totalCount,
            activeCount,
            contextId);
        PauseForCapacityReallocationIfNeeded(state);
        RecalculateAllActiveReadiness(state);
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
        Expertise.SetTacitAsset(
            state,
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
            contextId);
        RecalculateAllActiveReadiness(state);
    }

    public void ApplyCompetenceAtrophy(
        AdaptiveResearchCivilizationState state,
        double currentYear,
        double elapsedYears)
    {
        Expertise.ApplyCompetenceAtrophy(state, currentYear, elapsedYears);
        RecalculateAllActiveReadiness(state);
    }

    private void RecalculateAllActiveReadiness(AdaptiveResearchCivilizationState state)
    {
        foreach (var nodeId in state.ActiveProjects.Keys.ToArray())
            RecalculateProjectReadiness(state, nodeId);
    }

    private void RecalculateProjectReadiness(AdaptiveResearchCivilizationState state, string nodeId)
    {
        if (!state.ActiveProjects.TryGetValue(nodeId, out var project))
            return;
        var readiness = GetProjectReadiness(
            state,
            nodeId,
            project.Stage,
            project.AssignedEffectiveLabs,
            project.TargetApplicabilityContextId);
        if (Math.Abs(project.ReadinessEfficiency - readiness.RpEfficiency) < 0.000001)
            return;
        state.SetProject(project with { ReadinessEfficiency = readiness.RpEfficiency });
    }

    private static void PauseForCapacityReallocationIfNeeded(AdaptiveResearchCivilizationState state)
    {
        if (state.AssignedEffectiveLabs <= state.TotalEffectiveResearchLabs + 0.000001)
            return;

        // Without a player/AI priority decision, Research must not silently choose which project loses
        // capacity. Pause all active projects and release allocations for explicit reallocation.
        foreach (var project in state.ActiveProjects.Values.Where(project => !project.Paused).ToArray())
            state.SetProject(project with { Paused = true, PauseReason = "research_capacity_reallocation_required" });
    }
}
