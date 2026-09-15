using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
    var options = new JsonSerializerOptions
    {
        IncludeFields = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    var rows = new List<object>();

    StarSystemState S(int id, float x) => new(id, $"System {id}", new Vector2(x, 0),
        StarArchetype.Standard, true, false, false, false, null,
        StellarPrimaryClass.GYellowDwarf, null, null, null);
    FleetState F(int id = 10, FleetRole role = FleetRole.Scout, bool active = true,
        int? current = 0, int? destination = null, FleetTransitPhase phase = FleetTransitPhase.None,
        int revision = 7, bool hold = false, bool returning = false) => new()
        {
            Id = id, CivilizationId = 1, Name = $"Fleet {id}", Role = role,
            Position = Vector2.Zero, CurrentSystemId = current,
            DestinationSystemId = destination, TransitPhase = phase,
            TransitOriginSystemId = current, TransitTargetSystemId = destination,
            TransitProgress = .25, LocalTransitStart = new Vector2(.1f, .2f),
            LocalTransitPosition = new Vector2(.3f, .4f), LocalTransitTarget = new Vector2(.5f, .6f),
            PlannedRouteSystemIds = destination is null ? [] : [destination.Value],
            HoldRequested = hold, ReturnToBaseRequested = returning,
            ReturnToBaseFailureReason = returning ? "queued" : null,
            MissionOrderRevision = revision, StrategicSpeed = 22,
            MaximumLegRangeLightYears = 360, FuelCapacityLightYears = 1000,
            FuelRemainingLightYears = 800, SensorRange = 135, IsActive = active,
        };
    CivilizationKnowledgeState K(params (int System, SystemSurveyLevel Level, double Progress)[] values)
    {
        var result = new CivilizationKnowledgeState();
        foreach (var value in values)
        {
            if (value.Level == SystemSurveyLevel.Detected) result.RevealSystem(1, value.System);
            else if (value.Level == SystemSurveyLevel.PartiallySurveyed) result.RecordReconnaissance(1, value.System, value.Progress);
            else if (value.Level == SystemSurveyLevel.FullySurveyed) result.MarkSystemFullySurveyed(1, value.System);
        }
        return result;
    }
    ReachInput R(int system, bool supported = true, string? reason = null) =>
        new(system, supported, true, reason ?? (supported ? $"Reach {system}." : $"Blocked {system}."),
            supported ? [0, system] : null, system);

    var systems = new[] { S(0, 0), S(1, 1), S(2, 2) };
    var supported = systems.Select(system => R(system.Id)).ToArray();

    void Run(string name, bool survey, FleetState[] fleets, int destination,
        (int System, SystemSurveyLevel Level, double Progress)[] knowledgeValues,
        ReachInput[]? reaches = null, int fleetId = 10)
    {
        var knowledge = K(knowledgeValues);
        var galaxy = new GalaxyState
        {
            Seed = 106, Systems = systems, PlanetaryBodies = [], Civilizations = [],
            Fleets = fleets, Colonies = [], Economies = [], Technologies = [],
            ConstructionStates = [], ShipyardStates = [], PlayerCivilizationId = 1,
            Knowledge = knowledge,
        };
        var reachRows = reaches ?? supported;
        var before = JsonSerializer.SerializeToElement(fleets, options);
        object? result = null;
        object? error = null;
        try
        {
            var simulation = new ExplorationSimulation(new StubReach(reachRows));
            result = survey
                ? simulation.IssueSurveyOrder(galaxy, fleetId, destination)
                : simulation.IssueTravelOrder(galaxy, fleetId, destination);
        }
        catch (Exception exception)
        {
            error = new { Type = exception.GetType().Name, exception.Message };
        }
        rows.Add(new
        {
            Name = name, Kind = survey ? "Survey" : "Travel", FleetId = fleetId,
            DestinationSystemId = destination, Systems = systems, Fleets = before,
            Knowledge = knowledgeValues.Select(value => new
            {
                CivilizationId = 1, SystemId = value.System, Level = value.Level,
                value.Progress,
            }).ToArray(),
            Reach = reachRows, Result = result,
            AfterFleets = JsonSerializer.SerializeToElement(fleets, options), Error = error,
        });
    }

    Run("travel-missing", false, [F(11)], 1, []);
    Run("travel-inactive", false, [F(active: false)], 1, []);
    Run("travel-wrong-role", false, [F(role: FleetRole.Military)], 1, []);
    Run("travel-unknown-target", false, [F()], 99, []);
    Run("travel-blocked-no-mutation", false, [F(hold: true, returning: true)], 1, [],
        [R(0), R(1, false, "No lane."), R(2)]);
    Run("travel-scout-assign", false, [F(hold: true, returning: true)], 1, []);
    Run("travel-science-assign", false, [F(role: FleetRole.Science)], 2, []);
    Run("survey-scout-assign", true, [F()], 1, []);
    Run("travel-local-clear", false,
        [F(current: 1, phase: FleetTransitPhase.LocalDeparture, hold: true, returning: true)], 1, []);
    Run("travel-local-covered-clear", false,
        [F(current: 1, phase: FleetTransitPhase.LocalArrival)], 1,
        [(1, SystemSurveyLevel.PartiallySurveyed, .35)]);
    Run("survey-covered-rejected", true, [F(current: 1)], 1,
        [(1, SystemSurveyLevel.PartiallySurveyed, .35)]);
    Run("travel-reroute-local-arrival", false,
        [F(current: 0, destination: 2, phase: FleetTransitPhase.LocalArrival)], 1, []);

    File.WriteAllText(args[0], JsonSerializer.Serialize(new
    {
        Format = "stellar-exploration-orders-oracle-v1", Rows = rows,
        NativeBoundary = new { NullGalaxy = "Native world views are references and cannot represent null." },
    }, options) + Environment.NewLine);
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Exploration order oracle failed: {exception}");
    Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
    if (args.Length > 0) Console.Error.WriteLine($"Output path: {args[0]}");
    Environment.ExitCode = 1;
}

sealed record ReachInput(int SystemId, bool IsSupported, bool IsAuthoritative,
    string Reason, IReadOnlyList<int>? RouteSystemIds, double RouteDistanceLightYears);

sealed class StubReach(IEnumerable<ReachInput> inputs) : IInterstellarOperationalReachView
{
    private readonly Dictionary<int, ReachInput> _inputs = inputs.ToDictionary(input => input.SystemId);

    public MissionReachAssessment Assess(GalaxyState galaxy, int civilizationId, FleetState fleet,
        int targetSystemId, InterstellarMissionKind missionKind)
    {
        if (civilizationId != fleet.CivilizationId) throw new InvalidOperationException("Wrong reach civilization.");
        var row = _inputs[targetSystemId];
        return new(row.IsSupported, row.IsAuthoritative, row.Reason,
            row.RouteSystemIds, row.RouteDistanceLightYears);
    }
}
