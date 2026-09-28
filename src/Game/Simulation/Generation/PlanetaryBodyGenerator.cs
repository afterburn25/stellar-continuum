using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Generation;

/// <summary>
/// Generates a bounded physical planet/moon catalog from campaign seed + star-system identity.
/// The generator deliberately does not evaluate species suitability. Existing system-level
/// HasHabitableWorld is used only to preserve one legacy colonization candidate while the
/// species-relative colony contract is still being integrated.
/// </summary>
public sealed class PlanetaryBodyGenerator
{
    private const int BodyIdStride = 1000;

    public IReadOnlyList<PlanetaryBodyState> Generate(long campaignSeed, IReadOnlyList<StarSystemState> systems)
    {
        ArgumentNullException.ThrowIfNull(systems);
        var result = new List<PlanetaryBodyState>(systems.Count * 8);
        var balancedPlanetCounts = BuildBalancedPlanetCounts(campaignSeed, systems);

        foreach (var system in systems.OrderBy(system => system.Id))
        {
            if (system.CatalogPresetId is not null)
            {
                if (!SolCatalogPreset.IsSol(system))
                    throw new InvalidOperationException($"Unknown planetary catalog preset '{system.CatalogPresetId}' for system {system.Id}.");
                result.AddRange(SolCatalogPreset.Create(system));
                continue;
            }
            var random = StableRandom.ForSystem(campaignSeed, system.Id);
            var planetCount = balancedPlanetCounts is not null
                ? balancedPlanetCounts[system.Id]
                : ResolvePlanetCount(system.Archetype, ref random);
            var legacyOrbit = system.HasHabitableWorld ? random.NextInt(planetCount) : -1;
            var rareOrbit = system.HasRareResource ? random.NextInt(planetCount) : -1;
            var anomalyOrbit = system.HasAnomaly ? random.NextInt(planetCount) : -1;
            var localId = 1;

            for (var orbit = 0; orbit < planetCount; orbit++)
            {
                var isLegacyCandidate = orbit == legacyOrbit;
                var planetId = checked(system.Id * BodyIdStride + localId++);
                var planet = CreatePlanet(
                    campaignSeed,
                    planetId,
                    system,
                    orbit,
                    isLegacyCandidate,
                    orbit == rareOrbit,
                    orbit == anomalyOrbit,
                    system.HasPreWarpCivilization && isLegacyCandidate,
                    ref random);
                result.Add(planet.Validated());

                var moonCount = ResolveMoonCount(planet, ref random);
                for (var moon = 0; moon < moonCount; moon++)
                {
                    var moonId = checked(system.Id * BodyIdStride + localId++);
                    var body = CreateMoon(campaignSeed, moonId, planet, moon, system.StellarClass is not null, ref random);
                    result.Add(body.Validated());
                }
            }
        }

        // Environmental diversity is part of the canonical deterministic catalog itself,
        // not a fresh-generation-only post-process. Save/load reconstruction calls this same
        // generator from seed + systems, so both paths must receive the identical conditioned
        // physical catalog before any civilization/species assignment is considered.
        var conditioned = new PlanetaryEnvironmentalDiversityPolicy().Apply(campaignSeed, systems, result);
        ValidateCatalog(conditioned, systems);
        return conditioned;
    }

    private static PlanetaryBodyState CreatePlanet(
        long campaignSeed,
        int id,
        StarSystemState system,
        int orbit,
        bool legacyCandidate,
        bool rareResource,
        bool anomaly,
        bool preWarp,
        ref StableRandom random)
    {
        var name = system.StellarClass is not null
            ? CelestialBodyNamer.PlanetName(campaignSeed, system.Id, orbit)
            : $"{system.Name} {(char)('b' + orbit)}";
        if (legacyCandidate)
        {
            var radius = random.Range(0.78, 1.28);
            var gravity = random.Range(0.72, 1.30);
            var mass = gravity * radius * radius;
            var environment = new PlanetaryEnvironmentState(
                gravity,
                random.Range(255.0, 310.0),
                random.Range(62.0, 155.0),
                random.NextDouble() < 0.82 ? PlanetaryAtmosphereRegime.OxygenNitrogen : PlanetaryAtmosphereRegime.OxygenRich,
                PlanetarySolventRegime.Water,
                Math.Clamp(BaseRadiation(system) + random.Range(0.00, 0.10), 0.0, 0.35),
                IsImmersedEnvironment: random.NextDouble() < 0.18,
                HasSolidSurface: true);

            return new PlanetaryBodyState(
                id, system.Id, null, orbit, name, PlanetaryBodyKind.Planet,
                radius, mass, environment, true, rareResource, anomaly, preWarp);
        }

        var orbitFraction = orbit / 7.0;
        var gasGiant = random.NextDouble() < Math.Clamp(0.10 + orbitFraction * 0.36, 0.10, 0.46);
        if (gasGiant)
        {
            var radius = random.Range(3.4, 10.8);
            var mass = random.Range(18.0, 320.0);
            var gravity = Math.Clamp(mass / (radius * radius), 0.55, 3.8);
            var temperature = Math.Clamp(420.0 - orbit * 48.0 + random.Range(-55.0, 55.0), 35.0, 650.0);
            var gasAtmosphere = random.NextDouble() < 0.72
                ? PlanetaryAtmosphereRegime.Reducing
                : PlanetaryAtmosphereRegime.Inert;
            var gasSolvent = temperature < 135.0 && random.NextDouble() < 0.45
                ? PlanetarySolventRegime.Hydrocarbon
                : PlanetarySolventRegime.None;
            var environment = new PlanetaryEnvironmentState(
                gravity,
                temperature,
                random.Range(7000.0, 180000.0),
                gasAtmosphere,
                gasSolvent,
                Math.Clamp(BaseRadiation(system) + random.Range(0.08, 0.32), 0.0, 1.0),
                IsImmersedEnvironment: false,
                HasSolidSurface: false);

            return new PlanetaryBodyState(
                id, system.Id, null, orbit, name, PlanetaryBodyKind.Planet,
                radius, mass, environment, false, rareResource, anomaly,
                HasPreWarpCivilization: false);
        }

        var rockyRadius = random.Range(0.30, 1.95);
        var densityFactor = random.Range(0.55, 1.55);
        var rockyMass = Math.Max(0.01, rockyRadius * rockyRadius * rockyRadius * densityFactor);
        var rockyGravity = Math.Clamp(rockyMass / (rockyRadius * rockyRadius), 0.03, 3.2);
        var rockyTemperature = Math.Clamp(445.0 - orbit * 54.0 + random.Range(-70.0, 70.0), 28.0, 760.0);
        var atmosphere = SelectRockyAtmosphere(rockyMass, rockyTemperature, ref random);
        var pressure = SelectRockyPressure(atmosphere, rockyMass, ref random);
        var solvent = SelectSolvent(rockyTemperature, pressure, ref random);
        var immersed = solvent != PlanetarySolventRegime.None && random.NextDouble() < 0.13;
        var rockyEnvironment = new PlanetaryEnvironmentState(
            rockyGravity,
            rockyTemperature,
            pressure,
            atmosphere,
            solvent,
            Math.Clamp(BaseRadiation(system) + (pressure < 5.0 ? 0.20 : 0.04) + random.Range(0.00, 0.22), 0.0, 1.0),
            immersed,
            HasSolidSurface: true);

        return new PlanetaryBodyState(
            id, system.Id, null, orbit, name, PlanetaryBodyKind.Planet,
            rockyRadius, rockyMass, rockyEnvironment, false, rareResource, anomaly,
            HasPreWarpCivilization: false);
    }

    private static PlanetaryBodyState CreateMoon(
        long campaignSeed,
        int id,
        PlanetaryBodyState parent,
        int moonIndex,
        bool useProperName,
        ref StableRandom random)
    {
        var radius = random.Range(0.07, Math.Min(0.78, Math.Max(0.13, parent.RadiusEarth * 0.22)));
        var mass = Math.Max(0.0005, radius * radius * radius * random.Range(0.55, 1.35));
        var gravity = Math.Clamp(mass / (radius * radius), 0.005, 0.75);
        var temperature = Math.Clamp(parent.Environment.TemperatureKelvin + random.Range(-22.0, 22.0), 18.0, 780.0);
        var atmosphere = random.NextDouble() < 0.78
            ? PlanetaryAtmosphereRegime.Vacuum
            : (random.NextDouble() < 0.55 ? PlanetaryAtmosphereRegime.Inert : PlanetaryAtmosphereRegime.Reducing);
        var pressure = atmosphere == PlanetaryAtmosphereRegime.Vacuum ? 0.0 : random.Range(0.05, 18.0);
        var solvent = SelectSolvent(temperature, pressure, ref random);
        var environment = new PlanetaryEnvironmentState(
            gravity,
            temperature,
            pressure,
            atmosphere,
            solvent,
            Math.Clamp(parent.Environment.RadiationHazard + random.Range(0.02, 0.24), 0.0, 1.0),
            IsImmersedEnvironment: false,
            HasSolidSurface: true);

        return new PlanetaryBodyState(
            id,
            parent.SystemId,
            parent.Id,
            moonIndex,
            useProperName ? CelestialBodyNamer.MoonName(campaignSeed, parent, moonIndex) : $"{parent.Name}-{moonIndex + 1}",
            PlanetaryBodyKind.Moon,
            radius,
            mass,
            environment,
            LegacyColonizationCandidate: false,
            HasRareResource: false,
            HasAnomaly: false,
            HasPreWarpCivilization: false);
    }

    private static int ResolvePlanetCount(StarArchetype archetype, ref StableRandom random)
    {
        var min = archetype is StarArchetype.BlackHole or StarArchetype.NeutronPulsar ? 1 : 2;
        var maxExclusive = archetype == StarArchetype.Nebula ? 6 : 8;
        return min + random.NextInt(Math.Max(1, maxExclusive - min));
    }

    private static IReadOnlyDictionary<int, int>? BuildBalancedPlanetCounts(
        long campaignSeed,
        IReadOnlyList<StarSystemState> systems)
    {
        var catalogScale = systems.Count % 100 == 0 && systems.Count is >= 500 and <= 2500 &&
            (systems.All(system => system.StellarCatalogId is not null) ||
             systems.All(system => system.StellarClass.HasValue)) ? systems.Count / 100 : 1;
        if (systems.Count != 100 * catalogScale || catalogScale == 1 && systems.Any(system => system.StellarClass is null)) return null;
        var random = new Random(unchecked((int)(campaignSeed ^ (campaignSeed >> 32) ^ 0x504C4E54)));
        var nonSol = systems.Where(system => system.CatalogPresetId != SolCatalogPreset.PresetId).ToList();
        Shuffle(nonSol, random);
        var zeroIds = nonSol
            .Where(system => !system.HasHabitableWorld)
            .OrderBy(system => system.StellarClass is StellarPrimaryClass.BlackHole or StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar or
                StellarPrimaryClass.Protostar ? 0 : 1)
            .Take(18 * catalogScale)
            .Select(system => system.Id)
            .ToHashSet();
        if (zeroIds.Count != 18 * catalogScale)
            throw new InvalidOperationException($"Balanced planetary architecture needs {18 * catalogScale} non-habitable planetless systems.");

        var counts = zeroIds.ToDictionary(id => id, _ => 0);
        var populated = nonSol.Where(system => !zeroIds.Contains(system.Id)).ToList();
        var deck = new List<int>(systems.Count - zeroIds.Count - 1);
        for (var index = 0; index < 22 * catalogScale; index++) deck.Add(1 + index % 2);
        for (var index = 0; index < 42 * catalogScale; index++) deck.Add(3 + index % 4);
        // Sol's authored eight planets occupy one of the fourteen 7–10-system slots.
        for (var index = 0; index < 14 * catalogScale - 1; index++) deck.Add(7 + index % 4);
        for (var index = 0; index < 4 * catalogScale; index++) deck.Add(11 + index % 4);
        Shuffle(deck, random);
        for (var index = 0; index < populated.Count; index++) counts[populated[index].Id] = deck[index];
        counts[SolCatalogPreset.SystemId] = 8;
        return counts;
    }

    private static void Shuffle<T>(IList<T> values, Random random)
    {
        for (var index = values.Count - 1; index > 0; index--)
        {
            var swap = random.Next(index + 1);
            (values[index], values[swap]) = (values[swap], values[index]);
        }
    }

    private static int ResolveMoonCount(PlanetaryBodyState planet, ref StableRandom random)
    {
        if (planet.Kind != PlanetaryBodyKind.Planet)
            return 0;
        if (!planet.Environment.HasSolidSurface)
            return random.NextInt(4);
        if (planet.MassEarth > 1.4 && random.NextDouble() < 0.55)
            return 1 + random.NextInt(2);
        return random.NextDouble() < 0.32 ? 1 : 0;
    }

    private static PlanetaryAtmosphereRegime SelectRockyAtmosphere(
        double massEarth,
        double temperatureKelvin,
        ref StableRandom random)
    {
        if (massEarth < 0.10 || (massEarth < 0.32 && random.NextDouble() < 0.72))
            return PlanetaryAtmosphereRegime.Vacuum;
        if (temperatureKelvin > 430.0)
            return random.NextDouble() < 0.70
                ? PlanetaryAtmosphereRegime.CarbonDioxideRich
                : PlanetaryAtmosphereRegime.Other;
        if (temperatureKelvin < 150.0)
            return random.NextDouble() < 0.62
                ? PlanetaryAtmosphereRegime.Inert
                : PlanetaryAtmosphereRegime.Reducing;

        var roll = random.NextDouble();
        if (roll < 0.34) return PlanetaryAtmosphereRegime.CarbonDioxideRich;
        if (roll < 0.57) return PlanetaryAtmosphereRegime.Reducing;
        if (roll < 0.78) return PlanetaryAtmosphereRegime.Inert;
        if (roll < 0.92) return PlanetaryAtmosphereRegime.Other;
        return PlanetaryAtmosphereRegime.OxygenNitrogen;
    }

    private static double SelectRockyPressure(
        PlanetaryAtmosphereRegime atmosphere,
        double massEarth,
        ref StableRandom random)
    {
        if (atmosphere == PlanetaryAtmosphereRegime.Vacuum)
            return 0.0;

        var retention = Math.Clamp(0.35 + massEarth * 0.55, 0.25, 2.8);
        return atmosphere switch
        {
            PlanetaryAtmosphereRegime.CarbonDioxideRich => random.Range(8.0, 1200.0) * retention,
            PlanetaryAtmosphereRegime.Reducing => random.Range(2.0, 420.0) * retention,
            PlanetaryAtmosphereRegime.Inert => random.Range(0.4, 260.0) * retention,
            PlanetaryAtmosphereRegime.OxygenNitrogen => random.Range(45.0, 165.0) * retention,
            PlanetaryAtmosphereRegime.OxygenRich => random.Range(35.0, 145.0) * retention,
            _ => random.Range(0.2, 310.0) * retention,
        };
    }

    private static PlanetarySolventRegime SelectSolvent(
        double temperatureKelvin,
        double pressureKPa,
        ref StableRandom random)
    {
        if (pressureKPa <= 0.01 || random.NextDouble() < 0.42)
            return PlanetarySolventRegime.None;
        if (temperatureKelvin >= 250.0 && temperatureKelvin <= 390.0)
            return random.NextDouble() < 0.58 ? PlanetarySolventRegime.Water : PlanetarySolventRegime.None;
        if (temperatureKelvin >= 165.0 && temperatureKelvin < 250.0)
            return random.NextDouble() < 0.42 ? PlanetarySolventRegime.Ammonia : PlanetarySolventRegime.None;
        if (temperatureKelvin >= 70.0 && temperatureKelvin < 165.0)
            return random.NextDouble() < 0.46 ? PlanetarySolventRegime.Hydrocarbon : PlanetarySolventRegime.None;
        return random.NextDouble() < 0.04 ? PlanetarySolventRegime.Other : PlanetarySolventRegime.None;
    }

    private static double BaseRadiation(StarArchetype archetype) => archetype switch
    {
        StarArchetype.NeutronPulsar => 0.62,
        StarArchetype.BlackHole => 0.45,
        StarArchetype.Dangerous => 0.36,
        StarArchetype.Nebula => 0.18,
        _ => 0.06,
    };

    private static double BaseRadiation(StarSystemState system) => Math.Max(
        BaseRadiation(system.Archetype),
        system.StellarClass switch
        {
            StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar => 0.62,
            StellarPrimaryClass.BlackHole => 0.45,
            StellarPrimaryClass.HotBlueStar => 0.40,
            StellarPrimaryClass.Protostar => 0.31,
            StellarPrimaryClass.Giant => 0.22,
            StellarPrimaryClass.WhiteDwarf => 0.18,
            _ => 0.06,
        });

    private static void ValidateCatalog(
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<StarSystemState> systems)
    {
        var ids = new HashSet<int>();
        foreach (var body in bodies)
        {
            if (!ids.Add(body.Id))
                throw new InvalidOperationException($"Duplicate planetary body ID {body.Id}.");
            if (!systems.Any(system => system.Id == body.SystemId))
                throw new InvalidOperationException($"Planetary body {body.Id} references unknown system {body.SystemId}.");
        }

        var byId = bodies.ToDictionary(body => body.Id);
        foreach (var moon in bodies.Where(body => body.Kind == PlanetaryBodyKind.Moon))
        {
            if (moon.ParentBodyId is null || !byId.TryGetValue(moon.ParentBodyId.Value, out var parent))
                throw new InvalidOperationException($"Moon {moon.Id} has no valid parent body.");
            if (parent.Kind != PlanetaryBodyKind.Planet || parent.SystemId != moon.SystemId)
                throw new InvalidOperationException($"Moon {moon.Id} parent is not a planet in the same system.");
        }

        foreach (var system in systems.Where(system => system.HasHabitableWorld))
        {
            if (!bodies.Any(body => body.SystemId == system.Id && body.LegacyColonizationCandidate))
                throw new InvalidOperationException($"Legacy habitable system {system.Id} has no compatibility colony candidate.");
        }
    }

    private struct StableRandom
    {
        private ulong _state;

        private StableRandom(ulong state)
        {
            _state = state == 0 ? 0x9E3779B97F4A7C15UL : state;
        }

        public static StableRandom ForSystem(long campaignSeed, int systemId)
        {
            var seed = unchecked((ulong)campaignSeed);
            seed ^= unchecked((ulong)(systemId + 1)) * 0x9E3779B97F4A7C15UL;
            seed ^= seed >> 30;
            seed *= 0xBF58476D1CE4E5B9UL;
            seed ^= seed >> 27;
            seed *= 0x94D049BB133111EBUL;
            seed ^= seed >> 31;
            return new StableRandom(seed);
        }

        public double NextDouble() => (NextUInt64() >> 11) * (1.0 / (1UL << 53));

        public int NextInt(int maxExclusive)
        {
            if (maxExclusive <= 1) return 0;
            return (int)(NextUInt64() % (uint)maxExclusive);
        }

        public double Range(double minimum, double maximum) =>
            minimum + (maximum - minimum) * NextDouble();

        private ulong NextUInt64()
        {
            var x = _state;
            x ^= x >> 12;
            x ^= x << 25;
            x ^= x >> 27;
            _state = x;
            return x * 0x2545F4914F6CDD1DUL;
        }
    }
}

public static class CelestialBodyNamer
{
    private static readonly string[] PlanetNames =
    {
        "Aestra", "Aion", "Arden", "Caelia", "Caligo", "Ceryn", "Damaris", "Eidra",
        "Elara", "Eryon", "Hesper", "Ilyra", "Kaelis", "Liora", "Maeron", "Neris",
        "Orison", "Phaedra", "Quillon", "Rhyssa", "Sereph", "Talora", "Thane", "Umbriel",
        "Vesper", "Viridia", "Xanthe", "Yarrow", "Zephra", "Aurelia", "Corven", "Pelagos",
    };

    private static readonly string[] MoonEpithets =
    {
        "Ari", "Belen", "Cira", "Dysis", "Enna", "Faron", "Galen", "Hira",
        "Ione", "Jora", "Kora", "Lume", "Mira", "Noma", "Oryn", "Prax",
        "Quill", "Rhea", "Sola", "Tarin", "Una", "Vela", "Wren", "Xira",
        "Yana", "Zori", "Aven", "Brin", "Cyra", "Doran", "Eris", "Fira",
    };

    public static string PlanetName(long seed, int systemId, int orbitIndex)
    {
        var offset = StableIndex(seed, systemId, 0x504C414E, PlanetNames.Length);
        return PlanetNames[(offset + orbitIndex * 7) % PlanetNames.Length];
    }

    public static string MoonName(long seed, PlanetaryBodyState parent, int moonIndex)
    {
        var offset = StableIndex(seed, parent.Id, 0x4D4F4F4E, MoonEpithets.Length);
        return $"{parent.Name} {MoonEpithets[(offset + moonIndex * 5) % MoonEpithets.Length]}";
    }

    private static int StableIndex(long seed, int identity, int salt, int count)
    {
        var mixed = unchecked((ulong)seed) ^ unchecked((ulong)(identity + 1)) * 0x9E3779B97F4A7C15UL;
        mixed ^= unchecked((uint)salt);
        mixed ^= mixed >> 30;
        mixed *= 0xBF58476D1CE4E5B9UL;
        mixed ^= mixed >> 27;
        return (int)(mixed % (uint)count);
    }
}
