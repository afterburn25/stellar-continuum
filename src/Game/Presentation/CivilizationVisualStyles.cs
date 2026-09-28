using Godot;
using Game.Simulation.Species;

namespace Game.Presentation;

/// <summary>Presentation identity only. Species capabilities, ownership, technology and
/// game rules remain in the simulation. Unknown/future species get a neutral fallback.</summary>
public sealed record CivilizationVisualStyle(string SpeciesId, string Motif, Color HullColor,
    Color SecondaryColor, Color AccentColor, Color EngineColor, Color GlassColor, string Insignia);

public static class CivilizationVisualStyles
{
    public static readonly CivilizationVisualStyle Terran = new(SpeciesCatalog.TerranBaselineId,
        "modular", new("68747b"), new("253540"), new("d5b478"), new("80cce8"), new("123548"), "UT");
    private static readonly CivilizationVisualStyle Pelagic = new(SpeciesCatalog.PelagicHighPressureId,
        "pressure-shell", new("466f78"), new("16333f"), new("77d7c8"), new("59e2d5"), new("143345"), "PL");
    private static readonly CivilizationVisualStyle Compact = new(SpeciesCatalog.CompactHighGravityId,
        "armored", new("776d65"), new("342d2c"), new("dc9067"), new("ffc58b"), new("332c2d"), "CG");
    private static readonly CivilizationVisualStyle Cryogenic = new(SpeciesCatalog.CryogenicHydrocarbonId,
        "crystalline", new("72788d"), new("282e46"), new("b4a7e3"), new("b9cdff"), new("272a46"), "CH");
    private static readonly CivilizationVisualStyle Neutral = new("unknown", "neutral",
        new("616970"), new("2b3239"), new("a1acb5"), new("bed1db"), new("27333c"), "");

    public static CivilizationVisualStyle ForSpecies(string? speciesId) => speciesId switch
    {
        SpeciesCatalog.TerranBaselineId => Terran,
        SpeciesCatalog.PelagicHighPressureId => Pelagic,
        SpeciesCatalog.CompactHighGravityId => Compact,
        SpeciesCatalog.CryogenicHydrocarbonId => Cryogenic,
        _ => Neutral,
    };
}

public partial class Main
{
    public CivilizationVisualStyle UiVisualStyle => _galaxy is null
        ? CivilizationVisualStyles.Terran : CivilizationVisualStyles.ForSpecies(PlayerCivilization.SpeciesId);
}
