using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Species;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path");

var json = new JsonSerializerOptions {
    WriteIndented = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals
};
var cases = new List<object>();
var capabilities = new Dictionary<int, HashSet<string>>();
var simulation = new ConstructionSimulation(new FixtureCapabilities(capabilities));

CivilizationState Civilization(int id = 1, bool player = true, bool ancient = false,
    CivilizationTraits? traits = null) => new(id, "Civ " + id, 0, CivilizationArchetype.Scientific,
        traits ?? new CivilizationTraits(0, 0, 0, 0, 0, 0, false), player,
        ancient ? CivilizationDevelopmentStage.AncientSpacefaring : CivilizationDevelopmentStage.WarpCapable,
        ancient, SpeciesId: SpeciesCatalog.TerranBaselineId);

GalaxyState World(double credits = 5000, double industry = 0, bool player = true, bool ancient = false,
    bool includeEconomy = true) {
    var civilization = Civilization(1, player, ancient);
    return new GalaxyState {
        Seed = 1, Systems = Array.Empty<StarSystemState>(), PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
        Civilizations = new List<CivilizationState> { civilization }, Fleets = new List<FleetState>(),
        Colonies = new List<ColonyState>(), Economies = includeEconomy
            ? new List<CivilizationEconomyState> { new() { CivilizationId = 1, Credits = credits, Industry = industry } }
            : Array.Empty<CivilizationEconomyState>(), Technologies = new List<Game.Simulation.Research.TechnologyState>(),
        ConstructionStates = new List<ConstructionState> { new() { CivilizationId = 1 } },
        ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(), PlayerCivilizationId = 1,
        Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState()
    };
}

JsonElement Clone(object value) => JsonSerializer.SerializeToElement(value, json);
void Add(string name, string kind, GalaxyState galaxy, object arguments, Func<object?> operation) {
    var before = Clone(galaxy);
    var capabilitySnapshot = capabilities.OrderBy(pair => pair.Key)
        .Select(pair => new { CivilizationId = pair.Key, CapabilityIds = pair.Value.Order().ToArray() }).ToArray();
    object? result = null;
    object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    var after = Clone(galaxy);
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Capabilities = capabilitySnapshot, Before = before,
        Result = result, Error = error, After = after });
}

void Start(string name, GalaxyState world, int civilization, string project) =>
    Add(name, "Start", world, new { CivilizationId = civilization, ProjectId = project },
        () => simulation.StartProject(world, civilization, project));
void Queue(string name, GalaxyState world, int civilization, string project) =>
    Add(name, "Queue", world, new { CivilizationId = civilization, ProjectId = project },
        () => simulation.QueueProject(world, civilization, project));
void Cancel(string name, GalaxyState world, int civilization, string project) =>
    Add(name, "Cancel", world, new { CivilizationId = civilization, ProjectId = project },
        () => simulation.CancelProject(world, civilization, project));
void SelectedAdvance(string name, GalaxyState world, int civilization, double budget, double days) =>
    Add(name, "SelectedAdvance", world, new { CivilizationId = civilization, Budget = budget, Days = days },
        () => simulation.AdvanceForCivilization(world, civilization, budget, days));
void GlobalAdvance(string name, GalaxyState world, Dictionary<int, double>? budgets, double days) =>
    Add(name, "GlobalAdvance", world, new { Budgets = budgets, Days = days },
        () => simulation.Advance(world, budgets, days));

// Catalog, lookups and read-only views.
cases.Add(new { Name = "catalog", Kind = "Catalog", Catalog = ConstructionRegistry.All });
var world = World();
Add("available-registry-order", "Available", world, new { CivilizationId = 1 },
    () => simulation.GetAvailableProjects(world, 1));
world.ConstructionStates[0].CompletedProjectIds.Add("orbital_launch_complex");
capabilities[1] = new() { "orbital_industry" };
Add("available-capability-and-prerequisite", "Available", world, new { CivilizationId = 1 },
    () => simulation.GetAvailableProjects(world, 1));
capabilities.Clear();
world = World();
Add("lock-both-requirements", "Lock", world, new { CivilizationId = 1, ProjectId = "orbital_shipyard" },
    () => simulation.GetLockReason(world, 1, ConstructionRegistry.Get("orbital_shipyard")));
world.ConstructionStates[0].QueuedProjects.Add(new("unknown", 7));
Add("blocker-unavailable", "Blocker", world, new { CivilizationId = 1 },
    () => simulation.GetQueueBlockerReason(world, 1));
world = World(); world.ConstructionStates[0].CompletedProjectIds.Add("research_network");
world.ConstructionStates[0].QueuedProjects.Add(new("research_network", 7));
Add("blocker-completed-head", "Blocker", world, new { CivilizationId = 1 },
    () => simulation.GetQueueBlockerReason(world, 1));
world = World(); world.ConstructionStates[0].CompletedProjectIds.Add("orbital_launch_complex");
world.ConstructionStates[0].QueuedProjects.Add(new("orbital_shipyard", 350));
Add("blocker-lost-capability", "Blocker", world, new { CivilizationId = 1 },
    () => simulation.GetQueueBlockerReason(world, 1));
world = World(); world.ConstructionStates[0].ActiveProjectId = "research_network";
world.ConstructionStates[0].ActiveProjectProgress = 100;
Add("demand-active", "Demand", world, new { CivilizationId = 1, Days = 2.0 },
    () => simulation.GetIndustryDemand(world, 1, 2));
world = World(); world.ConstructionStates[0].ActiveProjectId = "unknown-project";
world.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1, SystemId = 1, Name = "Home",
    SurfaceBuildings = { new SurfaceBuildingState { Id = 1, TypeId = "unknown-building" } } });
Add("demand-project-error-precedes-surface-error", "Demand", world, new { CivilizationId = 1, Days = 1.0 },
    () => simulation.GetIndustryDemand(world, 1, 1));
Add("refund-preview-legacy-zero", "Refund", world, new { CivilizationId = 1, ProjectId = "research_network" },
    () => simulation.GetCancellationRefundPreview(world.ConstructionStates[0], "research_network"));

// Starts and queues, including source ordering and promotion before later rejection.
Start("start-paid", World(), 1, "research_network");
Start("start-unknown-civilization", World(), 9, "research_network");
world = World(); world.ConstructionStates.Clear(); Start("start-missing-state-throws", world, 1, "research_network");
Start("start-unknown-project", World(), 1, "missing");
world = World(); world.ConstructionStates[0].CompletedProjectIds.Add("research_network");
Start("start-complete", world, 1, "research_network");
Start("start-locked", World(), 1, "orbital_shipyard");
Start("start-insufficient-threshold", World(149.9998), 1, "research_network");
Start("start-tolerance", World(149.99995), 1, "research_network");
world = World(); world.ConstructionStates[0].ActiveProjectId = "industrial_automation";
Start("start-active-precedes-project-validation", world, 1, "missing");
world = World(); world.ConstructionStates[0].QueuedProjects.Add(new("research_network", 19));
Start("start-promotes-before-reject", world, 1, "missing");

world = World(); simulation.StartProject(world, 1, "research_network");
Queue("queue-paid", world, 1, "industrial_automation");
Queue("queue-duplicate-active", world, 1, "research_network");
world = World(); simulation.StartProject(world, 1, "research_network");
for (var index = 0; index < ConstructionState.MaxQueuedProjects; ++index)
    world.ConstructionStates[0].QueuedProjects.Add(new QueuedConstructionProject("queued-" + index, index));
Queue("queue-full-precedes-validation", world, 1, "missing");
world = World(includeEconomy: false); world.ConstructionStates[0].ActiveProjectId = "research_network";
Queue("queue-missing-economy-throws", world, 1, "industrial_automation");
world = World(); simulation.StartProject(world, 1, "research_network");
Add("paid-queue-cancel-requeue-cycle", "QueueCycle", world,
    new { CivilizationId = 1, ProjectId = "industrial_automation" }, () => new object[] {
        simulation.QueueProject(world, 1, "industrial_automation"),
        simulation.CancelProject(world, 1, "industrial_automation"),
        simulation.QueueProject(world, 1, "industrial_automation")
    });

// Cancellation uses recorded authorization and promotes without charging twice.
world = World(); simulation.StartProject(world, 1, "research_network");
world.ConstructionStates[0].ActiveProjectProgress = 350; Cancel("cancel-partial", world, 1, "research_network");
world = World(); world.ConstructionStates[0].ActiveProjectId = "research_network";
world.ConstructionStates[0].ActiveProjectProgress = 350; world.ConstructionStates[0].ActiveProjectAuthorizationCredits = 0;
Cancel("cancel-old-save-zero", world, 1, "research_network");
world = World(); world.ConstructionStates[0].ActiveProjectId = "research_network";
world.ConstructionStates[0].ActiveProjectAuthorizationCredits = 150;
world.ConstructionStates[0].QueuedProjects.Add(new("industrial_automation", 17));
Cancel("cancel-active-promotes-recorded", world, 1, "research_network");
world = World(); world.ConstructionStates[0].ActiveProjectId = "research_network";
world.ConstructionStates[0].QueuedProjects.Add(new("industrial_automation", 33));
Cancel("cancel-queued-recorded", world, 1, "industrial_automation");
Cancel("cancel-noop", World(), 1, "research_network");

// Selected advancement: validation order, no-op selection, stock/budget rules and completion.
world = World(industry: 900); simulation.StartProject(world, 1, "research_network");
SelectedAdvance("advance-partial", world, 1, 60, 2);
world = World(industry: 900); simulation.StartProject(world, 1, "research_network");
world.ConstructionStates[0].ActiveProjectProgress = 699.99995;
world.ConstructionStates[0].QueuedProjects.Add(new("industrial_automation", 41));
SelectedAdvance("advance-complete-promotes-no-double-spend", world, 1, 900, 1);
SelectedAdvance("advance-unknown-selected-noop", World(industry: 20), 9, double.NaN, 1);
SelectedAdvance("advance-zero-days-before-bad-budget", World(industry: 20), 1, double.NaN, 0);
SelectedAdvance("advance-negative-days", World(), 1, 0, -1);
SelectedAdvance("advance-nonfinite-days", World(), 1, 0, double.PositiveInfinity);
SelectedAdvance("advance-nonfinite-budget", World(industry: 20), 1, double.NaN, 1);
SelectedAdvance("advance-nonfinite-stock-explicit-budget", World(industry: double.NaN), 1, 0, 1);
SelectedAdvance("advance-negative-budget-clamps", World(industry: 100), 1, -5, 1);

// Surface and project share the same resolved budget in source order.
world = World(industry: 1000); simulation.StartProject(world, 1, "research_network");
world.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1, SystemId = 1, Name = "Home",
    SurfaceBuildings = { new SurfaceBuildingState { Id = 1, TypeId = "power_generator", IndustryProgress = 0 } } });
SelectedAdvance("advance-shared-surface-split", world, 1, 60, 1);

// Whole-world path: automatic AI ordering, civilization order, ancient skip and sparse budgets.
world = World(credits: 1000, industry: 100, player: false);
GlobalAdvance("global-ai-authorizes-and-spends", world, null, 1);
world = World(credits: 1000, industry: double.NaN, player: false, ancient: true);
GlobalAdvance("global-ancient-skips-stock-and-budget", world, new() { [1] = double.NaN }, 1);
world = World(credits: 1000, industry: 100); simulation.StartProject(world, 1, "research_network");
GlobalAdvance("global-missing-budget-means-zero", world, new() { [99] = double.NaN }, 1);
world = World(credits: 1000, industry: 100); simulation.StartProject(world, 1, "research_network");
GlobalAdvance("global-default-stock-budget", world, null, 1);
world = World(credits: 1000, industry: 100, player: false, includeEconomy: false);
GlobalAdvance("global-ai-missing-economy-throws", world, null, 1);
world = World(credits: 1000, industry: 100, player: true, includeEconomy: false);
GlobalAdvance("global-player-missing-economy-throws", world, null, 1);

File.WriteAllText(args[0], JsonSerializer.Serialize(new {
    Format = "stellar-construction-projects-oracle-v2", Cases = cases
}, json) + Environment.NewLine);

sealed class FixtureCapabilities(Dictionary<int, HashSet<string>> values) : IConstructionCapabilityView {
    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) =>
        values.TryGetValue(civilizationId, out var set) && set.Contains(capabilityId);
}
