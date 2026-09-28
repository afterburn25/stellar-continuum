using Game.Simulation.Models;

namespace Game.Simulation.Shipbuilding;

/// <summary>
/// Advisory civilization-level preference for the next automatic ship role. This does not grant
/// prerequisites, bypass population reservation, choose colony destinations, or force duplicate
/// roles. Shipbuilding remains authoritative for available designs and order creation.
/// </summary>
public sealed record ShipbuildingStrategicPreference(
    FleetRole? PreferredNewFleetRole,
    bool DeferNewColonization)
{
    public static ShipbuildingStrategicPreference None { get; } = new(null, false);
}

public interface IShipbuildingStrategicPreferenceView
{
    ShipbuildingStrategicPreference GetPreference(int civilizationId);
}
