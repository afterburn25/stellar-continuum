using System;
using System.Numerics;
using System.Linq;

namespace Game.Simulation.Models;

/// <summary>
/// Authoritative physical separation between star systems. Chart coordinates remain 2D for
/// presentation; older saves omit depth and therefore retain their original flat geometry.
/// </summary>
public static class InterstellarDistance
{
    public static double Between(StarSystemState first, StarSystemState second)
    {
        ArgumentNullException.ThrowIfNull(first);
        ArgumentNullException.ThrowIfNull(second);
        if (first.GalacticDepthLightYears is null && second.GalacticDepthLightYears is null)
            return Vector2.Distance(first.Position, second.Position);
        var dx = (double)first.Position.X - second.Position.X;
        var dy = (double)first.Position.Y - second.Position.Y;
        var dz = first.GalacticDepthLightYears.GetValueOrDefault() - second.GalacticDepthLightYears.GetValueOrDefault();
        return Math.Sqrt(dx * dx + dy * dy + dz * dz);
    }

    public static double SquaredBetween(StarSystemState first, StarSystemState second)
    {
        ArgumentNullException.ThrowIfNull(first);
        ArgumentNullException.ThrowIfNull(second);
        if (first.GalacticDepthLightYears is null && second.GalacticDepthLightYears is null)
            return Vector2.DistanceSquared(first.Position, second.Position);
        var dx = (double)first.Position.X - second.Position.X;
        var dy = (double)first.Position.Y - second.Position.Y;
        var dz = first.GalacticDepthLightYears.GetValueOrDefault() - second.GalacticDepthLightYears.GetValueOrDefault();
        return dx * dx + dy * dy + dz * dz;
    }

    public static Vector2 InterpolateChartPosition(StarSystemState origin, StarSystemState target, double progress) =>
        Vector2.Lerp(origin.Position, target.Position, (float)Math.Clamp(progress, 0.0, 1.0));

    public static double FromFleet(GalaxyState galaxy, FleetState fleet, StarSystemState target)
    {
        var origin = galaxy.Systems.FirstOrDefault(system => system.Id ==
            (fleet.TransitPhase == FleetTransitPhase.InterstellarWarp ? fleet.TransitOriginSystemId : fleet.CurrentSystemId));
        if (origin?.GalacticDepthLightYears is null && target.GalacticDepthLightYears is null)
            return Vector2.Distance(fleet.Position, target.Position);
        var depth = origin?.GalacticDepthLightYears ?? 0;
        if (fleet.TransitPhase == FleetTransitPhase.InterstellarWarp)
        {
            var waypointId = fleet.PlannedRouteSystemIds.Count > 0 ? fleet.PlannedRouteSystemIds[0] : fleet.DestinationSystemId;
            var waypoint = galaxy.Systems.FirstOrDefault(system => system.Id == waypointId);
            if (waypoint is not null)
                depth += (waypoint.GalacticDepthLightYears.GetValueOrDefault() - depth) * Math.Clamp(fleet.TransitProgress, 0, 1);
        }
        var dx = (double)fleet.Position.X - target.Position.X;
        var dy = (double)fleet.Position.Y - target.Position.Y;
        var dz = depth - target.GalacticDepthLightYears.GetValueOrDefault();
        return Math.Sqrt(dx * dx + dy * dy + dz * dz);
    }
}
