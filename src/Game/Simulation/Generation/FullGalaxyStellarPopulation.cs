using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Models;

namespace Game.Simulation.Generation;

/// <summary>
/// Versioned physical population for the full 500-system profile. The nearest classified
/// catalogue records remain measured facts; every other coordinate and identity is explicitly
/// seeded game content spread through clustered barred-spiral regions.
/// </summary>
public static class FullGalaxyStellarPopulation
{
    public const int DefaultSystemCount = 500;
    public const int MeasuredSystemCount = 96;
    // A finite playable galaxy scales by area, preserving neighbour spacing as its
    // population changes. This is not the physical radius of the real Milky Way.
    public const float DefaultGalaxyRadiusLightYears = 128.0f;
    public const float MinimumGeneratedSpacingLightYears = 3.5f;
    public static float ProtectedNeighborhoodRadiusLightYears => (float)MeasuredStars.Max(star =>
        Math.Sqrt(star.XLightYears * star.XLightYears + star.YLightYears * star.YLightYears)) +
        MinimumGeneratedSpacingLightYears;
    public static Vector2 SolOffsetFor(int systemCount)
    {
        var radius = RadiusFor(systemCount);
        return new(radius * .48f, radius * .20f);
    }

    private static readonly IReadOnlyDictionary<StellarPrimaryClass, int> ReferenceStellarClassCounts =
        new Dictionary<StellarPrimaryClass, int>
        {
            [StellarPrimaryClass.MRedDwarf] = 375,
            [StellarPrimaryClass.KOrangeDwarf] = 50,
            [StellarPrimaryClass.GYellowDwarf] = 25,
            [StellarPrimaryClass.FYellowWhiteDwarf] = 10,
            [StellarPrimaryClass.AWhiteStar] = 5,
            [StellarPrimaryClass.HotBlueStar] = 1,
            [StellarPrimaryClass.Giant] = 5,
            [StellarPrimaryClass.WhiteDwarf] = 24,
            [StellarPrimaryClass.NeutronStar] = 2,
            [StellarPrimaryClass.Pulsar] = 1,
            [StellarPrimaryClass.BlackHole] = 1,
            [StellarPrimaryClass.Protostar] = 1,
        };

    public static IReadOnlyList<int> AllowedSystemCounts { get; } =
        Array.AsReadOnly(new[] { 250, 500, 1000, 2500 });

    public static float RadiusFor(int systemCount)
    {
        ValidateSystemCount(systemCount);
        return DefaultGalaxyRadiusLightYears * MathF.Sqrt(systemCount / (float)DefaultSystemCount);
    }

    public static IReadOnlyDictionary<StellarPrimaryClass, int> TargetStellarClassCounts(int systemCount)
    {
        ValidateSystemCount(systemCount);
        var allocations = ReferenceStellarClassCounts.Select(pair => new ClassAllocation(pair.Key,
                (int)Math.Floor(pair.Value * systemCount / (double)DefaultSystemCount),
                pair.Value * systemCount / (double)DefaultSystemCount % 1.0))
            .ToList();
        var remaining = systemCount - allocations.Sum(allocation => allocation.Count);
        foreach (var allocation in allocations.OrderByDescending(allocation => allocation.Remainder)
                     .ThenBy(allocation => allocation.StellarClass).Take(remaining))
            allocation.Count++;

        // Rare bins are intentional finite-sample representation floors. Debit the dominant
        // M-dwarf bin when largest-remainder rounding would otherwise omit one entirely.
        var rareClasses = new[]
        {
            StellarPrimaryClass.HotBlueStar, StellarPrimaryClass.Giant, StellarPrimaryClass.WhiteDwarf,
            StellarPrimaryClass.NeutronStar, StellarPrimaryClass.Pulsar, StellarPrimaryClass.BlackHole,
            StellarPrimaryClass.Protostar,
        };
        foreach (var stellarClass in rareClasses)
        {
            var allocation = allocations.Single(item => item.StellarClass == stellarClass);
            if (allocation.Count > 0) continue;
            allocation.Count = 1;
            allocations.Single(item => item.StellarClass == StellarPrimaryClass.MRedDwarf).Count--;
        }
        return allocations.ToDictionary(item => item.StellarClass, item => item.Count);
    }

    public static IReadOnlyList<NearbyCatalogStar> MeasuredStars =>
        NearbyStarCatalog.NearestClassified(MeasuredSystemCount);

    public static IReadOnlyList<StellarPrimaryClass> BuildStellarClasses(long seed, int systemCount)
    {
        ValidateSystemCount(systemCount);
        var measured = MeasuredStars.Select(star => NearbyStarCatalog.Classify(star.SpectralType)!.Value).ToArray();
        var remainder = TargetStellarClassCounts(systemCount).ToDictionary(pair => pair.Key, pair => pair.Value);
        foreach (var stellarClass in measured)
        {
            if (!remainder.TryGetValue(stellarClass, out var available) || available == 0)
                throw new InvalidOperationException($"Measured nearby stars exceed the full-galaxy quota for {stellarClass}.");
            remainder[stellarClass] = available - 1;
        }

        var generated = remainder.SelectMany(pair => Enumerable.Repeat(pair.Key, pair.Value)).ToList();
        if (generated.Count != systemCount - MeasuredSystemCount)
            throw new InvalidOperationException($"Full-galaxy stellar quotas do not total {systemCount} systems.");
        var random = PopulationRandom(seed, 0x53544152);
        Shuffle(generated, random);
        return Array.AsReadOnly(measured.Concat(generated).ToArray());
    }

    public static IReadOnlyList<Vector2> BuildGeneratedPositions(long seed, int systemCount, GalacticCoreMetadata core)
    {
        ArgumentNullException.ThrowIfNull(core);
        ValidateSystemCount(systemCount);
        var generatedCount = systemCount - MeasuredSystemCount;
        var positions = new List<Vector2>(generatedCount);
        var random = PopulationRandom(seed, 0x504F534E);
        var corePosition = new Vector2(core.X, core.Y);
        var galaxyRadius = RadiusFor(systemCount);
        var protectedRadius = ProtectedNeighborhoodRadiusLightYears;
        var spacing = new Dictionary<(int X, int Y), List<Vector2>>();
        var clusterCount = generatedCount / 4;
        var largerClusters = generatedCount % 4;
        for (var clusterIndex = 0; clusterIndex < clusterCount; clusterIndex++)
        {
            var accepted = false;
            for (var attempt = 0; attempt < 2048 && !accepted; attempt++)
            {
                var anchor = GalaxySpatialLayout.NextPosition(GalaxyShape.FullGalaxy,
                    galaxyRadius, random) + corePosition;
                var rotation = random.NextDouble() * Math.Tau;
                var cluster = new Vector2[clusterIndex < largerClusters ? 5 : 4];
                cluster[0] = anchor;
                for (var member = 1; member < cluster.Length; member++)
                {
                    var angle = rotation + (member - 1) * Math.Tau / (cluster.Length - 1) +
                        (random.NextDouble() - .5) * .18;
                    var radius = 4.5 + random.NextDouble() * 4.0;
                    cluster[member] = anchor + new Vector2((float)(Math.Cos(angle) * radius),
                        (float)(Math.Sin(angle) * radius));
                }
                if (!cluster.All(position => IsAllowed(position, corePosition, core.ExclusionRadius, galaxyRadius,
                        protectedRadius) && HasSpacing(position, spacing)) ||
                    cluster.Where((position, index) => cluster.Take(index).Any(other =>
                        Vector2.DistanceSquared(position, other) <
                        MinimumGeneratedSpacingLightYears * MinimumGeneratedSpacingLightYears)).Any())
                    continue;
                positions.AddRange(cluster);
                foreach (var position in cluster)
                {
                    var cell = SpacingCell(position);
                    if (!spacing.TryGetValue(cell, out var points)) spacing[cell] = points = new();
                    points.Add(position);
                }
                accepted = true;
            }
            if (!accepted)
                throw new InvalidOperationException("Could not place a bounded full-galaxy stellar region.");
        }
        return Array.AsReadOnly(positions.ToArray());
    }

    public static StarArchetype AlignCompactArchetype(StellarPrimaryClass stellarClass, StarArchetype archetype) =>
        stellarClass switch
        {
            StellarPrimaryClass.BlackHole => StarArchetype.BlackHole,
            StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar => StarArchetype.NeutronPulsar,
            _ when archetype is StarArchetype.BlackHole or StarArchetype.NeutronPulsar => StarArchetype.Standard,
            _ => archetype,
        };

    private static bool IsAllowed(Vector2 position, Vector2 core, float coreExclusionRadius, float galaxyRadius,
        float protectedRadius)
    {
        var galactocentricDistance = Vector2.Distance(position, core);
        return position.Length() >= protectedRadius &&
            galactocentricDistance >= coreExclusionRadius &&
            galactocentricDistance <= galaxyRadius;
    }

    private static (int X, int Y) SpacingCell(Vector2 position) =>
        ((int)MathF.Floor(position.X / MinimumGeneratedSpacingLightYears),
         (int)MathF.Floor(position.Y / MinimumGeneratedSpacingLightYears));

    private static bool HasSpacing(Vector2 position, Dictionary<(int X, int Y), List<Vector2>> cells)
    {
        var cell = SpacingCell(position);
        for (var x = cell.X - 1; x <= cell.X + 1; x++)
        for (var y = cell.Y - 1; y <= cell.Y + 1; y++)
            if (cells.TryGetValue((x, y), out var points) && points.Any(other =>
                Vector2.DistanceSquared(position, other) <
                MinimumGeneratedSpacingLightYears * MinimumGeneratedSpacingLightYears))
                return false;
        return true;
    }

    private static void ValidateSystemCount(int systemCount)
    {
        if (!AllowedSystemCounts.Contains(systemCount))
            throw new ArgumentOutOfRangeException(nameof(systemCount),
                $"Full-galaxy system count must be one of: {string.Join(", ", AllowedSystemCounts)}.");
    }

    private static Random PopulationRandom(long seed, int salt) =>
        new(unchecked((int)(seed ^ (seed >> 32) ^ salt)));

    private static void Shuffle<T>(IList<T> values, Random random)
    {
        for (var index = values.Count - 1; index > 0; index--)
        {
            var swap = random.Next(index + 1);
            (values[index], values[swap]) = (values[swap], values[index]);
        }
    }

    private sealed class ClassAllocation
    {
        public ClassAllocation(StellarPrimaryClass stellarClass, int count, double remainder)
        { StellarClass = stellarClass; Count = count; Remainder = remainder; }
        public StellarPrimaryClass StellarClass { get; }
        public int Count { get; set; }
        public double Remainder { get; }
    }
}
