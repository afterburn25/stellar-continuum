using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ExplorationMissionPlanningValidation
{
    public static void ValidateBoundedObserverSafeMissionPlan()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var fleet = AddSurveyFleet(galaxy, player.Id, FleetRole.Science, home.Id, home.Position, "Planning Science Vessel");

        var partial = galaxy.Systems.First(system => system.Id != home.Id);
        var detected = galaxy.Systems.First(system => system.Id != home.Id && system.Id != partial.Id);
        galaxy.Knowledge.RecordReconnaissance(player.Id, partial.Id, 0.40);
        galaxy.Knowledge.RevealSystem(player.Id, detected.Id);

        var planner = new ExplorationMissionPlanner(new ValidationReachView());
        var first = planner.BuildPlan(galaxy, fleet.Id, maximumCandidates: 5);
        var second = planner.BuildPlan(galaxy, fleet.Id, maximumCandidates: 5);

        Require(first.CanReceiveOrders, "science vessel was incorrectly unavailable for mission planning");
        Require(first.Candidates.Count <= 5, "mission planner exceeded the requested candidate bound");
        Require(first.Candidates.Select(candidate => candidate.SystemId).SequenceEqual(second.Candidates.Select(candidate => candidate.SystemId)),
            "identical mission-planning inputs produced a different candidate order");

        var fullPlan = planner.BuildPlan(galaxy, fleet.Id, maximumCandidates: ExplorationMissionPlanner.HardMaximumCandidates);
        var partialCandidate = fullPlan.Candidates.FirstOrDefault(candidate => candidate.SystemId == partial.Id)
            ?? throw new InvalidOperationException("reconnoitered target was missing from the science mission plan");
        var detectedCandidate = fullPlan.Candidates.FirstOrDefault(candidate => candidate.SystemId == detected.Id)
            ?? throw new InvalidOperationException("detected target was missing from the science mission plan");

        Require(partialCandidate.PriorityBand == 0, "science planner did not prioritize partially surveyed work");
        Require(partialCandidate.EstimatedRemainingScienceSurveyDays is > 0.0,
            "reconnaissance-grade target did not expose legitimate remaining science-survey effort");
        Require(partialCandidate.SurveyOperationalHazard is not null,
            "reconnaissance-grade target did not expose its legitimate survey hazard classification");
        Require(detectedCandidate.EstimatedRemainingScienceSurveyDays is null,
            "detected-only target leaked hidden body complexity through a survey-time estimate");
        Require(detectedCandidate.SurveyOperationalHazard is null,
            "detected-only target leaked hidden body hazard information");

        var unknownCandidate = fullPlan.Candidates.FirstOrDefault(candidate => candidate.SurveyLevel == SystemSurveyLevel.Unknown);
        if (unknownCandidate is not null)
        {
            Require(unknownCandidate.EstimatedRemainingScienceSurveyDays is null,
                "catalog-only target leaked hidden survey effort");
            Require(unknownCandidate.SurveyOperationalHazard is null,
                "catalog-only target leaked hidden survey hazard");
        }

        var military = new FleetState
        {
            Id = galaxy.Fleets.Max(existing => existing.Id) + 1,
            CivilizationId = player.Id,
            Name = "Planning Non-Survey Fleet",
            Role = FleetRole.Military,
            Position = home.Position,
            CurrentSystemId = home.Id,
            IsActive = true,
        };
        galaxy.Fleets.Add(military);
        var unavailable = planner.BuildPlan(galaxy, military.Id);
        Require(!unavailable.CanReceiveOrders && unavailable.Candidates.Count == 0,
            "non-survey fleet received exploration mission candidates");
    }

    public static void ValidateSharedReachRejectionAndLocalOrders()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var target = galaxy.Systems.First(system => system.Id != home.Id);
        galaxy.Knowledge.RevealSystem(player.Id, target.Id);

        var science = AddSurveyFleet(galaxy, player.Id, FleetRole.Science, home.Id, home.Position, "Reach Validation Science Vessel");
        var reach = new ValidationReachView(target.Id, "MISSION PLAN TEST BLOCK");
        var planner = new ExplorationMissionPlanner(reach);
        var plan = planner.BuildPlan(galaxy, science.Id, maximumCandidates: ExplorationMissionPlanner.HardMaximumCandidates);
        var blocked = plan.Candidates.FirstOrDefault(candidate => candidate.SystemId == target.Id)
            ?? throw new InvalidOperationException("blocked target was omitted from the diagnostic mission plan");

        Require(!blocked.Reach.IsSupported, "blocked target was marked operationally supported");
        Require(blocked.Reach.Reason.Contains("MISSION PLAN TEST BLOCK", StringComparison.Ordinal),
            "planner lost the Logistics-owned reach rejection reason");

        var simulation = new ExplorationSimulation(reach);
        var rejected = simulation.IssueSurveyOrder(galaxy, science.Id, target.Id);
        Require(!rejected.Accepted, "player survey order bypassed the planner reach rejection");
        Require(rejected.Message.Contains("MISSION PLAN TEST BLOCK", StringComparison.Ordinal),
            "player order rejection did not preserve the same reach reason as the mission plan");
        Require(science.DestinationSystemId is null, "rejected survey order still mutated fleet destination");

        var localScout = AddSurveyFleet(galaxy, player.Id, FleetRole.Scout, target.Id, target.Position, "Local Planning Scout");
        var localSimulation = new ExplorationSimulation(new ValidationReachView());
        var localOrder = localSimulation.IssueSurveyOrder(galaxy, localScout.Id, target.Id);
        Require(localOrder.Accepted && localOrder.IsLocalSurvey,
            "local reconnaissance was not accepted as an in-place survey order");
        Require(localScout.DestinationSystemId is null,
            "local survey order incorrectly created a zero-distance travel destination");

        localSimulation.Advance(galaxy, ExplorationSimulation.ScoutReconnaissanceDays);
        Require(galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) >= SystemSurveyLevel.PartiallySurveyed,
            "accepted local scout order did not produce reconnaissance on advance");
    }

    public static void ValidateAiUsesSharedMissionPlan()
    {
        var galaxy = CreateValidationGalaxy();
        var ai = galaxy.Civilizations.First(civilization => !civilization.IsPlayer);
        var home = galaxy.Systems.First(system => system.Id == ai.HomeSystemId);
        var fleet = AddSurveyFleet(galaxy, ai.Id, FleetRole.Science, home.Id, home.Position, "AI Planning Science Vessel");
        var reach = new ValidationReachView();
        var planner = new ExplorationMissionPlanner(reach);
        var plan = planner.BuildPlan(galaxy, fleet.Id);
        var expected = plan.Candidates.FirstOrDefault(candidate => candidate.Reach.IsSupported)
            ?? throw new InvalidOperationException("AI validation fleet had no supported mission-planning candidate");

        var simulation = new ExplorationSimulation(reach);
        simulation.Advance(galaxy, 0.000001);

        Require(fleet.DestinationSystemId == expected.SystemId,
            $"AI selected system {fleet.DestinationSystemId?.ToString() ?? "none"} instead of shared planner target {expected.SystemId}");

        // Keep the central registry stable: deeper AI coordination checks remain attached to
        // this existing mission-planning gate while validating live reservation behavior.
        ExplorationAiDeconflictionValidation.ValidateDistinctTargetsWhenAlternativesExist();
        ExplorationAiDeconflictionValidation.ValidateSharedFallbackWhenOnlyOneTargetRemains();
        ExplorationAiDeconflictionValidation.ValidateLocalSurveyWorkActsAsReservation();
    }

    private static FleetState AddSurveyFleet(
        GalaxyState galaxy,
        int civilizationId,
        FleetRole role,
        int systemId,
        Vector2 position,
        string name)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 10000 : galaxy.Fleets.Max(existing => existing.Id) + 10000,
            CivilizationId = civilizationId,
            Name = name,
            Role = role,
            Position = position,
            CurrentSystemId = systemId,
            StrategicSpeed = role == FleetRole.Scout ? 22.0 : 18.0,
            SensorRange = role == FleetRole.Scout ? 135.0f : 185.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4D49_5353_494F_4E50L,
            new GalaxyGenerationSettings
            {
                SystemCount = 48,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 520.0f,
            });

    private sealed class ValidationReachView : IInterstellarOperationalReachView
    {
        private readonly int? _blockedSystemId;
        private readonly string _blockedReason;

        public ValidationReachView(int? blockedSystemId = null, string blockedReason = "TEST BLOCK")
        {
            _blockedSystemId = blockedSystemId;
            _blockedReason = blockedReason;
        }

        public MissionReachAssessment Assess(
            GalaxyState galaxy,
            int civilizationId,
            FleetState fleet,
            int targetSystemId,
            InterstellarMissionKind missionKind)
        {
            return targetSystemId == _blockedSystemId
                ? MissionReachAssessment.Unsupported(_blockedReason)
                : MissionReachAssessment.Supported("MISSION PLAN TEST SUPPORT");
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
