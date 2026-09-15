using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Colonization;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output path.");
var options = new JsonSerializerOptions { IncludeFields=true,
    NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
var cases = new List<object>();

StarSystemState System(int id) => new(id,$"System {id}",new Vector2(id*3,0),
    StarArchetype.Standard,true,false,false,false,null,StellarPrimaryClass.GYellowDwarf,
    null,null,null,$"fixture:{id}");
PlanetaryBodyState Body(int id,int system,double gravity=1,double temperature=288,
    double pressure=101.3,bool legacy=true,bool solid=true,bool native=false,
    PlanetaryAtmosphereRegime atmosphere=PlanetaryAtmosphereRegime.OxygenNitrogen,
    PlanetarySolventRegime solvent=PlanetarySolventRegime.Water,double radiation=.1) =>
    new(id,system,null,id,$"Body {id}",PlanetaryBodyKind.Planet,1,1,
        new PlanetaryEnvironmentState(gravity,temperature,pressure,atmosphere,
            solvent,radiation,false,solid),legacy,false,false,native);
CivilizationState Civ(int id=1,string species="terran_baseline") => new(id,$"Civ {id}",0,
    CivilizationArchetype.Adaptive,CivilizationTraits.Balanced,true,
    CivilizationDevelopmentStage.WarpCapable,SpeciesId:species);
ColonyState Colony(int id,int civilization,int system,string species,double population) => new()
{
    Id=id,CivilizationId=civilization,SystemId=system,Name=$"Colony {id}",
    PopulationSpeciesId=species,PopulationMillions=population,
};
FleetState Fleet(int id,int civilization=1,FleetRole role=FleetRole.Colony,
    int? current=null,int? destination=null,string? species="terran_baseline",double population=1) => new()
{
    Id=id,CivilizationId=civilization,Name=$"Fleet {id}",Role=role,
    Position=Vector2.Zero,
    CurrentSystemId=current,DestinationSystemId=destination,
    EmbarkedPopulationSpeciesId=species,EmbarkedPopulationMillions=population,
};
GalaxyState World() => new()
{
    Seed=28,Systems=[System(0),System(1),System(2)],
    PlanetaryBodies=new List<PlanetaryBodyState>{Body(20,2),Body(11,1,gravity:1.4),Body(10,1),
        Body(12,1,temperature:700,pressure:0,legacy:true),
        Body(13,1,temperature:700,pressure:0,legacy:false,solid:false),
        Body(14,1,native:true)},
    Civilizations=[Civ(1),Civ(2,"pelagic_high_pressure")],Fleets=[],Colonies=[],
    Economies=[],Technologies=[],ConstructionStates=[],ShipyardStates=[],
    PlayerCivilizationId=1,Knowledge=new CivilizationKnowledgeState(),
};

object Observe(GalaxyState g)
{
    var observers=g.Civilizations.Select(c=>c.Id).Distinct().ToArray();
    return new
    {
        Systems=g.Systems.Select(s=>new{s.Id,s.Name,Position=new{s.Position.X,s.Position.Y},
            s.Archetype,s.HasHabitableWorld,s.HasAnomaly,s.HasRareResource,
            s.HasPreWarpCivilization,s.CatalogPresetId,s.StellarClass,
            s.SecondaryStellarClass,s.TertiaryStellarClass,s.GalacticDepthLightYears,
            s.StellarCatalogId}).ToArray(),
        Bodies=g.PlanetaryBodies,
        Civilizations=g.Civilizations.Select(c=>new{c.Id,c.Name,c.HomeSystemId,c.Archetype,
            Traits=c.Traits,c.IsPlayer,c.DevelopmentStage,c.IsSeededAncient,
            c.ExpansionAllowed,c.NeutralUnlessProvoked,c.SpeciesId}).ToArray(),
        Colonies=g.Colonies,
        Fleets=g.Fleets,
        Knowledge=observers.Select(id=>new{CivilizationId=id,
            Surveys=g.Knowledge.GetSystemSurveyKnowledge(id)}).ToArray(),
    };
}

void FullSurvey(GalaxyState g,int civilization,int system) =>
    g.Knowledge.MarkSystemFullySurveyed(civilization,system);

void Add(string name,string kind,Action<GalaxyState> arrange,int observer=1,
    string? species="terran_baseline",int system=1,int requestingFleetId=100)
{
    var g=World(); arrange(g);
    FleetState? requesting = kind is "BuildReservations" or "TryReservation"
        ? g.Fleets.FirstOrDefault(f=>f.Id==requestingFleetId) ?? Fleet(requestingFleetId)
        : null;
    var before=Freeze(Observe(g));
    var arguments=Freeze(new{ObserverCivilizationId=observer,SpeciesId=species,SystemId=system,
        RequestingFleet=requesting,World=before});
    object? raw=null,error=null;
    PlanetaryBodyState? resolvedBody=null;
    var resolveCompleted=false;
    IReadOnlyDictionary<int,int>? reservations=null;
    bool? reservationFound=null;
    var reservingFleetId=0;
    try
    {
        if(kind=="BuildForSpecies") raw=new SpeciesPlanetaryReadModel().BuildForSpecies(g,observer,species!);
        else if(kind=="BuildForAvailablePopulations") raw=new SpeciesPlanetaryReadModel().BuildForAvailablePopulations(g,observer);
        else if(kind=="ResolveBestAvailableBody"){resolvedBody=new ColonySettlementBodyResolver().ResolveBestAvailableBody(g,observer,system,species!);resolveCompleted=true;}
        else if(kind=="BuildReservations") reservations=FriendlyColonyMissionReservations.BuildBySystem(g,requesting!);
        else if(kind=="TryReservation") reservationFound=FriendlyColonyMissionReservations.TryGetReservingFleetId(g,requesting!,system,out reservingFleetId);
        else throw new InvalidOperationException("generator kind");
    }
    catch(Exception ex){error=new{Type=ex.GetType().Name,ex.Message};}
    object? frozenResultSource=raw;
    if(resolveCompleted) frozenResultSource=resolvedBody?.Id;
    else if(reservations is not null) frozenResultSource=reservations.Select(entry=>new{SystemId=entry.Key,FleetId=entry.Value}).ToArray();
    else if(reservationFound is not null) frozenResultSource=new{Found=reservationFound.Value,ReservingFleetId=reservingFleetId};
    JsonElement? result=frozenResultSource is null?null:Freeze(frozenResultSource);
    var after=Freeze(Observe(g));
    cases.Add(new{Name=name,Kind=kind,Arguments=arguments,Result=result,Error=error,
        Before=before,After=after});
}

Add("species-unknown","BuildForSpecies",_=>{},species:"missing");
Add("species-unknown-observer-after-species","BuildForSpecies",_=>{},observer:99,species:"missing");
Add("species-valid-unknown-observer","BuildForSpecies",_=>{},observer:99);
Add("species-no-survey","BuildForSpecies",_=>{});
Add("species-detected-private","BuildForSpecies",g=>g.Knowledge.RevealSystem(1,1));
Add("species-partial-private","BuildForSpecies",g=>g.Knowledge.AdvanceSystemSurvey(1,1,.8));
Add("species-full-system1-sorted","BuildForSpecies",g=>FullSurvey(g,1,1));
Add("species-full-two-systems","BuildForSpecies",g=>{FullSurvey(g,1,2);FullSurvey(g,1,1);});
foreach(var id in new[]{"terran_baseline","pelagic_high_pressure","compact_high_gravity","cryogenic_hydrocarbon"})
    Add($"species-profile-{id}","BuildForSpecies",g=>FullSurvey(g,1,1),species:id);
Add("species-duplicate-observer-valid","BuildForSpecies",g=>{g.Civilizations.Insert(0,Civ(1));FullSurvey(g,1,1);});

Add("available-unknown-observer","BuildForAvailablePopulations",_=>{},observer:99);
Add("available-home-only","BuildForAvailablePopulations",g=>FullSurvey(g,1,1));
Add("available-positive-owned","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"pelagic_high_pressure",1));});
Add("available-zero-excluded","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"missing",0));});
Add("available-negative-excluded","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"missing",-1));});
Add("available-nan-excluded","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"missing",double.NaN));});
Add("available-infinity-included-error","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"missing",double.PositiveInfinity));});
Add("available-foreign-excluded","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,2,0,"missing",1));});
Add("available-distinct-ordinal-sorted","BuildForAvailablePopulations",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,0,"pelagic_high_pressure",1));g.Colonies.Add(Colony(2,1,0,"terran_baseline",2));g.Colonies.Add(Colony(3,1,0,"compact_high_gravity",3));});
Add("available-invalid-home","BuildForAvailablePopulations",g=>{g.Civilizations[0]=Civ(1,"missing");FullSurvey(g,1,1);});

Add("resolve-unknown-species-first","ResolveBestAvailableBody",_=>{},observer:99,system:99,species:"missing");
Add("resolve-unknown-civilization","ResolveBestAvailableBody",_=>{},observer:99);
Add("resolve-missing-system","ResolveBestAvailableBody",_=>{},system:99);
Add("resolve-private","ResolveBestAvailableBody",_=>{});
Add("resolve-occupied-own","ResolveBestAvailableBody",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,1,1,"terran_baseline",1));});
Add("resolve-occupied-foreign","ResolveBestAvailableBody",g=>{FullSurvey(g,1,1);g.Colonies.Add(Colony(1,2,1,"pelagic_high_pressure",1));});
Add("resolve-ranked-terran","ResolveBestAvailableBody",g=>FullSurvey(g,1,1));
Add("resolve-ranked-compact","ResolveBestAvailableBody",g=>FullSurvey(g,1,1),species:"compact_high_gravity");
Add("resolve-no-bodies","ResolveBestAvailableBody",g=>{FullSurvey(g,1,0);},system:0);
Add("resolve-input-permutation","ResolveBestAvailableBody",g=>{FullSurvey(g,1,1);((List<PlanetaryBodyState>)g.PlanetaryBodies).Reverse();});

void BaseReservation(GalaxyState g){g.Fleets.Add(Fleet(100));}
Add("reservations-empty","BuildReservations",BaseReservation);
Add("reservations-remote-committed","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(9,destination:99,species:"",population:1));});
Add("reservations-filter-requesting-id","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(100,destination:1));});
Add("reservations-filter-foreign","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,2,destination:1));});
Add("reservations-filter-inactive","BuildReservations",g=>{BaseReservation(g);var f=Fleet(1,destination:1);f.IsActive=false;g.Fleets.Add(f);});
Add("reservations-filter-role","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,role:FleetRole.Scout,destination:1));});
Add("reservations-filter-population-zero","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,destination:1,population:0));});
Add("reservations-filter-population-nan","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,destination:1,population:double.NaN));});
Add("reservations-remote-infinity-population","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,destination:1,population:double.PositiveInfinity));});
Add("reservations-remote-before-local-occupancy","BuildReservations",g=>{BaseReservation(g);g.Colonies.Add(Colony(1,2,1,"terran_baseline",1));g.Fleets.Add(Fleet(1,current:1,destination:2,species:"missing"));});
Add("reservations-local-occupied","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);g.Colonies.Add(Colony(1,2,1,"terran_baseline",1));g.Fleets.Add(Fleet(1,current:1));});
Add("reservations-local-private","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(1,current:1));});
Add("reservations-local-invalid-species","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);g.Fleets.Add(Fleet(1,current:1,species:"missing"));});
Add("reservations-local-blank-species","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);g.Fleets.Add(Fleet(1,current:1,species:" "));});
Add("reservations-local-any-viable","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);g.Fleets.Add(Fleet(7,current:1));});
Add("reservations-local-explicit-viable","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);var f=Fleet(7,current:1);f.DestinationPlanetaryBodyId=10;g.Fleets.Add(f);});
Add("reservations-local-explicit-wrong-system","BuildReservations",g=>{BaseReservation(g);FullSurvey(g,1,1);var f=Fleet(7,current:1);f.DestinationPlanetaryBodyId=20;g.Fleets.Add(f);});
Add("reservations-lowest-fleet-per-system","BuildReservations",g=>{BaseReservation(g);g.Fleets.Add(Fleet(20,destination:2));g.Fleets.Add(Fleet(3,destination:2));g.Fleets.Add(Fleet(8,destination:1));});
Add("reservations-hold-and-prevent-not-excluded","BuildReservations",g=>{BaseReservation(g);var f=Fleet(3,destination:2);f.HoldRequested=true;f.PreventAutomaticSettlement=true;g.Fleets.Add(f);});
Add("try-found","TryReservation",g=>{BaseReservation(g);g.Fleets.Add(Fleet(3,destination:2));},system:2);
Add("try-not-found-default-zero","TryReservation",g=>{BaseReservation(g);g.Fleets.Add(Fleet(3,destination:2));},system:1);

var nulls=new List<object>();
foreach(var kind in new[]{"BuildForSpecies","BuildForAvailablePopulations","ResolveBestAvailableBody","BuildReservations","TryReservation"})
{
    object? error=null;
    try
    {
        if(kind=="BuildForSpecies")_ = new SpeciesPlanetaryReadModel().BuildForSpecies(null!,1,"terran_baseline");
        else if(kind=="BuildForAvailablePopulations")_ = new SpeciesPlanetaryReadModel().BuildForAvailablePopulations(null!,1);
        else if(kind=="ResolveBestAvailableBody")_ = new ColonySettlementBodyResolver().ResolveBestAvailableBody(null!,1,1,"terran_baseline");
        else if(kind=="BuildReservations")_=FriendlyColonyMissionReservations.BuildBySystem(null!,Fleet(1));
        else _=FriendlyColonyMissionReservations.TryGetReservingFleetId(null!,Fleet(1),1,out _);
    }catch(Exception ex){error=new{Type=ex.GetType().Name,ex.Message};}
    nulls.Add(new{Kind=kind,Error=error});
}
var fleetNulls=new List<object>();
foreach(var kind in new[]{"BuildReservations","TryReservation"})
{
    object? error=null;
    try{if(kind=="BuildReservations")_=FriendlyColonyMissionReservations.BuildBySystem(World(),null!);else _=FriendlyColonyMissionReservations.TryGetReservingFleetId(World(),null!,1,out _);}
    catch(Exception ex){error=new{Type=ex.GetType().Name,ex.Message};}
    fleetNulls.Add(new{Kind=kind,Error=error});
}
var speciesNulls=new List<object>();
foreach(var kind in new[]{"BuildForSpecies","ResolveBestAvailableBody"})
{
    object? error=null;
    try{if(kind=="BuildForSpecies")_=new SpeciesPlanetaryReadModel().BuildForSpecies(World(),1,null!);else _=new ColonySettlementBodyResolver().ResolveBestAvailableBody(World(),1,1,null!);}
    catch(Exception ex){error=new{Type=ex.GetType().Name,ex.Message};}
    speciesNulls.Add(new{Kind=kind,Error=error});
}

File.WriteAllText(args[0],JsonSerializer.Serialize(new{
    Format="stellar-settlement-knowledge-oracle-v1",Cases=cases,
    SourceOnlyNullGalaxy=nulls,SourceOnlyNullRequestingFleet=fleetNulls,
    SourceOnlyNullSpecies=speciesNulls,
    NativeBoundary="Typed native references cannot represent null galaxy or requesting fleet."
},options)+Environment.NewLine);
