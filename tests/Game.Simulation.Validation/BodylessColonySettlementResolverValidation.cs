using System.Numerics;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class BodylessColonySettlementResolverValidation
{
    public static void ValidateReadStatusAndFoundingUseSameSpeciesRelativeBody()
    {
        var generated = new GalaxyGenerator().Generate(
            0x424F_4459_4C45_5353L,
            new GalaxyGenerationSettings
            {
                SystemCount = 48,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 520.0f,
            });
        var player = generated.Civilizations.First(civilization => civilization.Id == generated.PlayerCivilizationId);
        var passengerSpeciesId = SpeciesCatalog.TerranBaselineId;
        // Transported people can differ from their fleet owner's founding population.
        // A cryogenic owner cannot naturally inhabit the Terran passenger destination.
        var playerIndex = generated.Civilizations.IndexOf(player);
        player = player with { SpeciesId = SpeciesCatalog.CryogenicHydrocarbonId };
        generated.Civilizations[playerIndex] = player;
        var home = generated.Systems.First(system => system.Id == player.HomeSystemId);
        var occupied = generated.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var target = generated.Systems
            .Where(system => system.Id != home.Id && !occupied.Contains(system.Id))
            .OrderBy(system => system.Id)
            .FirstOrDefault()
            ?? throw new InvalidOperationException("validation galaxy had no unoccupied target system");

        var nextBodyId = generated.PlanetaryBodies.Max(body => body.Id) + 1000;
        var legacyFallback = new PlanetaryBodyState(
            nextBodyId,
            target.Id,
            null,
            0,
            "Legacy Fallback",
            PlanetaryBodyKind.Planet,
            1.1,
            1.2,
            new PlanetaryEnvironmentState(
                2.8,
                510.0,
                4.0,
                PlanetaryAtmosphereRegime.CarbonDioxideRich,
                PlanetarySolventRegime.None,
                0.85,
                false,
                true),
            true,
            false,
            false,
            false).Validated();
        var naturalWorld = new PlanetaryBodyState(
            nextBodyId + 1,
            target.Id,
            null,
            1,
            "Natural Haven",
            PlanetaryBodyKind.Planet,
            1.0,
            1.0,
            new PlanetaryEnvironmentState(
                1.0,
                288.0,
                101.3,
                PlanetaryAtmosphereRegime.OxygenNitrogen,
                PlanetarySolventRegime.Water,
                0.05,
                false,
                true),
            false,
            false,
            false,
            false).Validated();

        var customBodies = generated.PlanetaryBodies
            .Where(body => body.SystemId != target.Id)
            .Concat(new[] { legacyFallback, naturalWorld })
            .OrderBy(body => body.Id)
            .ToArray();
        var galaxy = new GalaxyState
        {
            Seed = generated.Seed,
            Systems = generated.Systems,
            PlanetaryBodies = customBodies,
            Civilizations = generated.Civilizations,
            Fleets = generated.Fleets,
            Colonies = generated.Colonies,
            Economies = generated.Economies,
            Technologies = generated.Technologies,
            ConstructionStates = generated.ConstructionStates,
            ShipyardStates = generated.ShipyardStates,
            PlayerCivilizationId = generated.PlayerCivilizationId,
            Knowledge = generated.Knowledge,
        };

        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var fallbackAssessment = habitability.Evaluate(legacyFallback, passengerSpeciesId);
        var naturalAssessment = habitability.Evaluate(naturalWorld, passengerSpeciesId);
        Require(fallbackAssessment.Viability == SpeciesColonizationViability.HabitatSupportedFallback,
            "fixture legacy body did not produce the intended fallback viability");
        Require(naturalAssessment.Viability == SpeciesColonizationViability.NaturallyViable,
            "fixture natural world did not produce natural viability");
        Require(!habitability.Evaluate(naturalWorld, player.SpeciesId).CanFoundCurrentColony,
            "fixture did not distinguish passenger suitability from owner species suitability");

        var oldCompatibilityChoice = customBodies
            .Where(body => body.SystemId == target.Id)
            .OrderBy(body => body.Id)
            .FirstOrDefault(body => body.LegacyColonizationCandidate && body.Environment.HasSolidSurface);
        Require(oldCompatibilityChoice?.Id == legacyFallback.Id,
            "fixture did not reproduce the old low-ID legacy compatibility choice");

        var independentBest = customBodies
            .Where(body => body.SystemId == target.Id)
            .Select(body => new
            {
                Body = body,
                Assessment = habitability.Evaluate(body, passengerSpeciesId),
            })
            .Where(candidate => candidate.Assessment.CanFoundCurrentColony)
            .OrderByDescending(candidate => candidate.Assessment.Viability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.NaturalHabitability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.UnprotectedOperationalCapacity)
            .ThenBy(candidate => candidate.Body.Id)
            .Select(candidate => candidate.Body)
            .FirstOrDefault();
        Require(independentBest?.Id == naturalWorld.Id,
            "fixture did not establish a species-relative best body distinct from the old compatibility choice");

        var fleet = AddBodylessColonyFleet(galaxy, player.Id, home.Id, home.Position, passengerSpeciesId);
        fleet.DestinationSystemId = target.Id;

        var readModel = new ExplorationReadModel();
        var beforeSurvey = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == fleet.Id);
        Require(beforeSurvey.TargetPlanetaryBodyId is null,
            "body-less mission exposed a planetary target before the destination was fully surveyed");

        var otherObserver = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        galaxy.Knowledge.MarkSystemFullySurveyed(otherObserver.Id, target.Id);
        galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id);
        var afterRecon = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == fleet.Id);
        Require(afterRecon.TargetPlanetaryBodyId is null,
            "reconnaissance or another civilization's full survey revealed passenger settlement suitability");

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        var traveling = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == fleet.Id);
        Require(traveling.TargetPlanetaryBodyId == naturalWorld.Id,
            "body-less traveling mission read model did not select the species-relative best world");

        fleet.Position = target.Position;
        fleet.CurrentSystemId = target.Id;
        fleet.DestinationSystemId = null;

        var arrivedView = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == fleet.Id);
        Require(arrivedView.TargetPlanetaryBodyId == naturalWorld.Id,
            "arrived body-less mission read model disagreed with the species-relative best world");

        var status = new ExplorationMissionStatusEvaluator().Build(galaxy, fleet);
        Require(status.Phase == ExplorationMissionPhase.ColonySettlementReady,
            "body-less arrival did not report settlement readiness on the shared best world");
        Require(status.Summary.Contains(naturalWorld.Name, StringComparison.Ordinal),
            "body-less mission status named a different settlement world than the read model");
        Require(!status.Summary.Contains(legacyFallback.Name, StringComparison.Ordinal),
            "body-less mission status regressed to the old legacy compatibility world");

        // An invalid exact target must remain blocked even when the same system contains
        // a viable fallback. Clearing intent is an explicit operation, never a resolver side effect.
        fleet.DestinationPlanetaryBodyId = int.MaxValue;
        var invalidExact = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == fleet.Id);
        Require(invalidExact.TargetPlanetaryBodyId is null &&
            invalidExact.Status.Phase == ExplorationMissionPhase.AwaitingOrder,
            "an invalid exact target silently fell back to the best available world");
        var colonization = new ColonizationSimulation();
        var invalidEvents = colonization.Advance(galaxy);
        Require(!invalidEvents.Any(entry => entry.FleetId == fleet.Id) &&
            fleet.IsActive && fleet.EmbarkedPopulationMillions == 180.0 &&
            fleet.DestinationPlanetaryBodyId == int.MaxValue,
            "founding mutated or redirected a colony ship whose exact target was invalid");
        fleet.DestinationPlanetaryBodyId = null;

        var economy = galaxy.Economies.First(state => state.CivilizationId == player.Id);
        economy.LastBaseOperationsFundingFraction = 0.0;
        var suspendedEvents = colonization.Advance(galaxy);
        Require(!suspendedEvents.Any(entry => entry.FleetId == fleet.Id) && fleet.IsActive,
            "unfunded colony fleet founded a settlement");
        Require(new ExplorationMissionStatusEvaluator().Build(galaxy, fleet).Summary
                .Contains("operations are unfunded", StringComparison.Ordinal),
            "unfunded arrived colony mission did not explain its suspension");
        economy.LastBaseOperationsFundingFraction = 1.0;

        Require(colonization.Advance(galaxy).Count == 0, "arrival completed colony construction instantly");
        var events = colonization.Advance(galaxy, ColonizationSimulation.ColonyEstablishmentDays);
        var foundedEvent = events.FirstOrDefault(entry => entry.FleetId == fleet.Id)
            ?? throw new InvalidOperationException("body-less arrival did not found a colony");
        var founded = galaxy.Colonies.FirstOrDefault(colony => colony.Id == foundedEvent.ColonyId)
            ?? throw new InvalidOperationException("founding event referenced no colony");
        Require(founded.PlanetaryBodyId == naturalWorld.Id,
            "actual founding chose a different world than read model and mission status");
        Require(foundedEvent.Message.Contains(naturalWorld.Name, StringComparison.Ordinal),
            "founding event named a different world than the shared body-less resolver");
        Require(!fleet.IsActive && fleet.EmbarkedPopulationMillions == 0.0,
            "successful body-less founding did not consume/deactivate the colony fleet");
        Require(founded.PopulationSpeciesId == passengerSpeciesId && founded.PopulationMillions == 180.0,
            "body-less founding changed the transported species or population");

        // Reopen this test system and repeat with an intentional lower-ranked exact target.
        // The resolver must never upgrade the destination behind the command owner's back.
        galaxy.Colonies.Remove(founded);
        var exactFleet = AddBodylessColonyFleet(galaxy, player.Id, target.Id, target.Position, passengerSpeciesId);
        exactFleet.DestinationPlanetaryBodyId = legacyFallback.Id;
        var exactView = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == exactFleet.Id);
        Require(exactView.TargetPlanetaryBodyId == legacyFallback.Id &&
            exactView.Status.Summary.Contains(legacyFallback.Name, StringComparison.Ordinal),
            "an explicit lower-ranked world was replaced in mission read/status surfaces");
        colonization.Advance(galaxy);
        colonization.Advance(galaxy, ColonizationSimulation.ColonyEstablishmentDays);
        var exactColony = galaxy.Colonies.Single(colony => colony.SystemId == target.Id);
        Require(exactColony.PlanetaryBodyId == legacyFallback.Id &&
            exactColony.PopulationSpeciesId == passengerSpeciesId &&
            exactColony.PopulationMillions == 180.0,
            "exact founding re-ranked the selected world or changed passenger population");

        var blockedBodyless = AddBodylessColonyFleet(galaxy, player.Id, target.Id, target.Position, passengerSpeciesId);
        var occupiedView = readModel.Build(galaxy, player.Id).ActiveMissions
            .First(mission => mission.FleetId == blockedBodyless.Id);
        Require(occupiedView.TargetPlanetaryBodyId is null &&
            occupiedView.Status.Phase == ExplorationMissionPhase.AwaitingOrder,
            "body-less target resolution ignored a newly founded colony in the system");
    }

    private static FleetState AddBodylessColonyFleet(
        GalaxyState galaxy,
        int civilizationId,
        int systemId,
        Vector2 position,
        string speciesId)
    {
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 88000 : galaxy.Fleets.Max(existing => existing.Id) + 88000,
            CivilizationId = civilizationId,
            Name = "Shared Resolver Colony Ship",
            Role = FleetRole.Colony,
            Position = position,
            CurrentSystemId = systemId,
            DestinationSystemId = null,
            DestinationPlanetaryBodyId = null,
            StrategicSpeed = 13.5,
            SensorRange = 135.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 180.0,
            EmbarkedPopulationSpeciesId = speciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
