using System.Globalization;
using System.Numerics;
using System.Text.Json;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");

var json = new JsonSerializerOptions { WriteIndented = true };
var cases = new List<object>();
var capabilities = new Dictionary<int, HashSet<string>>();
var simulation = new ShipbuildingSimulation(new FixtureCapabilities(capabilities));

GalaxyState World() => new() {
    Seed = 1, Systems = Array.Empty<StarSystemState>(), PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
    Civilizations = new List<CivilizationState>(), Fleets = new List<FleetState>(), Colonies = new List<ColonyState>(),
    Economies = Array.Empty<CivilizationEconomyState>(), Technologies = new List<Game.Simulation.Research.TechnologyState>(),
    ConstructionStates = new List<ConstructionState> { new() { CivilizationId = 1 }, new() { CivilizationId = 2 } },
    ShipyardStates = new List<ShipyardState>(), PlayerCivilizationId = 1,
    Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState()
};
JsonElement Clone(object value) => JsonSerializer.SerializeToElement(value, json);
object Snapshot(GalaxyState world) => new {
    ConstructionStates = world.ConstructionStates,
    Capabilities = capabilities.OrderBy(pair => pair.Key).Select(pair => new {
        CivilizationId = pair.Key, CapabilityIds = pair.Value.Order().ToArray()
    }).ToArray()
};
void Add(string name, string kind, GalaxyState world, object arguments, Func<object?> operation) {
    var before = Clone(Snapshot(world)); object? result = null; object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Before = before, Result = result,
        Error = error, After = Clone(Snapshot(world)) });
}
FleetState Fleet(string? designId, FleetRole role) => new() {
    Id = 7, CivilizationId = 1, Name = "Fixture", DesignId = designId, Role = role, Position = Vector2.Zero
};
void Resolve(string name, string? designId, FleetRole role) => Add(name, "Resolve", World(),
    new { DesignId = designId, Role = (int)role }, () => ShipDesignRegistry.GetForFleet(Fleet(designId, role)));
void Lock(string name, GalaxyState world, ShipDesignDefinition design) => Add(name, "Lock", world,
    new { CivilizationId = 1, Design = design }, () => simulation.GetLockReason(world, 1, design));
void Available(string name, GalaxyState world) => Add(name, "Available", world, new { CivilizationId = 1 },
    () => simulation.GetAvailableDesigns(world, 1));
void Propulsion(string name, GalaxyState world, int civilizationId) => Add(name, "Propulsion", world,
    new { CivilizationId = civilizationId, DesignId = "warp_scout" }, () =>
        simulation.GetEffectivePropulsion(world, civilizationId, ShipDesignRegistry.Get("warp_scout")));

cases.Add(new { Name = "catalog-all-authored-fields", Kind = "Catalog", Catalog = ShipDesignRegistry.All });
Resolve("resolver-null-scout", null, FleetRole.Scout);
Resolve("resolver-empty-science", "", FleetRole.Science);
Resolve("resolver-invalid-colony", "missing", FleetRole.Colony);
Resolve("resolver-case-mismatch-military", "PATROL_CORVETTE", FleetRole.Military);
Resolve("resolver-persisted-role-mismatch", "resource_outpost_ship", FleetRole.Logistics);
Resolve("resolver-persisted-outpost", "resource_outpost_ship", FleetRole.Colony);
Resolve("resolver-baseline-scout", null, FleetRole.Scout); Resolve("resolver-baseline-science", null, FleetRole.Science);
Resolve("resolver-baseline-colony", null, FleetRole.Colony); Resolve("resolver-baseline-military", null, FleetRole.Military);
Resolve("resolver-baseline-logistics", null, FleetRole.Logistics);
Add("resolver-invalid-role", "Resolve", World(), new { DesignId = (string?)null, Role = 99 }, () =>
    ShipDesignRegistry.GetForFleet(Fleet(null, (FleetRole)99)));

var world = World();
Lock("lock-all-baseline-requirements", world, ShipDesignRegistry.Get("warp_scout"));
world = World(); world.ConstructionStates.Clear();
Lock("lock-missing-construction-state", world, ShipDesignRegistry.Get("warp_scout"));
Available("available-missing-construction-state", world);
world = World();
capabilities[1] = new() { ShipbuildingCapabilityIds.SpacecraftConstruction, ShipbuildingCapabilityIds.ExperimentalInterstellarTransit };
Lock("lock-project-after-capabilities", world, ShipDesignRegistry.Get("warp_scout"));
world.ConstructionStates[0].CompletedProjectIds.Add("orbital_shipyard");
Lock("lock-unlocked", world, ShipDesignRegistry.Get("warp_scout"));
var custom = new ShipDesignDefinition("custom", "Custom", "Custom prerequisites", FleetRole.Scout, 1, 1, 1, 1, 1,
    new(new[] { "alpha", "beta" }, new[] { "gamma", "delta" }, new[] { "orbital_shipyard" }));
world = World(); Lock("lock-custom-all-project-any", world, custom);
capabilities[1] = new() { "alpha", "beta" }; Lock("lock-custom-project-any", world, custom);
world.ConstructionStates[0].CompletedProjectIds.Add("orbital_shipyard"); Lock("lock-custom-any", world, custom);
capabilities[1].Add("delta"); Lock("lock-custom-unlocked", world, custom);
var otherProject = custom with { Prerequisites = new(Array.Empty<string>(), Array.Empty<string>(), new[] { "industrial_automation" }) };
world = World(); Lock("lock-custom-other-project", world, otherProject);
var unknownProject = custom with { Prerequisites = new(Array.Empty<string>(), Array.Empty<string>(), new[] { "unknown_project" }) };
Lock("lock-custom-unknown-project", world, unknownProject);
world = World(); var emptyCustom = custom with { Prerequisites = new(Array.Empty<string>(), Array.Empty<string>(), Array.Empty<string>()) };
Lock("lock-empty-custom-prerequisites", world, emptyCustom);
world = World(); Available("available-locked", world);
capabilities[1] = new() { ShipbuildingCapabilityIds.SpacecraftConstruction, ShipbuildingCapabilityIds.ExperimentalInterstellarTransit };
world.ConstructionStates[0].CompletedProjectIds.Add("orbital_shipyard"); Available("available-registry-order", world);

world = World(); Propulsion("propulsion-prototype", world, 1);
capabilities[1] = new() { ShipbuildingCapabilityIds.ReliableInterstellarTransit }; Propulsion("propulsion-reliable", world, 1);
capabilities[1] = new() { ShipbuildingCapabilityIds.ExtendedInterstellarTransit }; Propulsion("propulsion-extended", world, 1);
capabilities[1] = new() { ShipbuildingCapabilityIds.ReliableInterstellarTransit, ShipbuildingCapabilityIds.ExtendedInterstellarTransit }; Propulsion("propulsion-extended-precedes-reliable", world, 1);
capabilities[2] = new() { ShipbuildingCapabilityIds.ExtendedInterstellarTransit }; capabilities.Remove(1); Propulsion("propulsion-cross-civilization", world, 1);
foreach (var capability in new[] { "spacecraft_construction", "experimental_interstellar_transit", "reliable_ftl", "extended_ftl_range", "unknown_capability_value" })
    Add("display-" + capability, "Display", World(), new { CapabilityId = capability }, () => ShipbuildingCapabilityIds.DisplayName(capability));

File.WriteAllText(args[0], JsonSerializer.Serialize(new { Format = "stellar-ship-designs-oracle-v1", Cases = cases }, json) + Environment.NewLine);

sealed class FixtureCapabilities(Dictionary<int, HashSet<string>> values) : IShipbuildingCapabilityView {
    public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) =>
        values.TryGetValue(civilizationId, out var known) && known.Contains(capabilityId);
}
