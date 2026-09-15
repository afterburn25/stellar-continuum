using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

if(args.Length!=1)throw new ArgumentException("Expected fixture path.");
CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
var json=new JsonSerializerOptions{WriteIndented=true,IncludeFields=true,NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals};
JsonElement Freeze(object?o)=>JsonSerializer.SerializeToElement(o,json);
StarSystemState System(int id)=>new(id,"System "+id,new Vector2(id,0),StarArchetype.Standard,false,false,false,false);
FleetCombatState Combat(string profile="patrol_corvette_mk1")=>new(){ProfileId=profile,Shields=35,Armor=45,Hull=95,WeaponCooldownRemainingDays=0,Order=MilitaryOrderType.Hold};
FleetState Fleet(int id=1,int civ=1,string name="Alpha",FleetRole role=FleetRole.Military,int? system=10)=>new(){Id=id,CivilizationId=civ,Name=name,Role=role,DesignId="design",Position=new(1,2),CurrentSystemId=system,DestinationSystemId=20,TransitPhase=FleetTransitPhase.None,TransitOriginSystemId=9,TransitTargetSystemId=10,TransitProgress=.2,LocalTransitStart=new(.1f,.2f),LocalTransitPosition=new(.3f,.4f),LocalTransitTarget=new(.5f,.6f),PlannedRouteSystemIds=new(){20},HoldRequested=true,ReturnToBaseRequested=true,ReturnToBaseFailureReason="old",MissionOrderRevision=4,DestinationPlanetaryBodyId=77,PreventAutomaticSettlement=true,SettlementBodyId=78,SettlementDaysCompleted=2,ReconnaissanceSystemId=10,ReconnaissanceDaysCompleted=3,FreightTargetOutpostId=30,FreightHomeColonyId=20,CargoMaterialCapacity=100,CargoMaterials=5,StrategicSpeed=22,MaximumLegRangeLightYears=50,FuelCapacityLightYears=100,FuelRemainingLightYears=80,SensorRange=90,IsActive=true,EmbarkedPopulationMillions=4.125,EmbarkedPopulationSpeciesId="terran_baseline",Combat=Combat(),TacticalVessel=new MassiveVesselState{Id=id,Name=name+" vessel",DesignId="design",IsFlagship=true,HullFraction=.8f,EngineFraction=.7f,SensorFraction=.6f,WarpDriveFraction=.5f,ReactorFraction=.4f,InterdictorFraction=.3f,BattlesFought=3,ConfirmedKills=2}};
GalaxyState World()=>new(){Seed=30,Systems=new[]{System(10),System(20)},PlanetaryBodies=Array.Empty<PlanetaryBodyState>(),Civilizations=new List<CivilizationState>(),Fleets=new List<FleetState>{Fleet(),Fleet(2,2,"Beta")},Colonies=new List<ColonyState>(),Economies=Array.Empty<CivilizationEconomyState>(),Technologies=new List<Game.Simulation.Research.TechnologyState>(),ConstructionStates=new List<Game.Simulation.Construction.ConstructionState>(),ShipyardStates=new List<Game.Simulation.Shipbuilding.ShipyardState>(),PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState()};
object Snapshot(GalaxyState w)=>new{w.Systems,w.Fleets};
var cases = new List<object>();
void Add(string name, Action<GalaxyState>? setup, Command[] commands, bool defaultHostile = true,
    bool[]? hostilityOutcomes = null, int[]? throwCalls = null, bool useDefaultRuntime = false)
{
    var world = World();
    setup?.Invoke(world);
    var hostility = new SequencedHostility(defaultHostile, hostilityOutcomes ?? Array.Empty<bool>(), throwCalls ?? Array.Empty<int>());
    var runtime = useDefaultRuntime ? new CombatCommandRuntime() : new CombatCommandRuntime(hostility);
    var arguments = Freeze(new { World = Snapshot(world), Commands = commands, DefaultHostile = defaultHostile,
        HostilityOutcomes = hostilityOutcomes ?? Array.Empty<bool>(), ThrowCalls = throwCalls ?? Array.Empty<int>(), UseDefaultRuntime = useDefaultRuntime });
    var before = Freeze(Snapshot(world));
    var results = new List<object>();
    foreach (var command in commands)
    {
        Func<object?> operation = command.Kind switch
        {
            "Preview" => () => runtime.PreviewOrder(world, command.CivilizationId, command.FleetId,
                new MilitaryOrder(command.OrderType, command.TargetFleetId, command.DefendSystemId)),
            "PreviewBatch" => () => runtime.PreviewOrders(world, command.CivilizationId, command.FleetIds,
                new MilitaryOrder(command.OrderType, command.TargetFleetId, command.DefendSystemId)),
            "Issue" => () => runtime.IssueOrder(world, command.CivilizationId, command.FleetId,
                new MilitaryOrder(command.OrderType, command.TargetFleetId, command.DefendSystemId)),
            "IssueBatch" => () => runtime.IssueOrders(world, command.CivilizationId, command.FleetIds,
                new MilitaryOrder(command.OrderType, command.TargetFleetId, command.DefendSystemId)),
            "Engage" => () => runtime.IssueEngageHostiles(world, command.CivilizationId, command.FleetId),
            "Advance" => () => runtime.Simulation.Advance(world, command.Days),
            _ => throw new InvalidOperationException("Unknown oracle command kind."),
        };
        object? raw = null;
        Exception? caught = null;
        try { raw = operation(); }
        catch (Exception error) { caught = error; }
        var result = Freeze(raw);
        var sourceError = caught is null ? (JsonElement?)null : Freeze(new { Type = caught.GetType().Name, caught.Message });
        results.Add(new { Command = Freeze(command), Result = result, Error = sourceError, After = Freeze(Snapshot(world)) });
    }
    cases.Add(new { Name = name, Arguments = arguments, Before = before, Result = Freeze(results),
        After = Freeze(Snapshot(world)), HostilityCalls = Freeze(hostility.Calls) });
}
Command Preview(MilitaryOrderType type, int fleet = 1, int civ = 1, int? target = null, int? defend = null) =>
    new("Preview", civ, fleet, type, target, defend);
Command PreviewBatch(int[] ids, MilitaryOrderType type, int civ = 1, int? target = null, int? defend = null) =>
    new("PreviewBatch", civ, FleetIds: ids, OrderType: type, TargetFleetId: target, DefendSystemId: defend);
Command Issue(MilitaryOrderType type, int fleet = 1, int civ = 1, int? target = null, int? defend = null) =>
    new("Issue", civ, fleet, type, target, defend);
Command IssueBatch(int[] ids, MilitaryOrderType type, int civ = 1, int? target = null, int? defend = null) =>
    new("IssueBatch", civ, FleetIds: ids, OrderType: type, TargetFleetId: target, DefendSystemId: defend);
Command Engage(int fleet = 1, int civ = 1) => new("Engage", civ, fleet);
Command Advance(double days) => new("Advance", Days: days);

// Read-only preview rules and immutable fallback-profile behavior.
Add("preview-hold", null, new[] { Preview(MilitaryOrderType.Hold) });
Add("preview-retreat", null, new[] { Preview(MilitaryOrderType.Retreat) });
Add("preview-missing", null, new[] { Preview(MilitaryOrderType.Hold, fleet: 99) });
Add("preview-inactive", w => w.Fleets[0].IsActive = false, new[] { Preview(MilitaryOrderType.Hold) });
Add("preview-wrong-owner", null, new[] { Preview(MilitaryOrderType.Hold, civ: 2) });
Add("preview-first-active-owned-duplicate", w => w.Fleets.Add(Fleet(name: "Duplicate")), new[] { Preview(MilitaryOrderType.Hold) });
Add("preview-first-inactive-duplicate-skipped", w => { w.Fleets[0].IsActive = false; w.Fleets.Add(Fleet(name: "Duplicate")); }, new[] { Preview(MilitaryOrderType.Hold) });
Add("preview-unknown-profile-hold-no-normalize", w => w.Fleets[0].Combat = Combat("unknown"), new[] { Preview(MilitaryOrderType.Hold) });
Add("preview-null-combat-scout-unarmed", w => { w.Fleets[0] = Fleet(role: FleetRole.Scout); w.Fleets[0].Combat = null; }, new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-defend-default", null, new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-null-combat-military-fallback", w => w.Fleets[0].Combat = null, new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-whitespace-profile-military-fallback", w => w.Fleets[0].Combat = Combat("   "), new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-unknown-profile-military-fallback", w => w.Fleets[0].Combat = Combat("missing-profile"), new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-defend-explicit", null, new[] { Preview(MilitaryOrderType.Defend, defend: 10) });
Add("preview-defend-unarmed", w => w.Fleets[0].Combat = Combat("civilian_light_v1"), new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-defend-no-current", w => w.Fleets[0].CurrentSystemId = null, new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-defend-other-system", null, new[] { Preview(MilitaryOrderType.Defend, defend: 20) });
Add("preview-defend-unknown-system", w => w.Fleets[0].CurrentSystemId = 99, new[] { Preview(MilitaryOrderType.Defend) });
Add("preview-attack-unarmed-before-target", w => w.Fleets[0].Combat = Combat("civilian_light_v1"), new[] { Preview(MilitaryOrderType.Attack) });
Add("preview-attack-missing-target", null, new[] { Preview(MilitaryOrderType.Attack) });
Add("preview-attack-unknown-target", null, new[] { Preview(MilitaryOrderType.Attack, target: 99) });
Add("preview-attack-friendly", w => w.Fleets[1] = Fleet(2, 1, "Beta"), new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-attack-inactive-target", w => w.Fleets[1].IsActive = false, new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-attack-other-system", w => w.Fleets[1].CurrentSystemId = 20, new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-attack-disengaged-valid-profile", w => { w.Fleets[1].Combat!.IsDisengaged = true; w.Fleets[1].Combat!.DisengagedSystemId = 10; }, new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-attack-disengaged-unknown-profile-ignored", w => { w.Fleets[1].Combat = Combat("unknown"); w.Fleets[1].Combat!.IsDisengaged = true; w.Fleets[1].Combat!.DisengagedSystemId = 10; }, new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-attack-peaceful", null, new[] { Preview(MilitaryOrderType.Attack, target: 2) }, defaultHostile: false);
Add("preview-attack-hostile", null, new[] { Preview(MilitaryOrderType.Attack, target: 2) });
Add("preview-unknown-enum", null, new[] { Preview((MilitaryOrderType)99) });

// Batch ordering, duplicate elimination, aggregate flags, and heterogeneous acceptance.
Add("preview-batch-empty", null, new[] { PreviewBatch(Array.Empty<int>(), MilitaryOrderType.Hold) });
Add("preview-batch-sorted-distinct", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { PreviewBatch(new[] { 3, 1, 3, 99, 1 }, MilitaryOrderType.Hold) });
Add("preview-batch-all-accepted", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { PreviewBatch(new[] { 3, 1 }, MilitaryOrderType.Hold) });
Add("preview-batch-all-rejected", null, new[] { PreviewBatch(new[] { 98, 99 }, MilitaryOrderType.Hold) });
Add("preview-batch-attack-callback-order", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { PreviewBatch(new[] { 3, 1, 3 }, MilitaryOrderType.Attack, target: 2) });
Add("issue-batch-empty", null, new[] { IssueBatch(Array.Empty<int>(), MilitaryOrderType.Hold) });
Add("issue-batch-sorted-distinct-partial", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { IssueBatch(new[] { 99, 3, 1, 3 }, MilitaryOrderType.Hold) });
Add("issue-batch-all-accepted", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { IssueBatch(new[] { 3, 1 }, MilitaryOrderType.Retreat) });
Add("issue-batch-attack-callback-order", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { IssueBatch(new[] { 3, 1, 3 }, MilitaryOrderType.Attack, target: 2) });
Add("issue-batch-callback-throw-after-first-mutation", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { IssueBatch(new[] { 3, 1 }, MilitaryOrderType.Attack, target: 2) }, throwCalls: new[] { 2 });

// Direct wrapper issuance proves all order kinds reach the retained simulation.
Add("issue-direct-hold", w => { var state = w.Fleets[0].Combat!; state.IsDisengaged = true; state.DisengagedSystemId = 10; state.RetreatProgressDays = 2; state.RetreatStarted = true; }, new[] { Issue(MilitaryOrderType.Hold) });
Add("issue-direct-defend", null, new[] { Issue(MilitaryOrderType.Defend) });
Add("issue-direct-attack-peaceful", null, new[] { Issue(MilitaryOrderType.Attack, target: 2) }, defaultHostile: false);
Add("issue-direct-retreat", null, new[] { Issue(MilitaryOrderType.Retreat) });
Add("issue-direct-unknown", null, new[] { Issue((MilitaryOrderType)99) });
Add("preview-batch-callback-throw-after-first-readonly", w => w.Fleets.Add(Fleet(3, 1, "Gamma")), new[] { PreviewBatch(new[] { 3, 1 }, MilitaryOrderType.Attack, target: 2) }, throwCalls: new[] { 2 });
// Engage-hostiles privacy, stable candidates, and shared stateful policy use.
Add("engage-missing-actor", null, new[] { Engage(99) });
Add("engage-actor-no-system", w => w.Fleets[0].CurrentSystemId = null, new[] { Engage() });
Add("engage-no-foreign", w => w.Fleets[1] = Fleet(2, 1, "Beta"), new[] { Engage() });
Add("engage-peaceful-generic", null, new[] { Engage() }, defaultHostile: false);
Add("engage-skips-inactive-low-id", w => { w.Fleets[1] = Fleet(4, 2, "Beta"); var low = Fleet(2, 2, "Low"); low.IsActive = false; w.Fleets.Add(low); }, new[] { Engage() });
Add("engage-stable-lowest-id", w => { w.Fleets[1] = Fleet(4, 2, "Beta"); w.Fleets.Add(Fleet(2, 3, "Low")); }, new[] { Engage() });
Add("engage-skip-disengaged-low-id", w => { w.Fleets[1] = Fleet(4, 2, "Beta"); var low = Fleet(2, 3, "Low"); low.Combat!.IsDisengaged = true; low.Combat!.DisengagedSystemId = 10; w.Fleets.Add(low); }, new[] { Engage() });
Add("engage-preview-accept-issue-reject-policy-changes", null, new[] { Engage() }, hostilityOutcomes: new[] { true, false });
Add("engage-preview-reject-second-candidate", w => w.Fleets.Add(Fleet(3, 3, "Gamma")), new[] { Engage() }, hostilityOutcomes: new[] { false, true, true });
Add("engage-issue-callback-throws", null, new[] { Engage() }, hostilityOutcomes: new[] { true }, throwCalls: new[] { 2 });

Add("default-runtime-peaceful-sequence", null, new[] { Preview(MilitaryOrderType.Attack, target: 2), Issue(MilitaryOrderType.Attack, target: 2), Engage(), Advance(.1) }, useDefaultRuntime: true);
// One retained runtime proves preview, issuance, engage, and advancement share policy and simulation history.
Add("sequence-preview-issue-advance-history", null, new[] { Preview(MilitaryOrderType.Attack, target: 2), Issue(MilitaryOrderType.Attack, target: 2), Advance(.1), Advance(.1), Issue(MilitaryOrderType.Hold), Advance(.1) });
Add("sequence-stateful-policy-across-surfaces", w => w.Fleets.Add(Fleet(3, 3, "Gamma")), new[] { Preview(MilitaryOrderType.Attack, target: 2), Issue(MilitaryOrderType.Attack, target: 2), Engage(), Advance(.1) }, hostilityOutcomes: new[] { true, false, false, true, true, true, true });
Add("sequence-preview-does-not-clear-legacy-state", w => { var state = w.Fleets[0].Combat!; state.IsDisengaged = true; state.DisengagedSystemId = 20; state.RetreatProgressDays = 4; state.RetreatStarted = true; }, new[] { Preview(MilitaryOrderType.Hold), Preview(MilitaryOrderType.Retreat) });
Add("sequence-policy-throws-after-earlier-issued-order", null, new[] { Issue(MilitaryOrderType.Hold), Preview(MilitaryOrderType.Attack, target: 2), Issue(MilitaryOrderType.Retreat) }, throwCalls: new[] { 1 });

var nullWorld = World();
var nullRuntime = new CombatCommandRuntime();
var nullOrder = new MilitaryOrder(MilitaryOrderType.Hold);
var nullObservations = new List<object>();
void ObserveNull(string name, Action operation)
{
    Exception? caught = null;
    try { operation(); }
    catch (Exception error) { caught = error; }
    if (caught is null) throw new InvalidOperationException($"Expected null observation {name} to throw.");
    nullObservations.Add(new { Name = name, Error = Freeze(new { Type = caught.GetType().Name, caught.Message }) });
}
ObserveNull("PreviewOrder-null-galaxy", () => nullRuntime.PreviewOrder(null!, 1, 1, nullOrder));
ObserveNull("PreviewOrder-null-order", () => nullRuntime.PreviewOrder(nullWorld, 1, 1, null!));
ObserveNull("PreviewOrders-null-galaxy", () => nullRuntime.PreviewOrders(null!, 1, Array.Empty<int>(), nullOrder));
ObserveNull("PreviewOrders-null-fleetIds", () => nullRuntime.PreviewOrders(nullWorld, 1, null!, nullOrder));
ObserveNull("PreviewOrders-null-order", () => nullRuntime.PreviewOrders(nullWorld, 1, Array.Empty<int>(), null!));
ObserveNull("IssueOrder-null-galaxy", () => nullRuntime.IssueOrder(null!, 1, 1, nullOrder));
ObserveNull("IssueOrder-null-order", () => nullRuntime.IssueOrder(nullWorld, 1, 1, null!));
ObserveNull("IssueOrders-null-galaxy", () => nullRuntime.IssueOrders(null!, 1, Array.Empty<int>(), nullOrder));
ObserveNull("IssueOrders-null-fleetIds", () => nullRuntime.IssueOrders(nullWorld, 1, null!, nullOrder));
ObserveNull("IssueOrders-null-order", () => nullRuntime.IssueOrders(nullWorld, 1, Array.Empty<int>(), null!));
ObserveNull("IssueEngageHostiles-null-galaxy", () => nullRuntime.IssueEngageHostiles(null!, 1, 1));
File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-combat-command-runtime-oracle-v1",
    Cases = cases,
    SourceOnlyObservations = nullObservations

}, json));

sealed record Command(string Kind, int CivilizationId = 0, int FleetId = 0,
    MilitaryOrderType OrderType = MilitaryOrderType.Hold, int? TargetFleetId = null,
    int? DefendSystemId = null, int[]? FleetIds = null, double Days = 0)
{
    public int[] FleetIds { get; init; } = FleetIds ?? Array.Empty<int>();
}

sealed class SequencedHostility(bool defaultResult, IReadOnlyList<bool> outcomes, IReadOnlyCollection<int> throwCalls) : ICombatHostilityView
{
    public List<object> Calls { get; } = new();
    public bool AreHostile(int first, int second)
    {
        var call = Calls.Count + 1;
        Calls.Add(new { FirstCivilizationId = first, SecondCivilizationId = second });
        if (throwCalls.Contains(call)) throw new InvalidOperationException("Hostility oracle failure.");
        return call <= outcomes.Count ? outcomes[call - 1] : defaultResult;
    }
}
