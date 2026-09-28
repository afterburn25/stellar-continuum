using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Exploration;

public sealed record CivilianFleetHoldOrderResult(bool Accepted, string Message);

/// <summary>Authoritative boundary for player civilian hold and resume commands.</summary>
public static class CivilianFleetHoldOrders
{
    public static CivilianFleetHoldOrderResult Hold(GalaxyState galaxy, int civilizationId, int fleetId) =>
        SetHold(galaxy, civilizationId, fleetId, true);

    public static CivilianFleetHoldOrderResult Resume(GalaxyState galaxy, int civilizationId, int fleetId) =>
        SetHold(galaxy, civilizationId, fleetId, false);

    private static CivilianFleetHoldOrderResult SetHold(GalaxyState galaxy, int civilizationId, int fleetId, bool requested)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive &&
            candidate.CivilizationId == civilizationId && candidate.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony);
        if (fleet is null)
            return new(false, "No controllable active civilian mission ship with that identity is available.");
        if (fleet.HoldRequested == requested)
            return new(false, requested ? $"{fleet.Name} already has a hold order." : $"{fleet.Name} is already proceeding under its current orders.");

        fleet.HoldRequested = requested;
        if (!requested)
        {
            fleet.ReturnToBaseFailureReason = null;
            return new(true, $"{fleet.Name} resumed its existing orders.");
        }

        if (fleet.CurrentSystemId is int currentId)
        {
            var current = galaxy.Systems.FirstOrDefault(system => system.Id == currentId)?.Name ?? "the current system";
            return new(true, $"{fleet.Name} is holding at {current}.");
        }

        var nextId = fleet.PlannedRouteSystemIds.Count > 0
            ? fleet.PlannedRouteSystemIds[0]
            : fleet.DestinationSystemId ?? -1;
        var next = galaxy.Systems.FirstOrDefault(system => system.Id == nextId)?.Name ?? "the next system";
        return new(true, $"{fleet.Name} will hold after reaching {next}.");
    }
}
