using System.Collections.Generic;
using System.Numerics;
using Game.Simulation.Combat;

namespace Game.Simulation.Models;

public sealed class FleetState
{
    public required int Id { get; init; }
    public required int CivilizationId { get; init; }
    public required string Name { get; init; }
    public required FleetRole Role { get; init; }
    public string? DesignId { get; init; }
    public required Vector2 Position { get; set; }
    public int? CurrentSystemId { get; set; }
    public int? DestinationSystemId { get; set; }
    public FleetTransitPhase TransitPhase { get; set; }
    public int? TransitOriginSystemId { get; set; }
    public int? TransitTargetSystemId { get; set; }
    public double TransitProgress { get; set; }
    /// <summary>Persisted normalized system-chart movement, separate from strategic light-year positions and fuel.</summary>
    public Vector2 LocalTransitStart { get; set; }
    public Vector2 LocalTransitPosition { get; set; }
    public Vector2 LocalTransitTarget { get; set; }

    /// <summary>
    /// Remaining lane waypoints, excluding the system the fleet departed from and including
    /// the final destination. DestinationSystemId remains the mission target so survey and
    /// colony consumers never mistake an intermediate stop for arrival.
    /// </summary>
    public List<int> PlannedRouteSystemIds { get; set; } = new();

    /// <summary>
    /// Player-issued civilian hold. A ship already between systems completes its current lane,
    /// then retains the represented mission until an authoritative resume order clears this flag.
    /// </summary>
    public bool HoldRequested { get; set; }
    public bool ReturnToBaseRequested { get; set; }
    public string? ReturnToBaseFailureReason { get; set; }
    /// <summary>Monotonic identity for a civilian mission; settlement work does not change it.</summary>
    public int MissionOrderRevision { get; set; }

    /// <summary>
    /// Exact planetary-body target for a body-aware colony mission. Null is normal for
    /// non-colony fleets and idle colony ships. Keeping the body ID beside the system target
    /// avoids guessing after save/load once more than one body can be species-suitable.
    /// </summary>
    public int? DestinationPlanetaryBodyId { get; set; }
    // Persistent local work: travel and on-site work consume separate simulation steps.
    public bool PreventAutomaticSettlement { get; set; }
    public int? SettlementBodyId { get; set; }
    public double SettlementDaysCompleted { get; set; }
    public int? ReconnaissanceSystemId { get; set; }
    public double ReconnaissanceDaysCompleted { get; set; }
    public int? FreightTargetOutpostId { get; set; }
    public int? FreightHomeColonyId { get; set; }
    public double CargoMaterialCapacity { get; init; }
    public double CargoMaterials { get; set; }

    public double StrategicSpeed { get; init; } = 22.0;
    public double MaximumLegRangeLightYears { get; init; } = 360.0;
    public double FuelCapacityLightYears { get; init; } = 1000.0;
    public double FuelRemainingLightYears { get; set; } = 1000.0;
    public float SensorRange { get; init; } = 135.0f;
    public bool IsActive { get; set; } = true;

    /// <summary>
    /// Real population physically committed to this fleet. For early-release colony ships,
    /// shipbuilding removes these colonists from a source colony and colonization transfers
    /// exactly this amount into the destination settlement.
    /// </summary>
    public double EmbarkedPopulationMillions { get; set; }

    /// <summary>
    /// Species identity of the embarked scalar population. Null is valid only when no
    /// population is aboard; future multi-species transport should replace this bridge with
    /// a bounded manifest rather than parallel unbounded passenger objects.
    /// </summary>
    public string? EmbarkedPopulationSpeciesId { get; set; }

    /// <summary>
    /// Compact persistent vessel combat state. The current strategic model represents each
    /// constructed vessel directly as a FleetState; future multi-vessel composition can wrap
    /// this state without making presentation authoritative.
    /// </summary>
    public FleetCombatState? Combat { get; set; }
    /// <summary>Installed tactical equipment and persistent named-vessel damage/history. Absent in legacy saves.</summary>
    public Game.Simulation.Combat.Massive.MassiveCombatLoadout? TacticalLoadout { get; set; }
    public Game.Simulation.Combat.Massive.MassiveVesselState? TacticalVessel { get; set; }
}

/// <summary>Authoritative sub-leg of a lane crossing. Local gates are physical timed legs,
/// not presentation animation; work may start only once the phase returns to None.</summary>
public enum FleetTransitPhase { None, LocalDeparture, InterstellarWarp, LocalArrival }

public enum FleetRole
{
    Scout,
    Science,
    Colony,
    Military,
    Logistics,
}
