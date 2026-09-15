using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var jsonOptions = new JsonSerializerOptions
{
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, jsonOptions);

StarSystemState System(int id, float x, float y, double? depth = null,
    string? name = null, StarArchetype archetype = StarArchetype.Standard,
    StellarPrimaryClass? primary = StellarPrimaryClass.GYellowDwarf) =>
    new(id, name ?? $"System {id}", new Vector2(x, y), archetype, true, false,
        false, false, null, primary, null, null, depth, $"fixture:{id}");

PlanetaryBodyState Body(int id, int systemId, bool rare = false,
    bool anomaly = false, bool native = false, PlanetaryBodyKind kind = PlanetaryBodyKind.Planet) =>
    new(id, systemId, kind == PlanetaryBodyKind.Moon ? 1 : null, id,
        $"Body {id}", kind, 1, 1,
        new PlanetaryEnvironmentState(1, 288, 101.3,
            PlanetaryAtmosphereRegime.OxygenNitrogen,
            PlanetarySolventRegime.Water, 0.1, false, true),
        true, rare, anomaly, native);

CivilizationState Civilization(int id, bool player, int home) =>
    new(id, $"Civilization {id}", home, CivilizationArchetype.Adaptive,
        CivilizationTraits.Balanced, player, CivilizationDevelopmentStage.WarpCapable);

FleetState Fleet(int id = 10, int civilizationId = 1,
    FleetRole role = FleetRole.Scout, int? current = 0, int? destination = null,
    FleetTransitPhase phase = FleetTransitPhase.None,
    double speed = 22, double fuel = 1000, double capacity = 1000,
    float sensor = 4, double maximumLegRange = 5) => new()
{
    Id = id,
    CivilizationId = civilizationId,
    Name = $"Fleet {id}",
    Role = role,
    DesignId = role.ToString().ToLowerInvariant(),
    Position = current == 0 ? Vector2.Zero : current == 1 ? new(3, 0) : current == 2 ? new(6, 0) : new(0, 4),
    CurrentSystemId = current,
    DestinationSystemId = destination,
    TransitPhase = phase,
    StrategicSpeed = speed,
    MaximumLegRangeLightYears = maximumLegRange,
    FuelCapacityLightYears = capacity,
    FuelRemainingLightYears = fuel,
    SensorRange = sensor,
};

GalaxyState World(params FleetState[] fleets) => new()
{
    Seed = 25,
    Systems = new List<StarSystemState> { System(0, 0, 0, name: "Origin"), System(1, 3, 0, name: "Alpha"),
        System(2, 6, 0, 4, "Depth"), System(3, 0, 4, name: "Foreign") },
    PlanetaryBodies = [Body(12, 1, rare: true, anomaly: true, native: true),
        Body(10, 1, rare: true), Body(11, 1, anomaly: true, kind: PlanetaryBodyKind.Moon)],
    Civilizations = [Civilization(1, true, 0), Civilization(2, false, 3)],
    Fleets = fleets.ToList(),
    Colonies = [],
    Economies = new List<CivilizationEconomyState> { new() { CivilizationId = 1 },
        new() { CivilizationId = 2 } },
    Technologies = new List<TechnologyState>(),
    ConstructionStates = new List<ConstructionState>(),
    ShipyardStates = new List<ShipyardState>(),
    PlayerCivilizationId = 1,
    Knowledge = new CivilizationKnowledgeState(),
};

object Observe(GalaxyState galaxy)
{
    var observerIds = galaxy.Civilizations.Select(c => c.Id).Distinct().ToArray();
    return new
    {
        Systems = galaxy.Systems.Select(s => new { s.Id, s.Name, Position = new { s.Position.X, s.Position.Y },
            s.Archetype, s.HasHabitableWorld, s.HasAnomaly, s.HasRareResource,
            s.HasPreWarpCivilization, s.CatalogPresetId, s.StellarClass,
            s.SecondaryStellarClass, s.TertiaryStellarClass,
            s.GalacticDepthLightYears, s.StellarCatalogId }).ToArray(),
        Bodies = galaxy.PlanetaryBodies.Select(b => new { b.Id, b.SystemId, b.ParentBodyId,
            b.OrbitIndex, b.Name, b.Kind, b.RadiusEarth, b.MassEarth,
            Environment = new { b.Environment.GravityG, b.Environment.TemperatureKelvin,
                b.Environment.PressureKPa, b.Environment.Atmosphere,
                b.Environment.AvailableSolvent, b.Environment.RadiationHazard,
                b.Environment.IsImmersedEnvironment, b.Environment.HasSolidSurface },
            b.LegacyColonizationCandidate, b.HasRareResource, b.HasAnomaly,
            b.HasPreWarpCivilization, b.OrbitalEccentricity,
            b.OrbitalInclinationDegrees }).ToArray(),
        Civilizations = galaxy.Civilizations.Select(c => new { c.Id, c.Name,
            c.HomeSystemId, c.Archetype, Traits = c.Traits, c.IsPlayer,
            c.DevelopmentStage, c.IsSeededAncient, c.ExpansionAllowed,
            c.NeutralUnlessProvoked, c.SpeciesId }).ToArray(),
        Fleets = galaxy.Fleets,
        Colonies = galaxy.Colonies,
        Economies = galaxy.Economies,
        Knowledge = observerIds.Select(id => new
        {
            CivilizationId = id,
            KnownSystems = galaxy.Knowledge.GetKnownSystems(id),
            KnownCivilizations = galaxy.Knowledge.GetKnownCivilizations(id),
            Surveys = galaxy.Knowledge.GetSystemSurveyKnowledge(id),
            CoreAccess = galaxy.Knowledge.HasGalacticCoreAccess(id),
            CoreDiscovered = galaxy.Knowledge.IsGalacticCoreDiscovered(id),
        }).ToArray(),
        CoreObservers = galaxy.Knowledge.GetGalacticCoreObservers(),
    };
}

void Add(string name, double delta, Action<GalaxyState> arrange)
{
    var galaxy = World(Fleet());
    arrange(galaxy);
    var before = Freeze(Observe(galaxy));
    var arguments = Freeze(new { SimulationDelta = delta, World = before });
    IReadOnlyList<ExplorationEvent>? events = null;
    object? error = null;
    try { events = new ExplorationSimulation().Advance(galaxy, delta); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    object? result = events is null ? null : Freeze(events);
    var after = Freeze(Observe(galaxy));
    cases.Add(new { Name = name, Kind = "Advance", Arguments = arguments,
        Result = result, Error = error, Before = before, After = after });
}

void IdleKnown(GalaxyState galaxy, FleetRole role = FleetRole.Scout)
{
    galaxy.Fleets[0] = Fleet(role: role);
    galaxy.Knowledge.MarkSystemFullySurveyed(1, 0);
}

foreach (var delta in new[] { -1.0, double.NaN, double.PositiveInfinity, double.NegativeInfinity })
    Add($"invalid-delta-{Freeze(delta)}", delta, _ => { });
foreach (var role in Enum.GetValues<FleetRole>())
    Add($"zero-{role}", 0, g => g.Fleets[0] = Fleet(role: role));

foreach (var funding in new[] { 0.0, 1e-8, 1e-7, 1.0000001e-7, .25, 1.0, 2.0,
                                -1.0, double.NaN, double.PositiveInfinity })
    Add($"funding-{Freeze(funding)}", 1, g =>
    {
        g.Economies[0].LastBaseOperationsFundingFraction = funding;
        g.Fleets[0] = Fleet(role: FleetRole.Scout, fuel: 1, capacity: 20);
        g.Colonies.Add(new ColonyState { Id = 1, CivilizationId = 1,
            SystemId = 0, Name = "Home", Kind = SettlementKind.Colony });
    });

foreach (var service in new[] { "none", "foreign", "outpost", "colony", "mixed" })
    foreach (var fuel in new[] { 0.0, 7.0 })
        Add($"refuel-{service}-{fuel}", .25, g =>
        {
            IdleKnown(g, FleetRole.Military);
            g.Fleets[0] = Fleet(role: FleetRole.Military, fuel: fuel, capacity: 10);
            if (service is "foreign") g.Colonies.Add(new ColonyState { Id = 2, CivilizationId = 2, SystemId = 0, Name = "Foreign" });
            if (service is "outpost" or "mixed") g.Colonies.Add(new ColonyState { Id = 3, CivilizationId = 1, SystemId = 0, Name = "Outpost", Kind = SettlementKind.ResourceOutpost });
            if (service is "colony" or "mixed") g.Colonies.Add(new ColonyState { Id = 4, CivilizationId = 1, SystemId = 0, Name = "Colony", Kind = SettlementKind.Colony });
        });

foreach (var completed in new[] { 0.0, .5, 1.999999998, 2.0, 5.0 })
    Add($"scout-progress-{completed}", .5, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout, current: 1);
        g.Fleets[0].ReconnaissanceSystemId = completed == 5 ? 0 : 1;
        g.Fleets[0].ReconnaissanceDaysCompleted = completed;
        g.Knowledge.RevealSystem(1, 1);
    });
Add("scout-already-partial", 1, g => { g.Fleets[0] = Fleet(role: FleetRole.Scout, current: 1); g.Knowledge.RecordReconnaissance(1, 1); });
Add("scout-contact-order", 2, g => { g.Fleets[0] = Fleet(role: FleetRole.Scout, current: 1); g.Fleets.Add(Fleet(20, 2, FleetRole.Military, 1)); });

foreach (var delta in new[] { 1e-8, .5, 5.0, 20.0, 100.0 })
    Add($"science-delta-{delta}", delta, g => { g.Fleets[0] = Fleet(role: FleetRole.Science, current: 1); g.Knowledge.RevealSystem(1, 1); });
foreach (var progress in new[] { 0.2, .35, .9, .999999, 1.0 })
    Add($"science-existing-{progress}", 1, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Science, current: 1);
        if (progress >= 1) g.Knowledge.MarkSystemFullySurveyed(1, 1);
        else g.Knowledge.AdvanceSystemSurvey(1, 1, progress);
    });

foreach (var phase in Enum.GetValues<FleetTransitPhase>())
    Add($"hold-{phase}", 1, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout,
            current: phase == FleetTransitPhase.InterstellarWarp ? null : 0,
            destination: 1, phase: phase, speed: 3, fuel: 10);
        g.Fleets[0].HoldRequested = true;
        g.Fleets[0].TransitOriginSystemId = 0;
        g.Fleets[0].TransitTargetSystemId = 1;
        g.Fleets[0].LocalTransitStart = Vector2.Zero;
        g.Fleets[0].LocalTransitPosition = Vector2.Zero;
        g.Fleets[0].LocalTransitTarget = new(.82f, 0);
        g.Fleets[0].PlannedRouteSystemIds = [1];
    });

foreach (var phase in new[] { FleetTransitPhase.LocalDeparture,
                              FleetTransitPhase.InterstellarWarp,
                              FleetTransitPhase.LocalArrival })
    Add($"resume-{phase}", 1, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout,
            current: phase == FleetTransitPhase.InterstellarWarp ? null :
                     phase == FleetTransitPhase.LocalArrival ? 1 : 0,
            destination: 1, phase: phase, speed: 3, fuel: 10);
        g.Fleets[0].TransitOriginSystemId = 0;
        g.Fleets[0].TransitTargetSystemId = 1;
        g.Fleets[0].LocalTransitStart =
            phase == FleetTransitPhase.LocalArrival ? new(-.82f, 0) : Vector2.Zero;
        g.Fleets[0].LocalTransitPosition = g.Fleets[0].LocalTransitStart;
        g.Fleets[0].LocalTransitTarget =
            phase == FleetTransitPhase.LocalDeparture ? new(.82f, 0) : Vector2.Zero;
        g.Fleets[0].PlannedRouteSystemIds = [1];
    });

foreach (var delta in new[] { .01, .5, 1.0, 2.0, 5.0, 20.0 })
    Add($"route-flat-{delta}", delta, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout, current: 0,
            destination: 1, speed: 3, fuel: 10);
        g.Fleets[0].PlannedRouteSystemIds = [1];
    });
foreach (var delta in new[] { .5, 2.0, 5.0, 20.0 })
    Add($"route-depth-{delta}", delta, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Science, current: 1,
            destination: 2, speed: 4, fuel: 20);
        g.Fleets[0].PlannedRouteSystemIds = [2];
    });
foreach (var fuel in new[] { 0.0, 1.0, 3.0, 30.0 })
    Add($"warp-fuel-{fuel}", 5, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout, current: null,
            destination: 1, phase: FleetTransitPhase.InterstellarWarp,
            speed: 3, fuel: fuel, capacity: 30);
        g.Fleets[0].Position = Vector2.Zero;
        g.Fleets[0].TransitOriginSystemId = 0;
        g.Fleets[0].TransitTargetSystemId = 1;
        g.Fleets[0].PlannedRouteSystemIds = [1];
    });

foreach (var delta in new[] { 2.0, 4.0, 8.0, 20.0 })
    Add($"multi-leg-{delta}", delta, g =>
    {
        g.Fleets[0] = Fleet(role: FleetRole.Scout, current: 0,
            destination: 2, speed: 4, fuel: 30, capacity: 30,
            maximumLegRange: 5);
        g.Fleets[0].PlannedRouteSystemIds = [1, 2];
        g.Colonies.Add(new ColonyState { Id = 5, CivilizationId = 1,
            SystemId = 1, Name = "Relay", Kind = SettlementKind.ResourceOutpost });
    });

Add("legacy-inflight-shape", 1, g => { g.Fleets[0] = Fleet(role: FleetRole.Scout, current: null, destination: 1, phase: FleetTransitPhase.None, speed: 3); g.Fleets[0].Position = new(1, 0); });
Add("destinationless-local-arrival", 1, g => { g.Fleets[0] = Fleet(role: FleetRole.Military, current: 1, phase: FleetTransitPhase.LocalArrival); g.Fleets[0].LocalTransitStart = new(.82f, 0); g.Fleets[0].LocalTransitPosition = new(.4f, 0); });
Add("inactive-ignored", 10, g => g.Fleets[0].IsActive = false);
Add("tactical-history-preserved", 3, g =>
{
    g.Fleets[0] = Fleet(role: FleetRole.Military, current: 0, fuel: 17);
    g.Fleets[0].Combat = new FleetCombatState
    {
        ProfileId = "fixture-combat",
        Shields = 11.5,
        Armor = 22.5,
        Hull = 33.5,
        WeaponCooldownRemainingDays = 1.25,
        Order = MilitaryOrderType.Retreat,
        TargetFleetId = 71,
        DefendSystemId = 2,
        RetreatProgressDays = 4.5,
        RetreatStarted = true,
        IsDisengaged = true,
        DisengagedSystemId = 3,
    };
    g.Fleets[0].TacticalLoadout = new MassiveCombatLoadout
    {
        MassPerShip = 101,
        Acceleration = 19,
        MaximumSpeed = 121,
        ShieldPerShip = 36,
        ArmorPerShip = 46,
        HullPerShip = 96,
        ReactorOutputPerShip = 102,
        CoolingPerShip = 29,
        WarpStabilization = 51,
        WarpSpoolSeconds = 13,
        ModuleSlotCapacity = 14,
        MaximumModuleMass = 421,
    };
    g.Fleets[0].TacticalVessel = new MassiveVesselState
    {
        Id = 9001,
        Name = "Remembered flagship",
        DesignId = "fixture-design",
        IsFlagship = true,
        IsCarrier = true,
        IsInterdictor = true,
        IsStoryShip = true,
        HullFraction = .91f,
        EngineFraction = .82f,
        SensorFraction = .73f,
        WarpDriveFraction = .64f,
        ReactorFraction = .55f,
        InterdictorFraction = .46f,
        BattlesFought = 12,
        ConfirmedKills = 34,
        Destroyed = false,
        Escaped = true,
    };
});
Add("missing-owner", 1, g => g.Fleets[0] = Fleet(civilizationId: 99));
Add("missing-economy-default-funding", 1, g => { ((List<CivilizationEconomyState>)g.Economies).RemoveAt(0); g.Fleets[0] = Fleet(role:FleetRole.Scout,current:1); });
Add("missing-movement-target-after-refuel", 1, g => { g.Fleets[0] = Fleet(role:FleetRole.Military,current:0,destination:99); g.Colonies.Add(new ColonyState { Id=30,CivilizationId=1,SystemId=0,Name="Home"}); });
Add("missing-origin-after-target-lookup", 1, g => { g.Fleets[0] = Fleet(role:FleetRole.Military,current:99,destination:1); });
Add("prior-fleet-mutation-before-owner-error", 1, g => { g.Fleets[0] = Fleet(role:FleetRole.Military,current:0,fuel:1,capacity:10); g.Colonies.Add(new ColonyState {Id=31,CivilizationId=1,SystemId=0,Name="Home"}); g.Fleets.Add(Fleet(88,99,FleetRole.Military,0)); });
Add("completed-destinationless-local-arrival", 5, g => { g.Fleets[0] = Fleet(role:FleetRole.Military,current:1,phase:FleetTransitPhase.LocalArrival); g.Fleets[0].TransitOriginSystemId=0; g.Fleets[0].TransitTargetSystemId=1; g.Fleets[0].LocalTransitStart=new(.82f,0); g.Fleets[0].LocalTransitPosition=new(.00001f,0); });
Add("already-known-contact-suppressed", 2, g => { g.Fleets[0]=Fleet(role:FleetRole.Scout,current:1); g.Fleets.Add(Fleet(32,2,FleetRole.Military,1)); g.Knowledge.RevealCivilization(1,2); });
Add("duplicate-system-first", 2, g => { var systems=(List<StarSystemState>)g.Systems; systems.Clear(); systems.AddRange([System(0,0,0,name:"First"), System(0,9,9,name:"Second"), System(1,3,0)]); g.Fleets[0] = Fleet(current:0,destination:1,speed:3); g.Fleets[0].PlannedRouteSystemIds=[1]; });
Add("one-way-contact-colony", 2, g => { g.Fleets[0] = Fleet(role:FleetRole.Scout,current:1); g.Colonies.Add(new ColonyState { Id=7,CivilizationId=2,SystemId=1,Name="Foreign colony"}); });
Add("one-way-contact-inactive-foreign", 2, g => { g.Fleets[0] = Fleet(role:FleetRole.Scout,current:1); g.Fleets.Add(Fleet(21,2,FleetRole.Military,1)); g.Fleets[1].IsActive=false; });
Add("ai-scout", 1, g => { g.Fleets[0] = Fleet(civilizationId:2,role:FleetRole.Scout,current:3); g.Knowledge.MarkSystemFullySurveyed(2,3); });
Add("ai-science", 1, g => { g.Fleets[0] = Fleet(civilizationId:2,role:FleetRole.Science,current:3); g.Knowledge.MarkSystemFullySurveyed(2,3); });
Add("queued-return-inbound", 5, g =>
{
    g.Fleets[0] = Fleet(role:FleetRole.Scout,current:null,destination:1,
        phase:FleetTransitPhase.InterstellarWarp,speed:3,fuel:10);
    g.Fleets[0].Position=Vector2.Zero;
    g.Fleets[0].TransitOriginSystemId=0;
    g.Fleets[0].TransitTargetSystemId=1;
    g.Fleets[0].PlannedRouteSystemIds=[1];
    g.Fleets[0].ReturnToBaseRequested=true;
    g.Colonies.Add(new ColonyState { Id=8,CivilizationId=1,SystemId=0,Name="Home"});
});
Add("queued-return-no-reachable-base", 5, g =>
{
    g.Fleets[0] = Fleet(role:FleetRole.Scout,current:null,destination:1,
        phase:FleetTransitPhase.InterstellarWarp,speed:3,fuel:10);
    g.Fleets[0].Position=Vector2.Zero;
    g.Fleets[0].TransitOriginSystemId=0;
    g.Fleets[0].TransitTargetSystemId=1;
    g.Fleets[0].PlannedRouteSystemIds=[1];
    g.Fleets[0].ReturnToBaseRequested=true;
});

var nullErrors = new List<object>();
foreach (var delta in new[] { 1.0, 0.0, -1.0 })
{
    object? error = null;
    try { _ = new ExplorationSimulation().Advance(null!, delta); }
    catch (Exception exception) { error = new { Type=exception.GetType().Name, exception.Message }; }
    nullErrors.Add(new { SimulationDelta=delta, Error=error });
}

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-exploration-advance-oracle-v1",
    Cases = cases,
    SourceOnlyNullGalaxy = nullErrors,
    NativeBoundary = new
    {
        NullGalaxy = "Typed native world references cannot represent null.",
        NonFiniteGeometry = "Existing native physical-state imports reject nonfinite coordinates before advancement.",
        MissionRevisionOverflow = "Native route mutation rejects Int32 exhaustion before undefined signed overflow.",
    },
}, jsonOptions) + Environment.NewLine);
