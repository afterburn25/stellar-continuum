using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.AI;
using Game.Simulation.Species;
using Game.Simulation.Construction;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Knowledge;
using Game.Simulation.Combat.Massive;
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1)
  throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions {
  WriteIndented = true, IncludeFields = true,
  NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals
};
var cases = new List<object>();
CivilizationState Civ(int id = 1, bool player = true,
                      CivilizationDevelopmentStage stage =
                          CivilizationDevelopmentStage.WarpCapable,
                      bool expansion = true) =>
    new(id, "Civ " + id, 10, CivilizationArchetype.Scientific,
        new CivilizationTraits(0, 0, 0, 0, 0, 0, false), player, stage,
        ExpansionAllowed: expansion);
StarSystemState Home() => new(10, "Home", new Vector2(3, 4),
                              StarArchetype.Standard, false, false, false,
                              false);
ColonyState
Colony(double pop = 750, string species = "terran_baseline", int id = 1,
       int civilizationId = 1) => new() { Id = id,
                                          CivilizationId = civilizationId,
                                          SystemId = 10,
                                          Name = "Source",
                                          PopulationMillions = pop,
                                          PopulationSpeciesId = species };
JsonElement Clone(object? x) => JsonSerializer.SerializeToElement(x, options);
void Seed(string name, IReadOnlyList<StarSystemState> systems,
          IList<CivilizationState> civs, IList<ColonyState>? colonies) {
  var before = Clone(
      new { Systems = systems, Civilizations = civs, Colonies = colonies });
  object? result = null, error = null;
  try {
    result = new FleetSeeder().Seed(systems, civs, colonies);
  } catch (Exception e) {
    error = new { Type = e.GetType().Name, e.Message };
  }
  cases.Add(new { Name = name, Kind = "Seed", Arguments = before,
                  Before = before, Result = Clone(result), Error = error,
                  After = Clone(new { Systems = systems, Civilizations = civs,
                                      Colonies = colonies }) });
}
Seed("seed-warp-player-null-pop", new[] { Home() },
     new List<CivilizationState> { Civ() }, null);
Seed("seed-warp-player-empty-pop", new[] { Home() },
     new List<CivilizationState> { Civ() }, new List<ColonyState>());
Seed("seed-player-exact-threshold", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(750) });
Seed("seed-below-threshold", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(749.999) });
Seed("seed-ai-plus-pop", new[] { Home() },
     new List<CivilizationState> { Civ(player: false) },
     new List<ColonyState> { Colony(751) });
Seed("seed-prewarp", new[] { Home() },
     new List<CivilizationState> {
       Civ(stage: CivilizationDevelopmentStage.PreWarp)
     },
     new List<ColonyState> { Colony(1000) });
Seed("seed-ancient", new[] { Home() },
     new List<CivilizationState> {
       Civ(stage: CivilizationDevelopmentStage.AncientSpacefaring)
     },
     new List<ColonyState> { Colony(1000) });
Seed("seed-expansion-disabled", new[] { Home() },
     new List<CivilizationState> { Civ(expansion: false) },
     new List<ColonyState> { Colony(1000) });
Seed("seed-unknown-species-error", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(1000, "unknown") });
Seed("seed-tied-owned-first-source", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(750, "terran_baseline", 1),
                             Colony(750, "pelagic_high_pressure", 2) });
Seed("seed-first-nan-then-finite", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(double.NaN, "terran_baseline", 1),
                             Colony(750, "pelagic_high_pressure", 2) });
Seed("seed-sole-nan", new[] { Home() }, new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(double.NaN) });
Seed("seed-sole-infinity", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(double.PositiveInfinity) });
Seed("seed-foreign-larger-ignored", new[] { Home() },
     new List<CivilizationState> { Civ() },
     new List<ColonyState> { Colony(10000, "terran_baseline", 2, 2),
                             Colony(750) });
Seed("seed-missing-home", Array.Empty<StarSystemState>(),
     new List<CivilizationState> { Civ() }, null);
GalaxyState Galaxy(CivilizationState civilization,
                   IList<FleetState>? fleets = null,
                   IList<ColonyState>? colonies = null, bool home = true) =>
    new() { Seed = 1,
            Systems = home ? new[] { Home() } : Array.Empty<StarSystemState>(),
            PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
            Civilizations = new List<CivilizationState> { civilization },
            Fleets = fleets ?? new List<FleetState>(),
            Colonies = colonies ?? new List<ColonyState>(),
            Economies = Array.Empty<CivilizationEconomyState>(),
            Technologies = new List<Game.Simulation.Research.TechnologyState>(),
            ConstructionStates = new List<ConstructionState>(),
            ShipyardStates = new List<ShipyardState>(),
            PlayerCivilizationId = civilization.Id,
            Knowledge = new CivilizationKnowledgeState() };
FleetState Existing(int id, bool active, FleetRole role) => new() {
  Id = id,
  CivilizationId = 1,
  Name = "Existing",
  Role = role,
  Position = new(8, 9),
  IsActive = active,
  TacticalLoadout =
      new MassiveCombatLoadout { Weapons = new() { new() { Id = "history" } } },
  TacticalVessel = new MassiveVesselState { Id = 88, Name = "History",
                                            DesignId = "old",
                                            BattlesFought = 2 }
};
void Ensure(string name, GalaxyState galaxy, int civilizationId,
            string nativeIdentityBoundary = "") {
  var before = Clone(galaxy);
  object? result = null, error = null;
  try {
    new FleetSeeder().EnsureStarterFleets(galaxy, civilizationId);
    result = galaxy.Fleets;
  } catch (Exception e) {
    error = new { Type = e.GetType().Name, e.Message };
  }
  cases.Add(
      new { Name = name, Kind = "Ensure",
            Arguments = new { Galaxy = before, CivilizationId = civilizationId,
                              NativeIdentityBoundary = nativeIdentityBoundary },
            Before = before, Result = Clone(result), Error = error,
            After = Clone(galaxy) });
}
Ensure("ensure-missing-civilization", Galaxy(Civ()), 9);
Ensure("ensure-active-scout-missing-home",
       Galaxy(Civ(),
              new List<FleetState> { Existing(4, true, FleetRole.Scout) },
              home: false),
       1);
Ensure("ensure-inactive-scout-next-id",
       Galaxy(Civ(),
              new List<FleetState> { Existing(41, false, FleetRole.Scout) }),
       1);
Ensure("ensure-foreign-active-scout-does-not-suppress",
       Galaxy(Civ(), new List<FleetState> { new() {
                Id = 8, CivilizationId = 2, Name = "Foreign",
                Role = FleetRole.Scout, Position = new(0, 0), IsActive = true
              } }),
       1);
var existingColony = Existing(4, true, FleetRole.Colony);
existingColony.EmbarkedPopulationMillions = 250;
existingColony.EmbarkedPopulationSpeciesId = "terran_baseline";
Ensure("ensure-colony-existing-adds-duplicate-colony",
       Galaxy(Civ(), new List<FleetState> { existingColony },
              new List<ColonyState> { Colony(1000) }),
       1);
Ensure("ensure-prewarp-allowed",
       Galaxy(Civ(stage: CivilizationDevelopmentStage.PreWarp), null,
              new List<ColonyState> { Colony(1000) }),
       1);
Ensure("ensure-unknown-species-after-scout",
       Galaxy(Civ(), null, new List<ColonyState> { Colony(1000, "unknown") }),
       1);
Ensure("ensure-missing-home-no-scout", Galaxy(Civ(), null, null, home: false),
       1);
Ensure("ensure-max-id-source-wrap-native-boundary",
       Galaxy(Civ(), new List<FleetState> { Existing(int.MaxValue, false,
                                                     FleetRole.Science) }),
       1, nativeIdentityBoundary: "before-scout");
Ensure("ensure-max-minus-one-scout-without-colony",
       Galaxy(Civ(), new List<FleetState> { Existing(int.MaxValue - 1, false,
                                                     FleetRole.Science) }),
       1);
Ensure("ensure-max-minus-one-colony-native-boundary",
       Galaxy(Civ(),
              new List<FleetState> { Existing(int.MaxValue - 1, false,
                                              FleetRole.Science) },
              new List<ColonyState> { Colony(1000) }),
       1, nativeIdentityBoundary: "after-scout");
Seed("seed-mixed-civilization-source-order", new[] { Home() },
     new List<CivilizationState> {
       Civ(1), Civ(2, player: false),
       Civ(3, stage: CivilizationDevelopmentStage.PreWarp)
     },
     new List<ColonyState> { Colony(750, id: 1, civilizationId: 1),
                             Colony(750, "pelagic_high_pressure", id: 2,
                                    civilizationId: 2) });
File.WriteAllText(args[0], JsonSerializer.Serialize(
                               new { Format = "stellar-fleet-seeding-oracle-v1",
                                     Cases = cases },
                               options) +
                               Environment.NewLine);
