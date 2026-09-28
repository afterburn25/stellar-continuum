using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Generation;

/// <summary>
/// Adds a bounded, species-neutral spread of physically distinct biosphere niches to the
/// already-generated planet catalog before civilizations are seeded. It does not know Species
/// IDs, civilization count, AI traits, or future homeworld choices.
///
/// Four anchor environments repeat across the first half of each eight-system ID cycle:
/// temperate surface water, high-pressure ocean, high-gravity surface, and cryogenic
/// hydrocarbon. The other four systems remain entirely procedural. This guarantees environmental
/// diversity without making every star system a preferred biological niche.
/// </summary>
public sealed class PlanetaryEnvironmentalDiversityPolicy
{
    private const int CycleLength = 8;

    public IReadOnlyList<PlanetaryBodyState> Apply(
        long campaignSeed,
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies)
    {
        ArgumentNullException.ThrowIfNull(systems);
        ArgumentNullException.ThrowIfNull(bodies);

        var result = bodies.ToDictionary(body => body.Id);
        foreach (var system in systems.OrderBy(system => system.Id))
        {
            // Authored, versioned physical worlds must never become random diversity anchors.
            if (system.CatalogPresetId is not null)
                continue;
            var profile = ResolveProfile(system.Id);
            if (profile is null)
                continue;

            var target = bodies
                .Where(body => body.SystemId == system.Id && body.Kind == PlanetaryBodyKind.Planet)
                .OrderBy(body => body.HasPreWarpCivilization ? 1 : 0)
                .ThenBy(body => body.Environment.HasSolidSurface ? 0 : 1)
                .ThenBy(body => body.LegacyColonizationCandidate ? 0 : 1)
                .ThenBy(body => body.Id)
                .FirstOrDefault();

            if (target is null && (system.StellarClass is not null || system.StellarCatalogId is not null))
                continue; // Balanced Sandbox profiles deliberately contain planetless stars.
            if (target is null)
                throw new InvalidOperationException($"System {system.Id} has no planet available for its diversity anchor.");

            result[target.Id] = ApplyProfile(campaignSeed, target, profile.Value).Validated();
        }

        var ordered = bodies.Select(body => result[body.Id]).ToArray();
        ValidateIdentityPreservation(bodies, ordered);
        return ordered;
    }

    private static EnvironmentalDiversityProfile? ResolveProfile(int systemId)
    {
        var cycle = ((systemId % CycleLength) + CycleLength) % CycleLength;
        return cycle switch
        {
            0 => EnvironmentalDiversityProfile.TemperateSurfaceWater,
            1 => EnvironmentalDiversityProfile.HighPressureOcean,
            2 => EnvironmentalDiversityProfile.HighGravitySurface,
            3 => EnvironmentalDiversityProfile.CryogenicHydrocarbon,
            _ => null,
        };
    }

    private static PlanetaryBodyState ApplyProfile(
        long campaignSeed,
        PlanetaryBodyState body,
        EnvironmentalDiversityProfile profile)
    {
        var gravity = profile switch
        {
            EnvironmentalDiversityProfile.TemperateSurfaceWater => Jitter(campaignSeed, body.Id, 11, 1.00, 0.05),
            EnvironmentalDiversityProfile.HighPressureOcean => Jitter(campaignSeed, body.Id, 12, 0.85, 0.05),
            EnvironmentalDiversityProfile.HighGravitySurface => Jitter(campaignSeed, body.Id, 13, 1.75, 0.08),
            EnvironmentalDiversityProfile.CryogenicHydrocarbon => Jitter(campaignSeed, body.Id, 14, 0.14, 0.02),
            _ => throw new ArgumentOutOfRangeException(nameof(profile)),
        };
        var radius = profile switch
        {
            EnvironmentalDiversityProfile.TemperateSurfaceWater => Jitter(campaignSeed, body.Id, 21, 1.00, 0.07),
            EnvironmentalDiversityProfile.HighPressureOcean => Jitter(campaignSeed, body.Id, 22, 0.95, 0.07),
            EnvironmentalDiversityProfile.HighGravitySurface => Jitter(campaignSeed, body.Id, 23, 1.00, 0.06),
            EnvironmentalDiversityProfile.CryogenicHydrocarbon => Jitter(campaignSeed, body.Id, 24, 0.85, 0.06),
            _ => throw new ArgumentOutOfRangeException(nameof(profile)),
        };
        var environment = profile switch
        {
            EnvironmentalDiversityProfile.TemperateSurfaceWater => new PlanetaryEnvironmentState(
                gravity,
                Jitter(campaignSeed, body.Id, 31, 288.0, 4.0),
                Jitter(campaignSeed, body.Id, 32, 101.3, 8.0),
                PlanetaryAtmosphereRegime.OxygenNitrogen,
                PlanetarySolventRegime.Water,
                Jitter(campaignSeed, body.Id, 33, 0.06, 0.02),
                IsImmersedEnvironment: false,
                HasSolidSurface: true),
            EnvironmentalDiversityProfile.HighPressureOcean => new PlanetaryEnvironmentState(
                gravity,
                Jitter(campaignSeed, body.Id, 41, 282.0, 4.0),
                Jitter(campaignSeed, body.Id, 42, 350.0, 24.0),
                PlanetaryAtmosphereRegime.OxygenNitrogen,
                PlanetarySolventRegime.Water,
                Jitter(campaignSeed, body.Id, 43, 0.08, 0.025),
                IsImmersedEnvironment: true,
                HasSolidSurface: true),
            EnvironmentalDiversityProfile.HighGravitySurface => new PlanetaryEnvironmentState(
                gravity,
                Jitter(campaignSeed, body.Id, 51, 300.0, 4.0),
                Jitter(campaignSeed, body.Id, 52, 160.0, 12.0),
                PlanetaryAtmosphereRegime.OxygenRich,
                PlanetarySolventRegime.Water,
                Jitter(campaignSeed, body.Id, 53, 0.10, 0.025),
                IsImmersedEnvironment: false,
                HasSolidSurface: true),
            EnvironmentalDiversityProfile.CryogenicHydrocarbon => new PlanetaryEnvironmentState(
                gravity,
                Jitter(campaignSeed, body.Id, 61, 94.0, 4.0),
                Jitter(campaignSeed, body.Id, 62, 150.0, 12.0),
                PlanetaryAtmosphereRegime.Reducing,
                PlanetarySolventRegime.Hydrocarbon,
                Jitter(campaignSeed, body.Id, 63, 0.12, 0.025),
                IsImmersedEnvironment: false,
                HasSolidSurface: true),
            _ => throw new ArgumentOutOfRangeException(nameof(profile)),
        };

        // Keep body identity, orbit, legacy markers, resources/anomalies and any existing native
        // civilization marker. Only the physical radius/mass/environment are conditioned here.
        return body with
        {
            RadiusEarth = radius,
            MassEarth = Math.Max(0.0005, gravity * radius * radius),
            Environment = environment.Validated(),
        };
    }

    private static double Jitter(
        long campaignSeed,
        int bodyId,
        int salt,
        double center,
        double halfRange)
    {
        var mixed = unchecked((ulong)campaignSeed);
        mixed ^= unchecked((ulong)(bodyId + 1)) * 0x9E3779B97F4A7C15UL;
        mixed ^= unchecked((ulong)(salt + 1)) * 0xBF58476D1CE4E5B9UL;
        mixed ^= mixed >> 30;
        mixed *= 0xBF58476D1CE4E5B9UL;
        mixed ^= mixed >> 27;
        mixed *= 0x94D049BB133111EBUL;
        mixed ^= mixed >> 31;
        var unit = (mixed >> 11) * (1.0 / (1UL << 53));
        return center + (unit * 2.0 - 1.0) * halfRange;
    }

    private static void ValidateIdentityPreservation(
        IReadOnlyList<PlanetaryBodyState> before,
        IReadOnlyList<PlanetaryBodyState> after)
    {
        if (before.Count != after.Count)
            throw new InvalidOperationException("Environmental diversity conditioning changed planetary body count.");

        for (var index = 0; index < before.Count; index++)
        {
            var original = before[index];
            var conditioned = after[index];
            if (original.Id != conditioned.Id ||
                original.SystemId != conditioned.SystemId ||
                original.ParentBodyId != conditioned.ParentBodyId ||
                original.OrbitIndex != conditioned.OrbitIndex ||
                original.Kind != conditioned.Kind ||
                original.Name != conditioned.Name ||
                original.LegacyColonizationCandidate != conditioned.LegacyColonizationCandidate ||
                original.HasRareResource != conditioned.HasRareResource ||
                original.HasAnomaly != conditioned.HasAnomaly ||
                original.HasPreWarpCivilization != conditioned.HasPreWarpCivilization)
            {
                throw new InvalidOperationException(
                    $"Environmental diversity conditioning changed identity/non-environment facts for planetary body {original.Id}.");
            }
        }
    }

    private enum EnvironmentalDiversityProfile
    {
        TemperateSurfaceWater,
        HighPressureOcean,
        HighGravitySurface,
        CryogenicHydrocarbon,
    }
}
