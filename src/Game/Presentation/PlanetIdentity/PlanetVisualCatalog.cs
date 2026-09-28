using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Presentation.PlanetIdentity;

public sealed record PlanetVisualVariant(string Id, string Name, int Pattern, string Land, string Rock,
    string Ocean, string Atmosphere, float OceanCoverage, float Clouds, float Haze, float Roughness);
public sealed record PlanetVisualFamily(PlanetClass Class, string OrbitalFamily, string SurfaceFamily,
    IReadOnlyList<PlanetVisualVariant> Variants);

/// <summary>Small immutable master library. Patterns change morphology as well as palette.</summary>
public static class PlanetVisualCatalog
{
    public const int Version = 1;
    public const string OrbitalShader = "res://assets/visual/shaders/planet_identity_orbit.gdshader";
    public const string SurfaceShader = "res://assets/visual/shaders/planet_identity_surface.gdshader";
    public const string SkyShader = "res://assets/visual/shaders/planet_identity_sky.gdshader";
    public static readonly IReadOnlyList<PlanetVisualFamily> All = new[]
    {
        Family(PlanetClass.Terran,"continents","temperate","49634b","92846a","17485e","759fbb",.64f,.45f,.15f,"Continental Blue|Archipelago|Highland Green|Polar Coast|Old Continents",3,3,8,2,3),
        Family(PlanetClass.Ocean,"oceans","oceanic","526c5d","8f927d","13516b","87a9b8",.96f,.58f,.3f,"Deep Blue|Teal Shallows|Storm Ocean|Island Chains|Polar Ocean",4,4,4,3,2),
        Family(PlanetClass.HighPressureOcean,"storm-oceans","oceanic","486c67","708c80","163e57","8ea7ab",.985f,.78f,.75f,"Abyssal Storm|Silver Cloud Sea|Teal Cyclones|Dark Pelagic|Polar Maelstrom",4,4,4,4,2),
        Family(PlanetClass.Desert,"dry-basins","desert","b38251","684c39","334751","b39a7d",0,.08f,.22f,"Red Canyon|Golden Dune|Pale Salt Basin|Dark Rocky Desert|Orange Dust",8,1,9,7,1),
        Family(PlanetClass.Arid,"dry-highlands","arid","867d53","635c4b","315664","b4ad8d",.12f,.17f,.2f,"Dry Grassland|Stone Basin|Salt Coast|Copper Steppe|Wind Plateau",8,7,9,1,8),
        Family(PlanetClass.Ice,"glacial","frozen","bbcbd0","596f79","234c65","96b2c5",.05f,.27f,.27f,"White Glacier|Blue Fractured Ice|Dark Ice Rock|Snowstorm|Polar Ridge",2,2,7,2,8),
        Family(PlanetClass.CryogenicHydrocarbon,"hydrocarbon","cryogenic","aa8353","666759","252f2f","c69a64",.32f,.68f,.8f,"Dark Methane Sea|Amber Haze|Frozen Delta|Pale Hydrocarbon Plain|Polar Lakes",4,7,2,9,3),
        Family(PlanetClass.AirlessRocky,"cratered","airless","898984","474e53","242b31","151b25",0,0,0,"Gray Cratered|Dark Basaltic|Rusted Mineral|Pale Dust|Fractured Highlands",0,7,8,0,2),
        Family(PlanetClass.Greenhouse,"opaque-clouds","greenhouse","ad8050","604638","574431","ddc28f",0,.98f,1,"Cream Cloud Deck|Amber Convection|Sulfur Veil|Copper Furnace|Pale Superrotation",5,5,5,5,5),
        Family(PlanetClass.ReducingAtmosphere,"chemical-haze","reducing","80745a","515b52","36483b","b5a174",.06f,.6f,.65f,"Ammoniac Haze|Dark Chemical Plain|Amber Storm|Mineral Rifts|Pale Smog",7,8,5,2,9),
        Family(PlanetClass.Toxic,"chemical-clouds","toxic","8a8b66","666a59","384f44","b2b596",.03f,.58f,.7f,"Pale Chemical Veil|Sulfate Highlands|Corrosive Basin|Dark Cloud Bands|Mineral Fog",9,8,7,5,2),
        Family(PlanetClass.HighGravitySuperEarth,"compressed-highlands","high-gravity","7e765f","494e50","234b63","9fa4a4",.15f,.36f,.42f,"Massive Plateau|Iron Highlands|Lowland Basin|Dense Cloud World|Heavy Ocean Coast",8,7,9,5,3),
        Family(PlanetClass.Volcanic,"thermal-rifts","volcanic","3c3b3d","71605b","b84417","8b6c60",0,.12f,.45f,"Basalt Fissures|Lava Basins|Ash Highlands|Thermal Rift|Dark Caldera",6,6,8,2,0),
        Family(PlanetClass.Exotic,"unusual-minerals","exotic","716b82","4a565e","3d6267","9492a4",.14f,.35f,.4f,"Ammonia Basins|Iridescent Minerals|Radiation Highlands|Dense Veiled World|Dark Solvent Sea",4,2,8,5,4),
        Family(PlanetClass.GasGiant,"gas-bands","none","bd9d73","745b4b","6e7d8d","cdb69c",0,.85f,.6f,"Ochre Banded|Pale Cream|Blue Storm|Dark Bands|Ringed Giant",5,5,5,5,5),
        Family(PlanetClass.IceGiant,"volatile-bands","none","75a5ae","355866","497486","a6cdd4",0,.8f,.55f,"Cyan Envelope|Blue Storm|Jade Veil|Pale Volatile|Ringed Ice Giant",5,5,5,5,5),
        Family(PlanetClass.DwarfPlanet,"minor-rock-ice","minor","a49c8f","655a51","30495c","747f92",0,0,0,"Pale Basin|Ice Highlands|Dark Craters|Rusted Dwarf|Fractured Minor World",9,2,0,7,2),
        Family(PlanetClass.LargeMoon,"large-satellite","moon","9c9787","575650","32536a","8695a2",.02f,.05f,.08f,"Ancient Highlands|Dark Maria|Pale Craters|Mineral Ridges|Volatile Basin",8,7,0,2,9),
        Family(PlanetClass.BarrenMoon,"small-satellite","airless","99968e","4b4c4c","293846","111720",0,0,0,"Gray Craters|Basalt Maria|Pale Regolith|Iron Scars|Fractured Highlands",0,7,0,8,2),
        Family(PlanetClass.IceMoon,"icy-satellite","frozen","b8cbd1","4b6473","244356","718d9f",0,.03f,.05f,"Fractured Shell|Blue Ice|Dark Ice Rock|Pale Ridges|Frozen Basins",2,2,7,8,9)
    };
    public static PlanetVisualFamily Get(PlanetClass value) => All[(int)value];
    private static PlanetVisualFamily Family(PlanetClass c, string orbit, string surface, string land, string rock, string ocean, string air, float water, float clouds, float haze, string names, params int[] patterns)
    {
        var labels = names.Split('|');
        var variants = labels.Select((name, i) => new PlanetVisualVariant(ToId(c) + "-" + (i + 1).ToString("00"), name, patterns[i], land, rock, ocean, air,
            c is PlanetClass.Ocean or PlanetClass.HighPressureOcean ? Math.Clamp(water + (i == 3 ? -.10f : i == 4 ? -.03f : 0), .85f, 1) : water,
            Math.Clamp(clouds + (i == 2 ? .15f : i == 1 ? -.12f : 0), 0, 1), haze, c is PlanetClass.Ice or PlanetClass.IceMoon ? .38f : .86f)).ToArray();
        return new(c, orbit, surface, Array.AsReadOnly(variants));
    }
    public static string ToId(PlanetClass value) => string.Concat(value.ToString().Select((c, i) => char.IsUpper(c) && i > 0 ? "-" + char.ToLowerInvariant(c) : char.ToLowerInvariant(c).ToString()));
}
