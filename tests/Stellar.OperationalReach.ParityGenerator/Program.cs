using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Units;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");

var options = new JsonSerializerOptions
{
    WriteIndented = true,
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

JsonElement Clone<T>(T value) => JsonSerializer.SerializeToElement(value, options);
object FixtureNumber(double value) => value == 0.0 && double.IsNegative(value) ? "-0" : value;
void Add(string name, string kind, object arguments, Func<object?> operation, string nativeBoundary = "")
{
    object? result = null;
    object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Result = result, Error = error, NativeBoundary = nativeBoundary });
}

StarSystemState System(int id, float x, float y = 0, double? depth = null, string? name = null) =>
    new(id, name ?? "S" + id, new(x, y), StarArchetype.Standard, false, false, false, false,
        GalacticDepthLightYears: depth);
var line = Enumerable.Range(1, 6).Select(id => System(id, (id - 1) * 10)).ToArray();

ColonyState Colony(int id, int civilizationId, int systemId, SettlementKind kind) => new()
{
    Id = id,
    CivilizationId = civilizationId,
    SystemId = systemId,
    Name = "C" + id,
    Kind = kind,
    PopulationMillions = 1,
};

FleetState Fleet(int civilizationId = 1, int? currentSystemId = 1,
                 double maximumLegRange = 10, double fuelCapacity = 30) => new()
                 {
                     Id = 17,
                     CivilizationId = civilizationId,
                     Name = "Reach fixture",
                     Role = FleetRole.Scout,
                     DesignId = "scout_pathfinder_v1",
                     Position = new(2, 3),
                     CurrentSystemId = currentSystemId,
                     DestinationSystemId = 6,
                     TransitPhase = FleetTransitPhase.None,
                     TransitOriginSystemId = 5,
                     TransitTargetSystemId = 6,
                     TransitProgress = .25,
                     LocalTransitStart = new(1, 2),
                     LocalTransitPosition = new(2, 3),
                     LocalTransitTarget = new(4, 5),
                     PlannedRouteSystemIds = new() { 5, 6 },
                     HoldRequested = true,
                     ReturnToBaseRequested = true,
                     ReturnToBaseFailureReason = "old failure",
                     MissionOrderRevision = 7,
                     DestinationPlanetaryBodyId = 11,
                     PreventAutomaticSettlement = true,
                     SettlementBodyId = 12,
                     SettlementDaysCompleted = 4,
                     ReconnaissanceSystemId = 3,
                     ReconnaissanceDaysCompleted = 5,
                     FreightTargetOutpostId = 13,
                     FreightHomeColonyId = 14,
                     CargoMaterialCapacity = 20,
                     CargoMaterials = 7,
                     StrategicSpeed = 22,
                     MaximumLegRangeLightYears = maximumLegRange,
                     FuelCapacityLightYears = fuelCapacity,
                     FuelRemainingLightYears = 30,
                     SensorRange = 135,
                     IsActive = true,
                     EmbarkedPopulationMillions = 2,
                     EmbarkedPopulationSpeciesId = "terran_baseline",
                     Combat = new FleetCombatState { ProfileId = "civilian_light_v1", Hull = 2 },
                     TacticalLoadout = new MassiveCombatLoadout { Weapons = new() { new() { Id = "history" } } },
                     TacticalVessel = new MassiveVesselState { Id = 9, Name = "History", DesignId = "fixture", BattlesFought = 2 },
                 };

GalaxyState Galaxy(IReadOnlyList<StarSystemState> systems, IReadOnlyList<ColonyState>? colonies = null) => new()
{
    Seed = 1,
    Systems = systems,
    PlanetaryBodies = Array.Empty<PlanetaryBodyState>(),
    Civilizations = new List<CivilizationState>(),
    Fleets = new List<FleetState>(),
    Colonies = colonies?.ToList() ?? new List<ColonyState>(),
    Economies = Array.Empty<CivilizationEconomyState>(),
    Technologies = new List<Game.Simulation.Research.TechnologyState>(),
    ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(),
    ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(),
    PlayerCivilizationId = 1,
    Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState(),
};

Add("factory-supported-default", "FactorySupported", new { Reason = (string?)null },
    () => MissionReachAssessment.Supported());
Add("factory-supported-custom", "FactorySupported", new { Reason = "Ready." },
    () => MissionReachAssessment.Supported("Ready."));
foreach (var reason in new string?[] { null, "", "   ", "\u0085", "\u00a0", "\u1680", "\u2003",
                                      "\u2028", "\u2029", "\u202f", "\u205f", "\u3000", "Blocked." })
    Add("factory-unsupported-" + cases.Count, "FactoryUnsupported", new { Reason = reason },
        () => MissionReachAssessment.Unsupported(reason!));
Add("factory-provisional", "FactoryProvisional", new { Reason = "Pending logistics." },
    () => MissionReachAssessment.ProvisionalSupported("Pending logistics."));

double LyForKm(double kilometres) => kilometres / InterstellarDistanceUnits.KilometresPerLightYear;
foreach (var value in new[] { -1d, -0d, double.NaN, double.PositiveInfinity, double.NegativeInfinity, 0d,
                              LyForKm(Math.BitDecrement(.5)), LyForKm(.5), LyForKm(Math.BitIncrement(.5)),
                              LyForKm(2.5), LyForKm(3.5), LyForKm(999998.5),
                              1e6 / InterstellarDistanceUnits.KilometresPerLightYear * .999999,
                              1e6 / InterstellarDistanceUnits.KilometresPerLightYear,
                              999999.6 / InterstellarDistanceUnits.KilometresPerLightYear,
                              1d, 12.34d, 1e100, double.MaxValue })
    Add("format-primary-" + cases.Count, "FormatPrimary", new { Value = FixtureNumber(value) },
        () => InterstellarDistanceUnits.FormatMetricPrimary(value),
        double.IsFinite(value) && value * InterstellarDistanceUnits.KilometresPerLightYear == double.PositiveInfinity
            ? "conversion-overflow" : "");
foreach (var value in new[] { -0.1d, double.NaN, double.PositiveInfinity, 0d, .25d, 99.95d })
    Add("format-speed-" + cases.Count, "FormatSpeed", new { Value = FixtureNumber(value) },
        () => InterstellarDistanceUnits.FormatMetricSpeed(value));

void Assess(string name, FleetState fleet, int target, IReadOnlyList<StarSystemState>? systems = null,
            IReadOnlyList<ColonyState>? colonies = null, int civilizationId = 1,
            InterstellarMissionKind mission = InterstellarMissionKind.ScoutReconnaissance)
{
    systems ??= line;
    var galaxy = Galaxy(systems, colonies);
    var before = Clone(fleet);
    var beforeSystems = Clone(systems);
    var beforeColonies = Clone(galaxy.Colonies);
    object? result = null;
    object? error = null;
    try { result = new LaneInterstellarOperationalReachView().Assess(galaxy, civilizationId, fleet, target, mission); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new
    {
        Name = name,
        Kind = "Assess",
        Arguments = new
        {
            Systems = systems,
            Colonies = galaxy.Colonies,
            CivilizationId = civilizationId,
            Fleet = before,
            TargetSystemId = target,
            MissionKind = (int)mission
        },
        Before = before,
        BeforeSystems = beforeSystems,
        BeforeColonies = beforeColonies,
        Result = result,
        Error = error,
        After = Clone(fleet),
        AfterSystems = Clone(systems),
        AfterColonies = Clone(galaxy.Colonies),
        NativeBoundary = ""
    });
}

var fleet = Fleet(civilizationId: 2, currentSystemId: null);
Assess("assess-ownership-first", fleet, 99, civilizationId: 1);
Assess("assess-unknown-target", Fleet(currentSystemId: null), 99);
Assess("assess-missing-current", Fleet(currentSystemId: null), 2);
fleet = Fleet(currentSystemId: 99); Assess("assess-invalid-origin-throws", fleet, 2);
fleet = Fleet(maximumLegRange: 0); Assess("assess-zero-range", fleet, 2);
fleet = Fleet(maximumLegRange: 9); Assess("assess-unreachable-range", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 0; Assess("assess-same-system-zero-fuel", fleet, 1);
fleet = Fleet(); fleet.FuelRemainingLightYears = 9.999999; Assess("assess-short-first-leg", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 10 - 2e-9; Assess("assess-fuel-outside-tolerance", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 10 - .5e-9; Assess("assess-fuel-inside-tolerance", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 10; Assess("assess-exact-first-leg", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 20; Assess("assess-exact-two-legs", fleet, 3);
fleet = Fleet(); fleet.FuelRemainingLightYears = 0; Assess("assess-origin-full-service", fleet, 3,
    colonies: new[] { Colony(1, 1, 1, SettlementKind.Colony) });
fleet = Fleet(); fleet.FuelRemainingLightYears = 0; Assess("assess-origin-half-service", fleet, 2,
    colonies: new[] { Colony(1, 1, 1, SettlementKind.ResourceOutpost) });
fleet = Fleet(); fleet.FuelRemainingLightYears = 10; Assess("assess-intermediate-full-refuel", fleet, 4,
    colonies: new[] { Colony(1, 1, 2, SettlementKind.Colony) });
fleet = Fleet(fuelCapacity: 15); fleet.FuelRemainingLightYears = 10;
Assess("assess-intermediate-half-refuel-fails", fleet, 3,
    colonies: new[] { Colony(1, 1, 2, SettlementKind.ResourceOutpost) });
fleet = Fleet(); fleet.FuelRemainingLightYears = 10; Assess("assess-foreign-colony-ignored", fleet, 3,
    colonies: new[] { Colony(1, 2, 2, SettlementKind.Colony) });
fleet = Fleet(fuelCapacity: 30); fleet.FuelRemainingLightYears = 10;
Assess("assess-multiple-outposts-still-half", fleet, 4, colonies: new[] {
    Colony(1, 1, 2, SettlementKind.ResourceOutpost), Colony(2, 1, 2, SettlementKind.ResourceOutpost) });
fleet = Fleet(); fleet.FuelRemainingLightYears = 10; Assess("assess-colony-beats-outpost", fleet, 4,
    colonies: new[] { Colony(1, 1, 2, SettlementKind.ResourceOutpost), Colony(2, 1, 2, SettlementKind.Colony) });
foreach (var mission in Enum.GetValues<InterstellarMissionKind>())
{
    fleet = Fleet(); fleet.FuelRemainingLightYears = 10;
    Assess("assess-mission-kind-" + (int)mission, fleet, 2, mission: mission);
}
fleet = Fleet(maximumLegRange: double.NaN); Assess("assess-nan-range", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = double.NaN; Assess("assess-nan-fuel", fleet, 2);
fleet = Fleet(); fleet.FuelRemainingLightYears = 10;
Assess("assess-depth-route-distance", fleet, 3, new[] { System(1, 0, depth: 0), System(2, 6, depth: 8), System(3, 12, depth: 16) });

void Assign(string name, FleetState fleet, int destination, MissionReachAssessment reach,
            IReadOnlyList<StarSystemState>? systems = null, string nativeBoundary = "")
{
    systems ??= line;
    var before = Clone(fleet);
    var beforeSystems = Clone(systems);
    object? error = null;
    try { FleetRouteOrders.Assign(Galaxy(systems), fleet, destination, reach); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new
    {
        Name = name,
        Kind = "Assign",
        Arguments = new
        {
            Systems = systems,
            FinalDestinationSystemId = destination,
            Reach = reach
        },
        Before = before,
        Result = (object?)null,
        BeforeSystems = beforeSystems,
        Error = error,
        After = Clone(fleet),
        AfterSystems = Clone(systems),
        NativeBoundary = nativeBoundary
    });
}

fleet = Fleet(); Assign("assign-unsupported", fleet, 4, MissionReachAssessment.Unsupported("No."));
fleet = Fleet(); Assign("assign-provided-route-removes-current", fleet, 4,
    new(true, true, "ok", new[] { 1, 2, 1, 4 }, 30));
fleet = Fleet(); Assign("assign-null-route-graph", fleet, 4, MissionReachAssessment.Supported());
fleet = Fleet(); Assign("assign-null-route-same-system", fleet, 1, MissionReachAssessment.Supported());
fleet = Fleet(); Assign("assign-empty-route-fallback", fleet, 4, new(true, true, "ok", Array.Empty<int>(), 0));
fleet = Fleet(currentSystemId: null); Assign("assign-no-current-null-route", fleet, 4, MissionReachAssessment.Supported());
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.LocalDeparture;
Assign("assign-reroute-departure", fleet, 3, new(true, true, "ok", new[] { 1, 2, 3 }, 20));
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.LocalArrival;
Assign("assign-reroute-arrival", fleet, 3, new(true, true, "ok", new[] { 1, 2, 3 }, 20));
fleet = Fleet(currentSystemId: 99); fleet.TransitPhase = FleetTransitPhase.LocalDeparture;
Assign("assign-missing-current-record", fleet, 3, new(true, true, "ok", new[] { 99, 2, 3 }, 20));
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.LocalDeparture;
Assign("assign-missing-next-record", fleet, 99, new(true, true, "ok", new[] { 1, 99 }, 20));
fleet = Fleet(); fleet.MissionOrderRevision = int.MaxValue;
Assign("assign-native-revision-exhaustion", fleet, 2, new(true, true, "ok", new[] { 1, 2 }, 10), nativeBoundary: "revision-exhaustion");
fleet = Fleet(); fleet.MissionOrderRevision = int.MaxValue;
Assign("assign-unsupported-precedes-revision-exhaustion", fleet, 2, MissionReachAssessment.Unsupported("No."));
fleet = Fleet(currentSystemId: 99); fleet.MissionOrderRevision = int.MaxValue;
Assign("assign-route-error-precedes-revision-exhaustion", fleet, 2, MissionReachAssessment.Supported());

void Clear(string name, FleetState fleet, string nativeBoundary = "")
{
    var before = Clone(fleet);
    object? error = null;
    try { FleetRouteOrders.Clear(fleet); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new
    {
        Name = name,
        Kind = "Clear",
        Arguments = new { },
        Before = before,
        Result = (object?)null,
        Error = error,
        After = Clone(fleet),
        NativeBoundary = nativeBoundary
    });
}

fleet = Fleet(); Clear("clear-idle-current", fleet);
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.LocalDeparture; Clear("clear-local-departure", fleet);
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.LocalArrival; Clear("clear-local-arrival", fleet);
fleet = Fleet(); fleet.TransitPhase = FleetTransitPhase.InterstellarWarp; Clear("clear-warp-with-current", fleet);
fleet = Fleet(currentSystemId: null); fleet.TransitPhase = FleetTransitPhase.InterstellarWarp; Clear("clear-warp-no-current", fleet);
fleet = Fleet(currentSystemId: null); fleet.TransitPhase = FleetTransitPhase.LocalDeparture; Clear("clear-local-no-current", fleet);
fleet = Fleet(currentSystemId: null); fleet.TransitPhase = (FleetTransitPhase)99; Clear("clear-unknown-phase-no-current", fleet);
fleet = Fleet(); fleet.MissionOrderRevision = int.MaxValue; Clear("clear-native-revision-exhaustion", fleet, "revision-exhaustion");

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-operational-reach-oracle-v1",
    Cases = cases,
}, options) + Environment.NewLine);
