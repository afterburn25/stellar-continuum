using System;
using System.Numerics;
using Game.Simulation.Models;

namespace Game.Presentation.PlanetIdentity;

/// <summary>Explicit synthetic examples for developer inspection and regression checks.
/// Never inserted into a campaign, never used to classify a real body.</summary>
public static class PlanetIdentityExamples
{
    public static StarSystemState System(int id = 42, StellarPrimaryClass star = StellarPrimaryClass.GYellowDwarf,
        StellarPrimaryClass? secondary = null, StarArchetype archetype = StarArchetype.Standard) =>
        new(id, "Gallery system", Vector2.Zero, archetype, false, false, false, false,
            StellarClass: star, SecondaryStellarClass: secondary);
    public static PlanetaryBodyState Body(PlanetClass type, int id = 100, int systemId = 42)
    {
        var e = new PlanetaryEnvironmentState(1, 288, 101, PlanetaryAtmosphereRegime.OxygenNitrogen, PlanetarySolventRegime.Water, .05, false, true);
        var kind = PlanetaryBodyKind.Planet; double radius = 1, mass = 1; int? parent = null;
        switch (type)
        {
            case PlanetClass.Ocean: e = e with { IsImmersedEnvironment = true }; break;
            case PlanetClass.HighPressureOcean: e = e with { IsImmersedEnvironment = true, PressureKPa = 850 }; break;
            case PlanetClass.Desert: e = e with { AvailableSolvent = PlanetarySolventRegime.None, TemperatureKelvin = 340 }; break;
            case PlanetClass.Arid: e = e with { AvailableSolvent = PlanetarySolventRegime.None, TemperatureKelvin = 275, PressureKPa = 25 }; break;
            case PlanetClass.Ice: e = e with { TemperatureKelvin = 160, PressureKPa = 4 }; break;
            case PlanetClass.CryogenicHydrocarbon: e = e with { TemperatureKelvin = 94, PressureKPa = 147, Atmosphere = PlanetaryAtmosphereRegime.Reducing, AvailableSolvent = PlanetarySolventRegime.Hydrocarbon }; break;
            case PlanetClass.AirlessRocky: e = e with { TemperatureKelvin = 440, PressureKPa = 0, Atmosphere = PlanetaryAtmosphereRegime.Vacuum, AvailableSolvent = PlanetarySolventRegime.None }; radius = .38; mass = .055; break;
            case PlanetClass.Greenhouse: e = e with { TemperatureKelvin = 735, PressureKPa = 9200, Atmosphere = PlanetaryAtmosphereRegime.CarbonDioxideRich, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.ReducingAtmosphere: e = e with { TemperatureKelvin = 260, Atmosphere = PlanetaryAtmosphereRegime.Reducing, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.Toxic: e = e with { TemperatureKelvin = 370, PressureKPa = 380, Atmosphere = PlanetaryAtmosphereRegime.Other, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.HighGravitySuperEarth: e = e with { GravityG = 2.2 }; radius = 1.5; mass = 4.95; break;
            case PlanetClass.Volcanic: e = e with { TemperatureKelvin = 1600, PressureKPa = .3, Atmosphere = PlanetaryAtmosphereRegime.Other, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.Exotic: e = e with { TemperatureKelvin = 230, AvailableSolvent = PlanetarySolventRegime.Ammonia }; break;
            case PlanetClass.GasGiant: e = e with { TemperatureKelvin = 120, PressureKPa = 100000, HasSolidSurface = false, Atmosphere = PlanetaryAtmosphereRegime.Reducing, AvailableSolvent = PlanetarySolventRegime.None }; radius = 11; mass = 318; break;
            case PlanetClass.IceGiant: e = e with { TemperatureKelvin = 72, PressureKPa = 100000, HasSolidSurface = false, Atmosphere = PlanetaryAtmosphereRegime.Reducing, AvailableSolvent = PlanetarySolventRegime.None }; radius = 3.9; mass = 17; break;
            case PlanetClass.DwarfPlanet: kind = PlanetaryBodyKind.DwarfPlanet; radius = .18; mass = .002; e = e with { TemperatureKelvin = 45, PressureKPa = 0, Atmosphere = PlanetaryAtmosphereRegime.Vacuum, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.LargeMoon: kind = PlanetaryBodyKind.Moon; parent = 99; radius = .5; mass = .1; e = e with { TemperatureKelvin = 240, PressureKPa = 0, Atmosphere = PlanetaryAtmosphereRegime.Vacuum, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.BarrenMoon: kind = PlanetaryBodyKind.Moon; parent = 99; radius = .27; mass = .012; e = e with { TemperatureKelvin = 220, PressureKPa = 0, Atmosphere = PlanetaryAtmosphereRegime.Vacuum, AvailableSolvent = PlanetarySolventRegime.None }; break;
            case PlanetClass.IceMoon: kind = PlanetaryBodyKind.Moon; parent = 99; radius = .24; mass = .008; e = e with { TemperatureKelvin = 100, PressureKPa = 0, Atmosphere = PlanetaryAtmosphereRegime.Vacuum }; break;
        }
        if (type != PlanetClass.HighGravitySuperEarth) e = e with { GravityG = mass / (radius * radius) };
        return new(id, systemId, parent, 2, type.ToString() + " specimen", kind, radius, mass, e, false, false, false, false);
    }
}
