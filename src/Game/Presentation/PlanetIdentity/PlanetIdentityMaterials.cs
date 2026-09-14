using System;
using Godot;

namespace Game.Presentation.PlanetIdentity;

/// <summary>Material instances belong to the visible scene. Godot shares the three tiny shaders;
/// there is no per-galaxy texture cache and no generated full-resolution texture per body.</summary>
public static class PlanetIdentityMaterials
{
    public static ShaderMaterial Orbit(PlanetPresentation p)
    {
        var material = new ShaderMaterial { Shader = GD.Load<Shader>(PlanetVisualCatalog.OrbitalShader) };
        Configure(material, p);
        material.SetShaderParameter("atmosphere_color", new Color(p.Variant.Atmosphere));
        material.SetShaderParameter("star_color", new Color(p.Sky.Primary.Color));
        material.SetShaderParameter("secondary_strength", p.Sky.Secondary?.Intensity * .13f ?? 0);
        material.SetShaderParameter("secondary_color", new Color(p.Sky.Secondary?.Color ?? "ffffff"));
        material.SetShaderParameter("clouds", p.HasAtmosphere ? p.Variant.Clouds : 0);
        material.SetShaderParameter("haze", p.HasAtmosphere ? p.AtmosphereDensity : 0);
        material.SetShaderParameter("ice", p.Modifiers.HasFlag(PlanetVisualModifier.PolarCaps) ? 1f :
            p.Classification.Class == PlanetClass.Terran ? .35f : 0);
        material.SetShaderParameter("giant", !p.CanShowSolidSurface);
        material.SetShaderParameter("development", p.Development);
        material.SetShaderParameter("variant_index", VariantIndex(p));
        var path = "res://assets/visual/planets/orbital/" + p.Variant.Id + "-albedo.png";
        var available = ResourceLoader.Exists(path);
        material.SetShaderParameter("approved_map_available", available);
        if (available) material.SetShaderParameter("approved_map", GD.Load<Texture2D>(path));
        return material;
    }
    public static void Configure(ShaderMaterial material, PlanetPresentation p)
    {
        var index = VariantIndex(p);
        // Authored mineral palettes receive restrained albedo differences; morphology also
        // varies through independent patterns, scale, storms and the full physical identity.
        var tint = new Color[] { new("e6b5a0"), new("e8d6a5"), new("bbc9d3"), new("777f87"), new("d1c5b0") }[index];
        material.SetShaderParameter("land_color", new Color(p.Variant.Land).Lerp(tint, .08f + index * .018f));
        material.SetShaderParameter("rock_color", new Color(p.Variant.Rock).Lerp(tint, .06f));
        material.SetShaderParameter("ocean_color", new Color(p.Variant.Ocean));
        material.SetShaderParameter("seed", p.ShaderSeed);
        material.SetShaderParameter("pattern", p.Variant.Pattern);
        material.SetShaderParameter("water", p.Variant.OceanCoverage);
        material.SetShaderParameter("emission_strength", p.ThermalEmission);
        material.SetShaderParameter("roughness", p.Variant.Roughness);
        material.SetShaderParameter("ocean_world", p.Classification.Class is PlanetClass.Ocean or PlanetClass.HighPressureOcean);
        if (material.Shader.ResourcePath == PlanetVisualCatalog.SurfaceShader)
            material.SetShaderParameter("mineral_detail", GD.Load<Texture2D>("res://assets/visual/planets/modifiers/mineral-detail-packed.png"));
    }
    public static void Sky(ShaderMaterial material, SystemSkyProfile sky, PlanetPresentation? planet = null)
    {
        material.Shader = GD.Load<Shader>(PlanetVisualCatalog.SkyShader);
        material.SetShaderParameter("seed", (float)(sky.Identity % 1000003) / 113f);
        material.SetShaderParameter("nebula_color", new Color(sky.NebulaColor));
        material.SetShaderParameter("dust_color", new Color(sky.DustColor));
        material.SetShaderParameter("star_density", sky.StarDensity / 1000f);
        material.SetShaderParameter("nebula_visibility", sky.NebulaVisibility);
        material.SetShaderParameter("dust", sky.Dust);
        material.SetShaderParameter("star_color", new Color(sky.Primary.Color));
        material.SetShaderParameter("secondary_color", new Color(sky.Secondary?.Color ?? "ffffff"));
        material.SetShaderParameter("tertiary_color", new Color(sky.Tertiary?.Color ?? "ffffff"));
        material.SetShaderParameter("companions", (sky.Secondary is null ? 0f : 1f) + (sky.Tertiary is null ? 0f : 1f));
        material.SetShaderParameter("surface_view", planet is not null);
        material.SetShaderParameter("atmosphere", planet is { HasAtmosphere: true } ? Math.Clamp(planet.AtmosphereDensity * 1.85f, 0, .985f) : 0);
        material.SetShaderParameter("cloud_cover", planet is { HasAtmosphere: true } ? planet.Variant.Clouds : 0);
        material.SetShaderParameter("sun_size", sky.Primary.Class == Game.Simulation.Models.StellarPrimaryClass.BlackHole ? 0 :
            sky.Primary.ApparentRadius * MathF.PI / 180f * .5f);
        if (planet is not null) material.SetShaderParameter("atmosphere_color", new Color(planet.Variant.Atmosphere));
    }
    private static int VariantIndex(PlanetPresentation p) => int.Parse(p.Variant.Id[^2..], System.Globalization.CultureInfo.InvariantCulture) - 1;
}
