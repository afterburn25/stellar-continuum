using System.Numerics;
using System.Reflection;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

if (args.Length != 1)
    throw new ArgumentException("Expected output fixture path.");
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var json = new JsonSerializerOptions { WriteIndented = true, IncludeFields = true,
                                       NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Clone(object? value) => JsonSerializer.SerializeToElement(value, json);
var queueField = typeof(ShipyardState).GetField("_queuedBuilds", BindingFlags.Instance | BindingFlags.NonPublic)!;
List<ShipBuildOrderState> Queue(ShipyardState s) => (List<ShipBuildOrderState>)queueField.GetValue(s)!;
var caps = new List<(int Id, List<string> Values)>();
var prefs = new List<(int Id, ShipbuildingStrategicPreference Value)>();
var simulation = new ShipbuildingSimulation(new Caps(caps), new Prefs(prefs));
GalaxyState World(bool player = true)
{
    var civ = new CivilizationState(1, player ? "Terran Union" : "Orion League", 10, CivilizationArchetype.Adaptive,
                                    new CivilizationTraits(0, 0, 0, .8, 0, 0, false), player,
                                    CivilizationDevelopmentStage.WarpCapable, SpeciesId: "terran_baseline");
    var construction = new ConstructionState { CivilizationId = 1 };
    construction.CompletedProjectIds.Add("orbital_shipyard");
    caps.Clear();
    caps.Add((1, new() { "spacecraft_construction", "experimental_interstellar_transit", "reliable_ftl",
                         "extended_ftl_range" }));
    prefs.Clear();
    return new() { Seed = 1,
                   Systems = new[] { new StarSystemState(10, "Sol", new(2, 3), StarArchetype.Standard, true, false,
                                                         false, false) },
                   Civilizations = new List<CivilizationState> { civ },
                   Fleets = new List<FleetState>(),
                   Colonies = new List<ColonyState> { new() { Id = 20, CivilizationId = 1, SystemId = 10,
                                                              Name = "Earth", PopulationMillions = 3000,
                                                              PopulationSpeciesId = "terran_baseline" } },
                   Economies = new List<CivilizationEconomyState> { new() { CivilizationId = 1, Credits = 10000,
                                                                            Industry = 10000 } },
                   Technologies = new List<Game.Simulation.Research.TechnologyState>(),
                   ConstructionStates = new List<ConstructionState> { construction },
                   ShipyardStates = new List<ShipyardState> { new() { CivilizationId = 1 } },
                   PlayerCivilizationId = 1,
                   Knowledge = null! };
}
object Snapshot(GalaxyState g) => new {
    g.Civilizations,
    g.Systems,
    g.ConstructionStates,
    ShipyardStates = g.ShipyardStates
                         .Select(s => new { s.CivilizationId, s.NextOrderSequence, s.ActiveDesignId, s.ActiveOrderId,
                                            s.ActiveBuildProgress, s.ActiveAuthorizationCredits,
                                            s.ReservedPopulationMillions, s.ReservedPopulationSpeciesId,
                                            s.ReservedPopulationSourceColonyId, QueuedBuilds = Queue(s).ToArray() })
                         .ToArray(),
    g.Colonies,
    g.Economies,
    g.Fleets,
    Capabilities = caps.Select(x => new { CivilizationId = x.Id, CapabilityIds = x.Values }).ToArray(),
    Preferences = prefs
                      .Select(x => new { CivilizationId = x.Id,
                                         PreferredNewFleetRole = x.Value.PreferredNewFleetRole is {} r ? (int?)r : null,
                                         x.Value.DeferNewColonization })
                      .ToArray()
};
var cases = new List<object>();
void Add(string name, string kind, GalaxyState world, object arguments, Func<object?> operation)
{
    var before = Clone(Snapshot(world));
    object? rawResult = null;
    object? error = null;
    try
    {
        rawResult = operation();
    }
    catch (Exception ex)
    {
        error = new { Type = ex.GetType().Name, ex.Message };
    }
    var result = Clone(rawResult);
    var after = Clone(Snapshot(world));
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Before = before, Result = result, Error = error,
                    After = after });
}
void Start(string name, Action<GalaxyState>? setup = null, int civilization = 1, string design = "warp_scout")
{
    var w = World();
    setup?.Invoke(w);
    Add(name, "Start", w, new { CivilizationId = civilization, DesignId = design },
        () => simulation.StartBuild(w, civilization, design));
}
Start("start-active");
Start("start-unknown-civilization", civilization: 99);
Start("start-unknown-design", design: "missing");
Start("start-missing-shipyard", w => w.ShipyardStates.Clear());
Start("start-queue-full", w =>
                          {
                              var s = w.ShipyardStates[0];
                              s.ActiveDesignId = "warp_scout";
                              s.ActiveOrderId = "shipyard-1-1";
                              for (int i = 2; i <= 8; i++)
                                  Queue(s).Add(new() { OrderId = $"shipyard-1-{i}", DesignId = "warp_scout" });
                              s.NextOrderSequence = 9;
                          });
Start("start-queue-full-before-state-guard",
      w =>
      {
          var s = w.ShipyardStates[0];
          s.ActiveDesignId = "warp_scout";
          s.ActiveOrderId = "shipyard-1-1";
          s.ReservedPopulationMillions = double.NaN;
          for (int i = 2; i <= 8; i++)
              Queue(s).Add(new() { OrderId = $"shipyard-1-{i}", DesignId = "warp_scout" });
          s.NextOrderSequence = 9;
      });
Start("start-lock-capability", w => caps[0].Values.Remove("spacecraft_construction"));
Start("start-lock-project", w => w.ConstructionStates[0].CompletedProjectIds.Clear());
Start("start-missing-construction", w => w.ConstructionStates.Clear());
Start("start-missing-economy", w => ((List<CivilizationEconomyState>)w.Economies).Clear());
Start("start-insufficient-credit", w => w.Economies[0].Credits = 69);
Start("start-nonfinite-credit", w => w.Economies[0].Credits = double.NaN);
Start("start-order-exhausted", w => w.ShipyardStates[0].NextOrderSequence = long.MaxValue);
Start("start-stale-active-collision", w => w.ShipyardStates[0].ActiveOrderId = "shipyard-1-1");
Start("start-invalid-existing-id", w =>
                                   {
                                       var s = w.ShipyardStates[0];
                                       s.ActiveDesignId = "warp_scout";
                                       s.ActiveOrderId = "bad id";
                                   });
Start("start-population-missing", w => w.Colonies.Clear(), design: "colony_ship");
Start("start-population-low", w => w.Colonies[0].PopulationMillions = 749, design: "colony_ship");
Start("start-population-nan-stable",
      w =>
      {
          w.Colonies[0].PopulationMillions = double.NaN;
          w.Colonies.Add(new() { Id = 21, CivilizationId = 1, SystemId = 10, Name = "Mars", PopulationMillions = 900,
                                 PopulationSpeciesId = "terran_baseline" });
      },
      design: "colony_ship");
Start("start-unknown-species", w => w.Colonies[0].PopulationSpeciesId = "unknown", design: "colony_ship");
Start("start-queued", w => simulation.StartBuild(w, 1, "colony_ship"), design: "resource_outpost_ship");
Start("start-duplicate-colony-id-first-debit",
      w => w.Colonies.Insert(0, new() { Id = 20, CivilizationId = 9, SystemId = 10, Name = "Foreign duplicate",
                                        PopulationMillions = 600, PopulationSpeciesId = "terran_baseline" }),
      design: "colony_ship");
void Assess(string name, Action<GalaxyState>? setup = null, string id = "shipyard-1-1")
{
    var w = World();
    setup?.Invoke(w);
    Add(name, "Assess", w, new { CivilizationId = 1, OrderId = id }, () => simulation.AssessCancellation(w, 1, id));
}
Assess("assess-empty", id: "");
Assess("assess-unicode-blank", id: "\u2003");
Assess("assess-gone", id: "missing");
Assess("assess-active-partial", w =>
                                {
                                    simulation.StartBuild(w, 1, "colony_ship");
                                    simulation.AdvanceForCivilization(w, 1, 300, 10);
                                });
Assess("assess-queued",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           simulation.StartBuild(w, 1, "colony_ship");
       },
       "shipyard-1-2");
Assess("assess-ambiguous",
       w =>
       {
           var s = w.ShipyardStates[0];
           s.ActiveDesignId = "warp_scout";
           s.ActiveOrderId = "same";
           Queue(s).Add(new() { OrderId = "same", DesignId = "warp_scout" });
       },
       "same");
Assess("assess-unknown-design",
       w =>
       {
           var s = w.ShipyardStates[0];
           s.ActiveDesignId = "missing";
           s.ActiveOrderId = "x";
       },
       "x");
Assess("assess-invalid-accounting",
       w =>
       {
           var s = w.ShipyardStates[0];
           s.ActiveDesignId = "warp_scout";
           s.ActiveOrderId = "x";
           s.ActiveAuthorizationCredits = double.NaN;
       },
       "x");
Assess("assess-excess-progress",
       w =>
       {
           var s = w.ShipyardStates[0];
           s.ActiveDesignId = "warp_scout";
           s.ActiveOrderId = "x";
           s.ActiveAuthorizationCredits = 70;
           s.ActiveBuildProgress = 651;
       },
       "x");
Assess("assess-changed-source", w =>
                                {
                                    simulation.StartBuild(w, 1, "colony_ship");
                                    w.Colonies[0].PopulationSpeciesId = "pelagic_high_pressure";
                                });
Assess("assess-pop-overflow", w =>
                              {
                                  simulation.StartBuild(w, 1, "colony_ship");
                                  w.Colonies[0].PopulationMillions = double.MaxValue;
                              });
Assess("assess-treasury-overflow", w =>
                                   {
                                       simulation.StartBuild(w, 1, "warp_scout");
                                       w.Economies[0].Credits = double.MaxValue;
                                   });
Assess("assess-bad-promotion",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           Queue(w.ShipyardStates[0]).Add(new() { OrderId = "bad id", DesignId = "warp_scout" });
       });
Assess("assess-missing-state", w => w.ShipyardStates.Clear());
Assess("assess-missing-economy", w => ((List<CivilizationEconomyState>)w.Economies).Clear());
Assess("assess-negative-population",
       w =>
       {
           var s = w.ShipyardStates[0];
           s.ActiveDesignId = "warp_scout";
           s.ActiveOrderId = "x";
           s.ActiveAuthorizationCredits = 70;
           s.ReservedPopulationMillions = -1;
       },
       "x");
Assess("assess-state-guard", w => w.ShipyardStates[0].ReservedPopulationMillions = double.NaN, id: "missing");
void Cancel(string name, Action<GalaxyState> setup, string id)
{
    var w = World();
    setup(w);
    Add(name, "Cancel", w, new { CivilizationId = 1, OrderId = id }, () => simulation.CancelBuild(w, 1, id));
}
Cancel("cancel-active",
       w =>
       {
           simulation.StartBuild(w, 1, "colony_ship");
           simulation.AdvanceForCivilization(w, 1, 300, 10);
       },
       "shipyard-1-1");
Cancel("cancel-queued",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           simulation.StartBuild(w, 1, "colony_ship");
       },
       "shipyard-1-2");
Cancel("cancel-duplicate-state",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           w.ShipyardStates.Add(new() { CivilizationId = 1 });
       },
       "shipyard-1-1");
Cancel("cancel-duplicate-economy",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           ((List<CivilizationEconomyState>)w.Economies).Add(new() { CivilizationId = 1 });
       },
       "shipyard-1-1");
void Demand(string name, Action<GalaxyState>? setup = null, double days = double.PositiveInfinity)
{
    var w = World();
    setup?.Invoke(w);
    Add(name, "Demand", w, new { CivilizationId = 1, SimulationDays = days },
        () => simulation.GetIndustryDemand(w, 1, days));
}
Demand("demand-idle");
Demand("demand-active",
       w =>
       {
           simulation.StartBuild(w, 1, "warp_scout");
           w.ShipyardStates[0].ActiveBuildProgress = 123;
       },
       4);
Demand("demand-negative", w => simulation.StartBuild(w, 1, "warp_scout"), -2);
Demand("demand-nan", w => simulation.StartBuild(w, 1, "warp_scout"), double.NaN);
void Ensure(string name, Action<GalaxyState> setup)
{
    var w = World(false);
    setup(w);
    Add(name, "Ensure", w, new {},
        () =>
        {
            simulation.EnsureAutomaticOrders(w);
            return null;
        });
}
Ensure("ai-fallback-scout", w =>
                            {});
Ensure("ai-skip-player", w => w.Civilizations[0] = w.Civilizations[0] with { IsPlayer = true });
Ensure("ai-skip-ancient", w => w.Civilizations[0] = w.Civilizations[0] with { IsSeededAncient = true });
Ensure("ai-preferred-military", w => prefs.Add((1, new(FleetRole.Military, false))));
Ensure("ai-logistics-fallback", w => prefs.Add((1, new(FleetRole.Logistics, false))));
Ensure("ai-unaffordable", w => w.Economies[0].Credits = 0);
FleetState Fleet(int id, FleetRole role, bool active) => new() {
    Id = id,
    CivilizationId = 1,
    Name = "Existing",
    Role = role,
    DesignId = "warp_scout",
    Position = new(9, 8),
    CurrentSystemId = 10,
    DestinationSystemId = 11,
    TransitPhase = FleetTransitPhase.InterstellarWarp,
    TransitOriginSystemId = 10,
    TransitTargetSystemId = 11,
    TransitProgress = .4,
    LocalTransitStart = new(1, 2),
    LocalTransitPosition = new(3, 4),
    LocalTransitTarget = new(5, 6),
    PlannedRouteSystemIds = new() { 11 },
    HoldRequested = true,
    ReturnToBaseRequested = true,
    ReturnToBaseFailureReason = "hold",
    MissionOrderRevision = 7,
    DestinationPlanetaryBodyId = 4,
    PreventAutomaticSettlement = true,
    SettlementBodyId = 4,
    SettlementDaysCompleted = 2,
    ReconnaissanceSystemId = 10,
    ReconnaissanceDaysCompleted = 3,
    FreightTargetOutpostId = 2,
    FreightHomeColonyId = 20,
    CargoMaterialCapacity = 12,
    CargoMaterials = 5,
    StrategicSpeed = 7,
    MaximumLegRangeLightYears = 8,
    FuelCapacityLightYears = 9,
    FuelRemainingLightYears = 6,
    SensorRange = 4,
    IsActive = active,
    EmbarkedPopulationMillions = 1,
    EmbarkedPopulationSpeciesId = "terran_baseline",
    Combat = new() { ProfileId = "civilian_light_v1", Armor = 3, Hull = 4 },
    TacticalLoadout = new() { Weapons = new() { new() { Id = "w", Kind = MassiveWeaponKind.Beam } },
                              Modules = new() { new() { Id = "m", Kind = MassiveModuleKind.Sensor } } },
    TacticalVessel = new() { Id = 8, Name = "Historic", DesignId = "warp_scout", BattlesFought = 2, ConfirmedKills = 3 }
};
Ensure("ai-curious-science", w => w.Fleets.Add(Fleet(2, FleetRole.Scout, true)));
Ensure("ai-colony-fallback", w =>
                             {
                                 w.Fleets.Add(Fleet(2, FleetRole.Scout, true));
                                 w.Fleets.Add(Fleet(3, FleetRole.Science, true));
                             });
Ensure("ai-colony-deferred", w =>
                             {
                                 w.Fleets.Add(Fleet(2, FleetRole.Scout, true));
                                 w.Fleets.Add(Fleet(3, FleetRole.Science, true));
                                 prefs.Add((1, new(null, true)));
                             });
Ensure("ai-colony-expansion-disabled", w =>
                                       {
                                           w.Civilizations[0] = w.Civilizations[0] with { ExpansionAllowed = false };
                                           w.Fleets.Add(Fleet(2, FleetRole.Scout, true));
                                           w.Fleets.Add(Fleet(3, FleetRole.Science, true));
                                       });
Ensure("ai-colony-already-populated", w =>
                                      {
                                          w.Fleets.Add(Fleet(2, FleetRole.Scout, true));
                                          w.Fleets.Add(Fleet(3, FleetRole.Science, true));
                                          w.Fleets.Add(Fleet(4, FleetRole.Colony, true));
                                      });
Ensure("ai-busy-skipped", w => simulation.StartBuild(w, 1, "warp_scout"));
Ensure("ai-preferred-science", w => prefs.Add((1, new(FleetRole.Science, false))));
Ensure("ai-preferred-colony", w => prefs.Add((1, new(FleetRole.Colony, false))));
Ensure("ai-invalid-preference-fallback", w => prefs.Add((1, new((FleetRole)99, false))));
foreach (var design in new[] { "warp_scout", "science_vessel", "patrol_corvette", "colony_ship",
                               "resource_outpost_ship", "bulk_freighter" })
    foreach (var player in new[] { true, false })
    {
        var w = World(player);
        w.Fleets.Add(Fleet(40, ShipDesignRegistry.Get(design).Role, false));
        simulation.StartBuild(w, 1, design);
        Add($"complete-{design}-{(player?"player":"ai")}", "AdvanceSelected", w,
            new { CivilizationId = 1, IndustryBudget = 10000d, SimulationDays = 100d },
            () => simulation.AdvanceForCivilization(w, 1, 10000, 100));
    }
{
    var w = World();
    caps[0].Values.Remove("extended_ftl_range");
    simulation.StartBuild(w, 1, "warp_scout");
    Add("complete-reliable-propulsion", "AdvanceSelected", w,
        new { CivilizationId = 1, IndustryBudget = 10000d, SimulationDays = 100d },
        () => simulation.AdvanceForCivilization(w, 1, 10000, 100));
}
{
    var w = World();
    caps[0].Values.Remove("extended_ftl_range");
    caps[0].Values.Remove("reliable_ftl");
    simulation.StartBuild(w, 1, "warp_scout");
    Add("complete-prototype-propulsion", "AdvanceSelected", w,
        new { CivilizationId = 1, IndustryBudget = 10000d, SimulationDays = 100d },
        () => simulation.AdvanceForCivilization(w, 1, 10000, 100));
}
void Advance(string name, Action<GalaxyState> setup, Dictionary<int, double>? budgets, double days)
{
    var w = World();
    setup(w);
    Add(name, "Advance", w, new { Budgets = budgets, SimulationDays = days },
        () => simulation.Advance(w, budgets, days));
}
Advance("advance-zero", w => w.Civilizations[0] = w.Civilizations[0] with { IsPlayer = false }, null, 0);
Advance("advance-absent", w => simulation.StartBuild(w, 1, "warp_scout"), null, 1);
Advance("advance-empty", w => simulation.StartBuild(w, 1, "warp_scout"), new(), 1);
Advance("advance-missing-budget", w => simulation.StartBuild(w, 1, "warp_scout"), new() { { 2, 4 } }, 1);
Advance("advance-invalid-budget", w => simulation.StartBuild(w, 1, "warp_scout"), new() { { 1, double.NaN } }, 1);
foreach (var days in new[] { -1d, double.NaN, double.PositiveInfinity })
    Advance("advance-invalid-days-" + days,
            w =>
            {},
            null, days);
Advance("advance-idle-skips-economy", w => ((List<CivilizationEconomyState>)w.Economies).Clear(), null, 1);
Advance("advance-ancient",
        w =>
        {
            w.Civilizations[0] = w.Civilizations[0] with { IsSeededAncient = true };
            simulation.StartBuild(w, 1, "warp_scout");
        },
        null, 100);
Advance("advance-promotion-guard",
        w =>
        {
            simulation.StartBuild(w, 1, "warp_scout");
            w.ShipyardStates[0].ActiveBuildProgress = 650;
            Queue(w.ShipyardStates[0])
                .Add(new() { OrderId = "bad id", DesignId = "missing", ReservedPopulationMillions = 8,
                             ReservedPopulationSpeciesId = "terran_baseline", ReservedPopulationSourceColonyId = 20 });
        },
        null, 1);
Advance("advance-global-automatic-order", w => w.Civilizations[0] = w.Civilizations[0] with { IsPlayer = false }, null,
        1);
Advance("advance-partial-budget", w => simulation.StartBuild(w, 1, "warp_scout"), new() { { 1, 7 } }, 1);
{
    var w = World();
    var sequenceArguments = new { CivilizationId = 1,
                                  DesignIds = new[] { "colony_ship", "resource_outpost_ship", "warp_scout" },
                                  PartialIndustryBudget = 400d,
                                  PartialSimulationDays = 10d,
                                  CancellationOrderId = "shipyard-1-2",
                                  CompletionIndustryBudget = 5000d,
                                  CompletionSimulationDays = 100d,
                                  CompletionCount = 2 };
    Add("reservation-cancel-promotion-sequence", "Sequence", w, sequenceArguments,
        () =>
        {
            var orders =
                sequenceArguments.DesignIds.Select(id => simulation.StartBuild(w, sequenceArguments.CivilizationId, id))
                    .ToArray();
            var partial = simulation.AdvanceForCivilization(w, sequenceArguments.CivilizationId,
                                                            sequenceArguments.PartialIndustryBudget,
                                                            sequenceArguments.PartialSimulationDays);
            var assessment = simulation.AssessCancellation(w, sequenceArguments.CivilizationId,
                                                           sequenceArguments.CancellationOrderId);
            var cancellation =
                simulation.CancelBuild(w, sequenceArguments.CivilizationId, sequenceArguments.CancellationOrderId);
            var completions =
                Enumerable.Range(0, sequenceArguments.CompletionCount)
                    .Select(
                        _ => simulation.AdvanceForCivilization(w, sequenceArguments.CivilizationId,
                                                               sequenceArguments.CompletionIndustryBudget,
                                                               sequenceArguments.CompletionSimulationDays))
                    .ToArray();
            return new { Orders = orders, Partial = partial, Assessment = assessment, Cancellation = cancellation,
                         Completions = completions };
        });
}
File.WriteAllText(args[0],
                  JsonSerializer.Serialize(new { Format = "stellar-shipbuilding-oracle-v2", Cases = cases }, json));
sealed class Caps(List<(int Id, List<string> Values)> v) : IShipbuildingCapabilityView
{
    public bool HasCivilizationCapability(GalaxyState _, int id,
                                          string cap) => v.FirstOrDefault(x => x.Id == id).Values?.Contains(cap) ==
                                                         true;
}
sealed class Prefs(List<(int Id, ShipbuildingStrategicPreference Value)> v) : IShipbuildingStrategicPreferenceView
{
    public ShipbuildingStrategicPreference GetPreference(int id) => v.FirstOrDefault(x => x.Id == id).Value
                                                                    ?? ShipbuildingStrategicPreference.None;
}
