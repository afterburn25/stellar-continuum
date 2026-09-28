using System;
using Godot;
using Game.Presentation;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>Reusable near-future ship silhouettes.  Geometry is presentation-only and design IDs select the role.</summary>
public static class ShipGeometry
{
    public static Node3D Create(string designId, CivilizationVisualStyle? style = null, bool highDetail = true)
    {
        var ship = new Node3D { Name = "Ship_" + designId };
        ship.SetMeta("HighDetail", highDetail);
        var p = style ?? CivilizationVisualStyles.Terran;
        var materials = new Materials(p);
        switch (ResolveGeometryDesign(designId))
        {
            case "warp_scout": Scout(ship, materials, highDetail); break;
            case "science_vessel": Science(ship, materials, highDetail); break;
            case "colony_ship": Colony(ship, materials, highDetail); break;
            case "patrol_corvette": Corvette(ship, materials, highDetail); break;
            case "resource_outpost_ship": Outpost(ship, materials, highDetail); break;
            case "bulk_freighter": Freighter(ship, materials, highDetail); break;
            default: Scout(ship, materials, highDetail); break;
        }
        if (highDetail) ApplyMotif(ship, materials, p.Motif);
        return ship;
    }

    private static string ResolveGeometryDesign(string designId)
    {
        if (string.IsNullOrWhiteSpace(designId)) return "warp_scout";
        var key = designId.ToLowerInvariant().Replace('-', '_').Replace(' ', '_');
        if (key.Contains("corvette", StringComparison.Ordinal) || key.Contains("combat", StringComparison.Ordinal) ||
            key.Contains("military", StringComparison.Ordinal) || key.Contains("frigate", StringComparison.Ordinal) ||
            key.Contains("destroyer", StringComparison.Ordinal) || key.Contains("cruiser", StringComparison.Ordinal) ||
            key.Contains("battleship", StringComparison.Ordinal) || key.Contains("carrier", StringComparison.Ordinal) ||
            key.Contains("interdictor", StringComparison.Ordinal)) return "patrol_corvette";
        if (key.Contains("science", StringComparison.Ordinal) || key.Contains("research", StringComparison.Ordinal)) return "science_vessel";
        if (key.Contains("colony", StringComparison.Ordinal)) return "colony_ship";
        if (key.Contains("outpost", StringComparison.Ordinal)) return "resource_outpost_ship";
        if (key.Contains("freight", StringComparison.Ordinal) || key.Contains("logistic", StringComparison.Ordinal)) return "bulk_freighter";
        return key is "warp_scout" or "science_vessel" or "colony_ship" or "patrol_corvette" or
            "resource_outpost_ship" or "bulk_freighter" ? key : "warp_scout";
    }

    public static Node3D Create(FleetRole role, string? designId = null, CivilizationVisualStyle? style = null, bool highDetail = false) =>
        Create(designId ?? role switch
        {
            FleetRole.Scout => "warp_scout", FleetRole.Science => "science_vessel", FleetRole.Colony => "colony_ship",
            FleetRole.Military => "patrol_corvette", FleetRole.Logistics => "bulk_freighter", _ => "warp_scout",
        }, style, highDetail);

    // Ships face negative Z, matching the local-system travel convention. Low LOD retains each role's outline.
    private static void Scout(Node3D r, Materials m, bool detail)
    {
        // The scout is the close camera's most common vessel: a tapered lifting-body
        // silhouette replaces the old rectangular hull while retaining its winged scout motif.
        TaperedHull(r, new(0, 0, .10f), .20f, .68f, 4.25f, m.Hull);
        TaperedHull(r, new(0, .25f, -.42f), .15f, .42f, 2.25f, m.HullLight);
        TaperedHull(r, new(0, .39f, -1.02f), .10f, .31f, 1.25f, m.Glass);
        Wing(r, new(-1.34f, -.04f, .34f), 2.25f, .075f, m.Secondary, -13);
        Wing(r, new(1.34f, -.04f, .34f), 2.25f, .075f, m.Secondary, 13);
        Engines(r, new(0, -.04f, 2.18f), 2, .22f, m);
        if (!detail) return;

        ArmorPlates(r, new(0, .32f, .10f), 1.02f, 2.70f, m);
        SegmentedPanels(r, new(-1.50f, .02f, .38f), new(1.50f, .02f, .38f), m);
        DockingRing(r, new(0, -.05f, .78f), .27f, m.HullLight);
        Dish(r, new(0, .66f, -.35f), .42f, m);
        SensorCluster(r, new(0, .62f, -1.34f), m);
        Mast(r, new(-.47f, .43f, .82f), .78f, m.Accent);
        Lights(r, new(-.55f, .13f, -1.48f), new(.55f, .13f, -1.48f), m);
    }

    private static void Science(Node3D r, Materials m, bool detail)
    {
        Box(r, new(0, 0, .1f), new(1.7f, .6f, 4.6f), m.Hull); Box(r, new(0, .38f, -.45f), new(1.15f, .34f, 1.7f), m.Glass);
        Wing(r, new(-1.85f, 0, .55f), 3.3f, .10f, m.Secondary, 0); Wing(r, new(1.85f, 0, .55f), 3.3f, .10f, m.Secondary, 0);
        Engines(r, new(0, 0, 2.55f), 2, .26f, m); if (detail) { Dish(r, new(0, .84f, -1.3f), .75f, m); Mast(r, new(-.65f, .55f, 1.1f), 1.5f, m.Accent); Mast(r, new(.65f, .55f, 1.1f), 1.2f, m.Accent); }
    }

    private static void Colony(Node3D r, Materials m, bool detail)
    {
        Cylinder(r, new(0, 0, -.85f), 1.28f, 1.28f, 2.0f, m.Hull).RotationDegrees = new(90, 0, 0);
        Box(r, new(0, 0, 1.2f), new(2.15f, 1.15f, 2.8f), m.Secondary); Box(r, new(0, .72f, -.85f), new(1.3f, .13f, 1.3f), m.Glass);
        Wing(r, new(-2.05f, 0, 1.05f), 2.45f, .12f, m.Secondary, 0); Wing(r, new(2.05f, 0, 1.05f), 2.45f, .12f, m.Secondary, 0);
        Engines(r, new(0, 0, 2.82f), 3, .30f, m); if (detail) { for (var z = .3f; z <= 1.8f; z += .75f) Box(r, new(0, .62f, z), new(1.7f, .10f, .09f), m.Accent); Mast(r, new(0, 1.1f, 1.4f), 1.4f, m.HullLight); }
    }

    private static void Corvette(Node3D r, Materials m, bool detail)
    {
        TaperedHull(r, new(0, 0, 0), .36f, .92f, 4.9f, m.Hull);
        Box(r, new(0, .28f, .45f), new(1.65f, .24f, 3.15f), m.Secondary);
        Box(r, new(0, .46f, -.64f), new(.92f, .27f, 1.45f), m.Glass);
        Wing(r, new(-1.55f, -.03f, .72f), 2.2f, .16f, m.Secondary, -18);
        Wing(r, new(1.55f, -.03f, .72f), 2.2f, .16f, m.Secondary, 18);
        Engines(r, new(0, 0, 2.66f), 3, .29f, m);
        if (!detail) return;
        ArmorPlates(r, new(0, .48f, .42f), 1.35f, 2.7f, m);
        Box(r, new(0, .72f, 1.12f), new(.62f, .22f, 1.45f), m.Secondary);
        Turret(r, new(0, .72f, -1.52f), .26f, m);
        Turret(r, new(-.58f, .48f, .28f), .22f, m);
        Turret(r, new(.58f, .48f, .28f), .22f, m);
        SensorCluster(r, new(0, .78f, -.76f), m);
        Lights(r, new(-.84f, .2f, -1.9f), new(.84f, .2f, -1.9f), m);
    }

    private static void Outpost(Node3D r, Materials m, bool detail)
    {
        Box(r, new(0, 0, .55f), new(2.1f, 1.0f, 3.6f), m.Secondary); Cylinder(r, new(0, 0, -1.65f), 1.05f, 1.05f, 1.8f, m.Hull).RotationDegrees = new(90, 0, 0);
        Box(r, new(0, .58f, -1.65f), new(1.25f, .13f, 1.25f), m.Glass); Wing(r, new(-2.1f, 0, .65f), 2.8f, .11f, m.Secondary, 0); Wing(r, new(2.1f, 0, .65f), 2.8f, .11f, m.Secondary, 0);
        Engines(r, new(0, 0, 2.5f), 2, .28f, m); if (detail) { Mast(r, new(-.7f, 1.0f, .5f), 1.6f, m.Accent); Mast(r, new(.7f, 1.0f, .5f), 1.3f, m.Accent); for (var x = -1; x <= 1; x++) Box(r, new(x * .65f, -.72f, .55f), new(.38f, .18f, 2.55f), m.HullLight); }
    }

    private static void Freighter(Node3D r, Materials m, bool detail)
    {
        Box(r, new(0, 0, -1.45f), new(1.75f, .7f, 1.4f), m.Hull); Box(r, new(0, 0, 1.15f), new(2.45f, 1.32f, 4.0f), m.Secondary);
        for (var z = -.1f; z <= 2.3f; z += .8f) Box(r, new(0, .73f, z), new(2.12f, .12f, .12f), m.HullLight);
        Wing(r, new(-2.3f, 0, 1.15f), 2.5f, .11f, m.Secondary, 0); Wing(r, new(2.3f, 0, 1.15f), 2.5f, .11f, m.Secondary, 0);
        Engines(r, new(0, 0, 3.4f), 4, .28f, m); if (detail) { Box(r, new(0, .48f, -1.5f), new(1.15f, .22f, .52f), m.Glass); for (var x = -1; x <= 1; x++) Box(r, new(x * .75f, 1.0f, 1.15f), new(.10f, .35f, 3.7f), m.Accent); }
    }

    // Family hooks add a restrained surface-language cue without compromising role recognition.
    private static void ApplyMotif(Node3D r, Materials m, string motif)
    {
        switch (motif)
        {
            case "pressure-shell":
                Cylinder(r, new(0, .22f, .15f), .18f, .18f, 3.7f, m.Accent).RotationDegrees = new(90, 0, 0);
                break;
            case "armored":
                for (var z = -1.2f; z <= 1.2f; z += .6f) Box(r, new(0, .48f, z), new(2.2f, .12f, .18f), m.HullLight);
                break;
            case "crystalline":
                var fin = Box(r, new(0, 1.0f, .5f), new(.12f, 1.8f, 1.5f), m.Accent); fin.RotationDegrees = new(0, 0, 28);
                break;
            case "neutral":
                Box(r, new(0, .48f, .25f), new(1.05f, .08f, 2.1f), m.HullLight);
                break;
        }
    }

    private static void Wing(Node3D r, Vector3 at, float length, float thickness, Material mat, float pitch) { var wing = Box(r, at, new(length, thickness, .9f), mat); wing.RotationDegrees = new(0, 0, pitch); }
    private static void Engines(Node3D r, Vector3 at, int count, float radius, Materials m)
    {
        for (var i = 0; i < count; i++)
        {
            var x = (i - (count - 1) / 2f) * radius * 2.55f;
            Cylinder(r, at + new Vector3(x, 0, 0), radius, radius * 1.18f, .42f, m.Hull).RotationDegrees = new(90, 0, 0);
            var nozzle = Cylinder(r, at + new Vector3(x, 0, .24f), radius * .64f, radius * .64f, .08f, m.Engine);
            nozzle.Name = "EngineNozzle"; nozzle.RotationDegrees = new(90, 0, 0);
            var plume = Cylinder(r, at + new Vector3(x, 0, .73f), radius * .12f, radius * .56f, .92f, m.Exhaust);
            plume.Name = "EnginePlume"; plume.RotationDegrees = new(90, 0, 0);
            // A short, faceted nacelle and a bright nozzle lip give all focused roles
            // readable propulsion hardware without adding work to their low-detail forms.
            if (r.GetMeta("HighDetail", false).AsBool())
            {
                TaperedHull(r, at + new Vector3(x, 0, .02f), radius * 1.18f, radius * .82f, .76f, m.Secondary);
                DockingRing(r, at + new Vector3(x, 0, .48f), radius * .78f, m.Engine);
            }
        }
    }
    private static void Dish(Node3D r, Vector3 at, float radius, Materials m) { Cylinder(r, at, radius, .08f, .17f, m.HullLight); Mast(r, at + new Vector3(0, .42f, 0), .7f, m.Accent); }
    private static void Mast(Node3D r, Vector3 at, float height, Material m) => Box(r, at, new(.08f, height, .08f), m);
    private static void Lights(Node3D r, Vector3 a, Vector3 b, Materials m) { Box(r, a, new(.14f, .10f, .14f), m.Accent); Box(r, b, new(.14f, .10f, .14f), m.Accent); }
    private static void ArmorPlates(Node3D r, Vector3 at, float width, float length, Materials m)
    {
        for (var index = 0; index < 4; index++)
        {
            var z = at.Z - length * .42f + index * length * .28f;
            Box(r, new(at.X, at.Y, z), new(width * (1.0f - index * .08f), .055f, .13f), index % 2 == 0 ? m.HullLight : m.Secondary);
        }
    }

    private static void SegmentedPanels(Node3D r, Vector3 left, Vector3 right, Materials m)
    {
        foreach (var anchor in new[] { left, right })
        for (var segment = 0; segment < 3; segment++)
        {
            var x = anchor.X + MathF.Sign(anchor.X) * segment * .47f;
            Box(r, new(x, anchor.Y, anchor.Z + segment * .08f), new(.39f, .035f, .72f), m.Radiator);
            Box(r, new(x, anchor.Y + .028f, anchor.Z + segment * .08f), new(.025f, .022f, .76f), m.Accent);
        }
    }

    private static void DockingRing(Node3D r, Vector3 at, float radius, Material mat)
    {
        var ring = Add(r, new TorusMesh { InnerRadius = radius * .68f, OuterRadius = radius, Rings = 8, RingSegments = 12 }, at, mat);
        ring.RotationDegrees = new(90, 0, 0);
    }

    private static void SensorCluster(Node3D r, Vector3 at, Materials m)
    {
        Box(r, at, new(.18f, .12f, .18f), m.Accent);
        Box(r, at + new Vector3(-.24f, -.06f, .07f), new(.20f, .05f, .34f), m.HullLight);
        Box(r, at + new Vector3(.24f, -.06f, .07f), new(.20f, .05f, .34f), m.HullLight);
    }

    private static void Turret(Node3D r, Vector3 at, float radius, Materials m)
    {
        var mount = Cylinder(r, at, radius * .72f, radius, radius * .34f, m.HullLight);
        mount.Name = "WeaponTurret";
        var barrel = Cylinder(r, at + new Vector3(0, radius * .26f, -radius * 1.55f),
            radius * .13f, radius * .16f, radius * 2.5f, m.Accent);
        barrel.Name = "WeaponBarrel";
        barrel.RotationDegrees = new(90, 0, 0);
    }

    private static MeshInstance3D TaperedHull(Node3D p, Vector3 at, float noseRadius, float tailRadius, float length, Material mat)
    {
        // +90° around X maps the source cylinder's +Y top to +Z. Ships face -Z,
        // so the source bottom is the nose and the larger top remains at the engine end.
        var hull = Add(p, new CylinderMesh { TopRadius = tailRadius, BottomRadius = noseRadius, Height = length, RadialSegments = 8 }, at, mat);
        hull.RotationDegrees = new(90, 0, 0);
        return hull;
    }

    private static MeshInstance3D Box(Node3D p, Vector3 at, Vector3 size, Material mat) => Add(p, new BoxMesh { Size = size }, at, mat);
    private static MeshInstance3D Cylinder(Node3D p, Vector3 at, float top, float bottom, float height, Material mat) => Add(p, new CylinderMesh { TopRadius = top, BottomRadius = bottom, Height = height, RadialSegments = 16 }, at, mat);
    private static MeshInstance3D Add(Node3D p, Mesh mesh, Vector3 at, Material mat) { var node = new MeshInstance3D { Mesh = mesh, MaterialOverride = mat, Position = at }; p.AddChild(node); return node; }

    private sealed class Materials
    {
        public readonly StandardMaterial3D Hull, HullLight, Secondary, Radiator, Accent, Engine, Exhaust, Glass;
        public Materials(CivilizationVisualStyle p) { Hull = Make(p.HullColor, .72f, .32f); HullLight = Make(p.HullColor.Lightened(.20f), .58f, .28f); Secondary = Make(p.SecondaryColor, .66f, .42f); Radiator = Make(p.SecondaryColor.Darkened(.28f), .76f, .72f); Accent = Make(p.AccentColor, .35f, .28f, true); Engine = Make(p.EngineColor, .12f, .22f, true); Exhaust = MakeExhaust(p.EngineColor); Glass = Make(p.GlassColor, .46f, .18f); }
        private static StandardMaterial3D Make(Color color, float metal, float rough, bool glow = false) => new() { AlbedoColor = color, Metallic = metal, Roughness = rough, EmissionEnabled = glow, Emission = color, EmissionEnergyMultiplier = glow ? 1.8f : 1f };
        private static StandardMaterial3D MakeExhaust(Color color) => new() { AlbedoColor = new Color(color, .44f), Transparency = BaseMaterial3D.TransparencyEnum.Alpha, ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded, EmissionEnabled = true, Emission = color.Lightened(.28f), EmissionEnergyMultiplier = 7.5f };
    }
}
