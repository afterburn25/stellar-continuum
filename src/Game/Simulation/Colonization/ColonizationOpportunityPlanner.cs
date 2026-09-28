using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Colonization;

/// <summary>
/// Builds a bounded observer-safe set of exact planetary settlement opportunities for one
/// physical colony fleet. The planner composes authoritative Species suitability, Exploration
/// survey knowledge, represented colony occupancy, friendly mission reservations, and the
/// injected Logistics-owned operational reach contract. It does not own biology,
/// support/endurance formulas, or strategic AI value.
/// </summary>
public sealed class ColonizationOpportunityPlanner
{
    public const int DefaultMaximumCandidates = 32;
    public const int HardMaximumCandidates = 64;

    private readonly IInterstellarOperationalReachView _operationalReach;
    private readonly SpeciesPlanetaryReadModel _speciesReadModel = new();

    public ColonizationOpportunityPlanner(IInterstellarOperationalReachView? operationalReach = null)
    {
        _operationalReach = operationalReach ?? new LaneInterstellarOperationalReachView();
    }

    public ColonizationOpportunityPlan BuildPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = DefaultMaximumCandidates)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        maximumCandidates = Math.Clamp(maximumCandidates, 1, HardMaximumCandidates);

        var fleet = FindPopulatedColonyFleet(galaxy, fleetId);
        if (fleet is null)
        {
            return ColonizationOpportunityPlan.Unavailable(
                fleetId,
                "No active colony ship carrying reserved colonists with that fleet ID is available.");
        }

        if (!TryResolvePassengerSpecies(fleet, out var speciesId, out var speciesName, out var error))
            return ColonizationOpportunityPlan.Unavailable(fleet.Id, error!);

        var systemsById = galaxy.Systems.ToDictionary(system => system.Id);
        var bodiesById = galaxy.PlanetaryBodies.ToDictionary(body => body.Id);
        var reachBySystem = new Dictionary<int, MissionReachAssessment>();
        var friendlyReservations = FriendlyColonyMissionReservations.BuildBySystem(galaxy, fleet);

        // SpeciesPlanetaryReadModel is the observer-safe boundary: it returns suitability only
        // for systems with a completed science survey for this civilization.
        var candidates = _speciesReadModel
            .BuildForSpecies(galaxy, fleet.CivilizationId, speciesId!)
            .Where(view => bodiesById.ContainsKey(view.PlanetaryBodyId) && systemsById.ContainsKey(view.SystemId))
            .Select(view =>
            {
                var body = bodiesById[view.PlanetaryBodyId];
                var system = systemsById[view.SystemId];
                if (!reachBySystem.TryGetValue(system.Id, out var reach))
                {
                    reach = _operationalReach.Assess(
                        galaxy,
                        fleet.CivilizationId,
                        fleet,
                        system.Id,
                        InterstellarMissionKind.Colony);
                    reachBySystem[system.Id] = reach;
                }

                return BuildCandidate(
                    galaxy,
                    fleet,
                    system,
                    body,
                    view,
                    reach,
                    speciesName!,
                    friendlyReservations);
            })
            // This is opportunity ordering, not civilization strategy: orderable choices first,
            // then stronger biological fit, then current kinematic proximity and stable IDs.
            .OrderBy(candidate => candidate.CanOrder ? 0 : 1)
            .ThenByDescending(candidate => ViabilityRank(candidate.ColonizationViability))
            .ThenByDescending(candidate => candidate.NaturalHabitability)
            .ThenByDescending(candidate => candidate.UnprotectedOperationalCapacity)
            .ThenBy(candidate => candidate.DistanceFromFleet)
            .ThenBy(candidate => candidate.SystemId)
            .ThenBy(candidate => candidate.PlanetaryBodyId)
            .Take(maximumCandidates)
            .ToArray();

        var orderable = candidates.Count(candidate => candidate.CanOrder);
        var status = candidates.Length == 0
            ? "No fully surveyed planetary bodies are currently available for settlement evaluation."
            : orderable == 0
                ? $"{candidates.Length} fully surveyed settlement candidate{(candidates.Length == 1 ? string.Empty : "s")} evaluated; none are currently orderable."
                : $"{orderable} currently orderable settlement opportunit{(orderable == 1 ? "y" : "ies")} in a {candidates.Length}-body planning window.";

        return new ColonizationOpportunityPlan(
            fleet.Id,
            fleet.Name,
            fleet.CivilizationId,
            speciesId!,
            speciesName!,
            fleet.EmbarkedPopulationMillions,
            true,
            status,
            candidates);
    }

    public ColonizationOrderAssessment AssessOrder(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = FindPopulatedColonyFleet(galaxy, fleetId);
        if (fleet is null)
        {
            return ColonizationOrderAssessment.Reject(
                "No active colony ship carrying reserved colonists with that fleet ID is available.");
        }

        if (!TryResolvePassengerSpecies(fleet, out var speciesId, out var speciesName, out var speciesError))
            return ColonizationOrderAssessment.Reject(speciesError!);

        var system = galaxy.Systems.FirstOrDefault(candidate => candidate.Id == destinationSystemId);
        if (system is null)
            return ColonizationOrderAssessment.Reject("Unknown destination.");
        if (!galaxy.Knowledge.IsSystemKnown(fleet.CivilizationId, destinationSystemId))
            return ColonizationOrderAssessment.Reject("That astronomical target has not been detected yet.");
        if (galaxy.Knowledge.GetSystemSurveyLevel(fleet.CivilizationId, destinationSystemId) != SystemSurveyLevel.FullySurveyed)
        {
            return ColonizationOrderAssessment.Reject(
                "A completed science survey is required before a colony mission can be prepared.");
        }

        var body = galaxy.PlanetaryBodies.FirstOrDefault(candidate =>
            candidate.Id == planetaryBodyId && candidate.SystemId == destinationSystemId);
        if (body is null)
        {
            return ColonizationOrderAssessment.Reject(
                "That planetary body is not part of the surveyed destination system.");
        }

        var suitability = _speciesReadModel
            .BuildForSpecies(galaxy, fleet.CivilizationId, speciesId!)
            .FirstOrDefault(candidate => candidate.PlanetaryBodyId == body.Id);
        if (suitability is null)
        {
            return ColonizationOrderAssessment.Reject(
                "Detailed planetary suitability is not legitimately known for that body.");
        }

        var reach = _operationalReach.Assess(
            galaxy,
            fleet.CivilizationId,
            fleet,
            system.Id,
            InterstellarMissionKind.Colony);
        var friendlyReservations = FriendlyColonyMissionReservations.BuildBySystem(galaxy, fleet);
        var candidate = BuildCandidate(
            galaxy,
            fleet,
            system,
            body,
            suitability,
            reach,
            speciesName!,
            friendlyReservations);
        if (!candidate.CanOrder)
            return ColonizationOrderAssessment.Reject(candidate.Reason, candidate);

        return ColonizationOrderAssessment.Approve(
            $"{fleet.Name}: colony mission approved for {body.Name} in {system.Name} with {fleet.EmbarkedPopulationMillions:0.0} million {speciesName} colonists aboard. {candidate.Reason}",
            candidate);
    }

    public MissionReachAssessment AssessOperationalReach(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId)
    {
        var fleet = FindPopulatedColonyFleet(galaxy, fleetId);
        return fleet is null
            ? MissionReachAssessment.Unsupported("No populated colony ship is available.")
            : _operationalReach.Assess(
                galaxy,
                fleet.CivilizationId,
                fleet,
                destinationSystemId,
                InterstellarMissionKind.Colony);
    }

    private static ColonizationOpportunityCandidate BuildCandidate(
        GalaxyState galaxy,
        FleetState fleet,
        StarSystemState system,
        PlanetaryBodyState body,
        KnownSpeciesPlanetarySuitability suitability,
        MissionReachAssessment reach,
        string speciesName,
        IReadOnlyDictionary<int, int> friendlyReservations)
    {
        var occupied = galaxy.Colonies.Any(colony => colony.SystemId == system.Id);
        var reservedByFriendlyMission = friendlyReservations.TryGetValue(system.Id, out var reservingFleetId);
        var hasSurface = body.Environment.HasSolidSurface;
        var native = body.HasPreWarpCivilization;
        var biologicallyAvailable =
            hasSurface &&
            !native &&
            suitability.ColonizationViability != SpeciesColonizationViability.Unsuitable;
        var expeditionAffordable = fleet.DestinationSystemId is not null ||
            galaxy.Economies.First(economy => economy.CivilizationId == fleet.CivilizationId).Credits + 0.0001 >=
            ColonizationSimulation.ColonyExpeditionCreditCost;
        var canOrder =
            biologicallyAvailable &&
            !occupied &&
            !reservedByFriendlyMission &&
            reach.IsSupported &&
            expeditionAffordable;
        var distance = InterstellarDistance.FromFleet(galaxy, fleet, system);

        string reason;
        if (!hasSurface)
        {
            reason = $"{body.Name} has no solid settlement surface in the current colony model.";
        }
        else if (native)
        {
            reason = $"A native pre-warp civilization already inhabits {body.Name}.";
        }
        else if (suitability.ColonizationViability == SpeciesColonizationViability.Unsuitable)
        {
            reason =
                $"{body.Name} is not currently viable for {speciesName}: natural habitability {suitability.NaturalHabitability:P0}, unprotected operational capacity {suitability.UnprotectedOperationalCapacity:P0}. Required habitat-support capabilities are not yet connected to colony construction/logistics.";
        }
        else if (occupied)
        {
            reason = "That system already contains a founded colony in the current single-colony early-release model.";
        }
        else if (reservedByFriendlyMission)
        {
            reason = $"Another friendly colony ship (fleet {reservingFleetId}) is already committed to that system.";
        }
        else if (!reach.IsSupported)
        {
            reason = reach.Reason;
        }
        else if (!expeditionAffordable)
        {
            reason = $"{Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, fleet.CivilizationId).Format(ColonizationSimulation.ColonyExpeditionCreditCost)} is required to fund the colony expedition.";
        }
        else
        {
            var mode = suitability.ColonizationViability == SpeciesColonizationViability.NaturallyViable
                ? "naturally viable"
                : "currently available through the prototype habitat-supported fallback";
            reason = $"{body.Name} is {mode} for {speciesName}. {reach.Reason}";
        }

        return new ColonizationOpportunityCandidate(
            system.Id,
            system.Name,
            body.Id,
            body.Name,
            body.Kind,
            fleet.Id,
            suitability.SpeciesId,
            suitability.ColonizationViability,
            suitability.NaturalHabitability,
            suitability.UnprotectedOperationalCapacity,
            suitability.LimitingFactor,
            suitability.RequiresGravityMitigation,
            suitability.RequiresThermalControl,
            suitability.RequiresPressureControl,
            suitability.RequiresSealedHabitat,
            suitability.RequiresArtificialBiosphere,
            suitability.RequiresRadiationShielding,
            body.Environment.HasSolidSurface,
            body.HasPreWarpCivilization,
            body.HasRareResource,
            occupied,
            distance,
            reach,
            canOrder,
            reason)
        {
            SystemReservedByFriendlyColonyMission = reservedByFriendlyMission,
            ReservedByFleetId = reservedByFriendlyMission ? reservingFleetId : null,
        };
    }

    private static FleetState? FindPopulatedColonyFleet(GalaxyState galaxy, int fleetId) =>
        galaxy.Fleets.FirstOrDefault(fleet =>
            fleet.Id == fleetId &&
            fleet.IsActive &&
            fleet.Role == FleetRole.Colony &&
            fleet.DesignId != ShipDesignRegistry.ResourceOutpostShipId &&
            fleet.EmbarkedPopulationMillions > 0.0);

    private static bool TryResolvePassengerSpecies(
        FleetState fleet,
        out string? speciesId,
        out string? speciesName,
        out string? error)
    {
        speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out var species) || species is null)
        {
            speciesName = null;
            error = "The colony ship's passenger species identity is invalid.";
            return false;
        }

        speciesName = species.DisplayName;
        error = null;
        return true;
    }

    private static int ViabilityRank(SpeciesColonizationViability viability) => viability switch
    {
        SpeciesColonizationViability.NaturallyViable => 2,
        SpeciesColonizationViability.HabitatSupportedFallback => 1,
        _ => 0,
    };
}

public sealed record ColonizationOpportunityPlan(
    int FleetId,
    string FleetName,
    int CivilizationId,
    string PassengerSpeciesId,
    string PassengerSpeciesName,
    double EmbarkedPopulationMillions,
    bool CanReceiveOrders,
    string Status,
    IReadOnlyList<ColonizationOpportunityCandidate> Candidates)
{
    public static ColonizationOpportunityPlan Unavailable(int fleetId, string status) =>
        new(
            fleetId,
            string.Empty,
            -1,
            string.Empty,
            string.Empty,
            0.0,
            false,
            status,
            Array.Empty<ColonizationOpportunityCandidate>());
}

public sealed record ColonizationOpportunityCandidate(
    int SystemId,
    string SystemName,
    int PlanetaryBodyId,
    string PlanetaryBodyName,
    PlanetaryBodyKind PlanetaryBodyKind,
    int FleetId,
    string PassengerSpeciesId,
    SpeciesColonizationViability ColonizationViability,
    double NaturalHabitability,
    double UnprotectedOperationalCapacity,
    EnvironmentalLimitingFactor LimitingFactor,
    bool RequiresGravityMitigation,
    bool RequiresThermalControl,
    bool RequiresPressureControl,
    bool RequiresSealedHabitat,
    bool RequiresArtificialBiosphere,
    bool RequiresRadiationShielding,
    bool HasSolidSurface,
    bool HasNativePreWarpCivilization,
    bool HasRareResource,
    bool SystemOccupied,
    double DistanceFromFleet,
    MissionReachAssessment Reach,
    bool CanOrder,
    string Reason)
{
    public bool SystemReservedByFriendlyColonyMission { get; init; }
    public int? ReservedByFleetId { get; init; }
}

public sealed record ColonizationOrderAssessment(
    bool Accepted,
    string Message,
    ColonizationOpportunityCandidate? Candidate)
{
    public static ColonizationOrderAssessment Approve(
        string message,
        ColonizationOpportunityCandidate candidate) => new(true, message, candidate);

    public static ColonizationOrderAssessment Reject(
        string message,
        ColonizationOpportunityCandidate? candidate = null) => new(false, message, candidate);
}
