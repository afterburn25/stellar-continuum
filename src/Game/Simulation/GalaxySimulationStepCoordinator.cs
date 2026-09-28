using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.AI;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Industry;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation;

/// <summary>
/// One authoritative strategic simulation step. Godot supplies only accepted simulation time;
/// subsystem mutation order and shared Industry allocation are resolved here in plain C#.
/// </summary>
public sealed class GalaxySimulationStepCoordinator
{
    private readonly EconomySimulation _economy;
    private readonly FreightSimulation _freight;
    private readonly ConstructionSimulation _construction;
    private readonly ShipbuildingSimulation _shipbuilding;
    private readonly ResearchSimulation _research;
    private readonly ExplorationSimulation _exploration;
    private readonly CombatSimulation _combat;
    private readonly CombatCommandRuntime? _combatCommands;
    private readonly ColonizationSimulation _colonization;
    private readonly CivilizationStrategicRuntimeCoordinator _strategicAi;
    private readonly IIndustryAllocationPolicy _industryAllocationPolicy;
    private readonly CampaignIndustryPriorityProvider? _campaignIndustryPriorityProvider;
    private readonly bool _advanceLegacyResearch;

    public GalaxySimulationStepCoordinator(
        EconomySimulation? economy = null,
        ConstructionSimulation? construction = null,
        ShipbuildingSimulation? shipbuilding = null,
        ResearchSimulation? research = null,
        ExplorationSimulation? exploration = null,
        ColonizationSimulation? colonization = null,
        IIndustryAllocationPolicy? industryAllocationPolicy = null,
        CombatSimulation? combat = null,
        CivilizationStrategicRuntimeCoordinator? strategicAi = null,
        CombatCommandRuntime? combatRuntime = null,
        FreightSimulation? freight = null,
        bool advanceLegacyResearch = true)
    {
        if (combat is not null && combatRuntime is not null)
        {
            throw new ArgumentException(
                "Supply either a standalone CombatSimulation or a matched CombatCommandRuntime, not both.");
        }

        _economy = economy ?? new EconomySimulation();
        _freight = freight ?? new FreightSimulation();
        _construction = construction ?? new ConstructionSimulation();
        _strategicAi = strategicAi ?? new CivilizationStrategicRuntimeCoordinator();
        _shipbuilding = shipbuilding ?? new ShipbuildingSimulation(
            strategicPreferenceView: _strategicAi.ShipbuildingStrategicPreferenceView);
        _research = research ?? new ResearchSimulation();
        _exploration = exploration ?? new ExplorationSimulation();

        if (combatRuntime is not null)
        {
            _combatCommands = combatRuntime;
            _combat = combatRuntime.Simulation;
        }
        else if (combat is not null)
        {
            // Compatibility path for existing subsystem/tests that inject a raw simulation.
            // Issuance and stepping remain supported, but preview fails closed because Core
            // cannot prove which private hostility view that simulation was constructed with.
            _combat = combat;
        }
        else
        {
            _combatCommands = new CombatCommandRuntime();
            _combat = _combatCommands.Simulation;
        }

        _colonization = colonization ?? new ColonizationSimulation();
        if (industryAllocationPolicy is not null)
            _industryAllocationPolicy = industryAllocationPolicy;
        else
        {
            _campaignIndustryPriorityProvider = new CampaignIndustryPriorityProvider(_strategicAi.IndustryPriorityProvider);
            _industryAllocationPolicy = new WeightedFairIndustryAllocationPolicy(_campaignIndustryPriorityProvider);
        }
        _advanceLegacyResearch = advanceLegacyResearch;
    }

    public CombatOrderResult IssueMilitaryOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order) =>
        _combatCommands is null
            ? _combat.IssueOrder(galaxy, civilizationId, fleetId, order)
            : _combatCommands.IssueOrder(galaxy, civilizationId, fleetId, order);

    public CombatBatchOrderResult IssueMilitaryOrders(
        GalaxyState galaxy,
        int civilizationId,
        IEnumerable<int> fleetIds,
        MilitaryOrder order) =>
        _combatCommands is null
            ? new CombatCommandBatchService(_combat).IssueOrder(galaxy, civilizationId, fleetIds, order)
            : _combatCommands.IssueOrders(galaxy, civilizationId, fleetIds, order);

    public CombatOrderResult IssueEngageHostilesOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId) =>
        _combatCommands is null
            ? new(false, "Engage Hostiles requires the campaign's matched combat command runtime.")
            : _combatCommands.IssueEngageHostiles(galaxy, civilizationId, fleetId);

    public CombatOrderResult IssueMilitaryDeploymentOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        int destinationSystemId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId && candidate.IsActive &&
            candidate.CivilizationId == civilizationId && candidate.Role == FleetRole.Military);
        if (fleet is null)
            return new(false, "No controllable active military fleet with that identity is available.");
        var destination = galaxy.Systems.FirstOrDefault(system => system.Id == destinationSystemId);
        if (destination is null)
            return new(false, "The selected deployment destination is not a valid star system.");
        if (fleet.CurrentSystemId == destinationSystemId && fleet.DestinationSystemId is null)
            return new(false, $"{fleet.Name} is already stationed in {destination.Name}.");

        var reach = _exploration.AssessOperationalReach(galaxy, fleetId, destinationSystemId);
        if (!reach.IsSupported)
            return new(false, reach.Reason);

        // A moving fleet cannot retain a system-local attack or defense assignment.
        var hold = IssueMilitaryOrder(galaxy, civilizationId, fleetId, new MilitaryOrder(MilitaryOrderType.Hold));
        if (!hold.Accepted) return hold;
        FleetRouteOrders.Assign(galaxy, fleet, destinationSystemId, reach);
        fleet.DestinationPlanetaryBodyId = null;
        return new(true, $"{fleet.Name} is deploying to {destination.Name}. {reach.Reason}");
    }

    /// <summary>
    /// Non-mutating preflight from the exact command runtime that owns authoritative Combat
    /// issuance. A coordinator built with a legacy standalone CombatSimulation fails closed here
    /// rather than silently previewing against a different political-hostility policy.
    /// </summary>
    public CombatOrderPreview PreviewMilitaryOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order) =>
        RequireCombatCommandRuntime().PreviewOrder(galaxy, civilizationId, fleetId, order);

    /// <summary>
    /// Non-mutating transient multi-selection preflight using the same matched command runtime.
    /// Target discovery remains outside Combat and actual issuance still revalidates all state.
    /// </summary>
    public CombatBatchOrderPreview PreviewMilitaryOrders(
        GalaxyState galaxy,
        int civilizationId,
        IEnumerable<int> fleetIds,
        MilitaryOrder order) =>
        RequireCombatCommandRuntime().PreviewOrders(galaxy, civilizationId, fleetIds, order);

    public MilitaryForceSummary GetOwnMilitaryForceSummary(GalaxyState galaxy, int civilizationId) =>
        _combat.GetOwnMilitaryForceSummary(galaxy, civilizationId);

    public CombatReadinessSummary GetOwnCombatReadinessSummary(GalaxyState galaxy, int civilizationId) =>
        CombatReadinessCalculator.Build(galaxy, civilizationId);

    /// <summary>
    /// Exact-own, non-mutating active-vessel Combat status for UI/AI consumption. Foreign vessel
    /// state and exact Attack target identity are deliberately absent from this owner-only surface.
    /// </summary>
    public OwnCombatFleetStatusView GetOwnCombatFleetStatus(
        GalaxyState galaxy,
        int civilizationId) =>
        OwnCombatFleetStatusBuilder.Build(galaxy, civilizationId);

    /// <summary>
    /// Read-only colony opportunity surface from the same ColonizationSimulation instance used by
    /// authoritative stepping. Presentation consumers therefore inherit the exact same Species,
    /// knowledge and operational-reach dependencies instead of constructing a second planner.
    /// </summary>
    public ColonizationOpportunityPlan GetColonyOpportunityPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = ColonizationOpportunityPlanner.DefaultMaximumCandidates) =>
        _colonization.GetOpportunityPlan(galaxy, fleetId, maximumCandidates);

    public ResourceOutpostOpportunityPlan GetResourceOutpostOpportunityPlan(
        GalaxyState galaxy,
        int fleetId,
        int maximumCandidates = ResourceOutpostOpportunityPlanner.DefaultMaximumCandidates) =>
        _colonization.GetResourceOutpostOpportunityPlan(galaxy, fleetId, maximumCandidates);

    public ColonyOrderResult IssueResourceOutpostFleetOrder(
        GalaxyState galaxy,
        int actingCivilizationId,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId &&
            candidate.CivilizationId == actingCivilizationId && ResourceOutpostOpportunityPlanner.IsOutpostFleet(candidate));
        return fleet is null
            ? new ColonyOrderResult(false, "No controllable staffed resource-outpost vessel with that fleet ID is available.")
            : _colonization.IssueResourceOutpostFleetOrder(galaxy, fleet.Id, destinationSystemId, planetaryBodyId);
    }

    public FreightOrderResult IssueFreightTransitOrder(
        GalaxyState galaxy, int actingCivilizationId, int fleetId, int targetSystemId) =>
        _freight.IssueTransitOrder(galaxy, actingCivilizationId, fleetId, targetSystemId);

    public FreightOrderResult IssueFreightCollectionOrder(
        GalaxyState galaxy, int actingCivilizationId, int fleetId, int outpostId) =>
        _freight.IssueCollectionOrder(galaxy, actingCivilizationId, fleetId, outpostId);

    public CivilianFleetHoldOrderResult IssueCivilianHoldOrder(
        GalaxyState galaxy, int actingCivilizationId, int fleetId) =>
        CivilianFleetHoldOrders.Hold(galaxy, actingCivilizationId, fleetId);

    public CivilianFleetHoldOrderResult IssueCivilianResumeOrder(
        GalaxyState galaxy, int actingCivilizationId, int fleetId) =>
        CivilianFleetHoldOrders.Resume(galaxy, actingCivilizationId, fleetId);

    public CivilianFleetReturnOrderResult IssueCivilianReturnToBaseOrder(
        GalaxyState galaxy, int actingCivilizationId, int fleetId, bool confirmAbandonColonyWork = false) =>
        CivilianFleetReturnOrders.RequestReturn(galaxy, actingCivilizationId, fleetId, confirmAbandonColonyWork);

    public CivilianFleetReturnOrderResult PreviewCivilianReturnToBase(
        GalaxyState galaxy, int actingCivilizationId, int fleetId) =>
        CivilianFleetReturnOrders.PreviewReturn(galaxy, actingCivilizationId, fleetId);

    /// <summary>
    /// Observer-scoped exact colony-fleet command boundary. Foreign and nonexistent fleet IDs use
    /// the same rejection so caller-visible command behavior does not reveal hidden ownership.
    /// The underlying colony command revalidates survey, passenger species, occupancy and reach.
    /// </summary>
    public ColonyOrderResult IssueColonyFleetOrder(
        GalaxyState galaxy,
        int actingCivilizationId,
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var fleet = galaxy.Fleets.FirstOrDefault(candidate =>
            candidate.Id == fleetId &&
            candidate.IsActive &&
            candidate.CivilizationId == actingCivilizationId &&
            candidate.Role == FleetRole.Colony &&
            candidate.EmbarkedPopulationMillions > 0.0);
        if (fleet is null)
        {
            return new ColonyOrderResult(
                false,
                "No controllable populated colony ship with that fleet ID is available.");
        }

        return _colonization.IssueColonyFleetOrder(
            galaxy,
            fleet.Id,
            destinationSystemId,
            planetaryBodyId);
    }

    public SimulationStepResult Advance(GalaxyState galaxy, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays), "Simulation time must be finite and non-negative.");
        if (simulationDays <= 0.0)
            return SimulationStepResult.Empty;

        var territory = Game.Simulation.Territory.TerritorialRuntime.Initialize(galaxy);
        territory.Advance(galaxy, simulationDays);

        var existingIndustryReserves = galaxy.Economies.ToDictionary(
            economy => economy.CivilizationId,
            economy => economy.Industry);
        _economy.Advance(galaxy, simulationDays, accrueLegacyScience: _advanceLegacyResearch);
        _campaignIndustryPriorityProvider?.Bind(galaxy);
        _strategicAi.Advance(galaxy, simulationDays);
        _construction.EnsureAutomaticOrders(galaxy);
        _shipbuilding.EnsureAutomaticOrders(galaxy);

        var constructionBudgets = new Dictionary<int, double>();
        var shipbuildingBudgets = new Dictionary<int, double>();
        var allocations = new List<CivilizationIndustryAllocation>();

        foreach (var civilization in galaxy.Civilizations.Where(civilization => !civilization.IsSeededAncient))
        {
            var economy = galaxy.Economies.First(state => state.CivilizationId == civilization.Id);
            var allocation = _industryAllocationPolicy.Allocate(new IndustryAllocationContext(
                civilization.Id,
                Math.Max(0.0, economy.Industry),
                _construction.GetIndustryDemand(galaxy, civilization.Id, simulationDays),
                _shipbuilding.GetIndustryDemand(galaxy, civilization.Id, simulationDays)));

            constructionBudgets[civilization.Id] = allocation.ConstructionAllocated;
            shipbuildingBudgets[civilization.Id] = allocation.ShipbuildingAllocated;
            allocations.Add(allocation);
        }

        var constructionEvents = _construction.Advance(galaxy, constructionBudgets, simulationDays);
        var shipbuildingEvents = _shipbuilding.Advance(galaxy, shipbuildingBudgets, simulationDays);
        var researchEvents = _advanceLegacyResearch ? _research.Advance(galaxy) : Array.Empty<ResearchEvent>();
        var explorationEvents = _exploration.Advance(galaxy, simulationDays);
        _freight.Advance(galaxy, simulationDays);
        var combatEvents = _combat.Advance(galaxy, simulationDays);
        var colonizationEvents = _colonization.Advance(galaxy, simulationDays);
        if (colonizationEvents.Count > 0 || combatEvents.Count > 0) territory.Recompute(galaxy);
        EconomySimulation.ApplyIndustryStorageCaps(galaxy, existingIndustryReserves);

        return new SimulationStepResult(
            simulationDays,
            allocations,
            constructionEvents,
            shipbuildingEvents,
            researchEvents,
            explorationEvents,
            combatEvents,
            colonizationEvents);
    }

    private CombatCommandRuntime RequireCombatCommandRuntime() =>
        _combatCommands ?? throw new InvalidOperationException(
            "Military-order preview is unavailable for a coordinator constructed with a standalone CombatSimulation. Supply a matched CombatCommandRuntime so preview and issuance share one hostility policy.");
}

public sealed record SimulationStepResult(
    double SimulationDays,
    IReadOnlyList<CivilizationIndustryAllocation> IndustryAllocations,
    IReadOnlyList<ConstructionEvent> ConstructionEvents,
    IReadOnlyList<ShipbuildingEvent> ShipbuildingEvents,
    IReadOnlyList<ResearchEvent> ResearchEvents,
    IReadOnlyList<ExplorationEvent> ExplorationEvents,
    IReadOnlyList<CombatEvent> CombatEvents,
    IReadOnlyList<ColonizationEvent> ColonizationEvents)
{
    public CombatOutcomeSummary CombatOutcome => CombatOutcomeSummaryBuilder.Build(CombatEvents);

    public static SimulationStepResult Empty { get; } = new(
        0.0,
        Array.Empty<CivilizationIndustryAllocation>(),
        Array.Empty<ConstructionEvent>(),
        Array.Empty<ShipbuildingEvent>(),
        Array.Empty<ResearchEvent>(),
        Array.Empty<ExplorationEvent>(),
        Array.Empty<CombatEvent>(),
        Array.Empty<ColonizationEvent>());
}
