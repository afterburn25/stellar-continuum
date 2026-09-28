using Godot;
using Game.Simulation.Species;

namespace Game.Presentation;

/// <summary>Visual treatment of legitimately identified species. Unknown contacts use one
/// generic treatment; hidden IDs must never seed decorative visuals.</summary>
public sealed record DiplomacyTransmissionStyle(Color Accent)
{
    public static DiplomacyTransmissionStyle ForKnownSpecies(string? species) => species switch
    {
        SpeciesCatalog.TerranBaselineId => new(new Color("b7d9ee")),
        SpeciesCatalog.PelagicHighPressureId => new(new Color("86d6cf")),
        SpeciesCatalog.CompactHighGravityId => new(new Color("e4bc86")),
        SpeciesCatalog.CryogenicHydrocarbonId => new(new Color("b7b7eb")),
        _ => new(new Color("8cabb7")),
    };
    public StyleBoxFlat Frame()
    {
        var frame = VisualUi.Surface(margin: 0);
        frame.BgColor = new Color("0b1b2b");
        frame.BorderColor = Accent.Darkened(.4f);
        return frame;
    }
}
