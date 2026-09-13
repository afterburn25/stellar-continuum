using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

if (args.Length != 1)
    throw new ArgumentException("Expected output fixture path.");

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

var json = new JsonSerializerOptions
{
    WriteIndented = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};

JsonElement Clone(object? value) => JsonSerializer.SerializeToElement(value, json);

object SystemView(StarSystemState system) => new
{
    system.Id,
    system.Name,
    X = system.Position.X,
    Y = system.Position.Y,
    Archetype = system.Archetype.ToString(),
    system.HasHabitableWorld,
    system.HasAnomaly,
    system.HasRareResource,
    system.HasPreWarpCivilization,
    system.CatalogPresetId,
    StellarClass = system.StellarClass?.ToString(),
    SecondaryStellarClass = system.SecondaryStellarClass?.ToString(),
    TertiaryStellarClass = system.TertiaryStellarClass?.ToString(),
    system.GalacticDepthLightYears,
    system.StellarCatalogId,
};

object SystemsView(IReadOnlyList<StarSystemState>? systems) =>
    systems is null ? new { IsNull = true, Systems = Array.Empty<object>() } :
    new { IsNull = false, Systems = systems.Select(SystemView).ToArray() };

object FleetView(FleetState? fleet) => fleet is null ? new { IsNull = true } : new
{
    IsNull = false,
    fleet.Id,
    fleet.CivilizationId,
    fleet.Name,
    Role = fleet.Role.ToString(),
    fleet.DesignId,
    PositionX = fleet.Position.X,
    PositionY = fleet.Position.Y,
    fleet.CurrentSystemId,
    fleet.DestinationSystemId,
    TransitPhase = fleet.TransitPhase.ToString(),
    fleet.TransitOriginSystemId,
    fleet.TransitTargetSystemId,
    fleet.TransitProgress,
    LocalTransitStartX = fleet.LocalTransitStart.X,
    LocalTransitStartY = fleet.LocalTransitStart.Y,
    LocalTransitPositionX = fleet.LocalTransitPosition.X,
    LocalTransitPositionY = fleet.LocalTransitPosition.Y,
    LocalTransitTargetX = fleet.LocalTransitTarget.X,
    LocalTransitTargetY = fleet.LocalTransitTarget.Y,
    PlannedRouteSystemIds = fleet.PlannedRouteSystemIds.ToArray(),
    fleet.HoldRequested,
    fleet.ReturnToBaseRequested,
    fleet.ReturnToBaseFailureReason,
    fleet.MissionOrderRevision,
    fleet.DestinationPlanetaryBodyId,
    fleet.PreventAutomaticSettlement,
    fleet.SettlementBodyId,
    fleet.SettlementDaysCompleted,
    fleet.ReconnaissanceSystemId,
    fleet.ReconnaissanceDaysCompleted,
    fleet.FreightTargetOutpostId,
    fleet.FreightHomeColonyId,
    fleet.CargoMaterialCapacity,
    fleet.CargoMaterials,
    fleet.StrategicSpeed,
    fleet.MaximumLegRangeLightYears,
    fleet.FuelCapacityLightYears,
    fleet.FuelRemainingLightYears,
    fleet.SensorRange,
    fleet.IsActive,
    fleet.EmbarkedPopulationMillions,
    fleet.EmbarkedPopulationSpeciesId,
    fleet.Combat,
    fleet.TacticalLoadout,
    fleet.TacticalVessel,
};

object WorldView(GalaxyState? galaxy, FleetState? fleet) => new
{
    GalaxyIsNull = galaxy is null,
    Systems = galaxy is null ? null : SystemsView(galaxy.Systems),
    Fleet = FleetView(fleet),
};

object LaneView(InterstellarLane lane) => new
{
    lane.FirstSystemId,
    lane.SecondSystemId,
    lane.LengthLightYears,
};

var cases = new List<object>();

void Add(
    string name,
    string kind,
    object arguments,
    Func<object> before,
    Func<object?> operation,
    Func<object> after,
    object? nativeBoundary = null)
{
    var argumentsSnapshot = Clone(arguments);
    var beforeSnapshot = Clone(before());
    object? rawResult = null;
    Exception? caught = null;
    try
    {
        rawResult = operation();
    }
    catch (Exception exception)
    {
        caught = exception;
    }
    var resultSnapshot = Clone(rawResult);
    JsonElement? errorSnapshot = caught is null ? null : Clone(new { Type = caught.GetType().Name, caught.Message });
    var afterSnapshot = Clone(after());
    cases.Add(new
    {
        Name = name,
        Kind = kind,
        Arguments = argumentsSnapshot,
        Before = beforeSnapshot,
        Result = resultSnapshot,
        Error = errorSnapshot,
        After = afterSnapshot,
        NativeBoundary = Clone(nativeBoundary),
    });
}

StarSystemState S(int id, float x, float y, double? depth = null, string? name = null) =>
    new(id, name ?? $"System {id}", new Vector2(x, y), StarArchetype.Standard,
        false, false, false, false, GalacticDepthLightYears: depth);

FleetState F(
    Vector2 position,
    int? current = null,
    int? destination = null,
    IEnumerable<int>? route = null,
    FleetTransitPhase phase = FleetTransitPhase.None,
    int? origin = null,
    int? target = null,
    double progress = 0) => new()
{
    Id = 77,
    CivilizationId = 3,
    Name = "Oracle vessel",
    Role = FleetRole.Science,
    DesignId = "science_vessel",
    Position = position,
    CurrentSystemId = current,
    DestinationSystemId = destination,
    PlannedRouteSystemIds = route?.ToList() ?? new List<int>(),
    TransitPhase = phase,
    TransitOriginSystemId = origin,
    TransitTargetSystemId = target,
    TransitProgress = progress,
    LocalTransitStart = new Vector2(1, 2),
    LocalTransitPosition = new Vector2(3, 4),
    LocalTransitTarget = new Vector2(5, 6),
    HoldRequested = true,
    ReturnToBaseRequested = true,
    ReturnToBaseFailureReason = "preserve me",
    MissionOrderRevision = 12,
    DestinationPlanetaryBodyId = 101,
    PreventAutomaticSettlement = true,
    SettlementBodyId = 102,
    SettlementDaysCompleted = 3.5,
    ReconnaissanceSystemId = 103,
    ReconnaissanceDaysCompleted = 4.5,
    FreightTargetOutpostId = 104,
    FreightHomeColonyId = 105,
    CargoMaterialCapacity = 400,
    CargoMaterials = 125,
    StrategicSpeed = 31,
    MaximumLegRangeLightYears = 410,
    FuelCapacityLightYears = 900,
    FuelRemainingLightYears = 650,
    SensorRange = 88,
    IsActive = true,
    EmbarkedPopulationMillions = 2.5,
    EmbarkedPopulationSpeciesId = "oracle_species",
};

GalaxyState G(IReadOnlyList<StarSystemState> systems, FleetState? fleet = null) => new()
{
    Seed = 123,
    Systems = systems,
    Civilizations = null!,
    Fleets = fleet is null ? new List<FleetState>() : new List<FleetState> { fleet },
    Colonies = null!,
    Economies = null!,
    Technologies = null!,
    ConstructionStates = null!,
    ShipyardStates = null!,
    PlayerCivilizationId = 3,
    Knowledge = null!,
};

void Conversion(string name, string kind, double value, Func<double, double> operation) =>
    Add(name, kind, new { Value = value }, () => new { Value = value },
        () => new { Value = operation(value) }, () => new { Value = value });

Conversion("light-years-one-parsec", "LightYearsToParsecs", AstronomicalDistance.LightYearsPerParsec,
    AstronomicalDistance.LightYearsToParsecs);
Conversion("au-one", "AuToKilometres", 1, AstronomicalDistance.AuToKilometres);

void LaneCase(string name, string kind, InterstellarLane lane, int systemId) =>
    Add(name, kind, new { Lane = LaneView(lane), SystemId = systemId },
        () => LaneView(lane),
        () => kind == "LaneConnects" ? lane.Connects(systemId) : lane.Other(systemId),
        () => LaneView(lane));

var sampleLane = new InterstellarLane(4, 9, 12.25);
LaneCase("lane-connects-first", "LaneConnects", sampleLane, 4);
LaneCase("lane-connects-unrelated", "LaneConnects", sampleLane, 7);
LaneCase("lane-other-first", "LaneOther", sampleLane, 4);
LaneCase("lane-other-unrelated", "LaneOther", sampleLane, 7);

void BuildCase(string name, IReadOnlyList<StarSystemState>? systems)
{
    var network = new InterstellarLaneNetwork();
    var arguments = new { Systems = SystemsView(systems) };
    Add(name, "Build", arguments, () => SystemsView(systems),
        () => network.Build(systems!).Select(LaneView).ToArray(), () => SystemsView(systems));
}

BuildCase("build-null", null);
BuildCase("build-empty", Array.Empty<StarSystemState>());
BuildCase("build-single", new[] { S(5, 2, 3) });
BuildCase("build-pair-flat", new[] { S(8, 3, 4), S(2, 0, 0) });
BuildCase("build-pair-depth-3-4-12", new[] { S(701, 0, 0, 0), S(702, 3, 4, 12) });
BuildCase("build-duplicate-identifiers", new[] { S(1, 0, 0), S(1, 1, 0) });
BuildCase("build-collinear", new[] { S(9, 30, 0), S(2, 0, 0), S(6, 20, 0), S(4, 10, 0) });
BuildCase("build-collinear-permuted", new[] { S(4, 10, 0), S(9, 30, 0), S(6, 20, 0), S(2, 0, 0) });
BuildCase("build-square-ties", new[] { S(40, 10, 10), S(10, 0, 0), S(30, 0, 10), S(20, 10, 0) });
BuildCase("build-coincident", new[] { S(31, 10, 0), S(9, 0, 0), S(2, 10, 0), S(17, 0, 10), S(5, 10, 10) });
BuildCase("build-dense-six", new[] { S(0, 0, 0), S(1, 3, 1), S(2, 6, 0), S(3, 1, 5), S(4, 5, 5), S(5, 9, 4) });
BuildCase("build-sparse-outlier", new[] { S(0, 0, 0), S(1, 2, 0), S(2, 4, 0), S(3, 40, 25), S(4, 90, -30) });

var nearTieDepths = new[]
{
    S(0, 0, 0, 0),
    S(1, 0, 0, 10.000000003),
    S(2, 0, 0, 10.00000000225),
    S(3, 0, 0, 10.0000000015),
    S(4, 0, 0, 10.00000000075),
    S(5, 0, 0, 10.0),
    S(6, 0, 0, 30.0),
};
BuildCase("build-depth-near-tie-chain", nearTieDepths);
BuildCase("build-depth-near-tie-chain-permuted", nearTieDepths.Reverse().ToArray());

var overflowSystems = new[]
{
    S(1, float.MaxValue, 0),
    S(2, -float.MaxValue, 0),
};
var overflowNetwork = new InterstellarLaneNetwork();
Add("build-finite-input-overflow-boundary", "Build", new { Systems = SystemsView(overflowSystems) },
    () => SystemsView(overflowSystems),
    () => overflowNetwork.Build(overflowSystems).Select(LaneView).ToArray(),
    () => SystemsView(overflowSystems),
    new
    {
        Type = "ArgumentException",
        Message = "Interstellar lane geometry produced a nonfinite distance",
        Reason = "Native import rejects finite coordinates whose computed separation overflows; source fails through its -1 sentinel.",
    });

var catalogSystems = NearbyStarCatalog.Stars.Take(6).Select((star, index) =>
    NearbyStarCatalog.Apply(S(index, 0, 0, name: $"Catalog input {index}"), star)).ToArray();
BuildCase("build-nearby-catalog-six", catalogSystems);

var mutableSystems = new List<StarSystemState> { S(9, 0, 0), S(2, 10, 0), S(17, 0, 10), S(5, 10, 10) };
var cachedNetwork = new InterstellarLaneNetwork();
Add("build-cache-replace-entry", "BuildCacheMutation",
    new { Systems = SystemsView(mutableSystems), ReplacementIndex = 0, Replacement = SystemView(S(9, 50, 50)) },
    () => SystemsView(mutableSystems),
    () =>
    {
        var first = cachedNetwork.Build(mutableSystems);
        var repeat = cachedNetwork.Build(mutableSystems);
        mutableSystems[0] = S(9, 50, 50);
        var changed = cachedNetwork.Build(mutableSystems);
        return new
        {
            First = first.Select(LaneView).ToArray(),
            Repeat = repeat.Select(LaneView).ToArray(),
            SameReferenceBeforeReplacement = ReferenceEquals(first, repeat),
            Changed = changed.Select(LaneView).ToArray(),
            SameReferenceAfterReplacement = ReferenceEquals(first, changed),
        };
    },
    () => SystemsView(mutableSystems));

void RouteCase(string name, IReadOnlyList<StarSystemState>? systems, int origin, int destination,
    double range, IReadOnlySet<int>? permitted = null)
{
    var network = new InterstellarLaneNetwork();
    Add(name, "FindShortestRoute", new
    {
        Systems = SystemsView(systems),
        OriginSystemId = origin,
        DestinationSystemId = destination,
        MaximumLegRangeLightYears = range,
        PermittedSystemIds = permitted?.OrderBy(id => id).ToArray(),
    }, () => SystemsView(systems),
        () => network.FindShortestRoute(systems!, origin, destination, range, permitted).ToArray(),
        () => SystemsView(systems));
}

var routeSystems = new[] { S(9, 0, 0), S(2, 10, 0), S(17, 0, 10), S(5, 10, 10), S(24, 20, 0), S(31, 10, 0), S(40, 40, 20) };
RouteCase("route-invalid-zero-before-null", null, 99, 100, 0);
RouteCase("route-invalid-negative-before-unknown", routeSystems, 99, 100, -1);
RouteCase("route-invalid-nan-before-unknown", routeSystems, 99, 100, double.NaN);
RouteCase("route-null-systems", null, 1, 2, 10);
RouteCase("route-unknown-origin", routeSystems, 99, 5, 10);
RouteCase("route-unknown-destination", routeSystems, 9, 99, 10);
RouteCase("route-origin-equals-destination", routeSystems, 9, 9, double.PositiveInfinity);
RouteCase("route-unrestricted", routeSystems, 9, 40, double.PositiveInfinity);
RouteCase("route-range-disconnected", routeSystems, 9, 40, 10);
RouteCase("route-range-connected", routeSystems, 9, 40, 25);
RouteCase("route-range-tolerance-below-ten", routeSystems, 9, 2, 10 - 5e-10);
RouteCase("route-permitted-missing-origin", routeSystems, 9, 40, 100, new HashSet<int> { 2, 5, 17, 24, 31, 40 });
RouteCase("route-permitted-intermediate-exclusion", routeSystems, 9, 40, 25, new HashSet<int> { 5, 9, 17, 40 });
RouteCase("route-depth-range-blocked", new[] { S(701, 0, 0, 0), S(702, 3, 4, 12) }, 701, 702, 12.999);
RouteCase("route-depth-range-exact", new[] { S(701, 0, 0, 0), S(702, 3, 4, 12) }, 701, 702, 13);
RouteCase("route-square-tie", new[] { S(10, 0, 0), S(20, 10, 0), S(30, 0, 10), S(40, 10, 10) }, 10, 40, 10);

var permissionSystems = routeSystems.ToList();
var mutablePermission = permissionSystems.Select(system => system.Id).ToHashSet();
var permissionNetwork = new InterstellarLaneNetwork();
Add("route-permission-mutated-in-place", "FindShortestRoutePermissionMutation", new
{
    Systems = SystemsView(permissionSystems), OriginSystemId = 9, DestinationSystemId = 40,
    MaximumLegRangeLightYears = 25.0, InitialPermittedSystemIds = mutablePermission.OrderBy(id => id).ToArray(),
    RemovedSystemIds = new[] { 2, 31 },
}, () => new { Systems = SystemsView(permissionSystems), PermittedSystemIds = mutablePermission.OrderBy(id => id).ToArray() },
    () =>
    {
        var first = permissionNetwork.FindShortestRoute(permissionSystems, 9, 40, 25, mutablePermission).ToArray();
        mutablePermission.Remove(2);
        mutablePermission.Remove(31);
        var second = permissionNetwork.FindShortestRoute(permissionSystems, 9, 40, 25, mutablePermission).ToArray();
        return new { First = first, Second = second };
    },
    () => new { Systems = SystemsView(permissionSystems), PermittedSystemIds = mutablePermission.OrderBy(id => id).ToArray() });

var rangeNetwork = new InterstellarLaneNetwork();
Add("route-same-graph-different-ranges", "FindShortestRouteRangeSequence", new
{
    Systems = SystemsView(routeSystems), OriginSystemId = 9, DestinationSystemId = 40,
    MaximumLegRangesLightYears = new[] { 10.0, 25.0, double.PositiveInfinity, 10.0 },
}, () => SystemsView(routeSystems),
    () => new[] { 10.0, 25.0, double.PositiveInfinity, 10.0 }.Select(range => new
    {
        Range = range,
        Path = rangeNetwork.FindShortestRoute(routeSystems, 9, 40, range).ToArray(),
    }).ToArray(),
    () => SystemsView(routeSystems));

void MetricsCase(string name, GalaxyState? galaxy, FleetState? fleet)
{
    Add(name, "FleetRouteMetricsMeasure", new { World = WorldView(galaxy, fleet) },
        () => WorldView(galaxy, fleet),
        () => FleetRouteMetrics.Measure(galaxy!, fleet!),
        () => WorldView(galaxy, fleet));
}

var metricSystems = new[] { S(1, 0, 0), S(2, 3, 4), S(3, 6, 8), S(4, 9, 12) };
MetricsCase("metrics-null-galaxy", null, F(Vector2.Zero, 1, 2));
MetricsCase("metrics-null-fleet", G(metricSystems), null);
MetricsCase("metrics-no-destination-before-duplicate-ids", G(new[] { S(1, 0, 0), S(1, 2, 0) }), F(Vector2.Zero, 1));
MetricsCase("metrics-destination-fallback", G(metricSystems), F(Vector2.Zero, 1, 3));
MetricsCase("metrics-planned-multiple-legs", G(metricSystems), F(Vector2.Zero, 1, 4, new[] { 2, 3, 4 }));
MetricsCase("metrics-missing-waypoint-skipped", G(metricSystems), F(Vector2.Zero, 1, 4, new[] { 99, 2, 4 }));
MetricsCase("metrics-unknown-current-uses-fleet-position", G(metricSystems), F(new Vector2(3, 0), 99, 2));
MetricsCase("metrics-duplicate-system-identifiers", G(new[] { S(1, 0, 0), S(1, 2, 0) }), F(Vector2.Zero, 1, 1));
MetricsCase("metrics-inflight-half", G(metricSystems), F(new Vector2(1.5f, 2), null, 3, new[] { 2, 3 },
    FleetTransitPhase.InterstellarWarp, 1, 2, .5));
MetricsCase("metrics-inflight-progress-clamped-high", G(metricSystems), F(new Vector2(3, 4), null, 2, new[] { 2 },
    FleetTransitPhase.InterstellarWarp, 1, 2, 4));
var depthMetrics = new[] { S(1, 0, 0, 0), S(2, 3, 4, 12), S(3, 6, 8, 24) };
MetricsCase("metrics-depth-multiple-legs", G(depthMetrics), F(Vector2.Zero, 1, 3, new[] { 2, 3 }));
MetricsCase("metrics-inflight-depth-half", G(depthMetrics), F(new Vector2(1.5f, 2), null, 3, new[] { 2, 3 },
    FleetTransitPhase.InterstellarWarp, 1, 2, .5));

var document = new
{
    Format = "stellar-lane-network-oracle-v1",
    Source = "src/Game/Simulation/Exploration/InterstellarLaneNetwork.cs",
    PublicSurface = new[]
    {
        "AstronomicalDistance.LightYearsToParsecs(double)",
        "AstronomicalDistance.AuToKilometres(double)",
        "InterstellarLane.Connects(int)",
        "InterstellarLane.Other(int)",
        "InterstellarLaneNetwork.Build(IReadOnlyList<StarSystemState>)",
        "InterstellarLaneNetwork.FindShortestRoute(IReadOnlyList<StarSystemState>, int, int, double, IReadOnlySet<int>?)",
        "FleetRouteMetrics.Measure(GalaxyState, FleetState)",
    },
    Cases = cases,
};

var outputPath = Path.GetFullPath(args[0]);
Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
File.WriteAllText(outputPath, JsonSerializer.Serialize(document, json) + Environment.NewLine);
Console.WriteLine($"Wrote {cases.Count} cases to {outputPath}");
