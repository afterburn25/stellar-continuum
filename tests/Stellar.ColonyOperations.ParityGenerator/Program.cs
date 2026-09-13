using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    var options = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
    PlanetaryBodyState Body(int id, bool rare=true, double temperature=288, double gravity=1, double pressure=101, double radiation=.01, PlanetaryAtmosphereRegime atmosphere=PlanetaryAtmosphereRegime.OxygenNitrogen) => new(id, 7, null, 0, "Body"+id, PlanetaryBodyKind.Planet, 1, 1, new(gravity,temperature,pressure,atmosphere,PlanetarySolventRegime.Water,radiation,false,true),false,rare,false,false);
    ColonyState Colony(int? body, SettlementKind kind=SettlementKind.ResourceOutpost, params SurfaceBuildingState[] buildings) => new() { Id=31,CivilizationId=4,SystemId=7,PlanetaryBodyId=body,Name="Outpost",Kind=kind,PopulationSpeciesId="terran_baseline",PopulationMillions=1,Infrastructure=1,SurfaceHubLevel=1,SurfaceBuildings=buildings.ToList() };
    SurfaceBuildingState Building(int id,string type,bool complete=true,bool enabled=true,double condition=1) => new(){Id=id,TypeId=type,IsComplete=complete,IsEnabled=enabled,Condition=condition,IndustryProgress=complete ? SurfaceBuildingCatalog.Find(type)!.IndustryCost : 0};
    GalaxyState Galaxy(IReadOnlyList<PlanetaryBodyState> bodies, IReadOnlyList<CivilizationEconomyState>? economies=null) => new(){Seed=0,Systems=Array.Empty<StarSystemState>(),PlanetaryBodies=bodies,Civilizations=new List<CivilizationState>(),Fleets=new List<FleetState>(),Colonies=new List<ColonyState>(),Economies=economies??Array.Empty<CivilizationEconomyState>(),Technologies=new List<TechnologyState>(),ConstructionStates=new List<ConstructionState>(),ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=0,Knowledge=new Game.Simulation.Knowledge.CivilizationKnowledgeState()};
    ColonyState Clone(ColonyState source) => JsonSerializer.Deserialize<ColonyState>(JsonSerializer.Serialize(source,options),options)!;
    var cases=new List<object>();
    void Profile(string name, PlanetaryBodyState body) => cases.Add(new {Name=name,Kind="Profile",Body=body,Expected=ResourceDepositProfile.ForBody(body),ExpectedReserve=ResourceOutpostOperations.InitialDepositReserve(body)});
    Profile("profile-no-deposit",Body(1,false));
    var seen=new HashSet<string>();
    for(int id=2;id<2000 && seen.Count<24;id++) { var body=Body(id, true, id%7==0?150:288, .2+(id%9)*.35, id%11==0?0:101, (id%6)*.2); var p=ResourceDepositProfile.ForBody(body); if(seen.Add(p.MaterialName+"/"+p.Grade)) Profile("profile-"+p.MaterialName+"-"+p.Grade+"-"+id,body); }
    void Snapshot(string name, IReadOnlyList<PlanetaryBodyState> bodies, ColonyState colony, double? funding=null, IReadOnlyList<CivilizationEconomyState>? economies=null) {
       var galaxy=Galaxy(bodies,economies); var before=Clone(colony); var expected=ResourceOutpostOperations.GetSnapshot(galaxy,colony,funding); var after=Clone(colony);
       cases.Add(new {Name=name,Kind="Snapshot",Bodies=bodies,Economies=economies??Array.Empty<CivilizationEconomyState>(),Colony=before,Funding=funding,Expected=expected,ExpectedColonyBefore=before,ExpectedColonyAfter=after});
    }
    var rich=Body(71); Snapshot("ordinary",new[]{rich},Colony(rich.Id,SettlementKind.Colony));
    Snapshot("missing-body",Array.Empty<PlanetaryBodyState>(),Colony(71)); Snapshot("no-deposit",new[]{Body(72,false)},Colony(72));
    Snapshot("needs-fabricator",new[]{rich},Colony(rich.Id));
    Snapshot("offline-fabricator",new[]{rich},Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"fabricator",enabled:false)));
    Snapshot("powered-extraction",new[]{rich},Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")));
    Snapshot("partial-funding",new[]{rich},Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")),.125);
    foreach (var funding in new[] { .005, .125, .145, .995 }) Snapshot("percentage-rounding-"+funding,new[]{rich},Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")),funding);
    var full=Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")); full.StoredExtractedMaterials=125; Snapshot("full-storage",new[]{rich},full);
    var depleted=Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")); depleted.RemainingExtractableMaterials=0; Snapshot("depleted",new[]{rich},depleted);
    var econ=new CivilizationEconomyState { CivilizationId=4,LastBaseOperationsFundingFraction=.25 }; Snapshot("economy-default-funding",new[]{rich},Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")),null,new[]{econ});
    Snapshot("wrong-system-body",new[]{rich with { SystemId=8 }},Colony(rich.Id));
    void Advance(string name, ColonyState colony, double days, double funding, IReadOnlyList<PlanetaryBodyState> bodies) { var before=Clone(colony); ResourceOutpostOperations.Advance(Galaxy(bodies),colony,days,funding); cases.Add(new {Name=name,Kind="Advance",Bodies=bodies,Economies=Array.Empty<CivilizationEconomyState>(),Colony=before,Days=days,Funding=funding,ExpectedColonyBefore=before,ExpectedColonyAfter=Clone(colony)}); }
    Advance("advance-extraction",Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")),10,1,new[]{rich});
    var reserve=Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")); reserve.RemainingExtractableMaterials=.25; Advance("advance-depletion",reserve,10,1,new[]{rich});
    var capped=Colony(rich.Id,SettlementKind.ResourceOutpost,Building(1,"power_generator"),Building(2,"fabricator")); capped.StoredExtractedMaterials=124.9; Advance("advance-storage-cap",capped,10,1,new[]{rich});
    Advance("advance-early-ordinary",Colony(rich.Id,SettlementKind.Colony),1,double.NaN,new[]{rich}); Advance("advance-early-zero-days",Colony(rich.Id),0,double.NaN,new[]{rich});
    foreach(var funding in new[]{double.NaN,double.PositiveInfinity,-.1,1.1}) { Exception? error=null; try { ResourceOutpostOperations.GetSnapshot(Galaxy(new[]{rich}),Colony(rich.Id),funding); } catch(Exception x) { error=x; } if(error is null) throw new InvalidOperationException("Snapshot invalid-funding oracle unexpectedly succeeded."); cases.Add(new {Name="invalid-snapshot-"+funding,Kind="Snapshot",ErrorCategory="Funding",Bodies=new[]{rich},Economies=Array.Empty<CivilizationEconomyState>(),Colony=Colony(rich.Id),Funding=funding,ExpectedError=error.Message}); }
    foreach(var funding in new[]{double.NaN,double.PositiveInfinity,-.1,1.1}) { Exception? error=null; try { ResourceOutpostOperations.Advance(Galaxy(new[]{rich}),Colony(rich.Id),1,funding); } catch(Exception x) { error=x; } if(error is null) throw new InvalidOperationException("Advance invalid-funding oracle unexpectedly succeeded."); cases.Add(new {Name="invalid-advance-"+funding,Kind="Advance",ErrorCategory="Funding",Bodies=new[]{rich},Economies=Array.Empty<CivilizationEconomyState>(),Colony=Colony(rich.Id),Days=1d,Funding=funding,ExpectedError=error.Message}); }
    void Wear(string name, PlanetaryBodyState? body, ColonyState colony, double funding,double days) { var bodies=body is null?Array.Empty<PlanetaryBodyState>():new[]{body}; var before=Clone(colony); var value=SurfaceConstruction.GetEnvironmentalWearMultiplier(Galaxy(bodies),colony); SurfaceConstruction.AdvanceCondition(Galaxy(bodies),colony,funding,days); cases.Add(new {Name=name,Kind="Wear",Bodies=bodies,Colony=before,Funding=funding,Days=days,ExpectedMultiplier=value,ExpectedColonyBefore=before,ExpectedColonyAfter=Clone(colony)}); }
    var earth=Body(80,true,288,1,101,.01); Wear("wear-neutral",earth,Colony(earth.Id,SettlementKind.Colony,Building(1,"fabricator"),Building(2,"science_lab",enabled:false),Building(3,"power_generator",complete:false)),0,100);
    var mars=Body(81,true,180,.38,0,.6,PlanetaryAtmosphereRegime.Vacuum); Wear("wear-harsh-partial",mars,Colony(mars.Id,SettlementKind.Colony,Building(1,"fabricator",condition:.3)),.25,100);
    foreach (var pressure in new[] { 20d, 300d, 19.999d, 300.001d }) Wear("wear-pressure-threshold-"+pressure,Body(90+(int)(pressure*10),true,288,1,pressure,.1),Colony(90+(int)(pressure*10),SettlementKind.Colony,Building(1,"fabricator")),0,1);
    foreach (var temperature in new[] { 240d, 330d, 239.999d, 330.001d }) Wear("wear-temperature-threshold-"+temperature,Body(100+(int)(temperature*10),true,temperature,1,101,.1),Colony(100+(int)(temperature*10),SettlementKind.Colony,Building(1,"fabricator")),0,1);
    Wear("wear-missing-body",null,Colony(999,SettlementKind.Colony,Building(1,"fabricator")),0,1); Wear("wear-full-funding",earth,Colony(earth.Id,SettlementKind.Colony,Building(1,"fabricator")),1,100); Wear("wear-zero-days",earth,Colony(earth.Id,SettlementKind.Colony,Building(1,"fabricator")),0,0);
    foreach(var value in new[]{double.NaN,double.PositiveInfinity,-.1,1.1}) { Exception? error=null; try { SurfaceConstruction.AdvanceCondition(Galaxy(new[]{earth}),Colony(earth.Id),value,1); } catch(Exception x) { error=x; } if(error is null) throw new InvalidOperationException("Wear invalid-funding oracle unexpectedly succeeded."); cases.Add(new {Name="invalid-wear-funding-"+value,Kind="Wear",ErrorCategory="Maintenance",Bodies=new[]{earth},Colony=Colony(earth.Id),Funding=value,Days=1d,ExpectedError=error.Message}); }
    File.WriteAllText(args[0],JsonSerializer.Serialize(new {Format="stellar-colony-operations-parity-v1",Cases=cases},options)+Environment.NewLine); Console.WriteLine($"Exported {cases.Count} colony operations cases."); return 0;
} catch(Exception x) { Console.Error.WriteLine(x); return 1; }
