using System;
using System.Collections.Generic;
using Godot;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>
/// Globe materials for orbital and descent presentation. Global mapping uses dedicated
/// equirectangular maps. Earth restores its established view-facing photograph on native
/// depth geometry; the observed hemisphere is never presented as a global surface map.
/// </summary>
public static class PlanetMaterial3D
{
    private static readonly Dictionary<string, Texture2D?> Maps = new(StringComparer.Ordinal);

    public static Material Create(SystemSpatialBodyMarker body)
    {
        ArgumentNullException.ThrowIfNull(body);
        var known = body.HasDetailedEnvironment && body.VisualClass is not
            (SystemSpatialBodyVisualClass.UnknownPlanet or SystemSpatialBodyVisualClass.UnknownMoon);
        var earthPhoto = known && body.SurfaceKey == "earth" ? SolBodyMaterials.LoadColorTexture("earth") : null;
        var photographicEarth = earthPhoto is not null;
        var map = known && !photographicEarth ? LoadEquirectangularMap(body.SurfaceKey) : null;
        var material = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/planet_surface_3d.gdshader") };
        material.SetShaderParameter("photographic_earth", photographicEarth);
        if (earthPhoto is not null)
        {
            // Restore the previously approved observed hemisphere and its natural color.
            // It is a view-facing photographic treatment, not a global surface map.
            material.SetShaderParameter("earth_photo", earthPhoto);
            material.SetShaderParameter("earth_photo_disc", SolBodyMaterials.GetSourceDisc("earth"));
        }
        // Saturn's procedural bands use a restrained cream base rather than the generic
        // brown gas-giant value. This still applies only after the observer has detailed data.
        var baseColor = body.SurfaceKey == "saturn" ? new Color("d2bb87") : CelestialBodyMaterials.ResolveColor(body.VisualClass);
        material.SetShaderParameter("base_color", known ? baseColor : new Color("3b4650"));
        material.SetShaderParameter("mapped", map is not null);
        material.SetShaderParameter("known", known);
        material.SetShaderParameter("has_oceans", body.HasIllustratedOcean);
        material.SetShaderParameter("gas_giant", known && body.VisualClass is SystemSpatialBodyVisualClass.GasGiant or SystemSpatialBodyVisualClass.IceGiant);
        material.SetShaderParameter("saturn", known && body.SurfaceKey == "saturn");
        // The approved photo uses view-disc coordinates, while the night map uses
        // spherical UVs. Do not paint unregistered city lights across that photograph.
        var inhabitedEarth = known && body.HasCityLights && body.SurfaceKey == "earth" && !photographicEarth;
        material.SetShaderParameter("city_lights", inhabitedEarth);
        if (inhabitedEarth)
            material.SetShaderParameter("night_map", GD.Load<Texture2D>("res://assets/visual/sol/earth-night-map.jpg"));
        material.SetShaderParameter("seed", (float)(body.BodyId % 1024));
        if (map is not null) material.SetShaderParameter("surface_map", map);
        return material;
    }

    public static Color AtmosphereColor(SystemSpatialBodyMarker body) => body.VisualClass switch
    {
        SystemSpatialBodyVisualClass.Oceanic => new Color("6da6d6"),
        SystemSpatialBodyVisualClass.IceGiant => new Color("9dcbd2"),
        SystemSpatialBodyVisualClass.GasGiant => new Color("d5bf91"),
        SystemSpatialBodyVisualClass.Frozen => new Color("bdd9e7"),
        SystemSpatialBodyVisualClass.HotRocky => new Color("dbb17a"),
        _ when body.SurfaceKey == "venus" => new Color("ddd0aa"),
        _ => new Color("9bb4c7"),
    };

    public static bool HasAtmosphere(SystemSpatialBodyMarker body) => body.HasDetailedEnvironment &&
        body.VisualClass is not (SystemSpatialBodyVisualClass.UnknownPlanet or SystemSpatialBodyVisualClass.UnknownMoon or SystemSpatialBodyVisualClass.Moon) &&
        body.Atmosphere is { } atmosphere && atmosphere != PlanetaryAtmosphereRegime.Vacuum;

    private static Texture2D? LoadEquirectangularMap(string? key)
    {
        if (!SolBodyMaterials.IsCanonicalKey(key)) return null;
        if (Maps.TryGetValue(key!, out var cached)) return cached;
        foreach (var extension in new[] { ".jpg", ".png", ".webp" })
        {
            var path = $"res://assets/visual/sol/{key}-map{extension}";
            if (!ResourceLoader.Exists(path)) continue;
            cached = GD.Load<Texture2D>(path)
                ?? throw new InvalidOperationException($"Planet map could not load: {path}");
            Maps[key!] = cached;
            return cached;
        }

        // A zero source-disc declaration denotes an existing full-surface source. It is the
        // sole legacy fallback allowed on a sphere; all cropped source discs are deliberately ignored.
        if (SolBodyMaterials.GetSourceDisc(key) == Vector4.Zero)
            cached = SolBodyMaterials.LoadColorTexture(key);
        Maps[key!] = cached;
        return cached;
    }
}
