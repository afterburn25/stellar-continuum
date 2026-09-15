using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions { IncludeFields = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
var cases = new List<object>();

StarSystemState S(int id, float x = 0, float y = 0, double? depth = null,
    StarArchetype archetype = StarArchetype.Standard, StellarPrimaryClass? primary = StellarPrimaryClass.GYellowDwarf) =>
    new(id, $"System {id}", new Vector2(x, y), archetype, true, false, false, false,
        null, primary, null, null, depth);
PlanetaryBodyState B(int id, int system, PlanetaryBodyKind kind = PlanetaryBodyKind.Planet,
    double radiation = .1, bool anomaly = false, bool rare = false) =>
    new(id, system, kind == PlanetaryBodyKind.Moon ? id - 1 : null, id, $"Body {id}", kind, 1, 1,
        new(1, 280, 100, PlanetaryAtmosphereRegime.OxygenNitrogen,
            PlanetarySolventRegime.Water, radiation, false, true), false, rare, anomaly, false);
FleetState F(int id = 10, FleetRole role = FleetRole.Scout, bool active = true,
    int civilization = 1, int? current = 0, int? destination = null,
    Vector2? position = null, FleetTransitPhase phase = FleetTransitPhase.None,
    int? origin = null, double progress = 0) => new()
    {
        Id = id,
        CivilizationId = civilization,
        Name = $"Fleet {id}",
        Role = role,
        Position = position ?? Vector2.Zero,
        CurrentSystemId = current,
        DestinationSystemId = destination,
        TransitPhase = phase,
        TransitOriginSystemId = origin,
        TransitProgress = progress,
        StrategicSpeed = 22,
        MaximumLegRangeLightYears = 360,
        FuelCapacityLightYears = 1000,
        FuelRemainingLightYears = 1000,
        SensorRange = 135,
        IsActive = active,
    };
KnowledgeInput K(int system, SystemSurveyLevel level, double progress = 0, int civilization = 1) =>
    new(civilization, system, level, progress);
ReachInput R(int system, bool supported = true, string? reason = null,
    bool authoritative = true, double distance = 0) =>
    new(system, supported, authoritative, reason ?? (supported ? $"Reach {system}." : $"Blocked {system}."),
        supported ? new[] { 0, system } : null, distance);

CivilizationKnowledgeState Knowledge(IEnumerable<KnowledgeInput> inputs)
{
    var result = new CivilizationKnowledgeState();
    foreach (var input in inputs)
    {
        if (input.Level == SystemSurveyLevel.Detected) result.RevealSystem(input.CivilizationId, input.SystemId);
        else if (input.Level == SystemSurveyLevel.PartiallySurveyed) result.RecordReconnaissance(input.CivilizationId, input.SystemId, input.Progress);
        else if (input.Level == SystemSurveyLevel.FullySurveyed) result.MarkSystemFullySurveyed(input.CivilizationId, input.SystemId);
    }
    return result;
}
GalaxyState Galaxy(IReadOnlyList<StarSystemState> systems, IReadOnlyList<PlanetaryBodyState> bodies,
    IList<FleetState> fleets, CivilizationKnowledgeState knowledge) => new()
    {
        Seed = 24,
        Systems = systems,
        PlanetaryBodies = bodies,
        Civilizations = [],
        Fleets = fleets,
        Colonies = [],
        Economies = [],
        Technologies = [],
        ConstructionStates = [],
        ShipyardStates = [],
        PlayerCivilizationId = 1,
        Knowledge = knowledge,
    };
object SnapshotKnowledge(CivilizationKnowledgeState knowledge, IEnumerable<FleetState> fleets,
    IEnumerable<StarSystemState> systems) => fleets.Select(f => f.CivilizationId).Distinct().Select(c => new
    {
        CivilizationId = c,
        Survey = systems.Select(s => new { SystemId = s.Id, Level = knowledge.GetSystemSurveyLevel(c, s.Id), Progress = knowledge.GetSystemSurveyProgress(c, s.Id) }).ToArray(),
    }).ToArray();

void Run(string name, string kind, int fleetId, int destination, int maximum, bool requireWork,
    StarSystemState[] systems, PlanetaryBodyState[] bodies, FleetState[] fleets,
    KnowledgeInput[] knowledgeInputs, ReachInput[] reachInputs, int fleetArgumentId = -1,
    bool useInjectedReach = true, FleetState? fleetArgument = null)
{
    var knowledge = Knowledge(knowledgeInputs);
    var galaxy = Galaxy(systems, bodies, fleets, knowledge);
    var beforeFleets = JsonSerializer.SerializeToElement(fleets, options);
    var beforeKnowledge = SnapshotKnowledge(knowledge, fleets, systems);
    var arguments = new
    {
        FleetId = fleetId,
        DestinationSystemId = destination,
        MaximumCandidates = maximum,
        RequireSurveyWork = requireWork,
        FleetArgumentId = fleetArgumentId < 0 ? fleetId : fleetArgumentId,
        FleetArgument = fleetArgument,
        ReachMode = useInjectedReach ? "Injected" : "DefaultLane",
        Systems = systems,
        Bodies = bodies,
        Fleets = beforeFleets,
        Knowledge = knowledgeInputs,
        Reach = reachInputs,
    };
    var reach = new StubReach(reachInputs);
    var planner = useInjectedReach ? new ExplorationMissionPlanner(reach) : new ExplorationMissionPlanner();
    var coordinator = new ExplorationAiMissionCoordinator(planner);
    var selectFleet = fleetArgument ?? fleets.FirstOrDefault(f => f.Id == (fleetArgumentId < 0 ? fleetId : fleetArgumentId));
    object? result = null;
    object? error = null;
    try
    {
        result = kind switch
        {
            "Plan" => planner.BuildPlan(galaxy, fleetId, maximum),
            "Assess" => planner.AssessOrder(galaxy, fleetId, destination, requireWork),
            "Select" => coordinator.SelectMission(galaxy, selectFleet!),
            _ => throw new InvalidOperationException("Oracle kind is invalid."),
        };
    }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    var afterFleets = JsonSerializer.SerializeToElement(fleets, options);
    var afterKnowledge = SnapshotKnowledge(knowledge, fleets, systems);
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Before = new { Fleets = beforeFleets, Knowledge = beforeKnowledge }, After = new { Fleets = afterFleets, Knowledge = afterKnowledge }, Result = result, Error = error });
}

var baseSystems = new[] { S(0, 0), S(1, 1), S(2, 2), S(3, 3), S(4, 4) };
var allReach = baseSystems.Select(s => R(s.Id, distance: s.Id)).ToArray();
void P(string name, FleetState[] fleets, KnowledgeInput[] knowledge, ReachInput[]? reach = null,
    int maximum = 32, StarSystemState[]? systems = null, PlanetaryBodyState[]? bodies = null,
    int fleetId = 10, bool injected = true) =>
    Run(name, "Plan", fleetId, 0, maximum, false, systems ?? baseSystems, bodies ?? [], fleets,
        knowledge, reach ?? allReach, useInjectedReach: injected);
void A(string name, FleetState[] fleets, int destination, KnowledgeInput[] knowledge,
    ReachInput[]? reach = null, bool require = true, StarSystemState[]? systems = null,
    PlanetaryBodyState[]? bodies = null, int fleetId = 10, bool injected = true) =>
    Run(name, "Assess", fleetId, destination, 32, require, systems ?? baseSystems, bodies ?? [], fleets,
        knowledge, reach ?? allReach, useInjectedReach: injected);
void Q(string name, FleetState request, FleetState[] fleets, KnowledgeInput[] knowledge,
    ReachInput[]? reach = null) =>
    Run(name, "Select", request.Id, 0, 64, false, baseSystems, [], fleets, knowledge,
        reach ?? allReach, request.Id, fleetArgument: request);

P("plan-missing", [F(11)], [], fleetId: 10);
P("plan-inactive", [F(active: false)], []);
foreach (var role in new[] { FleetRole.Colony, FleetRole.Military, FleetRole.Logistics }) P($"plan-role-{role}", [F(role: role)], []);
P("plan-empty-scout-fully-covered", [F()], baseSystems.Select(s => K(s.Id, SystemSurveyLevel.PartiallySurveyed, .35)).ToArray());
P("plan-empty-science-fully-covered", [F(role: FleetRole.Science)], baseSystems.Select(s => K(s.Id, SystemSurveyLevel.FullySurveyed)).ToArray());
P("plan-one-singular", [F()], [K(0, SystemSurveyLevel.PartiallySurveyed), K(1, SystemSurveyLevel.PartiallySurveyed), K(2, SystemSurveyLevel.PartiallySurveyed), K(3, SystemSurveyLevel.PartiallySurveyed)]);
P("plan-maximum-zero-clamps-one", [F()], [], maximum: 0);
P("plan-maximum-negative-clamps-one", [F()], [], maximum: -99);
var manySystems = Enumerable.Range(0, 70).Select(i => S(i, i)).ToArray();
P("plan-maximum-overflow-clamps-64", [F()], [], manySystems.Select(s => R(s.Id)).ToArray(), maximum: 999, systems: manySystems);
P("scout-detected-before-unknown", [F()], [K(3, SystemSurveyLevel.Detected)]);
P("scout-excludes-partial-and-full", [F()], [K(1, SystemSurveyLevel.PartiallySurveyed, .35), K(2, SystemSurveyLevel.FullySurveyed)]);
P("science-partial-detected-unknown-order", [F(role: FleetRole.Science)], [K(3, SystemSurveyLevel.Detected), K(4, SystemSurveyLevel.PartiallySurveyed, .4)]);
P("supported-before-blocked", [F()], [], [R(0, false), R(1, false), R(2), R(3), R(4)]);
P("distance-before-system-id", [F(position: new Vector2(3.8f, 0))], [], allReach);
P("survey-privacy-unknown", [F(role: FleetRole.Science)], [], allReach, bodies: [B(1, 1, radiation: .9)]);
P("survey-privacy-detected", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.Detected)], allReach, bodies: [B(1, 1, radiation: .9)]);
P("partial-exposes-profile", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, .4)], allReach, bodies: [B(1, 1, radiation: .9)]);
P("partial-duration-rounding", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, .35)], allReach, bodies: [B(1, 1), B(2, 1)]);
P("partial-percent-half-low", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, .005)], allReach, bodies: [B(1, 1)]);
P("partial-percent-half-even", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, .025)], allReach, bodies: [B(1, 1)]);
P("partial-percent-exact-binary-half", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, .125)], allReach, bodies: [B(1, 1)]);
P("partial-nan-progress", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.PartiallySurveyed, double.NaN)], allReach, bodies: [B(1, 1)]);
P("blocked-reason", [F(role: FleetRole.Science)], [K(1, SystemSurveyLevel.Detected)], [R(0, false, "No route."), R(1, false, "No fuel."), R(2), R(3), R(4)]);
P("duplicate-fleet-first-inactive-skipped", [F(active: false), F(position: new Vector2(2, 0))], []);
P("inflight-distance", [F(role: FleetRole.Science, current: 0, position: new Vector2(1.5f, 0), phase: FleetTransitPhase.InterstellarWarp, origin: 0, progress: .5)], [], allReach);
var nanDistanceSystems = new[] { S(0, 0), S(1, float.NaN), S(2, 1) };
P("nan-distance-sorts-before-finite", [F()], [], nanDistanceSystems.Select(s => R(s.Id)).ToArray(), systems: nanDistanceSystems);
P("plan-default-lane-reach", [F()], [], injected: false);

A("assess-missing", [F(11)], 1, []);
A("assess-inactive", [F(active: false)], 1, []);
A("assess-wrong-role", [F(role: FleetRole.Colony)], 1, []);
A("assess-unknown-target", [F()], 99, []);
A("assess-scout-covered", [F()], 1, [K(1, SystemSurveyLevel.PartiallySurveyed, .35)]);
A("assess-science-complete", [F(role: FleetRole.Science)], 1, [K(1, SystemSurveyLevel.FullySurveyed)]);
A("assess-blocked-with-candidate", [F()], 1, [], [R(0), R(1, false, "No lane."), R(2), R(3), R(4)]);
A("assess-local-scout-work", [F(current: 1)], 1, []);
A("assess-local-science-work", [F(role: FleetRole.Science, current: 1)], 1, [K(1, SystemSurveyLevel.PartiallySurveyed, .5)], bodies: [B(1, 1)]);
A("assess-travel-scout-work", [F(current: 0)], 1, []);
A("assess-travel-science-work", [F(role: FleetRole.Science, current: 0)], 1, []);
A("assess-local-no-work-allowed", [F(current: 1)], 1, [K(1, SystemSurveyLevel.PartiallySurveyed, .35)], require: false);
A("assess-travel-no-work-allowed", [F(current: 0)], 1, [K(1, SystemSurveyLevel.PartiallySurveyed, .35)], require: false);
A("assess-local-with-destination-is-travel", [F(current: 1, destination: 2)], 1, [], require: false);
A("assess-default-lane-local", [F(current: 0)], 0, [], injected: false);
A("assess-default-lane-travel", [F(current: 0)], 1, [], injected: false);

Q("select-inactive", F(active: false), [F(active: false)], []);
Q("select-wrong-role", F(role: FleetRole.Military), [F(role: FleetRole.Military)], []);
Q("select-no-supported", F(), [F()], [], baseSystems.Select(s => R(s.Id, false)).ToArray());
Q("select-first-unreserved", F(), [F()], []);
Q("select-skips-destination-reservation", F(), [F(), F(11, destination: 0)], []);
Q("select-local-work-reservation", F(), [F(), F(11, current: 0)], []);
Q("select-local-completed-not-reserved", F(), [F(), F(11, current: 0)], [K(0, SystemSurveyLevel.PartiallySurveyed, .35)]);
Q("select-foreign-reservation-ignored", F(), [F(), F(11, civilization: 2, destination: 0)], []);
Q("select-inactive-reservation-ignored", F(), [F(), F(11, active: false, destination: 0)], []);
Q("select-nonsurvey-reservation-ignored", F(), [F(), F(11, role: FleetRole.Military, destination: 0)], []);
Q("select-destination-precedes-local", F(), [F(), F(11, current: 1, destination: 3)], []);
Q("select-shared-fallback", F(), [F(), F(11, destination: 0), F(12, destination: 1), F(13, destination: 2), F(14, destination: 3), F(15, destination: 4)], []);
Q("select-sorted-reservation-output", F(), [F(), F(11, destination: 4), F(12, destination: 2), F(13, destination: 3)], []);
Q("select-detached-not-in-galaxy", F(99), [F()], []);
Q("select-detached-galaxy-role-controls-plan", F(), [F(role: FleetRole.Military)], []);

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-exploration-planning-oracle-v1",
    Cases = cases,
    NativeBoundary = new { NullGalaxy = "C# rejects null; native view references cannot represent it.", NullFleet = "C# rejects null; native FleetState references cannot represent it.", NullPlanner = "C# coordinator rejects null; native coordinator requires a planner reference." },
}, options) + Environment.NewLine);

sealed record KnowledgeInput(int CivilizationId, int SystemId, SystemSurveyLevel Level, double Progress);
sealed record ReachInput(int SystemId, bool IsSupported, bool IsAuthoritative, string Reason,
    IReadOnlyList<int>? RouteSystemIds, double RouteDistanceLightYears);
sealed class StubReach(IEnumerable<ReachInput> inputs) : IInterstellarOperationalReachView
{
    private readonly Dictionary<int, ReachInput> _inputs = inputs.ToDictionary(input => input.SystemId);
    public MissionReachAssessment Assess(GalaxyState galaxy, int civilizationId, FleetState fleet,
        int targetSystemId, InterstellarMissionKind missionKind)
    {
        if (civilizationId != fleet.CivilizationId) throw new InvalidOperationException("Wrong reach civilization.");
        var expectedKind = fleet.Role switch
        {
            FleetRole.Scout => InterstellarMissionKind.ScoutReconnaissance,
            FleetRole.Science => InterstellarMissionKind.ScienceSurvey,
            FleetRole.Military => InterstellarMissionKind.MilitaryDeployment,
            FleetRole.Logistics => InterstellarMissionKind.Logistics,
            _ => InterstellarMissionKind.ScoutReconnaissance,
        };
        if (missionKind != expectedKind) throw new InvalidOperationException("Wrong reach mission kind.");
        var input = _inputs[targetSystemId];
        return new(input.IsSupported, input.IsAuthoritative, input.Reason,
            input.RouteSystemIds, input.RouteDistanceLightYears);
    }
}
