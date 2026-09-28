using System;
using System.Collections.Generic;
using System.Linq;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Presentation;

public partial class PlanetSurfaceView
{
    private const float SkyCompanionDistance = 2500f;
    // 10.91 m at 2,500 m gives a 0.50-degree apparent diameter, matching Earth's Moon.
    private const float SkyCompanionRadius = 10.91f;
    private readonly Dictionary<int, MeshInstance3D> _skyCompanions = new();
    private readonly Dictionary<int, string> _skyCompanionVisualKeys = new();

    private void RefreshSkyCompanions()
    {
        if (!_built || !IsOpen) { ClearSkyCompanions(); return; }
        if (ReadSkyCompanions is null) { ClearSkyCompanions(); return; }

        // The read model supplies surveyed moons of the active surface body only. No
        // decorative companion is invented when orbital knowledge contains none.
        var moons = ReadSkyCompanions().OrderBy(moon => moon.BodyId).ToArray();
        var ids = moons.Select(moon => moon.BodyId).ToHashSet();
        foreach (var stale in _skyCompanions.Keys.Where(id => !ids.Contains(id)).ToArray())
            RemoveSkyCompanion(stale);

        foreach (var moon in moons)
        {
            var visualKey = $"{moon.SurfaceKey}|{moon.VisualClass}|{moon.HasDetailedEnvironment}";
            if (!_skyCompanions.TryGetValue(moon.BodyId, out var mesh))
            {
                mesh = new MeshInstance3D
                {
                    Name = $"SkyMoon_{moon.BodyId}",
                    Mesh = new SphereMesh
                    {
                        Radius = SkyCompanionRadius, Height = SkyCompanionRadius * 2,
                        RadialSegments = 96, Rings = 48,
                    },
                    CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
                };
                _world.AddChild(mesh);
                _skyCompanions[moon.BodyId] = mesh;
            }
            if (!_skyCompanionVisualKeys.TryGetValue(moon.BodyId, out var previousKey) || previousKey != visualKey)
            {
                mesh.MaterialOverride = CreateSkyCompanionMaterial(moon);
                mesh.RotationDegrees = new Vector3(0, (moon.BodyId * 47) % 360, 0);
                _skyCompanionVisualKeys[moon.BodyId] = visualKey;
            }

            var direction = SkyCompanionDirection(moon).Normalized();
            // A moon below the local astronomical horizon must not shine through terrain.
            mesh.Visible = direction.Y > .015f;
            mesh.Position = _camera.Position + direction * SkyCompanionDistance;
            if (mesh.MaterialOverride is ShaderMaterial material)
            {
                var towardSun = _sun.GlobalTransform.Basis.Z.Normalized();
                var atmosphericDaylight = SurfaceVisualClass == "airless" ? 1f :
                    Mathf.Lerp(.42f, .92f, Mathf.Clamp((-towardSun.Y + .16f) / .72f, 0, 1));
                material.SetShaderParameter("sun_direction_world", towardSun);
                material.SetShaderParameter("sky_visibility", atmosphericDaylight);
            }
        }
    }

    private static ShaderMaterial CreateSkyCompanionMaterial(SystemSpatialBodyMarker moon)
    {
        var material = new ShaderMaterial
        {
            Shader = GD.Load<Shader>("res://assets/visual/shaders/surface_sky_moon.gdshader")
                ?? throw new InvalidOperationException("Surface moon shader could not be loaded."),
        };
        var mapped = moon.HasDetailedEnvironment && moon.SurfaceKey == "moon" &&
            ResourceLoader.Exists("res://assets/visual/sol/moon-map.jpg");
        material.SetShaderParameter("mapped", mapped);
        material.SetShaderParameter("base_color", moon.SurfaceKey == "moon"
            ? new Color("bcbab1") : new Color("aeb9c1"));
        if (mapped)
            material.SetShaderParameter("surface_map", GD.Load<Texture2D>("res://assets/visual/sol/moon-map.jpg"));
        return material;
    }

    private static Vector3 SkyCompanionDirection(SystemSpatialBodyMarker moon)
    {
        if (moon.SurfaceKey == "moon") return new Vector3(-.57f, .32f, -.75f);
        var azimuth = (moon.BodyId * 2.3999632f) % MathF.Tau;
        var altitude = -.12f + ((moon.BodyId * 37) % 9) * .075f;
        return new Vector3(MathF.Cos(azimuth), altitude, MathF.Sin(azimuth));
    }

    private void RemoveSkyCompanion(int bodyId)
    {
        if (_skyCompanions.Remove(bodyId, out var companion)) companion.QueueFree();
        _skyCompanionVisualKeys.Remove(bodyId);
    }

    private void ClearSkyCompanions()
    {
        foreach (var companion in _skyCompanions.Values) companion.QueueFree();
        _skyCompanions.Clear();
        _skyCompanionVisualKeys.Clear();
    }
}
