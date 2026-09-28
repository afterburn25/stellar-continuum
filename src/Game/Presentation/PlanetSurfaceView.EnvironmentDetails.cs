using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Construction;
using Godot;

namespace Game.Presentation;

public partial class PlanetSurfaceView
{
    private Node3D? _environmentDetails;
    private string _environmentDetailsKey = string.Empty;

    public int EnvironmentDetailCount => _environmentDetails?.GetChildCount() ?? 0;
    public int SurfaceTrafficCount => _environmentDetails?.GetChildren()
        .Count(node => node.Name.ToString().StartsWith("GroundTraffic", StringComparison.Ordinal)) ?? 0;

    private void RefreshEnvironmentDetails(UiSurfaceSnapshot snapshot)
    {
        var layout = string.Join(';', snapshot.Buildings.OrderBy(building => building.Id)
            .Select(building => $"{building.Id}:{building.TypeId}:{building.X:0.0}:{building.Z:0.0}:{building.Complete}"));
        var populationBand = Math.Clamp((int)Math.Floor(Math.Log10(Math.Max(1, snapshot.PopulationMillions))), 0, 5);
        var key = $"{snapshot.ColonyId}:{snapshot.BodyId}:{snapshot.SurfaceVisualClass}:{VisualStyle.SpeciesId}:{populationBand}:{layout}";
        if (_environmentDetailsKey == key) return;
        _environmentDetailsKey = key;
        if (_environmentDetails is not null)
        {
            _world.RemoveChild(_environmentDetails);
            _environmentDetails.QueueFree();
        }
        _environmentDetails = new SurfaceEnvironmentDetails(snapshot, VisualStyle);
        _world.AddChild(_environmentDetails);
    }
}

/// <summary>Bounded presentation-only surface infrastructure. It reads colony state to avoid
/// construction footprints but never participates in placement, collision, saves, or economy.</summary>
internal partial class SurfaceEnvironmentDetails : Node3D
{
    private readonly List<(Node3D Vehicle, Vector3 Start, Vector3 End, float Phase)> _traffic = new();
    private double _elapsed;

    public SurfaceEnvironmentDetails(UiSurfaceSnapshot snapshot, CivilizationVisualStyle style)
    {
        Name = "SurfaceEnvironmentDetails";
        var road = SurfaceBuildingVisuals.Material("151d21", .89f, .08f);
        var roadEdge = SurfaceBuildingVisuals.Material("59636a", .66f, .48f);
        var concrete = SurfaceBuildingVisuals.Material("4b5659", .86f, .12f);
        var utility = SurfaceBuildingVisuals.Material("303d43", .55f, .72f);
        var accent = new StandardMaterial3D
        {
            AlbedoColor = style.AccentColor.Darkened(.16f), Roughness = .36f, Metallic = .52f,
            EmissionEnabled = true, Emission = style.AccentColor, EmissionEnergyMultiplier = .18f,
        };
        var foliage = SurfaceBuildingVisuals.Material(
            snapshot.SurfaceVisualClass == "reducing" ? "34452d" : "173c2c", .96f);
        var bark = SurfaceBuildingVisuals.Material("332b23", .98f);

        var pad = ChooseClearPoint(snapshot.Buildings, new[]
        {
            new Vector2(172, -116), new Vector2(-178, 124), new Vector2(146, 168), new Vector2(-155, -172),
        }, 24);
        AddLandingPad(pad, concrete, road, accent);

        // Visual access roads use a small deterministic navigation grid. They dogleg around
        // authoritative footprints and are emitted as one terrain-conforming mesh per route.
        var routeStart = new Vector2(76, -44);
        var cityHub = Vector2.Zero;
        AddRoutedPath(snapshot.Buildings, cityHub, routeStart, road, roadEdge);
        AddRoutedPath(snapshot.Buildings, routeStart, pad, road, roadEdge);
        AddRoutedPath(snapshot.Buildings, new Vector2(-82, 65), new Vector2(82, 65), road, roadEdge);
        AddUtilities(snapshot.Buildings, routeStart, pad, utility, accent);
        AddServiceLinks(snapshot.Buildings, cityHub, road, roadEdge, utility, accent);

        var canGrow = snapshot.SurfaceVisualClass is "temperate" or "oceanic" or "reducing";
        var random = new Random(unchecked(snapshot.BodyId * 7919 + snapshot.ColonyId * 104729));
        for (var index = 0; index < (canGrow ? 24 : 10); index++)
        {
            var angle = (float)random.NextDouble() * MathF.Tau;
            var radius = 112 + (float)random.NextDouble() * 245;
            var point = new Vector2(MathF.Cos(angle) * radius, MathF.Sin(angle) * radius);
            if (!IsClear(snapshot.Buildings, point, canGrow ? 7 : 12)) continue;
            if (canGrow) AddTree(point, 4.5f + (float)random.NextDouble() * 5.5f, bark, foliage);
            else AddRock(point, 2.5f + (float)random.NextDouble() * 5, concrete, random);
        }

        var trafficCount = Math.Clamp(1 + snapshot.Buildings.Count / 4, 1, 5);
        if (RouteIsClear(snapshot.Buildings, routeStart, pad, 4.5f))
            for (var index = 0; index < trafficCount; index++)
                AddVehicle(index, trafficCount, routeStart, pad, utility, accent);
    }

    public override void _Process(double delta)
    {
        _elapsed += Math.Min(delta, .1);
        foreach (var (vehicle, start, end, phase) in _traffic)
        {
            var cycle = (float)((_elapsed * .045 + phase) % 1.0);
            var forward = cycle < .5f;
            var t = forward ? cycle * 2 : (1 - cycle) * 2;
            t = t * t * (3 - 2 * t);
            var point = start.Lerp(end, t);
            point.Y = SurfaceConstruction.TerrainHeight(point.X, point.Z) + .65f;
            vehicle.Position = point;
            var direction = forward ? end - start : start - end;
            if (direction.LengthSquared() > .01f) vehicle.LookAt(vehicle.Position + direction, Vector3.Up);
        }
    }

    private void AddLandingPad(Vector2 point, Material concrete, Material road, Material accent)
    {
        var ground = SurfaceConstruction.TerrainHeight(point.X, point.Y);
        SurfaceBuildingVisuals.Cylinder(this, 15.5f, 16.5f, .5f,
            new(point.X, ground + .28f, point.Y), concrete, 48).Name = "CivicLandingPad";
        SurfaceBuildingVisuals.Cylinder(this, 11.5f, 11.5f, .08f,
            new(point.X, ground + .57f, point.Y), road, 48);
        for (var arm = 0; arm < 4; arm++)
        {
            var angle = arm * MathF.Tau / 4;
            var marker = new Vector3(point.X + MathF.Cos(angle) * 10, ground + .68f,
                point.Y + MathF.Sin(angle) * 10);
            SurfaceBuildingVisuals.Box(this, new(4.5f, .08f, .32f), marker, accent).Rotation = new(0, -angle, 0);
        }
    }

    private bool AddRoutedPath(IReadOnlyList<UiSurfaceBuilding> buildings, Vector2 start, Vector2 end,
        Material road, Material edge)
    {
        var path = SurfaceRoadRouting.FindRoute(ToObstacles(buildings), start, end, 4.5f);
        if (path.Count < 2) return false;
        var surface = new SurfaceTool(); surface.Begin(Godot.Mesh.PrimitiveType.Triangles);
        var markings = new SurfaceTool(); markings.Begin(Godot.Mesh.PrimitiveType.Triangles);
        var routeDistance = 0f;
        for (var leg = 0; leg < path.Count - 1; leg++)
        {
            var distance = path[leg].DistanceTo(path[leg + 1]);
            var segments = Math.Max(1, (int)MathF.Ceiling(distance / 8));
            for (var index = 0; index < segments; index++)
            {
                var a = path[leg].Lerp(path[leg + 1], index / (float)segments);
                var b = path[leg].Lerp(path[leg + 1], (index + 1f) / segments);
                AddRoadQuad(surface, a, b, 2.7f, .12f);
                if (((int)(routeDistance / 9f)) % 2 == 0) AddRoadQuad(markings, a.Lerp(b, .3f), a.Lerp(b, .7f), .07f, .19f);
                routeDistance += a.DistanceTo(b);
            }
        }
        SurfaceBuildingVisuals.Mesh(this, surface.Commit(), Vector3.Zero, road).Name = "SurfaceAccessRoad";
        SurfaceBuildingVisuals.Mesh(this, markings.Commit(), Vector3.Zero, edge).Name = "SurfaceRoadMarkings";
        return true;
    }

    private static void AddRoadQuad(SurfaceTool mesh, Vector2 a, Vector2 b, float halfWidth, float lift)
    {
        var direction = (b - a).Normalized();
        if (direction.LengthSquared() < .001f) return;
        var side = new Vector2(-direction.Y, direction.X) * halfWidth;
        var points = new[] { a - side, b - side, b + side, a + side };
        foreach (var index in new[] { 0, 1, 2, 0, 2, 3 })
        {
            var point = points[index];
            mesh.SetNormal(Vector3.Up);
            mesh.AddVertex(new(point.X, SurfaceConstruction.TerrainHeight(point.X, point.Y) + lift, point.Y));
        }
    }

    private void AddUtilities(IReadOnlyList<UiSurfaceBuilding> buildings, Vector2 start, Vector2 end,
        Material utility, Material accent)
    {
        if (!RouteIsClear(buildings, start, end, 7)) return;
        var direction = (end - start).Normalized();
        var side = new Vector2(-direction.Y, direction.X) * 5.2f;
        for (var index = 1; index < 7; index++)
        {
            var point = start.Lerp(end, index / 7f) + side;
            var ground = SurfaceConstruction.TerrainHeight(point.X, point.Y);
            SurfaceBuildingVisuals.Cylinder(this, .10f, .18f, 5.5f,
                new(point.X, ground + 2.8f, point.Y), utility, 8);
            var crossbar = SurfaceBuildingVisuals.Box(this, new(2.7f, .15f, .15f),
                new(point.X, ground + 5.5f, point.Y), utility);
            crossbar.Rotation = new(0, -MathF.Atan2(direction.Y, direction.X), 0);
            SurfaceBuildingVisuals.Sphere(this, .18f,
                new(point.X, ground + 5.75f, point.Y), accent);
        }
    }

    private void AddServiceLinks(IReadOnlyList<UiSurfaceBuilding> buildings, Vector2 hub, Material road,
        Material edge, Material utility, Material accent)
    {
        // These are visual access spurs only. Each terminates outside the real footprint,
        // so it can reinforce a logical service network without changing placement or collision.
        foreach (var building in buildings.Where(building => building.Complete).OrderBy(building => building.Id).Take(32))
        {
            var center = new Vector2(building.X, building.Z);
            var direction = center - hub;
            if (direction.LengthSquared() < .01f) continue;
            direction = direction.Normalized();
            var radius = SurfaceBuildingCatalog.Find(building.TypeId)?.FootprintRadius ?? 15;
            var endpoint = center - direction * (radius + 5.2f);
            var obstacles = buildings.ToArray();
            if (AddRoutedPath(obstacles, hub, endpoint, road, edge))
                AddEntranceApron(endpoint, center - direction * radius * .62f, road, edge);
            if (building.Id % 2 == 0)
                AddUtilities(obstacles, hub, endpoint, utility, accent);
        }
    }

    private void AddEntranceApron(Vector2 outside, Vector2 entrance, Material road, Material edge)
    {
        var surface = new SurfaceTool(); surface.Begin(Godot.Mesh.PrimitiveType.Triangles);
        AddRoadQuad(surface, outside, entrance, 1.65f, .14f);
        SurfaceBuildingVisuals.Mesh(this, surface.Commit(), Vector3.Zero, road).Name = "BuildingEntranceApron";
        var marker = new SurfaceTool(); marker.Begin(Godot.Mesh.PrimitiveType.Triangles);
        AddRoadQuad(marker, outside.Lerp(entrance, .24f), outside.Lerp(entrance, .42f), .08f, .21f);
        SurfaceBuildingVisuals.Mesh(this, marker.Commit(), Vector3.Zero, edge).Name = "BuildingApronMarking";
    }

    private void AddVehicle(int index, int count, Vector2 from, Vector2 to, Material shell, Material accent)
    {
        var vehicle = new Node3D { Name = $"GroundTraffic{index + 1}" };
        AddChild(vehicle);
        SurfaceBuildingVisuals.Box(vehicle, new(1.35f, .58f, 2.8f), Vector3.Zero, shell);
        SurfaceBuildingVisuals.Box(vehicle, new(1.05f, .42f, 1.2f), new(0, .47f, -.15f), SurfaceBuildingVisuals.Glass);
        SurfaceBuildingVisuals.Box(vehicle, new(.18f, .13f, 2.2f), new(0, .08f, 0), accent);
        _traffic.Add((vehicle, new(from.X, 0, from.Y), new(to.X, 0, to.Y), index / (float)count));
    }

    private void AddTree(Vector2 point, float height, Material bark, Material foliage)
    {
        var ground = SurfaceConstruction.TerrainHeight(point.X, point.Y);
        SurfaceBuildingVisuals.Cylinder(this, .25f, .48f, height * .55f,
            new(point.X, ground + height * .275f, point.Y), bark, 8);
        for (var layer = 0; layer < 3; layer++)
            SurfaceBuildingVisuals.Cylinder(this, .15f, height * (.30f - layer * .045f), height * .34f,
                new(point.X, ground + height * (.50f + layer * .17f), point.Y), foliage, 9);
    }

    private void AddRock(Vector2 point, float size, Material stone, Random random)
    {
        var ground = SurfaceConstruction.TerrainHeight(point.X, point.Y);
        var rock = SurfaceBuildingVisuals.Sphere(this, 1, new(point.X, ground + size * .35f, point.Y), stone);
        rock.Scale = new(size, size * .7f, size * .82f);
        rock.RotationDegrees = new(11, (float)random.NextDouble() * 360, 7);
    }

    private static Vector2 ChooseClearPoint(IReadOnlyList<UiSurfaceBuilding> buildings,
        IEnumerable<Vector2> candidates, float margin) =>
        candidates.FirstOrDefault(point => IsClear(buildings, point, margin), new Vector2(172, -116));

    private static bool IsClear(IReadOnlyList<UiSurfaceBuilding> buildings, Vector2 point, float margin) =>
        SurfaceRoadRouting.IsClear(ToObstacles(buildings), point, margin);

    private static bool RouteIsClear(IReadOnlyList<UiSurfaceBuilding> buildings, Vector2 start, Vector2 end, float margin)
        => SurfaceRoadRouting.RouteIsClear(ToObstacles(buildings), start, end, margin);

    private static IReadOnlyList<SurfaceRoadObstacle> ToObstacles(IReadOnlyList<UiSurfaceBuilding> buildings) =>
        buildings.Select(building => new SurfaceRoadObstacle(new(building.X, building.Z),
            SurfaceBuildingCatalog.Find(building.TypeId)?.FootprintRadius ?? 15)).ToArray();
}

public readonly record struct SurfaceRoadObstacle(Vector2 Center, float Radius);

/// <summary>Deterministic bounded geometry helper for presentation-only surface roads.</summary>
public static class SurfaceRoadRouting
{
    private const float RouteGrid = 12f;
    private const int MaxRouteNodes = 18000;

    public static bool IsClear(IReadOnlyList<SurfaceRoadObstacle> obstacles, Vector2 point, float margin) =>
        obstacles.All(obstacle => point.DistanceTo(obstacle.Center) >= obstacle.Radius + margin);

    public static bool RouteIsClear(IReadOnlyList<SurfaceRoadObstacle> obstacles, Vector2 start, Vector2 end, float margin)
    {
        var length = start.DistanceTo(end);
        var samples = Math.Max(2, (int)MathF.Ceiling(length / 5));
        for (var index = 0; index <= samples; index++)
            if (!IsClear(obstacles, start.Lerp(end, index / (float)samples), margin)) return false;
        return true;
    }

    public static IReadOnlyList<Vector2> FindRoute(IReadOnlyList<SurfaceRoadObstacle> obstacles,
        Vector2 start, Vector2 end, float margin)
    {
        if (RouteIsClear(obstacles, start, end, margin)) return new[] { start, end };

        Vector2I Cell(Vector2 point) => new(
            Mathf.RoundToInt(point.X / RouteGrid), Mathf.RoundToInt(point.Y / RouteGrid));
        Vector2 Point(Vector2I cell) => new(cell.X * RouteGrid, cell.Y * RouteGrid);
        Vector2I? Anchor(Vector2 exact)
        {
            var origin = Cell(exact);
            for (var ring = 0; ring <= 4; ring++)
            for (var y = -ring; y <= ring; y++)
            for (var x = -ring; x <= ring; x++)
            {
                if (ring > 0 && Math.Abs(x) != ring && Math.Abs(y) != ring) continue;
                var candidate = origin + new Vector2I(x, y);
                var point = Point(candidate);
                if (IsClear(obstacles, point, margin) && RouteIsClear(obstacles, exact, point, margin)) return candidate;
            }
            return null;
        }
        var startAnchor = Anchor(start); var endAnchor = Anchor(end);
        if (startAnchor is null || endAnchor is null) return Array.Empty<Vector2>();
        var startCell = startAnchor.Value; var endCell = endAnchor.Value;
        var frontier = new PriorityQueue<Vector2I, float>();
        var cameFrom = new Dictionary<Vector2I, Vector2I>();
        var cost = new Dictionary<Vector2I, float> { [startCell] = 0 };
        frontier.Enqueue(startCell, 0);
        var directions = new[]
        {
            new Vector2I(1,0), new Vector2I(0,1), new Vector2I(-1,0), new Vector2I(0,-1),
            new Vector2I(1,1), new Vector2I(-1,1), new Vector2I(-1,-1), new Vector2I(1,-1),
        };
        var visited = 0;
        while (frontier.Count > 0 && visited++ < MaxRouteNodes)
        {
            var current = frontier.Dequeue();
            if (current == endCell) break;
            foreach (var offset in directions)
            {
                var next = current + offset;
                var point = Point(next);
                if (Math.Abs(point.X) > SurfaceConstruction.AreaHalfSize || Math.Abs(point.Y) > SurfaceConstruction.AreaHalfSize ||
                    !IsClear(obstacles, point, margin) || !RouteIsClear(obstacles, Point(current), point, margin)) continue;
                var nextCost = cost[current] + (offset.X == 0 || offset.Y == 0 ? 1f : 1.4142f);
                if (cost.TryGetValue(next, out var oldCost) && nextCost >= oldCost) continue;
                cost[next] = nextCost;
                cameFrom[next] = current;
                frontier.Enqueue(next, nextCost + Point(next).DistanceTo(end) / RouteGrid);
            }
        }
        if (!cost.ContainsKey(endCell)) return Array.Empty<Vector2>();
        var cells = new List<Vector2I> { endCell };
        while (cells[^1] != startCell) cells.Add(cameFrom[cells[^1]]);
        cells.Reverse();
        var candidates = new List<Vector2> { start };
        foreach (var cell in cells)
        {
            var point = Point(cell);
            if (!point.IsEqualApprox(candidates[^1])) candidates.Add(point);
        }
        if (!end.IsEqualApprox(candidates[^1])) candidates.Add(end);

        // Greedily collapse the staircase, but accept a shortcut only when the exact segment,
        // including its exact start/end anchors, preserves obstacle clearance.
        var result = new List<Vector2> { candidates[0] };
        var cursor = 0;
        while (cursor < candidates.Count - 1)
        {
            var next = cursor + 1;
            for (var candidate = candidates.Count - 1; candidate > cursor; candidate--)
                if (RouteIsClear(obstacles, candidates[cursor], candidates[candidate], margin))
                {
                    next = candidate;
                    break;
                }
            if (!RouteIsClear(obstacles, candidates[cursor], candidates[next], margin)) return Array.Empty<Vector2>();
            result.Add(candidates[next]);
            cursor = next;
        }
        return result;
    }
}
