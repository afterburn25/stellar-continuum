using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class ExplorationMissionStatusValidation
{
    public static void ValidateTransitEtaAndSurveyInformationBoundary()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var target = galaxy.Systems
            .Where(system => system.Id != home.Id)
            .OrderByDescending(system => Vector2.DistanceSquared(home.Position, system.Position))
            .First();
        var fleet = AddFleet(galaxy, player.Id, FleetRole.Science, home.Id, home.Position, "ETA Science Vessel");
        fleet.DestinationSystemId = target.Id;

        var evaluator = new ExplorationMissionStatusEvaluator();
        var before = evaluator.Build(galaxy, fleet);
        Require(before.Phase == ExplorationMissionPhase.Traveling, "in-transit science vessel did not report Traveling phase");
        Require(before.EstimatedTransitDaysRemaining is > 0.0, "traveling vessel had no positive transit ETA");
        Require(before.EstimatedSurveyDaysRemaining is null,
            "science vessel leaked target survey complexity before reconnaissance");
        Require(before.EstimatedMissionDaysRemaining is null,
            "science vessel exposed a total mission ETA before survey complexity was legitimately known");

        fleet.Position = Vector2.Lerp(home.Position, target.Position, 0.5f);
        var halfway = evaluator.Build(galaxy, fleet);
        Require(halfway.EstimatedTransitDaysRemaining is > 0.0,
            "halfway transit status lost its transit ETA");
        Require(halfway.EstimatedTransitDaysRemaining < before.EstimatedTransitDaysRemaining,
            "transit ETA did not decrease when the fleet moved closer to its destination");

        galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id, 0.40);
        var afterRecon = evaluator.Build(galaxy, fleet);
        Require(afterRecon.EstimatedSurveyDaysRemaining is > 0.0,
            "reconnaissance did not unlock legitimate remaining science-survey effort");
        Require(afterRecon.EstimatedMissionDaysRemaining is > 0.0,
            "known transit plus known survey effort did not produce a total mission ETA");
        Require(Math.Abs(
            afterRecon.EstimatedMissionDaysRemaining!.Value -
            (afterRecon.EstimatedTransitDaysRemaining!.Value + afterRecon.EstimatedSurveyDaysRemaining!.Value)) < 0.000001,
            "total mission ETA was not the sum of known transit and known survey work");

        var readModel = new ExplorationReadModel().Build(galaxy, player.Id);
        var missionView = readModel.ActiveMissions.First(mission => mission.FleetId == fleet.Id);
        Require(missionView.Status == afterRecon,
            "exploration read model did not expose the same derived mission status as the authoritative evaluator");
    }

    public static void ValidateLocalScoutAndSciencePhases()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var target = galaxy.Systems.First(system => system.Id != player.HomeSystemId);
        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        var evaluator = new ExplorationMissionStatusEvaluator();

        var scout = AddFleet(galaxy, player.Id, FleetRole.Scout, target.Id, target.Position, "Status Scout");
        var scoutReady = evaluator.Build(galaxy, scout);
        Require(scoutReady.Phase == ExplorationMissionPhase.ReconnaissanceReady,
            "local scout at an unsurveyed target did not report ReconnaissanceReady");
        Require(scoutReady.EstimatedTransitDaysRemaining == 0.0 && scoutReady.EstimatedSurveyDaysRemaining == ExplorationSimulation.ScoutReconnaissanceDays,
            "local scouting did not report its real remaining on-site duration");

        galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id, 0.40);
        var scoutDone = evaluator.Build(galaxy, scout);
        Require(scoutDone.Phase == ExplorationMissionPhase.AwaitingOrder,
            "scout remained survey-ready after reconnaissance-grade coverage was complete");

        var science = AddFleet(galaxy, player.Id, FleetRole.Science, target.Id, target.Position, "Status Science Vessel");
        var scienceWorking = evaluator.Build(galaxy, science);
        Require(scienceWorking.Phase == ExplorationMissionPhase.ScienceSurveying,
            "science vessel at partially surveyed system did not report ScienceSurveying");
        Require(scienceWorking.EstimatedSurveyDaysRemaining is > 0.0,
            "partial science survey did not expose legitimate remaining survey days");
        Require(scienceWorking.EstimatedTransitDaysRemaining == 0.0,
            "local science survey incorrectly reported transit time");
        Require(scienceWorking.EstimatedMissionDaysRemaining == scienceWorking.EstimatedSurveyDaysRemaining,
            "local science mission total ETA did not equal remaining survey effort");

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        var scienceDone = evaluator.Build(galaxy, science);
        Require(scienceDone.Phase == ExplorationMissionPhase.AwaitingOrder,
            "science vessel did not return to AwaitingOrder after full survey completion");
        Require(scienceDone.EstimatedSurveyDaysRemaining is null,
            "completed science survey retained a stale survey ETA");
    }

    public static void ValidateColonySettlementReadiness()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var occupiedSystems = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var targetBody = galaxy.PlanetaryBodies
            .Where(body => !occupiedSystems.Contains(body.SystemId))
            .Select(body => new
            {
                Body = body,
                Assessment = habitability.Evaluate(body, player.SpeciesId),
            })
            .Where(candidate => candidate.Assessment.CanFoundCurrentColony)
            .OrderByDescending(candidate => candidate.Assessment.Viability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.NaturalHabitability)
            .ThenBy(candidate => candidate.Body.SystemId)
            .ThenBy(candidate => candidate.Body.Id)
            .Select(candidate => candidate.Body)
            .FirstOrDefault()
            ?? throw new InvalidOperationException("validation galaxy had no unoccupied species-viable colony body");
        var target = galaxy.Systems.First(system => system.Id == targetBody.SystemId);
        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);

        var fleet = AddFleet(galaxy, player.Id, FleetRole.Colony, target.Id, target.Position, "Status Colony Ship");
        fleet.EmbarkedPopulationMillions = 250.0;
        fleet.EmbarkedPopulationSpeciesId = player.SpeciesId;
        fleet.DestinationPlanetaryBodyId = targetBody.Id;

        var evaluator = new ExplorationMissionStatusEvaluator();
        var ready = evaluator.Build(galaxy, fleet);
        Require(ready.Phase == ExplorationMissionPhase.ColonySettlementReady,
            "arrived populated colony fleet did not report ColonySettlementReady at its species-viable exact body target");
        Require(ready.EstimatedMissionDaysRemaining == Game.Simulation.Colonization.ColonizationSimulation.ColonyEstablishmentDays,
            "arrived colony fleet did not include establishment time in its mission ETA");
        Require(ready.Summary.Contains(targetBody.Name, StringComparison.Ordinal),
            "settlement readiness summary did not name the exact selected planetary body");
        Require(ready.Summary.Contains(SpeciesCatalog.Get(player.SpeciesId).DisplayName, StringComparison.Ordinal),
            "settlement readiness summary did not identify the passenger species");

        galaxy.Colonies.Add(new ColonyState
        {
            Id = galaxy.Colonies.Max(colony => colony.Id) + 1,
            CivilizationId = player.Id,
            SystemId = target.Id,
            PlanetaryBodyId = targetBody.Id,
            Name = "Status Occupancy Fixture",
            PopulationSpeciesId = player.SpeciesId,
            PopulationMillions = 1.0,
        });
        var occupied = evaluator.Build(galaxy, fleet);
        Require(occupied.Phase == ExplorationMissionPhase.AwaitingOrder,
            "colony fleet still reported settlement readiness after the system became occupied");
    }

    private static FleetState AddFleet(
        GalaxyState galaxy,
        int civilizationId,
        FleetRole role,
        int systemId,
        Vector2 position,
        string name)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 20000 : galaxy.Fleets.Max(existing => existing.Id) + 20000,
            CivilizationId = civilizationId,
            Name = name,
            Role = role,
            Position = position,
            CurrentSystemId = systemId,
            StrategicSpeed = role switch
            {
                FleetRole.Scout => 22.0,
                FleetRole.Science => 18.0,
                FleetRole.Colony => 13.5,
                _ => 18.0,
            },
            SensorRange = 135.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4554_415F_5354_4154L,
            new GalaxyGenerationSettings
            {
                SystemCount = 64,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
