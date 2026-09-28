using System;
using Godot;
using Game.Presentation;

namespace Game.Presentation.Spatial;

/// <summary>Depth-tested, staged orbital infrastructure made from deliberately bounded primitive detail.</summary>
public static class OrbitalStructureGeometry
{
    // Shared materials keep repeated structure cheap while panels and navigation lights remain legible.
    private static readonly StandardMaterial3D Hull = Material("71858d", .72f, .33f);
    private static readonly StandardMaterial3D HullLight = Material("aab8bd", .61f, .28f);
    private static readonly StandardMaterial3D Structure = Material("263845", .68f, .40f);
    private static readonly StandardMaterial3D Glass = Material("164d75", .50f, .20f);
    private static readonly StandardMaterial3D Solar = Material("102b4c", .48f, .18f);
    private static readonly StandardMaterial3D Rock = Material("6e6254", .03f, .97f);
    private static readonly StandardMaterial3D Beacon = Material("8bdaca", .20f, .30f, true);
    private static readonly StandardMaterial3D Warning = Material("e99b52", .15f, .34f, true);

    public static Node3D Create(SystemSpatialInfrastructureMarker marker, CivilizationVisualStyle? style = null)
    {
        var root = new Node3D { Name = marker.ProjectId };
        var phase = Math.Clamp((int)Math.Ceiling(marker.Progress * 4), 1, 4);
        switch (marker.ProjectId)
        {
            case "orbital_shipyard": CreateShipyard(root, phase); break;
            case "orbital_launch_complex": CreateLaunchComplex(root, phase); break;
            case "asteroid_resource_network": CreateAsteroidNetwork(root, phase); break;
            default: CreateOrbitalOutpost(root, phase); break;
        }
        if (style is not null) ApplyCivilizationStyle(root, style);
        return root;
    }

    private static void CreateShipyard(Node3D root, int phase)
    {
        // Long armored keel, stepped command core, and open docking side of a true shipyard.
        Box(root, new(0, 0, 0), new(15, .72f, 1.5f), Hull);
        Box(root, new(0, .58f, 0), new(11.8f, .35f, 2.55f), Structure);
        Box(root, new(0, 1.05f, 0), new(5.6f, 1.05f, 2.1f), Hull);
        Cylinder(root, new(0, 1.7f, 0), .82f, .82f, 2.2f, Structure);
        Box(root, new(0, 2.72f, 0), new(2.0f, .28f, 2.35f), Glass);
        PanelBand(root, new(-4.6f, 1.0f, 1.37f), 6, 1.35f, HullLight);
        if (phase >= 2) for (var side = -1; side <= 1; side += 2) DockingGantries(root, side);
        if (phase >= 3)
            for (var side = -1; side <= 1; side += 2)
            {
                SolarWing(root, new(side * 5.25f, .85f, -4.15f), side, 4.0f, 3.2f);
                for (var bay = -1; bay <= 1; bay++) DockingCradle(root, new(side * (3.6f + (bay + 1) * 1.4f), 1.12f, 3.1f), side);
            }
        if (phase == 4)
        {
            for (var bay = -2; bay <= 2; bay++)
            {
                var x = bay * 2.65f;
                Box(root, new(x, 1.95f, .05f), new(1.65f, .42f, 3.1f), Hull);
                Box(root, new(x, 2.19f, -1.48f), new(1.24f, .16f, .10f), Warning);
                Box(root, new(x, 2.19f, 1.48f), new(1.24f, .16f, .10f), Beacon);
            }
            for (var x = -6; x <= 6; x += 3)
            { Box(root, new(x, 3.15f, 0), new(.13f, 2.1f, .13f), HullLight); Box(root, new(x, 4.12f, 0), new(.55f, .10f, .55f), Beacon); }
        }
    }

    private static void DockingGantries(Node3D root, int side)
    {
        var x = side * 5.6f;
        Box(root, new(x, .62f, 0), new(1.0f, .28f, 11.5f), Hull);
        for (var z = -5; z <= 5; z += 2)
        {
            Box(root, new(x, 1.75f, z), new(.22f, 2.05f, .22f), HullLight);
            Box(root, new(x - side * .52f, 1.82f, z + .62f), new(.14f, 1.75f, .14f), Structure).RotationDegrees = new(0, 0, side * -28);
            Box(root, new(x + side * .52f, 1.82f, z - .62f), new(.14f, 1.75f, .14f), Structure).RotationDegrees = new(0, 0, side * 28);
            Box(root, new(x, 2.78f, z), new(1.25f, .15f, .28f), Structure);
        }
        Box(root, new(x, .96f, -5.35f), new(.32f, .18f, .32f), Beacon); Box(root, new(x, .96f, 5.35f), new(.32f, .18f, .32f), Beacon);
    }

    private static void DockingCradle(Node3D root, Vector3 at, int side)
    {
        Box(root, at, new(1.25f, .18f, 2.4f), Structure);
        Box(root, at + new Vector3(side * .48f, .38f, -.76f), new(.14f, .78f, .14f), HullLight);
        Box(root, at + new Vector3(side * .48f, .38f, .76f), new(.14f, .78f, .14f), HullLight);
        Box(root, at + new Vector3(0, .52f, 0), new(1.02f, .12f, .22f), Beacon);
    }

    private static void CreateLaunchComplex(Node3D root, int phase)
    {
        // A compact radial hub, service arms, dishes, and antennae intentionally contrast with the yard.
        Cylinder(root, new(0, 0, 0), 2.35f, 2.7f, 1.15f, Hull); Cylinder(root, new(0, .88f, 0), 1.65f, 2.15f, 1.05f, Structure); Cylinder(root, new(0, 1.66f, 0), 1.22f, 1.55f, .72f, Glass);
        for (var arm = 0; arm < 4; arm++) { var angle = Mathf.DegToRad(arm * 90 + 45); var p = new Vector3(Mathf.Cos(angle) * 3.5f, .28f, Mathf.Sin(angle) * 3.5f); Box(root, p, new(3.8f, .26f, .72f), Hull).Rotation = new(0, -angle, 0); }
        if (phase >= 2) for (var side = -1; side <= 1; side += 2) { Box(root, new(side * 5.3f, .75f, 0), new(4.8f, .22f, 1.15f), Structure); Box(root, new(side * 7.3f, 1.4f, 0), new(.24f, 1.55f, .24f), HullLight); Box(root, new(side * 7.3f, 2.25f, 0), new(.70f, .12f, .70f), Beacon); }
        if (phase >= 3) for (var side = -1; side <= 1; side += 2) { SolarWing(root, new(side * 3.9f, .35f, -3.1f), side, 3.2f, 2.25f); Box(root, new(side * 3.35f, 1.4f, 2.25f), new(.22f, 1.9f, 3.15f), HullLight); Box(root, new(side * 4.25f, 1.4f, 2.25f), new(.22f, 1.9f, 3.15f), HullLight); Box(root, new(side * 3.8f, 2.2f, 2.25f), new(1.2f, .18f, 3.4f), Structure); }
        if (phase == 4) { Cylinder(root, new(0, 3.0f, 0), .15f, .25f, 2.9f, HullLight); Box(root, new(0, 4.48f, 0), new(1.5f, .13f, 1.5f), Beacon); for (var i = 0; i < 6; i++) { var a = Mathf.DegToRad(i * 60); Box(root, new(Mathf.Cos(a) * 2.65f, 1.9f, Mathf.Sin(a) * 2.65f), new(.35f, .12f, .35f), Warning); } }
    }

    private static void CreateAsteroidNetwork(Node3D root, int phase)
    {
        var rock = Mesh(root, new SphereMesh { Radius = 3.45f, Height = 6.0f, RadialSegments = 16, Rings = 10 }, new(-2.1f, 0, 0), Rock); rock.Scale = new(1.16f, .74f, 1.08f); rock.RotationDegrees = new(17, 22, 31);
        Box(root, new(3.0f, .1f, 0), new(8.4f, .65f, 1.65f), Hull); Box(root, new(3.6f, .68f, 0), new(5.4f, .34f, 2.5f), Structure);
        for (var x = 1; x <= 6; x += 2) Box(root, new(x, .95f, 1.35f), new(1.15f, .22f, .14f), HullLight);
        if (phase >= 2) { Box(root, new(4.3f, 1.65f, 0), new(2.6f, 1.7f, 2.25f), Hull); Cylinder(root, new(5.85f, .0f, -.92f), .56f, .56f, 2.8f, Structure).RotationDegrees = new(90, 0, 0); Cylinder(root, new(5.85f, .0f, .92f), .56f, .56f, 2.8f, Structure).RotationDegrees = new(90, 0, 0); Box(root, new(-.1f, .45f, 0), new(3.25f, .24f, .24f), HullLight).RotationDegrees = new(0, 0, -18); }
        if (phase >= 3) for (var side = -1; side <= 1; side += 2) { SolarWing(root, new(4.65f, .7f, side * 3.0f), 1, 3.2f, 1.8f); Box(root, new(-1.0f, .7f, side * 1.75f), new(2.1f, .26f, .35f), Structure).RotationDegrees = new(0, 0, side * 27); Box(root, new(-1.9f, .18f, side * 2.22f), new(.92f, .62f, .92f), HullLight); }
        if (phase == 4) { for (var i = 0; i < 4; i++) { var x = 2.15f + i * 1.5f; Box(root, new(x, 2.45f, -.86f), new(1.08f, 1.08f, .84f), HullLight); Box(root, new(x, 2.45f, .86f), new(1.08f, 1.08f, .84f), HullLight); Box(root, new(x, 3.05f, 0), new(.74f, .14f, 2.3f), Structure); } Cylinder(root, new(-3.3f, .2f, 0), .14f, .34f, 2.5f, Warning).RotationDegrees = new(0, 0, 72); }
    }

    private static void CreateOrbitalOutpost(Node3D root, int phase)
    {
        Box(root, Vector3.Zero, new(12, .8f, 1.8f), Hull); Cylinder(root, new(0, 1.3f, 0), 1.25f, 1.7f, 2.4f, Structure);
        if (phase >= 2) { SolarWing(root, new(-3.5f, .6f, -2.4f), -1, 3, 2); SolarWing(root, new(3.5f, .6f, -2.4f), 1, 3, 2); }
        if (phase >= 3) PanelBand(root, new(-3.5f, .6f, 1.0f), 5, 1.45f, HullLight); if (phase == 4) Box(root, new(0, 3.15f, 0), new(.36f, 2.6f, .36f), Beacon);
    }

    private static void SolarWing(Node3D root, Vector3 at, int direction, float length, float width)
    { Box(root, at, new(length, .10f, width), Solar); for (var i = 1; i < 4; i++) Box(root, at + new Vector3(0, .08f, -width / 2 + i * width / 4), new(length, .04f, .05f), HullLight); Box(root, at + new Vector3(direction * length / 2, -.15f, 0), new(.14f, .42f, .22f), Structure); }
    private static void PanelBand(Node3D root, Vector3 start, int count, float spacing, Material material)
    { for (var i = 0; i < count; i++) Box(root, start + new Vector3(i * spacing, 0, 0), new(.76f, .32f, .10f), material); }

    // The base materials are immutable templates. Each styled station receives only six local
    // material instances, so simultaneous civilizations cannot tint one another's structures.
    private static void ApplyCivilizationStyle(Node root, CivilizationVisualStyle style)
    {
        var hull = Material(style.HullColor, .72f, .33f);
        var hullLight = Material(style.HullColor.Lightened(.20f), .61f, .28f);
        var structure = Material(style.SecondaryColor, .68f, .40f);
        var glass = Material(style.GlassColor, .50f, .20f);
        var solar = Material(style.SecondaryColor.Darkened(.18f), .48f, .18f);
        var beacon = Material(style.AccentColor, .20f, .30f, true);
        var engine = Material(style.EngineColor, .15f, .34f, true);
        Apply(root);
        void Apply(Node node)
        {
            if (node is MeshInstance3D mesh && mesh.MaterialOverride is Material current)
                mesh.MaterialOverride = ReferenceEquals(current, Hull) ? hull : ReferenceEquals(current, HullLight) ? hullLight :
                    ReferenceEquals(current, Structure) ? structure : ReferenceEquals(current, Glass) ? glass :
                    ReferenceEquals(current, Solar) ? solar : ReferenceEquals(current, Beacon) ? beacon :
                    ReferenceEquals(current, Warning) ? engine : current;
            foreach (var child in node.GetChildren()) Apply(child);
        }
    }
    private static StandardMaterial3D Material(string color, float metallic, float roughness, bool emission = false) => new() { AlbedoColor = new(color), Metallic = metallic, Roughness = roughness, EmissionEnabled = emission, Emission = new(color), EmissionEnergyMultiplier = 1.3f };
    private static StandardMaterial3D Material(Color color, float metallic, float roughness, bool emission = false) => new() { AlbedoColor = color, Metallic = metallic, Roughness = roughness, EmissionEnabled = emission, Emission = color, EmissionEnergyMultiplier = 1.3f };
    private static MeshInstance3D Box(Node3D parent, Vector3 at, Vector3 size, Material material) => Mesh(parent, new BoxMesh { Size = size }, at, material);
    private static MeshInstance3D Cylinder(Node3D parent, Vector3 at, float top, float bottom, float height, Material material) => Mesh(parent, new CylinderMesh { TopRadius = top, BottomRadius = bottom, Height = height, RadialSegments = 20 }, at, material);
    private static MeshInstance3D Mesh(Node3D parent, Mesh mesh, Vector3 at, Material material) { var node = new MeshInstance3D { Mesh = mesh, MaterialOverride = material, Position = at }; parent.AddChild(node); return node; }
}
