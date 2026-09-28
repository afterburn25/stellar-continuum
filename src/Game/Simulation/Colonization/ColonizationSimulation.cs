using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Colonization;

public sealed class ColonizationSimulation
{
    /// <summary>Strategic capital for landing infrastructure, habitats, and local administration.</summary>
    public const double ColonyExpeditionCreditCost = 120.0;
    public const double ColonyEstablishmentDays = 30;
    public const double OutpostEstablishmentDays = 20;
    public static double EstablishmentDays(FleetState fleet) =>
        ResourceOutpostOpportunityPlanner.IsOutpostFleet(fleet) ? OutpostEstablishmentDays : ColonyEstablishmentDays;
    public const double ResourceOutpostExpeditionCreditCost = 90.0;
    private readonly IInterstellarOperationalReachView _operationalReach;
    private readonly SpeciesPlanetaryHabitabilityEvaluator _habitability = new();
    private readonly ColonizationOpportunityPlanner _opportunityPlanner;
    private readonly ResourceOutpostOpportunityPlanner _outpostPlanner;
    private readonly ColonySettlementBodyResolver _settlementBodies = new();

    public ColonizationSimulation(IInterstellarOperationalReachView? operationalReach = null)
    {
        _operationalReach = operationalReach ?? new LaneInterstellarOperationalReachView();
        _opportunityPlanner = new ColonizationOpportunityPlanner(_operationalReach);
        _outpostPlanner = new ResourceOutpostOpportunityPlanner(_operationalReach);
    }

    public IReadOnlyList<ColonizationEvent> Advance(GalaxyState galaxy, double simulationDays = 1)
    {
        if (!double.IsFinite(simulationDays) || simulationDays < 0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays));
        if (simulationDays == 0) return Array.Empty<ColonizationEvent>();
        var events = new List<ColonizationEvent>();

        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.IsActive && fleet.Role == FleetRole.Colony))
        {
            if (fleet.HoldRequested) continue;
            if (CivilizationOperatingCapacity.GetFundingFraction(galaxy, fleet.CivilizationId) <= 0.0000001)
                continue;
            if (fleet.PreventAutomaticSettlement) continue;
            var civilization = galaxy.Civilizations.First(c => c.Id == fleet.CivilizationId);

            if (ResourceOutpostOpportunityPlanner.IsOutpostFleet(fleet))
            {
                if (fleet.TransitPhase == FleetTransitPhase.None && fleet.DestinationSystemId is null && fleet.CurrentSystemId is int outpostSystemId &&
                    fleet.DestinationPlanetaryBodyId is int outpostBodyId)
                {
                    var assessment = _outpostPlanner.AssessOrder(galaxy, fleet.Id, outpostSystemId, outpostBodyId);
                    if (assessment.Accepted)
                    {
                        if (!AdvanceEstablishment(galaxy, fleet, outpostBodyId, simulationDays)) continue;
                        var body = galaxy.PlanetaryBodies.First(candidate => candidate.Id == outpostBodyId);
                        var personnel = fleet.EmbarkedPopulationMillions;
                        var speciesId = RequireEmbarkedPopulationSpecies(fleet);
                        var colony = new ColonyState
                        {
                            Id = galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(c => c.Id) + 1,
                            CivilizationId = fleet.CivilizationId,
                            SystemId = outpostSystemId,
                            PlanetaryBodyId = body.Id,
                            Name = $"{civilization.Name} Resource Outpost {galaxy.Colonies.Count(c => c.CivilizationId == civilization.Id && c.Kind == SettlementKind.ResourceOutpost) + 1}",
                            Kind = SettlementKind.ResourceOutpost,
                            PopulationSpeciesId = speciesId,
                            PopulationMillions = personnel,
                            StoredFoodPopulationDaysMillions = personnel * ColonySustenanceReserves.MaximumFoodReserveDays,
                            StoredWaterPopulationDaysMillions = personnel * ColonySustenanceReserves.MaximumWaterReserveDays,
                            RemainingExtractableMaterials = ResourceOutpostOperations.InitialDepositReserve(body),
                            Infrastructure = 0.15,
                            Stability = 0.85,
                        };
                        galaxy.Colonies.Add(colony);
                        ConsumeSettlementVessel(fleet);
                        events.Add(new ColonizationEvent(fleet.CivilizationId, fleet.Id, outpostSystemId, colony.Id,
                            $"{civilization.Name} established a sealed staffed resource outpost on {body.Name} with {personnel:0.0} million specialist personnel."));
                    }
                }
                continue;
            }

            // Found first when a populated colony ship has already arrived. Body-aware v8
            // missions keep the exact target; legacy/in-memory missions without one use the
            // shared species-relative body-less resolver also consumed by read/status surfaces.
            if (fleet.TransitPhase == FleetTransitPhase.None && fleet.DestinationSystemId is null &&
                fleet.CurrentSystemId is int currentSystemId &&
                fleet.EmbarkedPopulationMillions > 0.0)
            {
                var speciesId = RequireEmbarkedPopulationSpecies(fleet);
                var targetBody = ResolveArrivalBody(galaxy, fleet, currentSystemId, speciesId);
                if (targetBody is not null &&
                    IsColonizable(galaxy, fleet.CivilizationId, targetBody, speciesId))
                {
                    if (!AdvanceEstablishment(galaxy, fleet, targetBody.Id, simulationDays)) continue;
                    var colonists = fleet.EmbarkedPopulationMillions;
                    var assessment = _habitability.Evaluate(targetBody, speciesId);
                    var colony = new ColonyState
                    {
                        Id = galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(c => c.Id) + 1,
                        CivilizationId = fleet.CivilizationId,
                        SystemId = currentSystemId,
                        PlanetaryBodyId = targetBody.Id,
                        Name = $"{civilization.Name} Colony {galaxy.Colonies.Count(c => c.CivilizationId == civilization.Id) + 1}",
                        PopulationSpeciesId = speciesId,
                        PopulationMillions = colonists,
                        StoredFoodPopulationDaysMillions = colonists * ColonySustenanceReserves.MaximumFoodReserveDays,
                        StoredWaterPopulationDaysMillions = colonists * ColonySustenanceReserves.MaximumWaterReserveDays,
                        Infrastructure = assessment.Viability == SpeciesColonizationViability.NaturallyViable ? 0.35 : 0.42,
                        Stability = assessment.Viability == SpeciesColonizationViability.NaturallyViable ? 0.92 : 0.88,
                    };

                    galaxy.Colonies.Add(colony);
                    ConsumeSettlementVessel(fleet);

                    var speciesName = SpeciesCatalog.Get(speciesId).DisplayName;
                    var mode = assessment.Viability == SpeciesColonizationViability.NaturallyViable
                        ? "natural environmental viability"
                        : "prototype habitat support";
                    events.Add(new ColonizationEvent(
                        fleet.CivilizationId,
                        fleet.Id,
                        currentSystemId,
                        colony.Id,
                        $"{civilization.Name} established {colony.Name} on {targetBody.Name} with {colonists:0.0} million {speciesName} colonists using {mode}."));
                    continue;
                }
            }

            if (fleet.DestinationSystemId is null &&
                !civilization.IsPlayer &&
                fleet.EmbarkedPopulationMillions > 0.0)
            {
                AssignAiColonyDestination(galaxy, fleet, civilization);
            }
        }

        return events;
    }

    private static bool AdvanceEstablishment(GalaxyState galaxy, FleetState fleet, int bodyId, double days)
    {
        if (fleet.SettlementBodyId != bodyId)
        {
            fleet.SettlementBodyId = bodyId;
            fleet.SettlementDaysCompleted = 0;
            return false; // Arrival cannot also count as a full step of construction.
        }
        var capacity = CivilizationOperatingCapacity.GetFundingFraction(galaxy, fleet.CivilizationId);
        fleet.SettlementDaysCompleted = Math.Min(EstablishmentDays(fleet), fleet.SettlementDaysCompleted + days * capacity);
        return fleet.SettlementDaysCompleted + 1e-9 >= EstablishmentDays(fleet);
    }

    public ColonyOrderResult IssueTransitOrder(GalaxyState galaxy, int civilizationId, int fleetId, int destinationSystemId)
    {
        var fleet = galaxy.Fleets.FirstOrDefault(f => f.Id == fleetId && f.IsActive && f.CivilizationId == civilizationId && f.Role == FleetRole.Colony);
        if (fleet is null) return new(false, "Select an active colony or outpost ship you control.");
        var reach = _operationalReach.Assess(galaxy, civilizationId, fleet, destinationSystemId, InterstellarMissionKind.Colony);
        if (!reach.IsSupported) return new(false, reach.Reason);
        FleetRouteOrders.Assign(galaxy, fleet, destinationSystemId, reach);
        AbandonMissionForTransit(fleet);
        return new(true, $"{fleet.Name}: course set. Colonists remain aboard until you right-click a surveyed world to authorize settlement. {reach.Reason}");
    }

    public ColonizationOpportunityPlan GetOpportunityPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = ColonizationOpportunityPlanner.DefaultMaximumCandidates) =>
        _opportunityPlanner.BuildPlan(galaxy, fleetId, maximumCandidates);

    /// <summary>Clears only mutable settlement target/progress; paid expedition accounting is retained.</summary>
    public static void AbandonMissionForTransit(FleetState fleet)
    {
        ArgumentNullException.ThrowIfNull(fleet);
        fleet.DestinationPlanetaryBodyId = null;
        fleet.SettlementBodyId = null;
        fleet.SettlementDaysCompleted = 0;
        fleet.PreventAutomaticSettlement = true;
    }

    public ResourceOutpostOpportunityPlan GetResourceOutpostOpportunityPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = ResourceOutpostOpportunityPlanner.DefaultMaximumCandidates) =>
        _outpostPlanner.BuildPlan(galaxy, fleetId, maximumCandidates);

    public ColonyOrderResult IssueResourceOutpostFleetOrder(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        var assessment = _outpostPlanner.AssessOrder(galaxy, fleetId, destinationSystemId, planetaryBodyId);
        if (!assessment.Accepted)
            return new ColonyOrderResult(false, assessment.Message);
        var fleet = galaxy.Fleets.First(candidate => candidate.Id == fleetId && ResourceOutpostOpportunityPlanner.IsOutpostFleet(candidate));
        var isNewMission = fleet.PreventAutomaticSettlement || (fleet.DestinationSystemId is null && fleet.DestinationPlanetaryBodyId is null && fleet.SettlementBodyId is null);
        var economy = galaxy.Economies.First(state => state.CivilizationId == fleet.CivilizationId);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, fleet.CivilizationId);
        if (isNewMission && economy.Credits + 0.0001 < ResourceOutpostExpeditionCreditCost)
            return new ColonyOrderResult(false, $"{currency.Format(ResourceOutpostExpeditionCreditCost)} is required to fund the resource-outpost expedition.");
        if (isNewMission)
            economy.Credits -= ResourceOutpostExpeditionCreditCost;
        FleetRouteOrders.Assign(galaxy, fleet, destinationSystemId, assessment.Candidate!.Reach);
        if (fleet.DestinationPlanetaryBodyId != planetaryBodyId)
        {
            fleet.SettlementBodyId = null;
            fleet.SettlementDaysCompleted = 0;
        }
        fleet.DestinationPlanetaryBodyId = planetaryBodyId;
        fleet.PreventAutomaticSettlement = false;
        return new ColonyOrderResult(true, assessment.Message + (isNewMission
            ? $" Expedition funded for {currency.Format(ResourceOutpostExpeditionCreditCost)}."
            : " Destination updated; the original expedition authorization remains in effect."));
    }

    public ColonizationOrderAssessment AssessColonyOrder(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId) =>
        _opportunityPlanner.AssessOrder(galaxy, fleetId, destinationSystemId, planetaryBodyId);

    public ColonyOrderResult IssuePlayerColonyOrder(
        GalaxyState galaxy,
        int civilizationId,
        int destinationSystemId)
    {
        var validation = ValidateSurveyedSystem(galaxy, civilizationId, destinationSystemId);
        if (validation is not null)
            return validation;

        var fleet = FindAvailableColonyFleet(galaxy, civilizationId);
        if (fleet is null)
            return new ColonyOrderResult(false, "No colony ship carrying reserved colonists is available.");

        var speciesId = RequireEmbarkedPopulationSpeciesForOrder(fleet, out var speciesError);
        if (speciesId is null)
            return new ColonyOrderResult(false, speciesError!);

        var body = _settlementBodies.ResolveBestAvailableBody(
            galaxy,
            civilizationId,
            destinationSystemId,
            speciesId);
        return body is null
            ? new ColonyOrderResult(
                false,
                "No surveyed body in that system is currently viable for the colony ship's population. Additional environmental-support capability may make other worlds usable later.")
            : IssuePlayerColonyOrder(galaxy, civilizationId, destinationSystemId, body.Id);
    }

    /// <summary>
    /// Compatibility civilization-scoped body order. When several populated colony fleets exist,
    /// only a genuinely uncommitted fleet parked at a friendly founded colony is eligible for a
    /// new implicit mission. Explicit fleet-ID orders remain the retargeting surface.
    /// </summary>
    public ColonyOrderResult IssuePlayerColonyOrder(
        GalaxyState galaxy,
        int civilizationId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        var validation = ValidateSurveyedSystem(galaxy, civilizationId, destinationSystemId);
        if (validation is not null)
            return validation;

        var fleet = FindAvailableColonyFleet(galaxy, civilizationId);
        if (fleet is null)
            return new ColonyOrderResult(false, "No colony ship carrying reserved colonists is available.");

        return IssueColonyFleetOrder(galaxy, fleet.Id, destinationSystemId, planetaryBodyId);
    }

    /// <summary>
    /// Explicit per-fleet colony command. Eligibility and the player-facing rejection reason come
    /// from the same observer-safe opportunity assessment used by read-only planning and AI
    /// eligibility filtering.
    /// </summary>
    public ColonyOrderResult IssueColonyFleetOrder(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        var assessment = _opportunityPlanner.AssessOrder(
            galaxy,
            fleetId,
            destinationSystemId,
            planetaryBodyId);
        if (!assessment.Accepted)
            return new ColonyOrderResult(false, assessment.Message);

        var fleet = galaxy.Fleets.First(f =>
            f.Id == fleetId &&
            f.IsActive &&
            f.Role == FleetRole.Colony &&
            f.EmbarkedPopulationMillions > 0.0);
        var isNewMission = fleet.PreventAutomaticSettlement || (fleet.DestinationSystemId is null && fleet.DestinationPlanetaryBodyId is null && fleet.SettlementBodyId is null);
        var economy = galaxy.Economies.First(e => e.CivilizationId == fleet.CivilizationId);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, fleet.CivilizationId);
        if (isNewMission && economy.Credits + 0.0001 < ColonyExpeditionCreditCost)
            return new ColonyOrderResult(false, $"{currency.Format(ColonyExpeditionCreditCost)} is required to fund the colony expedition.");

        if (isNewMission)
            economy.Credits -= ColonyExpeditionCreditCost;
        FleetRouteOrders.Assign(galaxy, fleet, destinationSystemId, assessment.Candidate!.Reach);
        if (fleet.DestinationPlanetaryBodyId != planetaryBodyId)
        {
            fleet.SettlementBodyId = null;
            fleet.SettlementDaysCompleted = 0;
        }
        fleet.DestinationPlanetaryBodyId = planetaryBodyId;
        fleet.PreventAutomaticSettlement = false;
        return new ColonyOrderResult(true, assessment.Message + (isNewMission
            ? $" Expedition funded for {currency.Format(ColonyExpeditionCreditCost)}."
            : " Destination updated; the original expedition authorization remains in effect."));
    }

    public MissionReachAssessment AssessOperationalReach(
        GalaxyState galaxy,
        int fleetId,
        int destinationSystemId) =>
        _opportunityPlanner.AssessOperationalReach(galaxy, fleetId, destinationSystemId);

    /// <summary>
    /// Resolves the occupied physical body. New body-aware colonies use their explicit ID;
    /// legacy/home colonies fall back to the deterministic compatibility body.
    /// </summary>
    public PlanetaryBodyState? ResolveCompatibilityColonyWorld(GalaxyState galaxy, ColonyState colony)
    {
        if (colony.PlanetaryBodyId is int bodyId)
        {
            return galaxy.PlanetaryBodies.FirstOrDefault(body =>
                body.Id == bodyId && body.SystemId == colony.SystemId);
        }

        return SelectCompatibilityCandidate(galaxy, colony.SystemId);
    }

    private MissionReachAssessment AssessOperationalReach(
        GalaxyState galaxy,
        FleetState fleet,
        int destinationSystemId) =>
        _operationalReach.Assess(
            galaxy,
            fleet.CivilizationId,
            fleet,
            destinationSystemId,
            InterstellarMissionKind.Colony);

    private bool IsColonizable(
        GalaxyState galaxy,
        int civilizationId,
        PlanetaryBodyState body,
        string speciesId) =>
        galaxy.Knowledge.IsSystemFullySurveyed(civilizationId, body.SystemId) &&
        _habitability.Evaluate(body, speciesId).CanFoundCurrentColony &&
        !galaxy.Colonies.Any(c => c.SystemId == body.SystemId);

    private void AssignAiColonyDestination(
        GalaxyState galaxy,
        FleetState fleet,
        CivilizationState civilization)
    {
        var systemsById = galaxy.Systems.ToDictionary(system => system.Id);
        var bodiesById = galaxy.PlanetaryBodies.ToDictionary(body => body.Id);
        var plan = _opportunityPlanner.BuildPlan(
            galaxy,
            fleet.Id,
            ColonizationOpportunityPlanner.HardMaximumCandidates);

        // The opportunity planner owns eligibility, including friendly mission reservations.
        // Civilization AI's existing strategic weighting remains here so species/logistics facts
        // are not turned into a second AI personality or destination policy.
        var candidate = plan.Candidates
            .Where(option => option.CanOrder)
            .Where(option => systemsById.ContainsKey(option.SystemId) && bodiesById.ContainsKey(option.PlanetaryBodyId))
            .Select(option =>
            {
                var body = bodiesById[option.PlanetaryBodyId];
                var system = systemsById[option.SystemId];
                var physicalDistance = InterstellarDistance.FromFleet(galaxy, fleet, system);
                var distance = system.GalacticDepthLightYears is null
                    ? Vector2.DistanceSquared(fleet.Position, system.Position)
                    : physicalDistance * physicalDistance;
                var value =
                    (option.ColonizationViability == SpeciesColonizationViability.NaturallyViable ? 14000.0 : 3500.0) +
                    option.NaturalHabitability * 9000.0 +
                    (body.HasRareResource ? 14000.0 : 0.0) +
                    civilization.Traits.Territoriality * 6000.0 +
                    civilization.Traits.Greed * (body.HasRareResource ? 9000.0 : 1500.0);
                return new
                {
                    Body = body,
                    System = system,
                    Reach = option.Reach,
                    Distance = distance,
                    Value = value,
                };
            })
            .OrderByDescending(option => option.Value - option.Distance)
            .ThenBy(option => option.System.Id)
            .ThenBy(option => option.Body.Id)
            .FirstOrDefault();

        if (candidate is not null)
        {
            // A replacement route cannot retain on-site work from an abandoned world.
            // In particular, persisting that state would associate a stationary work site
            // with a travelling fleet and make the next campaign load invalid.
            fleet.SettlementBodyId = null;
            fleet.SettlementDaysCompleted = 0;
            FleetRouteOrders.Assign(galaxy, fleet, candidate.System.Id, candidate.Reach);
            fleet.DestinationPlanetaryBodyId = candidate.Body.Id;
        }
    }

    private PlanetaryBodyState? ResolveArrivalBody(
        GalaxyState galaxy,
        FleetState fleet,
        int currentSystemId,
        string speciesId)
    {
        if (fleet.DestinationPlanetaryBodyId is int bodyId)
        {
            return galaxy.PlanetaryBodies.FirstOrDefault(body =>
                body.Id == bodyId && body.SystemId == currentSystemId);
        }

        return _settlementBodies.ResolveBestAvailableBody(
            galaxy,
            fleet.CivilizationId,
            currentSystemId,
            speciesId);
    }

    private static FleetState? FindAvailableColonyFleet(GalaxyState galaxy, int civilizationId)
    {
        var friendlyColonySystems = galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId)
            .Select(colony => colony.SystemId)
            .ToHashSet();

        return galaxy.Fleets
            .Where(fleet =>
                fleet.IsActive &&
                fleet.CivilizationId == civilizationId &&
                fleet.Role == FleetRole.Colony &&
                fleet.DesignId != ShipDesignRegistry.ResourceOutpostShipId &&
                fleet.EmbarkedPopulationMillions > 0.0 &&
                fleet.DestinationSystemId is null &&
                fleet.DestinationPlanetaryBodyId is null &&
                fleet.CurrentSystemId is int currentSystemId &&
                friendlyColonySystems.Contains(currentSystemId))
            .OrderBy(fleet => fleet.Id)
            .FirstOrDefault();
    }

    private static void ConsumeSettlementVessel(FleetState fleet)
    {
        fleet.EmbarkedPopulationMillions = 0.0;
        fleet.EmbarkedPopulationSpeciesId = null;
        fleet.IsActive = false;
        FleetRouteOrders.Clear(fleet);
        fleet.DestinationPlanetaryBodyId = null;
        fleet.SettlementBodyId = null;
        fleet.SettlementDaysCompleted = 0;
    }

    private static string RequireEmbarkedPopulationSpecies(FleetState fleet)
    {
        if (fleet.EmbarkedPopulationMillions <= 0.0)
            throw new InvalidOperationException($"Fleet {fleet.Id} has no embarked population to identify.");

        var speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out _))
        {
            throw new InvalidOperationException(
                $"Fleet {fleet.Id} carries population without a valid species identity '{speciesId}'.");
        }

        return speciesId;
    }

    private static string? RequireEmbarkedPopulationSpeciesForOrder(
        FleetState fleet,
        out string? error)
    {
        var speciesId = fleet.EmbarkedPopulationSpeciesId;
        if (string.IsNullOrWhiteSpace(speciesId) || !SpeciesCatalog.TryGet(speciesId, out _))
        {
            error = "The colony ship's passenger species identity is invalid.";
            return null;
        }

        error = null;
        return speciesId;
    }

    private static PlanetaryBodyState? SelectCompatibilityCandidate(GalaxyState galaxy, int systemId) =>
        galaxy.PlanetaryBodies
            .Where(body => body.SystemId == systemId)
            .OrderBy(body => body.Id)
            .FirstOrDefault(body => body.LegacyColonizationCandidate && body.Environment.HasSolidSurface);

    private static ColonyOrderResult? ValidateSurveyedSystem(
        GalaxyState galaxy,
        int civilizationId,
        int destinationSystemId)
    {
        var system = galaxy.Systems.FirstOrDefault(candidate => candidate.Id == destinationSystemId);
        if (system is null)
            return new ColonyOrderResult(false, "Unknown destination.");
        if (!galaxy.Knowledge.IsSystemKnown(civilizationId, destinationSystemId))
            return new ColonyOrderResult(false, "That astronomical target has not been detected yet.");
        if (!galaxy.Knowledge.IsSystemFullySurveyed(civilizationId, destinationSystemId))
            return new ColonyOrderResult(false, "A completed science survey is required before a colony mission can be prepared.");
        if (galaxy.Colonies.Any(c => c.SystemId == destinationSystemId))
            return new ColonyOrderResult(false, "That system already contains a founded colony in the current single-colony early-release model.");

        return null;
    }
}

public sealed record ColonyOrderResult(bool Accepted, string Message);
public sealed record ColonizationEvent(int CivilizationId, int FleetId, int SystemId, int ColonyId, string Message);
