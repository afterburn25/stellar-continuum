using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Colonization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
if(args.Length!=1)throw new ArgumentException("Expected output path.");
var json=new JsonSerializerOptions{IncludeFields=true,NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals};
JsonElement Freeze(object? value)=>JsonSerializer.SerializeToElement(value,json);

StarSystemState System(int id,float x=0,float y=0,double? depth=null)=>new(id,$"System {id}",new Vector2(x,y),StarArchetype.Standard,true,false,false,false,null,StellarPrimaryClass.GYellowDwarf,null,null,depth,$"fixture:{id}");
PlanetaryBodyState Body(int id,int system,bool rare=false,bool solid=true,bool native=false,double gravity=1,double temperature=288,double pressure=101.3,double radiation=.1)=>new(id,system,null,id,$"Body {id}",PlanetaryBodyKind.Planet,1,1,new(gravity,temperature,pressure,PlanetaryAtmosphereRegime.OxygenNitrogen,PlanetarySolventRegime.Water,radiation,false,solid),solid,rare,false,native);
CivilizationState Civ(int id=1)=>new(id,$"Civ {id}",0,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,true,CivilizationDevelopmentStage.WarpCapable,SpeciesId:"terran_baseline");
FleetState Fleet(int id=100,bool outpost=false,int civ=1,FleetRole role=FleetRole.Colony,string? design=null)=>new(){Id=id,CivilizationId=civ,Name=$"Fleet {id}",Role=role,DesignId=design??(outpost?ShipDesignRegistry.ResourceOutpostShipId:"colony_ship"),Position=Vector2.Zero,CurrentSystemId=0,EmbarkedPopulationSpeciesId="terran_baseline",EmbarkedPopulationMillions=4.5,MaximumLegRangeLightYears=20,FuelCapacityLightYears=100,FuelRemainingLightYears=100,IsActive=true};
ColonyState Colony(int id,int system,int civ=1)=>new(){Id=id,CivilizationId=civ,SystemId=system,PlanetaryBodyId=null,Name=$"Colony {id}",PopulationSpeciesId="terran_baseline",PopulationMillions=1};
GalaxyState World(bool outpost=false)=>new(){Seed=29,Systems=new List<StarSystemState>{System(0,0),System(1,3),System(2,6)},PlanetaryBodies=new List<PlanetaryBodyState>{Body(10,1),Body(11,1,gravity:1.4),Body(12,1,rare:true,temperature:700,pressure:0),Body(20,2,rare:true,temperature:700,pressure:0),Body(21,2,rare:false,solid:false)},Civilizations=new List<CivilizationState>{Civ()},Fleets=new List<FleetState>{Fleet(100,outpost)},Colonies=new List<ColonyState>(),Economies=new List<CivilizationEconomyState>{new(){CivilizationId=1,Credits=500}},Technologies=new List<TechnologyState>(),ConstructionStates=new List<ConstructionState>(),ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState()};
void Full(GalaxyState g,int system){g.Knowledge.MarkSystemFullySurveyed(1,system);}
object Snapshot(GalaxyState g)=>new{g.Systems,Bodies=g.Planary(),g.Civilizations,g.Colonies,g.Fleets,g.Economies,Knowledge=g.Civilizations.Select(c=>new{CivilizationId=c.Id,Surveys=g.Knowledge.GetSystemSurveyKnowledge(c.Id)}).ToArray()};
List<StarSystemState> Systems(GalaxyState g)=>(List<StarSystemState>)g.Systems;
List<PlanetaryBodyState> Bodies(GalaxyState g)=>(List<PlanetaryBodyState>)g.PlanetaryBodies;
List<CivilizationEconomyState> Economies(GalaxyState g)=>(List<CivilizationEconomyState>)g.Economies;

var cases=new List<object>();
void Add(string name,string kind,bool outpost,Action<GalaxyState> arrange,int fleet=100,int system=1,int body=10,int maximum=32,bool injected=true,bool supported=true,string reachReason="fixture reach",double routeDistance=7,bool reachThrows=false)
{
    var g=World(outpost);arrange(g);var before=Freeze(Snapshot(g));var stub=new StubReach(supported,reachReason,routeDistance,reachThrows);object? result=null;Exception? caught=null;
    try
    {
        if(kind=="ColonyBuild")result=new ColonizationOpportunityPlanner(injected?stub:null).BuildPlan(g,fleet,maximum);
        else if(kind=="ColonyAssess")result=new ColonizationOpportunityPlanner(injected?stub:null).AssessOrder(g,fleet,system,body);
        else if(kind=="ColonyReach")result=new ColonizationOpportunityPlanner(injected?stub:null).AssessOperationalReach(g,fleet,system);
        else if(kind=="OutpostBuild")result=new ResourceOutpostOpportunityPlanner(injected?stub:null).BuildPlan(g,fleet,maximum);
        else if(kind=="OutpostAssess")result=new ResourceOutpostOpportunityPlanner(injected?stub:null).AssessOrder(g,fleet,system,body);
        else if(kind=="IsOutpost")result=ResourceOutpostOpportunityPlanner.IsOutpostFleet(g.Fleets[0]);
        else throw new InvalidOperationException("generator kind");
    }
    catch(Exception ex){caught=ex;}
    var frozenResult=result is null?(JsonElement?)null:Freeze(result);var error=caught is null?(JsonElement?)null:Freeze(new{Type=caught.GetType().Name,caught.Message});var after=Freeze(Snapshot(g));var calls=Freeze(stub.Calls);
    cases.Add(new{Name=name,Kind=kind,Arguments=new{World=before,FleetId=fleet,SystemId=system,BodyId=body,MaximumCandidates=maximum,InjectedReach=injected,ReachSupported=supported,ReachReason=reachReason,ReachDistance=routeDistance,ReachThrows=reachThrows},Result=frozenResult,Error=error,Before=before,After=after,ReachCalls=calls});
}

Action<GalaxyState> Survey(params int[] ids)=>g=>{foreach(var id in ids)Full(g,id);};
Add("colony-no-survey","ColonyBuild",false,_=>{});
Add("colony-full-two-systems","ColonyBuild",false,Survey(1,2));
foreach(var maximum in new[]{-3,0,1,2,32,64,99})Add($"colony-maximum-{maximum}","ColonyBuild",false,Survey(1,2),maximum:maximum);
Add("colony-missing-fleet","ColonyBuild",false,_=>{},fleet:999);
Add("colony-inactive","ColonyBuild",false,g=>g.Fleets[0].IsActive=false);
Add("colony-zero-population","ColonyBuild",false,g=>g.Fleets[0].EmbarkedPopulationMillions=0);
Add("colony-outpost-design-rejected","ColonyBuild",false,g=>g.Fleets[0]=Fleet(outpost:true));
Add("colony-invalid-species","ColonyBuild",false,g=>g.Fleets[0].EmbarkedPopulationSpeciesId="missing");
Add("colony-null-species","ColonyBuild",false,g=>g.Fleets[0].EmbarkedPopulationSpeciesId=null);
Add("colony-duplicate-system","ColonyBuild",false,g=>{Full(g,1);Systems(g).Add(System(1,30));});
Add("colony-duplicate-body","ColonyBuild",false,g=>{Full(g,1);Bodies(g).Add(Body(10,1));});
Add("colony-missing-economy-even-unsuitable","ColonyBuild",false,g=>{Full(g,1);Economies(g).Clear();Bodies(g).Clear();Bodies(g).Add(Body(99,1,solid:false));});
Add("colony-destination-bypasses-economy","ColonyBuild",false,g=>{Full(g,1);Economies(g).Clear();g.Fleets[0].DestinationSystemId=2;});
Add("colony-unaffordable","ColonyBuild",false,g=>{Full(g,1);g.Economies[0].Credits=119.9998;});
Add("colony-affordable-epsilon","ColonyBuild",false,g=>{Full(g,1);g.Economies[0].Credits=119.9999;});
Add("colony-native","ColonyBuild",false,g=>{Full(g,1);Bodies(g)[0]=Body(10,1,native:true);});
Add("colony-no-surface","ColonyBuild",false,g=>{Full(g,1);Bodies(g)[0]=Body(10,1,solid:false);});
Add("colony-occupied-foreign","ColonyBuild",false,g=>{Full(g,1);g.Colonies.Add(Colony(1,1,2));});
Add("colony-reserved-remote","ColonyBuild",false,g=>{Full(g,1);var f=Fleet(7);f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("colony-foreign-reservation-ignored","ColonyBuild",false,g=>{Full(g,1);var f=Fleet(7,civ:2);f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("colony-outpost-reservation-ignored","ColonyBuild",false,g=>{Full(g,1);var f=Fleet(7,true);f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("colony-unsupported","ColonyBuild",false,Survey(1),supported:false,reachReason:"blocked by fixture");
Add("colony-reach-once-per-system","ColonyBuild",false,Survey(1,2));
Add("colony-reach-throws-first-system","ColonyBuild",false,Survey(1,2),reachThrows:true);
Add("colony-distance-nan","ColonyBuild",false,g=>{Full(g,1);g.Fleets[0].Position=new(float.NaN,0);});
Add("colony-sort-nan-before-finite","ColonyBuild",false,g=>{Bodies(g).Clear();Bodies(g).Add(Body(10,1));Bodies(g).Add(Body(20,2));Systems(g)[1]=System(1,float.NaN);Full(g,1);Full(g,2);});
Add("colony-flat-float-distance-tie","ColonyBuild",false,g=>{Bodies(g).Clear();Bodies(g).Add(Body(10,1));Bodies(g).Add(Body(20,2));Systems(g)[1]=System(1,100000,2);Systems(g)[2]=System(2,100000,1);Full(g,1);Full(g,2);});
Add("colony-depth-large-coordinate-order","ColonyBuild",false,g=>{Bodies(g).Clear();Bodies(g).Add(Body(10,1));Bodies(g).Add(Body(20,2));g.Fleets[0].Position=new(33554432,0);Systems(g)[1]=System(1,0,0,0);Systems(g)[2]=System(2,1,0,0);Full(g,1);Full(g,2);});
Add("colony-distance-depth","ColonyBuild",false,g=>{Full(g,1);Systems(g)[1]=System(1,3,4,12);});
Add("colony-assess-unknown-destination","ColonyAssess",false,Survey(1),system:99);
Add("colony-assess-private","ColonyAssess",false,_=>{});
Add("colony-assess-partial","ColonyAssess",false,g=>g.Knowledge.AdvanceSystemSurvey(1,1,.5));
Add("colony-assess-wrong-body-system","ColonyAssess",false,Survey(1),body:20);
Add("colony-assess-missing-body","ColonyAssess",false,Survey(1),body:999);
Add("colony-assess-approved","ColonyAssess",false,Survey(1));
Add("colony-assess-passengers-fractional","ColonyAssess",false,g=>{Full(g,1);g.Fleets[0].EmbarkedPopulationMillions=1.25;});
Add("colony-assess-passengers-infinity","ColonyAssess",false,g=>{Full(g,1);g.Fleets[0].EmbarkedPopulationMillions=double.PositiveInfinity;});
Add("colony-assess-rejected-with-candidate","ColonyAssess",false,g=>{Full(g,1);g.Colonies.Add(Colony(1,1));});
Add("colony-reach-missing-fleet","ColonyReach",false,_=>{},fleet:999);
Add("colony-reach-injected","ColonyReach",false,_=>{},system:2,supported:false,reachReason:"reach direct");
Add("colony-reach-default-current","ColonyReach",false,_=>{},system:0,injected:false);
Add("colony-build-default-reach","ColonyBuild",false,Survey(1),injected:false);
Add("colony-assess-beyond-display-cap","ColonyAssess",false,g=>{Bodies(g).Clear();for(var i=0;i<70;i++)Bodies(g).Add(Body(1000+i,1));Full(g,1);},body:1069);

Add("outpost-no-survey","OutpostBuild",true,_=>{});
Add("outpost-full-two-systems","OutpostBuild",true,Survey(1,2));
foreach(var maximum in new[]{-2,0,1,2,32,64,100})Add($"outpost-maximum-{maximum}","OutpostBuild",true,Survey(1,2),maximum:maximum);
Add("outpost-missing-fleet","OutpostBuild",true,_=>{},fleet:999);
Add("outpost-normal-colony-rejected","OutpostBuild",true,g=>g.Fleets[0]=Fleet());
Add("outpost-invalid-species","OutpostBuild",true,g=>g.Fleets[0].EmbarkedPopulationSpeciesId="missing");
Add("outpost-duplicate-system","OutpostBuild",true,g=>{Full(g,1);Systems(g).Add(System(1,30));});
Add("outpost-duplicate-body","OutpostBuild",true,g=>{Full(g,1);Bodies(g).Add(Body(10,1));});
Add("outpost-missing-economy-nonrare-before-filter","OutpostBuild",true,g=>{Full(g,1);Economies(g).Clear();Bodies(g).Clear();Bodies(g).Add(Body(99,1,rare:false,solid:false));});
Add("outpost-destination-bypasses-economy","OutpostBuild",true,g=>{Full(g,1);Economies(g).Clear();g.Fleets[0].DestinationSystemId=2;});
Add("outpost-unaffordable","OutpostBuild",true,g=>{Full(g,1);g.Economies[0].Credits=89.9998;});
Add("outpost-affordable-epsilon","OutpostBuild",true,g=>{Full(g,1);g.Economies[0].Credits=89.9999;});
Add("outpost-no-surface","OutpostBuild",true,g=>{Full(g,1);Bodies(g)[2]=Body(12,1,rare:true,solid:false);});
Add("outpost-native","OutpostBuild",true,g=>{Full(g,1);Bodies(g)[2]=Body(12,1,rare:true,native:true,temperature:700,pressure:0);});
Add("outpost-colony-viable-rejected","OutpostBuild",true,g=>{Full(g,1);Bodies(g)[2]=Body(12,1,rare:true);});
Add("outpost-occupied","OutpostBuild",true,g=>{Full(g,1);g.Colonies.Add(Colony(1,1,2));});
Add("outpost-normal-colony-reserves","OutpostBuild",true,g=>{Full(g,1);var f=Fleet(7);f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("outpost-unpopulated-colony-reserves","OutpostBuild",true,g=>{Full(g,1);var f=Fleet(7);f.EmbarkedPopulationMillions=0;f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("outpost-foreign-reservation-ignored","OutpostBuild",true,g=>{Full(g,1);var f=Fleet(7,civ:2);f.DestinationSystemId=1;g.Fleets.Add(f);});
Add("outpost-unsupported","OutpostBuild",true,Survey(1),supported:false,reachReason:"blocked outpost");
Add("outpost-reach-evaluates-nonrare","OutpostBuild",true,Survey(1,2));
Add("outpost-reach-throws-before-rare-filter","OutpostBuild",true,g=>{Bodies(g).Clear();Bodies(g).Add(Body(99,1));Full(g,1);},reachThrows:true);
Add("outpost-sort-nan-before-finite","OutpostBuild",true,g=>{Bodies(g).Clear();Bodies(g).Add(Body(12,1,rare:true,temperature:700,pressure:0));Bodies(g).Add(Body(20,2,rare:true,temperature:700,pressure:0));Systems(g)[1]=System(1,float.NaN);Full(g,1);Full(g,2);});
Add("outpost-assess-approved","OutpostAssess",true,Survey(1),body:12);
Add("outpost-assess-rejected-candidate","OutpostAssess",true,g=>{Full(g,1);g.Colonies.Add(Colony(1,1));},body:12);
Add("outpost-assess-private","OutpostAssess",true,_=>{},body:12);
Add("outpost-assess-beyond-cap","OutpostAssess",true,g=>{Bodies(g).Clear();for(var i=0;i<66;i++)Bodies(g).Add(Body(1000+i,1,rare:true,temperature:700,pressure:0));Full(g,1);},body:1065);
Add("outpost-default-reach","OutpostBuild",true,Survey(1),injected:false);
Add("is-outpost-true","IsOutpost",true,_=>{});
Add("is-outpost-inactive","IsOutpost",true,g=>g.Fleets[0].IsActive=false);
Add("is-outpost-role","IsOutpost",true,g=>g.Fleets[0]=Fleet(outpost:true,role:FleetRole.Scout));
Add("is-outpost-design","IsOutpost",true,g=>g.Fleets[0]=Fleet(outpost:true,design:"other"));
Add("is-outpost-zero","IsOutpost",true,g=>g.Fleets[0].EmbarkedPopulationMillions=0);
Add("is-outpost-nan","IsOutpost",true,g=>g.Fleets[0].EmbarkedPopulationMillions=double.NaN);
Add("is-outpost-infinity","IsOutpost",true,g=>g.Fleets[0].EmbarkedPopulationMillions=double.PositiveInfinity);

var sourceNulls=new List<object>();
foreach(var kind in new[]{"ColonyBuild","ColonyAssess","ColonyReach","OutpostBuild","OutpostAssess"}){object? error=null;try{if(kind=="ColonyBuild")_=new ColonizationOpportunityPlanner().BuildPlan(null!,1);else if(kind=="ColonyAssess")_=new ColonizationOpportunityPlanner().AssessOrder(null!,1,1,1);else if(kind=="ColonyReach")_=new ColonizationOpportunityPlanner().AssessOperationalReach(null!,1,1);else if(kind=="OutpostBuild")_=new ResourceOutpostOpportunityPlanner().BuildPlan(null!,1);else _=new ResourceOutpostOpportunityPlanner().AssessOrder(null!,1,1,1);}catch(Exception ex){error=new{Type=ex.GetType().Name,ex.Message};}sourceNulls.Add(new{Kind=kind,Error=error});}
object? nullFleetError=null;try{_=ResourceOutpostOpportunityPlanner.IsOutpostFleet(null!);}catch(Exception ex){nullFleetError=new{Type=ex.GetType().Name,ex.Message};}
File.WriteAllText(args[0],JsonSerializer.Serialize(new{Format="stellar-settlement-planning-oracle-v1",Cases=cases,SourceOnlyNullWorld=sourceNulls,SourceOnlyNullFleet=new[]{new{Kind="IsOutpost",Error=nullFleetError}},NativeBoundary="Typed native references cannot represent a null world or fleet.",CaseCount=cases.Count},json)+Environment.NewLine);

static class SnapshotExtensions{public static IReadOnlyList<PlanetaryBodyState> Planary(this GalaxyState g)=>g.PlanetaryBodies;}
sealed class StubReach(bool supported,string reason,double distance,bool throws):IInterstellarOperationalReachView
{
    public List<ReachCall> Calls{get;}=[];
    public MissionReachAssessment Assess(GalaxyState galaxy,int civilizationId,FleetState fleet,int targetSystemId,InterstellarMissionKind missionKind){Calls.Add(new(civilizationId,fleet.Id,targetSystemId,missionKind));if(throws)throw new InvalidOperationException("fixture reach failure");return new(supported,false,reason,new[]{fleet.CurrentSystemId??-1,targetSystemId},distance);}
}
sealed record ReachCall(int CivilizationId,int FleetId,int TargetSystemId,InterstellarMissionKind MissionKind);
