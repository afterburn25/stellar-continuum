using System;
using Game.Simulation.Models;

namespace Game.Presentation.PlanetIdentity;

public sealed record PlanetPresentation(PlanetClassification Classification, PlanetVisualVariant Variant,
    string OrbitalFamily, string SurfaceFamily, ulong Identity, float ShaderSeed, PlanetVisualModifier Modifiers,
    bool HasAtmosphere, bool CanShowSolidSurface, float AtmosphereDensity, float ThermalEmission,
    float Development, string? CanonicalKey, SystemSkyProfile Sky);

public static class PlanetPresentationResolver
{
    public static PlanetPresentation WithDevelopment(PlanetPresentation profile, double population)
    {
        var strength = DevelopmentStrength(population);
        var modifiers = profile.Modifiers & ~PlanetVisualModifier.Developed;
        if (strength > 0) modifiers |= PlanetVisualModifier.Developed;
        return profile with { Development = strength, Modifiers = modifiers };
    }
    // Always call after observer filtering. The false branch deliberately touches no hidden facts.
    public static PlanetPresentation? Resolve(PlanetaryBodyState body, StarSystemState system, long campaignSeed,
        bool fullySurveyed = true, double knownPopulationMillions = 0, double? radialFraction = null)
    {
        if (!fullySurveyed) return null;
        ArgumentNullException.ThrowIfNull(body); ArgumentNullException.ThrowIfNull(system);
        if (body.SystemId != system.Id) throw new ArgumentException("Planet presentation belongs to another system.");
        var canonical = system.Id == 0 && system.CatalogPresetId == "sol-v1";
        var classification = PlanetClassifier.Classify(body, canonical); var e = body.Environment;
        var hash = VisualHash.Start(campaignSeed, system.Id, body.Id);
        foreach (var value in new[] { body.RadiusEarth, body.MassEarth, e.GravityG, e.TemperatureKelvin, e.PressureKPa, e.RadiationHazard, body.OrbitalEccentricity, body.OrbitalInclinationDegrees }) hash = VisualHash.Add(hash, value);
        foreach (var value in new[] { (int)body.Kind, (int)e.Atmosphere, (int)e.AvailableSolvent, e.HasSolidSurface ? 1 : 0, e.IsImmersedEnvironment ? 1 : 0, (int)classification.Class }) hash = VisualHash.Add(hash, (ulong)value);
        var family = PlanetVisualCatalog.Get(classification.Class);
        var variant = family.Variants[(int)(hash % (ulong)family.Variants.Count)];
        var atmosphere = e.Atmosphere != PlanetaryAtmosphereRegime.Vacuum && e.PressureKPa >= .1;
        var modifiers = PlanetVisualModifier.None;
        if (!atmosphere) modifiers |= PlanetVisualModifier.ClearAtmosphere | PlanetVisualModifier.Cratered;
        if (atmosphere && variant.Clouds > .65) modifiers |= PlanetVisualModifier.HeavyCloud | PlanetVisualModifier.HighCloudDeck;
        else if (atmosphere && variant.Clouds > .1) modifiers |= PlanetVisualModifier.SparseCloud;
        if (atmosphere && variant.Haze > .35) modifiers |= PlanetVisualModifier.Hazy;
        if (classification.Class is PlanetClass.Ocean or PlanetClass.HighPressureOcean) modifiers |= PlanetVisualModifier.GlobalOcean;
        if (variant.Name == "Island Chains") modifiers |= PlanetVisualModifier.IslandChains;
        if (e.TemperatureKelvin < 220 && e.AvailableSolvent != PlanetarySolventRegime.None) modifiers |= PlanetVisualModifier.FracturedIce | PlanetVisualModifier.PolarCaps;
        if (classification.Class == PlanetClass.CryogenicHydrocarbon) modifiers |= PlanetVisualModifier.HydrocarbonHaze;
        if (classification.Class is PlanetClass.Desert or PlanetClass.Arid) modifiers |= PlanetVisualModifier.Dusty;
        if (variant.Clouds > .7 && atmosphere) modifiers |= PlanetVisualModifier.StormHeavy;
        if (classification.Class == PlanetClass.Volcanic) modifiers |= PlanetVisualModifier.ThermalGlow | PlanetVisualModifier.DarkBasalt;
        if (e.RadiationHazard > .6) modifiers |= PlanetVisualModifier.RadiationLit;
        if (atmosphere && e.RadiationHazard > .3) modifiers |= PlanetVisualModifier.Auroral;
        if (body.HasAnomaly) modifiers |= PlanetVisualModifier.Anomaly;
        string? key = null;
        if (canonical && ((body.Id is >= 1 and <= 8 && body.Name == new[] { "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune" }[body.Id - 1]) || body.Id == 9 && body.Name == "Moon" && body.ParentBodyId == 3)) key = body.Name.ToLowerInvariant();
        if (key == "saturn" || !canonical && classification.Class is PlanetClass.GasGiant or PlanetClass.IceGiant && variant.Id.EndsWith("05", StringComparison.Ordinal)) modifiers |= PlanetVisualModifier.Ringed;
        var development = DevelopmentStrength(knownPopulationMillions);
        if (development > 0) modifiers |= PlanetVisualModifier.Developed;
        var sky = SystemSkyResolver.Resolve(campaignSeed, system.Id, system.StellarClass, system.SecondaryStellarClass, system.TertiaryStellarClass, system.Archetype, radialFraction);
        return new(classification, variant, family.OrbitalFamily, e.HasSolidSurface ? family.SurfaceFamily : "none", hash,
            (float)(hash % 1000003UL) / 113f, modifiers, atmosphere, e.HasSolidSurface,
            atmosphere ? (float)Math.Clamp(Math.Log10(1 + e.PressureKPa) / 4, .03, 1) : 0,
            (float)Math.Clamp((e.TemperatureKelvin - 1000) / 700, 0, 1), development, key, sky);
    }
    private static float DevelopmentStrength(double population) => double.IsFinite(population) && population > 0
        ? Math.Max(.02f, (float)Math.Round(Math.Clamp(Math.Log10(1 + population) / 4, 0, 1) * 50) / 50) : 0;
}
