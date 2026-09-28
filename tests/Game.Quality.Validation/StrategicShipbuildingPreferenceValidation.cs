using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Quality.Validation;

internal static class StrategicShipbuildingPreferenceValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateBoundedStrategicPreference();
        Console.WriteLine("PASS: strategic Shipbuilding preference stays bounded and preserves colony ownership");
    }

    private static void ValidateBoundedStrategicPreference()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5348_4950_5052_4546L,
            new GalaxyGenerationSettings
            {
                SystemCount = 28,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 0,
                Radius = 360.0f,
            });

        var civilization = galaxy.Civilizations.First(candidate =>
            !candidate.IsPlayer && !candidate.IsSeededAncient && candidate.ExpansionAllowed);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilization.Id);
        construction.CompletedProjectIds.Add("orbital_shipyard");

        var shipyard = galaxy.ShipyardStates.First(state => state.CivilizationId == civilization.Id);
        ResetShipyard(shipyard);
        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.CivilizationId == civilization.Id))
            fleet.IsActive = false;

        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var nextFleetId = galaxy.Fleets.Count == 0 ? 90000 : galaxy.Fleets.Max(fleet => fleet.Id) + 90000;
        var scout = CreateFleet(nextFleetId++, civilization.Id, FleetRole.Scout, "Preference Scout", home.Id, home.Position);
        var science = CreateFleet(nextFleetId++, civilization.Id, FleetRole.Science, "Preference Science", home.Id, home.Position);
        var military = CreateFleet(nextFleetId++, civilization.Id, FleetRole.Military, "Preference Patrol", home.Id, home.Position);
        scout.IsActive = false;
        science.IsActive = false;
        military.IsActive = false;
        galaxy.Fleets.Add(scout);
        galaxy.Fleets.Add(science);
        galaxy.Fleets.Add(military);

        var provider = new StrategicShipbuildingPreferenceProvider();
        var simulation = new ShipbuildingSimulation(new AllCapabilitiesView(), provider);

        Require(provider.GetPreference(civilization.Id) == ShipbuildingStrategicPreference.None,
            "unpublished civilization did not receive the neutral Shipbuilding preference");

        // A strategic military preference may fill the first missing military role before the
        // legacy missing-scout fallback, but it still uses an actually available design.
        Publish(provider, civilization.Id, FleetRole.Military, deferColonization: false);
        simulation.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId == "patrol_corvette",
            "missing preferred Military role did not select the available Patrol Corvette");
        Require(shipyard.ReservedPopulationMillions == 0.0 && shipyard.ReservedPopulationSpeciesId is null,
            "military preference incorrectly reserved colony population");
        ResetShipyard(shipyard);

        // Preference must not become an unbounded duplicate-role builder. With a patrol already
        // active, the normal missing-scout fallback remains authoritative.
        military.IsActive = true;
        Publish(provider, civilization.Id, FleetRole.Military, deferColonization: false);
        simulation.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId == "warp_scout",
            "existing preferred Military role did not fall back to the missing Scout role");
        ResetShipyard(shipyard);

        // With all non-colony baseline roles represented, strategic defer must suppress automatic
        // colony-ship construction without reserving any people.
        scout.IsActive = true;
        science.IsActive = true;
        military.IsActive = true;
        var source = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilization.Id)
            .OrderByDescending(colony => colony.PopulationMillions)
            .First();
        var colonyDesign = ShipDesignRegistry.Get("colony_ship");
        source.PopulationMillions = Math.Max(
            source.PopulationMillions,
            colonyDesign.PopulationCostMillions + 900.0);
        var populationBeforeDeferred = source.PopulationMillions;

        Publish(provider, civilization.Id, FleetRole.Colony, deferColonization: true);
        simulation.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId is null,
            "DeferNewColonization still allowed automatic Colony construction");
        RequireNear(source.PopulationMillions, populationBeforeDeferred,
            "deferred Colony preference changed source population");
        RequireNear(shipyard.ReservedPopulationMillions, 0.0,
            "deferred Colony preference reserved population in the shipyard");

        // Removing defer allows the same strategic preference to start the real colony design.
        // Shipbuilding owns reservation only: no fleet/destination/body is invented at order time.
        Publish(provider, civilization.Id, FleetRole.Colony, deferColonization: false);
        var fleetCountBeforeColonyOrder = galaxy.Fleets.Count;
        simulation.EnsureAutomaticOrders(galaxy);
        Require(shipyard.ActiveDesignId == "colony_ship",
            "nondeferred missing Colony role did not start the Colony Ship design");
        RequireNear(shipyard.ReservedPopulationMillions, colonyDesign.PopulationCostMillions,
            "Colony Ship did not reserve its configured founding population");
        Require(shipyard.ReservedPopulationSpeciesId == source.PopulationSpeciesId,
            "Colony Ship reservation lost the source population species identity");
        RequireNear(
            source.PopulationMillions,
            populationBeforeDeferred - colonyDesign.PopulationCostMillions,
            "Colony Ship reservation did not conserve source population");
        Require(galaxy.Fleets.Count == fleetCountBeforeColonyOrder,
            "automatic Shipbuilding order created a fleet before construction completed");
        Require(!galaxy.Fleets.Any(fleet =>
                fleet.CivilizationId == civilization.Id &&
                fleet.IsActive &&
                fleet.Role == FleetRole.Colony &&
                (fleet.DestinationSystemId is not null || fleet.DestinationPlanetaryBodyId is not null)),
            "Shipbuilding strategic preference invented a colony destination/body");
    }

    private static void Publish(
        StrategicShipbuildingPreferenceProvider provider,
        int civilizationId,
        FleetRole role,
        bool deferColonization)
    {
        provider.Publish(new CivilizationStrategicIntent(
            civilizationId,
            GeneratedAtTick: 10,
            ReviewAfterTick: 40,
            Weights: new Dictionary<StrategicPriorityType, double>(),
            PreferredNewFleetRole: role,
            DeferNewColonization: deferColonization,
            Summary: "Validation preference"));
    }

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        FleetRole role,
        string name,
        int systemId,
        Vector2 position) => new()
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
