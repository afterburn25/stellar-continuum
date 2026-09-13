using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Exploration;
using Game.Simulation.Models;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");

var options = new JsonSerializerOptions {
    WriteIndented = true,
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

FleetState Fleet(double speed) => new() {
    Id = 1, CivilizationId = 1, Name = "Transit fixture", Role = FleetRole.Scout,
    Position = Vector2.Zero, StrategicSpeed = speed,
};
void Add(string name, string kind, object arguments, Func<object?> operation) {
    object? result = null;
    object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Result = result, Error = error });
}
FleetState FullFleet(double speed = 22) {
    var fleet = Fleet(speed);
    fleet.CargoMaterials = 7; fleet.EmbarkedPopulationMillions = 8; fleet.EmbarkedPopulationSpeciesId = "terran_baseline";
    fleet.PlannedRouteSystemIds = new() { 4, 9 }; fleet.HoldRequested = true; fleet.ReturnToBaseRequested = true;
    fleet.ReturnToBaseFailureReason = "blocked"; fleet.MissionOrderRevision = 3; fleet.SettlementDaysCompleted = 4;
    fleet.ReconnaissanceDaysCompleted = 5; fleet.Combat = new FleetCombatState { ProfileId = "civilian_light_v1", Hull = 2 };
    fleet.TacticalLoadout = new MassiveCombatLoadout { Weapons = new() { new() { Id = "history" } } };
    fleet.TacticalVessel = new MassiveVesselState { Id = 9, Name = "History", DesignId = "fixture", BattlesFought = 2 };
    return fleet;
}
JsonElement Clone(FleetState fleet) => JsonSerializer.SerializeToElement(fleet, options);
void AddFleet(string name, string kind, FleetState fleet, object arguments, Func<object?> operation) {
    var before = Clone(fleet); object? result = null; object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Before = before, Result = result, Error = error, After = Clone(fleet) });
}
foreach (var speed in new[] { 22d, 24d, 0d, -1d, double.NaN, double.PositiveInfinity, double.NegativeInfinity, 1e308 })
    Add("rate-" + cases.Count, "Rate", new { StrategicSpeed = speed }, () => FleetLocalTransit.Rate(Fleet(speed)));
foreach (var (source, current) in new[] {
    (new Vector2(1, 0), Vector2.Zero), (new Vector2(3, 4), Vector2.Zero), (Vector2.Zero, Vector2.Zero),
    (new Vector2(.0005f, 0), Vector2.Zero), (new Vector2(.0011f, 0), Vector2.Zero),
    (new Vector2(-3, -4), new Vector2(-1, -1)),
    (new Vector2(float.MaxValue, float.MaxValue), new Vector2(-float.MaxValue, -float.MaxValue)),
    (new Vector2(float.NaN, 1), Vector2.Zero),
})
    Add("gate-" + cases.Count, "GateTowards", new { Source = source, Current = current }, () => FleetLocalTransit.GateTowards(source, current));

foreach (var (phase, start, target) in new[] {
    (FleetTransitPhase.LocalDeparture, new Vector2(1, 2), new Vector2(3, 4)),
    (FleetTransitPhase.LocalArrival, new Vector2(1, 2), new Vector2(3, 4)),
    ((FleetTransitPhase)99, new Vector2(1, 2), new Vector2(3, 4)),
    (FleetTransitPhase.LocalDeparture, new Vector2(float.NaN, 2), new Vector2(3, 4)),
    (FleetTransitPhase.LocalDeparture, new Vector2(1, 2), new Vector2(float.PositiveInfinity, 4)),
    (FleetTransitPhase.LocalArrival, new Vector2(float.NaN, float.NaN), new Vector2(float.NegativeInfinity, float.NaN)),
}) {
    var fleet = FullFleet();
    AddFleet("begin-" + cases.Count, "Begin", fleet, new { Phase = (int)phase, Start = start, Target = target }, () => { FleetLocalTransit.Begin(fleet, phase, start, target); return null; });
}
void Advance(string name, double days, Action<FleetState> arrange, double speed = 22) {
    var fleet = FullFleet(speed); arrange(fleet);
    AddFleet(name, "Advance", fleet, new { Days = days }, () => FleetLocalTransit.Advance(fleet, days));
}
Advance("advance-partial", .5, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = Vector2.Zero; f.LocalTransitTarget = new(10, 0); });
Advance("advance-exact-finish", 10 / FleetLocalTransit.Rate(Fleet(22)), f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = Vector2.Zero; f.LocalTransitTarget = new(10, 0); });
Advance("advance-overshoot", 100, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = Vector2.Zero; f.LocalTransitTarget = new(1, 0); });
Advance("advance-already-target", 1, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = new(1, 0); f.LocalTransitTarget = new(1, 0); });
Advance("advance-zero", 0, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitTarget = new(1, 0); });
Advance("advance-negative", -1, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitTarget = new(1, 0); });
Advance("advance-nan-days", double.NaN, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitTarget = new(1, 0); });
Advance("advance-infinite-days", double.PositiveInfinity, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitTarget = new(1, 0); });
Advance("advance-nonfinite-position", 1, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = new(float.NaN, 0); f.LocalTransitTarget = new(1, 0); });
Advance("advance-nonfinite-start-target", 1, f => { f.LocalTransitStart = new(float.NaN, 0); f.LocalTransitPosition = new(1, 0); f.LocalTransitTarget = new(float.PositiveInfinity, 0); });
Advance("advance-degenerate-start-target", 1, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = new(1, 0); f.LocalTransitTarget = Vector2.Zero; });
Advance("advance-nan-speed", 1, f => { f.LocalTransitStart = Vector2.Zero; f.LocalTransitPosition = Vector2.Zero; f.LocalTransitTarget = new(1, 0); }, double.NaN);
void Query(string name, string kind, FleetState fleet, Func<object?> operation) {
    var before = Clone(fleet); object? result = null; object? error = null;
    try { result = operation(); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = before, Before = before, Result = result, Error = error, After = Clone(fleet) });
}
FleetState Local(FleetTransitPhase phase, Vector2 position, Vector2 target, double speed = 22) {
    var fleet = FullFleet(speed); fleet.TransitPhase = phase; fleet.LocalTransitPosition = position; fleet.LocalTransitTarget = target; return fleet;
}
var completeExact = Local(FleetTransitPhase.LocalDeparture, new(1, 2), new(1, 2)); Query("complete-exact-target", "Complete", completeExact, () => FleetLocalTransit.Complete(completeExact));
var completeInside = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(.00009f, 0)); Query("complete-inside-threshold", "Complete", completeInside, () => FleetLocalTransit.Complete(completeInside));
var completeOutside = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(.00011f, 0)); Query("complete-outside-threshold", "Complete", completeOutside, () => FleetLocalTransit.Complete(completeOutside));
var completeFar = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(1, 0)); Query("complete-far", "Complete", completeFar, () => FleetLocalTransit.Complete(completeFar));
var completeNan = Local(FleetTransitPhase.LocalDeparture, new(float.NaN, 0), Vector2.Zero); Query("complete-nan-position", "Complete", completeNan, () => FleetLocalTransit.Complete(completeNan));
var completeInf = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(float.PositiveInfinity, 0)); Query("complete-infinite-target", "Complete", completeInf, () => FleetLocalTransit.Complete(completeInf));
var departure = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(3, 4)); Query("remaining-departure", "RemainingDays", departure, () => FleetLocalTransit.RemainingDays(departure));
var arrival = Local(FleetTransitPhase.LocalArrival, Vector2.Zero, new(3, 4)); Query("remaining-arrival", "RemainingDays", arrival, () => FleetLocalTransit.RemainingDays(arrival));
foreach (var phase in new[] { FleetTransitPhase.None, FleetTransitPhase.InterstellarWarp, (FleetTransitPhase)99 }) { var fleet = Local(phase, Vector2.Zero, new(3, 4)); Query("remaining-phase-" + (int)phase, "RemainingDays", fleet, () => FleetLocalTransit.RemainingDays(fleet)); }
var remainingNanSpeed = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(3, 4), double.NaN); Query("remaining-nan-speed", "RemainingDays", remainingNanSpeed, () => FleetLocalTransit.RemainingDays(remainingNanSpeed));
var remainingInfSpeed = Local(FleetTransitPhase.LocalArrival, Vector2.Zero, new(3, 4), double.PositiveInfinity); Query("remaining-infinite-speed", "RemainingDays", remainingInfSpeed, () => FleetLocalTransit.RemainingDays(remainingInfSpeed));
var remainingNanPosition = Local(FleetTransitPhase.LocalDeparture, new(float.NaN, 0), new(3, 4)); Query("remaining-nan-position", "RemainingDays", remainingNanPosition, () => FleetLocalTransit.RemainingDays(remainingNanPosition));
StarSystemState System(int id, float x, float y, double? depth = null) => new(id, "S" + id, new(x, y), StarArchetype.Standard, false, false, false, false, GalacticDepthLightYears: depth);
GalaxyState ChartGalaxy(IReadOnlyList<StarSystemState> systems) => new() {
    Seed = 1, Systems = systems, PlanetaryBodies = Array.Empty<PlanetaryBodyState>(), Civilizations = new List<CivilizationState>(),
    Fleets = new List<FleetState>(), Colonies = new List<ColonyState>(), Economies = Array.Empty<CivilizationEconomyState>(),
    Technologies = new List<Game.Simulation.Research.TechnologyState>(), ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(),
    ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(), PlayerCivilizationId = 1,
    Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState(),
};
void Chart(string name, FleetState fleet, IReadOnlyList<StarSystemState> systems) {
    var before = Clone(fleet); var arguments = new { Fleet = before, Systems = systems };
    object? result = null; object? error = null;
    try { result = FleetLocalTransit.RemainingChartDistance(ChartGalaxy(systems), fleet); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = "RemainingChartDistance", Arguments = arguments, Before = before, Result = result, Error = error, After = Clone(fleet) });
}
var systems = new[] { System(1, 0, 0), System(2, 10, 0), System(3, 20, 0), System(4, 30, 0) };
var emptyNone = Local(FleetTransitPhase.None, new(1, 0), new(2, 0)); Chart("chart-empty-none", emptyNone, new[] { System(1, 0, 0), System(1, 9, 9) });
var emptyDeparture = Local(FleetTransitPhase.LocalDeparture, Vector2.Zero, new(3, 4)); Chart("chart-empty-departure", emptyDeparture, Array.Empty<StarSystemState>());
var emptyArrival = Local(FleetTransitPhase.LocalArrival, Vector2.Zero, new(3, 4)); Chart("chart-empty-arrival", emptyArrival, Array.Empty<StarSystemState>());
var multi = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); multi.TransitOriginSystemId = 1; multi.PlannedRouteSystemIds = new() { 2, 3, 4 }; Chart("chart-multi-hop", multi, systems);
var fallback = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); fallback.TransitOriginSystemId = 1; fallback.DestinationSystemId = 3; Chart("chart-destination-fallback", fallback, systems);
var arrivalSkip = Local(FleetTransitPhase.LocalArrival, new(1, 1), new(2, 2)); arrivalSkip.CurrentSystemId = 2; arrivalSkip.PlannedRouteSystemIds = new() { 2, 3 }; Chart("chart-arrival-skip-route-head", arrivalSkip, systems);
var noneOutbound = Local(FleetTransitPhase.None, new(1, 0), Vector2.Zero); noneOutbound.CurrentSystemId = 1; noneOutbound.PlannedRouteSystemIds = new() { 2, 3 }; Chart("chart-none-outbound-approach", noneOutbound, systems);
var missingOrigin = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); missingOrigin.TransitOriginSystemId = 99; missingOrigin.PlannedRouteSystemIds = new() { 2, 3 }; Chart("chart-missing-origin", missingOrigin, systems);
var missingIntermediate = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); missingIntermediate.TransitOriginSystemId = 1; missingIntermediate.PlannedRouteSystemIds = new() { 99, 2 }; Chart("chart-missing-intermediate-retains-origin", missingIntermediate, systems);
var duplicateSystems = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); duplicateSystems.TransitOriginSystemId = 1; duplicateSystems.PlannedRouteSystemIds = new() { 2 }; Chart("chart-duplicate-system-ids", duplicateSystems, new[] { System(1, 0, 0), System(1, 1, 0), System(2, 2, 0) });
var missingFollowing = Local(FleetTransitPhase.InterstellarWarp, Vector2.Zero, Vector2.Zero); missingFollowing.TransitOriginSystemId = 1; missingFollowing.PlannedRouteSystemIds = new() { 2, 99 }; Chart("chart-missing-following-endpoint", missingFollowing, systems);
var routeNoCurrent = Local(FleetTransitPhase.None, Vector2.Zero, Vector2.Zero); routeNoCurrent.PlannedRouteSystemIds = new() { 2 }; Chart("chart-none-missing-current", routeNoCurrent, systems);
var interpolationOrigin = System(1, -3.25f, 4.5f); var interpolationTarget = System(2, 17.75f, -9.5f);
foreach (var progress in new[] { 0d, 1d, .37d, -.2d, 1.2d, double.NaN })
    Add("interpolate-" + cases.Count, "InterpolateChartPosition", new { Origin = interpolationOrigin, Target = interpolationTarget, Progress = progress }, () => InterstellarDistance.InterpolateChartPosition(interpolationOrigin, interpolationTarget, progress));
void FromFleet(string name, FleetState fleet, IReadOnlyList<StarSystemState> source, StarSystemState target) {
    var before = Clone(fleet); object? result = null; object? error = null;
    try { result = InterstellarDistance.FromFleet(ChartGalaxy(source), fleet, target); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = "FromFleet", Arguments = new { Fleet = before, Systems = source, Target = target }, Before = before, Result = result, Error = error, After = Clone(fleet) });
}
FleetState DistanceFleet(FleetTransitPhase phase = FleetTransitPhase.None) { var fleet = FullFleet(); fleet.TransitPhase = phase; fleet.Position = new(3, 4); return fleet; }
var target2d = System(9, 13, 4); var origin2d = System(1, 0, 0);
var idle2d = DistanceFleet(); idle2d.CurrentSystemId = 1; FromFleet("from-fleet-idle-2d", idle2d, new[] { origin2d }, target2d);
var depth3d = DistanceFleet(); depth3d.CurrentSystemId = 1; FromFleet("from-fleet-both-depth", depth3d, new[] { System(1, 0, 0, 5) }, System(9, 13, 4, 17));
var mixedDepth = DistanceFleet(); mixedDepth.CurrentSystemId = 1; FromFleet("from-fleet-mixed-depth", mixedDepth, new[] { System(1, 0, 0, 5) }, System(9, 13, 4));
var distanceMissingOrigin = DistanceFleet(); distanceMissingOrigin.CurrentSystemId = 99; FromFleet("from-fleet-missing-origin", distanceMissingOrigin, Array.Empty<StarSystemState>(), System(9, 13, 4, 8));
var waypointFirst = DistanceFleet(FleetTransitPhase.InterstellarWarp); waypointFirst.TransitOriginSystemId = 1; waypointFirst.CurrentSystemId = 2; waypointFirst.PlannedRouteSystemIds = new() { 3, 4 }; waypointFirst.DestinationSystemId = 4; waypointFirst.TransitProgress = .5; FromFleet("from-fleet-warp-first-waypoint", waypointFirst, new[] { System(1, 0, 0, 0), System(2, 0, 0, 99), System(3, 0, 0, 10), System(4, 0, 0, 50) }, System(9, 13, 4, 20));
var fallbackDestination = DistanceFleet(FleetTransitPhase.InterstellarWarp); fallbackDestination.TransitOriginSystemId = 1; fallbackDestination.DestinationSystemId = 3; fallbackDestination.TransitProgress = .5; FromFleet("from-fleet-warp-destination-fallback", fallbackDestination, new[] { System(1, 0, 0, 0), System(3, 0, 0, 10) }, System(9, 13, 4, 20));
var lowProgress = DistanceFleet(FleetTransitPhase.InterstellarWarp); lowProgress.TransitOriginSystemId = 1; lowProgress.DestinationSystemId = 3; lowProgress.TransitProgress = -2; FromFleet("from-fleet-progress-low", lowProgress, new[] { System(1, 0, 0, 0), System(3, 0, 0, 10) }, System(9, 13, 4, 20));
var highProgress = DistanceFleet(FleetTransitPhase.InterstellarWarp); highProgress.TransitOriginSystemId = 1; highProgress.DestinationSystemId = 3; highProgress.TransitProgress = 2; FromFleet("from-fleet-progress-high", highProgress, new[] { System(1, 0, 0, 0), System(3, 0, 0, 10) }, System(9, 13, 4, 20));
var missingWaypoint = DistanceFleet(FleetTransitPhase.InterstellarWarp); missingWaypoint.TransitOriginSystemId = 1; missingWaypoint.PlannedRouteSystemIds = new() { 99 }; missingWaypoint.TransitProgress = .5; FromFleet("from-fleet-missing-waypoint", missingWaypoint, new[] { System(1, 0, 0, 5) }, System(9, 13, 4, 20));
var early2d = DistanceFleet(FleetTransitPhase.InterstellarWarp); early2d.TransitOriginSystemId = 1; early2d.PlannedRouteSystemIds = new() { 3 }; early2d.TransitProgress = .5; FromFleet("from-fleet-both-depth-absent-early-2d", early2d, new[] { System(1, 0, 0), System(3, 0, 0, 99) }, target2d);
var nanProgress = DistanceFleet(FleetTransitPhase.InterstellarWarp); nanProgress.TransitOriginSystemId = 1; nanProgress.DestinationSystemId = 3; nanProgress.TransitProgress = double.NaN; FromFleet("from-fleet-nan-progress", nanProgress, new[] { System(1, 0, 0, 0), System(3, 0, 0, 10) }, System(9, 13, 4, 20));
var originPrecedence = DistanceFleet(FleetTransitPhase.InterstellarWarp); originPrecedence.TransitOriginSystemId = 1; originPrecedence.CurrentSystemId = 2; originPrecedence.DestinationSystemId = 3; FromFleet("from-fleet-transit-origin-precedence", originPrecedence, new[] { System(1, 0, 0, 1), System(2, 0, 0, 99), System(3, 0, 0, 10) }, System(9, 13, 4, 20));

File.WriteAllText(args[0], JsonSerializer.Serialize(new { Format = "stellar-fleet-transit-oracle-v1", Cases = cases }, options) + Environment.NewLine);
