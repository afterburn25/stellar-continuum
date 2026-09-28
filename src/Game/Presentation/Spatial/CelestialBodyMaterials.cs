using System;
using System.Collections.Generic;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>Cached GPU materials for observer-safe body presentation.</summary>
public static class CelestialBodyMaterials
{
    public const string PlanetShaderPath = "res://assets/visual/shaders/celestial_planet.gdshader";
    private readonly record struct Appearance(bool IsKnown, bool HasAtmosphere, SystemSpatialBodyVisualClass VisualClass, string? SourceKey);
    private sealed record MaterialEntry(Appearance Appearance, ShaderMaterial Material);
    private static readonly Dictionary<int, MaterialEntry> PlanetMaterials = new();
    private static Shader? _planetShader;
    private static Shader? _starShader;
    private static readonly Dictionary<Color, ShaderMaterial> StarMaterials = new();
    public const float StarExtentMultiplier = 2.20f;
    private static Texture2D? _whiteTexture;

    public static Texture2D WhiteTexture => _whiteTexture ??= new GradientTexture2D
    {
        Width = 2, Height = 2,
        Gradient = new Gradient { Colors = new[] { Colors.White, Colors.White } },
    };

    /// <summary>Use a rectangle 2.20 times the desired photosphere diameter to contain the corona.</summary>
    public static ShaderMaterial GetStarMaterial(Color color)
    {
        if (StarMaterials.TryGetValue(color, out var cached)) return cached;
        _starShader ??= GD.Load<Shader>("res://assets/visual/shaders/celestial_star.gdshader")
            ?? throw new InvalidOperationException("Cinematic star shader could not be loaded.");
        var material = new ShaderMaterial { Shader = _starShader };
        material.SetShaderParameter("star_color", color);
        StarMaterials.Add(color, material);
        return material;
    }

    public static ShaderMaterial GetPlanetMaterial(SystemSpatialBodyMarker body)
    {
        ArgumentNullException.ThrowIfNull(body);
        var known = body.HasDetailedEnvironment
            && body.VisualClass is not (SystemSpatialBodyVisualClass.UnknownPlanet or SystemSpatialBodyVisualClass.UnknownMoon);
        // SurfaceKey is already observer-filtered by the projection; independently
        // require detailed knowledge here so a stale/malformed marker cannot leak a map.
        var sourceKey = known && SolBodyMaterials.IsCanonicalKey(body.SurfaceKey) ? body.SurfaceKey : null;
        var hasAtmosphere = known && body.Atmosphere is not null
            && body.Atmosphere != Game.Simulation.Models.PlanetaryAtmosphereRegime.Vacuum;
        var appearance = new Appearance(known, hasAtmosphere, body.VisualClass, sourceKey);
        if (PlanetMaterials.TryGetValue(body.BodyId, out var entry) && entry.Appearance == appearance)
        {
            UpdateLighting(entry.Material, new Vector2(-body.OffsetX, -body.OffsetY));
            return entry.Material;
        }

        _planetShader ??= GD.Load<Shader>(PlanetShaderPath)
            ?? throw new InvalidOperationException($"Planet shader could not be loaded: {PlanetShaderPath}");
        // Keep an existing per-body material when observer data refreshes. TextureRect
        // references stay valid and no shader is recreated during camera animation.
        var material = entry?.Material ?? new ShaderMaterial { Shader = _planetShader };
        var source = sourceKey is null ? null : SolBodyMaterials.LoadColorTexture(sourceKey);
        var gas = known && body.VisualClass is SystemSpatialBodyVisualClass.GasGiant or SystemSpatialBodyVisualClass.IceGiant;
        var ocean = known && body.HasIllustratedOcean;
        material.SetShaderParameter("surface_texture", source ?? WhiteTexture);
        material.SetShaderParameter("has_source", source is not null);
        material.SetShaderParameter("known_surface", known);
        material.SetShaderParameter("source_disc", SolBodyMaterials.GetSourceDisc(sourceKey));
        material.SetShaderParameter("surface_color", known ? ResolveColor(body.VisualClass) : new Color("394752"));
        material.SetShaderParameter("is_gas", gas);
        material.SetShaderParameter("is_ocean", ocean);
        material.SetShaderParameter("surface_seed", (body.BodyId & 255) * 0.137f);
        material.SetShaderParameter("atmosphere_color", ocean ? new Color("649fcf") :
            sourceKey == "venus" ? new Color("d8cfab") :
            body.VisualClass == SystemSpatialBodyVisualClass.IceGiant ? new Color("8bbbc4") : new Color("c5b598"));
        material.SetShaderParameter("atmosphere_strength", !hasAtmosphere ? 0.0f : ocean ? 0.20f :
            sourceKey == "venus" ? 0.15f : gas ? 0.12f : 0.0f);
        UpdateLighting(material, new Vector2(-body.OffsetX, -body.OffsetY));
        PlanetMaterials[body.BodyId] = new MaterialEntry(appearance, material);
        return material;
    }

    public static void UpdateLighting(ShaderMaterial material, Vector2 towardStar)
    {
        var direction = towardStar.LengthSquared() > 0.0001f ? towardStar.Normalized() : new Vector2(-0.8f, -0.4f);
        material.SetShaderParameter("light_direction", new Vector3(direction.X * 0.86f, direction.Y * 0.86f, 0.54f).Normalized());
    }

    /// <summary>Release the cache owner only; Godot nodes may still reference the material safely.</summary>
    public static void ReleasePlanetMaterial(int bodyId) => PlanetMaterials.Remove(bodyId);

    public static Color ResolveColor(SystemSpatialBodyVisualClass visualClass) => visualClass switch
    {
        SystemSpatialBodyVisualClass.Oceanic => new Color("386e93"),
        SystemSpatialBodyVisualClass.Frozen => new Color("a0b7c5"),
        SystemSpatialBodyVisualClass.HotRocky => new Color("a47b59"),
        SystemSpatialBodyVisualClass.GasGiant => new Color("b8a07f"),
        SystemSpatialBodyVisualClass.IceGiant => new Color("6a9fab"),
        SystemSpatialBodyVisualClass.Moon => new Color("8b9098"),
        SystemSpatialBodyVisualClass.Rocky => new Color("8f897f"),
        _ => new Color("394752"),
    };
}
