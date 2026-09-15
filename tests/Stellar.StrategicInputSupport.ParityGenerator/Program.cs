using System.Globalization;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output path.");
var json = new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object value) => JsonSerializer.SerializeToElement(value, json);

CivilizationState Civ(int id) => new(id, $"Civ {id}", 0, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, id == 1, CivilizationDevelopmentStage.WarpCapable);
FleetState Fleet(int id, int civilization = 1, FleetRole role = FleetRole.Military, bool active = true, FleetCombatState? combat = null, int? system = 7) => new() {
    Id = id, CivilizationId = civilization, Name = $"Fleet {id}", Role = role, Position = Vector2.Zero,
    CurrentSystemId = system, IsActive = active, Combat = combat
};
FleetCombatState Combat(string profile, double shields, double armor, double hull, int order = 0, bool disengaged = false, int? system = null) => new() {
    ProfileId = profile, Shields = shields, Armor = armor, Hull = hull,
    Order = (MilitaryOrderType)order, IsDisengaged = disengaged, DisengagedSystemId = system
};
TechnologyState Tech(int civilization, params string[] completed) { var state = new TechnologyState { CivilizationId = civilization }; foreach (var id in completed) state.CompletedTechnologyIds.Add(id); return state; }
GalaxyState World(IEnumerable<FleetState>? fleets = null, IEnumerable<TechnologyState>? technologies = null, params int[] civilizations) => new() {
    Seed = 34, Systems = Array.Empty<StarSystemState>(), PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
    Civilizations = (civilizations.Length == 0 ? new[] { 1, 2 } : civilizations).Select(Civ).ToList(),
    Fleets = (fleets ?? Array.Empty<FleetState>()).ToList(), Colonies = new List<ColonyState>(),
    Economies = Array.Empty<CivilizationEconomyState>(), Technologies = (technologies ?? Array.Empty<TechnologyState>()).ToList(),
    ConstructionStates = new List<ConstructionState>(), ShipyardStates = new List<ShipyardState>(),
    PlayerCivilizationId = 1, Knowledge = new CivilizationKnowledgeState()
};
object State(GalaxyState world) => new { world.Civilizations, world.Fleets, world.Technologies };

var readiness = new List<object>();
void Ready(string name, GalaxyState world, int civilization = 1) {
    var before = Freeze(State(world)); CombatReadinessSummary? result = null; Exception? error = null;
    try { result = CombatReadinessCalculator.Build(world, civilization); } catch (Exception caught) { error = caught; }
    var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    var after = Freeze(State(world));
    readiness.Add(new { Name = name, CivilizationId = civilization, Input = before, Result = frozenResult, Error = frozenError, Before = before, After = after });
}
Ready("empty", World());
Ready("inactive", World(new[] { Fleet(1, active: false) }));
Ready("foreign", World(new[] { Fleet(1, civilization: 2) }));
foreach (var role in Enum.GetValues<FleetRole>()) Ready($"missing-{role}", World(new[] { Fleet(10 + (int)role, role: role) }));
foreach (var role in Enum.GetValues<FleetRole>()) Ready($"invalid-profile-{role}", World(new[] { Fleet(20 + (int)role, role: role, combat: Combat("missing", 1, 2, 3, 3, true, 7)) }));
var profiles = new[] { "civilian_light_v1", "civilian_science_v1", "civilian_heavy_v1", "patrol_corvette_mk1" };
var values = new[] { ("zero", 0d, 0d, 0d), ("fractional", .125, .375, .625), ("negative", -1d, -2d, -3d), ("over", 999d, 999d, 999d), ("nan", double.NaN, double.NaN, double.NaN), ("infinities", double.PositiveInfinity, double.NegativeInfinity, double.PositiveInfinity) };
var id = 100;
foreach (var profile in profiles) foreach (var value in values) Ready($"{profile}-{value.Item1}", World(new[] { Fleet(id++, combat: Combat(profile, value.Item2, value.Item3, value.Item4)) }));
foreach (var order in new[] { -1, 0, 1, 2, 3, 4 }) Ready($"order-{order}", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, order)) }));
Ready("disengaged-null-null", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, disengaged: true, system: null), system: null) }));
Ready("disengaged-null-current", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, disengaged: true, system: null), system: 7) }));
Ready("disengaged-match", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, disengaged: true, system: 7), system: 7) }));
Ready("disengaged-mismatch", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, disengaged: true, system: 8), system: 7) }));
Ready("disengaged-flag-false", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 95, disengaged: false, system: 7), system: 7) }));
Ready("unknown-profile-disengaged-ignored", World(new[] { Fleet(id++, combat: Combat("bad", 0, 0, 0, 3, true, 7)) }));
Ready("multi-unsorted-fractional", World(new[] { Fleet(9, combat: Combat("patrol_corvette_mk1", .1, .2, .3)), Fleet(2, combat: Combat("patrol_corvette_mk1", .4, .5, .6)), Fleet(5, combat: Combat("civilian_science_v1", .7, .8, .9)) }));
Ready("multi-equal-id-stable", World(new[] { Fleet(4, combat: Combat("patrol_corvette_mk1", .1, .2, .3)), Fleet(4, combat: Combat("patrol_corvette_mk1", .4, .5, .6)), Fleet(3, combat: Combat("patrol_corvette_mk1", .7, .8, .9)) }));
Ready("mixed-active-owner", World(new[] { Fleet(1), Fleet(2, active: false), Fleet(3, civilization: 2), Fleet(4, role: FleetRole.Scout) }));
Ready("epsilon-hull-zero", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, 0)) }));
Ready("epsilon-hull-exact", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, .0000001)) }));
Ready("epsilon-hull-above", World(new[] { Fleet(id++, combat: Combat("patrol_corvette_mk1", 35, 45, .0000001000001)) }));
Ready("missing-civilization", World(), 999);

var capabilities = new List<object>();
void Capability(string name, string kind, GalaxyState world, int civilization, string capability) {
    if (kind != "construction" && kind != "shipbuilding") throw new InvalidOperationException($"Unknown fixture capability kind {kind}.");
    var before = Freeze(State(world)); bool? result = null; Exception? error = null;
    try { result = kind == "construction" ? new PrototypeConstructionCapabilityView().HasCivilizationCapability(world, civilization, capability) : new PrototypeShipbuildingCapabilityView().HasCivilizationCapability(world, civilization, capability); }
    catch (Exception caught) { error = caught; }
    var after = Freeze(State(world));
    capabilities.Add(new { Name = name, Kind = kind, CivilizationId = civilization, CapabilityId = capability, Input = before, Result = result, Error = error is null ? null : new { Type = error.GetType().Name, error.Message }, Before = before, After = after });
}
var duplicate = World(technologies: new[] { Tech(1, "first", "orbital_industry"), Tech(1, "second", "prototype_warp_drive", "reliable_ftl", "extended_ftl_range"), Tech(2, "other") });
foreach (var capability in new[] { "first", "second", "orbital_industry", "prototype_warp_drive", "ORBITAL_INDUSTRY", "", "missing" }) Capability($"construction-{capability}", "construction", duplicate, 1, capability);
Capability("construction-missing-civ", "construction", duplicate, 9, "first");
foreach (var capability in new[] { "spacecraft_construction", "experimental_interstellar_transit", "reliable_ftl", "extended_ftl_range", "orbital_industry", "prototype_warp_drive", "SPACECRAFT_CONSTRUCTION", "missing" }) Capability($"ship-{capability}", "shipbuilding", duplicate, 1, capability);
Capability("ship-second-duplicate-hidden", "shipbuilding", World(technologies: new[] { Tech(1), Tech(1, "orbital_industry", "prototype_warp_drive") }), 1, "spacecraft_construction");
Capability("ship-missing-civ", "shipbuilding", duplicate, 9, "spacecraft_construction");
var bothUnlocks = World(technologies: new[] { Tech(1, "orbital_industry", "prototype_warp_drive", "reliable_ftl", "extended_ftl_range") });
Capability("ship-both-spacecraft-true", "shipbuilding", bothUnlocks, 1, "spacecraft_construction");
Capability("ship-both-experimental-true", "shipbuilding", bothUnlocks, 1, "experimental_interstellar_transit");
Capability("ship-named-reliable-still-false", "shipbuilding", bothUnlocks, 1, "reliable_ftl");
Capability("ship-named-extended-still-false", "shipbuilding", bothUnlocks, 1, "extended_ftl_range");

var sourceOnly = new List<object>();
try { CombatReadinessCalculator.Build(null!, 1); } catch (Exception e) { sourceOnly.Add(new { Operation = "readiness-null-world", Error = new { Type = e.GetType().Name, e.Message }, Boundary = "source-only-null" }); }
try { new PrototypeConstructionCapabilityView().HasCivilizationCapability(null!, 1, "x"); } catch (Exception e) { sourceOnly.Add(new { Operation = "construction-null-world", Error = new { Type = e.GetType().Name, e.Message }, Boundary = "source-only-null" }); }
try { new PrototypeShipbuildingCapabilityView().HasCivilizationCapability(null!, 1, "x"); } catch (Exception e) { sourceOnly.Add(new { Operation = "shipbuilding-null-world", Error = new { Type = e.GetType().Name, e.Message }, Boundary = "source-only-null" }); }

var root = new { Format = "stellar-strategic-input-support-oracle-v1", ReadinessCaseCount = readiness.Count, CapabilityCaseCount = capabilities.Count, ReadinessCases = readiness, CapabilityCases = capabilities, SourceOnlyNull = sourceOnly };
File.WriteAllText(args[0], JsonSerializer.Serialize(root, json) + "\n", new UTF8Encoding(false));
Console.Error.WriteLine($"generated {readiness.Count} readiness cases, {capabilities.Count} capability cases, {sourceOnly.Count} source-only null observations");
