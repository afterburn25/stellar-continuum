using System;
using System.Linq;
using System.Numerics;
using Game.Simulation.Colonization;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Economy;

namespace Game.Simulation.Exploration;

public enum ExplorationMissionPhase
{
    AwaitingOrder,
    Traveling,
    ReconnaissanceReady,
    ScienceSurveying,
    ColonySettlementReady,
}

/// <summary>
/// Reconstructible player/AI-facing status for one owned exploration/colony fleet. Transit ETA
/// uses only the fleet's own represented speed and common astronomical coordinates. Detailed
/// science ETA is withheld until reconnaissance legitimately reveals survey complexity.
/// </summary>
public sealed record ExplorationMissionStatus(
    ExplorationMissionPhase Phase,
    double? EstimatedTransitDaysRemaining,
    double? EstimatedSurveyDaysRemaining,
    double? EstimatedMissionDaysRemaining,
    string Summary)
{
    public static ExplorationMissionStatus Awaiting(string summary) =>
        new(ExplorationMissionPhase.AwaitingOrder, null, null, null, summary);
}

public sealed class ExplorationMissionStatusEvaluator
{
    private readonly SurveyOperationsProfiler _surveyProfiler;
    private readonly SpeciesPlanetaryHabitabilityEvaluator _habitability = new();
    private readonly ColonySettlementBodyResolver _settlementBodies = new();

    public ExplorationMissionStatusEvaluator(SurveyOperationsProfiler? surveyProfiler = null)
    {
        _surveyProfiler = surveyProfiler ?? new SurveyOperationsProfiler();
    }

    public ExplorationMissionStatus Build(GalaxyState galaxy, FleetState fleet)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleet);

        if (!fleet.IsActive)
            return ExplorationMissionStatus.Awaiting($"{fleet.Name} is not an active mission fleet.");

        var operatingCapacity = CivilizationOperatingCapacity.GetFundingFraction(galaxy, fleet.CivilizationId);
        if (operatingCapacity <= 0.0000001)
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} is suspended because fleet operations are unfunded. Restore the operating budget to resume its existing mission.");

        if (fleet.HoldRequested)
        {
            if (fleet.CurrentSystemId is int heldSystemId)
            {
                var heldSystem = galaxy.Systems.FirstOrDefault(system => system.Id == heldSystemId)?.Name ?? "the current system";
                return ExplorationMissionStatus.Awaiting($"{fleet.Name} is held at {heldSystem}; resume to continue its existing mission.");
            }

            var nextStopId = fleet.PlannedRouteSystemIds.Count > 0
                ? fleet.PlannedRouteSystemIds[0]
                : fleet.DestinationSystemId;
            var nextStop = nextStopId is int id
                ? galaxy.Systems.FirstOrDefault(system => system.Id == id)?.Name ?? "the next system"
                : "the next system";
            return new ExplorationMissionStatus(ExplorationMissionPhase.Traveling, null, null, null,
                $"{fleet.Name} is holding after reaching {nextStop}; resume to continue its existing mission.");
        }

        if (fleet.DestinationSystemId is int destinationSystemId)
            return BuildTravelStatus(galaxy, fleet, destinationSystemId, operatingCapacity);

        if (fleet.CurrentSystemId is not int currentSystemId)
            return ExplorationMissionStatus.Awaiting($"{fleet.Name} has no active destination and is not currently at a star system.");

        return fleet.Role switch
        {
            FleetRole.Scout => BuildLocalScoutStatus(galaxy, fleet, currentSystemId, operatingCapacity),
            FleetRole.Science => BuildLocalScienceStatus(galaxy, fleet, currentSystemId, operatingCapacity),
            FleetRole.Colony => BuildLocalColonyStatus(galaxy, fleet, currentSystemId, operatingCapacity),
            _ => ExplorationMissionStatus.Awaiting($"{fleet.Name} has no exploration mission order."),
        };
    }

    private ExplorationMissionStatus BuildTravelStatus(
        GalaxyState galaxy,
        FleetState fleet,
        int destinationSystemId,
        double operatingCapacity)
    {
        var target = galaxy.Systems.FirstOrDefault(system => system.Id == destinationSystemId);
        if (target is null)
            return ExplorationMissionStatus.Awaiting($"{fleet.Name} references an unknown destination system.");

        var distance = FleetRouteMetrics.Measure(galaxy, fleet).DistanceLightYears;
        var localDays = FleetLocalTransit.RemainingChartDistance(galaxy, fleet) / FleetLocalTransit.Rate(fleet);
        double? transitDays = fleet.StrategicSpeed > 0.0 && double.IsFinite(fleet.StrategicSpeed)
            ? Math.Max(0.0, (distance / fleet.StrategicSpeed + localDays) / operatingCapacity)
            : null;

        double? surveyDays = null;
        if (fleet.Role == FleetRole.Science)
        {
            var level = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, target.Id);
            if (level >= SystemSurveyLevel.PartiallySurveyed)
            {
                var progress = galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, target.Id);
                var profile = _surveyProfiler.Build(galaxy, target.Id);
                surveyDays = Math.Max(0.0, profile.EstimatedScienceSurveyDays * (1.0 - progress) / operatingCapacity);
            }
        }

        double? missionDays = fleet.Role switch
        {
            FleetRole.Science when surveyDays is double knownSurvey && transitDays is double knownTransit => knownTransit + knownSurvey,
            FleetRole.Science => null,
            FleetRole.Colony => transitDays + (fleet.PreventAutomaticSettlement ? 0 : ColonizationSimulation.EstablishmentDays(fleet) / operatingCapacity),
            FleetRole.Scout => transitDays + (galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, target.Id) < SystemSurveyLevel.PartiallySurveyed
                ? ExplorationSimulation.ScoutReconnaissanceDays / operatingCapacity : 0),
            _ => transitDays,
        };

        var eta = transitDays is double travel
            ? $"approximately {travel:0.#} transit days remain"
            : "transit ETA is unavailable";
        var followUp = surveyDays is double survey
            ? $"; approximately {survey:0.#} known detailed-survey days remain after arrival"
            : fleet.Role == FleetRole.Science
                ? "; detailed-survey duration remains unknown until reconnaissance establishes system complexity"
                : string.Empty;

        var destinationLabel = target.Name;
        if (fleet.Role == FleetRole.Colony && fleet.DestinationPlanetaryBodyId is int bodyId)
        {
            var body = galaxy.PlanetaryBodies.FirstOrDefault(candidate =>
                candidate.Id == bodyId && candidate.SystemId == destinationSystemId);
            if (body is not null)
                destinationLabel = $"{body.Name} in {target.Name}";
        }

        return new ExplorationMissionStatus(
            ExplorationMissionPhase.Traveling,
            transitDays,
            surveyDays,
            missionDays,
            $"{fleet.Name} is traveling to {destinationLabel}; {eta}{followUp}.");
    }

    private static ExplorationMissionStatus BuildLocalScoutStatus(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId, double operatingCapacity)
    {
        var system = galaxy.Systems.First(system => system.Id == systemId);
        var level = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId);
        if (level < SystemSurveyLevel.PartiallySurveyed)
        {
            var remaining = Math.Max(0, ExplorationSimulation.ScoutReconnaissanceDays -
                (fleet.ReconnaissanceSystemId == systemId ? fleet.ReconnaissanceDaysCompleted : 0)) / operatingCapacity;
            return new ExplorationMissionStatus(ExplorationMissionPhase.ReconnaissanceReady, 0, remaining, remaining,
                $"{fleet.Name} is scouting {system.Name}; approximately {remaining:0.0} game days remain.");
        }

        return ExplorationMissionStatus.Awaiting(
            $"{fleet.Name} has completed reconnaissance-grade work in {system.Name} and is awaiting another order.");
    }

    private ExplorationMissionStatus BuildLocalScienceStatus(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId,
        double operatingCapacity)
    {
        var system = galaxy.Systems.First(system => system.Id == systemId);
        var level = galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, systemId);
        if (level == SystemSurveyLevel.FullySurveyed)
        {
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} has completed the detailed survey of {system.Name} and is awaiting another order.");
        }

        double? surveyDays = null;
        if (level >= SystemSurveyLevel.PartiallySurveyed)
        {
            var progress = galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, systemId);
            var profile = _surveyProfiler.Build(galaxy, systemId);
            surveyDays = Math.Max(0.0, profile.EstimatedScienceSurveyDays * (1.0 - progress) / operatingCapacity);
        }

        var estimate = surveyDays is double known
            ? $"approximately {known:0.#} detailed-survey days remain"
            : "survey duration is not yet known; the first science pass will establish reconnaissance-grade complexity";

        return new ExplorationMissionStatus(
            ExplorationMissionPhase.ScienceSurveying,
            0.0,
            surveyDays,
            surveyDays,
            $"{fleet.Name} is conducting a detailed survey of {system.Name}; {estimate}.");
    }

    private ExplorationMissionStatus BuildLocalColonyStatus(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId, double operatingCapacity)
    {
        var system = galaxy.Systems.First(system => system.Id == systemId);
        if (fleet.EmbarkedPopulationMillions <= 0.0)
            return ExplorationMissionStatus.Awaiting($"{fleet.Name} is not carrying colonists and has no active colony mission.");

        if (fleet.PreventAutomaticSettlement)
            return ExplorationMissionStatus.Awaiting($"{fleet.Name} is on station in {system.Name}; select a surveyed world to authorize settlement.");
        if (fleet.SettlementBodyId is int siteId)
        {
            var site = galaxy.PlanetaryBodies.FirstOrDefault(b => b.Id == siteId && b.SystemId == systemId);
            var remaining = Math.Max(0, ColonizationSimulation.EstablishmentDays(fleet) - fleet.SettlementDaysCompleted) / operatingCapacity;
            return new ExplorationMissionStatus(ExplorationMissionPhase.ColonySettlementReady, 0, null, remaining,
                $"Establishing {(site?.Name ?? "settlement")}: approximately {remaining:0.0} game days remain. Habitats and services are under construction.");
        }
        var speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out var species) || species is null)
        {
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} carries population without a valid passenger species identity.");
        }

        if (!galaxy.Knowledge.IsSystemFullySurveyed(fleet.CivilizationId, systemId))
        {
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} is carrying {fleet.EmbarkedPopulationMillions:0.#} million {species.DisplayName} colonists in {system.Name}, but a completed science survey is still required before settlement.");
        }

        if (galaxy.Colonies.Any(colony => colony.SystemId == systemId))
        {
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} is carrying colonists in {system.Name}, but that system already contains a founded colony under the current single-colony early-release model.");
        }

        var candidate = ResolveSettlementBody(galaxy, fleet, systemId, speciesId);
        if (candidate is null)
        {
            return ExplorationMissionStatus.Awaiting(
                $"{fleet.Name} is carrying {fleet.EmbarkedPopulationMillions:0.#} million {species.DisplayName} colonists in {system.Name}, but no surveyed body is currently viable for that population.");
        }

        var assessment = _habitability.Evaluate(candidate, speciesId);
        var viability = assessment.Viability == SpeciesColonizationViability.NaturallyViable
            ? "naturally viable"
            : "currently supported by the prototype habitat-compatibility fallback";

        return new ExplorationMissionStatus(
            ExplorationMissionPhase.ColonySettlementReady,
            0.0,
            null,
            ColonizationSimulation.EstablishmentDays(fleet) / operatingCapacity,
            $"{fleet.Name} has arrived at {candidate.Name} in {system.Name}; the world is {viability} for {species.DisplayName} and establishment requires {ColonizationSimulation.EstablishmentDays(fleet):0} game days.");
    }

    private PlanetaryBodyState? ResolveSettlementBody(
        GalaxyState galaxy,
        FleetState fleet,
        int systemId,
        string speciesId)
    {
        if (fleet.DestinationPlanetaryBodyId is int explicitBodyId)
        {
            var explicitBody = galaxy.PlanetaryBodies.FirstOrDefault(body =>
                body.Id == explicitBodyId && body.SystemId == systemId);
            if (explicitBody is null)
                return null;

            return _habitability.Evaluate(explicitBody, speciesId).CanFoundCurrentColony
                ? explicitBody
                : null;
        }

        return _settlementBodies.ResolveBestAvailableBody(
            galaxy,
            fleet.CivilizationId,
            systemId,
            speciesId);
    }
}
