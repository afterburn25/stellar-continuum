using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected one fixture output path.");
var json = new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
string Json(object value) => JsonSerializer.Serialize(value, json);

var cases = new List<object>();

GalaxyState World() {
    var civ = new CivilizationState(1,"Terran Union",1,CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,true,CivilizationDevelopmentStage.WarpCapable,SpeciesId:"terran_baseline");
    var body = new PlanetaryBodyState(1,1,null,0,"Test World",PlanetaryBodyKind.Planet,1,1,
        new PlanetaryEnvironmentState(1,288,101,PlanetaryAtmosphereRegime.OxygenNitrogen,PlanetarySolventRegime.Water,0,false,true),true,false,false,false);
    return new GalaxyState {
        Seed=1,Systems=Array.Empty<StarSystemState>(),PlanetaryBodies=new List<PlanetaryBodyState>{body},Civilizations=new List<CivilizationState>{civ},
        Fleets=new List<FleetState>(),Colonies=new List<ColonyState>{Colony()},Economies=new List<CivilizationEconomyState>{Economy()},
        Technologies=new List<TechnologyState>(),ConstructionStates=new List<ConstructionState>{new(){CivilizationId=1}},
        ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState() };
}
ColonyState Colony(params SurfaceBuildingState[] buildings) => new() {
    Id=1,CivilizationId=1,SystemId=1,PlanetaryBodyId=1,Name="Surface",PopulationSpeciesId="terran_baseline",
    PopulationMillions=100,Infrastructure=1,Stability=1,SurfaceHubLevel=1,SurfaceBuildings=buildings.ToList() };
CivilizationEconomyState Economy(double credits=1000,double industry=2000,double funding=1) => new() {
    CivilizationId=1,Credits=credits,Industry=industry,LastBaseOperationsFundingFraction=funding };
SurfaceBuildingState Building(int id,string type,bool complete=true,float x=100,float z=100,float rotation=0,double progress=0,
    bool enabled=true,double condition=1,int priority=0,double stored=0,string? pending=null,double upgradeDays=0) => new() {
    Id=id,TypeId=type,X=x,Z=z,RotationDegrees=rotation,IndustryProgress=complete?SurfaceBuildingCatalog.Find(type)?.IndustryCost??progress:progress,
    IsComplete=complete,IsEnabled=enabled,Condition=condition,OperatingPriority=priority,StoredPowerDays=stored,
    PendingUpgradeTypeId=pending,UpgradeDaysRemaining=upgradeDays };

ColonyState CloneColony(ColonyState c) => new() {
    Id=c.Id,CivilizationId=c.CivilizationId,SystemId=c.SystemId,PlanetaryBodyId=c.PlanetaryBodyId,Name=c.Name,Kind=c.Kind,
    PopulationSpeciesId=c.PopulationSpeciesId,PopulationMillions=c.PopulationMillions,Infrastructure=c.Infrastructure,Stability=c.Stability,
    StoredFoodPopulationDaysMillions=c.StoredFoodPopulationDaysMillions,StoredWaterPopulationDaysMillions=c.StoredWaterPopulationDaysMillions,
    StoredExtractedMaterials=c.StoredExtractedMaterials,RemainingExtractableMaterials=c.RemainingExtractableMaterials,
    SurfaceHubLevel=c.SurfaceHubLevel,SurfaceHubUpgradeDaysRemaining=c.SurfaceHubUpgradeDaysRemaining,
    SurfaceBuildings=c.SurfaceBuildings.Select(b=>new SurfaceBuildingState { Id=b.Id,TypeId=b.TypeId,X=b.X,Z=b.Z,RotationDegrees=b.RotationDegrees,
        IndustryProgress=b.IndustryProgress,IsComplete=b.IsComplete,IsEnabled=b.IsEnabled,PendingUpgradeTypeId=b.PendingUpgradeTypeId,
        UpgradeDaysRemaining=b.UpgradeDaysRemaining,OperatingPriority=b.OperatingPriority,Condition=b.Condition,StoredPowerDays=b.StoredPowerDays }).ToList() };
CivilizationEconomyState CloneEconomy(CivilizationEconomyState e) => new() { CivilizationId=e.CivilizationId,Credits=e.Credits,Industry=e.Industry,
    Science=e.Science,LastCreditsPerSecond=e.LastCreditsPerSecond,LastIndustryPerSecond=e.LastIndustryPerSecond,LastSciencePerSecond=e.LastSciencePerSecond,
    LastResearchSpendingPerDay=e.LastResearchSpendingPerDay,LastResearchFundingFraction=e.LastResearchFundingFraction,OperatingArrears=e.OperatingArrears,
    LastBaseOperationsFundingFraction=e.LastBaseOperationsFundingFraction,IndustryPriority=e.IndustryPriority };
ConstructionState CloneConstruction(ConstructionState s) { var copy=new ConstructionState { CivilizationId=s.CivilizationId,ActiveProjectId=s.ActiveProjectId,
    ActiveProjectProgress=s.ActiveProjectProgress,ActiveProjectAuthorizationCredits=s.ActiveProjectAuthorizationCredits };
    foreach(var id in s.CompletedProjectIds) copy.CompletedProjectIds.Add(id); foreach(var q in s.QueuedProjects) copy.QueuedProjects.Add(q); return copy; }
object Snapshot(GalaxyState g, Caps caps) => new { Civilizations=g.Civilizations.ToArray(),Bodies=g.PlanetaryBodies.ToArray(),
    Construction=g.ConstructionStates.Select(CloneConstruction).ToArray(),Colonies=g.Colonies.Select(CloneColony).ToArray(),
    Economies=g.Economies.Select(CloneEconomy).ToArray(),Capabilities=caps.Values.Select(x=>new {CivilizationId=x.Key,CapabilityIds=x.Value.ToArray()}).ToArray() };

object Apply(GalaxyState g,Caps caps,Op op) => op.Kind switch {
    "Place" => SurfaceConstruction.Place(g,op.CivilizationId,op.ColonyId,op.TypeId!,op.X,op.Z,op.Rotation),
    "Remove" => SurfaceConstruction.Remove(g,op.CivilizationId,op.ColonyId,op.BuildingId),
    "Upgrade" => SurfaceConstruction.Upgrade(g,op.CivilizationId,op.ColonyId,op.BuildingId,caps),
    "UpgradeHub" => SurfaceConstruction.UpgradeHub(g,op.CivilizationId,op.ColonyId,caps),
    "Repair" => SurfaceConstruction.Repair(g,op.CivilizationId,op.ColonyId,op.BuildingId),
    "SetEnabled" => SurfaceConstruction.SetEnabled(g,op.CivilizationId,op.ColonyId,op.BuildingId,op.Flag),
    "SetPriority" => SurfaceConstruction.SetOperatingPriority(g,op.CivilizationId,op.ColonyId,op.BuildingId,op.Flag),
    "Advance" => Advance(g,op),
    _ => throw new InvalidOperationException("Unknown generator operation") };
object Advance(GalaxyState g,Op op) { SurfaceConstruction.Advance(g,op.CivilizationId,op.Budget,op.Days); return new { }; }

void Sequence(string name,Action<GalaxyState,Caps> setup,params Op[] operations) {
    var g=World();var caps=new Caps();setup(g,caps);var before=Snapshot(g,caps);var frozen=Json(before);var steps=new List<object>();
    foreach(var op in operations) { object? result=null;object? error=null;try { result=Apply(g,caps,op); } catch(Exception e) { error=new {Type=e.GetType().Name,Message=e.Message}; }
        steps.Add(new {Operation=op,Result=result,Error=error,After=Snapshot(g,caps)}); }
    if(Json(before)!=frozen)throw new InvalidOperationException(name+": detached before snapshot changed");
    cases.Add(new {Name=name,Kind="Sequence",Before=before,Steps=steps}); }
void Query(string name,string query,Action<GalaxyState,Caps> setup,double argument=0) {
    var g=World();var caps=new Caps();setup(g,caps);var before=Snapshot(g,caps);var frozen=Json(before);object? result=null;object? error=null;
    try { result=query switch { "Multiplier"=>SurfaceConstruction.GetConstructionCostMultiplier(g,g.Colonies[0]),
        "Authorization"=>SurfaceConstruction.GetAuthorizationCost(g,g.Colonies[0],SurfaceBuildingCatalog.Find("fabricator")!),
        "UpgradeAuthorization"=>SurfaceConstruction.GetUpgradeAuthorizationCost(g,g.Colonies[0],SurfaceBuildingCatalog.Find("fabricator")!),
        "HubCost"=>HubCost(g),
        "BuildingLock"=>SurfaceConstruction.GetBuildingUpgradeLockReason(g,1,SurfaceBuildingCatalog.Find("fabricator")!,caps),
        "HubLock"=>SurfaceConstruction.GetHubUpgradeLockReason(g,1,g.Colonies[0],caps),
        "Demand"=>SurfaceConstruction.GetIndustryDemand(g,1,argument), _=>throw new InvalidOperationException() }; }
    catch(Exception e){error=new {Type=e.GetType().Name,Message=e.Message};}
    if(Json(before)!=frozen)throw new InvalidOperationException(name+": query mutated state");cases.Add(new{Name=name,Kind="Query",Query=query,Argument=argument,Before=before,Result=result,Error=error}); }
object? HubCost(GalaxyState g) { var value=SurfaceConstruction.GetHubUpgradeCost(g,g.Colonies[0]);return value is null?null:new {CreditCost=value.Value.CreditCost,IndustryCost=value.Value.IndustryCost}; }
void Placement(string name,List<SurfaceBuildingState> buildings,string type,float x,float z,float rotation) {
    var before=buildings.Select(b=>CloneColony(Colony(b)).SurfaceBuildings[0]).ToList();var frozen=Json(before);
    var result=SurfaceConstruction.PlacementError(buildings,type,x,z,rotation);if(Json(before)!=frozen)throw new InvalidOperationException(name+": placement query mutated state");
    cases.Add(new{Name=name,Kind="Placement",Buildings=before,TypeId=type,X=x,Z=z,Rotation=rotation,Expected=result}); }
void Validate(string name,Action<ColonyState> setup) {var c=Colony();setup(c);var before=CloneColony(c);var frozen=Json(before);object? error=null;
    try{SurfaceConstruction.Validate(c);}catch(Exception e){error=new{Type=e.GetType().Name,Message=e.Message};}if(Json(before)!=frozen)throw new InvalidOperationException(name+": validation mutated state");
    cases.Add(new{Name=name,Kind="Validate",Colony=before,Error=error});}

// Environment costs, body/system resolution, and lock/capability rules.
Query("multiplier-missing-body","Multiplier",(g,c)=>g.Colonies[0].PlanetaryBodyId=null);
Query("multiplier-wrong-system-body","Multiplier",(g,c)=>g.Colonies[0]=CloneWith(g.Colonies[0],system:2));
Query("multiplier-earthlike","Multiplier",(g,c)=>{});
Query("multiplier-all-hazards-clamped","Multiplier",(g,c)=>SetBody(g,gravity:4,temperature:500,pressure:400,atmosphere:PlanetaryAtmosphereRegime.Vacuum,radiation:1));
Query("multiplier-thresholds-inclusive","Multiplier",(g,c)=>SetBody(g,temperature:240,pressure:20,radiation:.10));
Query("multiplier-pressure-below","Multiplier",(g,c)=>SetBody(g,pressure:19.999));
Query("multiplier-pressure-above","Multiplier",(g,c)=>SetBody(g,pressure:300.001));
Query("multiplier-temperature-below","Multiplier",(g,c)=>SetBody(g,temperature:239.999));
Query("multiplier-temperature-above","Multiplier",(g,c)=>SetBody(g,temperature:330.001));
Query("multiplier-radiation-above","Multiplier",(g,c)=>SetBody(g,radiation:.1001));
Query("multiplier-gravity-addition-cap","Multiplier",(g,c)=>SetBody(g,gravity:10));
Query("multiplier-away-midpoint","Multiplier",(g,c)=>SetBody(g,gravity:1.025));
Query("authorization-environment","Authorization",(g,c)=>SetBody(g,gravity:2,atmosphere:PlanetaryAtmosphereRegime.Vacuum));
Query("upgrade-authorization-environment","UpgradeAuthorization",(g,c)=>SetBody(g,gravity:2,atmosphere:PlanetaryAtmosphereRegime.Vacuum));
Query("hub-cost-level1","HubCost",(g,c)=>{}); Query("hub-cost-level2","HubCost",(g,c)=>g.Colonies[0].SurfaceHubLevel=2);
Query("hub-cost-max","HubCost",(g,c)=>g.Colonies[0].SurfaceHubLevel=3); Query("hub-cost-outpost","HubCost",(g,c)=>g.Colonies[0].Kind=SettlementKind.ResourceOutpost);
Query("building-lock","BuildingLock",(g,c)=>{}); Query("building-unlocked","BuildingLock",(g,c)=>c.Add(1,"additive_manufacturing"));
Query("hub-lock-level1","HubLock",(g,c)=>{}); Query("hub-unlocked-level1","HubLock",(g,c)=>g.ConstructionStates[0].CompletedProjectIds.Add("industrial_automation"));
Query("hub-lock-orbital","HubLock",(g,c)=>g.Colonies[0].SurfaceHubLevel=2);
Query("hub-lock-small-body","HubLock",(g,c)=>{g.Colonies[0].SurfaceHubLevel=2;c.Add(1,"orbital_industry");SetBody(g,radius:.349);});

// Placement geometry includes exact allowed boundaries and strict overlap/hub thresholds.
Placement("placement-unknown",new(),"unknown",100,100,0); Placement("placement-nonfinite",new(),"fabricator",float.NaN,100,0);
Placement("placement-boundary-equal",new(),"fabricator",495,100,0); Placement("placement-boundary-outside",new(),"fabricator",495.001f,100,0);
Placement("placement-hub-equal",new(),"fabricator",44,0,0); Placement("placement-hub-inside",new(),"fabricator",43.999f,0,0);
Placement("placement-overlap",new(){Building(1,"fabricator")},"fabricator",100,100,0);
Placement("placement-overlap-equal",new(){Building(1,"fabricator")},"fabricator",137,100,0);
Placement("placement-existing-unknown",new(){Building(1,"unknown",progress:0)},"fabricator",200,200,0);
Placement("placement-limit",Enumerable.Range(1,64).Select(i=>Building(i,"power_generator",x:-480+(i%8)*50,z:-480+(i/8)*50)).ToList(),"fabricator",450,450,0);
Placement("placement-terrain-slope-evaluation",new(),"power_generator",400,400,0);

// Paid placement and ordinary construction, including pause and completion.
Sequence("paid-place-partial-pause-complete",(g,c)=>{},
    new Op("Place",TypeId:"fabricator",X:100,Z:100,Rotation:-450),new Op("Advance",Budget:100,Days:2),new Op("Advance",Budget:0,Days:3),new Op("Advance",Budget:500,Days:13));
Sequence("place-rejection-order",(g,c)=>{},new Op("Place",CivilizationId:2,TypeId:"unknown",X:0,Z:0),new Op("Place",TypeId:"advanced_fabricator",X:100,Z:100),new Op("Place",TypeId:"fabricator",X:0,Z:0));
Sequence("place-missing-economy-before-type",(g,c)=>((List<CivilizationEconomyState>)g.Economies).Clear(),new Op("Place",TypeId:"unknown",X:100,Z:100));
Sequence("place-no-surface",(g,c)=>SetBody(g,solid:false),new Op("Place",TypeId:"fabricator",X:100,Z:100));
Sequence("place-insufficient-credit",(g,c)=>g.Economies[0].Credits=49.9998,new Op("Place",TypeId:"fabricator",X:100,Z:100));
Sequence("place-missing-civilization-currency",(g,c)=>((List<CivilizationState>)g.Civilizations).Clear(),new Op("Place",TypeId:"fabricator",X:100,Z:100));
Sequence("place-negative-maximum-id",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(-1,"power_generator")),new Op("Place",TypeId:"fabricator",X:200,Z:200));
Sequence("place-max-minus-one-id",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(int.MaxValue-1,"power_generator")),new Op("Place",TypeId:"fabricator",X:200,Z:200));
Sequence("place-max-id-overflow",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(int.MaxValue,"power_generator")),new Op("Place",TypeId:"fabricator",X:200,Z:200));
Sequence("outpost-trade-and-capacity",(g,c)=>{g.Colonies[0].Kind=SettlementKind.ResourceOutpost;for(int i=1;i<=8;i++)g.Colonies[0].SurfaceBuildings.Add(Building(i,"power_generator",x:-450+i*45,z:300));},new Op("Place",TypeId:"trade_hub",X:200,Z:200),new Op("Place",TypeId:"fabricator",X:200,Z:200));

Sequence("cancel-refund-and-demolish",(g,c)=>{g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false,progress:100));g.Colonies[0].SurfaceBuildings.Add(Building(2,"science_lab",x:180,z:180));},new Op("Remove",BuildingId:1),new Op("Remove",BuildingId:2),new Op("Remove",BuildingId:99));
Sequence("remove-unknown-and-no-economy",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"unknown",progress:0)),new Op("Remove",BuildingId:1));
Sequence("remove-incomplete-missing-civilization-currency",(g,c)=>{((List<CivilizationState>)g.Civilizations).Clear();g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false,progress:100));},new Op("Remove",BuildingId:1));
Sequence("remove-complete-skips-currency",(g,c)=>{((List<CivilizationState>)g.Civilizations).Clear();g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator"));},new Op("Remove",BuildingId:1));

Sequence("upgrade-offline-zero-budget-funded",(g,c)=>{g.Colonies[0].SurfaceBuildings.Add(Building(1,"science_lab",enabled:false,condition:.4,stored:0));},
    new Op("Upgrade",BuildingId:1),new Op("Advance",Budget:0,Days:5),new Op("Advance",Budget:0,Days:5.666666666666667));
Sequence("upgrade-zero-funding-pauses",(g,c)=>{g.Economies[0].LastBaseOperationsFundingFraction=0;g.Colonies[0].SurfaceBuildings.Add(Building(1,"science_lab"));},new Op("Upgrade",BuildingId:1),new Op("Advance",Budget:0,Days:100));
Sequence("upgrade-rejections",(g,c)=>{g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false));g.Colonies[0].SurfaceBuildings.Add(Building(2,"advanced_fabricator",x:180,z:180));g.Colonies[0].SurfaceBuildings.Add(Building(3,"fabricator",x:260,z:260,pending:"advanced_fabricator",upgradeDays:1));},
    new Op("Upgrade",BuildingId:1),new Op("Upgrade",BuildingId:2),new Op("Upgrade",BuildingId:3),new Op("Upgrade",BuildingId:99));
Sequence("upgrade-lock-and-insufficient",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator")),new Op("Upgrade",BuildingId:1));
Sequence("upgrade-insufficient-grouped-message",(g,c)=>{c.Add(1,"additive_manufacturing");g.Economies[0].Industry=1234.5;g.Economies[0].Credits=0;g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator"));},new Op("Upgrade",BuildingId:1));
Sequence("upgrade-insufficient-missing-civilization-currency",(g,c)=>{c.Add(1,"additive_manufacturing");g.Economies[0].Credits=0;((List<CivilizationState>)g.Civilizations).Clear();g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator"));},new Op("Upgrade",BuildingId:1));
Sequence("upgrade-affordable-skips-currency",(g,c)=>{c.Add(1,"additive_manufacturing");((List<CivilizationState>)g.Civilizations).Clear();g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator"));},new Op("Upgrade",BuildingId:1));

Sequence("hub-level1-authorize-and-complete",(g,c)=>g.ConstructionStates[0].CompletedProjectIds.Add("industrial_automation"),new Op("UpgradeHub"),new Op("Advance",Budget:0,Days:8.333333333333334));
Sequence("hub-level2-authorize-partial",(g,c)=>{g.Colonies[0].SurfaceHubLevel=2;c.Add(1,"orbital_industry");},new Op("UpgradeHub"),new Op("Advance",Budget:0,Days:10));
Sequence("hub-rejections",(g,c)=>{},new Op("UpgradeHub"));
Sequence("hub-already-running",(g,c)=>g.Colonies[0].SurfaceHubUpgradeDaysRemaining=1,new Op("UpgradeHub"));
Sequence("hub-max-and-outpost",(g,c)=>g.Colonies[0].SurfaceHubLevel=3,new Op("UpgradeHub"));
Sequence("hub-outpost-command",(g,c)=>g.Colonies[0].Kind=SettlementKind.ResourceOutpost,new Op("UpgradeHub"));
Sequence("hub-insufficient-materials",(g,c)=>{g.ConstructionStates[0].CompletedProjectIds.Add("industrial_automation");g.Economies[0].Industry=249.9998;},new Op("UpgradeHub"));
Sequence("hub-missing-civilization-currency",(g,c)=>{g.ConstructionStates[0].CompletedProjectIds.Add("industrial_automation");((List<CivilizationState>)g.Civilizations).Clear();},new Op("UpgradeHub"));

Sequence("repair-and-manage",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",condition:.5)),new Op("Repair",BuildingId:1),new Op("SetEnabled",BuildingId:1,Flag:false),new Op("SetEnabled",BuildingId:1,Flag:false),new Op("SetPriority",BuildingId:1,Flag:true),new Op("SetPriority",BuildingId:1,Flag:true));
Sequence("repair-already-full",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator")),new Op("Repair",BuildingId:1));
Sequence("repair-missing-economy",(g,c)=>{((List<CivilizationEconomyState>)g.Economies).Clear();g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",condition:.5));},new Op("Repair",BuildingId:1));
Sequence("repair-insufficient-grouped",(g,c)=>{g.Economies[0].Industry=1234.5;g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",condition:-10));},new Op("Repair",BuildingId:1));
Sequence("repair-negative-infinity-format",(g,c)=>{g.Economies[0].Industry=double.NegativeInfinity;g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",condition:.5));},new Op("Repair",BuildingId:1));
Sequence("repair-infinite-cost-format",(g,c)=>{g.Economies[0].Industry=1234;g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",condition:double.NegativeInfinity));},new Op("Repair",BuildingId:1));
Sequence("manage-incomplete",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false)),new Op("SetEnabled",BuildingId:1,Flag:false),new Op("SetPriority",BuildingId:1,Flag:true));
Sequence("priority-unknown-name-fallback",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"unknown",progress:0)),new Op("SetPriority",BuildingId:1,Flag:true));

Sequence("proportional-sorted-without-reorder",(g,c)=>{g.Economies[0].Industry=100;g.Colonies.Clear();g.Colonies.Add(Colony(Building(20,"fabricator",false,progress:0),Building(10,"science_lab",false,progress:0)));g.Colonies[0]=CloneWith(g.Colonies[0],id:2);var first=Colony(Building(30,"power_generator",false,progress:0));g.Colonies.Add(first);},new Op("Advance",Budget:45,Days:1));
Sequence("committed-upgrades-before-missing-economy",(g,c)=>{((List<CivilizationEconomyState>)g.Economies).Clear();g.Colonies[0].SurfaceHubUpgradeDaysRemaining=1;g.Colonies[0].SurfaceBuildings.Add(Building(1,"science_lab",pending:"advanced_science_lab",upgradeDays:1));},new Op("Advance",Budget:0,Days:1),new Op("Advance",Budget:1,Days:1));
Sequence("advance-invalid-values",(g,c)=>{},new Op("Advance",Budget:double.NaN,Days:1),new Op("Advance",Budget:1,Days:double.PositiveInfinity),new Op("Advance",Budget:-1,Days:1));
Query("demand-all-sites","Demand",(g,c)=>{g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false,progress:420));g.Colonies[0].SurfaceBuildings.Add(Building(2,"science_lab",false,x:180,z:180,progress:100));},1);
Query("demand-negative-days","Demand",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"fabricator",false)), -1);
Query("demand-unknown-site","Demand",(g,c)=>g.Colonies[0].SurfaceBuildings.Add(Building(1,"unknown",false,progress:0)),1);

Validate("validate-valid",c=>c.SurfaceBuildings.Add(Building(1,"fabricator")));
Validate("validate-hub-nan",c=>c.SurfaceHubUpgradeDaysRemaining=double.NaN); Validate("validate-hub-outpost",c=>{c.Kind=SettlementKind.ResourceOutpost;c.SurfaceHubUpgradeDaysRemaining=1;});
Validate("validate-id-zero",c=>c.SurfaceBuildings.Add(Building(0,"fabricator"))); Validate("validate-duplicate-id",c=>{c.SurfaceBuildings.Add(Building(1,"fabricator"));c.SurfaceBuildings.Add(Building(1,"science_lab",x:180,z:180));});
Validate("validate-placement",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",x:0,z:0))); Validate("validate-progress-nan",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",false,progress:double.NaN)));
Validate("validate-progress-complete-mismatch",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",false,progress:450))); Validate("validate-priority",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",priority:2)));
Validate("validate-condition",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",condition:1.1))); Validate("validate-pending-with-zero-timer",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",pending:"advanced_fabricator",upgradeDays:0)));
Validate("validate-timer-without-pending",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",upgradeDays:1))); Validate("validate-illegal-upgrade",c=>c.SurfaceBuildings.Add(Building(1,"science_lab",pending:"advanced_fabricator",upgradeDays:1)));
Validate("validate-storage",c=>c.SurfaceBuildings.Add(Building(1,"grid_battery",stored:12.000001)));
Validate("validate-storage-tolerance",c=>c.SurfaceBuildings.Add(Building(1,"grid_battery",stored:12.00000005)));
Validate("validate-upgrade-timer-nan",c=>c.SurfaceBuildings.Add(Building(1,"fabricator",pending:"advanced_fabricator",upgradeDays:double.NaN)));
Validate("validate-unknown-type",c=>c.SurfaceBuildings.Add(Building(1,"unknown",progress:0)));

File.WriteAllText(args[0],Json(new{Format="stellar-surface-construction-oracle-v1",Cases=cases})+Environment.NewLine);
Console.WriteLine($"Exported {cases.Count} surface construction cases.");

void SetBody(GalaxyState g,double gravity=1,double temperature=288,double pressure=101,PlanetaryAtmosphereRegime atmosphere=PlanetaryAtmosphereRegime.OxygenNitrogen,double radiation=0,double radius=1,bool solid=true) =>
    ((List<PlanetaryBodyState>)g.PlanetaryBodies)[0]=g.PlanetaryBodies[0] with {RadiusEarth=radius,Environment=new PlanetaryEnvironmentState(gravity,temperature,pressure,atmosphere,PlanetarySolventRegime.Water,radiation,false,solid)};
ColonyState CloneWith(ColonyState c,int? id=null,int? system=null) {var x=CloneColony(c);return new ColonyState {Id=id??x.Id,CivilizationId=x.CivilizationId,SystemId=system??x.SystemId,PlanetaryBodyId=x.PlanetaryBodyId,Name=x.Name,Kind=x.Kind,PopulationSpeciesId=x.PopulationSpeciesId,PopulationMillions=x.PopulationMillions,Infrastructure=x.Infrastructure,Stability=x.Stability,StoredFoodPopulationDaysMillions=x.StoredFoodPopulationDaysMillions,StoredWaterPopulationDaysMillions=x.StoredWaterPopulationDaysMillions,StoredExtractedMaterials=x.StoredExtractedMaterials,RemainingExtractableMaterials=x.RemainingExtractableMaterials,SurfaceHubLevel=x.SurfaceHubLevel,SurfaceHubUpgradeDaysRemaining=x.SurfaceHubUpgradeDaysRemaining,SurfaceBuildings=x.SurfaceBuildings};}

sealed class Caps : IConstructionCapabilityView {
    public Dictionary<int,List<string>> Values {get;}=new();
    public void Add(int civ,string id){if(!Values.TryGetValue(civ,out var list)){list=new();Values[civ]=list;}if(!list.Contains(id,StringComparer.Ordinal))list.Add(id);}
    public bool HasCivilizationCapability(GalaxyState galaxy,int civilizationId,string capabilityId)=>Values.TryGetValue(civilizationId,out var list)&&list.Contains(capabilityId,StringComparer.Ordinal);
}
sealed record Op(string Kind,int CivilizationId=1,int ColonyId=1,int BuildingId=0,string? TypeId=null,float X=0,float Z=0,float Rotation=0,bool Flag=false,double Budget=0,double Days=1);
