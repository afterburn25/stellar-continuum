using System.Runtime.CompilerServices;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class ColonizationOpportunityPlannerValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateObserverSafeBoundedCandidates();
        ValidateReachRejectionParity();
        ValidateExplicitFleetAndPassengerSpecies();
        Console.WriteLine("PASS: observer-safe colony opportunity planning and exact-fleet orders");
    }

    private static void ValidateObserverSafeBoundedCandidates()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableOwnedColonyFleets(galaxy, civilization.Id);
        var fleet = AddColonyFleet(galaxy, civilization, civilization.SpeciesId, "Opportunity Boundary Colony Ship");

        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var partial = galaxy.Systems.First(system => system.Id != home.Id);
        var detailed = galaxy.Systems.First(system => system.Id != home.Id && system.Id != partial.Id);
        galaxy.Knowledge.RecordReconnaissance(civilization.Id, partial.Id, 0.45);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, detailed.Id);

        var planner = new ColonizationOpportunityPlanner(new ValidationReachView());
        var bounded = planner.BuildPlan(galaxy, fleet.Id, maximumCandidates: 3);
        Require(bounded.CanReceiveOrders, "valid populated colony fleet could not receive opportunity planning");
        Require(bounded.Candidates.Count <= 3, "colony opportunity planner exceeded requested candidate bound");
        Require(bounded.PassengerSpeciesId == fleet.EmbarkedPopulationSpeciesId,
            "colony opportunity plan did not use the fleet passenger species");

        var full = planner.BuildPlan(galaxy, fleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates);
        Require(full.Candidates.Count > 0, "colony opportunity planner returned no fully surveyed bodies");
        Require(full.Candidates.All(candidate =>
                galaxy.Knowledge.GetSystemSurveyLevel(civilization.Id, candidate.SystemId) == SystemSurveyLevel.FullySurveyed),
            "colony opportunity planner leaked a body from a non-fully-surveyed system");
        Require(full.Candidates.All(candidate => candidate.SystemId != partial.Id),
            "reconnaissance-only system leaked into colony opportunity planning");
        Require(full.Candidates.Any(candidate => candidate.SystemId == detailed.Id),
            "explicitly fully surveyed validation system did not appear in colony opportunity planning");

        var speciesViews = new SpeciesPlanetaryReadModel()
            .BuildForSpecies(galaxy, civilization.Id, fleet.EmbarkedPopulationSpeciesId!);
        var comparison = full.Candidates
            .Select(candidate => new
            {
                Candidate = candidate,
                Species = speciesViews.FirstOrDefault(view => view.PlanetaryBodyId == candidate.PlanetaryBodyId),
            })
            .First(pair => pair.Species is not null);
        Require(Math.Abs(comparison.Candidate.NaturalHabitability - comparison.Species!.NaturalHabitability) < 0.0000001,
            "colony planner changed Species-owned natural habitability");
        Require(Math.Abs(comparison.Candidate.UnprotectedOperationalCapacity - comparison.Species.UnprotectedOperationalCapacity) < 0.0000001,
            "colony planner changed Species-owned operational capacity");
        Require(comparison.Candidate.ColonizationViability == comparison.Species.ColonizationViability,
            "colony planner changed Species-owned colonization viability");
    }

    private static void ValidateReachRejectionParity()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableOwnedColonyFleets(galaxy, civilization.Id);
        MarkAllSystemsFullySurveyed(galaxy, civilization.Id);
        var fleet = AddColonyFleet(galaxy, civilization, civilization.SpeciesId, "Opportunity Reach Colony Ship");

        var supportedPlanner = new ColonizationOpportunityPlanner(new ValidationReachView());
        var supported = supportedPlanner.BuildPlan(galaxy, fleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates);
        var target = supported.Candidates.FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("validation galaxy did not provide an orderable colony opportunity");

        const string blockedReason = "COLONY OPPORTUNITY TEST BLOCK";
        var reach = new ValidationReachView(target.SystemId, blockedReason);
        var blockedPlanner = new ColonizationOpportunityPlanner(reach);
        var blockedPlan = blockedPlanner.BuildPlan(galaxy, fleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates);
        var blocked = blockedPlan.Candidates.FirstOrDefault(candidate => candidate.PlanetaryBodyId == target.PlanetaryBodyId)
            ?? throw new InvalidOperationException("reach-blocked colony body disappeared from diagnostic opportunity planning");

        Require(!blocked.CanOrder, "reach-blocked colony opportunity remained orderable");
        Require(!blocked.Reach.IsSupported && blocked.Reach.Reason == blockedReason,
            "colony opportunity planner did not preserve Logistics-owned reach rejection");
        Require(blocked.Reason.Contains(blockedReason, StringComparison.Ordinal),
            "colony opportunity candidate lost the Logistics-owned reach reason");

        var simulation = new ColonizationSimulation(reach);
        var assessment = simulation.AssessColonyOrder(galaxy, fleet.Id, target.SystemId, target.PlanetaryBodyId);
        Require(!assessment.Accepted && assessment.Message.Contains(blockedReason, StringComparison.Ordinal),
            "colony order assessment did not match planner reach rejection");

        var order = simulation.IssueColonyFleetOrder(galaxy, fleet.Id, target.SystemId, target.PlanetaryBodyId);
        Require(!order.Accepted && order.Message.Contains(blockedReason, StringComparison.Ordinal),
            "explicit colony fleet order did not preserve planner reach rejection");
        Require(fleet.DestinationSystemId is null && fleet.DestinationPlanetaryBodyId is null,
            "rejected colony fleet order mutated destination state");
    }

    private static void ValidateExplicitFleetAndPassengerSpecies()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableOwnedColonyFleets(galaxy, civilization.Id);
        MarkAllSystemsFullySurveyed(galaxy, civilization.Id);

        var first = AddColonyFleet(
            galaxy,
            civilization,
            SpeciesCatalog.TerranBaselineId,
            "Terran Opportunity Colony Ship");
        var secondSpecies = civilization.SpeciesId == SpeciesCatalog.CompactHighGravityId
            ? SpeciesCatalog.CryogenicHydrocarbonId
            : SpeciesCatalog.CompactHighGravityId;
        var second = AddColonyFleet(
            galaxy,
            civilization,
            secondSpecies,
            "Alternate Species Opportunity Colony Ship");

        var simulation = new ColonizationSimulation(new ValidationReachView());
        var firstPlan = simulation.GetOpportunityPlan(galaxy, first.Id, ColonizationOpportunityPlanner.HardMaximumCandidates);
        var secondPlan = simulation.GetOpportunityPlan(galaxy, second.Id, ColonizationOpportunityPlanner.HardMaximumCandidates);

        Require(firstPlan.PassengerSpeciesId == SpeciesCatalog.TerranBaselineId,
            "first colony plan did not use first fleet passenger species");
        Require(secondPlan.PassengerSpeciesId == secondSpecies,
            "second colony plan did not use second fleet passenger species");
        Require(firstPlan.Candidates.All(candidate => candidate.FleetId == first.Id && candidate.PassengerSpeciesId == SpeciesCatalog.TerranBaselineId),
            "first colony plan mixed another fleet/species into its candidates");
        Require(secondPlan.Candidates.All(candidate => candidate.FleetId == second.Id && candidate.PassengerSpeciesId == secondSpecies),
            "second colony plan mixed another fleet/species into its candidates");

        var target = secondPlan.Candidates.FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("alternate-species validation fleet had no orderable colony opportunity");
        var order = simulation.IssueColonyFleetOrder(galaxy, second.Id, target.SystemId, target.PlanetaryBodyId);
        Require(order.Accepted, "explicit second-fleet colony order was rejected despite an orderable planner candidate");
        Require(second.DestinationSystemId == target.SystemId && second.DestinationPlanetaryBodyId == target.PlanetaryBodyId,
            "explicit colony fleet order did not set the selected fleet's exact body target");
        Require(first.DestinationSystemId is null && first.DestinationPlanetaryBodyId is null,
            "explicit second-fleet colony order mutated the first available colony fleet");
    }

    private static void DisableOwnedColonyFleets(GalaxyState galaxy, int civilizationId)
    {
        foreach (var fleet in galaxy.Fleets.Where(fleet =>
                     fleet.CivilizationId == civilizationId && fleet.Role == FleetRole.Colony))
        {
            fleet.IsActive = false;
        }
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        string speciesId,
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 40000 : galaxy.Fleets.Max(existing => existing.Id) + 40000,
            CivilizationId = civilization.Id,
            Name = name,
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 12.0,
            SensorRange = 95.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 120.0,
            EmbarkedPopulationSpeciesId = speciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static void MarkAllSystemsFullySurveyed(GalaxyState galaxy, int civilizationId)
    {
        foreach (var system in galaxy.Systems)
            galaxy.Knowledge.MarkSystemFullySurveyed(civilizationId, system.Id);
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x434F_4C4F_4E59_504CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 56,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 560.0f,
            });

    private sealed class ValidationReachView : IInterstellarOperationalReachView
    {
        private readonly int? _blockedSystemId;
        private readonly string _blockedReason;

        public ValidationReachView(int? blockedSystemId = null, string blockedReason = "COLONY OPPORTUNITY TEST SUPPORT")
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
                : MissionReachAssessment.Supported("COLONY OPPORTUNITY TEST SUPPORT");
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
