using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Combat;
using Game.Simulation.Models;

namespace Game.Simulation.Shipbuilding;

public static class ShipDesignRegistry
{
    public const string ColonyShipId = "colony_ship";
    public const string ResourceOutpostShipId = "resource_outpost_ship";
    public const string BulkFreighterId = "bulk_freighter";

    private static readonly ShipDesignPrerequisites FirstGenerationInterstellarPrerequisites = new(
        new[]
        {
            ShipbuildingCapabilityIds.SpacecraftConstruction,
            ShipbuildingCapabilityIds.ExperimentalInterstellarTransit,
        },
        Array.Empty<string>(),
        new[] { "orbital_shipyard" });

    public static readonly IReadOnlyList<ShipDesignDefinition> All = new[]
    {
        new ShipDesignDefinition(
            "warp_scout",
            "Pathfinder Scout",
            "Fast first-generation interstellar survey ship optimized for rapid reconnaissance.",
            FleetRole.Scout,
            650.0,
            24.0,
            420.0,
            1200.0,
            140.0f,
            FirstGenerationInterstellarPrerequisites,
            CrewComplementIndividuals: 24, CreditCost: 70.0),
        new ShipDesignDefinition(
            "science_vessel",
            "Deep-Space Science Vessel",
            "Long-range research platform with enhanced sensors for anomalies and unusual systems.",
            FleetRole.Science,
            850.0,
            18.0,
            400.0,
            1100.0,
            185.0f,
            FirstGenerationInterstellarPrerequisites,
            CrewComplementIndividuals: 72, CreditCost: 100.0),
        new ShipDesignDefinition(
            "patrol_corvette",
            "Patrol Corvette",
            "First-generation armed patrol and escort vessel for local defense and early fleet combat.",
            FleetRole.Military,
            1000.0,
            21.0,
            340.0,
            800.0,
            125.0f,
            FirstGenerationInterstellarPrerequisites,
            PopulationCostMillions: 0.0,
            CombatProfileId: CombatProfileIds.PatrolCorvetteMk1,
            CrewComplementIndividuals: 85, CreditCost: 120.0),
        new ShipDesignDefinition(
            ColonyShipId,
            "Interstellar Colony Ship",
            "Large settlement vessel carrying industrial seed equipment and a founding population.",
            FleetRole.Colony,
            1500.0,
            13.5,
            300.0,
            750.0,
            80.0f,
            FirstGenerationInterstellarPrerequisites,
            PopulationCostMillions: 250.0,
            CrewComplementIndividuals: 320, CreditCost: 180.0),
        new ShipDesignDefinition(
            ResourceOutpostShipId,
            "Sealed Resource Outpost Vessel",
            "Carries a compact pressure-sealed habitat, extraction equipment, and a permanent specialist crew for valuable harsh worlds.",
            FleetRole.Colony,
            950.0,
            16.0,
            330.0,
            900.0,
            95.0f,
            FirstGenerationInterstellarPrerequisites,
            PopulationCostMillions: 8.0,
            CrewComplementIndividuals: 180,
            CreditCost: 130.0),
        new ShipDesignDefinition(
            BulkFreighterId,
            "Interstellar Bulk Freighter",
            "Early freight vessel that collects processed outpost material and returns it to a developed colony.",
            FleetRole.Logistics,
            800.0,
            17.0,
            350.0,
            1000.0,
            85.0f,
            FirstGenerationInterstellarPrerequisites,
            CrewComplementIndividuals: 60,
            CreditCost: 90.0,
            CargoMaterialCapacity: 100.0,
            CargoTransferRatePerDay: 20.0),
    };

    public static ShipDesignDefinition Get(string id) =>
        All.First(design => string.Equals(design.Id, id, StringComparison.Ordinal));

    public static bool TryGet(string? id, out ShipDesignDefinition? definition)
    {
        definition = string.IsNullOrWhiteSpace(id)
            ? null
            : All.FirstOrDefault(design => string.Equals(design.Id, id, StringComparison.Ordinal));
        return definition is not null;
    }

    public static ShipDesignDefinition GetForFleet(FleetState fleet)
    {
        ArgumentNullException.ThrowIfNull(fleet);
        if (TryGet(fleet.DesignId, out var persisted) && persisted!.Role == fleet.Role)
            return persisted;

        return GetCurrentDesignForRole(fleet.Role);
    }

    /// <summary>
    /// Legacy resolver for fleets and fixtures created before persistent design identity.
    /// New fleets should resolve through GetForFleet so multiple designs can share a role.
    /// </summary>
    public static ShipDesignDefinition GetCurrentDesignForRole(FleetRole role)
    {
        var baselineId = role switch
        {
            FleetRole.Scout => "warp_scout",
            FleetRole.Science => "science_vessel",
            FleetRole.Military => "patrol_corvette",
            FleetRole.Colony => ColonyShipId,
            FleetRole.Logistics => BulkFreighterId,
            _ => throw new InvalidOperationException($"No baseline ship design is registered for fleet role {role}."),
        };
        var design = Get(baselineId);
        if (design.CrewComplementIndividuals <= 0)
        {
            throw new InvalidOperationException(
                $"Ship design {design.Id} does not define a positive crew complement.");
        }

        return design;
    }
}
