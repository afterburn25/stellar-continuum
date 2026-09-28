using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Models;
using Game.Simulation.Generation;

namespace Game.Simulation.Species;

public sealed record SpeciesHomeworldAssignment(
    int CivilizationId,
    string SpeciesId,
    int SystemId,
    int PlanetaryBodyId,
    double NaturalHabitability,
    SpeciesSettlementSuitability Suitability);

/// <summary>
/// Selects distinct natural founding worlds from already-generated physical planetary facts.
/// It never rewrites a planet, invents habitat support, or consults civilization personality.
/// Species IDs are supplied explicitly so the assignment remains independent of AI archetype.
/// </summary>
public sealed class SpeciesHomeworldPlanner
{
    private const int MaximumConstrainedSearchStates = 100_000;
    private readonly PlanetarySpeciesHabitabilityEvaluator _habitability = new();

    public IReadOnlyList<SpeciesHomeworldAssignment> Plan(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<string> speciesIds)
    {
        ArgumentNullException.ThrowIfNull(systems);
        ArgumentNullException.ThrowIfNull(bodies);
        ArgumentNullException.ThrowIfNull(speciesIds);

        if (speciesIds.Count == 0)
            return Array.Empty<SpeciesHomeworldAssignment>();
        if (systems.Count < speciesIds.Count)
            throw new InvalidOperationException("There are fewer star systems than founding civilizations.");

        var systemsById = systems.ToDictionary(system => system.Id);
        var candidateSets = speciesIds
            .Select((speciesId, civilizationId) => new
            {
                CivilizationId = civilizationId,
                Species = SpeciesCatalog.Get(speciesId),
            })
            .Select(entry => new CandidateSet(
                entry.CivilizationId,
                entry.Species.Id,
                BuildCandidates(entry.Species, bodies, systemsById)))
            .ToArray();

        var empty = candidateSets.Where(set => set.Candidates.Count == 0).ToArray();
        if (empty.Length > 0)
        {
            throw new InvalidOperationException(
                "Natural homeworld planning failed because no compatible uninhabited body exists for: " +
                string.Join(", ", empty.Select(set => $"civ {set.CivilizationId} / {set.SpeciesId}")) + ".");
        }

        // Assign the most constrained species first, then civilization ID for deterministic ties.
        // This avoids an abundant Terran-like species consuming the only suitable system for a
        // rarer environmental niche.
        var remaining = candidateSets
            .OrderBy(set => set.Candidates.Select(candidate => candidate.System.Id).Distinct().Count())
            .ThenBy(set => set.CivilizationId)
            .ToList();
        var chosen = new List<SpeciesHomeworldAssignment>(speciesIds.Count);
        var occupiedSystems = new HashSet<int>();

        while (remaining.Count > 0)
        {
            var set = remaining[0];
            remaining.RemoveAt(0);

            var candidate = set.Candidates
                .Where(item => !occupiedSystems.Contains(item.System.Id))
                .OrderByDescending(item => ScoreCandidate(item, chosen, systemsById))
                .ThenByDescending(item => item.Assessment.NaturalHabitability)
                .ThenBy(item => item.System.Id)
                .ThenBy(item => item.Body.Id)
                .FirstOrDefault();

            if (candidate is null)
            {
                var availableSystems = set.Candidates
                    .Select(item => item.System.Id)
                    .Distinct()
                    .OrderBy(id => id)
                    .ToArray();
                throw new InvalidOperationException(
                    $"Natural homeworld planning exhausted distinct systems for civ {set.CivilizationId} / {set.SpeciesId}. " +
                    $"Compatible systems: [{string.Join(",", availableSystems)}].");
            }

            occupiedSystems.Add(candidate.System.Id);
            chosen.Add(new SpeciesHomeworldAssignment(
                set.CivilizationId,
                set.SpeciesId,
                candidate.System.Id,
                candidate.Body.Id,
                candidate.Assessment.NaturalHabitability,
                candidate.Assessment.Suitability));
        }

        return chosen.OrderBy(assignment => assignment.CivilizationId).ToArray();
    }

    /// <summary>
    /// Fresh-generation fallback for a sparse opening map. It chooses distinct natural homes
    /// only when those homes also leave a complete, distinct two-system expansion assignment
    /// for every ordinary civilization. The normal planner remains the default path.
    /// </summary>
    public IReadOnlyList<SpeciesHomeworldAssignment> PlanWithNearbyExpansionGuarantees(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyList<string> speciesIds,
        int majorCivilizationCount,
        int maximumSearchStates = MaximumConstrainedSearchStates)
    {
        ArgumentNullException.ThrowIfNull(systems);
        ArgumentNullException.ThrowIfNull(bodies);
        ArgumentNullException.ThrowIfNull(speciesIds);
        if (majorCivilizationCount < 1 || majorCivilizationCount > speciesIds.Count)
            throw new ArgumentOutOfRangeException(nameof(majorCivilizationCount));
        if (maximumSearchStates < 1)
            throw new ArgumentOutOfRangeException(nameof(maximumSearchStates));

        var systemsById = systems.ToDictionary(system => system.Id);
        var expansionEligibleSystemIds = systems
            .Where(system => IsStableExpansionStar(system.StellarClass) &&
                bodies.Any(body => body.SystemId == system.Id && body.Kind == PlanetaryBodyKind.Planet &&
                    body.Environment.HasSolidSurface && !body.HasPreWarpCivilization))
            .Select(system => system.Id)
            .ToHashSet();
        var options = speciesIds.Select((speciesId, civilizationId) => new ConstrainedHomeSet(
                civilizationId,
                BuildCandidates(SpeciesCatalog.Get(speciesId), bodies, systemsById)
                    .GroupBy(candidate => candidate.System.Id)
                    .Select(group => group.OrderByDescending(candidate => WithinSystemScore(candidate.Assessment))
                        .ThenByDescending(candidate => candidate.Assessment.NaturalHabitability)
                        .ThenBy(candidate => candidate.Body.Id).First())
                    .OrderByDescending(candidate => WithinSystemScore(candidate.Assessment))
                    .ThenByDescending(candidate => candidate.Assessment.NaturalHabitability)
                    .ThenBy(candidate => candidate.System.Id)
                    .ToArray()))
            .ToArray();
        var missing = options.Where(option => option.Candidates.Count == 0).ToArray();
        if (missing.Length > 0)
            throw new InvalidOperationException("Constrained homeworld planning found no natural home for " +
                string.Join(", ", missing.Select(option => $"civ {option.CivilizationId}")) + ".");

        var ordered = options.OrderBy(option => option.Candidates.Count)
            .ThenBy(option => option.CivilizationId).ToArray();
        var chosen = new HomeworldCandidate?[speciesIds.Count];
        var occupied = new HashSet<int>();
        var exploredStates = 0;
        var searchBudgetExhausted = false;
        if (!TryPlanConstrainedHomes(0))
        {
            var outcome = searchBudgetExhausted
                ? "Constrained natural-home search budget exhausted"
                : "No complete natural-home and nearby-expansion assignment exists";
            throw new InvalidOperationException(
                $"{outcome} after {exploredStates} states; major civilizations={majorCivilizationCount}, systems={systems.Count}.");
        }

        return chosen.Select((candidate, civilizationId) => candidate is null
                ? throw new InvalidOperationException("Constrained homeworld planner returned an incomplete assignment.")
                : new SpeciesHomeworldAssignment(
                    civilizationId,
                    speciesIds[civilizationId],
                    candidate.System.Id,
                    candidate.Body.Id,
                    candidate.Assessment.NaturalHabitability,
                    candidate.Assessment.Suitability))
            .ToArray();

        bool TryPlanConstrainedHomes(int next)
        {
            if (++exploredStates > maximumSearchStates)
            {
                searchBudgetExhausted = true;
                return false;
            }
            if (next == ordered.Length)
                return HasCompleteExpansionAssignment(systems, expansionEligibleSystemIds, chosen, majorCivilizationCount);

            var set = ordered[next];
            foreach (var candidate in set.Candidates)
            {
                if (!occupied.Add(candidate.System.Id))
                    continue;
                chosen[set.CivilizationId] = candidate;
                if (HasPartialExpansionAssignment(systems, expansionEligibleSystemIds, chosen, majorCivilizationCount) &&
                    TryPlanConstrainedHomes(next + 1))
                    return true;
                if (searchBudgetExhausted)
                    return false;
                chosen[set.CivilizationId] = null;
                occupied.Remove(candidate.System.Id);
            }

            return false;
        }
    }

    /// <summary>
    /// Resolves the exact founding body after a civilization's home system has already been
    /// selected by <see cref="Plan"/>. This uses the same natural-colonizability and within-system
    /// ranking rules, allowing colony seeding to anchor itself without persisting a second
    /// homeworld field on CivilizationState.
    /// </summary>
    public SpeciesHomeworldAssignment ResolveWithinSystem(
        int civilizationId,
        string speciesId,
        int systemId,
        IReadOnlyList<PlanetaryBodyState> bodies)
    {
        ArgumentNullException.ThrowIfNull(bodies);
        var species = SpeciesCatalog.Get(speciesId);

        var candidate = bodies
            .Where(body => body.SystemId == systemId && !body.HasPreWarpCivilization)
            .Select(body => new
            {
                Body = body,
                Assessment = _habitability.Evaluate(species, body),
            })
            .Where(entry => entry.Assessment.NaturallyColonizable)
            .OrderByDescending(entry => WithinSystemScore(entry.Assessment))
            .ThenByDescending(entry => entry.Assessment.NaturalHabitability)
            .ThenBy(entry => entry.Body.Id)
            .FirstOrDefault();

        if (candidate is null)
        {
            throw new InvalidOperationException(
                $"Planned home system {systemId} has no naturally viable body for civ {civilizationId} / {speciesId}.");
        }

        return new SpeciesHomeworldAssignment(
            civilizationId,
            speciesId,
            systemId,
            candidate.Body.Id,
            candidate.Assessment.NaturalHabitability,
            candidate.Assessment.Suitability);
    }

    private static bool HasCompleteExpansionAssignment(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlySet<int> expansionEligibleSystemIds,
        IReadOnlyList<HomeworldCandidate?> homes,
        int majorCivilizationCount)
    {
        var homeSystems = homes.Select(home => home!.System.Id).ToHashSet();
        var candidatesByCivilization = homes.Take(majorCivilizationCount)
            .Select(home => systems.Where(system => !homeSystems.Contains(system.Id) &&
                    IsStableExpansionStar(system.StellarClass) &&
                    InterstellarDistance.Between(home!.System, system) <=
                        NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance &&
                    expansionEligibleSystemIds.Contains(system.Id))
                .OrderBy(system => InterstellarDistance.Between(home!.System, system))
                .ThenBy(system => system.Id).ToArray())
            .ToArray();
        if (candidatesByCivilization.Any(candidates => candidates.Length < 2))
            return false;

        var assigned = new Dictionary<int, int>();
        foreach (var slot in Enumerable.Range(0, majorCivilizationCount * 2)
                     .OrderBy(slot => candidatesByCivilization[slot / 2].Length)
                     .ThenBy(slot => slot))
        {
            if (!TryAssignExpansion(slot, candidatesByCivilization, assigned, new HashSet<int>()))
                return false;
        }

        return true;
    }

    private static bool HasPartialExpansionAssignment(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlySet<int> expansionEligibleSystemIds,
        IReadOnlyList<HomeworldCandidate?> homes,
        int majorCivilizationCount)
    {
        var selected = homes.Take(majorCivilizationCount).Where(home => home is not null)
            .Select(home => home!).ToArray();
        if (selected.Length == 0)
            return true;
        var occupiedHomes = homes.Where(home => home is not null).Select(home => home!.System.Id).ToHashSet();
        var candidates = selected.Select(home => systems.Where(system => !occupiedHomes.Contains(system.Id) &&
                IsStableExpansionStar(system.StellarClass) &&
                InterstellarDistance.Between(home.System, system) <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance &&
                expansionEligibleSystemIds.Contains(system.Id))
            .OrderBy(system => system.Id).ToArray()).ToArray();
        if (candidates.Any(candidate => candidate.Length < 2))
            return false;
        var assignments = new Dictionary<int, int>();
        foreach (var slot in Enumerable.Range(0, selected.Length * 2)
                     .OrderBy(slot => candidates[slot / 2].Length).ThenBy(slot => slot))
            if (!TryAssignExpansion(slot, candidates, assignments, new HashSet<int>()))
                return false;
        return true;
    }

    private static bool TryAssignExpansion(
        int slot,
        IReadOnlyList<StarSystemState[]> candidatesByCivilization,
        IDictionary<int, int> assignments,
        ISet<int> visited)
    {
        foreach (var candidate in candidatesByCivilization[slot / 2])
        {
            if (!visited.Add(candidate.Id))
                continue;
            if (!assignments.TryGetValue(candidate.Id, out var assignedSlot) ||
                TryAssignExpansion(assignedSlot, candidatesByCivilization, assignments, visited))
            {
                assignments[candidate.Id] = slot;
                return true;
            }
        }

        return false;
    }

    private static bool IsStableExpansionStar(StellarPrimaryClass? stellarClass) => stellarClass is not
        (StellarPrimaryClass.BlackHole or StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar or StellarPrimaryClass.HotBlueStar or
         StellarPrimaryClass.Giant or StellarPrimaryClass.Protostar);

    private IReadOnlyList<HomeworldCandidate> BuildCandidates(
        SpeciesDefinition species,
        IReadOnlyList<PlanetaryBodyState> bodies,
        IReadOnlyDictionary<int, StarSystemState> systemsById)
    {
        var hasCanonicalSol = systemsById.Values.Any(SolCatalogPreset.IsSol);
        return bodies
            .Where(body => !body.HasPreWarpCivilization)
            .Where(body => !hasCanonicalSol || (species.Id == SpeciesCatalog.TerranBaselineId
                ? body.SystemId == SolCatalogPreset.SystemId && body.Id == SolCatalogPreset.EarthBodyId
                : body.SystemId != SolCatalogPreset.SystemId))
            .Select(body => new
            {
                Body = body,
                Assessment = _habitability.Evaluate(species, body),
            })
            .Where(entry => entry.Assessment.NaturallyColonizable)
            .Select(entry => new HomeworldCandidate(
                systemsById[entry.Body.SystemId],
                entry.Body,
                entry.Assessment))
            .ToArray();
    }

    private static double ScoreCandidate(
        HomeworldCandidate candidate,
        IReadOnlyList<SpeciesHomeworldAssignment> chosen,
        IReadOnlyDictionary<int, StarSystemState> systemsById)
    {
        // Habitability remains the dominant criterion. A smaller spread term preserves the
        // game's existing preference for geographically separated civilizations.
        var spread = chosen.Count == 0
            ? 0.0
            : chosen.Min(existing =>
                InterstellarDistance.SquaredBetween(candidate.System, systemsById[existing.SystemId]));
        var normalizedSpread = Math.Min(1.0, Math.Sqrt(spread) / 500.0);

        return WithinSystemScore(candidate.Assessment) + normalizedSpread;
    }

    private static double WithinSystemScore(PlanetarySpeciesHabitabilityAssessment assessment)
    {
        var suitabilityBonus = assessment.Suitability == SpeciesSettlementSuitability.Comfortable
            ? 0.35
            : 0.0;
        return assessment.NaturalHabitability * 10.0 + suitabilityBonus;
    }

    private sealed record CandidateSet(
        int CivilizationId,
        string SpeciesId,
        IReadOnlyList<HomeworldCandidate> Candidates);

    private sealed record ConstrainedHomeSet(
        int CivilizationId,
        IReadOnlyList<HomeworldCandidate> Candidates);

    private sealed record HomeworldCandidate(
        StarSystemState System,
        PlanetaryBodyState Body,
        PlanetarySpeciesHabitabilityAssessment Assessment);
}
