using Game.Simulation;
using Game.Simulation.Colonization;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Campaign;
using Game.Simulation.AI;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class DemoProgressionValidation
{
    // The same quarter-day upper bound used by the live clock, with no wall-clock sleeps.
    private const double StepDays = 0.25;
    private const double MaximumDays = 7500;

    public static void Run() => RunSeed(20260908);
    public static void RunPlayerSandbox() => RunSeed(20260908, usePlayerSandbox: true);
    public static void RunDemo() => RunSeed(PlayableDemoScenario.Seed, useDemoClock: true);

    public static void RunSeed(long seed, bool useDemoClock = false, bool usePlayerSandbox = false)
    {
        var bootstrap = usePlayerSandbox
            ? new CampaignSessionService().CreateNew(seed.ToString(System.Globalization.CultureInfo.InvariantCulture))
            : null;
        var galaxy = bootstrap?.Galaxy ?? new GalaxyGenerator().Generate(seed);
        var adaptiveRuntime = bootstrap?.AdaptiveResearch.Runtime ?? AdaptiveResearchStrategicRuntime.LoadFromDirectory(
            AdaptiveResearchDataLocator.FindDataRoot());
        var adaptiveCampaign = bootstrap?.AdaptiveResearch ?? new AdaptiveResearchCampaignFactory(adaptiveRuntime).Create(galaxy);
        var player = galaxy.Civilizations.Single(c => c.IsPlayer);
        var playerId = player.Id;
        var adaptiveResearch = adaptiveCampaign.GetCivilization(playerId);
        var playerContext = adaptiveCampaign.Starts[playerId].ApplicabilityContextId;
        var constructionState = galaxy.ConstructionStates.Single(c => c.CivilizationId == playerId);
        var economy = galaxy.Economies.Single(e => e.CivilizationId == playerId);
        var source = galaxy.Colonies.Where(c => c.CivilizationId == playerId).MaxBy(c => c.PopulationMillions)!;
        var construction = new ConstructionSimulation(
            new AdaptiveResearchConstructionCapabilityView(adaptiveCampaign));
        var shipbuilding = new ShipbuildingSimulation(
            new AdaptiveResearchShipbuildingCapabilityView(adaptiveCampaign));
        var adaptiveSimulation = new AdaptiveResearchCampaignSimulation();
        var exploration = new ExplorationSimulation();
        var diplomacyState = bootstrap?.Diplomacy ?? new DiplomacyState();
        var diplomacyRuntime = new DiplomacyCampaignRuntimeCoordinator(diplomacyState);
        diplomacyRuntime.Reset(0, reviewImmediately: true);
        var strategicAi = new CivilizationStrategicRuntimeCoordinator(
            knowledgeProvider: new DiplomacyStrategicKnowledgeProvider(diplomacyState));
        var coordinator = new GalaxySimulationStepCoordinator(
            construction: construction, shipbuilding: shipbuilding,
            exploration: exploration, strategicAi: strategicAi,
            combatRuntime: diplomacyRuntime.CreateCombatCommandRuntime(), advanceLegacyResearch: false);
        var constructionPriority = new[] { "research_network", "industrial_automation",
            "orbital_launch_complex", "orbital_shipyard", "warp_test_facility" };
        var elapsed = 0.0;
        var warpDay = 0.0;
        var shipsQueued = false;
        var shipOrdersPlaced = new HashSet<string>(StringComparer.Ordinal);
        string? researchPausedForShipbuilding = null;
        var reconCompleted = false;
        var surveysCompleted = 0;
        int? scienceTarget = null;
        var nextSciencePlanningDay = 0.0;
        int? scoutTarget = null;
        int? colonyFleetId = null;
        int? settlementBodyId = null;
        var nextColonyPlanningDay = 0.0;
        var demoClock = new SimulationClock();
        demoClock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        var pendingSteps = new Queue<double>();
        var demoRealSeconds = 0.0;

        var scenario = useDemoClock ? "24x demo" : usePlayerSandbox ? "ordinary Player Sandbox" : "legacy generator";
        void Note(string message) => Console.WriteLine($"DEMO scenario={scenario} seed={seed} day={elapsed:0.##}: {message}");
        if (usePlayerSandbox)
        {
            Require(galaxy.Systems.Count == 500 && galaxy.GenerationMetadata is { GalaxyShape: "Full galaxy" },
                "ordinary Player Sandbox did not use the canonical full 500-system galaxy profile");
            Note("validated canonical full 500-system galaxy bootstrap");
        }
        Require(!galaxy.Fleets.Any(f => f.CivilizationId == playerId), "new pre-warp player already has ships");
        Require(!shipbuilding.StartBuild(galaxy, playerId, "colony_ship").Accepted,
            "new campaign bypassed physical-ship prerequisites");
        Note($"start species={player.SpeciesId}; population={source.PopulationMillions:0.##}M; industry={economy.Industry:0.##}; science={economy.Science:0.##}");

        while (elapsed < MaximumDays)
        {
            var currentStepDays = StepDays;
            if (useDemoClock)
            {
                if (pendingSteps.Count == 0)
                {
                    foreach (var frameStep in PlayableDemoScenario.AdvanceFrame(demoClock, 1.0 / 60.0)) pendingSteps.Enqueue(frameStep);
                    demoRealSeconds += 1.0 / 60.0;
                }
                currentStepDays = pendingSteps.Dequeue();
            }
            if (constructionState.ActiveProjectId is null)
            {
                var available = construction.GetAvailableProjects(galaxy, playerId);
                var next = constructionPriority.FirstOrDefault(id => available.Any(p => p.Id == id));
                if (next is not null)
                {
                    var order = construction.StartProject(galaxy, playerId, next);
                    Require(order.Accepted, order.Message);
                    Note(order.Message);
                }
            }
            if (adaptiveResearch.ActiveProjects.Count == 0)
            {
                var view = adaptiveRuntime.Authority.Kernel.BuildView(adaptiveResearch, playerContext);
                var next = EarlyCampaignResearchPlan.WarpCapabilityPath
                    .Select(id => view.VisibleNodes.FirstOrDefault(node => node.NodeId == id))
                    .FirstOrDefault(node => node is
                        { State: ResearchMaturity.Investigable, Blockers.Count: 0, MinimumLabs: not null } &&
                        node.MinimumLabs <= view.DirectedProgramCapacity.FreeEffectiveLabs + 0.000001);
                if (next is not null)
                {
                    var definition = adaptiveRuntime.Authority.Catalog.GetNode(next.NodeId);
                    var labs = Math.Min(definition.ProjectRequirements.RecommendedLabs,
                        adaptiveResearch.FreeEffectiveLabs);
                    var order = AdaptiveResearchCampaignCommands.StartDirectedResearch(
                        galaxy, adaptiveCampaign, playerId, next.NodeId, labs,
                        next.TargetApplicabilityContextId ?? playerContext);
                    Require(order.Accepted, order.Message);
                    Note($"Research started: {definition.Name} ({labs:0.#} labs).");
                }
            }
            if (!shipsQueued && adaptiveResearch.HasCapability("experimental_interstellar_transit"))
            {
                if (warpDay <= 0.0) warpDay = elapsed;
                if (researchPausedForShipbuilding is null)
                {
                    var active = adaptiveResearch.ActiveProjects.Values.FirstOrDefault(project => !project.Paused);
                    if (active is not null)
                    {
                        var pause = AdaptiveResearchCampaignCommands.PauseDirectedResearch(adaptiveCampaign, playerId, active.NodeId);
                        Require(pause.Accepted, pause.Message);
                        researchPausedForShipbuilding = active.NodeId;
                        Note($"Research paused to accumulate shipbuilding capital: {active.NodeId}.");
                    }
                }
                foreach (var design in new[] { "warp_scout", "science_vessel", "colony_ship" })
                {
                    if (shipOrdersPlaced.Contains(design)) continue;
                    var definition = ShipDesignRegistry.Get(design);
                    if (economy.Credits + 0.0001 < definition.CreditCost) break;
                    var populationBefore = source.PopulationMillions;
                    var order = shipbuilding.StartBuild(galaxy, playerId, design);
                    Require(order.Accepted, order.Message);
                    if (design == "colony_ship")
                        Require(Math.Abs(populationBefore - source.PopulationMillions - 250) < 0.000001,
                            "colony order did not reserve 250M actual source inhabitants");
                    Note(order.Message);
                    shipOrdersPlaced.Add(design);
                }
                shipsQueued = shipOrdersPlaced.Count == 3;
            }

            var own = galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == playerId).ToArray();
            var scout = own.SingleOrDefault(f => f.Role == FleetRole.Scout);
            var science = own.SingleOrDefault(f => f.Role == FleetRole.Science);
            var colony = own.SingleOrDefault(f => f.Role == FleetRole.Colony);
            if (scout is not null && scoutTarget is null)
            {
                var candidate = exploration.GetMissionPlan(galaxy, scout.Id).Candidates.First(c => c.Reach.IsSupported);
                var order = exploration.IssueSurveyOrder(galaxy, scout.Id, candidate.SystemId);
                Require(order.Accepted, order.Message);
                scoutTarget = candidate.SystemId;
                Note($"scout dispatched to system {scoutTarget}");
            }
            if (colony is not null && settlementBodyId is null && elapsed >= nextColonyPlanningDay)
            {
                nextColonyPlanningDay = elapsed + 5.0;
                colonyFleetId = colony.Id;
                var candidate = coordinator.GetColonyOpportunityPlan(galaxy, colony.Id).Candidates.FirstOrDefault(c => c.CanOrder);
                if (candidate is not null)
                {
                    Require(galaxy.Knowledge.IsSystemFullySurveyed(playerId, candidate.SystemId),
                        "settlement opportunity bypassed detailed survey");
                    var order = coordinator.IssueColonyFleetOrder(galaxy, playerId, colony.Id,
                        candidate.SystemId, candidate.PlanetaryBodyId);
                    Require(order.Accepted, order.Message);
                    settlementBodyId = candidate.PlanetaryBodyId;
                    Note($"colony mission accepted system={candidate.SystemId} body={candidate.PlanetaryBodyId}");
                }
            }

            if (science is not null && science.DestinationSystemId is null && elapsed >= nextSciencePlanningDay &&
                (scienceTarget is null || galaxy.Knowledge.IsSystemFullySurveyed(playerId, scienceTarget.Value)) &&
                settlementBodyId is null)
            {
                // Route planning becomes progressively more expensive as the detailed-survey
                // frontier closes. An idle ship should not recompute the same galaxy-wide plan
                // four times per simulated day while the treasury is accumulating.
                nextSciencePlanningDay = elapsed + 10.0;
                var candidate = exploration.GetMissionPlan(galaxy, science.Id).Candidates.FirstOrDefault(c => c.Reach.IsSupported);
                if (candidate is not null)
                {
                    var order = exploration.IssueSurveyOrder(galaxy, science.Id, candidate.SystemId);
                    Require(order.Accepted, order.Message);
                    scienceTarget = candidate.SystemId;
                    Note($"science vessel dispatched to system {scienceTarget}");
                }
                else
                {
                    // No state that can open another route changes while this scripted vessel
                    // is idle, so repeating the same exhaustive query cannot produce a mission.
                    nextSciencePlanningDay = double.PositiveInfinity;
                }
            }

            var step = coordinator.Advance(galaxy, currentStepDays);
            elapsed += currentStepDays;
            var adaptiveEvents = adaptiveSimulation.Advance(
                galaxy, adaptiveCampaign, currentStepDays, elapsed);
            diplomacyRuntime.Process(step.ExplorationEvents, step.CombatEvents, elapsed);
            foreach (var e in adaptiveEvents.Where(e => e.CivilizationId == playerId)) Note(e.Message);
            foreach (var e in step.ConstructionEvents.Where(e => e.CivilizationId == playerId)) Note(e.Message);
            foreach (var e in step.ShipbuildingEvents.Where(e => e.CivilizationId == playerId)) Note(e.Message);
            foreach (var e in step.ExplorationEvents.Where(e => e.CivilizationId == playerId))
            {
                if (e.Type == ExplorationEventType.SystemReconnoitered) reconCompleted = true;
                if (e.Type == ExplorationEventType.SystemSurveyed) surveysCompleted++;
                if (e.Type is ExplorationEventType.SystemReconnoitered or ExplorationEventType.SystemSurveyed) Note(e.Message);
            }
            Require(double.IsFinite(economy.Industry) && economy.Industry >= -0.000001 &&
                double.IsFinite(economy.Science) && economy.Science >= -0.000001 &&
                double.IsFinite(source.PopulationMillions) && source.PopulationMillions > 0,
                "progression produced invalid industry/science/population state");
            var settlement = galaxy.Colonies.FirstOrDefault(c => c.CivilizationId == playerId &&
                c.SystemId != player.HomeSystemId);
            if (settlement is null) continue;

            Require(shipsQueued && warpDay > 0 && reconCompleted && surveysCompleted > 0,
                "settlement did not traverse research, physical scout, reconnaissance and science survey");
            Require(settlement.PlanetaryBodyId == settlementBodyId && settlement.PopulationSpeciesId == source.PopulationSpeciesId,
                "settlement changed commanded body or passenger species");
            Require(Math.Abs(settlement.PopulationMillions - 250) < 0.000001,
                "settlement did not receive the actual 250M embarked passengers");
            Require(!galaxy.Fleets.Single(f => f.Id == colonyFleetId).IsActive,
                "settlement did not consume physical colony ship");
            Note($"PASS founded colony; warp={warpDay:0.##} days; settlement={elapsed:0.##} days; surveys={surveysCompleted}; fastest 8x active time={elapsed / 480:0.00} minutes; normal={elapsed / 60:0.00} minutes; industry={economy.Industry:0.##}; population={source.PopulationMillions:0.##}M");
            if (useDemoClock)
            {
                Require(demoRealSeconds < 300, $"24x demo exceeded five active minutes: {demoRealSeconds:0.##} seconds");
                Note($"PASS 24x bounded-frame demo: {demoRealSeconds:0.##} active seconds at 60 frames/second");
            }
            return;
        }
        var activeResearch = adaptiveResearch.ActiveProjects.Values.FirstOrDefault()?.NodeId ?? "idle";
        throw new InvalidOperationException($"Demo stalled seed={seed} after {elapsed} days: research={activeResearch} construction={constructionState.ActiveProjectId ?? "idle"}; shipsQueued={shipsQueued}; scienceTarget={scienceTarget}; surveys={surveysCompleted}; body={settlementBodyId}; industry={economy.Industry}; science={economy.Science}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
