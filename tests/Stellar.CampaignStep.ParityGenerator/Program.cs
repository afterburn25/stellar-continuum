using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Industry;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var json = new JsonSerializerOptions { WriteIndented = false, IncludeFields = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, json);

StarSystemState System(int id, float x = 0, float y = 0) => new(id, $"System {id}", new(x, y), StarArchetype.Standard, true, false, false, false, null, StellarPrimaryClass.GYellowDwarf, null, null, null, $"campaign-step:{id}");
PlanetaryBodyState Body(int id, int system, bool rare = false) => new(id, system, null, id, $"Body {id}", PlanetaryBodyKind.Planet, 1, 1, new(1, 288, 101.3, PlanetaryAtmosphereRegime.OxygenNitrogen, PlanetarySolventRegime.Water, .1, false, true), true, rare, false, false);
CivilizationState Civ(int id = 1, bool player = true, CivilizationDevelopmentStage stage = CivilizationDevelopmentStage.PreWarp, bool seededAncient = false) => new(id, $"Civ {id}", 0, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, player, stage, seededAncient, false, false, "terran_baseline") { Leadership = CivilizationLeadershipState.CreateFoundingRoster(id, player) };
ColonyState Colony(int id = 1, int system = 0, int civ = 1) => new() { Id = id, CivilizationId = civ, SystemId = system, PlanetaryBodyId = 10, Name = $"Colony {id}", PopulationSpeciesId = "terran_baseline", PopulationMillions = 1000, Infrastructure = 1.2, Stability = .92, StoredFoodPopulationDaysMillions = 100000, StoredWaterPopulationDaysMillions = 100000 };
FleetCombatState Combat(string profile = "patrol_corvette_mk1") => new() { ProfileId = profile, Shields = 35, Armor = 45, Hull = 95, Order = MilitaryOrderType.Hold };
FleetState Fleet(int id, int civ, FleetRole role, int? system = 0, double cargoCapacity = 0) => new() { Id = id, CivilizationId = civ, Name = $"Fleet {id}", Role = role, DesignId = role switch { FleetRole.Scout => "warp_scout", FleetRole.Science => "science_vessel", FleetRole.Colony => "colony_ship", FleetRole.Military => "patrol_corvette", _ => "bulk_freighter" }, Position = Vector2.Zero, CurrentSystemId = system, StrategicSpeed = 22, MaximumLegRangeLightYears = 500, CargoMaterialCapacity = cargoCapacity, FuelCapacityLightYears = 1000, FuelRemainingLightYears = 900, SensorRange = 135, IsActive = true, Combat = Combat(role == FleetRole.Military ? "patrol_corvette_mk1" : "civilian_light_v1"), TacticalVessel = new MassiveVesselState { Id = id, Name = $"Fleet {id} vessel", DesignId = "history", IsFlagship = true, HullFraction = .9f, EngineFraction = .8f, SensorFraction = .7f, WarpDriveFraction = .6f, ReactorFraction = .5f, InterdictorFraction = .4f, BattlesFought = 3, ConfirmedKills = 2 } };
GalaxyState World(bool colony = false, bool player = true)
{
    var civs = new List<CivilizationState> { Civ(player: player) };
    return new GalaxyState { Seed = 38001, Systems = new List<StarSystemState> { System(0), System(1, 4) }, PlanetaryBodies = new List<PlanetaryBodyState> { Body(10, 0), Body(20, 1, true) }, Civilizations = civs, Fleets = new List<FleetState>(), Colonies = colony ? new List<ColonyState> { Colony() } : new List<ColonyState>(), Economies = new List<CivilizationEconomyState> { new() { CivilizationId = 1, Credits = 500, Industry = 200, Science = 0 } }, Technologies = new TechnologySeeder().Seed(civs), ConstructionStates = new ConstructionSeeder().Seed(civs), ShipyardStates = new ShipyardSeeder().Seed(civs), PlayerCivilizationId = 1, Knowledge = new CivilizationKnowledgeState() };
}
object Snapshot(GalaxyState g) => new { g.Seed, g.Systems, Bodies = g.PlanetaryBodies, g.Civilizations, g.Fleets, g.Colonies, g.Economies, g.Technologies, Construction = g.ConstructionStates, Shipyards = g.ShipyardStates, g.PlayerCivilizationId, Knowledge = new { CoreObservers = g.Knowledge.GetGalacticCoreObservers(), Observers = g.Civilizations.Select(c => new { CivilizationId = c.Id, HasCoreAccess = g.Knowledge.HasGalacticCoreAccess(c.Id), CoreDiscovered = g.Knowledge.IsGalacticCoreDiscovered(c.Id), KnownSystems = g.Knowledge.GetKnownSystems(c.Id), KnownCivilizations = g.Knowledge.GetKnownCivilizations(c.Id), Survey = g.Knowledge.GetSystemSurveyKnowledge(c.Id) }).ToArray() }, Core = g.GalacticCore };

var cases = new List<object>();
void AddStep(string name, Action<GalaxyState> arrange, double[] steps, string combatMode = "Matched", string capabilityMode = "DefaultPrototype", bool advanceLegacyResearch = true, bool useDefaultConstructor = false, int[]? hostilityThrowCalls = null, bool injectKnowledge = false, int[]? capabilityThrowCalls = null, bool useStrategicShipbuildingPreferences = true)
{
    var world = World(); arrange(world); var before = Freeze(Snapshot(world));
    var callbackCalls = new List<object>();
    var constructionCapability = new RecordingConstructionCapability(callbackCalls, capabilityThrowCalls ?? Array.Empty<int>());
    var shipbuildingCapability = new RecordingShipbuildingCapability(callbackCalls, capabilityThrowCalls ?? Array.Empty<int>());
    var hostility = new RecordingHostility(callbackCalls, hostilityThrowCalls ?? Array.Empty<int>());
    var director = capabilityMode == "Injected"
        ? new CivilizationStrategicDirector(new CivilizationStrategicInputBuilder(shipbuildingCapabilities: shipbuildingCapability))
        : new CivilizationStrategicDirector();
    var strategic = injectKnowledge
        ? new CivilizationStrategicRuntimeCoordinator(director: director, knowledgeProvider: new RecordingKnowledgeProvider(callbackCalls))
        : new CivilizationStrategicRuntimeCoordinator(director: director);
    GalaxySimulationStepCoordinator coordinator;
    if (useDefaultConstructor) coordinator = new GalaxySimulationStepCoordinator();
    else
    {
        var construction = capabilityMode == "Injected" ? new ConstructionSimulation(constructionCapability) : new ConstructionSimulation();
        var preferenceView = useStrategicShipbuildingPreferences ? strategic.ShipbuildingStrategicPreferenceView : null;
        var shipbuilding = capabilityMode == "Injected" ? new ShipbuildingSimulation(shipbuildingCapability, preferenceView) : new ShipbuildingSimulation(strategicPreferenceView: preferenceView);
        coordinator = combatMode == "Raw"
            ? new GalaxySimulationStepCoordinator(construction: construction, shipbuilding: shipbuilding, strategicAi: strategic, combat: new CombatSimulation(hostility), advanceLegacyResearch: advanceLegacyResearch)
            : new GalaxySimulationStepCoordinator(construction: construction, shipbuilding: shipbuilding, strategicAi: strategic, combatRuntime: new CombatCommandRuntime(hostility), advanceLegacyResearch: advanceLegacyResearch);
    }
    var commands = new List<object>();
    foreach (var days in steps)
    {
        var input = Freeze(new { SimulationDays = days });
        var callStart = callbackCalls.Count;
        SimulationStepResult? result = null; Exception? caught = null;
        try { result = coordinator.Advance(world, days); }
        catch (Exception error) { caught = error; }
        var frozenResult = result is null ? (JsonElement?)null : Freeze(new { result.SimulationDays, result.IndustryAllocations, result.ConstructionEvents, result.ShipbuildingEvents, result.ResearchEvents, result.ExplorationEvents, result.CombatEvents, result.ColonizationEvents, CombatOutcome = result.CombatOutcome });
        var errorResult = caught is null ? (JsonElement?)null : Freeze(new { Type = caught.GetType().Name, caught.Message });
        var perStepCalls = Freeze(callbackCalls.Skip(callStart).ToArray());
        commands.Add(new { Input = input, Result = frozenResult, Error = errorResult, After = Freeze(Snapshot(world)), CallbackCalls = perStepCalls });
    }
    cases.Add(new { Name = name, Kind = "Step", Arguments = new { InitialState = before, Steps = steps.Select(days => new { SimulationDays = days }).ToArray(), InjectionControls = new { AdvanceLegacyResearch = advanceLegacyResearch, CombatMode = combatMode, CapabilityMode = capabilityMode, KnowledgeMode = injectKnowledge ? "Injected" : "DefaultEmpty", UseDefaultConstructor = useDefaultConstructor, UseStrategicShipbuildingPreferences = useStrategicShipbuildingPreferences, HostilityThrowCalls = hostilityThrowCalls ?? Array.Empty<int>(), CapabilityThrowCalls = capabilityThrowCalls ?? Array.Empty<int>(), UsedConstrainedHomeFallback = false } }, Before = before, Commands = Freeze(commands), After = Freeze(Snapshot(world)), CallbackCalls = Freeze(callbackCalls.ToArray()) });
}

AddStep("zero-empty-default", _ => { }, new[] { 0d }, useDefaultConstructor: true);
AddStep("negative-rejected", _ => { }, new[] { -1d });
AddStep("nan-rejected", _ => { }, new[] { double.NaN });
AddStep("positive-infinity-rejected", _ => { }, new[] { double.PositiveInfinity });
AddStep("negative-infinity-rejected", _ => { }, new[] { double.NegativeInfinity });
AddStep("smallest-positive-step", _ => { }, new[] { double.Epsilon });
AddStep("seeded-ancient-phase-order", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.AncientSpacefaring, seededAncient: true); }, new[] { 1d });
AddStep("basic-default-two-steps", _ => { }, new[] { .25, 1d }, useDefaultConstructor: true);
AddStep("basic-raw-combat", _ => { }, new[] { 1d }, combatMode: "Raw");
AddStep("legacy-research-disabled", g => { g.Economies[0].Science = 50; g.Technologies[0].ActiveResearchId = "fusion_propulsion"; }, new[] { 1d }, advanceLegacyResearch: false);
AddStep("duplicate-economy-before-mutation", g => ((List<CivilizationEconomyState>)g.Economies).Add(new() { CivilizationId = 1, Credits = 9, Industry = 8 }), new[] { 1d });
AddStep("zero-before-duplicate-economy-validation", g => ((List<CivilizationEconomyState>)g.Economies).Add(new() { CivilizationId = 1, Credits = 9, Industry = 8 }), new[] { 0d });
AddStep("missing-economy-fails-at-first-lookup", g => ((List<CivilizationEconomyState>)g.Economies).Clear(), new[] { 1d });
AddStep("duplicate-civilization-allocation-input-order", g => { g.Civilizations.Clear(); g.Civilizations.Add(Civ(2)); g.Civilizations.Add(Civ(1)); g.Civilizations.Add(Civ(2)); ((List<CivilizationEconomyState>)g.Economies).Add(new() { CivilizationId = 2, Credits = 500, Industry = 200 }); g.Technologies.Add(new() { CivilizationId = 2 }); g.ConstructionStates.Add(new() { CivilizationId = 2 }); g.ShipyardStates.Add(new() { CivilizationId = 2 }); }, new[] { 1d });
AddStep("funded-colony-economy", g => { g.Colonies.Add(Colony()); g.Economies[0].Credits = 1000; }, new[] { 1d, 2d });
AddStep("nonfinite-industry-propagation-and-failure", g => g.Economies[0].Industry = double.NaN, new[] { 1d });
AddStep("research-completion-across-steps", g => { var technology = g.Technologies[0]; foreach (var id in new[] { "orbital_industry", "fusion_propulsion", "deep_space_sensors", "exotic_field_theory", "warp_field_control" }) technology.CompletedTechnologyIds.Add(id); technology.ActiveResearchId = "prototype_warp_drive"; technology.ActiveResearchProgress = 4799; g.ConstructionStates[0].CompletedProjectIds.Add("warp_test_facility"); g.Economies[0].Science = 2; }, new[] { 1d, 1d });
AddStep("injected-capability-on-demand", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.WarpCapable); g.ConstructionStates[0].CompletedProjectIds.Add("orbital_launch_complex"); g.Technologies[0].CompletedTechnologyIds.Add("orbital_industry"); g.Technologies[0].CompletedTechnologyIds.Add("prototype_warp_drive"); g.Economies[0].Credits = 5000; }, new[] { 1d, 1d }, capabilityMode: "Injected");
AddStep("shipbuilding-without-strategic-preference-view-falls-back-to-scout", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.WarpCapable); g.Colonies.Add(Colony()); g.ConstructionStates[0].CompletedProjectIds.Add("orbital_shipyard"); g.Technologies[0].CompletedTechnologyIds.Add("orbital_industry"); g.Technologies[0].CompletedTechnologyIds.Add("prototype_warp_drive"); g.Economies[0].Credits = 5000; }, new[] { 1d }, useStrategicShipbuildingPreferences: false);
AddStep("injected-strategic-knowledge-on-demand", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.WarpCapable); }, new[] { 1d, 1d }, injectKnowledge: true);
AddStep("construction-unlocks-next-step-ship-order", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.WarpCapable); var construction = g.ConstructionStates[0]; construction.CompletedProjectIds.Add("orbital_launch_complex"); construction.ActiveProjectId = "orbital_shipyard"; construction.ActiveProjectProgress = 1599; g.Technologies[0].CompletedTechnologyIds.Add("orbital_industry"); g.Technologies[0].CompletedTechnologyIds.Add("prototype_warp_drive"); g.Economies[0].Credits = 5000; g.Economies[0].Industry = 2000; var home = Colony(); home.Infrastructure = 5; g.Colonies.Add(home); }, new[] { 1d, 1d, 100d }, capabilityMode: "Injected");
AddStep("survey-before-timed-colony", g => { g.Knowledge.AdvanceSystemSurvey(1, 1, .2); var science = Fleet(10, 1, FleetRole.Science, 1); g.Fleets.Add(science); var colony = Fleet(11, 1, FleetRole.Colony, 1); colony.EmbarkedPopulationMillions = 4; colony.EmbarkedPopulationSpeciesId = "terran_baseline"; colony.DestinationPlanetaryBodyId = 20; colony.SettlementBodyId = 20; colony.SettlementDaysCompleted = 29; g.Fleets.Add(colony); }, new[] { 30d });
AddStep("next-step-freight-before-colonization-and-storage-cap", g => { g.Knowledge.MarkSystemFullySurveyed(1, 1); g.Economies[0].Industry = 0; var colony = Fleet(11, 1, FleetRole.Colony, 1); colony.EmbarkedPopulationMillions = 4; colony.EmbarkedPopulationSpeciesId = "terran_baseline"; colony.DestinationPlanetaryBodyId = 20; colony.SettlementBodyId = 20; colony.SettlementDaysCompleted = 30; g.Fleets.Add(colony); var freight = Fleet(12, 1, FleetRole.Logistics, 1, cargoCapacity: 100); freight.FreightHomeColonyId = 0; freight.CargoMaterials = 50; g.Fleets.Add(freight); }, new[] { 1d, 5d });
void CombatSetup(GalaxyState g) { var a = Fleet(1, 1, FleetRole.Military); var b = Fleet(2, 2, FleetRole.Military); a.Combat!.Order = MilitaryOrderType.Attack; a.Combat.TargetFleetId = 2; g.Fleets.Add(a); g.Fleets.Add(b); g.Civilizations.Add(Civ(2, false, CivilizationDevelopmentStage.WarpCapable)); ((List<CivilizationEconomyState>)g.Economies).Add(new() { CivilizationId = 2, Credits = 500, Industry = 200 }); g.Technologies.Add(new() { CivilizationId = 2 }); g.ConstructionStates.Add(new() { CivilizationId = 2 }); g.ShipyardStates.Add(new() { CivilizationId = 2 }); }
AddStep("combat-history-two-steps", CombatSetup, new[] { .1, .1 });
AddStep("raw-combat-history-two-steps", CombatSetup, new[] { .1, .1 }, combatMode: "Raw");
AddStep("default-constructor-peaceful-combat", CombatSetup, new[] { .1, .1 }, useDefaultConstructor: true);
AddStep("capability-failure-after-economy-then-retry", g => { g.Civilizations[0] = Civ(player: false, stage: CivilizationDevelopmentStage.WarpCapable); g.Economies[0].Credits = 5000; }, new[] { 1d, 1d }, capabilityMode: "Injected", capabilityThrowCalls: new[] { 2 });
AddStep("late-hostility-failure-then-retry", g => { g.Colonies.Add(Colony()); CombatSetup(g); }, new[] { .1, .1 }, hostilityThrowCalls: new[] { 1 });

void AddOutcome(string name, CombatEvent[] events)
{
    CombatOutcomeSummary? result = null; Exception? caught = null;
    try { result = CombatOutcomeSummaryBuilder.Build(events); }
    catch (Exception error) { caught = error; }
    cases.Add(new { Name = name, Kind = "CombatOutcome", Arguments = new { Events = Freeze(events) }, Result = result is null ? (JsonElement?)null : Freeze(result), Error = caught is null ? (JsonElement?)null : Freeze(new { Type = caught.GetType().Name, caught.Message }) });
}
CombatEvent Event(CombatEventType type, int? system = 1, int actorCiv = 1, int actorFleet = 1, int? targetCiv = 2, int? targetFleet = 2, double shield = 0, double armor = 0, double hull = 0, double casualties = 0) => new(type, system, actorCiv, actorFleet, targetCiv, targetFleet, shield, armor, hull, $"event-{(int)type}", casualties);
AddOutcome("outcome-empty", Array.Empty<CombatEvent>());
AddOutcome("outcome-damage-and-destroy", new[] { Event(CombatEventType.EngagementStarted), Event(CombatEventType.DamageApplied, shield: 3, armor: 4, hull: 5), Event(CombatEventType.FleetDestroyed, casualties: 2.5), Event(CombatEventType.EngagementEnded) });
AddOutcome("outcome-system-order-null-last", new[] { Event(CombatEventType.DamageApplied, system: null), Event(CombatEventType.DamageApplied, system: 5), Event(CombatEventType.DamageApplied, system: -1) });
AddOutcome("outcome-negative-sanitized", new[] { Event(CombatEventType.DamageApplied, shield: -1, armor: -2, hull: -3), Event(CombatEventType.FleetDestroyed, casualties: -4) });
AddOutcome("outcome-nonfinite-sanitized", new[] { Event(CombatEventType.DamageApplied, shield: double.NaN, armor: double.PositiveInfinity, hull: double.NegativeInfinity), Event(CombatEventType.FleetDestroyed, casualties: double.NaN) });
AddOutcome("outcome-self-target-same-civilization", new[] { Event(CombatEventType.DamageApplied, actorCiv: 1, targetCiv: 1, shield: 2, hull: 3), Event(CombatEventType.FleetDestroyed, actorCiv: 1, targetCiv: 1, casualties: 1) });
AddOutcome("outcome-retreat-and-escape", new[] { Event(CombatEventType.FleetRetreatInitiated, targetCiv: null, targetFleet: null), Event(CombatEventType.FleetEscaped, targetCiv: null, targetFleet: null) });
AddOutcome("outcome-unknown-enum-still-counted", new[] { Event((CombatEventType)99, system: null, actorCiv: 7, targetCiv: 8) });

var sourceOnly = new List<object>();
Exception? nullError = null; try { _ = new GalaxySimulationStepCoordinator().Advance(null!, 1); } catch (Exception error) { nullError = error; }
sourceOnly.Add(new { Name = "Advance-null-galaxy", Error = nullError is null ? (JsonElement?)null : Freeze(new { Type = nullError.GetType().Name, nullError.Message }) });
Exception? nullEventsError = null; try { _ = CombatOutcomeSummaryBuilder.Build(null!); } catch (Exception error) { nullEventsError = error; }
sourceOnly.Add(new { Name = "CombatOutcome-null-events", Error = nullEventsError is null ? (JsonElement?)null : Freeze(new { Type = nullEventsError.GetType().Name, nullEventsError.Message }) });
Exception? nullEntryError = null; try { _ = CombatOutcomeSummaryBuilder.Build(new CombatEvent[] { null! }); } catch (Exception error) { nullEntryError = error; }
sourceOnly.Add(new { Name = "CombatOutcome-null-entry", Error = nullEntryError is null ? (JsonElement?)null : Freeze(new { Type = nullEntryError.GetType().Name, nullEntryError.Message }) });
Exception? conflictingCombatError = null; try { _ = new GalaxySimulationStepCoordinator(combat: new CombatSimulation(), combatRuntime: new CombatCommandRuntime()); } catch (Exception error) { conflictingCombatError = error; }
sourceOnly.Add(new { Name = "Coordinator-conflicting-combat-injection", Error = conflictingCombatError is null ? (JsonElement?)null : Freeze(new { Type = conflictingCombatError.GetType().Name, conflictingCombatError.Message }) });
File.WriteAllText(args[0], JsonSerializer.Serialize(new { Schema = "stellar-campaign-step-oracle-v1", Cases = cases, SourceOnlyObservations = sourceOnly, Metadata = new { UsedConstrainedHomeFallback = "Not observable on C# GalaxyState; native handcrafted inputs initialize false.", RawDataset = "Bounded handcrafted source worlds; no substitute phase calculations." } }, json) + Environment.NewLine);

sealed class RecordingKnowledgeProvider(List<object> calls) : IStrategicKnowledgeProvider { public KnowledgeSnapshot Build(int observerCivilizationId, long nowTick) { calls.Add(new { Phase = "StrategicKnowledge", ObserverCivilizationId = observerCivilizationId, NowTick = nowTick }); return new KnowledgeSnapshot { ObservedAtTick = nowTick, Civilizations = new Dictionary<int, KnownCivilization>() }; } }
sealed class RecordingConstructionCapability(List<object> calls, IReadOnlyCollection<int> throwCalls) : IConstructionCapabilityView { public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) { var result = galaxy.Technologies.FirstOrDefault(t => t.CivilizationId == civilizationId)?.CompletedTechnologyIds.Contains(capabilityId) == true; calls.Add(new { Phase = "ConstructionCapability", CivilizationId = civilizationId, CapabilityId = capabilityId, Result = result }); if (throwCalls.Contains(calls.Count)) throw new InvalidOperationException("campaign-step capability failure"); return result; } }
sealed class RecordingShipbuildingCapability(List<object> calls, IReadOnlyCollection<int> throwCalls) : IShipbuildingCapabilityView { public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) { var technology = galaxy.Technologies.FirstOrDefault(t => t.CivilizationId == civilizationId); var result = capabilityId switch { ShipbuildingCapabilityIds.SpacecraftConstruction => technology?.CompletedTechnologyIds.Contains("orbital_industry") == true, ShipbuildingCapabilityIds.ExperimentalInterstellarTransit => technology?.CompletedTechnologyIds.Contains("prototype_warp_drive") == true, _ => false }; calls.Add(new { Phase = "ShipbuildingCapability", CivilizationId = civilizationId, CapabilityId = capabilityId, Result = result }); if (throwCalls.Contains(calls.Count)) throw new InvalidOperationException("campaign-step capability failure"); return result; } }
sealed class RecordingHostility(List<object> calls, IReadOnlyCollection<int> throwCalls) : ICombatHostilityView { private int index; public bool AreHostile(int first, int second) { index++; calls.Add(new { Phase = "Hostility", FirstCivilizationId = first, SecondCivilizationId = second }); if (throwCalls.Contains(index)) throw new InvalidOperationException("campaign-step hostility failure"); return true; } }
