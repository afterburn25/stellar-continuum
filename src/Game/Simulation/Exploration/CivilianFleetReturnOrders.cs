using System;
using System.Linq;
using Game.Simulation.Colonization;
using Game.Simulation.Models;

namespace Game.Simulation.Exploration;

public sealed record CivilianFleetReturnOrderResult(bool Accepted, bool RequiresConfirmation, string Message);

/// <summary>Authoritative civilian recovery orders. Return always remains physical lane travel.</summary>
public static class CivilianFleetReturnOrders
{
    public static CivilianFleetReturnOrderResult RequestReturn(
        GalaxyState galaxy, int civilizationId, int fleetId, bool confirmAbandonColonyWork = false)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = FindControlledCivilian(galaxy, civilizationId, fleetId);
        if (fleet is null)
            return new(false, false, "No controllable active civilian mission ship with that identity is available.");
        if (fleet.ReturnToBaseRequested)
            return new(false, false, $"{fleet.Name} already has a return-to-base order.");

        if (HasPaidColonyCommitment(fleet) && !confirmAbandonColonyWork)
            return PaidColonyConfirmation(fleet);

        if (fleet.CurrentSystemId is null)
        {
            fleet.HoldRequested = false;
            fleet.ReturnToBaseRequested = true;
            fleet.ReturnToBaseFailureReason = null;
            return new(true, false, $"{fleet.Name} will finish its current lane, then re-evaluate a safe return route.");
        }

        return ActivateAtSystem(galaxy, fleet, acceptedQueuedReturn: false);
    }

    /// <summary>Called only after a physical lane arrival. It never invents fuel or a route.</summary>
    public static CivilianFleetReturnOrderResult ActivateQueuedReturnAtSystem(GalaxyState galaxy, FleetState fleet)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        return !fleet.ReturnToBaseRequested
            ? new(false, false, "No civilian return order is pending.")
            : ActivateAtSystem(galaxy, fleet, acceptedQueuedReturn: true);
    }

    public static CivilianFleetReturnOrderResult PreviewReturn(GalaxyState galaxy, int civilizationId, int fleetId)
    {
        var fleet = FindControlledCivilian(galaxy, civilizationId, fleetId);
        if (fleet is null) return new(false, false, "No controllable active civilian mission ship with that identity is available.");
        if (HasPaidColonyCommitment(fleet))
            return PaidColonyConfirmation(fleet);
        if (fleet.CurrentSystemId is null)
            return new(true, false, "Finish the current lane first; return routing will then be rechecked using actual fuel.");
        return FindNearestReachableBase(galaxy, fleet, out var baseSystemId, out var reach)
            ? new(true, false, $"Nearest reachable refuelling settlement: {galaxy.Systems.First(system => system.Id == baseSystemId).Name}. {reach!.Reason}")
            : new(false, false, "No owned refuelling settlement is reachable with the fleet's current fuel.");
    }

    private static CivilianFleetReturnOrderResult ActivateAtSystem(GalaxyState galaxy, FleetState fleet, bool acceptedQueuedReturn)
    {
        if (fleet.CurrentSystemId is null)
            return new(false, false, $"{fleet.Name} must finish its current lane before return routing can be rechecked.");
        if (!FindNearestReachableBase(galaxy, fleet, out var baseSystemId, out var reach))
        {
            if (!acceptedQueuedReturn)
                return new(false, false, "No owned refuelling settlement is reachable with the fleet's current fuel.");
            fleet.ReturnToBaseRequested = false;
            fleet.ReturnToBaseFailureReason = "No owned refuelling settlement is reachable with current fuel.";
            fleet.HoldRequested = true;
            return new(false, false, $"{fleet.Name} is holding safely: {fleet.ReturnToBaseFailureReason}");
        }

        if (baseSystemId == fleet.CurrentSystemId)
        {
            if (fleet.Role == FleetRole.Colony)
                ColonizationSimulation.AbandonMissionForTransit(fleet);
            FleetRouteOrders.Clear(fleet);
            fleet.ReturnToBaseRequested = false;
            fleet.ReturnToBaseFailureReason = null;
            return new(true, false, $"{fleet.Name} is already at the nearest owned refuelling settlement.");
        }

        if (fleet.Role == FleetRole.Colony)
            ColonizationSimulation.AbandonMissionForTransit(fleet);
        FleetRouteOrders.Assign(galaxy, fleet, baseSystemId, reach!);
        fleet.ReturnToBaseRequested = true;
        return new(true, false, $"{fleet.Name} is returning to {galaxy.Systems.First(system => system.Id == baseSystemId).Name}. {reach!.Reason}");
    }

    private static bool FindNearestReachableBase(GalaxyState galaxy, FleetState fleet, out int baseSystemId, out MissionReachAssessment? selected)
    {
        var reach = new LaneInterstellarOperationalReachView();
        var choices = galaxy.Colonies.Where(colony => colony.CivilizationId == fleet.CivilizationId)
            .Select(colony => colony.SystemId).Distinct()
            .Select(systemId => (SystemId: systemId, Reach: reach.Assess(galaxy, fleet.CivilizationId, fleet, systemId,
                fleet.Role == FleetRole.Colony ? InterstellarMissionKind.Colony : InterstellarMissionKind.ScoutReconnaissance)))
            .Where(item => item.Reach.IsSupported)
            .OrderBy(item => item.Reach.RouteDistanceLightYears).ThenBy(item => item.SystemId).FirstOrDefault();
        baseSystemId = choices.SystemId;
        selected = choices.Reach;
        return selected is not null;
    }

    private static FleetState? FindControlledCivilian(GalaxyState galaxy, int civilizationId, int fleetId) =>
        galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive && candidate.CivilizationId == civilizationId &&
            candidate.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony);

    private static bool HasPaidColonyCommitment(FleetState fleet) => fleet.Role == FleetRole.Colony &&
        (fleet.DestinationPlanetaryBodyId is not null || fleet.SettlementBodyId is not null || fleet.SettlementDaysCompleted > 0.0);

    private static CivilianFleetReturnOrderResult PaidColonyConfirmation(FleetState fleet) => new(false, true,
        $"Returning {fleet.Name} will abandon its paid colony authorization with no refund. " +
        $"Current establishment progress: {fleet.SettlementDaysCompleted:0.#} days; all of it will be lost. " +
        "Colonists remain aboard. Confirm return to continue.");

}
