using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class ArrivedColonyMissionBodyViewValidation
{
    public static void ValidateArrivedAndIdleColonyBodyTargetVisibility()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
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
            .OrderBy(candidate => candidate.Body.SystemId)
            .ThenBy(candidate => candidate.Body.Id)
            .Select(candidate => candidate.Body)
            .FirstOrDefault()
            ?? throw new InvalidOperationException("validation galaxy had no unoccupied viable colony target body");
        var target = galaxy.Systems.First(system => system.Id == targetBody.SystemId);
        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);

        var exactFleet = AddColonyFleet(galaxy, player.Id, home.Id, home.Position, "Exact Body Mission");
        exactFleet.DestinationSystemId = target.Id;
        exactFleet.DestinationPlanetaryBodyId = targetBody.Id;

        var readModel = new ExplorationReadModel();
        var traveling = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == exactFleet.Id);
        Require(traveling.TargetPlanetaryBodyId == targetBody.Id,
            "traveling exact-body colony mission lost its selected planetary body");

        exactFleet.Position = target.Position;
        exactFleet.CurrentSystemId = target.Id;
        exactFleet.DestinationSystemId = null;

        var arrived = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == exactFleet.Id);
        Require(arrived.TargetPlanetaryBodyId == targetBody.Id,
            "arrived exact-body colony mission lost its planetary target when system travel completed");

        var expectedLegacyBody = galaxy.PlanetaryBodies
            .Where(body => body.SystemId == target.Id)
            .Select(body => new
            {
                Body = body,
                Assessment = habitability.Evaluate(body, player.SpeciesId),
            })
            .Where(candidate => candidate.Assessment.CanFoundCurrentColony)
            .OrderByDescending(candidate => candidate.Assessment.Viability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.NaturalHabitability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.UnprotectedOperationalCapacity)
            .ThenBy(candidate => candidate.Body.Id)
            .Select(candidate => candidate.Body)
            .FirstOrDefault()
            ?? throw new InvalidOperationException("selected target system lost all viable settlement bodies");

        var legacyFleet = AddColonyFleet(galaxy, player.Id, target.Id, target.Position, "Legacy Bodyless Arrival");
        legacyFleet.DestinationSystemId = null;
        legacyFleet.DestinationPlanetaryBodyId = null;

        var legacyArrival = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == legacyFleet.Id);
        Require(legacyArrival.TargetPlanetaryBodyId == expectedLegacyBody.Id,
            "body-less populated colony arrival did not expose the species-relative settlement target");

        var idleHomeFleet = AddColonyFleet(galaxy, player.Id, home.Id, home.Position, "Idle Home Colony Ship");
        idleHomeFleet.DestinationSystemId = null;
        idleHomeFleet.DestinationPlanetaryBodyId = null;

        var idle = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == idleHomeFleet.Id);
        Require(idle.TargetPlanetaryBodyId is null,
            "idle body-less colony ship parked at a founded colony falsely appeared settlement-bound");

        exactFleet.DestinationPlanetaryBodyId = int.MaxValue;
        var staleExact = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == exactFleet.Id);
        Require(staleExact.TargetPlanetaryBodyId is null,
            "arrived colony mission exposed a stale/nonexistent exact body target");
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        int civilizationId,
        int systemId,
        Vector2 position,
        string name)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 70000 : galaxy.Fleets.Max(existing => existing.Id) + 70000,
            CivilizationId = civilizationId,
            Name = name,
            Role = FleetRole.Colony,
            Position = position,
            CurrentSystemId = systemId,
            StrategicSpeed = 13.5,
            SensorRange = 135.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 250.0,
            EmbarkedPopulationSpeciesId = galaxy.Civilizations.First(civilization => civilization.Id == civilizationId).SpeciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4152_5249_5645_445FL,
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
