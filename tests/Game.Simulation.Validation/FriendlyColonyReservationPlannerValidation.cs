using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class FriendlyColonyReservationPlannerValidation
{
    public static void ValidateFriendlyReservationBlocksSecondFleetPlanAndOrder()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableAllColonyFleets(galaxy);

        var targetBody = FindViableUnoccupiedBody(galaxy, civilization.SpeciesId);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, targetBody.SystemId);

        var first = AddColonyFleet(galaxy, civilization, "Reservation Owner Colony Ship");
        var second = AddColonyFleet(galaxy, civilization, "Reservation Blocked Colony Ship");
        var simulation = new ColonizationSimulation();

        var firstOrder = simulation.IssueColonyFleetOrder(
            galaxy,
            first.Id,
            targetBody.SystemId,
            targetBody.Id);
        Require(firstOrder.Accepted,
            "first friendly colony fleet could not reserve an otherwise orderable target");
        Require(first.DestinationSystemId == targetBody.SystemId,
            "accepted first colony order did not set destination system");

        var secondPlan = simulation.GetOpportunityPlan(
            galaxy,
            second.Id,
            ColonizationOpportunityPlanner.HardMaximumCandidates);
        var blocked = secondPlan.Candidates.FirstOrDefault(candidate =>
            candidate.PlanetaryBodyId == targetBody.Id)
            ?? throw new InvalidOperationException("friendly-reserved target disappeared from diagnostic colony opportunity plan");

        Require(!blocked.CanOrder,
            "friendly-reserved colony target remained orderable for a second colony fleet");
        Require(blocked.SystemReservedByFriendlyColonyMission,
            "friendly-reserved candidate did not expose reservation metadata");
        Require(blocked.ReservedByFleetId == first.Id,
            "friendly-reserved candidate did not identify the reserving fleet");
        Require(blocked.Reason.Contains("friendly colony ship", StringComparison.OrdinalIgnoreCase),
            "friendly-reserved candidate did not expose an actionable reservation reason");

        var assessment = simulation.AssessColonyOrder(
            galaxy,
            second.Id,
            targetBody.SystemId,
            targetBody.Id);
        Require(!assessment.Accepted && assessment.Candidate?.ReservedByFleetId == first.Id,
            "manual colony order assessment bypassed shared friendly reservation eligibility");

        var rejected = simulation.IssueColonyFleetOrder(
            galaxy,
            second.Id,
            targetBody.SystemId,
            targetBody.Id);
        Require(!rejected.Accepted,
            "manual explicit fleet order bypassed friendly mission reservation");
        Require(second.DestinationSystemId is null && second.DestinationPlanetaryBodyId is null,
            "rejected friendly-reservation order mutated the second fleet destination");
    }

    public static void ValidateReservationExcludesRequestingFleetAndForeignIntent()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        var foreign = galaxy.Civilizations.First(candidate => candidate.Id != civilization.Id);
        DisableAllColonyFleets(galaxy);

        var targetBody = FindViableUnoccupiedBody(galaxy, civilization.SpeciesId);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, targetBody.SystemId);

        var requester = AddColonyFleet(galaxy, civilization, "Self Reservation Validation Colony Ship");
        requester.DestinationSystemId = targetBody.SystemId;
        requester.DestinationPlanetaryBodyId = targetBody.Id;

        var simulation = new ColonizationSimulation();
        var ownAssessment = simulation.AssessColonyOrder(
            galaxy,
            requester.Id,
            targetBody.SystemId,
            targetBody.Id);
        Require(ownAssessment.Accepted,
            "requesting colony fleet incorrectly blocked itself as a friendly reservation");
        Require(ownAssessment.Candidate is not null &&
                !ownAssessment.Candidate.SystemReservedByFriendlyColonyMission &&
                ownAssessment.Candidate.ReservedByFleetId is null,
            "requesting fleet's own destination leaked into reservation metadata");

        requester.DestinationSystemId = null;
        requester.DestinationPlanetaryBodyId = null;
        var foreignMission = AddColonyFleet(galaxy, foreign, "Hidden Foreign Reservation Validation Mission");
        foreignMission.DestinationSystemId = targetBody.SystemId;
        foreignMission.DestinationPlanetaryBodyId = targetBody.Id;

        var foreignSafe = simulation.AssessColonyOrder(
            galaxy,
            requester.Id,
            targetBody.SystemId,
            targetBody.Id);
        Require(foreignSafe.Accepted,
            "foreign colony mission intent incorrectly blocked an observer's colony order");
        Require(foreignSafe.Candidate is not null &&
                !foreignSafe.Candidate.SystemReservedByFriendlyColonyMission &&
                foreignSafe.Candidate.ReservedByFleetId is null,
            "foreign mission leaked into friendly reservation metadata");
    }

    public static void ValidateLocalFriendlyColonyFleetReservesCurrentSystem()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableAllColonyFleets(galaxy);

        var targetBody = FindViableUnoccupiedBody(galaxy, civilization.SpeciesId);
        var targetSystem = galaxy.Systems.First(system => system.Id == targetBody.SystemId);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, targetSystem.Id);

        var localReservation = AddColonyFleet(galaxy, civilization, "Local Reservation Colony Ship");
        localReservation.Position = targetSystem.Position;
        localReservation.CurrentSystemId = targetSystem.Id;
        localReservation.DestinationSystemId = null;
        localReservation.DestinationPlanetaryBodyId = null;

        var requester = AddColonyFleet(galaxy, civilization, "Local Reservation Blocked Colony Ship");
        var plan = new ColonizationSimulation().GetOpportunityPlan(
            galaxy,
            requester.Id,
            ColonizationOpportunityPlanner.HardMaximumCandidates);
        var candidate = plan.Candidates.FirstOrDefault(item => item.PlanetaryBodyId == targetBody.Id)
            ?? throw new InvalidOperationException("local friendly reservation target disappeared from opportunity plan");

        Require(!candidate.CanOrder && candidate.SystemReservedByFriendlyColonyMission,
            "friendly colony fleet physically present at an uncolonized target did not reserve the system");
        Require(candidate.ReservedByFleetId == localReservation.Id,
            "local friendly reservation did not identify the present colony fleet");
    }

    private static PlanetaryBodyState FindViableUnoccupiedBody(GalaxyState galaxy, string speciesId)
    {
        var occupied = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        return galaxy.PlanetaryBodies
            .Where(body => !occupied.Contains(body.SystemId))
            .OrderBy(body => body.SystemId)
            .ThenBy(body => body.Id)
            .First(body => evaluator.Evaluate(body, speciesId).CanFoundCurrentColony);
    }

    private static void DisableAllColonyFleets(GalaxyState galaxy)
    {
        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.Role == FleetRole.Colony))
            fleet.IsActive = false;
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 140000 : galaxy.Fleets.Max(existing => existing.Id) + 140000,
            CivilizationId = civilization.Id,
            Name = name,
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 12.0,
            SensorRange = 95.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 120.0,
            EmbarkedPopulationSpeciesId = civilization.SpeciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x5245_5345_5256_434CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
