using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>
/// Projects only authoritative interstellar graph edges into local-system gate markers.
/// Catalog knowledge and physical proximity can change a label, but cannot create a gate.
/// </summary>
public static class SystemLanePresentation
{
    public static IReadOnlyList<LocalLaneMarker> Build(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<InterstellarLane> canonicalLanes,
        int currentSystemId,
        Func<int, bool> isKnown)
    {
        ArgumentNullException.ThrowIfNull(systems);
        ArgumentNullException.ThrowIfNull(canonicalLanes);
        ArgumentNullException.ThrowIfNull(isKnown);

        var byId = systems.ToDictionary(system => system.Id);
        if (!byId.TryGetValue(currentSystemId, out var current))
            return Array.Empty<LocalLaneMarker>();

        return canonicalLanes
            .Where(lane => lane.FirstSystemId != lane.SecondSystemId && lane.Connects(currentSystemId))
            .GroupBy(lane => lane.Other(currentSystemId))
            .Select(group => group.First())
            .Where(lane => byId.ContainsKey(lane.Other(currentSystemId)))
            .OrderBy(lane => lane.Other(currentSystemId))
            .Select(lane =>
            {
                var destinationId = lane.Other(currentSystemId);
                var destination = byId[destinationId];
                var delta = destination.Position - current.Position;
                return new LocalLaneMarker(destination.Id, destination.Name,
                    new Vector2(delta.X, delta.Y), isKnown(destination.Id), lane.LengthLightYears);
            })
            .ToArray();
    }

    public static bool HasCanonicalConnection(
        IReadOnlyList<InterstellarLane> canonicalLanes,
        int currentSystemId,
        int destinationSystemId)
    {
        ArgumentNullException.ThrowIfNull(canonicalLanes);
        if (currentSystemId == destinationSystemId) return false;
        return canonicalLanes.Any(lane => lane.FirstSystemId != lane.SecondSystemId &&
            lane.Connects(currentSystemId) && lane.Other(currentSystemId) == destinationSystemId);
    }
}
