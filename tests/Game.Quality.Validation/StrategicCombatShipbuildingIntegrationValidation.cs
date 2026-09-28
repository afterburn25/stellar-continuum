using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Quality.Validation;

internal static class StrategicCombatShipbuildingIntegrationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateCombatStrategyShipbuildingChain();
        Console.WriteLine("PASS: exact-own Combat readiness drives fair strategic Shipbuilding intent");
    }

    private static void ValidateCombatStrategyShipbuildingChain()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_4D42_4154_4149L,
            new GalaxyGenerationSettings
            {
                SystemCount = 30,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 0,
                Radius = 380.0f,
            });

        var civilization = galaxy.Civilizations.First(candidate =>
            !candidate.IsPlayer && !candidate.IsSeededAncient && candidate.ExpansionAllowed);
        var foreign = galaxy.Civilizations.First(candidate => candidate.Id != civilization.Id);
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);

        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilization.Id);
        construction.CompletedProjectIds.Add("orbital_shipyard");
        var shipyard = galaxy.ShipyardStates.First(state => state.CivilizationId == civilization.Id);
        ResetShipyard(shipyard);

        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.CivilizationId == civilization.Id))
            fleet.IsActive = false;

        var nextFleetId = galaxy.Fleets.Count == 0 ? 120000 : galaxy.Fleets.Max(fleet => fleet.Id) + 120000;
        var ownPatrol = CreatePatrol(nextFleetId++, civilization.Id, home.Id, home.Position, "Strategic Own Patrol");
        var foreignPatrol = CreatePatrol(nextFleetId++, foreign.Id, home.Id, home.Position, "Strategic Foreign Patrol");
        galaxy.Fleets.Add(ownPatrol);
        galaxy.Fleets.Add(foreignPatrol);

        var inputBuilder = new CivilizationStrategicInputBuilder(
            shipbuildingCapabilities: new AllCapabilitiesView());
        var ownState = inputBuilder.Build(galaxy, civilization.Id);
        var ownReadiness = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        RequireNear(ownState.MilitaryStrength, Math.Max(1.0, ownReadiness.CombatEffectiveArmedStrength),
            "strategic own strength diverged from Combat readiness before planning");

        var observedThreat = Math.Max(10.0, ownState.MilitaryStrength * 4.0);
        var knowledge = new KnowledgeSnapshot
        {
            ObservedAtTick = 100,
            Civilizations = new Dictionary<int, KnownCivilization>
            {
                [foreign.Id] = new KnownCivilization(
                    CivilizationId: foreign.Id,
                    Trust: -0.70,
                    EstimatedMilitaryLow: observedThreat * 0.90,
                    EstimatedMilitaryHigh: observedThreat * 1.10,
                    EstimateConfidence: 0.90,
                    LastMilitaryObservationTick: 95,
                    HasSharedBorder: true,
                    KnownTradeDependence: 0.0,
                    KnownWarExhaustion: 0.0,
                    KnownToBeAtWar: true,
                    HasDefenseTreatyWithObserver: false),
            },
        };

        var director = new CivilizationStrategicDirector();
        var review = director.Review(
            galaxy,
            civilization.Id,
            civilization.Traits,
            knowledge,
            nowTick: 100,
            forceReview: true);

        Require(review.Plan.Priorities.Any(priority => priority.Type == StrategicPriorityType.Defend),
            "legitimately observed stronger rival did not create a defensive strategic priority");
        Require(review.Intent.PreferredNewFleetRole == FleetRole.Military,
            "defensive strategic pressure did not prefer a Military vessel");
        Require(review.Intent.DeferNewColonization,
            "defensive strategic pressure did not defer automatic colony expansion");

        // Mutating authoritative foreign fleet truth must not affect own-state inputs or a plan
        // driven by the fixed observer-local estimate above.
        foreignPatrol.Combat!.Shields = 0.0;
        foreignPatrol.Combat.Armor = 0.0;
        foreignPatrol.Combat.Hull = 1.0;
        foreignPatrol.Combat.Order = MilitaryOrderType.Retreat;
        var afterForeignMutation = inputBuilder.Build(galaxy, civilization.Id);
        RequireNear(afterForeignMutation.MilitaryStrength, ownState.MilitaryStrength,
            "foreign authoritative fleet mutation leaked into Civilization own strength");

        var secondDirector = new CivilizationStrategicDirector();
        var secondReview = secondDirector.Review(
            galaxy,
            civilization.Id,
            civilization.Traits,
            knowledge,
            nowTick: 100,
            forceReview: true);
        Require(secondReview.Intent.PreferredNewFleetRole == review.Intent.PreferredNewFleetRole &&
                secondReview.Intent.DeferNewColonization == review.Intent.DeferNewColonization,
            "foreign authoritative fleet mutation changed strategy despite unchanged observer knowledge");

        var preferenceProvider = new StrategicShipbuildingPreferenceProvider();
        preferenceProvider.Publish(review);
        var shipbuilding = new ShipbuildingSimulation(new AllCapabilitiesView(), preferenceProvider);

        // Remove the planning fixture patrol only after the review: Shipbuilding should consume
        // the published Military preference and create a real Military order through its own rules.
        ownPatrol.IsActive = false;
        shipbuilding.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId == "patrol_corvette",
            "defensive strategic intent did not reach automatic Shipbuilding as a Military preference");
        ResetShipyard(shipyard);

        // Once baseline roles are represented, the same defensive intent must prevent the legacy
        // automatic Colony fallback from reserving population. Shipbuilding still owns the gate.
        galaxy.Fleets.Add(CreateFleet(nextFleetId++, civilization.Id, FleetRole.Scout, home.Id, home.Position, "Strategic Scout"));
        galaxy.Fleets.Add(CreateFleet(nextFleetId++, civilization.Id, FleetRole.Science, home.Id, home.Position, "Strategic Science"));
        galaxy.Fleets.Add(CreateFleet(nextFleetId++, civilization.Id, FleetRole.Military, home.Id, home.Position, "Strategic Guard"));

        var source = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id)
            .OrderByDescending(colony => colony.PopulationMillions)
            .First();
        var colonyDesign = ShipDesignRegistry.Get("colony_ship");
        source.PopulationMillions = Math.Max(source.PopulationMillions, colonyDesign.PopulationCostMillions + 900.0);
        var populationBefore = source.PopulationMillions;

        shipbuilding.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId is null,
            "defensive DeferNewColonization allowed the automatic Colony fallback to start");
        RequireNear(source.PopulationMillions, populationBefore,
            "deferred automatic Colony fallback reserved population");
        RequireNear(shipyard.ReservedPopulationMillions, 0.0,
            "deferred automatic Colony fallback left a population reservation");
    }

    private static FleetState CreatePatrol(
        int id,
        int civilizationId,
        int systemId,
        Vector2 position,
        string name)
    {
        var fleet = CreateFleet(id, civilizationId, FleetRole.Military, systemId, position, name);
        fleet.Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military);
        return fleet;
    }

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        FleetRole role,
        int systemId,
        Vector2 position,
        string name) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = role,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 20.0,
        SensorRange = 120.0f,
        IsActive = true,
    };

    private static void ResetShipyard(ShipyardState shipyard)
    {
        shipyard.ActiveDesignId = null;
        shipyard.ActiveBuildProgress = 0.0;
        shipyard.ReservedPopulationMillions = 0.0;
        shipyard.ReservedPopulationSpeciesId = null;
        shipyard.QueuedBuilds.Clear();
    }

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private sealed class AllCapabilitiesView : IShipbuildingCapabilityView
    {
        public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId)
        {
            _ = galaxy;
            _ = civilizationId;
            _ = capabilityId;
            return true;
        }
    }
}
