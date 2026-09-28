using System.Runtime.CompilerServices;
using Game.Simulation;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class ColonyOpportunityActionValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateObserverScopedFleetAuthorization();
        ValidateStaleDisplayedSiteIsRevalidated();
        ValidateValidExactBodyCommand();
        Console.WriteLine("PASS: observer-scoped actionable colony opportunity commands");
    }

    private static void ValidateObserverScopedFleetAuthorization()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var foreign = galaxy.Civilizations.First(civilization => civilization.Id != player.Id && !civilization.IsSeededAncient);
        PrepareFullySurveyedObserver(galaxy, player.Id);
        DisableColonyFleets(galaxy, player.Id);
        DisableColonyFleets(galaxy, foreign.Id);

        var playerFleet = AddColonyFleet(galaxy, player, "Authorized Colony Fleet");
        var foreignFleet = AddColonyFleet(galaxy, foreign, "Foreign Colony Fleet");
        var coordinator = CreateCoordinator();
        var target = coordinator
            .GetColonyOpportunityPlan(galaxy, playerFleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates)
            .Candidates
            .FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("authorization validation had no orderable player colony opportunity");

        var foreignResult = coordinator.IssueColonyFleetOrder(
            galaxy,
            player.Id,
            foreignFleet.Id,
            target.SystemId,
            target.PlanetaryBodyId);
        var missingResult = coordinator.IssueColonyFleetOrder(
            galaxy,
            player.Id,
            int.MaxValue,
            target.SystemId,
            target.PlanetaryBodyId);

        Require(!foreignResult.Accepted && !missingResult.Accepted,
            "foreign or nonexistent colony fleet command was unexpectedly accepted");
        Require(foreignResult.Message == missingResult.Message,
            "foreign and nonexistent colony fleet IDs produced distinguishable command rejections");
        Require(foreignFleet.DestinationSystemId is null && foreignFleet.DestinationPlanetaryBodyId is null,
            "rejected foreign colony command mutated foreign fleet state");
    }

    private static void ValidateStaleDisplayedSiteIsRevalidated()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        PrepareFullySurveyedObserver(galaxy, player.Id);
        DisableColonyFleets(galaxy, player.Id);
        var fleet = AddColonyFleet(galaxy, player, "Stale Site Colony Fleet");
        var coordinator = CreateCoordinator();
        var target = coordinator
            .GetColonyOpportunityPlan(galaxy, fleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates)
            .Candidates
            .FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("stale-site validation had no orderable colony opportunity");

        // Simulate state changing after UI display but before the player presses Settle Here.
        galaxy.Colonies.Add(new ColonyState
        {
            Id = galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(colony => colony.Id) + 1,
            CivilizationId = player.Id,
            SystemId = target.SystemId,
            PlanetaryBodyId = target.PlanetaryBodyId,
            Name = "Intervening Settlement",
            PopulationSpeciesId = player.SpeciesId,
            PopulationMillions = 50.0,
            Infrastructure = 0.3,
            Stability = 0.9,
        });

        var result = coordinator.IssueColonyFleetOrder(
            galaxy,
            player.Id,
            fleet.Id,
            target.SystemId,
            target.PlanetaryBodyId);

        Require(!result.Accepted,
            "stale displayed colony opportunity bypassed click-time authoritative revalidation");
        Require(fleet.DestinationSystemId is null && fleet.DestinationPlanetaryBodyId is null,
            "rejected stale colony command mutated destination state");
    }

    private static void ValidateValidExactBodyCommand()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        PrepareFullySurveyedObserver(galaxy, player.Id);
        DisableColonyFleets(galaxy, player.Id);
        var fleet = AddColonyFleet(galaxy, player, "Exact Target Colony Fleet");
        var coordinator = CreateCoordinator();
        var target = coordinator
            .GetColonyOpportunityPlan(galaxy, fleet.Id, ColonizationOpportunityPlanner.HardMaximumCandidates)
            .Candidates
            .FirstOrDefault(candidate => candidate.CanOrder)
            ?? throw new InvalidOperationException("exact-target validation had no orderable colony opportunity");

        var result = coordinator.IssueColonyFleetOrder(
            galaxy,
            player.Id,
            fleet.Id,
            target.SystemId,
            target.PlanetaryBodyId);

        Require(result.Accepted, "valid exact-body colony command was rejected");
        Require(fleet.DestinationSystemId == target.SystemId,
            "valid colony command did not set the selected destination system");
        Require(fleet.DestinationPlanetaryBodyId == target.PlanetaryBodyId,
            "valid colony command did not set the selected exact planetary body");
    }

    private static GalaxySimulationStepCoordinator CreateCoordinator() =>
        new(colonization: new ColonizationSimulation(new ValidationReachView()));

    private static void PrepareFullySurveyedObserver(GalaxyState galaxy, int civilizationId)
    {
        foreach (var system in galaxy.Systems)
            galaxy.Knowledge.MarkSystemFullySurveyed(civilizationId, system.Id);
    }

    private static void DisableColonyFleets(GalaxyState galaxy, int civilizationId)
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
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 50000 : galaxy.Fleets.Max(existing => existing.Id) + 50000,
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
            0x434F_4C4F_4E59_4143L,
            new GalaxyGenerationSettings
            {
                SystemCount = 56,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 560.0f,
            });

    private sealed class ValidationReachView : IInterstellarOperationalReachView
    {
        public MissionReachAssessment Assess(
            GalaxyState galaxy,
            int civilizationId,
            FleetState fleet,
            int targetSystemId,
            InterstellarMissionKind missionKind) =>
            MissionReachAssessment.Supported("ACTIONABLE COLONY TEST SUPPORT");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
