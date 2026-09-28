using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;
using Game.Units;

namespace Game.Simulation.Exploration;

/// <summary>
/// Consumer-side contract for practical interstellar mission reach. The authoritative
/// implementation belongs to Economy/Logistics; Exploration and Colonization only ask it
/// whether a physical fleet can support a mission to a target system.
/// </summary>
public interface IInterstellarOperationalReachView
{
    MissionReachAssessment Assess(
        GalaxyState galaxy,
        int civilizationId,
        FleetState fleet,
        int targetSystemId,
        InterstellarMissionKind missionKind);
}

public enum InterstellarMissionKind
{
    ScoutReconnaissance,
    ScienceSurvey,
    Colony,
    MilitaryDeployment,
    Logistics,
}

public sealed record MissionReachAssessment(
    bool IsSupported,
    bool IsAuthoritative,
    string Reason,
    IReadOnlyList<int>? RouteSystemIds = null,
    double RouteDistanceLightYears = 0.0)
{
    public static MissionReachAssessment Supported(string reason = "Mission is within current operational reach.") =>
        new(true, true, reason);

    public static MissionReachAssessment Unsupported(string reason) =>
        new(false, true, string.IsNullOrWhiteSpace(reason) ? "Mission is beyond current operational reach." : reason);

    public static MissionReachAssessment ProvisionalSupported(string reason) =>
        new(true, false, reason);
}

/// <summary>Authoritative early-game reach over the generated interstellar lane graph.</summary>
public sealed class LaneInterstellarOperationalReachView : IInterstellarOperationalReachView
{
    private readonly InterstellarLaneNetwork _lanes = new();

    public MissionReachAssessment Assess(
        GalaxyState galaxy,
        int civilizationId,
        FleetState fleet,
        int targetSystemId,
        InterstellarMissionKind missionKind)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleet);
        if (fleet.CivilizationId != civilizationId)
            return MissionReachAssessment.Unsupported("That fleet is not controlled by this civilization.");
        if (!galaxy.Systems.Any(system => system.Id == targetSystemId))
            return MissionReachAssessment.Unsupported("Unknown mission target.");
        if (fleet.CurrentSystemId is not int originSystemId)
            return MissionReachAssessment.Unsupported("The fleet must finish its current lane leg before receiving a new interstellar route.");

        var route = _lanes.FindShortestRoute(
            galaxy.Systems,
            originSystemId,
            targetSystemId,
            fleet.MaximumLegRangeLightYears);
        if (route.Count == 0)
        {
            return MissionReachAssessment.Unsupported(
                $"No connected lane route is available within this fleet's {InterstellarDistanceUnits.FormatMetricPrimary(fleet.MaximumLegRangeLightYears)} maximum leg range.");
        }

        var systems = galaxy.Systems.ToDictionary(system => system.Id);
        var refuelingSystems = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId)
            .GroupBy(colony => colony.SystemId)
            .ToDictionary(group => group.Key, group => group.Any(colony => colony.Kind == SettlementKind.Colony)
                ? 1.0
                : 0.5);
        var fuelRemaining = refuelingSystems.TryGetValue(originSystemId, out var originService)
            ? fleet.FuelCapacityLightYears * originService
            : fleet.FuelRemainingLightYears;
        foreach (var (first, second) in route.Zip(route.Skip(1)))
        {
            var legDistance = InterstellarDistance.Between(systems[first], systems[second]);
            if (legDistance > fuelRemaining + 1e-9)
            {
                return MissionReachAssessment.Unsupported(
                    $"Insufficient fuel endurance for the lane into {systems[second].Name}: {InterstellarDistanceUnits.FormatMetricPrimary(legDistance)} required, {InterstellarDistanceUnits.FormatMetricPrimary(fuelRemaining)} available before refueling.");
            }
            fuelRemaining -= legDistance;
            if (refuelingSystems.TryGetValue(second, out var serviceLevel))
                fuelRemaining = fleet.FuelCapacityLightYears * serviceLevel;
        }
        var distance = route.Zip(route.Skip(1), (first, second) =>
            InterstellarDistance.Between(systems[first], systems[second])).Sum();
        var legs = Math.Max(0, route.Count - 1);
        return new MissionReachAssessment(
            true,
            true,
            legs == 0
                ? "The fleet is already in the target system."
                : $"Route: {legs} lane leg{(legs == 1 ? string.Empty : "s")}, {InterstellarDistanceUnits.FormatMetricPrimary(distance)} total; maximum leg {InterstellarDistanceUnits.FormatMetricPrimary(fleet.MaximumLegRangeLightYears)}; projected fuel reserve {InterstellarDistanceUnits.FormatMetricPrimary(fuelRemaining)}.",
            route,
            distance);
    }
}

public static class FleetRouteOrders
{
    public static void Assign(
        GalaxyState galaxy,
        FleetState fleet,
        int finalDestinationSystemId,
        MissionReachAssessment reach)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleet);
        ArgumentNullException.ThrowIfNull(reach);
        if (!reach.IsSupported)
            throw new InvalidOperationException("A fleet route can only be assigned from a supported reach assessment.");

        var route = reach.RouteSystemIds;
        if (route is null && fleet.CurrentSystemId is int originSystemId)
        {
            route = new InterstellarLaneNetwork().FindShortestRoute(
                galaxy.Systems,
                originSystemId,
                finalDestinationSystemId,
                fleet.MaximumLegRangeLightYears);
        }
        if (route is null || route.Count == 0)
            route = new[] { finalDestinationSystemId };

        fleet.DestinationSystemId = finalDestinationSystemId;
        fleet.MissionOrderRevision++;
        fleet.HoldRequested = false;
        fleet.ReturnToBaseRequested = false;
        fleet.ReturnToBaseFailureReason = null;
        fleet.PlannedRouteSystemIds = route
            .Where(systemId => systemId != fleet.CurrentSystemId)
            .ToList();
        // Re-routing inside a system retains its real chart position. Only the outbound gate
        // changes; never snap a vessel back to the mission centre.
        if (fleet.CurrentSystemId is int currentId &&
            fleet.TransitPhase is FleetTransitPhase.LocalDeparture or FleetTransitPhase.LocalArrival &&
            galaxy.Systems.FirstOrDefault(system => system.Id == currentId) is { } current)
        {
            var nextId = fleet.PlannedRouteSystemIds.Count > 0
                ? fleet.PlannedRouteSystemIds[0] : finalDestinationSystemId;
            if (galaxy.Systems.FirstOrDefault(system => system.Id == nextId) is { } next)
            {
                fleet.TransitOriginSystemId = currentId;
                fleet.TransitTargetSystemId = nextId;
                FleetLocalTransit.Begin(fleet, FleetTransitPhase.LocalDeparture, fleet.LocalTransitPosition,
                    FleetLocalTransit.GateTowards(next.Position, current.Position));
            }
        }
    }

    public static void Clear(FleetState fleet)
    {
        fleet.DestinationSystemId = null;
        fleet.MissionOrderRevision++;
        fleet.HoldRequested = false;
        fleet.ReturnToBaseRequested = false;
        fleet.ReturnToBaseFailureReason = null;
        fleet.PlannedRouteSystemIds.Clear();
        if (fleet.CurrentSystemId is not null && fleet.TransitPhase != FleetTransitPhase.None)
        {
            // Cancelling a local course retains its chart location and returns through the
            // final approach before local work becomes available.
            FleetLocalTransit.Begin(fleet, FleetTransitPhase.LocalArrival, fleet.LocalTransitPosition, System.Numerics.Vector2.Zero);
            fleet.TransitTargetSystemId = null;
        }
        else if (fleet.TransitPhase != FleetTransitPhase.InterstellarWarp)
        {
            fleet.TransitPhase = FleetTransitPhase.None;
            fleet.TransitOriginSystemId = null;
            fleet.TransitTargetSystemId = null;
            fleet.TransitProgress = 0;
        }
    }
}

/// <summary>
/// Temporary compatibility adapter while Solar Economy/Logistics does not yet expose fleet
/// endurance / origin-to-target support. It deliberately contains no distance, fuel, supply,
/// or endurance formula. Once the authoritative logistics adapter is available, Core should
/// inject it into ExplorationSimulation and ColonizationSimulation.
/// </summary>
public sealed class PrototypeInterstellarOperationalReachView : IInterstellarOperationalReachView
{
    public MissionReachAssessment Assess(
        GalaxyState galaxy,
        int civilizationId,
        FleetState fleet,
        int targetSystemId,
        InterstellarMissionKind missionKind)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleet);
        if (!galaxy.Systems.Any(system => system.Id == targetSystemId))
            return MissionReachAssessment.Unsupported("Unknown mission target.");

        return MissionReachAssessment.ProvisionalSupported(
            "Operational reach is provisionally available until the authoritative logistics endurance contract is integrated.");
    }
}
