using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;
using Game.Simulation.Territory;

namespace Game.Presentation;

/// <summary>Cached observer-safe influence contours. Claims stay separate from control.</summary>
public sealed class StrategicTerritoryProjection
{
    private sealed record CachedProjection(int Fingerprint, StrategicTerritoryProjection Projection);
    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<GalaxyState, Dictionary<int, CachedProjection>> Projections = new();
    public IReadOnlyList<StrategicTerritoryRegion> Territories { get; }
    public IReadOnlyList<StrategicTerritoryClaimOutline> Claims { get; }
    public IReadOnlyList<StrategicTerritoryFillRun> FogRuns { get; }
    public IReadOnlyList<IReadOnlyList<Vector2>> FogContours { get; }
    public StrategicFogMask FogMask { get; }
    public IReadOnlySet<int> UnexploredSystemIds { get; }
    public IReadOnlyList<StrategicTerritoryContestedSystem> ContestedSystems { get; }
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
        int gridCellCount,
        IReadOnlyList<StrategicTerritoryContestedSystem> contestedSystems)
    {
        Territories = territories;
        Claims = claims;
        FogRuns = fogRuns;
        FogContours = fogContours;
        FogMask = fogMask;
        UnexploredSystemIds = unexplored;
        UnownedCellCount = unownedCellCount;
        GridCellCount = gridCellCount;
        ContestedSystems = contestedSystems;
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
        var territorialSnapshot = TerritorialRuntime.Peek(galaxy);
        var anchors = new Dictionary<(int, int), StrategicTerritoryAnchor>();

        bool Visible(int civilizationId, int systemId) => TerritorialObservationMemory.CanObserve(galaxy, observerId, civilizationId, systemId);

        void Add(int civilizationId, int systemId, StrategicTerritoryAnchorKind kind)
        {
            if (!systems.TryGetValue(systemId, out var star) || !Visible(civilizationId, systemId)) return;
            var key = (civilizationId, systemId);
            if (!anchors.TryGetValue(key, out var previous) || kind < previous.Kind)
                anchors[key] = new(civilizationId, systemId, positions[star.Id], kind);
        }

        // A player always has direct knowledge of its represented, physical sources. This is
        // not a natal marker: a source appears and disappears with its authoritative colony.
        foreach (var colony in galaxy.Colonies.Where(colony => colony.CivilizationId == observerId && colony.PopulationMillions > 0))
            Add(observerId, colony.SystemId, StrategicTerritoryAnchorKind.Control);

        // Foreign control is current only when it is legitimately visible. If its live owner
        // becomes hidden, retain only that system's prior reported owner; hidden activity in one
        // place must never freeze unrelated player expansion or source loss elsewhere.
        foreach (var territory in territorialSnapshot?.Systems.Values ?? Enumerable.Empty<SystemTerritory>())
        {
            var owner = territory.Status is TerritorialControlStatus.Controlled or TerritorialControlStatus.Dominant
                ? territory.ControllerId ?? territory.Civilizations.FirstOrDefault()?.CivilizationId : null;
            var remembered = TerritorialObservationMemory.Recall(galaxy, observerId, territory.SystemId);
            var unobservedLocalInfluence = territory.Civilizations.Any(contender => contender.Political > 0 && !Visible(contender.CivilizationId, territory.SystemId));
            if (owner == observerId)
            {
                // Project genuinely controlled empty systems too, once surveyed. Owning a
                // station or projecting control never requires an inhabited planet.
                // Unvisited catalogue points remain behind exploration fog.
                TerritorialObservationMemory.Forget(galaxy, observerId, territory.SystemId);
                if (galaxy.Knowledge.IsSystemFullySurveyed(observerId, territory.SystemId))
                    Add(observerId, territory.SystemId, StrategicTerritoryAnchorKind.Control);
                continue;
            }
            if (owner is null && remembered is not null && !unobservedLocalInfluence)
                TerritorialObservationMemory.Forget(galaxy, observerId, territory.SystemId);
            if (owner is int civilizationId && Visible(civilizationId, territory.SystemId))
            {
                TerritorialObservationMemory.Remember(galaxy, observerId, territory.SystemId, civilizationId, territory.Status);
                Add(civilizationId, territory.SystemId, StrategicTerritoryAnchorKind.Control);
            }
            else if (TerritorialObservationMemory.Recall(galaxy, observerId, territory.SystemId) is { } rememberedAfter &&
                     Visible(rememberedAfter.CivilizationId, rememberedAfter.SystemId))
                Add(rememberedAfter.CivilizationId, rememberedAfter.SystemId, StrategicTerritoryAnchorKind.Control);
        }

        var all = anchors.Values.OrderBy(anchor => anchor.CivilizationId).ThenBy(anchor => anchor.SystemId).ToArray();
        var unknown = galaxy.Systems.Where(system => !galaxy.Knowledge.IsSystemKnown(observerId, system.Id))
            .Select(system => system.Id).ToHashSet();
        var contested = territorialSnapshot?.Systems.Values
            .Where(item => item.Status == TerritorialControlStatus.Contested && systems.ContainsKey(item.SystemId) &&
                item.Civilizations.Where(contender => contender.Share >= TerritorialBalance.ContestedMinimumShare)
                    .All(contender => Visible(contender.CivilizationId, item.SystemId)))
            .Select(item => new StrategicTerritoryContestedSystem(item.SystemId, positions[item.SystemId]))
            .Where(item => galaxy.Knowledge.IsSystemFullySurveyed(observerId, item.SystemId))
            .OrderBy(item => item.SystemId).ToArray() ?? Array.Empty<StrategicTerritoryContestedSystem>();
        // Numerical control reviews need not retessellate an unchanged observer map.
        // Cache only the exact visual inputs; changed ownership, survey, claims, names or
        // positions invalidate this bounded per-campaign observer cache.
        var fingerprint = new HashCode(); fingerprint.Add(coordinateScale);
        foreach (var system in galaxy.Systems) { fingerprint.Add(system.Id); fingerprint.Add(system.Position); fingerprint.Add(system.GalacticDepthLightYears); fingerprint.Add(unknown.Contains(system.Id)); }
        foreach (var anchor in all) { fingerprint.Add(anchor.CivilizationId); fingerprint.Add(anchor.SystemId); }
        foreach (var civilization in galaxy.Civilizations) { fingerprint.Add(civilization.Id); fingerprint.Add(civilization.Name); }
        foreach (var claim in observerClaims ?? Array.Empty<TerritorialClaimSnapshot>())
        { fingerprint.Add(claim.ClaimantCivilizationId); fingerprint.Add(claim.SystemId); fingerprint.Add(claim.Active); fingerprint.Add(Visible(claim.ClaimantCivilizationId, claim.SystemId)); }
        foreach (var item in contested) fingerprint.Add(item.SystemId);
        var key = fingerprint.ToHashCode();
        var observerCache = Projections.GetValue(galaxy, _ => new());
        if (observerCache.TryGetValue(observerId, out var cached) && cached.Fingerprint == key) return cached.Projection;
        var grid = TerritoryGrid.Create(positions.Values);
        var physicalCoordinates = galaxy.Systems.Any(system => system.GalacticDepthLightYears.HasValue);
        var radii = all.ToDictionary(
            anchor => (anchor.CivilizationId, anchor.SystemId),
            anchor => Math.Max(Radius(anchor, all, coordinateScale, physicalCoordinates), grid.CellSize * .72f));
        var cells = Assign(grid, all, radii);
        PreserveVisibleOwners(grid, cells, all);
        var field = new SmoothTerritoryField(grid, all, radii,
            new Game.Simulation.Exploration.InterstellarLaneNetwork().Build(galaxy.Systems));
        var regions = Regions(grid, cells, all, radii, civilizations, field);
        var claims = new List<StrategicTerritoryClaimOutline>();
        foreach (var claim in observerClaims ?? Array.Empty<TerritorialClaimSnapshot>())
            if (claim.Active
                && systems.TryGetValue(claim.SystemId, out var star)
                && civilizations.ContainsKey(claim.ClaimantCivilizationId)
                && Visible(claim.ClaimantCivilizationId, claim.SystemId))
                claims.Add(new(claim.ClaimantCivilizationId, claim.SystemId, positions[star.Id], Math.Max(28f, grid.CellSize * 1.3f)));

        var unowned = cells.Cast<int>().Count(owner => owner < 0);
        var fogCells = BuildFogCells(grid, galaxy.Systems, positions, unknown);
        var result = new StrategicTerritoryProjection(
            regions,
            claims.OrderBy(x => x.CivilizationId).ThenBy(x => x.SystemId).ToArray(),
            Runs(grid, fogCells, 1),
            Contours(grid, fogCells, 1),
            BuildFogMask(grid, fogCells),
            unknown,
            unowned,
            grid.Width * grid.Height,
            contested);
        observerCache[observerId] = new(key, result);
        return result;
    }

    private static int[,] Assign(TerritoryGrid grid, IReadOnlyList<StrategicTerritoryAnchor> anchors, IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii)
    {
        var cells = new int[grid.Width, grid.Height];
        var scores = new float[grid.Width, grid.Height];
        for (var x = 0; x < grid.Width; x++) for (var y = 0; y < grid.Height; y++) cells[x, y] = -1;
        // Only examine cells inside an anchor's small radius. This preserves the old
        // distance field while avoiding a grid-by-every-system pass in large campaigns.
        foreach (var anchor in anchors)
        {
            var radius = radii[(anchor.CivilizationId, anchor.SystemId)];
            var minimumX = Math.Max(0, (int)Math.Floor((anchor.Position.X - radius - grid.Origin.X) / grid.CellSize));
            var maximumX = Math.Min(grid.Width - 1, (int)Math.Ceiling((anchor.Position.X + radius - grid.Origin.X) / grid.CellSize));
            var minimumY = Math.Max(0, (int)Math.Floor((anchor.Position.Y - radius - grid.Origin.Y) / grid.CellSize));
            var maximumY = Math.Min(grid.Height - 1, (int)Math.Ceiling((anchor.Position.Y + radius - grid.Origin.Y) / grid.CellSize));
            for (var x = minimumX; x <= maximumX; x++)
                for (var y = minimumY; y <= maximumY; y++)
                {
                    var score = radius - Vector2.Distance(grid.Center(x, y), anchor.Position);
                    if (score <= 0 || score < scores[x, y] || score == scores[x, y] && cells[x, y] >= 0 && cells[x, y] < anchor.CivilizationId) continue;
                    scores[x, y] = score;
                    cells[x, y] = anchor.CivilizationId;
                }
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
    private static float Radius(StrategicTerritoryAnchor anchor, IReadOnlyList<StrategicTerritoryAnchor> anchors,
        float coordinateScale, bool physicalCoordinates)
    {
        var nearestFriendly = float.PositiveInfinity;
        foreach (var other in anchors)
            if (other != anchor && other.CivilizationId == anchor.CivilizationId)
                nearestFriendly = Math.Min(nearestFriendly, Vector2.Distance(anchor.Position, other.Position));

        // Overlapping same-owner fields make connected settlements one patch; each cell has one winning owner, clipping opposing territory.
        // Physical catalogue coordinates are enlarged by the camera adapter. The influence
        // footprint must use that same scale; leaving a 48-unit visual radius unscaled made
        // neighboring controlled systems look like isolated islands in the full galaxy.
        var minimum = (physicalCoordinates ? 8f : 48f) * coordinateScale;
        var maximum = (physicalCoordinates ? 18f : 118f) * coordinateScale;
        return Math.Clamp(float.IsFinite(nearestFriendly) ? Math.Max(minimum, nearestFriendly * .58f) : minimum * 1.2f,
            minimum, maximum);
    }

    private static IReadOnlyList<StrategicTerritoryRegion> Regions(
        TerritoryGrid grid,
        int[,] cells,
        IReadOnlyList<StrategicTerritoryAnchor> anchors,
        IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii,
        IReadOnlyDictionary<int, CivilizationState> civs, SmoothTerritoryField field)
    {
        var result = new List<StrategicTerritoryRegion>();
        foreach (var owner in anchors.Select(anchor => anchor.CivilizationId).Distinct().OrderBy(owner => owner))
        {
            var owned = anchors.Where(anchor => anchor.CivilizationId == owner).ToArray();
            var occupiedRuns = Runs(grid, cells, owner);
            if (occupiedRuns.Count == 0) continue;
            var fill = SmoothFill(grid, field, owner);
            var largest = fill.Runs.OrderByDescending(run => run.Size.X * run.Size.Y).FirstOrDefault();
            var label = largest is not null ? largest.Position + largest.Size * .5f : owned[0].Position;
            result.Add(new(owner, civs[owner].Name, owned, label,
                fill.Runs, fill.Polygons, fill.Contours));
        }
        return result;
    }

    private static TerritoryFillGeometry SmoothFill(
        TerritoryGrid grid,
        SmoothTerritoryField field,
        int owner)
    {
        // Interpolate the same observer-safe influence field used by the cell oracle. Fully
        // interior cells stay merged into cheap runs; only mixed boundary cells emit polygons.
        // A center sample keeps small circular holdings and enclosed rivals from disappearing.
        var values = new Dictionary<GridPoint, float>();
        float Value(GridPoint point)
        {
            if (!values.TryGetValue(point, out var value))
                values[point] = value = field.Value(owner, point.X, point.Y, false);
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
                var centerValue = field.Value(owner, x, y, true);
                if (centerValue <= 0f && samples.All(value => value <= 0f)) continue;
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


    /// <summary>Rasterize local analytic circles and lane corridors once per observer snapshot.
    /// Boundary interpolation reads a constant-time field, never all anchors per triangle.</summary>
    private sealed class SmoothTerritoryField
    {
        private readonly Dictionary<int, float[]> _values = new();
        private readonly int _width, _height;
        private readonly float _step;
        private readonly Vector2 _origin;
        public SmoothTerritoryField(TerritoryGrid grid, IReadOnlyList<StrategicTerritoryAnchor> anchors,
            IReadOnlyDictionary<(int CivilizationId, int SystemId), float> radii,
            IReadOnlyList<Game.Simulation.Exploration.InterstellarLane> lanes)
        {
            _width = (grid.Width + 4) * 2 + 1;
            _height = (grid.Height + 4) * 2 + 1;
            _step = grid.CellSize * .5f;
            _origin = grid.Origin - new Vector2(grid.CellSize * 2);
            foreach (var owner in anchors.Select(a => a.CivilizationId).Distinct())
            {
                _values[owner] = new float[_width * _height];
                Array.Fill(_values[owner], -1f);
            }
            foreach (var anchor in anchors)
                Stamp(anchor.CivilizationId, anchor.Position, anchor.Position,
                    radii[(anchor.CivilizationId, anchor.SystemId)], 1f);
            var bySystem = anchors.GroupBy(a => a.SystemId).ToDictionary(g => g.Key, g => g.ToArray());
            foreach (var lane in lanes)
            {
                if (!bySystem.TryGetValue(lane.FirstSystemId, out var first) ||
                    !bySystem.TryGetValue(lane.SecondSystemId, out var second)) continue;
                foreach (var a in first)
                foreach (var b in second)
                {
                    if (a.CivilizationId != b.CivilizationId) continue;
                    var radius = Math.Min(radii[(a.CivilizationId, a.SystemId)], radii[(b.CivilizationId, b.SystemId)]);
                    // Join local holdings along genuine lanes; never draw a claim across an
                    // unsupported inter-arm gulf or swallow a rival anchor.
                    if (Vector2.Distance(a.Position, b.Position) > radius * 3f) continue;
                    Stamp(a.CivilizationId, a.Position, b.Position, radius * .72f, .72f);
                }
            }
            var owners = _values.Keys.ToArray();
            var strongest = new float[_width * _height];
            var secondStrongest = new float[strongest.Length];
            var winner = new int[strongest.Length];
            Array.Fill(winner, -1);
            foreach (var owner in owners)
                for (var i = 0; i < strongest.Length; i++)
                {
                    var value = _values[owner][i];
                    if (value > strongest[i]) { secondStrongest[i] = strongest[i]; strongest[i] = value; winner[i] = owner; }
                    else if (value > secondStrongest[i]) secondStrongest[i] = value;
                }
            foreach (var owner in owners)
                for (var i = 0; i < strongest.Length; i++)
                    _values[owner][i] -= winner[i] == owner ? secondStrongest[i] : strongest[i];
        }
        private void Stamp(int owner, Vector2 start, Vector2 end, float radius, float strength)
        {
            var padding = radius + _step * 4;
            var min = Vector2.Min(start, end) - new Vector2(padding);
            var max = Vector2.Max(start, end) + new Vector2(padding);
            var minX = Math.Clamp((int)MathF.Floor((min.X - _origin.X) / _step), 0, _width - 1);
            var minY = Math.Clamp((int)MathF.Floor((min.Y - _origin.Y) / _step), 0, _height - 1);
            var maxX = Math.Clamp((int)MathF.Ceiling((max.X - _origin.X) / _step), 0, _width - 1);
            var maxY = Math.Clamp((int)MathF.Ceiling((max.Y - _origin.Y) / _step), 0, _height - 1);
            var vector = end - start; var length = vector.LengthSquared(); var values = _values[owner];
            for (var y = minY; y <= maxY; y++)
            for (var x = minX; x <= maxX; x++)
            {
                var point = _origin + new Vector2(x * _step, y * _step);
                var fraction = length > .000001f ? Math.Clamp(Vector2.Dot(point - start, vector) / length, 0, 1) : 0;
                var value = strength * (1f - Vector2.Distance(point, start + vector * fraction) / radius);
                var i = y * _width + x;
                if (value > values[i]) values[i] = value;
            }
        }
        public float Value(int owner, int x, int y, bool center)
        {
            var column = (x + 2) * 2 + (center ? 1 : 0);
            var row = (y + 2) * 2 + (center ? 1 : 0);
            return column >= 0 && column < _width && row >= 0 && row < _height
                ? _values[owner][row * _width + column] : -1;
        }
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
        var nearest = new NearestSystemLookup(systems, positions);
        for (var x = 0; x < grid.Width; x++)
            for (var y = 0; y < grid.Height; y++)
            {
                var systemIndex = nearest.Find(grid.Center(x, y));
                cells[x, y] = unknown.Contains(systems[systemIndex].Id) ? 1 : 0;
            }
        return cells;
    }

    // The fog oracle is nearest-system ownership, not an approximation.  The tree keeps the
    // former catalogue-index tie rule so equal-distance masks are byte-for-byte stable.
    private sealed class NearestSystemLookup
    {
        private readonly Node? _root;

        public NearestSystemLookup(IReadOnlyList<StarSystemState> systems, IReadOnlyDictionary<int, Vector2> positions)
        {
            if (systems.Count == 0) throw new ArgumentException("Fog projection needs at least one system.", nameof(systems));
            var entries = systems.Select((system, index) => new Entry(positions[system.Id], index)).ToArray();
            _root = Build(entries, 0, entries.Length, 0);
        }

        public int Find(Vector2 point)
        {
            var bestIndex = int.MaxValue;
            var bestDistance = float.PositiveInfinity;
            Search(_root, point, ref bestIndex, ref bestDistance);
            return bestIndex;
        }

        private static Node? Build(Entry[] entries, int start, int end, int depth)
        {
            if (start >= end) return null;
            var axis = depth & 1;
            Array.Sort(entries, start, end - start, Comparer<Entry>.Create((left, right) =>
            {
                var comparison = axis == 0 ? left.Position.X.CompareTo(right.Position.X) : left.Position.Y.CompareTo(right.Position.Y);
                return comparison != 0 ? comparison : left.Index.CompareTo(right.Index);
            }));
            var middle = start + (end - start) / 2;
            return new(entries[middle], axis, Build(entries, start, middle, depth + 1), Build(entries, middle + 1, end, depth + 1));
        }

        private static void Search(Node? node, Vector2 point, ref int bestIndex, ref float bestDistance)
        {
            if (node is null) return;
            var distance = Vector2.DistanceSquared(point, node.Entry.Position);
            if (distance < bestDistance || distance == bestDistance && node.Entry.Index < bestIndex)
            {
                bestDistance = distance;
                bestIndex = node.Entry.Index;
            }
            var delta = node.Axis == 0 ? point.X - node.Entry.Position.X : point.Y - node.Entry.Position.Y;
            var first = delta <= 0 ? node.Left : node.Right;
            var second = delta <= 0 ? node.Right : node.Left;
            Search(first, point, ref bestIndex, ref bestDistance);
            // Equality must visit both halves because the older linear scan picked the first
            // catalogue item at an exact bisector.
            if (delta * delta <= bestDistance) Search(second, point, ref bestIndex, ref bestDistance);
        }

        private readonly record struct Entry(Vector2 Position, int Index);
        private sealed record Node(Entry Entry, int Axis, Node? Left, Node? Right);
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

            const float minimumCell = 3f;
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

public enum StrategicTerritoryAnchorKind { Control }
public sealed record StrategicTerritoryAnchor(int CivilizationId, int SystemId, Vector2 Position, StrategicTerritoryAnchorKind Kind);
public sealed record StrategicTerritoryFillRun(Vector2 Position, Vector2 Size);
public sealed record StrategicTerritoryFillPolygon(IReadOnlyList<Vector2> Points);
public sealed record StrategicFogMask(Vector2 Position, Vector2 Size, int Width, int Height, byte[] Alpha);
public sealed record StrategicTerritoryRegion(int CivilizationId, string CivilizationName, IReadOnlyList<StrategicTerritoryAnchor> Anchors, Vector2 LabelPosition, IReadOnlyList<StrategicTerritoryFillRun> FillRuns, IReadOnlyList<StrategicTerritoryFillPolygon> FillPolygons, IReadOnlyList<IReadOnlyList<Vector2>> Contours);
public sealed record StrategicTerritoryClaimOutline(int CivilizationId, int SystemId, Vector2 Position, float Radius);
public sealed record StrategicTerritoryContestedSystem(int SystemId, Vector2 Position);
internal sealed record TerritoryFillGeometry(
    IReadOnlyList<StrategicTerritoryFillRun> Runs,
    IReadOnlyList<StrategicTerritoryFillPolygon> Polygons,
    IReadOnlyList<IReadOnlyList<Vector2>> Contours);
internal readonly record struct FieldVertex(Vector2 Point, float Value);
internal readonly record struct FieldSegment(Vector2 A, Vector2 B);
internal readonly record struct ContourPointKey(int X, int Y);
