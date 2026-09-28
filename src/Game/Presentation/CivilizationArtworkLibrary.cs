using System;
using Game.Simulation.Species;

namespace Game.Presentation;

/// <summary>Stable paths for species representatives and the first human leadership council.</summary>
public static class CivilizationArtworkLibrary
{
    public const string TerranBaseline = "res://assets/visual/species/terran-baseline.jpg";
    public const string PelagicHighPressure = "res://assets/visual/species/pelagic-high-pressure.jpg";
    public const string CompactHighGravity = "res://assets/visual/species/compact-high-gravity.jpg";
    public const string CryogenicHydrocarbon = "res://assets/visual/species/cryogenic-hydrocarbon.jpg";
    public const string ChiefScientist = "res://assets/visual/leaders/chief-scientist.jpg";
    public const string FleetCommander = "res://assets/visual/leaders/fleet-commander.jpg";
    public const string PlanetaryGovernor = "res://assets/visual/leaders/planetary-governor.jpg";

    public static string PathForSpecies(string speciesId) => speciesId switch
    {
        SpeciesCatalog.TerranBaselineId => TerranBaseline,
        SpeciesCatalog.PelagicHighPressureId => PelagicHighPressure,
        SpeciesCatalog.CompactHighGravityId => CompactHighGravity,
        SpeciesCatalog.CryogenicHydrocarbonId => CryogenicHydrocarbon,
        _ => throw new ArgumentOutOfRangeException(nameof(speciesId), speciesId,
            "No production portrait is registered for this species."),
    };

    /// <summary>Panoramic communications rooms; square identity portraits remain available elsewhere.</summary>
    public static string TransmissionPathForSpecies(string speciesId) => speciesId switch
    {
        SpeciesCatalog.TerranBaselineId => "res://assets/visual/species/terran-baseline-communications-v2.png",
        SpeciesCatalog.PelagicHighPressureId => "res://assets/visual/species/pelagic-high-pressure-communications-v2.png",
        SpeciesCatalog.CompactHighGravityId => "res://assets/visual/species/compact-high-gravity-communications-v2.png",
        SpeciesCatalog.CryogenicHydrocarbonId => "res://assets/visual/species/cryogenic-hydrocarbon-communications-v2.png",
        _ => throw new ArgumentOutOfRangeException(nameof(speciesId), speciesId,
            "No communications scene is registered for this species."),
    };
}
