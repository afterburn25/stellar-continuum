using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Generation;

public enum GalaxyShape
{
    LegacyDisk,
    BarredSpiral,
    SolarNeighborhood,
    FullGalaxy,
}

public sealed class GalaxyGenerationSettings
{
    // The first playable map is intentionally compact: it gives scouting, colonization,
    // diplomacy, and the system view room to matter without becoming a wall of stars.
    public int SystemCount { get; init; } = 100;
    public GalaxyShape GalaxyShape { get; init; } = GalaxyShape.LegacyDisk;
    /// <summary>Reserves the barred-spiral's physical centre for its non-routable core landmark.</summary>
    public bool IncludeGalacticCore { get; init; }
    public int PreWarpCivilizationCount { get; init; } = 8;
    public int AncientCivilizationCount { get; init; } = 2;
    public float Radius { get; init; } = 900.0f;
    public float InitialPreWarpSensorRange { get; init; } = 95.0f;
    public float InitialAncientSensorRange { get; init; } = 420.0f;
    public string PlayerSpeciesId { get; init; } = SpeciesCatalog.TerranBaselineId;

    public IReadOnlyDictionary<StarArchetype, double> ArchetypeWeights { get; init; } =
        new Dictionary<StarArchetype, double>
        {
            [StarArchetype.Standard] = 46,
            [StarArchetype.ResourceRich] = 12,
            [StarArchetype.HabitableRich] = 10,
            [StarArchetype.BarrenFrontier] = 8,
            [StarArchetype.Nebula] = 6,
            [StarArchetype.NeutronPulsar] = 5,
            [StarArchetype.BlackHole] = 3,
            [StarArchetype.AncientRuin] = 4,
            [StarArchetype.Dangerous] = 4,
            [StarArchetype.Legendary] = 2,
        };

    public double HabitableChance { get; init; } = 0.18;
    public double AnomalyChance { get; init; } = 0.20;
    public double RareResourceChance { get; init; } = 0.12;
    public double IndependentPreWarpChance { get; init; } = 0.04;
}

public sealed record GalaxyGenerationMetadata(
    string EnteredSeed,
    long InternalSeed,
    string GeneratorVersion,
    DateTimeOffset CreatedAtUtc,
    int SystemCount,
    string GalaxyShape,
    string StellarVariety,
    string PlanetBearingSystems,
    string HabitableWorlds,
    int GuaranteedNearbyHabitableWorlds,
    int OtherCivilizations,
    string AncientCivilizations,
    string SpaceHazards,
    string StartingDevelopment,
    string Difficulty,
    string ArtProfileVersion = "legacy-static-v1",
    string? PlayerSpeciesId = null,
    string AnomalyFrequency = "Standard")
{
    public const string CurrentGeneratorVersion = "galaxy-v4";
    public const string CatalogGeneratorVersion = "hyg-nearby-500-v1";
    public const string FullGalaxyGeneratorVersion = "full-galaxy-compact-v1";
    /// <summary>Absent on old saves; its presence explicitly opts this snapshot into the core.</summary>
    public GalacticCoreMetadata? GalacticCore { get; init; }

    public string SpoilerFreeSummary =>
        $"{SystemCount} systems · {StellarVariety.ToLowerInvariant()} stellar variety · " +
        $"{HabitableWorlds.ToLowerInvariant()} habitable worlds · {OtherCivilizations} other civilizations · " +
        $"{AncientCivilizations.ToLowerInvariant()} ancient powers";

    public static GalaxyGenerationMetadata Standard100(
        string enteredSeed,
        long internalSeed,
        string playerSpeciesId = SpeciesCatalog.TerranBaselineId) => new(
        enteredSeed,
        internalSeed,
        CurrentGeneratorVersion,
        DateTimeOffset.UtcNow,
        100,
        "Barred spiral",
        "Balanced",
        "Common",
        "Uncommon",
        2,
        5,
        "Rare",
        "Standard",
        "Early Space Age",
        "Standard",
        "milky-way-barred-v1",
        playerSpeciesId)
    {
        GalacticCore = GalacticCoreMetadata.Create(900),
    };

    public static GalaxyGenerationMetadata MilkyWay500(
        string enteredSeed, long internalSeed,
        string playerSpeciesId = SpeciesCatalog.TerranBaselineId) => new(
        enteredSeed, internalSeed, CatalogGeneratorVersion, DateTimeOffset.UtcNow,
        500, "Solar neighborhood", "Catalogue", "Common", "Uncommon", 2, 5,
        "Rare", "Standard", "Early Space Age", "Standard", "hyg-local-500-v1", playerSpeciesId);

    public static GalaxyGenerationMetadata FullGalaxy500(
        string enteredSeed, long internalSeed,
        string playerSpeciesId = SpeciesCatalog.TerranBaselineId,
        int systemCount = FullGalaxyStellarPopulation.DefaultSystemCount) => new(
        enteredSeed, internalSeed, FullGalaxyGeneratorVersion, DateTimeOffset.UtcNow,
        systemCount, "Full galaxy", "Dwarf-heavy", "Common", "Uncommon", 2, 5,
        "Rare", "Standard", "Early Space Age", "Standard", "milky-way-full-500-v1", playerSpeciesId)
    {
        GalacticCore = GalacticCoreMetadata.CreateFullGalaxy(systemCount),
    };

    public GalaxyGenerationSettings ToSettings() => new()
    {
        SystemCount = SystemCount,
        GalaxyShape = GalaxyShape == "Solar neighborhood"
            ? global::Game.Simulation.Generation.GalaxyShape.SolarNeighborhood
            : GalaxyShape == "Full galaxy"
            ? global::Game.Simulation.Generation.GalaxyShape.FullGalaxy
            : GalaxyShape == "Barred spiral"
            ? global::Game.Simulation.Generation.GalaxyShape.BarredSpiral
            : global::Game.Simulation.Generation.GalaxyShape.LegacyDisk,
        IncludeGalacticCore = GalacticCore is not null,
        Radius = GalaxyShape == "Full galaxy" ? FullGalaxyStellarPopulation.RadiusFor(SystemCount) : 900.0f,
        InitialPreWarpSensorRange = GalaxyShape is "Solar neighborhood" or "Full galaxy" ? 8.0f : 95.0f,
        InitialAncientSensorRange = GalaxyShape is "Solar neighborhood" or "Full galaxy" ? 25.0f : 420.0f,
        PreWarpCivilizationCount = OtherCivilizations + 1,
        AncientCivilizationCount = AncientCivilizations == "None" ? 0 : AncientCivilizations == "Standard" ? 2 : 1,
        HabitableChance = HabitableWorlds == "Rare" ? 0.09 : HabitableWorlds == "Common" ? 0.25 : 0.16,
        AnomalyChance = AnomalyFrequency == "Low" ? 0.05 : AnomalyFrequency == "High" ? 0.35 : 0.20,
        PlayerSpeciesId = string.IsNullOrWhiteSpace(PlayerSpeciesId)
            ? SpeciesCatalog.TerranBaselineId
            : PlayerSpeciesId,
    };
}

/// <summary>
/// Persisted, non-routable landmark for new barred-spiral Sandboxes. It intentionally has no
/// system ID: late-game access will add its own destination and travel rules.
/// </summary>
public sealed record GalacticCoreMetadata(string LandmarkKey, float X, float Y, float ExclusionRadius)
{
    public const string StableLandmarkKey = "galactic-core-smbh-v1";
    public static GalacticCoreMetadata Create(float radius) => new(
        StableLandmarkKey,
        -GalaxySpatialLayout.SolOffset(radius).X,
        -GalaxySpatialLayout.SolOffset(radius).Y,
        radius * .14f);

    public static GalacticCoreMetadata CreateFullGalaxy(int systemCount = FullGalaxyStellarPopulation.DefaultSystemCount) => new(
        StableLandmarkKey,
        -FullGalaxyStellarPopulation.SolOffsetFor(systemCount).X,
        -FullGalaxyStellarPopulation.SolOffsetFor(systemCount).Y,
        FullGalaxyStellarPopulation.RadiusFor(systemCount) * .14f);
}

public static class CampaignSeed
{
    public static long Parse(string enteredSeed)
    {
        if (string.IsNullOrWhiteSpace(enteredSeed))
            throw new ArgumentException("Enter a number or a memorable text seed.", nameof(enteredSeed));
        var trimmed = enteredSeed.Trim();
        if (long.TryParse(trimmed, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var numeric))
            return numeric;

        var normalized = trimmed.Normalize(NormalizationForm.FormKC).ToUpperInvariant();
        var digest = SHA256.HashData(Encoding.UTF8.GetBytes(normalized));
        return BinaryPrimitives.ReadInt64LittleEndian(digest);
    }

    public static string CreateRandomNumericText()
    {
        Span<byte> bytes = stackalloc byte[sizeof(long)];
        RandomNumberGenerator.Fill(bytes);
        return BinaryPrimitives.ReadInt64LittleEndian(bytes)
            .ToString(CultureInfo.InvariantCulture);
    }
}
