using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Construction;
using Godot;

namespace Game.Presentation;

/// <summary>Demo architecture, intentionally independent of simulation and saved state.</summary>
public static class SurfaceBuildingVisuals
{
    internal static readonly ShaderMaterial Shell = Facade("35444d", "071b2a", 1);
    internal static readonly StandardMaterial3D Metal = Material("18242b", .46f, .82f);
    internal static readonly StandardMaterial3D Bronze = Material("9a7040", .42f, .68f);
    internal static readonly StandardMaterial3D Solar = Material("071d34", .20f, .48f);
    internal static readonly StandardMaterial3D Glass = Material("071823", .10f, .30f);
    internal static readonly StandardMaterial3D Light = Material("70bed0", .22f, .12f, true);
    internal static readonly StandardMaterial3D Amber = Material("e39a42", .42f, .16f, true);
    private static readonly Dictionary<Vector3, BoxMesh> Boxes = new();

    private static Shader? _facadeShader;
    internal static ShaderMaterial Facade(string wall, string glass, float seed)
        => Facade(new Color(wall), new Color(glass), seed);

    internal static ShaderMaterial Facade(Color wall, Color glass, float seed)
    {
        var material = new ShaderMaterial { Shader = _facadeShader ??= GD.Load<Shader>("res://assets/visual/shaders/building_facade.gdshader") };
        material.SetShaderParameter("wall_color", wall);
        material.SetShaderParameter("glass_color", glass);
        material.SetShaderParameter("building_seed", seed);
        material.SetShaderParameter("window_light", .12f);
        return material;
    }

    public static SurfaceBuildingVisual Create(string typeId) => new(typeId);

    public static Node3D CreateHub(int level = 1, bool isCapital = false, bool isOutpost = false)
    {
        var root = new Node3D { Name = "ColonyHub" };
        var concrete = Material("596064", .82f, .16f);
        var trim = Material("75674f", .58f, .52f);
        Cylinder(root, 19, 20, 1.2f, new(0, .6f, 0), Metal, 8);
        Cylinder(root, 12, 14, 5, new(0, 3.7f, 0), Shell, 8);
        Cylinder(root, 8, 11, 1, new(0, 6.7f, 0), Metal, 8);
        Cylinder(root, 5, 7, 3, new(0, 8.7f, 0), Glass, 8);
        Cylinder(root, 5.8f, 5.8f, .35f, new(0, 10.4f, 0), Light, 8);
        Cylinder(root, .35f, .65f, 11, new(0, 15.7f, 0), Shell, 12);
        Box(root, new(7, .4f, .4f), new(0, 19, 0), Metal);
        Sphere(root, 1.5f, new(0, 22, 0), Light);
        // Broad civic stairs, a recessed doorway, and a weather canopy establish a clear front.
        for (var step = 0; step < 3; step++)
            Box(root, new(7.5f + step * 1.7f, .34f, 2.4f), new(0, .18f + step * .26f, 19.8f + step * 1.7f), concrete);
        Box(root, new(5.2f, 3.4f, .32f), new(0, 3f, 14.15f), Glass);
        Box(root, new(8.4f, .38f, 3.4f), new(0, 5f, 15.45f), trim);
        foreach (var side in new[] { -1, 1 })
            Cylinder(root, .28f, .38f, 4.2f, new(side * 3.5f, 2.5f, 15.3f), Metal, 8);
        for (var i = 0; i < 4; i++)
        {
            var angle = i * MathF.PI * .5f;
            Box(root, new(4.5f, .22f, 8), new(MathF.Sin(angle) * 16, 1.35f, MathF.Cos(angle) * 16), Bronze)
                .Rotation = new(0, angle, 0);
        }
        if (level >= 2)
        {
            Cylinder(root, 22.5f, 22.5f, .22f, new(0, 1.25f, 0), Bronze, 48);
            for (var i = 0; i < 4; i++)
            {
                var angle = i * MathF.PI * .5f + MathF.PI * .25f;
                var x = MathF.Sin(angle) * 14.5f;
                var z = MathF.Cos(angle) * 14.5f;
                Cylinder(root, 2.2f, 2.8f, 8.5f, new(x, 5.3f, z), Shell, 10);
                Cylinder(root, 1.1f, 1.35f, .7f, new(x, 9.9f, z), Metal, 12);
                Box(root, new(1.8f, .35f, 1.2f), new(x, 10.45f, z), trim);
            }
        }
        if (level >= 3)
        {
            var crown = Cylinder(root, 8.8f, 8.8f, .45f, new(0, 12.2f, 0), isCapital ? Bronze : Metal, 32);
            crown.RotationDegrees = new(0, 11.25f, 0);
            for (var i = 0; i < 8; i++)
            {
                var angle = i * MathF.Tau / 8;
                Sphere(root, .42f, new(MathF.Sin(angle) * 8.2f, 12.7f, MathF.Cos(angle) * 8.2f), Light);
            }
            Cylinder(root, .12f, .18f, 6, new(3.4f, 16.2f, 0), Metal, 8);
            var dish = Sphere(root, 1.25f, new(3.4f, 19.3f, 0), Glass);
            dish.Scale = new(1.7f, .3f, 1.7f);
        }
        if (isOutpost)
        {
            var warning = Material("d77d32", .45f, .2f, true);
            for (var i = 0; i < 4; i++)
            {
                var angle = i * MathF.PI * .5f;
                Sphere(root, .38f, new(MathF.Sin(angle) * 20, 1.8f, MathF.Cos(angle) * 20), warning);
            }
        }
        return root;
    }

    public static Node3D CreateHabitatCluster(double populationMillions, int requiredHabitatSystems, string visualClass,
        CivilizationVisualStyle? style = null, IReadOnlyList<UiSurfaceBuilding>? buildings = null) =>
        new SurfaceSettlementVisual(populationMillions, requiredHabitatSystems, visualClass,
            style ?? CivilizationVisualStyles.Terran, buildings ?? Array.Empty<UiSurfaceBuilding>());

    internal static StandardMaterial3D Material(string color, float roughness, float metallic = 0, bool glow = false)
    {
        var value = new Color(color);
        return new StandardMaterial3D
        {
            AlbedoColor = value, Roughness = roughness, Metallic = metallic,
            EmissionEnabled = glow, Emission = value, EmissionEnergyMultiplier = glow ? .34f : 0,
        };
    }

    internal static MeshInstance3D Box(Node3D parent, Vector3 size, Vector3 at, Material material)
    {
        if (!Boxes.TryGetValue(size, out var mesh)) { mesh = new BoxMesh { Size = size }; Boxes.Add(size, mesh); }
        return Mesh(parent, mesh, at, material);
    }
    internal static MeshInstance3D Cylinder(Node3D parent, float top, float bottom, float height,
        Vector3 at, Material material, int segments = 24) => Mesh(parent, new CylinderMesh
        { TopRadius = top, BottomRadius = bottom, Height = height, RadialSegments = Math.Max(48, segments), Rings = 1 }, at, material);
    internal static MeshInstance3D Sphere(Node3D parent, float radius, Vector3 at, Material material) =>
        Mesh(parent, new SphereMesh { Radius = radius, Height = radius * 2, RadialSegments = 64, Rings = 32 }, at, material);
    internal static MeshInstance3D Mesh(Node3D parent, Godot.Mesh mesh, Vector3 at, Material material)
    {
        var node = new MeshInstance3D { Mesh = mesh, Position = at, MaterialOverride = material };
        parent.AddChild(node);
        return node;
    }
}

/// <summary>Dense, animated settlement dressing derived from colony population. It is cosmetic:
/// traffic and skyline nodes never enter saved or authoritative simulation state.</summary>
public partial class SurfaceSettlementVisual : Node3D
{
    private readonly List<(Node3D Craft, float Phase, Vector2 Destination, float CruiseHeight)> _traffic = new();
    private readonly IReadOnlyList<SurfaceRoadObstacle> _constructionFootprints;
    private double _elapsed;

    public SurfaceSettlementVisual(double populationMillions, int requiredHabitatSystems, string visualClass,
        CivilizationVisualStyle style, IReadOnlyList<UiSurfaceBuilding> buildings)
    {
        Name = "EstablishedSettlement";
        _constructionFootprints = buildings.Select(building => new SurfaceRoadObstacle(new(building.X, building.Z),
            SurfaceBuildingCatalog.Find(building.TypeId)?.FootprintRadius ?? 15)).ToArray();
        var density = Math.Clamp(5 + (int)Math.Floor(Math.Log10(Math.Max(0.001, populationMillions) * 1000 + 1)), 6, 15);
        var sealedWorld = requiredHabitatSystems > 0;
        var shell = SurfaceBuildingVisuals.Facade(
            visualClass == "rocky" ? style.HullColor.Darkened(.18f) : style.SecondaryColor.Lightened(.10f),
            style.GlassColor, 7);
        var darkGlass = SurfaceBuildingVisuals.Facade(style.SecondaryColor.Darkened(.18f), style.GlassColor.Darkened(.28f), 29);
        var window = SurfaceBuildingVisuals.Material("b77834", .22f, .52f, true);
        var facadeTrim = SurfaceBuildingVisuals.Material("756b5a", .58f, .48f);
        var roofPlant = SurfaceBuildingVisuals.Material("294a37", .94f);
        var road = SurfaceBuildingVisuals.Material("182126", .78f, .14f);
        road.CullMode = BaseMaterial3D.CullModeEnum.Disabled;
        var curb = SurfaceBuildingVisuals.Material("657176", .64f, .42f);
        var marking = SurfaceBuildingVisuals.Material("b99c61", .46f, .28f, true);
        var plaza = SurfaceBuildingVisuals.Material("4d5048", .78f, .12f);
        var foliage = SurfaceBuildingVisuals.Material("244c38", .96f);
        var bark = SurfaceBuildingVisuals.Material("4a3828", .98f);

        // Radial transit avenues make the settlement read as a connected city from altitude.
        for (var spoke = 0; spoke < 8; spoke++)
        {
            var angle = spoke * MathF.Tau / 8;
            // Conform each street section to the same terrain used by placement.
            var direction = new Vector2(MathF.Sin(angle), MathF.Cos(angle));
            for (var segment=0; segment<18; segment++)
            {
                var p = direction * (segment*5f+2.5f);
                // Leave the real hub apron to its own geometry, and avoid every
                // construction footprint over the complete street segment.
                if (segment < 5 || !SurfaceRoadRouting.RouteIsClear(_constructionFootprints,
                    p - direction * 2.6f, p + direction * 2.6f, 5.4f)) continue;
                var avenue = SurfaceBuildingVisuals.Box(this, new(3.4f, .10f, 5.05f),
                    new(p.X, SurfaceConstruction.TerrainHeight(p.X,p.Y)+.10f, p.Y), road);
                avenue.Rotation = new(0, angle, 0);
                var side = new Vector2(-direction.Y, direction.X) * 2.25f;
                foreach (var sign in new[] { -1f, 1f })
                {
                    var walk = p + side * sign;
                    SurfaceBuildingVisuals.Box(this, new(.38f, .13f, 5.05f),
                        new(walk.X, SurfaceConstruction.TerrainHeight(walk.X, walk.Y) + .16f, walk.Y), curb).Rotation = new(0, angle, 0);
                }
                if (segment % 2 == 0)
                    SurfaceBuildingVisuals.Box(this, new(.18f, .07f, 1.65f),
                        new(p.X, SurfaceConstruction.TerrainHeight(p.X, p.Y) + .18f, p.Y), marking).Rotation = new(0, angle, 0);
            }
        }
        var ringIndex = 0;
        foreach (var ring in new[] { 38f, 66f })
        {
            var mesh = new SurfaceTool(); mesh.Begin(Godot.Mesh.PrimitiveType.Triangles);
            var hasClearArc = false;
            for (var i = 0; i < 128; i++)
            {
                var a=i*MathF.Tau/128; var b=(i+1)*MathF.Tau/128;
                var pa=new Vector3(MathF.Cos(a),0,MathF.Sin(a)); var pb=new Vector3(MathF.Cos(b),0,MathF.Sin(b));
                // A real module only interrupts its occupied arc. The remaining
                // promenade stays readable rather than disappearing as a whole.
                if (!SurfaceRoadRouting.RouteIsClear(_constructionFootprints,
                    new Vector2(pa.X * ring, pa.Z * ring), new Vector2(pb.X * ring, pb.Z * ring), 3.0f)) continue;
                hasClearArc = true;
                foreach (var v in new[] {pa*(ring-2.1f),pb*(ring-2.1f),pa*(ring+2.1f),pa*(ring+2.1f),pb*(ring-2.1f),pb*(ring+2.1f)})
                { mesh.SetNormal(Vector3.Up); mesh.AddVertex(v+new Vector3(0,SurfaceConstruction.TerrainHeight(v.X,v.Z)+.10f,0)); }
            }
            if (hasClearArc)
                SurfaceBuildingVisuals.Mesh(this,mesh.Commit(),Vector3.Zero,road).Name=$"DistrictRingRoad{++ringIndex}";
        }

        // Parks, low-rise blocks, and street lamps break up the skyline and make the roads
        // read as occupied districts instead of decorative lines around isolated towers.
        for (var district = 0; district < 6; district++)
        {
            var angle = district * MathF.Tau / 6 + .28f;
            var radius = district % 2 == 0 ? 48f : 74f;
            var x = MathF.Cos(angle) * radius;
            var z = MathF.Sin(angle) * radius;
            if (!SurfaceRoadRouting.IsClear(_constructionFootprints, new Vector2(x, z), 12)) continue;
            var ground = SurfaceConstruction.TerrainHeight(x, z);
            SurfaceBuildingVisuals.Cylinder(this, 7.5f, 8f, .45f, new(x, ground + .24f, z), plaza, 20)
                .Name = $"DistrictPlaza{district + 1}";
            if (district % 2 == 0)
            {
                SurfaceBuildingVisuals.Cylinder(this, .45f, .65f, 4.5f, new(x, ground + 2.5f, z), bark, 8);
                // Layered angular canopy over a visible trunk reads as a planted tree instead
                // of the former unsupported green sphere.
                SurfaceBuildingVisuals.Cylinder(this, .3f, 3.5f, 3.8f, new(x, ground + 6f, z), foliage, 9);
                SurfaceBuildingVisuals.Cylinder(this, .15f, 2.5f, 2.8f, new(x, ground + 8.1f, z), foliage, 9);
            }
            else
            {
                SurfaceBuildingVisuals.Box(this, new(11, 4.5f, 8), new(x, ground + 2.5f, z), shell);
                SurfaceBuildingVisuals.Box(this, new(11.2f, .3f, 8.2f), new(x, ground + 4.2f, z), window);
                // Commercial annexes make the outer district read as usable frontage,
                // rather than a ring of repeated residential towers.
                SurfaceBuildingVisuals.Box(this, new(7.5f, 1.1f, 2.2f), new(x, ground + 5.3f, z - 4.4f), SurfaceBuildingVisuals.Bronze);
            }
        }
        for (var lamp = 0; lamp < 16; lamp++)
        {
            var angle = lamp * MathF.Tau / 16;
            var x = MathF.Cos(angle) * 65;
            var z = MathF.Sin(angle) * 65;
            if (!SurfaceRoadRouting.IsClear(_constructionFootprints, new Vector2(x, z), 8)) continue;
            var ground = SurfaceConstruction.TerrainHeight(x, z);
            SurfaceBuildingVisuals.Cylinder(this, .11f, .16f, 3.5f,
                new(x, ground + 1.9f, z), SurfaceBuildingVisuals.Metal, 6);
            SurfaceBuildingVisuals.Sphere(this, .3f, new(x, ground + 3.85f, z), SurfaceBuildingVisuals.Light);
        }

        for (var index = 0; index < density; index++)
        {
            var angle = (index % 8 + .5f) * MathF.Tau / 8;
            var radius = index < 4 ? 28 : 52;
            var x = MathF.Cos(angle) * radius;
            var z = MathF.Sin(angle) * radius;
            if (!SurfaceRoadRouting.IsClear(_constructionFootprints, new Vector2(x, z), sealedWorld ? 9 : 14)) continue;
            var ground = SurfaceConstruction.TerrainHeight(x, z);
            if (sealedWorld)
            {
                SurfaceBuildingVisuals.Cylinder(this, 6.2f, 6.8f, 1.4f, new(x, ground + .7f, z), SurfaceBuildingVisuals.Metal, 20);
                var dome = SurfaceBuildingVisuals.Sphere(this, 5.8f, new(x, ground + 2.8f, z), index % 3 == 0 ? darkGlass : shell);
                dome.Scale = new(1, .58f, 1);
                SurfaceBuildingVisuals.Cylinder(this, 1.8f, 2.5f, 7 + index % 3 * 3,
                    new(x, ground + 6, z), darkGlass, 12);
            }
            else
            {
                // Four deliberately different massing families prevent the former
                // repeated needle skyline: residential terraces, broad offices,
                // research crowns, and compact industrial/service blocks.
                var family = index % 4;
                var height = 14f + (index * 17 % 32) + (index < 3 ? 15 : 0);
                if (family == 0) height *= .78f;
                if (family == 2) height *= 1.13f;
                var width = 6.5f + index % 3 * 1.7f;
                var depth = width * (.78f + index % 2 * .18f);
                if (family == 1) width *= 1.22f;
                if (family == 3) depth *= 1.28f;
                var podiumHeight = 3.2f;
                var lowerHeight = height * .58f;
                var upperHeight = height - lowerHeight;
                var upperWidth = width * .72f;
                SurfaceBuildingVisuals.Box(this, new(width + 3.4f, podiumHeight, depth + 3.4f),
                    new(x, ground + podiumHeight * .5f, z), SurfaceBuildingVisuals.Metal).Name = $"HighRise{index + 1}";
                var towerFacade = SurfaceBuildingVisuals.Facade(
                    style.HullColor.Darkened(.22f + (index % 3) * .045f),
                    style.GlassColor.Darkened(index % 2 == 0 ? .12f : .28f), 41 + index * 13);
                SurfaceBuildingVisuals.Box(this, new(width, lowerHeight, depth),
                    new(x, ground + podiumHeight + lowerHeight * .5f, z), index % 4 == 0 ? darkGlass : towerFacade);
                SurfaceBuildingVisuals.Box(this, new(upperWidth, upperHeight, depth * .72f),
                    new(x, ground + podiumHeight + lowerHeight + upperHeight * .5f, z),
                    index % 3 == 0 ? darkGlass : towerFacade);
                if (family == 0)
                {
                    // Residential wings and planted roofs form a block around a
                    // sheltered courtyard, rather than another isolated tower.
                    foreach (var side in new[] { -1f, 1f })
                    {
                        var wing = new Vector3(x + side * (width * .58f + 1.5f),
                            ground + podiumHeight + lowerHeight * .34f, z);
                        SurfaceBuildingVisuals.Box(this, new(width * .42f, lowerHeight * .68f, depth * .74f), wing, shell);
                        SurfaceBuildingVisuals.Box(this, new(width * .45f, .22f, depth * .77f),
                            wing + new Vector3(0, lowerHeight * .35f, 0), roofPlant);
                    }
                }
                else if (family == 2)
                {
                    // A recessed research lantern gives a legible skyline landmark.
                    SurfaceBuildingVisuals.Cylinder(this, upperWidth * .34f, upperWidth * .42f, 6.2f,
                        new(x, ground + podiumHeight + height + 3.1f, z), darkGlass, 12);
                    SurfaceBuildingVisuals.Cylinder(this, upperWidth * .48f, upperWidth * .48f, .22f,
                        new(x, ground + podiumHeight + height + 6.35f, z), SurfaceBuildingVisuals.Light, 16);
                }
                else if (family == 3)
                {
                    // Service/industry blocks have a visible loading canopy and roof plant.
                    SurfaceBuildingVisuals.Box(this, new(width * .82f, 2.4f, depth * .34f),
                        new(x, ground + podiumHeight + 1.2f, z + depth * .58f), SurfaceBuildingVisuals.Bronze);
                    for (var unit = -1; unit <= 1; unit++)
                        SurfaceBuildingVisuals.Box(this, new(1.35f, .9f, 1.55f),
                            new(x + unit * 1.65f, ground + podiumHeight + height + 1.15f, z), SurfaceBuildingVisuals.Metal);
                }
                if (index % 4 == 1)
                {
                    // A stepped office crown is a distinct silhouette at overview scale.
                    SurfaceBuildingVisuals.Box(this, new(upperWidth * .56f, 8.5f, upperWidth * .56f),
                        new(x, ground + podiumHeight + height + 4.25f, z), darkGlass);
                }
                else if (index % 4 == 2)
                {
                    // A parapet, planter beds and low shrubs form a believable occupied terrace.
                    SurfaceBuildingVisuals.Box(this, new(upperWidth * .9f, .55f, depth * .63f),
                        new(x, ground + podiumHeight + height + .75f, z), facadeTrim);
                    foreach (var side in new[] { -1, 1 })
                        SurfaceBuildingVisuals.Box(this, new(upperWidth * .28f, .65f, 1.05f),
                            new(x + side * upperWidth * .24f, ground + podiumHeight + height + 1.35f, z), roofPlant);
                }
                for (var floor = 10f; floor < height - 2; floor += 13f)
                {
                    var levelWidth = floor < lowerHeight ? width : upperWidth;
                    var y = ground + podiumHeight + floor;
                    SurfaceBuildingVisuals.Box(this, new(levelWidth + .16f, .24f, depth + .16f), new(x, y, z), facadeTrim);
                }
                // Recessed street entrance and canopy face the nearest radial avenue.
                var outward = new Vector2(x, z).Normalized();
                var entrance = new Vector3(x - outward.X * (depth * .5f + 1.72f), ground + 1.7f,
                    z - outward.Y * (depth * .5f + 1.72f));
                var entranceNode = SurfaceBuildingVisuals.Box(this, new(3.4f, 2.7f, .28f), entrance, SurfaceBuildingVisuals.Glass);
                entranceNode.Rotation = new(0, -MathF.Atan2(outward.Y, outward.X) + MathF.PI * .5f, 0);
                var canopy = SurfaceBuildingVisuals.Box(this, new(4.8f, .28f, 2.6f), entrance + new Vector3(0, 1.45f, 0), facadeTrim);
                canopy.Rotation = entranceNode.Rotation;
                SurfaceBuildingVisuals.Box(this, new(upperWidth * .84f, .8f, upperWidth * .84f),
                    new(x, ground + podiumHeight + height + .4f, z), SurfaceBuildingVisuals.Metal);
                for (var plant=0; plant<3; plant++)
                    SurfaceBuildingVisuals.Box(this,new(1.2f,.8f,1.6f),
                        new(x+(plant-1)*1.5f,ground+podiumHeight+height+1.2f,z),SurfaceBuildingVisuals.Metal);
                SurfaceBuildingVisuals.Cylinder(this, .15f, .22f, 5.5f,
                    new(x, ground + podiumHeight + height + 3.55f, z), SurfaceBuildingVisuals.Metal, 8);
                SurfaceBuildingVisuals.Sphere(this, .46f,
                    new(x, ground + podiumHeight + height + 6.7f, z), SurfaceBuildingVisuals.Light);
            }
        }

        var padGround = SurfaceConstruction.TerrainHeight(76, -44);
        SurfaceBuildingVisuals.Cylinder(this, 12, 13, .45f, new(76, padGround + .24f, -44), SurfaceBuildingVisuals.Metal, 32);
        SurfaceBuildingVisuals.Cylinder(this, 8.5f, 8.5f, .08f, new(76, padGround + .52f, -44), plaza, 48);
        SurfaceBuildingVisuals.Box(this,new(5,.08f,.38f),new(76,padGround+.58f,-44),SurfaceBuildingVisuals.Amber);
        var trafficCount = Math.Clamp(density / 4, 2, 4);
        for (var index = 0; index < trafficCount; index++) AddShuttle(index, trafficCount);
    }

    public override void _Process(double delta)
    {
        _elapsed += Math.Min(delta, .1);
        foreach (var (craft, phase, destination, cruiseHeight) in _traffic)
        {
            var progress = (float)((_elapsed * .022 + phase) % 1.0);
            var outbound = progress < .46f;
            var inbound = progress > .54f;
            craft.Visible = outbound || inbound;
            if (!craft.Visible) continue;

            var pathProgress = outbound ? progress / .46f : (1f - progress) / .46f;
            pathProgress = Math.Clamp(pathProgress, 0, 1);
            craft.Position = FlightPosition(destination, cruiseHeight, pathProgress);
            var lookProgress = Math.Clamp(pathProgress + (outbound ? .012f : -.012f), 0, 1);
            var lookAt = FlightPosition(destination, cruiseHeight, lookProgress);
            if (lookAt.DistanceSquaredTo(craft.Position) > .0001f)
                craft.LookAt(lookAt, Vector3.Up);
        }
    }

    private static Vector3 FlightPosition(Vector2 destination, float cruiseHeight, float progress)
    {
        var eased = progress * progress * (3f - 2f * progress);
        var x = Mathf.Lerp(76, destination.X, eased);
        var z = Mathf.Lerp(-44, destination.Y, eased);
        var ground = SurfaceConstruction.TerrainHeight(x, z);
        var climb = MathF.Sin(progress * MathF.PI) * cruiseHeight + progress * 72f;
        return new(x, ground + 3.2f + climb, z);
    }

    private void AddShuttle(int index, int count)
    {
        var craft = new Node3D { Name = $"CivilianShuttle{index + 1}" };
        AddChild(craft);
        SurfaceBuildingVisuals.Box(craft, new(1.5f, .55f, 5.5f), Vector3.Zero, SurfaceBuildingVisuals.Shell);
        SurfaceBuildingVisuals.Box(craft, new(5.2f, .16f, 1.5f), new(0, -.05f, .2f), SurfaceBuildingVisuals.Metal);
        SurfaceBuildingVisuals.Box(craft, new(.72f, .32f, 1.8f), new(0, .3f, -1.35f), SurfaceBuildingVisuals.Glass);
        SurfaceBuildingVisuals.Sphere(craft, .24f, new(-2.3f, 0, .4f), SurfaceBuildingVisuals.Amber);
        SurfaceBuildingVisuals.Sphere(craft, .24f, new(2.3f, 0, .4f), SurfaceBuildingVisuals.Light);
        var angle = .45f + index * MathF.Tau / count + (index % 2 == 0 ? .18f : -.12f);
        var distance = 720f + index * 95f;
        var destination = new Vector2(MathF.Cos(angle) * distance, MathF.Sin(angle) * distance);
        _traffic.Add((craft, index / (float)count, destination, 34 + index * 8));
    }
}

public partial class SurfaceBuildingVisual : Node3D
{
    public string TypeId { get; }
    public int VisualTier { get; }
    private readonly Node3D _structure = new();
    private readonly Node3D _scaffold = new();
    private readonly Node3D _supports = new();
    private readonly List<(MeshInstance3D Mesh, Vector2 Offset)> _supportPosts = new();
    private readonly List<(MeshInstance3D Mesh, Vector2 Offset)> _scaffoldPosts = new();
    private readonly List<(MeshInstance3D Mesh, Material? Material)> _surfaces = new();
    private readonly StandardMaterial3D _preview = new()
    {
        Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
        ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
        AlbedoColor = new Color(.25f, .9f, .72f, .5f),
        CullMode = BaseMaterial3D.CullModeEnum.Disabled,
    };
    private readonly Label3D _status;
    private readonly MeshInstance3D _beacon;
    private readonly MeshInstance3D _footprint;
    private readonly MeshInstance3D _priorityHalo;
    private readonly MeshInstance3D _offlineHalo;
    private readonly Node3D _scanner = new();
    private bool _isPreview;
    private bool _previewValid;
    private bool _complete;
    private double _shownProgress;
    private double _targetProgress;
    private float _phase;
    private readonly float _radius;
    private Vector3? _groundingKey;

    public SurfaceBuildingVisual(string typeId)
    {
        TypeId = typeId;
        Name = "Building_" + typeId;
        AddChild(_structure);
        AddChild(_scaffold);
        AddChild(_supports);
        var baseType = typeId.StartsWith("advanced_", StringComparison.Ordinal)
            ? typeId["advanced_".Length..] : typeId;
        VisualTier = baseType == typeId ? 1 : 2;
        var radius = baseType is "fabricator" or "controlled_agriculture" or "cargo_terminal" ? 17f : baseType is "science_lab" or "trade_hub" or "habitat_complex" or "water_reclamation" ? 15f : 12f;
        _radius = radius;
        SurfaceBuildingVisuals.Cylinder(_structure, radius * .85f, radius * .91f, 1.4f,
            new(0, .7f, 0), SurfaceBuildingVisuals.Metal, 8);
        switch (baseType)
        {
            case "power_generator": BuildGenerator(); break;
            case "science_lab": BuildLab(); break;
            case "fabricator": BuildFabricator(); break;
            case "trade_hub": BuildTradeHub(); break;
            case "habitat_complex": BuildHabitat(); break;
            case "controlled_agriculture": BuildHabitat(); break;
            case "water_reclamation": BuildFabricator(); break;
            case "grid_battery": BuildBattery(); break;
            case "cargo_terminal": BuildCargoTerminal(); break;
        }
        AddGroundAccess(baseType, radius);
        if (baseType != typeId)
        {
            SurfaceBuildingVisuals.Mesh(_structure, new TorusMesh
            {
                InnerRadius = radius * .43f, OuterRadius = radius * .52f, Rings = 36, RingSegments = 8,
            }, new(0, 12.4f, 0), SurfaceBuildingVisuals.Light);
            for (var index = 0; index < 4; index++)
            {
                var angle = index * MathF.Tau / 4;
                SurfaceBuildingVisuals.Sphere(_structure, .7f,
                    new(MathF.Cos(angle) * radius * .62f, 10.6f, MathF.Sin(angle) * radius * .62f),
                    SurfaceBuildingVisuals.Amber);
            }
            AddAdvancedTier(baseType, radius);
        }
        _beacon = SurfaceBuildingVisuals.Sphere(_structure, .6f, new(0, 13, 0), SurfaceBuildingVisuals.Light);
        _priorityHalo = SurfaceBuildingVisuals.Mesh(_structure, new TorusMesh
        {
            InnerRadius = radius * .48f, OuterRadius = radius * .55f, Rings = 48, RingSegments = 8,
        }, new(0, 14.2f, 0), SurfaceBuildingVisuals.Amber);
        _priorityHalo.Visible = false;
        _offlineHalo = SurfaceBuildingVisuals.Mesh(_structure, new TorusMesh
        {
            InnerRadius = radius * .88f, OuterRadius = radius * .94f, Rings = 48, RingSegments = 8,
        }, new(0, 1.55f, 0), SurfaceBuildingVisuals.Material("c84c3f", .4f, .08f, true));
        _offlineHalo.Visible = false;
        for (var index = 0; index < 8; index++)
        {
            var angle = index * MathF.Tau / 8;
            var offset = new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * radius * .8f;
            var post = SurfaceBuildingVisuals.Cylinder(_supports, .75f, 1.05f, 1,
                new(offset.X, 0, offset.Y), SurfaceBuildingVisuals.Metal, 8);
            _supportPosts.Add((post, offset));
        }
        RememberSurfaces(_structure);
        RememberSurfaces(_supports);
        var scaffoldMaterial = SurfaceBuildingVisuals.Material("bd954c", .8f);
        foreach (var x in new[] { -radius * .72f, radius * .72f })
        foreach (var z in new[] { -radius * .72f, radius * .72f })
        {
            var post = SurfaceBuildingVisuals.Box(_scaffold, new(.24f, 1, .24f), new(x, 6.5f, z), scaffoldMaterial);
            post.Scale = new(1, 13, 1);
            _scaffoldPosts.Add((post, new(x, z)));
        }
        for (var level = 1; level <= 3; level++)
        {
            foreach (var side in new[] { -1, 1 })
            {
                SurfaceBuildingVisuals.Box(_scaffold, new(radius * 1.44f, .2f, .2f),
                    new(0, level * 4, side * radius * .72f), scaffoldMaterial);
                SurfaceBuildingVisuals.Box(_scaffold, new(.2f, .2f, radius * 1.44f),
                    new(side * radius * .72f, level * 4, 0), scaffoldMaterial);
            }
        }
        _scaffold.AddChild(_scanner);
        SurfaceBuildingVisuals.Box(_scanner, new(radius * 1.45f, .12f, .3f), Vector3.Zero, SurfaceBuildingVisuals.Light);
        SurfaceBuildingVisuals.Box(_scanner, new(.28f, .28f, radius * 1.18f), Vector3.Zero, SurfaceBuildingVisuals.Amber);
        _status = new Label3D
        {
            Position = new(0, 17, 0), FontSize = 34, PixelSize = .025f,
            Billboard = BaseMaterial3D.BillboardModeEnum.Enabled, NoDepthTest = false,
            Modulate = new Color("dcecea"), OutlineSize = 7,
        };
        AddChild(_status);
        _footprint = SurfaceBuildingVisuals.Mesh(this, new TorusMesh
        { InnerRadius = radius - .2f, OuterRadius = radius + .2f, Rings = 48, RingSegments = 6 },
            new(0, .18f, 0), _preview);
        _footprint.Visible = false;
        _supports.Visible = false; // Palette thumbnails have no terrain to support against.
    }

    private void AddAdvancedTier(string baseType, float radius)
    {
        // Upgrades retain the functional silhouette and add a readable second layer of
        // machinery. These parts are driven by the authoritative upgraded TypeId.
        switch (baseType)
        {
            case "power_generator":
                foreach (var side in new[] { -1, 1 })
                {
                    SurfaceBuildingVisuals.Cylinder(_structure, 1.35f, 1.8f, 12,
                        new(side * 7.2f, 7.2f, 0), SurfaceBuildingVisuals.Metal, 16);
                    for (var ring = 0; ring < 4; ring++)
                        SurfaceBuildingVisuals.Cylinder(_structure, 1.6f, 1.6f, .22f,
                            new(side * 7.2f, 3.8f + ring * 2.4f, 0), SurfaceBuildingVisuals.Light, 24);
                }
                break;
            case "science_lab":
                for (var pod = 0; pod < 3; pod++)
                {
                    var angle = pod * MathF.Tau / 3 + .35f;
                    var at = new Vector3(MathF.Cos(angle) * 11.5f, 7.5f, MathF.Sin(angle) * 11.5f);
                    var observatory = SurfaceBuildingVisuals.Sphere(_structure, 3.1f, at, SurfaceBuildingVisuals.Glass);
                    observatory.Scale = new(1, .48f, 1);
                    SurfaceBuildingVisuals.Cylinder(_structure, .16f, .25f, 4.8f,
                        at + new Vector3(0, 3.2f, 0), SurfaceBuildingVisuals.Bronze, 10);
                }
                break;
            case "fabricator":
                foreach (var z in new[] { -6f, 6f })
                {
                    SurfaceBuildingVisuals.Box(_structure, new(radius * 1.55f, .55f, 1.0f),
                        new(0, 17.5f, z), SurfaceBuildingVisuals.Bronze);
                    for (var x = -1; x <= 1; x++)
                        SurfaceBuildingVisuals.Box(_structure, new(.35f, 6.5f, .35f),
                            new(x * radius * .62f, 14.2f, z), SurfaceBuildingVisuals.Metal);
                }
                break;
            case "trade_hub":
                for (var index = 0; index < 6; index++)
                {
                    var angle = index * MathF.Tau / 6;
                    SurfaceBuildingVisuals.Box(_structure, new(4.2f, 7.5f, 2.4f),
                        new(MathF.Cos(angle) * radius * .73f, 7.2f, MathF.Sin(angle) * radius * .73f),
                        SurfaceBuildingVisuals.Glass).Rotation = new(0, -angle, 0);
                }
                break;
            case "habitat_complex":
                SurfaceBuildingVisuals.Cylinder(_structure, 4.2f, 5.4f, 18,
                    new(0, 11.5f, 0), SurfaceBuildingVisuals.Shell, 20);
                for (var floor = 0; floor < 5; floor++)
                    SurfaceBuildingVisuals.Cylinder(_structure, 4.5f, 4.5f, .22f,
                        new(0, 5.2f + floor * 3.2f, 0), SurfaceBuildingVisuals.Light, 24);
                break;
        }
    }

    /// <summary>Level the whole visual above the highest terrain under its footprint. The saved
    /// X/Z and simulation placement stay exact; supports extend down to the shared heightfield.</summary>
    public void PlaceOnTerrain(float x, float z, float rotationDegrees)
    {
        var key = new Vector3(x, z, rotationDegrees);
        if (_groundingKey == key) return;
        _groundingKey = key;
        var radians = Mathf.DegToRad(rotationDegrees);
        var cosine = MathF.Cos(radians); var sine = MathF.Sin(radians);
        float GroundAt(Vector2 local) => SurfaceConstruction.TerrainHeight(
            x + cosine * local.X + sine * local.Y, z - sine * local.X + cosine * local.Y);
        var highest = GroundAt(Vector2.Zero);
        // Include the outer scaffold corners, plus the interior, so neither walls nor scaffolds
        // begin below a hill. A small margin covers the 4 m terrain triangles between samples.
        for (var ring = 1; ring <= 3; ring++)
        for (var sample = 0; sample < 32; sample++)
        {
            var angle = sample * MathF.Tau / 32;
            highest = Math.Max(highest, GroundAt(new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * _radius * 1.04f * ring / 3));
        }
        highest += .2f;
        Position = new(x, highest, z);
        RotationDegrees = new(0, rotationDegrees, 0);
        _supports.Visible = true;
        foreach (var (post, offset) in _supportPosts)
        {
            var drop = Math.Max(.1f, highest - GroundAt(offset) + .2f);
            post.Scale = new(1, drop + .6f, 1);
            post.Position = new(offset.X, (.6f - drop) * .5f, offset.Y);
        }
        foreach (var (post, offset) in _scaffoldPosts)
        {
            var drop = Math.Max(.1f, highest - GroundAt(offset) + .2f);
            post.Scale = new(1, 13 + drop, 1);
            post.Position = new(offset.X, (13 - drop) * .5f, offset.Y);
        }
    }

    public void ShowPreview(bool valid)
    {
        if (_isPreview && _previewValid == valid) return;
        _isPreview = true;
        _previewValid = valid;
        _preview.AlbedoColor = valid ? new(.23f, .95f, .7f, .55f) : new(1, .24f, .18f, .57f);
        foreach (var part in _surfaces) part.Mesh.MaterialOverride = _preview;
        _structure.Scale = Vector3.One;
        _scaffold.Visible = false;
        _status.Visible = false;
        _footprint.Visible = true;
    }

    public void UpdateState(UiSurfaceBuilding building)
    {
        _isPreview = false;
        _complete = building.Complete;
        _targetProgress = Math.Clamp(building.Progress, 0, 1);
        // Progress only rises in authoritative snapshots; ease the visual between ticks.
        if (_shownProgress > _targetProgress) _shownProgress = _targetProgress;
        _scaffold.Visible = !_complete;
        _scanner.Visible = !_complete;
        _footprint.Visible = false;
        foreach (var part in _surfaces) part.Mesh.MaterialOverride = part.Material;
        _beacon.MaterialOverride = building.Enabled && building.Powered ? SurfaceBuildingVisuals.Light : SurfaceBuildingVisuals.Amber;
        _priorityHalo.Visible = building.Complete && building.Prioritized;
        _offlineHalo.Visible = building.Complete && (!building.Enabled || building.Condition <= SurfaceConstruction.MinimumOperationalCondition);
        _status.Text = building.Complete ? (!building.Enabled ? building.Name + " · shut down" : building.Condition <= SurfaceConstruction.MinimumOperationalCondition ? building.Name + " · repair required" : !building.Staffed ? building.Name + " · needs workers" : building.Powered ? building.Name : building.Name + " · needs power")
            : $"{building.Name} · {building.ConstructionStage} {building.ConstructionStageProgress:P0}";
        _status.Modulate = building.Complete && (!building.Enabled || !building.Powered) ? new Color("e8b463") : new Color("dcecea");
        if (_complete) _structure.Scale = Vector3.One;
    }

    public void SetSelected(bool selected)
    {
        if (_isPreview) return;
        _preview.AlbedoColor = new Color(.3f, .86f, 1f, .72f);
        _footprint.Visible = selected;
        _status.Visible = selected || !_complete;
    }

    public override void _Process(double delta)
    {
        if (_isPreview || _complete) return;
        _shownProgress = Math.Min(_targetProgress, _shownProgress + delta * .35);
        _structure.Scale = new(1, .08f + .92f * (float)_shownProgress, 1);
        _phase += (float)delta * 1.7f;
        _scanner.Position = new(0, 2 + (MathF.Sin(_phase) * .5f + .5f) * 10, 0);
    }

    private void RememberSurfaces(Node node)
    {
        foreach (var child in node.GetChildren())
        {
            if (child is MeshInstance3D mesh) _surfaces.Add((mesh, mesh.MaterialOverride));
            RememberSurfaces(child);
        }
    }

    private void AddGroundAccess(string baseType, float radius)
    {
        // Common scale cues: a paved apron, person-sized recessed door, canopy, and service vents.
        // They remain inside each catalog footprint and do not affect placement or operation.
        var concrete = SurfaceBuildingVisuals.Material("596064", .84f, .12f);
        var door = SurfaceBuildingVisuals.Material("101b21", .30f, .52f);
        var trim = SurfaceBuildingVisuals.Material("76684f", .56f, .48f);
        var frontage = baseType is "fabricator" or "cargo_terminal" ? 7.5f : 5.2f;
        SurfaceBuildingVisuals.Box(_structure, new(frontage + 3.2f, .28f, 4.4f),
            new(0, 1.58f, radius * .68f), concrete);
        SurfaceBuildingVisuals.Box(_structure, new(frontage, 3.2f, .28f),
            new(0, 3.1f, radius * .69f), door);
        SurfaceBuildingVisuals.Box(_structure, new(frontage + 1.6f, .34f, 2.8f),
            new(0, 4.8f, radius * .79f), trim);
        foreach (var side in new[] { -1, 1 })
        {
            SurfaceBuildingVisuals.Cylinder(_structure, .18f, .27f, 2.8f,
                new(side * (frontage * .5f + .45f), 3.15f, radius * .75f), SurfaceBuildingVisuals.Metal, 8);
            SurfaceBuildingVisuals.Box(_structure, new(1.4f, .9f, 1.1f),
                new(side * radius * .5f, 2.05f, -radius * .52f), SurfaceBuildingVisuals.Metal);
        }
    }

    private void BuildGenerator()
    {
        SurfaceBuildingVisuals.Cylinder(_structure, 3.1f, 3.8f, 7, new(0, 5, 0), SurfaceBuildingVisuals.Shell);
        for (var i = 0; i < 3; i++)
            SurfaceBuildingVisuals.Cylinder(_structure, 3.35f, 3.35f, .38f, new(0, 3.4f + i * 2, 0), SurfaceBuildingVisuals.Light);
        foreach (var side in new[] { -1, 1 })
        {
            SurfaceBuildingVisuals.Box(_structure, new(1, 3.6f, 1), new(side * 7, 3, 0), SurfaceBuildingVisuals.Bronze);
            var panel = new Node3D { Position = new(side * 7, 5, 0), Rotation = new(0, 0, side * -.22f) };
            _structure.AddChild(panel);
            SurfaceBuildingVisuals.Box(panel, new(6.2f, .25f, 10), Vector3.Zero, SurfaceBuildingVisuals.Solar);
            for (var line = -2; line <= 2; line++)
                SurfaceBuildingVisuals.Box(panel, new(.07f, .07f, 10), new(line * 1.16f, .17f, 0), SurfaceBuildingVisuals.Shell);
            for (var line = -3; line <= 3; line++)
                SurfaceBuildingVisuals.Box(panel, new(6.2f, .07f, .06f), new(0, .17f, line * 1.4f), SurfaceBuildingVisuals.Shell);
        }
    }

    private void BuildLab()
    {
        SurfaceBuildingVisuals.Cylinder(_structure, 8, 9, 3.5f, new(0, 3, 0), SurfaceBuildingVisuals.Shell);
        var dome = SurfaceBuildingVisuals.Sphere(_structure, 7.4f, new(0, 4.2f, 0), SurfaceBuildingVisuals.Glass);
        dome.Scale = new(1, .75f, 1);
        SurfaceBuildingVisuals.Cylinder(_structure, 8, 8, .3f, new(0, 4.6f, 0), SurfaceBuildingVisuals.Light);
        foreach (var side in new[] { -1, 1 })
        {
            SurfaceBuildingVisuals.Box(_structure, new(5, 3.4f, 8), new(side * 9, 3.1f, 0), SurfaceBuildingVisuals.Shell);
            SurfaceBuildingVisuals.Box(_structure, new(.15f, 1.4f, 5), new(side * 11.55f, 3.8f, 0), SurfaceBuildingVisuals.Glass);
        }
        SurfaceBuildingVisuals.Cylinder(_structure, .2f, .35f, 6, new(5, 10, 5), SurfaceBuildingVisuals.Metal, 12);
        var dish = SurfaceBuildingVisuals.Sphere(_structure, 2.2f, new(5, 13, 5), SurfaceBuildingVisuals.Shell);
        dish.Scale = new(1, .25f, 1);
        dish.Rotation = new(.35f, 0, .3f);
    }

    private void BuildFabricator()
    {
        SurfaceBuildingVisuals.Box(_structure, new(20, 6, 15), new(0, 4.5f, 0), SurfaceBuildingVisuals.Shell);
        SurfaceBuildingVisuals.Box(_structure, new(21, .7f, 16), new(0, 7.85f, 0), SurfaceBuildingVisuals.Metal);
        SurfaceBuildingVisuals.Box(_structure, new(9, 4.5f, .25f), new(0, 3.8f, 7.65f), SurfaceBuildingVisuals.Glass);
        for (var i = -2; i <= 2; i++)
            SurfaceBuildingVisuals.Box(_structure, new(.3f, 4.5f, .4f), new(i * 1.8f, 3.8f, 7.85f), SurfaceBuildingVisuals.Bronze);
        foreach (var side in new[] { -1, 1 })
        {
            SurfaceBuildingVisuals.Box(_structure, new(.8f, 13, .8f), new(side * 12, 7.2f, 0), SurfaceBuildingVisuals.Bronze);
            SurfaceBuildingVisuals.Cylinder(_structure, 1.7f, 1.7f, 4, new(side * 5, 10, -4), SurfaceBuildingVisuals.Metal);
        }
        SurfaceBuildingVisuals.Box(_structure, new(25, 1, 1.8f), new(0, 13.6f, 0), SurfaceBuildingVisuals.Bronze);
        SurfaceBuildingVisuals.Box(_structure, new(3, 1, 2.3f), new(2, 12.7f, 0), SurfaceBuildingVisuals.Metal);
        SurfaceBuildingVisuals.Box(_structure, new(.15f, 3, .15f), new(2, 10.7f, 0), SurfaceBuildingVisuals.Light);
    }

    private void BuildTradeHub()
    {
        SurfaceBuildingVisuals.Cylinder(_structure, 9, 10, 2.2f, new(0, 1.7f, 0), SurfaceBuildingVisuals.Shell, 12);
        for (var level = 0; level < 3; level++)
        {
            var radius = 7.2f - level * 1.25f;
            SurfaceBuildingVisuals.Cylinder(_structure, radius, radius + .45f, 2.4f,
                new(0, 4.2f + level * 2.35f, 0), level == 1 ? SurfaceBuildingVisuals.Glass : SurfaceBuildingVisuals.Metal, 12);
        }
        for (var side = 0; side < 4; side++)
        {
            var angle = side * MathF.Tau / 4;
            var position = new Vector3(MathF.Cos(angle) * 10.5f, 3.2f, MathF.Sin(angle) * 10.5f);
            SurfaceBuildingVisuals.Box(_structure, new(5.5f, 3.8f, 3.2f), position, SurfaceBuildingVisuals.Glass);
        }
        SurfaceBuildingVisuals.Cylinder(_structure, .35f, .5f, 7, new(0, 12, 0), SurfaceBuildingVisuals.Bronze, 10);
        SurfaceBuildingVisuals.Sphere(_structure, 1.25f, new(0, 16, 0), SurfaceBuildingVisuals.Light);
    }

    private void BuildBattery()
    {
        for (var bank = -1; bank <= 1; bank++)
        {
            SurfaceBuildingVisuals.Box(_structure, new(5.2f, 5.8f, 3.4f),
                new(bank * 5.8f, 4.3f, 0), SurfaceBuildingVisuals.Shell);
            for (var level = 0; level < 4; level++)
                SurfaceBuildingVisuals.Box(_structure, new(4.5f, .18f, 3.48f),
                    new(bank * 5.8f, 2.0f + level * 1.55f, 0), SurfaceBuildingVisuals.Light);
        }
        SurfaceBuildingVisuals.Cylinder(_structure, 1.1f, 1.35f, 8.5f,
            new(0, 6.1f, 0), SurfaceBuildingVisuals.Bronze, 12);
    }

    private void BuildCargoTerminal()
    {
        SurfaceBuildingVisuals.Box(_structure, new(22, 1.2f, 15), new(0, 1.2f, 0), SurfaceBuildingVisuals.Metal);
        for (var row = -1; row <= 1; row++)
        for (var column = -2; column <= 2; column++)
            SurfaceBuildingVisuals.Box(_structure, new(3.2f, 2.2f, 2.6f),
                new(column * 3.7f, 2.9f, row * 3.3f),
                (row + column) % 2 == 0 ? SurfaceBuildingVisuals.Bronze : SurfaceBuildingVisuals.Shell);
        foreach (var side in new[] { -1, 1 })
        {
            SurfaceBuildingVisuals.Box(_structure, new(.8f, 11, .8f), new(side * 10.5f, 7, -6), SurfaceBuildingVisuals.Bronze);
            SurfaceBuildingVisuals.Box(_structure, new(9, .7f, .8f), new(side * 6.5f, 12, -6), SurfaceBuildingVisuals.Bronze);
            SurfaceBuildingVisuals.Box(_structure, new(.5f, 7, .5f), new(side * 2.5f, 8.5f, -6), SurfaceBuildingVisuals.Metal);
        }
        SurfaceBuildingVisuals.Box(_structure, new(7, 4.5f, 5), new(0, 4.1f, 7), SurfaceBuildingVisuals.Glass);
        SurfaceBuildingVisuals.Sphere(_structure, .9f, new(0, 8.2f, 7), SurfaceBuildingVisuals.Light);
    }

    private void BuildHabitat()
    {
        SurfaceBuildingVisuals.Cylinder(_structure, 10, 11, 1.8f, new(0, 1.3f, 0), SurfaceBuildingVisuals.Metal, 20);
        for (var index = 0; index < 3; index++)
        {
            var angle = index * MathF.Tau / 3;
            var x = MathF.Cos(angle) * 6.5f;
            var z = MathF.Sin(angle) * 6.5f;
            var dome = SurfaceBuildingVisuals.Sphere(_structure, 5.4f, new(x, 4.1f, z),
                index == 0 ? SurfaceBuildingVisuals.Glass : SurfaceBuildingVisuals.Shell);
            dome.Scale = new(1, .62f, 1);
        }
        SurfaceBuildingVisuals.Cylinder(_structure, 2.2f, 2.8f, 8, new(0, 7, 0), SurfaceBuildingVisuals.Bronze, 12);
        SurfaceBuildingVisuals.Sphere(_structure, 1.1f, new(0, 11.5f, 0), SurfaceBuildingVisuals.Light);
    }
}
