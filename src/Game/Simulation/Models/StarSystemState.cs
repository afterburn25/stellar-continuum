using System.Numerics;

namespace Game.Simulation.Models;

public sealed record StarSystemState(
    int Id,
    string Name,
    Vector2 Position,
    StarArchetype Archetype,
    bool HasHabitableWorld,
    bool HasAnomaly,
    bool HasRareResource,
    bool HasPreWarpCivilization,
    string? CatalogPresetId = null,
    StellarPrimaryClass? StellarClass = null,
    StellarPrimaryClass? SecondaryStellarClass = null,
    StellarPrimaryClass? TertiaryStellarClass = null,
    double? GalacticDepthLightYears = null,
    string? StellarCatalogId = null
);

public enum StellarPrimaryClass
{
    MRedDwarf,
    KOrangeDwarf,
    GYellowDwarf,
    FYellowWhiteDwarf,
    AWhiteStar,
    HotBlueStar,
    Giant,
    WhiteDwarf,
    NeutronStar,
    BlackHole,
    Protostar,
    Pulsar,
}

public enum StarArchetype
{
    Standard,
    ResourceRich,
    HabitableRich,
    BarrenFrontier,
    Nebula,
    NeutronPulsar,
    BlackHole,
    AncientRuin,
    Dangerous,
    Legendary,
}
