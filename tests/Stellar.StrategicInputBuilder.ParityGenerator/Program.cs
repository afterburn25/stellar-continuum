using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
if(args.Length!=1)throw new ArgumentException("Expected output path.");
var json=new JsonSerializerOptions{IncludeFields=true,NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals};
JsonElement Freeze(object? value)=>JsonSerializer.SerializeToElement(value,json);
StarSystemState System(int id,float x=0)=>new(id,$"System {id}",new Vector2(x,0),StarArchetype.Standard,true,false,false,false,null,StellarPrimaryClass.GYellowDwarf,null,null,null,$"fixture:{id}");
PlanetaryBodyState Body(int id,int system,double gravity=1,double temp=288,double pressure=101.3,bool solid=true,bool legacy=true)=>new(id,system,null,id,$"Body {id}",PlanetaryBodyKind.Planet,1,1,new(gravity,temp,pressure,PlanetaryAtmosphereRegime.OxygenNitrogen,PlanetarySolventRegime.Water,.1,false,solid),legacy,false,false,false);
CivilizationState Civ(int id=1,string species="terran_baseline")=>new(id,$"Civ {id}",0,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,id==1,CivilizationDevelopmentStage.WarpCapable,SpeciesId:species);
ColonyState Colony(int id,int system,int civ=1,double pop=100,string species="terran_baseline")=>new(){Id=id,CivilizationId=civ,SystemId=system,Name=$"Colony {id}",PopulationMillions=pop,PopulationSpeciesId=species};
FleetState Fleet(int id,int civ=1,FleetRole role=FleetRole.Scout,bool active=true,int? current=0,FleetCombatState? combat=null)=>new(){Id=id,CivilizationId=civ,Name=$"Fleet {id}",Role=role,IsActive=active,CurrentSystemId=current,Position=Vector2.Zero,MaximumLegRangeLightYears=20,FuelCapacityLightYears=100,FuelRemainingLightYears=100,Combat=combat};
FleetCombatState Combat(double shields=35,double armor=45,double hull=95)=>new(){ProfileId="patrol_corvette_mk1",Shields=shields,Armor=armor,Hull=hull};
CivilizationEconomyState Economy(int civ=1,double industry=200,double science=5,double ips=1)=>new(){CivilizationId=civ,Industry=industry,LastSciencePerSecond=science,LastIndustryPerSecond=ips};
TechnologyState Tech(int civ=1,string? active=null,params string[] done){var x=new TechnologyState{CivilizationId=civ,ActiveResearchId=active};foreach(var id in done)x.CompletedTechnologyIds.Add(id);return x;}
ConstructionState Construction(int civ=1,params string[] done){var x=new ConstructionState{CivilizationId=civ};foreach(var id in done)x.CompletedProjectIds.Add(id);return x;}
GalaxyState World()=>new(){Seed=35,Systems=new List<StarSystemState>{System(0),System(1,3)},PlanetaryBodies=new List<PlanetaryBodyState>{Body(10,1)},Civilizations=new List<CivilizationState>{Civ()},Colonies=new List<ColonyState>{Colony(1,0)},Fleets=new List<FleetState>(),Economies=new List<CivilizationEconomyState>{Economy()},Technologies=new List<TechnologyState>{Tech(1,null,"orbital_industry","prototype_warp_drive")},ConstructionStates=new List<ConstructionState>{Construction(1,"orbital_shipyard","orbital_launch_complex")},ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState()};
object Snapshot(GalaxyState g)=>new{g.Systems,Bodies=g.PlanetaryBodies,g.Civilizations,g.Colonies,g.Fleets,g.Economies,g.Technologies,Construction=g.ConstructionStates,Surveys=g.Civilizations.Select(c=>new{CivilizationId=c.Id,Values=g.Knowledge.GetSystemSurveyKnowledge(c.Id)}).ToArray()};
List<CivilizationEconomyState> Economies(GalaxyState g)=>(List<CivilizationEconomyState>)g.Economies;
List<StarSystemState> Systems(GalaxyState g)=>(List<StarSystemState>)g.Systems;
List<PlanetaryBodyState> Bodies(GalaxyState g)=>(List<PlanetaryBodyState>)g.PlanetaryBodies;
var cases=new List<object>();
void Add(string name,Action<GalaxyState> arrange,int civ=1,bool injected=true,double coverage=.8,bool spacecraft=true,bool transit=true,bool logisticsThrows=false,string? capabilityThrow=null,bool reachSupported=true,bool reachThrows=false){
 var g=World();arrange(g);var before=Freeze(Snapshot(g));var calls=new List<string>();var logistics=new LogisticsStub(coverage,logisticsThrows,calls);var capability=new CapabilityStub(spacecraft,transit,capabilityThrow,calls);var reach=new ReachStub(reachSupported,reachThrows,calls);var builder=injected?new CivilizationStrategicInputBuilder(logistics,capability,new ExplorationMissionPlanner(reach)):new CivilizationStrategicInputBuilder();CivilizationOwnState? result=null;Exception? caught=null;
 try{result=builder.Build(g,civ);}catch(Exception e){caught=e;}
 var output=result is null?(JsonElement?)null:Freeze(result);var error=caught is null?(JsonElement?)null:Freeze(new{Type=caught.GetType().Name,caught.Message});var after=Freeze(Snapshot(g));
 cases.Add(new{Name=name,Arguments=Freeze(new{World=before,CivilizationId=civ,Injected=injected,Coverage=coverage,Spacecraft=spacecraft,Transit=transit,LogisticsThrows=logisticsThrows,CapabilityThrow=capabilityThrow,ReachSupported=reachSupported,ReachThrows=reachThrows}),Result=output,Error=error,Before=before,After=after,Calls=Freeze(calls)});
}
Add("baseline-injected",g=>{});Add("baseline-default",g=>{},injected:false);Add("baseline-default-explorer",g=>g.Fleets.Add(Fleet(2)),injected:false);Add("baseline-default-colonization",g=>g.Knowledge.MarkSystemFullySurveyed(1,1),injected:false);
Add("missing-civilization",g=>{},civ:9);Add("missing-economy",g=>Economies(g).Clear());Add("missing-technology",g=>g.Technologies.Clear());Add("missing-construction",g=>g.ConstructionStates.Clear());Add("logistics-throws",g=>{},logisticsThrows:true);
Add("duplicate-civilization-first",g=>{g.Civilizations.Insert(0,Civ(1,"pelagic_high_pressure"));g.Colonies.Clear();Bodies(g)[0]=Body(10,1,legacy:false);g.Knowledge.MarkSystemFullySurveyed(1,1);});
Add("duplicate-economy-first",g=>{Economies(g).Clear();Economies(g).Add(Economy(1,17,3));Economies(g).Add(Economy(1,999,999));});
Add("duplicate-technology-first",g=>{g.Technologies.Clear();g.Technologies.Add(Tech(1));g.Technologies.Add(Tech(1,null,"orbital_industry","prototype_warp_drive"));});
Add("duplicate-construction-first",g=>{g.ConstructionStates.Clear();g.ConstructionStates.Add(Construction(1));g.ConstructionStates.Add(Construction(1,"orbital_shipyard"));});
Add("coverage-nan",g=>{},coverage:double.NaN);Add("industry-negative",g=>{Economies(g).Clear();Economies(g).Add(Economy(industry:-7));});Add("industry-nan",g=>{Economies(g).Clear();Economies(g).Add(Economy(industry:double.NaN));});Add("science-negative",g=>{Economies(g).Clear();Economies(g).Add(Economy(science:-2));});Add("science-nan",g=>{Economies(g).Clear();Economies(g).Add(Economy(science:double.NaN));});
Add("cap-neither",g=>{},spacecraft:false,transit:false);Add("cap-spacecraft-only",g=>{},transit:false);Add("cap-transit-only",g=>{},spacecraft:false);Add("cap-first-throws",g=>{},capabilityThrow:"spacecraft_construction");Add("cap-second-throws",g=>{},capabilityThrow:"experimental_interstellar_transit");
Add("shipyard-missing",g=>{g.ConstructionStates.Clear();g.ConstructionStates.Add(Construction(1));},spacecraft:true,transit:true);
Add("research-active",g=>g.Technologies[0].ActiveResearchId="fusion_propulsion");Add("research-none-available",g=>{var t=Tech();foreach(var d in TechnologyRegistry.All)t.CompletedTechnologyIds.Add(d.Id);g.Technologies.Clear();g.Technologies.Add(t);});Add("research-project-unlocks",g=>{g.Technologies.Clear();g.Technologies.Add(Tech());});
Add("exploration-none",g=>{});Add("exploration-inactive",g=>g.Fleets.Add(Fleet(2,role:FleetRole.Scout,active:false)));Add("exploration-foreign",g=>g.Fleets.Add(Fleet(2,civ:2)));Add("exploration-military",g=>g.Fleets.Add(Fleet(2,role:FleetRole.Military,combat:Combat())));Add("exploration-scout-supported",g=>g.Fleets.Add(Fleet(2)));Add("exploration-scout-blocked",g=>g.Fleets.Add(Fleet(2)),reachSupported:false);Add("exploration-science-supported",g=>g.Fleets.Add(Fleet(2,role:FleetRole.Science)));Add("exploration-id-order-stop",g=>{g.Fleets.Add(Fleet(9));g.Fleets.Add(Fleet(2));});Add("exploration-reach-throws",g=>g.Fleets.Add(Fleet(2)),reachThrows:true);
Add("exploration-duplicate-fleet-first-match",g=>{g.Fleets.Add(Fleet(2,role:FleetRole.Military,combat:Combat()));g.Fleets.Add(Fleet(2));});
Add("exploration-hard-cap-window",g=>{g.Fleets.Add(Fleet(2));for(var id=2;id<=70;id++)Systems(g).Add(System(id,id));},reachSupported:false);
Add("opportunity-unsurveyed",g=>{},transit:true);Add("opportunity-partial",g=>g.Knowledge.AdvanceSystemSurvey(1,1,.5));Add("opportunity-full",g=>g.Knowledge.MarkSystemFullySurveyed(1,1));Add("opportunity-occupied-own",g=>{g.Knowledge.MarkSystemFullySurveyed(1,1);g.Colonies.Add(Colony(2,1));});Add("opportunity-occupied-foreign",g=>{g.Knowledge.MarkSystemFullySurveyed(1,1);g.Colonies.Add(Colony(2,1,2));});Add("opportunity-no-transit",g=>g.Knowledge.MarkSystemFullySurveyed(1,1),transit:false);
Add("species-positive-mixed",g=>{Bodies(g)[0]=Body(10,1,legacy:false);g.Knowledge.MarkSystemFullySurveyed(1,1);g.Colonies[0].PopulationSpeciesId="pelagic_high_pressure";g.Colonies.Add(Colony(2,2,pop:2,species:"terran_baseline"));});Add("species-zero-excluded",g=>{g.Knowledge.MarkSystemFullySurveyed(1,1);g.Colonies.Add(Colony(2,2,pop:0,species:"pelagic_high_pressure"));});Add("species-foreign-excluded",g=>{g.Knowledge.MarkSystemFullySurveyed(1,1);g.Colonies.Add(Colony(2,2,civ:2,pop:2,species:"pelagic_high_pressure"));});
Add("military-active-count",g=>g.Fleets.Add(Fleet(3,role:FleetRole.Military,combat:Combat())));Add("military-inactive-excluded",g=>g.Fleets.Add(Fleet(3,role:FleetRole.Military,active:false,combat:Combat())));Add("military-damaged-strength",g=>g.Fleets.Add(Fleet(3,role:FleetRole.Military,combat:Combat(1,2,3))));Add("military-foreign-excluded",g=>g.Fleets.Add(Fleet(3,civ:2,role:FleetRole.Military,combat:Combat())));Add("desired-three-colonies",g=>{g.Colonies.Add(Colony(2,2));g.Colonies.Add(Colony(3,3));});
object? nullError=null;try{_=new CivilizationStrategicInputBuilder().Build(null!,1);}catch(Exception e){nullError=new{Type=e.GetType().Name,e.Message};}
File.WriteAllText(args[0],JsonSerializer.Serialize(new{Format="stellar-strategic-input-builder-oracle-v1",Cases=cases,SourceOnlyNullWorld=new{Error=nullError},CaseCount=cases.Count},json)+Environment.NewLine);Console.WriteLine($"strategic input builder oracle: {cases.Count} cases");

sealed class LogisticsStub(double coverage,bool throws,List<string> calls):IEconomyLogisticsView{public CivilizationLogisticsSnapshot GetSnapshot(GalaxyState g,int id){calls.Add($"logistics:{id}");if(throws)throw new InvalidOperationException("fixture logistics failure");return new(id,0,0,0,0,coverage,SupplyCondition.Healthy,Array.Empty<ColonyLogisticsSnapshot>());}}
sealed class CapabilityStub(bool spacecraft,bool transit,string? throws,List<string> calls):IShipbuildingCapabilityView{public bool HasCivilizationCapability(GalaxyState g,int id,string capability){calls.Add($"capability:{id}:{capability}");if(capability==throws)throw new InvalidOperationException("fixture capability failure "+capability);return capability==ShipbuildingCapabilityIds.SpacecraftConstruction?spacecraft:capability==ShipbuildingCapabilityIds.ExperimentalInterstellarTransit?transit:false;}}
sealed class ReachStub(bool supported,bool throws,List<string> calls):IInterstellarOperationalReachView{public MissionReachAssessment Assess(GalaxyState g,int id,FleetState fleet,int target,InterstellarMissionKind kind){calls.Add($"reach:{fleet.Id}:{target}:{kind}");if(throws)throw new InvalidOperationException("fixture exploration failure");return new(supported,false,supported?"fixture supported":"fixture blocked",new[]{fleet.CurrentSystemId??-1,target},target);}}
