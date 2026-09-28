using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;

namespace Game.Presentation;

/// <summary>Cached observer-safe political ownership cells. Claims stay separate from ownership.</summary>
public sealed class StrategicTerritoryProjection
{
    public IReadOnlyList<StrategicTerritoryRegion> Territories { get; }
    public IReadOnlyList<StrategicTerritoryClaimOutline> Claims { get; }
    public IReadOnlyList<StrategicTerritoryFillRun> FogRuns { get; }
    public IReadOnlyList<IReadOnlyList<Vector2>> FogContours { get; }
    public StrategicFogMask FogMask { get; }
    public IReadOnlySet<int> UnexploredSystemIds { get; }
    public int UnownedCellCount { get; }
    public int GridCellCount { get; }
    private StrategicTerritoryProjection(
        IReadOnlyList<StrategicTerritoryRegion> territories,
        IReadOnlyList<StrategicTerritoryClaimOutline> claims,
        IReadOnlyList<StrategicTerritoryFillRun> fogRuns,
        IReadOnlyList<IReadOnlyList<Vector2>> fogContours,
        StrategicFogMask fogMask,
        IReadOnlySet<int> unexplored,
        int unownedCellCount,
        int gridCellCount)
    {
        Territories = territories;
        Claims = claims;
        FogRuns = fogRuns;
        FogContours = fogContours;
        FogMask = fogMask;
        UnexploredSystemIds = unexplored;
        UnownedCellCount = unownedCellCount;
        GridCellCount = gridCellCount;
    }

    public static StrategicTerritoryProjection Build(GalaxyState galaxy, int observerId,
        IReadOnlyList<TerritorialClaimSnapshot>? observerClaims = null, float coordinateScale = 1f)
    {
        if (!float.IsFinite(coordinateScale) || coordinateScale <= 0)
            throw new ArgumentOutOfRangeException(nameof(coordinateScale));
        var systems = galaxy.Systems.ToDictionary(system => system.Id);
        // Territory geometry is a presentation cache. A local catalogue may enlarge its
        // projected light-year coordinates for readability, but this never changes the
        // authoritative systems, ownership, diplomacy, or observer knowledge.
        var positions = galaxy.Systems.ToDictionary(system => system.Id, system => system.Position * coordinateScale);
        var civilizations = galaxy.Civilizations.ToDictionary(civilization => civilization.Id);
        var settlementOwners = galaxy.Colonies
            .GroupBy(colony => colony.SystemId)
            .ToDictionary(group => group.Key, group => group.OrderBy(colony => colony.Id).First().CivilizationId);
        var anchors = new Dictionary<(int, int), StrategicTerritoryAnchor>();

        bool Visible(int civilizationId, int systemId) =>
            civilizationId == observerId
            || galaxy.Knowledge.IsCivilizationKnown(observerId, civilizationId)
            && galaxy.Knowledge.IsSystemFullySurveyed(observerId, systemId);

        void Add(int civilizationId, int systemId, StrategicTerritoryAnchorKind kind)
        {
            if (!systems.TryGetValue(systemId, out var star) || !Visible(civilizationId, systemId)) return;
            var key = (civilizationId, systemId);
            if (!anchors.TryGetValue(key, out var previous) || kind < previous.Kind)
                anchors[key] = new(civilizationId, systemId, positions[star.Id], kind);
        }

        // A current settlement authority supersedes a former civilization's natal marker.
        foreach (var civilization in civilizations.Values)
            if (!settlementOwners.TryGetValue(civilization.HomeSystemId, out var owner)
                || owner == civilization.Id
                || !Visible(owner, civilization.HomeSystemId))
                Add(civilization.Id, civilization.HomeSystemId, StrategicTerritoryAnchorKind.Home);
        foreach (var colony in galaxy.Colonies)
            Add(colony.CivilizationId, colony.SystemId, StrategicTerritoryAnchorKind.Settlement);

        var all = anchors.Values.OrderBy(anchor => anchor.CivilizationId).ThenBy(anchor => anchor.SystemId).ToArray();
        var grid = TerritoryGrid.Create(positions.Values);
        var radii = all.ToDictionary(
            anchor => (anchor.CivilizationId, anchor.SystemId),
            anchor => Math.Max(Radius(anchor, all), grid.CellSize * .72f));
        var cells = Assign(grid, all, radii);
        PreserveVisibleOwners(grid, cells, all);
        var regions = Regions(grid, cells, all, radii, civilizations);
        var claims = new List<StrategicTerritoryClaimOutline>();
        foreach (var claim in observerClaims ?? Array.Empty<TerritorialClaimSnapshot>())
            if (claim.Active
                && systems.TryGetValue(claim.SystemId, out var star)
                && civilizations.ContainsKey(claim.ClaimantCivilizationId)
                && Visible(claim.ClaimantCivilizationId, claim.SystemId))
                claims.Add(new(claim.ClaimantCivilizationId, claim.SystemId, positions[star.Id], Math.Max(28f, grid.CellSize * 1.3f)));

        var unknown = galaxy.Systems
            .Where(system => !galaxy.Knowledge.IsSystemKnown(observerId, system.Id))
            .Select(system => system.Id)
            .ToHashSet();
        var unowned = cells.Cast<int>().Count(owner => owner < 0);
        var fogCells = BuildFogCells(grid, galaxy.Systems, positions, unknown);
        return new(
            regions,
            claims.OrderBy(x => x.CivilizationId).ThenBy(x => x.SystemId).ToArray(),
            Runs(grid, fogCells, 1),
            Contours(grid, fogCells, 1),
            BuildFogMask(grid, fogCells),
            unknown,
            unowned,
            grid.Width * grid.Height);
    }

    private static int[,] Assign(TerritoryGrid grid, IReadOnlyList<StrategicTerritoryAnchor> anchors, IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii)
    {
        var cells = new int[grid.Width, grid.Height];
        for (var x = 0; x < grid.Width; x++)
            for (var y = 0; y < grid.Height; y++)
            {
                var bestOwner = -1;
                var bestScore = 0f;
                var point = grid.Center(x, y);
                foreach (var anchor in anchors)
                {
                    var score = radii[(anchor.CivilizationId, anchor.SystemId)] - Vector2.Distance(point, anchor.Position);
                    if (score > bestScore || score == bestScore && score > 0 && (bestOwner < 0 || anchor.CivilizationId < bestOwner))
                    {
                        bestScore = score;
                        bestOwner = anchor.CivilizationId;
                    }
                }
                cells[x, y] = bestOwner;
            }
        return cells;
    }

    private static void PreserveVisibleOwners(TerritoryGrid grid, int[,] cells, IReadOnlyList<StrategicTerritoryAnchor> anchors)
    {
        var counts = new Dictionary<int, int>();
        for (var x = 0; x < grid.Width; x++)
            for (var y = 0; y < grid.Height; y++)
            {
                var owner = cells[x, y];
                if (owner >= 0) counts[owner] = counts.GetValueOrDefault(owner) + 1;
            }

        foreach (var group in anchors.GroupBy(anchor => anchor.CivilizationId).OrderBy(group => group.Key))
        {
            if (counts.GetValueOrDefault(group.Key) > 0) continue;

            var bestX = -1;
            var bestY = -1;
            var bestDistance = float.PositiveInfinity;
            for (var x = 0; x < grid.Width; x++)
                for (var y = 0; y < grid.Height; y++)
                {
                    var previousOwner = cells[x, y];
                    if (previousOwner >= 0 && counts.GetValueOrDefault(previousOwner) <= 1) continue;

                    var center = grid.Center(x, y);
                    var distance = group.Min(anchor => Vector2.DistanceSquared(center, anchor.Position));
                    if (distance >= bestDistance) continue;
                    bestDistance = distance;
                    bestX = x;
                    bestY = y;
                }

            if (bestX < 0) continue;
            var displacedOwner = cells[bestX, bestY];
            if (displacedOwner >= 0) counts[displacedOwner]--;
            cells[bestX, bestY] = group.Key;
            counts[group.Key] = 1;
        }
    }
    private static float Radius(StrategicTerritoryAnchor anchor, IReadOnlyList<StrategicTerritoryAnchor> anchors)
    {
        var nearestFriendly = float.PositiveInfinity;
        foreach (var other in anchors)
            if (other != anchor && other.CivilizationId == anchor.CivilizationId)
                nearestFriendly = Math.Min(nearestFriendly, Vector2.Distance(anchor.Position, other.Position));

        // Overlapping same-owner fields make connected settlements one patch; each cell has one winning owner, clipping opposing territory.
        return Math.Clamp(float.IsFinite(nearestFriendly) ? Math.Max(48f, nearestFriendly * .58f) : 58f, 42f, 118f);
    }

    private static IReadOnlyList<StrategicTerritoryRegion> Regions(
        TerritoryGrid grid,
        int[,] cells,
        IReadOnlyList<StrategicTerritoryAnchor> anchors,
        IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii,
        IReadOnlyDictionary<int, CivilizationState> civs)
    {
        var result = new List<StrategicTerritoryRegion>();
        foreach (var owner in anchors.Select(anchor => anchor.CivilizationId).Distinct().OrderBy(owner => owner))
        {
            var owned = anchors.Where(anchor => anchor.CivilizationId == owner).ToArray();
            var occupiedRuns = Runs(grid, cells, owner);
            if (occupiedRuns.Count == 0) continue;
            var fill = SmoothFill(grid, anchors, radii, owner);
            var largest = occupiedRuns.OrderByDescending(run => run.Size.X * run.Size.Y).First();
            var label = largest.Position + largest.Size * .5f;
            result.Add(new(owner, civs[owner].Name, owned, label,
                fill.Runs, fill.Polygons, fill.Contours));
        }
        return result;
    }

    private static TerritoryFillGeometry SmoothFill(
        TerritoryGrid grid,
        IReadOnlyList<StrategicTerritoryAnchor> anchors,
        IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii,
        int owner)
    {
        // Interpolate the same observer-safe influence field used by the cell oracle. Fully
        // interior cells stay merged into cheap runs; only mixed boundary cells emit polygons.
        // A center sample keeps small circular holdings and enclosed rivals from disappearing.
        var values = new Dictionary<GridPoint, float>();
        float Value(GridPoint point)
        {
            if (!values.TryGetValue(point, out var value))
                values[point] = value = Dominance(grid.Node(point), owner, anchors, radii);
            return value;
        }

        var full = new bool[grid.Width, grid.Height];
        var polygons = new List<StrategicTerritoryFillPolygon>();
        var boundary = new List<FieldSegment>();
        // Two field cells beyond the 105-unit grid padding close even the maximum 118-unit
        // influence radius at an extreme catalog coordinate. This only completes presentation
        // contours; the bounded ownership grid, fog projection, and simulation stay unchanged.
        for (var x = -2; x <= grid.Width + 1; x++)
            for (var y = -2; y <= grid.Height + 1; y++)
            {
                var corners = new[] { new GridPoint(x, y), new(x + 1, y), new(x + 1, y + 1), new(x, y + 1) };
                var points = new[]
                {
                    grid.Node(corners[0]), grid.Node(corners[1]), grid.Node(corners[2]), grid.Node(corners[3]),
                };
                var samples = corners.Select(Value).ToArray();
                var center = (points[0] + points[2]) * .5f;
                var centerValue = Dominance(center, owner, anchors, radii);
                if (centerValue > 0f && samples.All(value => value > 0f))
                {
                    if (x >= 0 && y >= 0 && x < grid.Width && y < grid.Height) full[x, y] = true;
                    else polygons.Add(new(points));
                    continue;
                }

                for (var side = 0; side < 4; side++)
                {
                    var next = (side + 1) % 4;
                    var trianglePoints = new[] { points[side], points[next], center };
                    var triangleValues = new[] { samples[side], samples[next], centerValue };
                    var clipped = ClipPositiveTriangle(trianglePoints, triangleValues);
                    if (clipped.Count >= 3)
                        polygons.Add(new(clipped));
                    var crossings = TriangleCrossings(trianglePoints, triangleValues);
                    if (crossings.Count == 2 && Vector2.DistanceSquared(crossings[0], crossings[1]) > .0001f)
                        boundary.Add(new(crossings[0], crossings[1]));
                }
            }
        return new(Runs(grid, full), polygons, StitchContours(boundary));
    }

    private static float Dominance(
        Vector2 point,
        int owner,
        IReadOnlyList<StrategicTerritoryAnchor> anchors,
        IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii)
    {
        var own = float.NegativeInfinity;
        var rival = 0f;
        foreach (var anchor in anchors)
        {
            var influence = radii[(anchor.CivilizationId, anchor.SystemId)] - Vector2.Distance(point, anchor.Position);
            if (anchor.CivilizationId == owner) own = Math.Max(own, influence);
            else rival = Math.Max(rival, influence);
        }
        return own - rival;
    }

    private static IReadOnlyList<Vector2> ClipPositiveTriangle(Vector2[] points, float[] values)
    {
        var input = Enumerable.Range(0, 3).Select(index => new FieldVertex(points[index], values[index])).ToList();
        var output = new List<FieldVertex>(4);
        for (var index = 0; index < input.Count; index++)
        {
            var from = input[index];
            var to = input[(index + 1) % input.Count];
            var fromInside = from.Value > 0f;
            var toInside = to.Value > 0f;
            if (fromInside) output.Add(from);
            if (fromInside == toInside) continue;
            var amount = from.Value / (from.Value - to.Value);
            output.Add(new(Vector2.Lerp(from.Point, to.Point, amount), 0f));
        }
        return NormalizeFillPolygon(output.Select(vertex => vertex.Point));
    }

    private static IReadOnlyList<Vector2> NormalizeFillPolygon(IEnumerable<Vector2> source)
    {
        // A field sample can land exactly on zero. Sutherland-Hodgman then reaches that
        // vertex from both adjacent edges and emits it twice. Godot's polygon triangulator
        // rejects the resulting zero-length edge, so keep the same clipped area while
        // canonicalizing duplicate vertices before the geometry reaches the renderer.
        const float duplicateDistanceSquared = .00000001f;
        var points = new List<Vector2>(4);
        foreach (var point in source)
            if (points.Count == 0 || Vector2.DistanceSquared(points[^1], point) > duplicateDistanceSquared)
                points.Add(point);
        if (points.Count > 1 && Vector2.DistanceSquared(points[0], points[^1]) <= duplicateDistanceSquared)
            points.RemoveAt(points.Count - 1);

        if (points.Count < 3) return Array.Empty<Vector2>();
        double twiceArea = 0;
        for (var index = 0; index < points.Count; index++)
        {
            var next = points[(index + 1) % points.Count];
            twiceArea += (double)points[index].X * next.Y - (double)next.X * points[index].Y;
        }
        return Math.Abs(twiceArea) > .000001 ? points.ToArray() : Array.Empty<Vector2>();
    }

    private static IReadOnlyList<Vector2> TriangleCrossings(Vector2[] points, float[] values)
    {
        var result = new List<Vector2>(2);
        for (var index = 0; index < 3; index++)
        {
            var next = (index + 1) % 3;
            if ((values[index] > 0f) == (values[next] > 0f)) continue;
            result.Add(CanonicalIntersection(points[index], values[index], points[next], values[next]));
        }
        return result;
    }

    private static Vector2 CanonicalIntersection(Vector2 first, float firstValue, Vector2 second, float secondValue)
    {
        // Adjacent triangles traverse their shared edge in opposite directions. Always doing
        // the interpolation from the same endpoint produces an identical float at large galaxy
        // coordinates, so a continuous boundary cannot split across neighboring quantized keys.
        if (first.X > second.X || first.X == second.X && first.Y > second.Y)
            (first, firstValue, second, secondValue) = (second, secondValue, first, firstValue);
        return Vector2.Lerp(first, second, firstValue / (firstValue - secondValue));
    }

    private static IReadOnlyList<IReadOnlyList<Vector2>> StitchContours(IReadOnlyList<FieldSegment> segments)
    {
        static ContourPointKey Key(Vector2 point) => new((int)MathF.Round(point.X * 1_000f), (int)MathF.Round(point.Y * 1_000f));
        var byEnd = new Dictionary<ContourPointKey, List<int>>();
        void Add(ContourPointKey key, int index)
        {
            if (!byEnd.TryGetValue(key, out var indices)) byEnd[key] = indices = new();
            indices.Add(index);
        }
        for (var index = 0; index < segments.Count; index++)
        {
            Add(Key(segments[index].A), index);
            Add(Key(segments[index].B), index);
        }

        var remaining = Enumerable.Range(0, segments.Count).ToHashSet();
        var contours = new List<IReadOnlyList<Vector2>>();
        while (remaining.Count > 0)
        {
            var firstIndex = remaining.Min();
            remaining.Remove(firstIndex);
            var first = segments[firstIndex];
            var start = Key(first.A); var current = Key(first.B);
            var points = new List<Vector2> { first.A, first.B };
            while (current != start)
            {
                var nextIndex = byEnd[current].FirstOrDefault(remaining.Contains, -1);
                if (nextIndex < 0) break;
                remaining.Remove(nextIndex);
                var next = segments[nextIndex];
                var nextPoint = Key(next.A) == current ? next.B : next.A;
                points.Add(nextPoint);
                current = Key(nextPoint);
            }
            if (current == start && points.Count >= 4)
            {
                points.RemoveAt(points.Count - 1);
                contours.Add(points);
            }
        }
        return contours;
    }

    private static IReadOnlyList<StrategicTerritoryFillRun> Runs(TerritoryGrid grid, bool[,] cells)
    {
        var result = new List<StrategicTerritoryFillRun>();
        for (var y = 0; y < grid.Height; y++)
        {
            var start = -1;
            for (var x = 0; x <= grid.Width; x++)
            {
                var match = x < grid.Width && cells[x, y];
                if (match && start < 0) start = x;
                if (!match && start >= 0)
                {
                    result.Add(grid.Run(start, y, x - start));
                    start = -1;
                }
            }
        }
        return result;
    }

    private static int[,] BuildFogCells(TerritoryGrid grid, IReadOnlyList<StarSystemState> systems,
        IReadOnlyDictionary<int, Vector2> positions, IReadOnlySet<int> unknown)
    {
        var cells = new int[grid.Width, grid.Height];
        for (var x = 0; x < grid.Width; x++)
            for (var y = 0; y < grid.Height; y++)
            {
                var point = grid.Center(x, y);
                var nearest = systems[0];
                var bestDistance = Vector2.DistanceSquared(point, positions[nearest.Id]);
                for (var index = 1; index < systems.Count; index++)
                {
                    var distance = Vector2.DistanceSquared(point, positions[systems[index].Id]);
                    if (distance >= bestDistance) continue;
                    bestDistance = distance;
                    nearest = systems[index];
                }
                cells[x, y] = unknown.Contains(nearest.Id) ? 1 : 0;
            }
        return cells;
    }

    private static StrategicFogMask BuildFogMask(TerritoryGrid grid, int[,] cells)
    {
        // One cached continuous veil replaces hundreds of independently rasterized cell
        // rectangles. Padding closes the outer edge; the separable filter softens the
        // exploration frontier without changing the observer's authoritative knowledge.
        const int padding = 8;
        var width = grid.Width + padding * 2;
        var height = grid.Height + padding * 2;
        var source = new float[width * height];
        for (var y = 0; y < grid.Height; y++)
        for (var x = 0; x < grid.Width; x++)
            source[(y + padding) * width + x + padding] = cells[x, y];
        var horizontal = new float[source.Length];
        var alpha = new byte[source.Length];
        int[] weights = [1, 6, 15, 20, 15, 6, 1];
        for (var y = 3; y < height - 3; y++)
        for (var x = 3; x < width - 3; x++)
        {
            float value = 0;
            for (var offset = -3; offset <= 3; offset++)
                value += source[y * width + x + offset] * weights[offset + 3];
            horizontal[y * width + x] = value / 64f;
        }
        for (var y = 3; y < height - 3; y++)
        for (var x = 3; x < width - 3; x++)
        {
            float value = 0;
            for (var offset = -3; offset <= 3; offset++)
                value += horizontal[(y + offset) * width + x] * weights[offset + 3];
            alpha[y * width + x] = (byte)Math.Clamp(MathF.Round(value * 255f / 64f), 0, 255);
        }
        return new(grid.Origin - new Vector2(padding * grid.CellSize),
            new Vector2(width, height) * grid.CellSize, width, height, alpha);
    }

    private static IReadOnlyList<StrategicTerritoryFillRun> Runs(TerritoryGrid grid, int[,] cells, int owner)
    {
        var result = new List<StrategicTerritoryFillRun>();
        for (var y = 0; y < grid.Height; y++)
        {
            var start = -1;
            for (var x = 0; x <= grid.Width; x++)
            {
                var match = x < grid.Width && cells[x, y] == owner;
                if (match && start < 0) start = x;
                if (!match && start >= 0)
                {
                    result.Add(grid.Run(start, y, x - start));
                    start = -1;
                }
            }
        }
        return result;
    }
    private static IReadOnlyList<IReadOnlyList<Vector2>> Contours(TerritoryGrid grid, int[,] cells, int owner)
    {
        var edges = new Dictionary<GridPoint, List<GridPoint>>();
        void Add(GridPoint from, GridPoint to)
        {
            if (!edges.TryGetValue(from, out var destinations)) edges[from] = destinations = new();
            destinations.Add(to);
        }
        bool Own(int x, int y) => x >= 0 && y >= 0 && x < grid.Width && y < grid.Height && cells[x, y] == owner;

        for (var x = 0; x < grid.Width; x++)
            for (var y = 0; y < grid.Height; y++)
            {
                if (!Own(x, y)) continue;
                if (!Own(x, y - 1)) Add(new(x, y), new(x + 1, y));
                if (!Own(x + 1, y)) Add(new(x + 1, y), new(x + 1, y + 1));
                if (!Own(x, y + 1)) Add(new(x + 1, y + 1), new(x, y + 1));
                if (!Own(x - 1, y)) Add(new(x, y + 1), new(x, y));
            }

        var result = new List<IReadOnlyList<Vector2>>();
        while (edges.Count > 0)
        {
            var first = edges.Keys.OrderBy(point => point.Y).ThenBy(point => point.X).First();
            var current = first;
            var loop = new List<Vector2>();
            do
            {
                loop.Add(grid.Node(current));
                var next = edges[current][0];
                edges[current].RemoveAt(0);
                if (edges[current].Count == 0) edges.Remove(current);
                current = next;
            } while (current != first && edges.ContainsKey(current));
            if (loop.Count >= 3) result.Add(Smooth(loop));
        }
        return result;
    }

    private static IReadOnlyList<Vector2> Smooth(IReadOnlyList<Vector2> points)
    {
        // Round each preserved contour corner with a short quadratic arc. This changes only
        // presentation geometry; ownership cells, disconnected loops, and fog inputs remain
        // exactly those produced by the observer-safe projection above.
        var result = new List<Vector2>(points.Count * 3);
        for (var index = 0; index < points.Count; index++)
        {
            var previous = points[(index + points.Count - 1) % points.Count];
            var current = points[index];
            var next = points[(index + 1) % points.Count];
            var entry = Vector2.Lerp(current, previous, .28f);
            var exit = Vector2.Lerp(current, next, .28f);
            result.Add(entry);
            result.Add(Vector2.Lerp(Vector2.Lerp(entry, current, .5f), Vector2.Lerp(current, exit, .5f), .5f));
            result.Add(exit);
        }
        return result;
    }

    private readonly record struct GridPoint(int X, int Y);

    private readonly record struct TerritoryGrid(Vector2 Origin, int Width, int Height, float CellSize)
    {
        public static TerritoryGrid Create(IEnumerable<Vector2> positions)
        {
            using var iterator = positions.GetEnumerator();
            if (!iterator.MoveNext()) throw new ArgumentException("Territory projection needs at least one system.", nameof(positions));
            var min = iterator.Current;
            var max = min;
            while (iterator.MoveNext())
            {
                min = Vector2.Min(min, iterator.Current);
                max = Vector2.Max(max, iterator.Current);
            }

            const float minimumCell = 12f;
            const float margin = 105f;
            const float maximumCellsPerAxis = 160f;
            var span = max - min + new Vector2(margin * 2);
            var cell = Math.Max(minimumCell, Math.Max(span.X, span.Y) / maximumCellsPerAxis);
            return new(
                min - new Vector2(margin),
                Math.Max(1, (int)Math.Ceiling(span.X / cell)),
                Math.Max(1, (int)Math.Ceiling(span.Y / cell)),
                cell);
        }

        public Vector2 Center(int x, int y) => Origin + new Vector2((x + .5f) * CellSize, (y + .5f) * CellSize);
        public Vector2 Node(GridPoint point) => Origin + new Vector2(point.X * CellSize, point.Y * CellSize);
        public StrategicTerritoryFillRun Run(int x, int y, int width) => new(
            Origin + new Vector2(x * CellSize, y * CellSize),
            new Vector2(width * CellSize, CellSize));
    }
}

public enum StrategicTerritoryAnchorKind { Home, Settlement }
public sealed record StrategicTerritoryAnchor(int CivilizationId, int SystemId, Vector2 Position, StrategicTerritoryAnchorKind Kind);
public sealed record StrategicTerritoryFillRun(Vector2 Position, Vector2 Size);
public sealed record StrategicTerritoryFillPolygon(IReadOnlyList<Vector2> Points);
public sealed record StrategicFogMask(Vector2 Position, Vector2 Size, int Width, int Height, byte[] Alpha);
public sealed record StrategicTerritoryRegion(int CivilizationId, string CivilizationName, IReadOnlyList<StrategicTerritoryAnchor> Anchors, Vector2 LabelPosition, IReadOnlyList<StrategicTerritoryFillRun> FillRuns, IReadOnlyList<StrategicTerritoryFillPolygon> FillPolygons, IReadOnlyList<IReadOnlyList<Vector2>> Contours);
public sealed record StrategicTerritoryClaimOutline(int CivilizationId, int SystemId, Vector2 Position, float Radius);
internal sealed record TerritoryFillGeometry(
    IReadOnlyList<StrategicTerritoryFillRun> Runs,
    IReadOnlyList<StrategicTerritoryFillPolygon> Polygons,
    IReadOnlyList<IReadOnlyList<Vector2>> Contours);
internal readonly record struct FieldVertex(Vector2 Point, float Value);
internal readonly record struct FieldSegment(Vector2 A, Vector2 B);
internal readonly record struct ContourPointKey(int X, int Y);
