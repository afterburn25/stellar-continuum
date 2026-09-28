using System;
using System.Linq;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.Simulation.AI;

/// <summary>
/// Builds the civilization's own strategic-state input from authoritative systems while
/// preserving the fair-information boundary for foreign state. Rival information is still
/// supplied separately through KnowledgeSnapshot.
/// </summary>
public sealed class CivilizationStrategicInputBuilder
{
    private readonly IEconomyLogisticsView _logisticsView;
    private readonly IShipbuildingCapabilityView _shipbuildingCapabilities;
    private readonly ExplorationMissionPlanner _explorationMissionPlanner;
    private readonly SpeciesPlanetaryHabitabilityEvaluator _habitability = new();

    public CivilizationStrategicInputBuilder(
        IEconomyLogisticsView? logisticsView = null,
        IShipbuildingCapabilityView? shipbuildingCapabilities = null,
        ExplorationMissionPlanner? explorationMissionPlanner = null)
    {
        _logisticsView = logisticsView ?? new PrototypeEconomyLogisticsView();
        _shipbuildingCapabilities = shipbuildingCapabilities ?? new PrototypeShipbuildingCapabilityView();
        _explorationMissionPlanner = explorationMissionPlanner ?? new ExplorationMissionPlanner();
    }

    public CivilizationOwnState Build(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        var civilization = galaxy.Civilizations.FirstOrDefault(state => state.Id == civilizationId)
            ?? throw new InvalidOperationException($"Unknown civilization {civilizationId}.");
        var economy = galaxy.Economies.FirstOrDefault(state => state.CivilizationId == civilizationId)
            ?? throw new InvalidOperationException($"Civilization {civilizationId} has no economy state.");
        var technology = galaxy.Technologies.FirstOrDefault(state => state.CivilizationId == civilizationId)
            ?? throw new InvalidOperationException($"Civilization {civilizationId} has no technology state.");
        var construction = galaxy.ConstructionStates.FirstOrDefault(state => state.CivilizationId == civilizationId)
            ?? throw new InvalidOperationException($"Civilization {civilizationId} has no construction state.");
        var logistics = _logisticsView.GetSnapshot(galaxy, civilizationId);

        var colonizedSystemIds = galaxy.Colonies
            .Select(colony => colony.SystemId)
            .ToHashSet();

        // Current colonies expose the population species actually available to seed expansion.
        // This is already multi-species-ready at the query boundary even though each individual
        // ColonyState still owns one scalar species population in the early-release model.
        var populationSpeciesIds = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId && colony.PopulationMillions > 0.0)
            .Select(colony => colony.PopulationSpeciesId)
            .Append(civilization.SpeciesId)
            .Distinct(StringComparer.Ordinal)
            .ToArray();

        var activeFleets = galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == civilizationId)
            .ToArray();

        // Exploration owns legitimate survey-work and reach assessment. Strategy only asks whether
        // any active owned Scout/Science vessel has at least one currently supported candidate in
        // the bounded canonical planning window. It does not infer reach from total catalog size,
        // foreign fleets, or hidden system value.
        var hasSupportedExplorationWork = activeFleets
            .Where(fleet => fleet.Role is FleetRole.Scout or FleetRole.Science)
            .OrderBy(fleet => fleet.Id)
            .Any(fleet => _explorationMissionPlanner
                .BuildPlan(galaxy, fleet.Id, ExplorationMissionPlanner.HardMaximumCandidates)
                .Candidates
                .Any(candidate => candidate.Reach.IsSupported));

        // Only fully surveyed systems may contribute environmental facts. Habitability is then
        // contextual to an actually available population species rather than the old universal
        // LegacyColonizationCandidate bit.
        var hasKnownColonizationOpportunity = galaxy.PlanetaryBodies.Any(body =>
            galaxy.Knowledge.IsSystemFullySurveyed(civilizationId, body.SystemId)
            && !colonizedSystemIds.Contains(body.SystemId)
            && populationSpeciesIds.Any(speciesId => _habitability.Evaluate(body, speciesId).CanFoundCurrentColony));

        var hasSpacecraftConstruction = _shipbuildingCapabilities.HasCivilizationCapability(
            galaxy,
            civilizationId,
            ShipbuildingCapabilityIds.SpacecraftConstruction);
        var hasExperimentalTransit = _shipbuildingCapabilities.HasCivilizationCapability(
            galaxy,
            civilizationId,
            ShipbuildingCapabilityIds.ExperimentalInterstellarTransit);
        var hasOrbitalShipyard = construction.CompletedProjectIds.Contains("orbital_shipyard");
        var canBuildInterstellarShips = hasSpacecraftConstruction && hasExperimentalTransit && hasOrbitalShipyard;

        var militaryFleetCount = activeFleets.Count(fleet => fleet.Role == FleetRole.Military);

        // Combat owns the exact strength semantics for the civilization's own vessels. Using
        // combat-effective armed strength here means damaged ships retain reduced value, while
        // retreating/disengaged ships do not count as immediately available combat power. This
        // remains exact self-knowledge only; no foreign force state crosses this boundary.
        var combatReadiness = CombatReadinessCalculator.Build(galaxy, civilizationId);

        var colonyCount = galaxy.Colonies.Count(colony => colony.CivilizationId == civilizationId);
        var desiredMilitaryFleets = canBuildInterstellarShips ? Math.Max(1, (int)Math.Ceiling(colonyCount / 2.0)) : 0;
        var fleetCapacityShortfall = militaryFleetCount < desiredMilitaryFleets;

        var availableResearch = technology.ActiveResearchId is null
            && TechnologyRegistry.GetAvailable(technology, construction).Count > 0;

        // Science throughput is the closest current runtime proxy for usable research capacity.
        // The Adaptive Research runtime can replace this input source later without changing
        // CivilizationOwnState or the strategic planner.
        var researchCapacity = Math.Max(0.0, economy.LastSciencePerSecond);

        return new CivilizationOwnState(
            MilitaryStrength: Math.Max(1.0, combatReadiness.CombatEffectiveArmedStrength),
            SupplyCoverageRatio: logistics.EffectiveCoverageRatio,
            IndustryReserve: Math.Max(0.0, economy.Industry),
            ResearchCapacity: researchCapacity,
            HasAvailableResearch: availableResearch,
            HasUnexploredReachableSystems: hasSupportedExplorationWork,
            HasKnownColonizationOpportunity: hasKnownColonizationOpportunity && hasExperimentalTransit,
            CanBuildInterstellarShips: canBuildInterstellarShips,
            HasFleetCapacityShortfall: fleetCapacityShortfall);
    }
}
