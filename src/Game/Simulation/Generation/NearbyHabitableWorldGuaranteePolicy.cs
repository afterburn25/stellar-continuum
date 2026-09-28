using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Generation;

/// <summary>Creates fair, undiscovered expansion candidates using the real species environment model.</summary>
public sealed class NearbyHabitableWorldGuaranteePolicy
{
    public const float MaximumOpeningDistance = 340.0f;
    private readonly SpeciesPlanetaryHabitabilityEvaluator _habitability = new();

    public IReadOnlyList<PlanetaryBodyState> Apply(
        long seed,
        IList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<CivilizationState> civilizations,
        int guaranteedPerMajorCivilization)
    {
        var originalSystems = systems.ToArray();
        try
        {
            // Keep the established output for all seeds the greedy reservation can satisfy.
            return ApplyGreedy(seed, systems, bodies, civilizations, guaranteedPerMajorCivilization);
        }
        catch (InvalidOperationException exception) when (exception.Message.StartsWith("Could not place ", StringComparison.Ordinal))
        {
            for (var index = 0; index < systems.Count; index++)
                systems[index] = originalSystems[index];
            return ApplyGlobally(seed, systems, bodies, civilizations, guaranteedPerMajorCivilization);
        }
    }

    private IReadOnlyList<PlanetaryBodyState> ApplyGreedy(
        long seed,
        IList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<CivilizationState> civilizations,
        int guaranteedPerMajorCivilization)
    {
        if (guaranteedPerMajorCivilization <= 0) return bodies;
        var result = bodies.ToDictionary(body => body.Id);
        var homeSystemIds = civilizations.Select(civilization => civilization.HomeSystemId).ToHashSet();
        var reservedSystems = new HashSet<int>(homeSystemIds);

        foreach (var civilization in civilizations.Where(civilization => !civilization.IsSeededAncient)
                     .OrderBy(civilization => civilization.Id))
        {
            var homeSystem = systems.Single(system => system.Id == civilization.HomeSystemId);
            var homeBody = bodies.Where(body => body.SystemId == homeSystem.Id)
                .Select(body => (Body: body, Assessment: _habitability.Evaluate(body, civilization.SpeciesId)))
                .Where(item => item.Assessment.Viability == SpeciesColonizationViability.NaturallyViable)
                .OrderByDescending(item => item.Assessment.Environment.NaturalHabitability)
                .ThenBy(item => item.Body.Id)
                .FirstOrDefault().Body
                ?? throw new InvalidOperationException($"Civilization {civilization.Id} has no natural homeworld for nearby-world guarantees.");

            var candidates = systems
                .Where(system => !reservedSystems.Contains(system.Id) && IsStableCandidateStar(system.StellarClass))
                .Select(system => new
                {
                    System = system,
                    Distance = InterstellarDistance.Between(homeSystem, system),
                    Planets = bodies.Where(body => body.SystemId == system.Id && body.Kind == PlanetaryBodyKind.Planet &&
                        body.Environment.HasSolidSurface && !body.HasPreWarpCivilization).OrderBy(body => body.Id).ToArray(),
                })
                .Where(candidate => candidate.Distance <= MaximumOpeningDistance && candidate.Planets.Length > 0)
                .OrderBy(candidate => candidate.Distance)
                .ThenBy(candidate => candidate.System.Id)
                .ToList();

            var accepted = 0;
            foreach (var candidate in candidates)
            {
                var natural = candidate.Planets.FirstOrDefault(body =>
                    _habitability.Evaluate(result[body.Id], civilization.SpeciesId).Viability ==
                    SpeciesColonizationViability.NaturallyViable);
                if (natural is null) continue;
                reservedSystems.Add(candidate.System.Id);
                accepted++;
                if (accepted == guaranteedPerMajorCivilization) break;
            }

            if (accepted < guaranteedPerMajorCivilization)
            {
                foreach (var candidate in candidates.Where(candidate => !reservedSystems.Contains(candidate.System.Id)))
                {
                    var target = candidate.Planets[(int)(Mix(seed, civilization.Id, candidate.System.Id) %
                        (uint)candidate.Planets.Length)];
                    var radius = target.RadiusEarth;
                    result[target.Id] = target with
                    {
                        MassEarth = Math.Max(0.0005, homeBody.Environment.GravityG * radius * radius),
                        Environment = homeBody.Environment,
                    };
                    var systemIndex = systems.IndexOf(candidate.System);
                    systems[systemIndex] = candidate.System with { HasHabitableWorld = true };
                    reservedSystems.Add(candidate.System.Id);
                    accepted++;
                    if (accepted == guaranteedPerMajorCivilization) break;
                }
            }

            if (accepted != guaranteedPerMajorCivilization)
                throw new InvalidOperationException(
                    $"Could not place {guaranteedPerMajorCivilization} nearby viable worlds for civilization {civilization.Id} within {MaximumOpeningDistance:0} map units.");
        }

        return bodies.Select(body => result[body.Id].Validated()).ToArray();
    }

    private IReadOnlyList<PlanetaryBodyState> ApplyGlobally(
        long seed,
        IList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<CivilizationState> civilizations,
        int guaranteedPerMajorCivilization)
    {
        var result = bodies.ToDictionary(body => body.Id);
        var homeSystemIds = civilizations.Select(civilization => civilization.HomeSystemId).ToHashSet();
        var requirements = civilizations.Where(civilization => !civilization.IsSeededAncient)
            .OrderBy(civilization => civilization.Id)
            .SelectMany(civilization => BuildGlobalRequirements(
                seed, systems, bodies, result, homeSystemIds, civilization, guaranteedPerMajorCivilization))
            .ToArray();
        var assignments = new Dictionary<int, ExpansionRequirement>();

        // Match every civilization's two slots before changing a body.  This frees a shared
        // candidate for a later civilization when an earlier one has an alternative.
        foreach (var requirement in requirements
                     .OrderBy(requirement => requirement.Candidates.Count)
                     .ThenBy(requirement => requirement.Civilization.Id)
                     .ThenBy(requirement => requirement.Slot))
        {
            if (!TryAssign(requirement, assignments, new HashSet<int>()))
                throw CreateGlobalPlacementException(seed, requirement, requirements, assignments);
        }

        foreach (var assignment in assignments.OrderBy(pair => pair.Key))
        {
            var candidate = assignment.Value.Candidates.Single(candidate => candidate.System.Id == assignment.Key);
            if (candidate.NaturalBody is not null)
                continue;

            var target = candidate.FallbackBody;
            var environment = assignment.Value.HomeBody.Environment;
            result[target.Id] = target with
            {
                MassEarth = Math.Max(0.0005, environment.GravityG * target.RadiusEarth * target.RadiusEarth),
                Environment = environment,
            };
            var systemIndex = systems.IndexOf(candidate.System);
            systems[systemIndex] = candidate.System with { HasHabitableWorld = true };
        }

        return bodies.Select(body => result[body.Id].Validated()).ToArray();
    }

    private IEnumerable<ExpansionRequirement> BuildGlobalRequirements(
        long seed,
        IList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyDictionary<int, PlanetaryBodyState> result,
        ISet<int> homeSystemIds,
        CivilizationState civilization,
        int guaranteedPerMajorCivilization)
    {
        var homeSystem = systems.Single(system => system.Id == civilization.HomeSystemId);
        var homeBody = bodies.Where(body => body.SystemId == homeSystem.Id)
            .Select(body => (Body: body, Assessment: _habitability.Evaluate(body, civilization.SpeciesId)))
            .Where(item => item.Assessment.Viability == SpeciesColonizationViability.NaturallyViable)
            .OrderByDescending(item => item.Assessment.Environment.NaturalHabitability)
            .ThenBy(item => item.Body.Id)
            .FirstOrDefault().Body
            ?? throw new InvalidOperationException($"Civilization {civilization.Id} has no natural homeworld for nearby-world guarantees.");
        var candidates = systems
            .Where(system => !homeSystemIds.Contains(system.Id) && IsStableCandidateStar(system.StellarClass))
            .Select(system => new
            {
                System = system,
                Distance = InterstellarDistance.Between(homeSystem, system),
                Planets = bodies.Where(body => body.SystemId == system.Id && body.Kind == PlanetaryBodyKind.Planet &&
                    body.Environment.HasSolidSurface && !body.HasPreWarpCivilization).OrderBy(body => body.Id).ToArray(),
            })
            .Where(candidate => candidate.Distance <= MaximumOpeningDistance && candidate.Planets.Length > 0)
            .Select(candidate => new ExpansionCandidate(
                candidate.System,
                candidate.Distance,
                candidate.Planets.FirstOrDefault(body =>
                    _habitability.Evaluate(result[body.Id], civilization.SpeciesId).Viability ==
                    SpeciesColonizationViability.NaturallyViable),
                candidate.Planets[(int)(Mix(seed, civilization.Id, candidate.System.Id) % (uint)candidate.Planets.Length)]))
            .OrderBy(candidate => candidate.NaturalBody is null)
            .ThenBy(candidate => candidate.Distance)
            .ThenBy(candidate => candidate.System.Id)
            .ToArray();

        for (var slot = 0; slot < guaranteedPerMajorCivilization; slot++)
            yield return new ExpansionRequirement(civilization, homeBody, slot, candidates);
    }

    private static bool TryAssign(
        ExpansionRequirement requirement,
        IDictionary<int, ExpansionRequirement> assignments,
        ISet<int> visitedSystems)
    {
        foreach (var candidate in requirement.Candidates)
        {
            if (!visitedSystems.Add(candidate.System.Id))
                continue;
            if (!assignments.TryGetValue(candidate.System.Id, out var assigned) ||
                TryAssign(assigned, assignments, visitedSystems))
            {
                assignments[candidate.System.Id] = requirement;
                return true;
            }
        }

        return false;
    }

    private static InvalidOperationException CreateGlobalPlacementException(
        long seed,
        ExpansionRequirement requirement,
        IReadOnlyList<ExpansionRequirement> requirements,
        IReadOnlyDictionary<int, ExpansionRequirement> assignments)
    {
        var candidateSystems = requirements.SelectMany(value => value.Candidates)
            .Select(candidate => candidate.System.Id).Distinct().Count();
        return new InvalidOperationException(
            $"Could not place nearby viable worlds for civilization {requirement.Civilization.Id} within " +
            $"{MaximumOpeningDistance:0} map units for seed {seed}; slot {requirement.Slot + 1} has " +
            $"{requirement.Candidates.Count} eligible systems, {assignments.Count}/{requirements.Count} slots " +
            $"were assigned, and the global candidate pool contains {candidateSystems} systems.");
    }

    private static bool IsStableCandidateStar(StellarPrimaryClass? stellarClass) => stellarClass is not
        (StellarPrimaryClass.BlackHole or StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar or StellarPrimaryClass.HotBlueStar or
         StellarPrimaryClass.Giant or StellarPrimaryClass.Protostar);

    private static uint Mix(long seed, int civilizationId, int systemId)
    {
        var value = unchecked((ulong)seed) ^ (uint)civilizationId * 0x9E3779B9UL ^ (uint)systemId * 0x85EBCA6BUL;
        value ^= value >> 30;
        value *= 0xBF58476D1CE4E5B9UL;
        value ^= value >> 27;
        return (uint)(value ^ (value >> 32));
    }

    private sealed record ExpansionRequirement(
        CivilizationState Civilization,
        PlanetaryBodyState HomeBody,
        int Slot,
        IReadOnlyList<ExpansionCandidate> Candidates);

    private sealed record ExpansionCandidate(
        StarSystemState System,
        double Distance,
        PlanetaryBodyState? NaturalBody,
        PlanetaryBodyState FallbackBody);
}
