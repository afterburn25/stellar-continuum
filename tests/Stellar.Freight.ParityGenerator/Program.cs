using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var options = new JsonSerializerOptions { WriteIndented = true, IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);

StarSystemState System(int id, float x) => new(id, "System " + id, new(x, 0),
    StarArchetype.Standard, false, false, false, false);
FleetState Fleet() => new() {
    Id=7, CivilizationId=1, Name="Atlas", Role=FleetRole.Logistics,
    DesignId=ShipDesignRegistry.BulkFreighterId, Position=new(1,2), CurrentSystemId=10,
    DestinationSystemId=null, TransitPhase=FleetTransitPhase.None,
    TransitOriginSystemId=9, TransitTargetSystemId=10, TransitProgress=.25,
    LocalTransitStart=new(.1f,.2f), LocalTransitPosition=new(.3f,.4f), LocalTransitTarget=new(.5f,.6f),
    PlannedRouteSystemIds=new(){99}, HoldRequested=true, ReturnToBaseRequested=true,
    ReturnToBaseFailureReason="old", MissionOrderRevision=5, DestinationPlanetaryBodyId=77,
    PreventAutomaticSettlement=true, SettlementBodyId=78, SettlementDaysCompleted=2,
    ReconnaissanceSystemId=79, ReconnaissanceDaysCompleted=3,
    CargoMaterialCapacity=100, CargoMaterials=0, StrategicSpeed=17,
    MaximumLegRangeLightYears=100, FuelCapacityLightYears=200,
    FuelRemainingLightYears=150, SensorRange=85, IsActive=true,
    EmbarkedPopulationMillions=4, EmbarkedPopulationSpeciesId="terran_baseline"
};
ColonyState Home() => new(){ Id=20,CivilizationId=1,SystemId=10,Name="Home",Kind=SettlementKind.Colony,
    PopulationMillions=1000,Infrastructure=1,Stability=1 };
ColonyState Outpost() => new(){ Id=30,CivilizationId=1,SystemId=20,Name="Mine",Kind=SettlementKind.ResourceOutpost,
    PopulationMillions=.1,Infrastructure=1,Stability=1,StoredExtractedMaterials=50 };
GalaxyState World(PlanetaryBodyState? body=null) => new(){ Seed=26, Systems=new[]{System(10,0),System(20,10),System(30,20)},
    PlanetaryBodies=new[]{body??DepositBody()},
    Civilizations=new List<CivilizationState>{new(1,"One",10,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,true,CivilizationDevelopmentStage.WarpCapable)},
    Fleets=new List<FleetState>{Fleet()}, Colonies=new List<ColonyState>{Home(),Outpost()},
    Economies=new List<CivilizationEconomyState>{new(){CivilizationId=1,Industry=200,LastBaseOperationsFundingFraction=1}},
    Technologies=new List<TechnologyState>(), ConstructionStates=new List<ConstructionState>{new(){CivilizationId=1}},
    ShipyardStates=new List<ShipyardState>(), PlayerCivilizationId=1, Knowledge=new CivilizationKnowledgeState() };
PlanetaryBodyState DepositBody(int id=100,int system=20,bool rare=true,double mass=1) => new(id,system,null,0,"Ore world",
    PlanetaryBodyKind.Planet,1,mass,new PlanetaryEnvironmentState(1,288,101,
        PlanetaryAtmosphereRegime.OxygenNitrogen,PlanetarySolventRegime.Water,0,false,true),
    false,rare,false,false);
void AttachDeposit(GalaxyState world,bool powered=true,double? remaining=100) {
    world.Colonies[1].PlanetaryBodyId=100;
    world.Colonies[1].RemainingExtractableMaterials=remaining; world.Colonies[1].StoredExtractedMaterials=0;
    world.Colonies[1].PopulationMillions=powered?.1:0;
    world.Colonies[1].SurfaceBuildings.Add(Building("fabricator"));
}

object Snapshot(GalaxyState world) => new { world.Systems, world.Civilizations, world.PlanetaryBodies,
    world.Fleets, world.Colonies, world.Economies, world.ConstructionStates };
var cases = new List<object>();
void Add(string name,string kind,GalaxyState world,object arguments,Func<StubReach,object?> operation,
         bool supported=true,string reason="Reach ok.") {
    var stub=new StubReach(supported,reason);
    var frozenArguments=Freeze(new { World=Snapshot(world), Operation=arguments, Reach=new { supported,reason } });
    var before=Freeze(Snapshot(world)); object? raw=null; Exception? caught=null;
    try { raw=operation(stub); } catch(Exception e) { caught=e; }
    var result=Freeze(raw); var after=Freeze(Snapshot(world)); var calls=Freeze(stub.Calls);
    var error=caught is null?(JsonElement?)null:Freeze(new {Type=caught.GetType().Name,caught.Message});
    cases.Add(new{Name=name,Kind=kind,Arguments=frozenArguments,Before=before,Result=result,Error=error,After=after,ReachCalls=calls});
}

void Transit(string name,Action<GalaxyState>? setup=null,bool supported=true,int fleet=7,int civ=1,int target=20) {
 var w=World(); setup?.Invoke(w); Add(name,"Transit",w,new{CivilizationId=civ,FleetId=fleet,TargetSystemId=target},
  r=>new FreightSimulation(r).IssueTransitOrder(w,civ,fleet,target),supported,supported?"Reach ok.":"No route."); }
Transit("transit-supported"); Transit("transit-unsupported",supported:false);
Transit("transit-missing",fleet:999); Transit("transit-inactive",w=>w.Fleets[0].IsActive=false);
Transit("transit-foreign",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],civilization:2));
Transit("transit-wrong-role",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],role:FleetRole.Military));
Transit("transit-home-busy",w=>w.Fleets[0].FreightHomeColonyId=20);
Transit("transit-valid-second-duplicate",w=>{w.Fleets[0].IsActive=false;w.Fleets.Add(Fleet());});
Transit("transit-first-valid-duplicate",w=>w.Fleets.Add(CloneFleet(Fleet(),name:"Second")));
Transit("transit-null-current-supported",w=>w.Fleets[0].CurrentSystemId=null);
Transit("transit-empty-name",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],name:""));

void Collection(string name,Action<GalaxyState>? setup=null,bool supported=true,int fleet=7,int civ=1,int outpost=30,PlanetaryBodyState? body=null) {
 var w=World(body); setup?.Invoke(w); Add(name,"Collection",w,new{CivilizationId=civ,FleetId=fleet,OutpostId=outpost},
  r=>new FreightSimulation(r).IssueCollectionOrder(w,civ,fleet,outpost),supported,supported?"Collection ok.":"Too far."); }
Collection("collection-supported"); Collection("collection-unsupported",supported:false);
Collection("collection-missing",fleet:99); Collection("collection-inactive",w=>w.Fleets[0].IsActive=false);
Collection("collection-foreign",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],civilization:2));
Collection("collection-wrong-role",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],role:FleetRole.Scout));
Collection("collection-wrong-design",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],design:"scout_pathfinder_v1"));
Collection("collection-destination-busy",w=>w.Fleets[0].DestinationSystemId=20);
Collection("collection-positive-cargo-busy",w=>w.Fleets[0].CargoMaterials=.00000001);
Collection("collection-negative-cargo-idle",w=>w.Fleets[0].CargoMaterials=-1);
Collection("collection-nan-cargo-idle",w=>w.Fleets[0].CargoMaterials=double.NaN);
Collection("collection-between-legs",w=>w.Fleets[0].CurrentSystemId=null);
Collection("collection-no-home",w=>w.Colonies.RemoveAt(0));
Collection("collection-home-wrong-kind",w=>w.Colonies[0].Kind=SettlementKind.ResourceOutpost);
Collection("collection-outpost-missing",outpost:99);
Collection("collection-outpost-foreign",w=>w.Colonies[1]=CloneColony(w.Colonies[1],civilization:2));
Collection("collection-outpost-wrong-kind",w=>w.Colonies[1].Kind=SettlementKind.Colony);
Collection("collection-extraction-powered-zero-stored",w=>AttachDeposit(w));
Collection("collection-extraction-unpowered-zero-stored",w=>AttachDeposit(w,powered:false));
Collection("collection-deposit-depleted",w=>AttachDeposit(w,remaining:0));
Collection("collection-deposit-body-wrong-system",w=>{AttachDeposit(w);w.Colonies[1]=CloneColony(w.Colonies[1],system:30);});
Collection("collection-deposit-body-id-missing",w=>{AttachDeposit(w);w.Colonies[1].PlanetaryBodyId=999;});
Collection("collection-deposit-invalid-reserve",w=>AttachDeposit(w,remaining:-1));
Collection("collection-deposit-fallback-stored-nan",w=>{AttachDeposit(w,remaining:null);w.Colonies[1].StoredExtractedMaterials=double.NaN;});
Collection("collection-deposit-fallback-invalid-initial",w=>AttachDeposit(w,remaining:null),body:DepositBody(mass:-1));
Collection("collection-funding-negative",w=>w.Economies[0].LastBaseOperationsFundingFraction=-1);
Collection("collection-funding-above-one",w=>w.Economies[0].LastBaseOperationsFundingFraction=1.01);
Collection("collection-funding-nan",w=>w.Economies[0].LastBaseOperationsFundingFraction=double.NaN);
Collection("collection-capacity-tenth",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],capacity:12.25));
Collection("collection-home-first-duplicate",w=>w.Colonies.Insert(0,CloneColony(Home(),id:21,name:"First Home")));
Collection("collection-outpost-first-duplicate",w=>w.Colonies.Insert(1,CloneColony(Outpost(),name:"First Mine")));

void Advance(string name,Action<GalaxyState>? setup=null,double days=1,bool supported=true) {
 var w=World(); w.Fleets[0].FreightHomeColonyId=20; w.Fleets[0].FreightTargetOutpostId=30;
 w.Fleets[0].CurrentSystemId=20; setup?.Invoke(w); Add(name,"Advance",w,new{SimulationDays=days},
  r=>{new FreightSimulation(r).Advance(w,days);return (object?)null;},supported,supported?"Return ok.":"Return blocked."); }
foreach(var value in new[]{-1d,double.NaN,double.PositiveInfinity,double.NegativeInfinity}) Advance("advance-invalid-"+cases.Count,days:value);
Advance("advance-zero",days:0); Advance("advance-negative-zero",days:-0d);
Advance("advance-load-partial",w=>{w.Fleets[0]=CloneFleet(w.Fleets[0],capacity:100);w.Colonies[1].StoredExtractedMaterials=80;},days:.5);
Advance("advance-load-day-limited",w=>w.Colonies[1].StoredExtractedMaterials=80,days:2);
Advance("advance-load-storage-limited",w=>w.Colonies[1].StoredExtractedMaterials=3,days:2);
Advance("advance-load-capacity-limited",w=>{w.Fleets[0].CargoMaterials=98;w.Colonies[1].StoredExtractedMaterials=50;},days:2);
Advance("advance-load-return-blocked",w=>{w.Fleets[0].CargoMaterials=99;w.Colonies[1].StoredExtractedMaterials=50;},supported:false);
Advance("advance-load-return-supported",w=>{w.Fleets[0].CargoMaterials=99;w.Colonies[1].StoredExtractedMaterials=50;});
Advance("advance-inactive",w=>w.Fleets[0].IsActive=false);
Advance("advance-in-transit",w=>w.Fleets[0].TransitPhase=FleetTransitPhase.LocalArrival);
Advance("advance-has-destination",w=>w.Fleets[0].DestinationSystemId=10);
Advance("advance-no-home",w=>w.Fleets[0].FreightHomeColonyId=null);
Advance("advance-funding-zero",w=>w.Economies[0].LastBaseOperationsFundingFraction=0);
Advance("advance-funding-threshold",w=>w.Economies[0].LastBaseOperationsFundingFraction=.0000001);
Advance("advance-funding-above",w=>w.Economies[0].LastBaseOperationsFundingFraction=Math.BitIncrement(.0000001));
Advance("advance-funding-missing-default",w=>((List<CivilizationEconomyState>)w.Economies).Clear());
Advance("advance-funding-nan-zero",w=>w.Economies[0].LastBaseOperationsFundingFraction=double.NaN);
Advance("advance-home-missing",w=>w.Colonies.RemoveAt(0));
Advance("advance-outpost-missing",w=>w.Colonies.RemoveAt(1));
Advance("advance-not-at-outpost",w=>w.Fleets[0].CurrentSystemId=10);
Advance("advance-negative-stored",w=>w.Colonies[1].StoredExtractedMaterials=-2);
Advance("advance-nan-stored",w=>w.Colonies[1].StoredExtractedMaterials=double.NaN);
Advance("advance-nan-capacity",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],capacity:double.NaN));
Advance("advance-unload-partial",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;},days:1);
Advance("advance-unload-complete",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=3;},days:1);
Advance("advance-unload-not-home",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=30;w.Fleets[0].CargoMaterials=50;});
Advance("advance-unload-storage-full",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;w.Economies[0].Industry=1000;});
Advance("advance-unload-economy-missing",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;((List<CivilizationEconomyState>)w.Economies).Clear();});
Advance("advance-unload-civilization-missing",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;w.Civilizations.Clear();});
Advance("advance-unload-construction-missing",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;w.ConstructionStates.Clear();});
Advance("advance-unload-nan-industry",w=>{w.Fleets[0].FreightTargetOutpostId=null;w.Fleets[0].CurrentSystemId=10;w.Fleets[0].CargoMaterials=50;w.Economies[0].Industry=double.NaN;});

void Helper(string name,string kind,Action<GalaxyState>? setup,Func<GalaxyState,double> op) { var w=World(); setup?.Invoke(w); Add(name,kind,w,new{},_=>op(w)); }
Helper("cargo-known","CargoRate",null,w=>FreightSimulation.GetCargoTransferRatePerDay(w.Fleets[0]));
Helper("cargo-unknown-positive","CargoRate",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],design:"unknown",capacity:2),w=>FreightSimulation.GetCargoTransferRatePerDay(w.Fleets[0]));
Helper("cargo-unknown-zero","CargoRate",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],design:"unknown",capacity:0),w=>FreightSimulation.GetCargoTransferRatePerDay(w.Fleets[0]));
Helper("cargo-unknown-nan","CargoRate",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],design:"unknown",capacity:double.NaN),w=>FreightSimulation.GetCargoTransferRatePerDay(w.Fleets[0]));
Helper("cargo-known-zero-rate-fallback","CargoRate",w=>w.Fleets[0]=CloneFleet(w.Fleets[0],design:"scout_pathfinder_v1",capacity:2),w=>FreightSimulation.GetCargoTransferRatePerDay(w.Fleets[0]));
Helper("port-basic","PortRate",null,w=>FreightSimulation.GetPortTransferCapacityPerDay(w.Colonies[0]));
Helper("port-powered-terminal","PortRate",w=>w.Colonies[0].SurfaceBuildings.Add(Building("cargo_terminal")),w=>FreightSimulation.GetPortTransferCapacityPerDay(w.Colonies[0]));
Helper("port-unpowered-terminal","PortRate",w=>w.Colonies[0].SurfaceBuildings.Add(Building("cargo_terminal",enabled:false)),w=>FreightSimulation.GetPortTransferCapacityPerDay(w.Colonies[0]));
Helper("effective-basic","EffectiveRate",null,w=>FreightSimulation.GetEffectiveTransferRatePerDay(w.Fleets[0],w.Colonies[0]));
Helper("effective-powered-terminal","EffectiveRate",w=>w.Colonies[0].SurfaceBuildings.Add(Building("cargo_terminal")),w=>FreightSimulation.GetEffectiveTransferRatePerDay(w.Fleets[0],w.Colonies[0]));

File.WriteAllText(args[0],JsonSerializer.Serialize(new{Format="stellar-freight-oracle-v1",Cases=cases},options));

SurfaceBuildingState Building(string type,bool enabled=true)=>new(){Id=1,TypeId=type,IsComplete=true,IsEnabled=enabled,Condition=1};
FleetState CloneFleet(FleetState f,int? civilization=null,FleetRole? role=null,string? design=null,string? name=null,double? capacity=null)=>new(){
 Id=f.Id,CivilizationId=civilization??f.CivilizationId,Name=name??f.Name,Role=role??f.Role,DesignId=design??f.DesignId,
 Position=f.Position,CurrentSystemId=f.CurrentSystemId,DestinationSystemId=f.DestinationSystemId,TransitPhase=f.TransitPhase,
 TransitOriginSystemId=f.TransitOriginSystemId,TransitTargetSystemId=f.TransitTargetSystemId,TransitProgress=f.TransitProgress,
 LocalTransitStart=f.LocalTransitStart,LocalTransitPosition=f.LocalTransitPosition,LocalTransitTarget=f.LocalTransitTarget,
 PlannedRouteSystemIds=new(f.PlannedRouteSystemIds),HoldRequested=f.HoldRequested,ReturnToBaseRequested=f.ReturnToBaseRequested,
 ReturnToBaseFailureReason=f.ReturnToBaseFailureReason,MissionOrderRevision=f.MissionOrderRevision,
 DestinationPlanetaryBodyId=f.DestinationPlanetaryBodyId,PreventAutomaticSettlement=f.PreventAutomaticSettlement,
 SettlementBodyId=f.SettlementBodyId,SettlementDaysCompleted=f.SettlementDaysCompleted,ReconnaissanceSystemId=f.ReconnaissanceSystemId,
 ReconnaissanceDaysCompleted=f.ReconnaissanceDaysCompleted,FreightTargetOutpostId=f.FreightTargetOutpostId,
 FreightHomeColonyId=f.FreightHomeColonyId,CargoMaterialCapacity=capacity??f.CargoMaterialCapacity,CargoMaterials=f.CargoMaterials,
 StrategicSpeed=f.StrategicSpeed,MaximumLegRangeLightYears=f.MaximumLegRangeLightYears,FuelCapacityLightYears=f.FuelCapacityLightYears,
 FuelRemainingLightYears=f.FuelRemainingLightYears,SensorRange=f.SensorRange,IsActive=f.IsActive,
 EmbarkedPopulationMillions=f.EmbarkedPopulationMillions,EmbarkedPopulationSpeciesId=f.EmbarkedPopulationSpeciesId};
ColonyState CloneColony(ColonyState c,int? id=null,int? civilization=null,string? name=null,int? system=null)=>new(){Id=id??c.Id,CivilizationId=civilization??c.CivilizationId,SystemId=system??c.SystemId,Name=name??c.Name,Kind=c.Kind,PlanetaryBodyId=c.PlanetaryBodyId,PopulationSpeciesId=c.PopulationSpeciesId,PopulationMillions=c.PopulationMillions,Infrastructure=c.Infrastructure,Stability=c.Stability,StoredFoodPopulationDaysMillions=c.StoredFoodPopulationDaysMillions,StoredWaterPopulationDaysMillions=c.StoredWaterPopulationDaysMillions,StoredExtractedMaterials=c.StoredExtractedMaterials,RemainingExtractableMaterials=c.RemainingExtractableMaterials,SurfaceHubLevel=c.SurfaceHubLevel,SurfaceHubUpgradeDaysRemaining=c.SurfaceHubUpgradeDaysRemaining,SurfaceBuildings=c.SurfaceBuildings.ToList()};

sealed class StubReach(bool supported,string reason):IInterstellarOperationalReachView {
 public List<object> Calls {get;}=new();
 public MissionReachAssessment Assess(GalaxyState galaxy,int civilizationId,FleetState fleet,int targetSystemId,InterstellarMissionKind missionKind){
  Calls.Add(new{CivilizationId=civilizationId,FleetId=fleet.Id,TargetSystemId=targetSystemId,MissionKind=missionKind});
  return new(supported,true,reason,new[]{fleet.CurrentSystemId??targetSystemId,targetSystemId},10);
 }
}
