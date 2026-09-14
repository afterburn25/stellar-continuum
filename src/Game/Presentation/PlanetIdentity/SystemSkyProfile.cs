using System;
using Game.Simulation.Models;

namespace Game.Presentation.PlanetIdentity;

public sealed record StellarLightProfile(StellarPrimaryClass? Class, string Color, float Intensity, float ApparentRadius);
public sealed record SystemSkyProfile(ulong Identity, int SystemId, StellarLightProfile Primary,
    StellarLightProfile? Secondary, StellarLightProfile? Tertiary, int StarDensity, string Background,
    string NebulaColor, string DustColor, float NebulaVisibility, float Dust, float AmbientLight,
    bool AuroraPotential, bool EclipsePotential, bool DebrisBand, string GalacticContext);

public static class SystemSkyResolver
{
    // Location is a broad observed radius fraction, never an invented arm/core measurement.
    public static SystemSkyProfile Resolve(long seed, int systemId, StellarPrimaryClass? primary,
        StellarPrimaryClass? secondary = null, StellarPrimaryClass? tertiary = null, StarArchetype? archetype = null, double? radialFraction = null)
    {
        var hash = VisualHash.Start(seed, systemId, 0x534b59);
        var palettes = new[] { ("465b73", "292635"), ("694b48", "242532"), ("465853", "262a32"), ("61566d", "2d2635"), ("687078", "292b35") };
        var palette = palettes[(int)(hash % 5)];
        var outer = radialFraction is >= .80; var inner = radialFraction is <= .18;
        var nebula = archetype is StarArchetype.Nebula || primary is StellarPrimaryClass.Protostar;
        var background = outer ? "Outer-galaxy sparse" : inner ? "Dense inner region" : nebula ? "Star-forming clouds" : primary == StellarPrimaryClass.BlackHole ? "Dark compact-object region" : new[] { "Sparse black field", "Open stellar field", "Galactic star band", "Dust-lane field" }[(int)((hash >> 8) % 4)];
        return new(hash, systemId, Light(primary), secondary is null ? null : Light(secondary), tertiary is null ? null : Light(tertiary),
            outer ? 480 : inner ? 1600 : 640 + (int)((hash >> 12) % 600), background, palette.Item1, palette.Item2,
            nebula ? .38f : archetype.HasValue ? .035f + (float)((hash >> 20) % 45) / 1000f : .025f,
            nebula ? .32f : .08f + (float)((hash >> 26) % 10) / 100f, .17f,
            primary is StellarPrimaryClass.MRedDwarf or StellarPrimaryClass.HotBlueStar or StellarPrimaryClass.NeutronStar,
            secondary.HasValue, archetype.HasValue && (hash >> 31) % 7 == 0,
            radialFraction is null ? "Position context unavailable" : outer ? "Outer region" : inner ? "Inner region" : "Intermediate radius");
    }
    public static StellarLightProfile Light(StellarPrimaryClass? type) => type switch
    {
        StellarPrimaryClass.MRedDwarf => new(type, "ffaf88", .72f, .32f),
        StellarPrimaryClass.KOrangeDwarf => new(type, "ffd1a0", .95f, .45f),
        StellarPrimaryClass.GYellowDwarf => new(type, "fff0d7", 1.15f, .5f),
        StellarPrimaryClass.FYellowWhiteDwarf => new(type, "f7f6ff", 1.25f, .55f),
        StellarPrimaryClass.AWhiteStar => new(type, "dce8ff", 1.35f, .62f),
        StellarPrimaryClass.HotBlueStar => new(type, "bad5ff", 1.5f, .72f),
        StellarPrimaryClass.Giant => new(type, "ffbd90", 1.2f, 1.2f),
        StellarPrimaryClass.WhiteDwarf => new(type, "dcecff", .8f, .12f),
        StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar => new(type, "a6d6ee", .5f, .06f),
        StellarPrimaryClass.BlackHole => new(type, "d1c8c0", .08f, .12f),
        StellarPrimaryClass.Protostar => new(type, "ffd0a6", .65f, .65f),
        _ => new(type, "c3c9cd", .8f, .5f)
    };
}

public static class VisualHash
{
    public static ulong Add(ulong hash, ulong value)
    { unchecked { for (int i = 0; i < 8; i++) { hash ^= (byte)value; hash *= 1099511628211UL; value >>= 8; } return hash; } }
    public static ulong Start(long seed, int systemId, int bodyId) => Add(Add(Add(14695981039346656037UL, unchecked((ulong)seed)), unchecked((uint)systemId)), unchecked((uint)bodyId));
    public static ulong Add(ulong hash, double value) => Add(hash, unchecked((ulong)BitConverter.DoubleToInt64Bits(value)));
}
