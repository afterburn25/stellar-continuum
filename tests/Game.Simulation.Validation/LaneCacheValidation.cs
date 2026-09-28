using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class LaneCacheValidation
{
    [Game.Validation.RegressionCheck]
    public static void PreservesRoutesAndInvalidatesChangedGeometry()
    {
        // Deliberate ties, a zero-length edge, noncontiguous IDs and disconnected range gates.
        var systems = new[] { (9, 0f, 0f), (2, 10f, 0f), (17, 0f, 10f), (5, 10f, 10f),
            (24, 20f, 0f), (31, 10f, 0f), (40, 40f, 20f) }
            .Select(s => new StarSystemState(s.Item1, $"Test {s.Item1}", new Vector2(s.Item2, s.Item3),
                StarArchetype.Standard, false, false, false, false)).ToList();
        var network = new InterstellarLaneNetwork();
        void CheckAll(IReadOnlySet<int>? permitted = null)
        {
            foreach (var range in new[] { 5.0, 10.0, 25.0, double.PositiveInfinity })
            foreach (var origin in systems)
            foreach (var target in systems)
            {
                var expected = OriginalRoute(network.Build(systems), systems, origin.Id, target.Id, range, permitted);
                var actual = network.FindShortestRoute(systems, origin.Id, target.Id, range, permitted);
                if (!expected.SequenceEqual(actual))
                    throw new InvalidOperationException($"Changed route {origin.Id}->{target.Id}, range {range}.");
            }
        }
        CheckAll();
        var originalGraph = network.Build(systems);
        if (!ReferenceEquals(originalGraph, new InterstellarLaneNetwork().Build(systems)))
            throw new InvalidOperationException("Independent mission planners rebuilt unchanged campaign lane geometry.");
        var permitted = systems.Select(s => s.Id).ToHashSet();
        CheckAll(permitted);
        permitted.Remove(2); permitted.Remove(31);
        CheckAll(permitted); // Mutating permissions must not reuse unrestricted or stale routes.
        systems[0] = systems[0] with { Position = new Vector2(200, 200) };
        if (ReferenceEquals(originalGraph, network.Build(systems)))
            throw new InvalidOperationException("Changed stellar coordinates retained obsolete lane geometry.");
        CheckAll();
        systems.RemoveAt(2);
        CheckAll();
        // Exercise more origins/ranges than the bounded tree cache holds; evictions must be transparent.
        for (var range = 1; range < 90; range++)
            network.FindShortestRoute(systems, systems[0].Id, systems[^1].Id, range);
        CheckAll();
    }

    private static IReadOnlyList<int> OriginalRoute(IReadOnlyList<InterstellarLane> lanes,
        IReadOnlyList<StarSystemState> systems, int origin, int target, double range, IReadOnlySet<int>? permitted)
    {
        var ids = systems.Select(s => s.Id).Where(id => permitted is null || permitted.Contains(id)).ToHashSet();
        if (!ids.Contains(origin) || !ids.Contains(target)) return Array.Empty<int>();
        var distance = ids.ToDictionary(id => id, _ => double.PositiveInfinity);
        var previous = new Dictionary<int, int>();
        var remaining = new HashSet<int>(ids);
        distance[origin] = 0;
        while (remaining.Count > 0)
        {
            var current = remaining.OrderBy(id => distance[id]).ThenBy(id => id).First();
            if (!double.IsFinite(distance[current]) || current == target) break;
            remaining.Remove(current);
            foreach (var lane in lanes.Where(l => l.Connects(current) && l.LengthLightYears <= range + 1e-9))
            {
                var next = lane.Other(current);
                var candidate = distance[current] + lane.LengthLightYears;
                if (!remaining.Contains(next) || candidate >= distance[next] - 1e-9) continue;
                distance[next] = candidate; previous[next] = current;
            }
        }
        if (!double.IsFinite(distance[target])) return Array.Empty<int>();
        var route = new List<int> { target };
        while (route[^1] != origin) route.Add(previous[route[^1]]);
        route.Reverse();
        return route;
    }
}
