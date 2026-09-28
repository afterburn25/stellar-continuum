using System;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Colonization;

/// <summary>
/// Resolves the deterministic settlement body for a legacy/body-less colony mission.
/// Exact body-aware mission intent is resolved by the owning caller and is never re-ranked here.
///
/// This resolver composes existing authoritative boundaries only: completed observer-local survey
/// knowledge, the current single-colony-per-system occupancy rule, and Species-owned planetary
/// colonization viability. It owns no biology, Logistics reach, AI value, or persistent state.
/// </summary>
public sealed class ColonySettlementBodyResolver
{
    private readonly SpeciesPlanetaryHabitabilityEvaluator _habitability = new();

    public PlanetaryBodyState? ResolveBestAvailableBody(
        GalaxyState galaxy,
        int civilizationId,
        int systemId,
        string speciesId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        _ = SpeciesCatalog.Get(speciesId);

        if (!galaxy.Civilizations.Any(civilization => civilization.Id == civilizationId))
            throw new InvalidOperationException($"Unknown civilization {civilizationId}.");
        if (!galaxy.Systems.Any(system => system.Id == systemId))
            return null;
        if (!galaxy.Knowledge.IsSystemFullySurveyed(civilizationId, systemId))
            return null;
        if (galaxy.Colonies.Any(colony => colony.SystemId == systemId))
            return null;

        return galaxy.PlanetaryBodies
            .Where(body => body.SystemId == systemId)
            .Select(body => new
            {
                Body = body,
                Assessment = _habitability.Evaluate(body, speciesId),
            })
            .Where(candidate => candidate.Assessment.CanFoundCurrentColony)
            .OrderByDescending(candidate => candidate.Assessment.Viability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.NaturalHabitability)
            .ThenByDescending(candidate => candidate.Assessment.Environment.UnprotectedOperationalCapacity)
            .ThenBy(candidate => candidate.Body.Id)
            .Select(candidate => candidate.Body)
            .FirstOrDefault();
    }
}
