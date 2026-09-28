using System;
using System.Globalization;
using Game.Simulation.Models;

namespace Game.Presentation.PlanetIdentity;

public enum PlanetClass
{
    Terran, Ocean, HighPressureOcean, Desert, Arid, Ice, CryogenicHydrocarbon,
    AirlessRocky, Greenhouse, ReducingAtmosphere, Toxic, HighGravitySuperEarth,
    Volcanic, Exotic, GasGiant, IceGiant, DwarfPlanet, LargeMoon, BarrenMoon, IceMoon
}

[Flags]
public enum PlanetVisualModifier : ulong
{
    None = 0, StormHeavy = 1, ClearAtmosphere = 2, Dusty = 4, Cratered = 8, Ringed = 16,
    ThermalGlow = 32, PolarCaps = 64, HeavyCloud = 128, SparseCloud = 256, GlobalOcean = 512,
    IslandChains = 1024, FracturedIce = 2048, Auroral = 4096, RadiationLit = 8192,
    Hazy = 16384, DarkBasalt = 32768, HydrocarbonHaze = 65536, HighCloudDeck = 131072,
    Anomaly = 262144, Developed = 524288
}

public sealed record PlanetClassification(PlanetClass Class, string Name, string Reason);

/// <summary>Derived interpretation only. Never writes a physical fact or decides species suitability.</summary>
public static class PlanetClassifier
{
    public static PlanetClassification Classify(PlanetaryBodyState body, bool canonicalSol = false)
    {
        ArgumentNullException.ThrowIfNull(body); body.Validated();
        var e = body.Environment;
        var airless = e.Atmosphere == PlanetaryAtmosphereRegime.Vacuum || e.PressureKPa < .1;
        PlanetClass result; string reason;
        // Body kind takes precedence. Volatile evidence is required to call a cold moon icy.
        if (body.Kind == PlanetaryBodyKind.DwarfPlanet) { result = PlanetClass.DwarfPlanet; reason = "Authoritative dwarf-planet body kind"; }
        else if (body.Kind == PlanetaryBodyKind.Moon)
        {
            if (e.TemperatureKelvin < 210 && (e.AvailableSolvent != PlanetarySolventRegime.None || !airless))
            { result = PlanetClass.IceMoon; reason = "Moon with a cold, volatile-bearing environment"; }
            else if (body.RadiusEarth >= .35) { result = PlanetClass.LargeMoon; reason = "Moon radius at least 0.35 Earth radii"; }
            else { result = PlanetClass.BarrenMoon; reason = "Small moon without confirmed cold volatile or extensive liquid conditions"; }
        }
        else if (!e.HasSolidSurface)
        {
            // Bulk ice fraction is not modeled: use a documented mass/size/thermal proxy,
            // with exact reserved Sol identities retaining their authored giant classes.
            var solIce = canonicalSol && (body.Id == 7 && body.Name == "Uranus" || body.Id == 8 && body.Name == "Neptune");
            var solGas = canonicalSol && (body.Id == 5 && body.Name == "Jupiter" || body.Id == 6 && body.Name == "Saturn");
            var ice = solIce || !solGas && e.TemperatureKelvin < 170 && body.MassEarth < 50 && body.RadiusEarth < 6;
            result = ice ? PlanetClass.IceGiant : PlanetClass.GasGiant;
            reason = solIce || solGas ? "Authored Sol giant identity" : "Non-solid envelope; mass, radius and temperature distinguish the giant family (composition is not measured)";
        }
        else if (e.TemperatureKelvin >= 450 && e.PressureKPa >= 1000 && !airless)
        { result = PlanetClass.Greenhouse; reason = "Very hot solid world with atmospheric pressure of at least 1,000 kPa"; }
        else if (e.AvailableSolvent == PlanetarySolventRegime.Hydrocarbon && e.TemperatureKelvin <= 180 &&
            e.Atmosphere is PlanetaryAtmosphereRegime.Reducing or PlanetaryAtmosphereRegime.Inert)
        { result = PlanetClass.CryogenicHydrocarbon; reason = "Cryogenic hydrocarbon solvent with reducing or inert atmospheric chemistry"; }
        else if (e.TemperatureKelvin >= 1000)
        { result = PlanetClass.Volcanic; reason = "Extreme surface temperature supports a thermal/volcanic interpretation; geological activity is not separately measured"; }
        else if (airless) { result = PlanetClass.AirlessRocky; reason = "Exposed solid surface with vacuum or less than 0.1 kPa pressure"; }
        else if (e.TemperatureKelvin < 200 && (e.AvailableSolvent != PlanetarySolventRegime.None || e.PressureKPa >= .1))
        { result = PlanetClass.Ice; reason = "Cold solid environment with solvent or atmospheric volatile evidence"; }
        else if (e.IsImmersedEnvironment && e.AvailableSolvent == PlanetarySolventRegime.Water)
        {
            result = e.PressureKPa >= 250 ? PlanetClass.HighPressureOcean : PlanetClass.Ocean;
            reason = e.PressureKPa >= 250 ? "Water-immersed environment at 250 kPa or greater pressure" : "Water-immersed environment below the high-pressure threshold";
        }
        else if (e.GravityG >= 1.6 || body.MassEarth >= 3 && body.RadiusEarth >= 1.2)
        { result = PlanetClass.HighGravitySuperEarth; reason = "Solid world with at least 1.6 g, or a larger terrestrial mass/radius combination"; }
        else if (e.AvailableSolvent is PlanetarySolventRegime.Other or PlanetarySolventRegime.Ammonia ||
            e.IsImmersedEnvironment || e.RadiationHazard >= .8 && e.Atmosphere == PlanetaryAtmosphereRegime.Other)
        { result = PlanetClass.Exotic; reason = "Non-water solvent, unusual immersed chemistry, or extreme radiation with unclassified atmosphere"; }
        else if (e.Atmosphere == PlanetaryAtmosphereRegime.Reducing)
        { result = PlanetClass.ReducingAtmosphere; reason = "Reducing atmospheric chemistry outside the cryogenic hydrocarbon regime"; }
        else if (e.PressureKPa >= 10 && e.Atmosphere is PlanetaryAtmosphereRegime.CarbonDioxideRich or PlanetaryAtmosphereRegime.Inert or PlanetaryAtmosphereRegime.Other)
        { result = PlanetClass.Toxic; reason = "Substantial non-oxygen atmosphere; 'toxic' describes Earthlike open-air conditions, not alien habitability"; }
        else if (e.AvailableSolvent == PlanetarySolventRegime.Water && e.TemperatureKelvin is >= 260 and <= 320 &&
            e.PressureKPa is >= 40 and <= 200 && e.Atmosphere is PlanetaryAtmosphereRegime.OxygenNitrogen or PlanetaryAtmosphereRegime.OxygenRich &&
            e.GravityG is >= .3 and < 1.6 && e.RadiationHazard < .25)
        { result = PlanetClass.Terran; reason = "Moderate water-bearing, oxygen-rich solid environment without extreme gravity or radiation"; }
        else if (e.AvailableSolvent == PlanetarySolventRegime.None && e.TemperatureKelvin >= 285)
        { result = PlanetClass.Desert; reason = "Warm exposed solid terrain without an available surface solvent"; }
        else { result = PlanetClass.Arid; reason = "Non-immersed solid terrain outside the stronger wet, chemical, thermal and gravity classes; moisture coverage is unresolved"; }
        return new(result, Name(result), reason + " · " + e.TemperatureKelvin.ToString("0.#", CultureInfo.InvariantCulture) + " K");
    }

    public static string Name(PlanetClass value) => value switch
    {
        PlanetClass.HighPressureOcean => "High-Pressure Ocean World",
        PlanetClass.CryogenicHydrocarbon => "Cryogenic Hydrocarbon World",
        PlanetClass.AirlessRocky => "Airless Rocky World",
        PlanetClass.ReducingAtmosphere => "Reducing-Atmosphere World",
        PlanetClass.HighGravitySuperEarth => "High-Gravity Super-Earth",
        PlanetClass.GasGiant => "Gas Giant",
        PlanetClass.IceGiant => "Ice Giant",
        PlanetClass.DwarfPlanet => "Dwarf Planet",
        PlanetClass.LargeMoon => "Large Moon",
        PlanetClass.BarrenMoon => "Barren Moon",
        PlanetClass.IceMoon => "Ice Moon",
        _ => value + " World"
    };
}
