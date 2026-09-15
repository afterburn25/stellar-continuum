using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

if (args.Length != 1)
    throw new ArgumentException("Expected output fixture path.");
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

var json = new JsonSerializerOptions
{
    WriteIndented = true,
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, json);

StarSystemState System(int id, string name, float x, float y, double? depth = null) =>
    new(id, name, new Vector2(x, y), StarArchetype.Standard, false, false,
        false, false, GalacticDepthLightYears: depth);

FleetState Fleet(string name = "Pathfinder", FleetRole role = FleetRole.Scout) => new()
{
    Id = 7,
    CivilizationId = 1,
    Name = name,
    Role = role,
    DesignId = "oracle_design",
    Position = new Vector2(.25f, -.5f),
    CurrentSystemId = 10,
    DestinationSystemId = 30,
    TransitPhase = FleetTransitPhase.None,
    TransitOriginSystemId = 10,
    TransitTargetSystemId = 20,
    TransitProgress = .25,
    LocalTransitStart = new Vector2(.1f, .2f),
    LocalTransitPosition = new Vector2(.3f, .4f),
    LocalTransitTarget = new Vector2(.5f, .6f),
    PlannedRouteSystemIds = new List<int> { 20, 30 },
    HoldRequested = false,
    ReturnToBaseRequested = false,
    ReturnToBaseFailureReason = "old failure",
    MissionOrderRevision = 5,
    PreventAutomaticSettlement = false,
    SettlementDaysCompleted = 0,
    ReconnaissanceSystemId = 44,
    ReconnaissanceDaysCompleted = 2.5,
    FreightTargetOutpostId = 55,
    FreightHomeColonyId = 66,
    CargoMaterialCapacity = 500,
    CargoMaterials = 123,
    StrategicSpeed = 27,
    MaximumLegRangeLightYears = 50,
    FuelCapacityLightYears = 100,
    FuelRemainingLightYears = 80,
    SensorRange = 77,
    IsActive = true,
    EmbarkedPopulationMillions = 3.5,
    EmbarkedPopulationSpeciesId = "oracle_species",
};

GalaxyState World(FleetState? fleet = null) => new()
{
    Seed = 23023,
    Systems = new[]
    {
        System(10, "Sol", 0, 0), System(20, "Alpha", 10, 0),
        System(30, "Beta", 20, 0), System(40, "Twin", -10, 0),
        System(50, "Far", 100, 0),
    },
    Civilizations = new List<CivilizationState>(),
    Fleets = new List<FleetState> { fleet ?? Fleet() },
    Colonies = new List<ColonyState>
    {
        new() { Id = 1, CivilizationId = 1, SystemId = 10, Name = "Sol Colony" },
        new() { Id = 2, CivilizationId = 1, SystemId = 20, Name = "Alpha Colony" },
    },
    Economies = new List<CivilizationEconomyState>(),
    Technologies = new List<Game.Simulation.Research.TechnologyState>(),
    ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(),
    ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(),
    PlayerCivilizationId = 1,
    Knowledge = new CivilizationKnowledgeState(),
};

object State(GalaxyState? world) => world is null ? new { IsNull = true } : new
{
    IsNull = false,
    Systems = world.Systems,
    Colonies = world.Colonies,
    Fleets = world.Fleets,
};

var cases = new List<object>();
void Add(string name, GalaxyState? world, Command[] commands,
         object? nativeBoundary = null)
{
    var arguments = Freeze(new { World = State(world), Commands = commands });
    var before = Freeze(State(world));
    var results = new List<object>();
    foreach (var command in commands)
    {
        object? rawResult = null;
        Exception? caught = null;
        try
        {
            rawResult = Dispatch(world, command);
        }
        catch (Exception exception)
        {
            caught = exception;
        }
        var result = Freeze(rawResult);
        var error = caught is null ? (JsonElement?)null :
            Freeze(new { Type = caught.GetType().Name, caught.Message });
        results.Add(new { Command = Freeze(command), Result = result, Error = error });
    }
    var after = Freeze(State(world));
    cases.Add(new
    {
        Name = name,
        Arguments = arguments,
        Before = before,
        Result = Freeze(results),
        After = after,
        NativeBoundary = Freeze(nativeBoundary),
    });
}

object? Dispatch(GalaxyState? world, Command command)
{
    var galaxy = command.NullGalaxy ? null! : world!;
    return command.Operation switch
    {
        "Hold" => CivilianFleetHoldOrders.Hold(galaxy, command.CivilizationId, command.FleetId),
        "Resume" => CivilianFleetHoldOrders.Resume(galaxy, command.CivilizationId, command.FleetId),
        "PreviewReturn" => CivilianFleetReturnOrders.PreviewReturn(galaxy, command.CivilizationId, command.FleetId),
        "RequestReturn" => CivilianFleetReturnOrders.RequestReturn(galaxy, command.CivilizationId, command.FleetId, command.Confirm),
        "ActivateQueuedReturnAtSystem" => CivilianFleetReturnOrders.ActivateQueuedReturnAtSystem(
            galaxy, command.NullFleet ? null! : world!.Fleets[command.FleetIndex]),
        "AbandonMissionForTransit" => Abandon(command.NullFleet ? null! : world!.Fleets[command.FleetIndex]),
        _ => throw new InvalidOperationException($"Unknown source command {command.Operation}."),
    };
}

object? Abandon(FleetState fleet)
{
    ColonizationSimulation.AbandonMissionForTransit(fleet);
    return null;
}

Command Hold(int civilization = 1, int fleet = 7) => new("Hold", civilization, fleet);
Command Resume(int civilization = 1, int fleet = 7) => new("Resume", civilization, fleet);
Command Preview(int civilization = 1, int fleet = 7) => new("PreviewReturn", civilization, fleet);
Command Request(bool confirm = false, int civilization = 1, int fleet = 7) =>
    new("RequestReturn", civilization, fleet, Confirm: confirm);
Command Activate(int index = 0) => new("ActivateQueuedReturnAtSystem", FleetIndex: index);
Command AbandonCommand(int index = 0) => new("AbandonMissionForTransit", FleetIndex: index);

void One(string name, Action<GalaxyState>? setup, Command command)
{
    var world = World();
    setup?.Invoke(world);
    Add(name, world, new[] { command });
}

One("lookup-missing", null, Hold(fleet: 999));
One("lookup-inactive", w => w.Fleets[0].IsActive = false, Hold());
One("lookup-foreign", w => w.Fleets[0] = Fleet().WithCivilization(2), Hold());
One("lookup-military", w => w.Fleets[0] = Fleet(role: FleetRole.Military), Hold());
One("lookup-logistics", w => w.Fleets[0] = Fleet(role: FleetRole.Logistics), Hold());
One("lookup-invalid-before-valid", w =>
{
    w.Fleets[0].IsActive = false;
    w.Fleets.Add(Fleet("Valid duplicate"));
}, Hold());
One("lookup-first-valid-duplicate", w => w.Fleets.Add(Fleet("Second valid")), Hold());

One("hold-already-held", w => w.Fleets[0].HoldRequested = true, Hold());
One("resume-already-proceeding", w => w.Fleets[0].HoldRequested = false, Resume());
One("hold-current-known", null, Hold());
One("hold-current-unknown", w => w.Fleets[0].CurrentSystemId = 999, Hold());
One("hold-planned-head", w => w.Fleets[0].CurrentSystemId = null, Hold());
One("hold-destination-fallback", w =>
{
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].PlannedRouteSystemIds.Clear();
}, Hold());
One("hold-next-fallback", w =>
{
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].PlannedRouteSystemIds.Clear();
    w.Fleets[0].DestinationSystemId = null;
}, Hold());
One("hold-duplicate-system-first-name", w =>
{
    var systems = (StarSystemState[])w.Systems;
    systems[0] = systems[0] with { Name = "First Sol" };
    systems[1] = System(10, "Second Sol", 1, 0);
}, Hold());
foreach (var phase in new[] { FleetTransitPhase.LocalDeparture, FleetTransitPhase.LocalArrival, FleetTransitPhase.InterstellarWarp })
    One($"hold-phase-{phase}", w => w.Fleets[0].TransitPhase = phase, Hold());

One("resume-clears-failure-preserves-return", w =>
{
    w.Fleets[0].HoldRequested = true;
    w.Fleets[0].ReturnToBaseRequested = true;
}, Resume());
One("resume-empty-failure", w =>
{
    w.Fleets[0].HoldRequested = true;
    w.Fleets[0].ReturnToBaseFailureReason = null;
}, Resume());
One("resume-max-revision", w =>
{
    w.Fleets[0].HoldRequested = true;
    w.Fleets[0].MissionOrderRevision = int.MaxValue;
}, Resume());

void Paid(string name, Action<FleetState> setup, FleetRole role = FleetRole.Colony)
{
    One(name, w => { w.Fleets[0] = Fleet(role: role); setup(w.Fleets[0]); }, Preview());
}
Paid("paid-destination-body", f => f.DestinationPlanetaryBodyId = 100);
Paid("paid-settlement-body", f => f.SettlementBodyId = 101);
Paid("paid-progress-tenth-format", f => f.SettlementDaysCompleted = 1.25);
Paid("paid-progress-infinity", f => f.SettlementDaysCompleted = double.PositiveInfinity);
Paid("paid-progress-zero", f => f.SettlementDaysCompleted = 0);
Paid("paid-progress-negative", f => f.SettlementDaysCompleted = -1);
Paid("paid-progress-nan", f => f.SettlementDaysCompleted = double.NaN);
Paid("paid-fields-scout", f => f.SettlementBodyId = 101, FleetRole.Scout);

One("request-already-returning-before-paid", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].SettlementBodyId = 101;
}, Request());
One("request-paid-before-inlane", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].SettlementBodyId = 101;
}, Request());
One("request-confirmed-colony-queues-inlane", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].SettlementDaysCompleted = 2;
    w.Fleets[0].HoldRequested = true;
}, Request(confirm: true));
One("request-scout-queues-inlane", w =>
{
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].HoldRequested = true;
}, Request());
One("preview-inlane", w => w.Fleets[0].CurrentSystemId = null, Preview());
One("preview-paid-inlane", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].CurrentSystemId = null;
    w.Fleets[0].SettlementBodyId = 101;
}, Preview());

One("preview-no-colonies", w => w.Colonies.Clear(), Preview());
One("preview-foreign-only", w =>
{
    w.Colonies[0] = new ColonyState { Id = 1, CivilizationId = 2, SystemId = 10, Name = "Foreign Sol" };
    w.Colonies[1] = new ColonyState { Id = 2, CivilizationId = 2, SystemId = 20, Name = "Foreign Alpha" };
}, Preview());
One("preview-duplicate-colony-system", w => w.Colonies.Add(new ColonyState { Id = 3, CivilizationId = 1, SystemId = 20, Name = "Duplicate Alpha" }), Preview());
One("preview-equal-distance-tie-system-id", w =>
{
    w.Colonies.Clear();
    w.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1, SystemId = 40, Name = "Twin colony" });
    w.Colonies.Add(new ColonyState { Id = 2, CivilizationId = 1, SystemId = 20, Name = "Alpha colony" });
}, Preview());
One("preview-outpost-half-service-origin", w => w.Colonies[0].Kind = SettlementKind.ResourceOutpost, Preview());
One("preview-foreign-service-ignored", w =>
{
    w.Fleets[0].FuelRemainingLightYears = 0;
    w.Colonies[0] = new ColonyState { Id = 1, CivilizationId = 2, SystemId = 10, Name = "Foreign Sol" };
    w.Colonies[1] = new ColonyState { Id = 2, CivilizationId = 2, SystemId = 20, Name = "Foreign Alpha" };
    w.Colonies.Add(new ColonyState { Id = 4, CivilizationId = 1, SystemId = 20, Name = "Remote" });
}, Preview());
One("request-immediate-no-base-noop", w =>
{
    w.Colonies.Clear();
    w.Fleets[0].HoldRequested = true;
}, Request(confirm: true));

One("request-same-system-scout", w => w.Colonies.RemoveAt(1), Request());
One("request-same-system-colony", w =>
{
    w.Colonies.RemoveAt(1);
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].DestinationPlanetaryBodyId = 102;
    w.Fleets[0].SettlementDaysCompleted = 2;
}, Request(confirm: true));
One("request-same-system-local-arrival", w =>
{
    w.Colonies.RemoveAt(1);
    w.Fleets[0].TransitPhase = FleetTransitPhase.LocalArrival;
}, Request());
One("request-remote-scout", w => w.Colonies.RemoveAt(0), Request());
One("request-remote-science-local-arrival", w =>
{
    w.Colonies.RemoveAt(0);
    w.Fleets[0] = Fleet(role: FleetRole.Science);
    w.Fleets[0].TransitPhase = FleetTransitPhase.LocalArrival;
}, Request());
One("request-remote-colony", w =>
{
    w.Colonies.RemoveAt(0);
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].SettlementDaysCompleted = 2;
}, Request(confirm: true));

One("activate-no-pending", null, Activate());
One("activate-pending-still-inlane", w =>
{
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].CurrentSystemId = null;
}, Activate());
One("activate-direct-inactive", w =>
{
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].IsActive = false;
}, Activate());
One("activate-direct-military", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Military);
    w.Fleets[0].ReturnToBaseRequested = true;
}, Activate());
One("activate-queued-failure", w =>
{
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Colonies.Clear();
    w.Fleets[0].TransitPhase = FleetTransitPhase.LocalArrival;
}, Activate());
One("activate-queued-remote", w =>
{
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Colonies.RemoveAt(0);
    w.Fleets[0].TransitPhase = FleetTransitPhase.LocalArrival;
}, Activate());
One("activate-queued-same-system", w =>
{
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Colonies.RemoveAt(1);
}, Activate());

One("abandon-four-fields", w =>
{
    w.Fleets[0].DestinationPlanetaryBodyId = 100;
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].SettlementDaysCompleted = 9;
}, AbandonCommand());

void RevisionBoundary(string name, FleetRole role, bool remote)
{
    var world = World(Fleet(role: role));
    if (remote) world.Colonies.RemoveAt(0); else world.Colonies.RemoveAt(1);
    world.Fleets[0].MissionOrderRevision = int.MaxValue;
    if (role == FleetRole.Colony)
    {
        world.Fleets[0].SettlementBodyId = 101;
        world.Fleets[0].DestinationPlanetaryBodyId = 102;
        world.Fleets[0].SettlementDaysCompleted = 3;
    }
    var nativeWorld = World(Fleet(role: role));
    if (remote) nativeWorld.Colonies.RemoveAt(0); else nativeWorld.Colonies.RemoveAt(1);
    nativeWorld.Fleets[0].MissionOrderRevision = int.MaxValue;
    if (role == FleetRole.Colony)
    {
        nativeWorld.Fleets[0].SettlementBodyId = 101;
        nativeWorld.Fleets[0].DestinationPlanetaryBodyId = 102;
        nativeWorld.Fleets[0].SettlementDaysCompleted = 3;
        ColonizationSimulation.AbandonMissionForTransit(nativeWorld.Fleets[0]);
    }
    Add(name, world, new[] { Request(confirm: true) }, new
    {
        Error = new { Type = "OverflowError", Message = "Fleet mission revision space is exhausted." },
        After = Freeze(State(nativeWorld)),
    });
}
RevisionBoundary("revision-max-same-scout", FleetRole.Scout, false);
RevisionBoundary("revision-max-same-colony", FleetRole.Colony, false);
RevisionBoundary("revision-max-remote-science", FleetRole.Science, true);
RevisionBoundary("revision-max-remote-colony", FleetRole.Colony, true);

One("hold-max-revision", w => w.Fleets[0].MissionOrderRevision = int.MaxValue, Hold());
One("preview-max-revision", w => w.Fleets[0].MissionOrderRevision = int.MaxValue, Preview());
One("request-inlane-max-revision", w =>
{
    w.Fleets[0].MissionOrderRevision = int.MaxValue;
    w.Fleets[0].CurrentSystemId = null;
}, Request());
One("preview-tie-colonies-reversed", w =>
{
    w.Colonies.Clear();
    w.Colonies.Add(new ColonyState { Id = 2, CivilizationId = 1, SystemId = 20, Name = "Alpha colony" });
    w.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1, SystemId = 40, Name = "Twin colony" });
}, Preview());
One("preview-zero-current-fuel", w =>
{
    w.Colonies.RemoveAt(0);
    w.Fleets[0].FuelRemainingLightYears = 0;
}, Preview());
One("preview-exact-current-fuel", w =>
{
    w.Colonies.RemoveAt(0);
    w.Fleets[0].FuelRemainingLightYears = 10;
}, Preview());
One("preview-short-current-fuel", w =>
{
    w.Colonies.RemoveAt(0);
    w.Fleets[0].FuelRemainingLightYears = 9.999;
}, Preview());
One("preview-outpost-remote-candidate", w =>
{
    w.Colonies.RemoveAt(0);
    w.Colonies[0].Kind = SettlementKind.ResourceOutpost;
}, Preview());
One("preview-candidate-source-order-materialized", w =>
{
    w.Colonies.Clear();
    w.Colonies.Add(new ColonyState { Id = 3, CivilizationId = 1, SystemId = 30, Name = "Beta first" });
    w.Colonies.Add(new ColonyState { Id = 2, CivilizationId = 1, SystemId = 20, Name = "Alpha second" });
}, Preview());
One("preview-unknown-candidate-then-valid", w =>
{
    w.Colonies.Clear();
    w.Colonies.Add(new ColonyState { Id = 9, CivilizationId = 1, SystemId = 999, Name = "Unknown" });
    w.Colonies.Add(new ColonyState { Id = 2, CivilizationId = 1, SystemId = 20, Name = "Alpha" });
}, Preview());
One("preview-duplicate-system-graph-error", w =>
{
    var systems = (StarSystemState[])w.Systems;
    systems[1] = systems[1] with { Id = 10 };
    w.Colonies.Clear();
    w.Colonies.Add(new ColonyState { Id = 9, CivilizationId = 1, SystemId = 999, Name = "Unknown first" });
    w.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1, SystemId = 10, Name = "Known second" });
}, Preview());
One("activate-arrival-intermediate-remote", w =>
{
    w.Fleets[0].CurrentSystemId = 20;
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].TransitPhase = FleetTransitPhase.LocalArrival;
    w.Colonies.RemoveAt(1);
}, Activate());
One("activate-colony-failure-preserves-work", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].SettlementDaysCompleted = 4;
    w.Colonies.Clear();
}, Activate());
One("activate-colony-remote-abandons-work", w =>
{
    w.Fleets[0] = Fleet(role: FleetRole.Colony);
    w.Fleets[0].ReturnToBaseRequested = true;
    w.Fleets[0].SettlementBodyId = 101;
    w.Fleets[0].DestinationPlanetaryBodyId = 102;
    w.Fleets[0].SettlementDaysCompleted = 4;
    w.Colonies.RemoveAt(0);
}, Activate());

Add("null-galaxy-hold", null, new[] { Hold() with { NullGalaxy = true } });
Add("null-galaxy-request", null, new[] { Request() with { NullGalaxy = true } });
Add("null-galaxy-preview", null, new[] { Preview() with { NullGalaxy = true } });
var nullFleetWorld = World();
Add("null-fleet-abandon", nullFleetWorld, new[] { AbandonCommand() with { NullFleet = true } });
var nullActivateGalaxyWorld = World();
Add("null-galaxy-activate", nullActivateGalaxyWorld,
    new[] { Activate() with { NullGalaxy = true } });
var nullActivateFleetWorld = World();
Add("null-fleet-activate", nullActivateFleetWorld,
    new[] { Activate() with { NullFleet = true } });

if (cases.Count < 50 || cases.Count > 80)
    throw new InvalidOperationException($"Expected 50-80 cases, got {cases.Count}.");
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);
File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-civilian-recovery-oracle-v1",
    Cases = cases,
}, json) + Environment.NewLine);
Console.WriteLine($"Wrote {cases.Count} cases.");

static class FleetExtensions
{
    public static FleetState WithCivilization(this FleetState fleet, int civilizationId)
    {
        return new FleetState
        {
            Id = fleet.Id, CivilizationId = civilizationId, Name = fleet.Name,
            Role = fleet.Role, DesignId = fleet.DesignId, Position = fleet.Position,
            CurrentSystemId = fleet.CurrentSystemId, DestinationSystemId = fleet.DestinationSystemId,
            PlannedRouteSystemIds = fleet.PlannedRouteSystemIds.ToList(), IsActive = fleet.IsActive,
        };
    }
}

sealed record Command(
    string Operation,
    int CivilizationId = 0,
    int FleetId = 0,
    bool Confirm = false,
    int FleetIndex = 0,
    bool NullGalaxy = false,
    bool NullFleet = false);
