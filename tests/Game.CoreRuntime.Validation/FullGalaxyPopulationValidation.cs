using System.Numerics;
using Game.Campaign;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class FullGalaxyPopulationValidation
{
    internal static void Run()
    {
        var sessions = new CampaignSessionService();
        GalaxyState? medium = null;
        foreach (var count in FullGalaxyStellarPopulation.AllowedSystemCounts)
        {
            var result = sessions.CreateNew("FULL-GALAXY-" + count, systemCount: count);
            ValidateGalaxy(result.Galaxy, count);
            if (count == 500) medium = result.Galaxy;
        }

        Require(medium is not null, "medium full-galaxy fixture was not generated");
        ValidatePersistence(sessions, medium!);
        ValidateOptions(sessions);
        ValidateSpeciesOpenings(sessions);
        ValidateLegacyProfiles(sessions);
        foreach (var seed in new long[] { 0, 1, -1, long.MinValue, long.MaxValue, 777, 20500101, 8675309 })
        foreach (var count in FullGalaxyStellarPopulation.AllowedSystemCounts)
        {
            var positions = FullGalaxyStellarPopulation.BuildGeneratedPositions(seed, count, GalacticCoreMetadata.CreateFullGalaxy(count));
            Require(positions.Count == count - FullGalaxyStellarPopulation.MeasuredSystemCount && positions.Distinct().Count() == positions.Count,
                $"compact packing failed for seed {seed}, count {count}");
        }
    }

    private static void ValidateGalaxy(GalaxyState galaxy, int count)
    {
        var metadata = galaxy.GenerationMetadata!;
        Require(galaxy.Systems.Count == count && metadata.SystemCount == count &&
                metadata.GeneratorVersion == GalaxyGenerationMetadata.FullGalaxyGeneratorVersion &&
                metadata.GalaxyShape == "Full galaxy",
            $"full-galaxy {count} metadata or system count changed");
        var radius = FullGalaxyStellarPopulation.RadiusFor(count);
        var core = galaxy.GalacticCore!;
        Require(core == GalacticCoreMetadata.CreateFullGalaxy(count) && metadata.GalacticCore == core &&
                Math.Abs(Vector2.Distance(Vector2.Zero, new Vector2(core.X, core.Y)) - radius * .52) < .01,
            $"full-galaxy {count} lost its scaled Sol/core relation");
        Require(galaxy.Systems.All(system =>
                Vector2.Distance(system.Position, new Vector2(core.X, core.Y)) is var distance &&
                distance >= core.ExclusionRadius && distance <= radius),
            $"full-galaxy {count} placed an ordinary system inside the core or beyond the disk");

        var measured = FullGalaxyStellarPopulation.MeasuredStars;
        for (var index = 0; index < measured.Count; index++)
        {
            var expected = NearbyStarCatalog.Apply(galaxy.Systems[index], measured[index]);
            var actual = galaxy.Systems[index];
            Require((actual.Name, actual.Position, actual.GalacticDepthLightYears, actual.StellarCatalogId,
                    actual.StellarClass, actual.SecondaryStellarClass, actual.TertiaryStellarClass) ==
                (expected.Name, expected.Position, expected.GalacticDepthLightYears, expected.StellarCatalogId,
                    expected.StellarClass, expected.SecondaryStellarClass, expected.TertiaryStellarClass),
                $"measured nearby identity {index} changed in full-galaxy {count}: actual={actual}; expected={expected}");
        }
        foreach (var name in new[] { "Sol", "Proxima Centauri", "Rigil Kentaurus", "Barnard's Star", "Wolf 359", "Sirius" })
            Require(galaxy.Systems.Any(system => system.Name == name && system.StellarCatalogId is not null),
                $"full-galaxy {count} omitted measured neighbor {name}");

        var generated = galaxy.Systems.Skip(FullGalaxyStellarPopulation.MeasuredSystemCount).ToArray();
        var nearestDistances = generated.Select(system => galaxy.Systems.Where(other => other.Id != system.Id)
            .Min(other => InterstellarDistance.Between(system, other))).Order().ToArray();
        Require(nearestDistances.First() >= 3.49 && nearestDistances.Last() <= 8.51 &&
                nearestDistances[nearestDistances.Length / 2] is >= 3.5 and <= 7.5,
            $"full-galaxy {count} became overcrowded or too sparse as its star count changed");
        Require(Math.Abs(radius * radius / count - 128.0 * 128.0 / 500) < .01,
            $"full-galaxy {count} changed its population density");
        Console.WriteLine($"FULL_GALAXY_SPACING count={count} radiusLy={radius:0.00} " +
            $"nearestMinLy={nearestDistances.First():0.00} nearestMedianLy={nearestDistances[nearestDistances.Length / 2]:0.00} " +
            $"nearestMaxLy={nearestDistances.Last():0.00}");
        Require(generated.All(system => system.StellarCatalogId is null && system.GalacticDepthLightYears is null &&
                    !system.Name.StartsWith("SYS-", StringComparison.OrdinalIgnoreCase) &&
                    system.Position.Length() >= FullGalaxyStellarPopulation.ProtectedNeighborhoodRadiusLightYears) &&
                galaxy.Systems.Select(system => system.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == count,
            $"full-galaxy {count} confused generated systems with measured catalogue facts");
        Require(generated.All(system => generated.Count(other => other.Id != system.Id &&
                    Vector2.Distance(other.Position, system.Position) <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance) >= 2),
            $"full-galaxy {count} generated a region without two opening-range neighbors");
        Require(generated.Max(system => Vector2.Distance(system.Position, new Vector2(core.X, core.Y))) > radius * .82,
            $"full-galaxy {count} did not populate the wider disk");

        var expectedCounts = FullGalaxyStellarPopulation.TargetStellarClassCounts(count);
        var actualCounts = galaxy.Systems.GroupBy(system => system.StellarClass!.Value)
            .ToDictionary(group => group.Key, group => group.Count());
        Require(expectedCounts.All(pair => actualCounts.GetValueOrDefault(pair.Key) == pair.Value) &&
                expectedCounts.Values.Sum() == count,
            $"full-galaxy {count} did not retain its exact dwarf-heavy stellar population");
        Require(FullGalaxyStellarPopulation.BuildStellarClasses(galaxy.Seed, count)
                    .SequenceEqual(galaxy.Systems.Select(system => system.StellarClass!.Value)) &&
                FullGalaxyStellarPopulation.BuildGeneratedPositions(galaxy.Seed, count, core)
                    .SequenceEqual(generated.Select(system => system.Position)),
            $"full-galaxy {count} stellar classes or generated coordinates were not deterministic");
        Require(galaxy.Systems.All(system => system.StellarClass == StellarPrimaryClass.BlackHole
                    ? system.Archetype == StarArchetype.BlackHole
                    : system.Archetype != StarArchetype.BlackHole) &&
                galaxy.Systems.All(system => system.StellarClass is StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar
                    ? system.Archetype == StarArchetype.NeutronPulsar
                    : system.Archetype != StarArchetype.NeutronPulsar),
            $"full-galaxy {count} physical compact objects disagreed with their hazard archetypes");

        var lanes = new InterstellarLaneNetwork().Build(galaxy.Systems);
        Require(lanes.Count >= count - 1 && lanes.All(lane => double.IsFinite(lane.LengthLightYears) && lane.LengthLightYears > 0),
            $"full-galaxy {count} did not produce a finite connected lane backbone");
        var connected = new HashSet<int> { galaxy.Systems[0].Id };
        while (true)
        {
            var previousCount = connected.Count;
            foreach (var lane in lanes)
            {
                if (connected.Contains(lane.FirstSystemId)) connected.Add(lane.SecondSystemId);
                if (connected.Contains(lane.SecondSystemId)) connected.Add(lane.FirstSystemId);
            }
            if (connected.Count == previousCount) break;
        }
        Require(connected.Count == count,
            $"full-galaxy {count} lane graph split into disconnected regions");
        Require(generated.All(system => lanes.Count(lane =>
                    (lane.FirstSystemId == system.Id || lane.SecondSystemId == system.Id) &&
                    lane.LengthLightYears <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance) >= 2),
            $"full-galaxy {count} did not connect each generated system to two opening-range lanes");
        Require(!galaxy.Knowledge.HasGalacticCoreAccess(galaxy.PlayerCivilizationId) &&
                !galaxy.Knowledge.IsGalacticCoreDiscovered(galaxy.PlayerCivilizationId),
            $"full-galaxy {count} disclosed the central black hole at campaign start");
    }

    private static void ValidatePersistence(CampaignSessionService sessions, GalaxyState galaxy)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-full-galaxy-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var original = sessions.CreateNew("FULL-GALAXY-SAVE", systemCount: 500);
            var path = Path.Combine(directory, "campaign.json");
            sessions.Save(path, original.Galaxy, original.Diplomacy, original.AdaptiveResearch, 0);
            var loaded = sessions.LoadExisting(path).Galaxy;
            Require(loaded.GenerationMetadata == original.Galaxy.GenerationMetadata &&
                    loaded.Systems.SequenceEqual(original.Galaxy.Systems) && loaded.GalacticCore == original.Galaxy.GalacticCore,
                "full-galaxy save/load changed metadata, stars, or the core");
        }
        finally { Directory.Delete(directory, true); }
    }

    private static void ValidateOptions(CampaignSessionService sessions)
    {
        const string seed = "FULL-GALAXY-OPTIONS";
        var metadata = GalaxyGenerationMetadata.FullGalaxy500(seed, CampaignSeed.Parse(seed), systemCount: 250) with
        {
            OtherCivilizations = 8,
            AncientCivilizations = "None",
            HabitableWorlds = "Common",
            AnomalyFrequency = "High",
        };
        var galaxy = sessions.CreateNew(metadata).Galaxy;
        Require(galaxy.Civilizations.Count == 9 && metadata.ToSettings().HabitableChance == .25 &&
                metadata.ToSettings().AnomalyChance == .35,
            "authoritative full-galaxy metadata options were ignored");
        var nonhumanSpecies = SpeciesCatalog.All.First(species => species.Id != SpeciesCatalog.TerranBaselineId);
        var solitary = sessions.CreateNew(metadata with
        {
            PlayerSpeciesId = nonhumanSpecies.Id,
            OtherCivilizations = 0,
        }).Galaxy;
        Require(solitary.Civilizations.Count == 1 && solitary.Civilizations.Single(civilization => civilization.IsPlayer).SpeciesId == nonhumanSpecies.Id,
            "zero-rival full-galaxy option created a hidden Human rival for a nonhuman player");
        var rejected = false;
        try { sessions.CreateNew(metadata with { OtherCivilizations = 7 }); }
        catch (ArgumentException) { rejected = true; }
        Require(rejected, "unsupported full-galaxy metadata options were accepted");
    }

    private static void ValidateSpeciesOpenings(CampaignSessionService sessions)
    {
        foreach (var count in new[] { 250, 500 })
        foreach (var species in SpeciesCatalog.All)
        {
            var galaxy = sessions.CreateNew($"FULL-{count}-{species.Id}", species.Id, systemCount: count).Galaxy;
            var occupied = galaxy.Civilizations.Select(civilization => civilization.HomeSystemId).ToHashSet();
            foreach (var civilization in galaxy.Civilizations.Where(civilization => !civilization.IsSeededAncient))
            {
                var home = galaxy.Systems[civilization.HomeSystemId];
                var viable = galaxy.Systems.Where(system => !occupied.Contains(system.Id) &&
                        InterstellarDistance.Between(home, system) <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance)
                    .Count(system => galaxy.PlanetaryBodies.Any(body => body.SystemId == system.Id &&
                        new SpeciesPlanetaryHabitabilityEvaluator().Evaluate(body, civilization.SpeciesId).Viability ==
                        SpeciesColonizationViability.NaturallyViable));
                Require(viable >= 2, $"{count}-system opening guarantee failed for {species.Id} civilization {civilization.Id}");
            }
        }
    }

    private static void ValidateLegacyProfiles(CampaignSessionService sessions)
    {
        var nearby = sessions.CreateNearbyCatalog("FULL-COMPAT-NEARBY").Galaxy;
        var legacy = sessions.CreateNew(8128L).Galaxy;
        Require(nearby.GenerationMetadata?.GeneratorVersion == GalaxyGenerationMetadata.CatalogGeneratorVersion &&
                nearby.Systems.All(system => system.StellarCatalogId is not null) && nearby.GalacticCore is null,
            "explicit nearby-500 compatibility profile changed");
        Require(legacy.Systems.Count == 100 && legacy.GenerationMetadata?.GeneratorVersion ==
                GalaxyGenerationMetadata.CurrentGeneratorVersion,
            "legacy 100-system profile changed");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
