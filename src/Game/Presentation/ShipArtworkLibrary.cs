using System;
using Game.Simulation.Models;

namespace Game.Presentation;

/// <summary>Stable runtime paths for the first playable human ship family.</summary>
public static class ShipArtworkLibrary
{
    public const string PathfinderScout = "res://assets/visual/ships/pathfinder-scout.jpg";
    public const string ScienceVessel = "res://assets/visual/ships/deep-space-science-vessel.jpg";
    public const string PatrolCorvette = "res://assets/visual/ships/patrol-corvette.jpg";
    public const string ColonyShip = "res://assets/visual/ships/interstellar-colony-ship.jpg";
    public const string ResourceOutpostShip = "res://assets/visual/ships/resource-outpost-ship.png";
    public const string BulkFreighter = "res://assets/visual/ships/interstellar-bulk-freighter.png";

    public static string PathForDesign(string designId) => designId switch
    {
        "warp_scout" => PathfinderScout,
        "science_vessel" => ScienceVessel,
        "patrol_corvette" => PatrolCorvette,
        "colony_ship" => ColonyShip,
        "resource_outpost_ship" => ResourceOutpostShip,
        "bulk_freighter" => BulkFreighter,
        _ => throw new ArgumentOutOfRangeException(nameof(designId), designId,
            "No production ship artwork is registered for this design."),
    };

    public static string PathForRole(FleetRole role) => role switch
    {
        FleetRole.Scout => PathfinderScout,
        FleetRole.Science => ScienceVessel,
        FleetRole.Military => PatrolCorvette,
        FleetRole.Colony => ColonyShip,
        FleetRole.Logistics => BulkFreighter,
        _ => throw new ArgumentOutOfRangeException(nameof(role), role,
            "No production ship artwork is registered for this fleet role."),
    };
}
