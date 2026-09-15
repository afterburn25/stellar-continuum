using System.Globalization;
using System.Text.Json;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
    if(args.Length!=1) throw new ArgumentException("Expected output fixture path");
    var profiles=SpeciesCatalog.All.Select(s=>new {
        s.Id,s.DisplayName,s.Biochemistry,GravityG=s.Environment.GravityG,TemperatureKelvin=s.Environment.TemperatureKelvin,
        PressureKPa=s.Environment.PressureKPa,s.Environment.PreferredAtmosphere,s.Environment.BiologicalSolvent,
        s.Environment.RequiresImmersion,s.Environment.CanOperateInVacuumUnprotected,s.Physiology.RadiationTolerance,
        BreathableAtmospheres=s.BreathableAtmospheres.OrderBy(x=>(int)x),CompatibleSolvents=s.CompatibleSolvents.OrderBy(x=>(int)x)}).ToArray();
    var habitats=new[]{new HabitatEnvironment(1,288,101.3,AtmosphereClass.OxygenNitrogen,SolventClass.Water,.06,false),
        new HabitatEnvironment(.85,282,350,AtmosphereClass.OxygenNitrogen,SolventClass.Water,.08,true),
        new HabitatEnvironment(1.75,300,160,AtmosphereClass.OxygenRich,SolventClass.Water,.1,false),
        new HabitatEnvironment(.14,94,150,AtmosphereClass.Reducing,SolventClass.Hydrocarbon,.12,false),
        new HabitatEnvironment(1,288,101.3,AtmosphereClass.Vacuum,SolventClass.None,.9,false)};
    var evaluator=new SpeciesEnvironmentEvaluator();
    var environmentCases=new List<object>();
    foreach(var s in SpeciesCatalog.All)
    {
        void Add(HabitatEnvironment h,PopulationAdaptationState? a=null) => environmentCases.Add(new {SpeciesId=s.Id,Habitat=h,Adaptation=a,Expected=evaluator.Evaluate(s,h,a)});
        foreach(var h in habitats) Add(h);
        var e=s.Environment;
        // Exact tolerance boundaries exercise comfortable/survivable interpolation on every continuous axis.
        foreach(var v in new[]{e.GravityG.Preferred+e.GravityG.ComfortableDeviation,e.GravityG.Preferred+e.GravityG.SurvivableDeviation})
            Add(new HabitatEnvironment(v,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,e.PreferredAtmosphere,e.BiologicalSolvent,s.Physiology.RadiationTolerance,e.RequiresImmersion));
        foreach(var v in new[]{e.TemperatureKelvin.Preferred+e.TemperatureKelvin.ComfortableDeviation,e.TemperatureKelvin.Preferred+e.TemperatureKelvin.SurvivableDeviation})
            Add(new HabitatEnvironment(e.GravityG.Preferred,v,e.PressureKPa.Preferred,e.PreferredAtmosphere,e.BiologicalSolvent,s.Physiology.RadiationTolerance,e.RequiresImmersion));
        foreach(var v in new[]{e.PressureKPa.Preferred+e.PressureKPa.ComfortableDeviation,e.PressureKPa.Preferred+e.PressureKPa.SurvivableDeviation})
            Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,v,e.PreferredAtmosphere,e.BiologicalSolvent,s.Physiology.RadiationTolerance,e.RequiresImmersion));
        Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,e.PreferredAtmosphere,e.BiologicalSolvent,s.Physiology.RadiationTolerance,e.RequiresImmersion));
        Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,e.PreferredAtmosphere,e.BiologicalSolvent,1.0,e.RequiresImmersion));
        Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,AtmosphereClass.Vacuum,SolventClass.None,s.Physiology.RadiationTolerance,false));
        Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,AtmosphereClass.CarbonDioxideRich,SolventClass.Ammonia,s.Physiology.RadiationTolerance,false));
        Add(new HabitatEnvironment(e.GravityG.Preferred,e.TemperatureKelvin.Preferred,e.PressureKPa.Preferred,e.PreferredAtmosphere,e.BiologicalSolvent,s.Physiology.RadiationTolerance,false));
        Add(new HabitatEnvironment(e.GravityG.Preferred+.1,e.TemperatureKelvin.Preferred+10,e.PressureKPa.Preferred+20,e.PreferredAtmosphere,e.BiologicalSolvent,Math.Min(1,s.Physiology.RadiationTolerance+.1),e.RequiresImmersion),
            new PopulationAdaptationState(s.Id,.05,.05,5,5,10,10,.1,.5));
    }
    var assignmentCases=(from seed in new[]{0L,1L,-1L,8374837L,long.MinValue,long.MaxValue} from civ in Enumerable.Range(0,16) from canonical in new[]{false,true}
        select new {Seed=seed,CivilizationId=civ,Canonical=canonical,Expected=canonical?SpeciesAssignmentPolicy.AssignNewCampaign(seed,civ):SpeciesAssignmentPolicy.Assign(seed,civ)}).ToArray();
    var solSystem=new StarSystemState(0,"Sol",System.Numerics.Vector2.Zero,StarArchetype.Standard,true,false,false,false,SolCatalogPreset.PresetId,StellarPrimaryClass.GYellowDwarf);
    var sol=SolCatalogPreset.Create(solSystem); var planetEvaluator=new PlanetarySpeciesHabitabilityEvaluator();
    var planetCases=SpeciesCatalog.All.SelectMany(s=>sol.Select(b=>new {SpeciesId=s.Id,Body=b,Expected=planetEvaluator.Evaluate(s,b)})).ToList();
    foreach(var s in SpeciesCatalog.All) {
        PlanetaryAtmosphereRegime Atmosphere(AtmosphereClass a) => a switch { AtmosphereClass.Vacuum=>PlanetaryAtmosphereRegime.Vacuum,AtmosphereClass.OxygenNitrogen=>PlanetaryAtmosphereRegime.OxygenNitrogen,AtmosphereClass.OxygenRich=>PlanetaryAtmosphereRegime.OxygenRich,AtmosphereClass.CarbonDioxideRich=>PlanetaryAtmosphereRegime.CarbonDioxideRich,AtmosphereClass.Reducing=>PlanetaryAtmosphereRegime.Reducing,AtmosphereClass.Inert=>PlanetaryAtmosphereRegime.Inert,_=>PlanetaryAtmosphereRegime.Other};
        PlanetarySolventRegime Solvent(SolventClass v) => v switch { SolventClass.None=>PlanetarySolventRegime.None,SolventClass.Water=>PlanetarySolventRegime.Water,SolventClass.Ammonia=>PlanetarySolventRegime.Ammonia,SolventClass.Hydrocarbon=>PlanetarySolventRegime.Hydrocarbon,_=>PlanetarySolventRegime.Other};
        PlanetaryBodyState Ideal(double gravity,bool solid=true,bool immersed=false) => new(900000+planetCases.Count,77,null,0,"Ideal",PlanetaryBodyKind.Planet,1,1,
            new PlanetaryEnvironmentState(gravity,s.Environment.TemperatureKelvin.Preferred,s.Environment.PressureKPa.Preferred,
                Atmosphere(s.Environment.PreferredAtmosphere),Solvent(s.Environment.BiologicalSolvent),s.Physiology.RadiationTolerance,immersed,solid),false,false,false,false);
        foreach(var score in new[]{.95,.949999,.20,.199999}) {
            var d=s.Environment.GravityG.ComfortableDeviation+(1-score)*(s.Environment.GravityG.SurvivableDeviation-s.Environment.GravityG.ComfortableDeviation);
            var body=Ideal(s.Environment.GravityG.Preferred+d,immersed:s.Environment.RequiresImmersion); planetCases.Add(new {SpeciesId=s.Id,Body=body,Expected=planetEvaluator.Evaluate(s,body)});
        }
        var noSolid=Ideal(s.Environment.GravityG.Preferred,false,s.Environment.RequiresImmersion); planetCases.Add(new {SpeciesId=s.Id,Body=noSolid,Expected=planetEvaluator.Evaluate(s,noSolid)});
        var noImmersion=Ideal(s.Environment.GravityG.Preferred,true,false); planetCases.Add(new {SpeciesId=s.Id,Body=noImmersion,Expected=planetEvaluator.Evaluate(s,noImmersion)});
    }
    var galaxy=new GalaxyGenerator().Generate(8374837,GalaxyGenerationMetadata.FullGalaxy500("fixture",0,systemCount:250).ToSettings());
    var species=SpeciesCatalog.All.Select(s=>s.Id).ToArray();
    var homes=new SpeciesHomeworldPlanner().Plan(galaxy.Systems,galaxy.PlanetaryBodies,species);
    var legacySystems=galaxy.Systems.Reverse().ToArray();
    var legacyBodies=galaxy.PlanetaryBodies.Reverse().ToArray();
    var legacySpecies=new[]{SpeciesCatalog.PelagicHighPressureId,SpeciesCatalog.CompactHighGravityId,SpeciesCatalog.CryogenicHydrocarbonId};
    var legacyHomes=new SpeciesHomeworldPlanner().Plan(legacySystems,legacyBodies,legacySpecies);
    var shuffledSystems=galaxy.Systems.OrderBy(s=>(s.Id*37)%251).ToArray();
    var shuffledBodies=galaxy.PlanetaryBodies.OrderBy(b=>(b.Id*97)%1009).ToArray();
    var shuffledHomes=new SpeciesHomeworldPlanner().Plan(shuffledSystems,shuffledBodies,species);
    var payload=new {Format="stellar-species-parity-v1",Profiles=profiles,EnvironmentCases=environmentCases,AssignmentCases=assignmentCases,PlanetCases=planetCases,
        HomeCases=new object[]{new {Kind="canonical",Seed=8374837L,Systems=galaxy.Systems,Bodies=galaxy.PlanetaryBodies,SpeciesIds=species,Expected=homes},
            new {Kind="legacy",Seed=8374837L,Systems=legacySystems,Bodies=legacyBodies,SpeciesIds=legacySpecies,Expected=legacyHomes},
            new {Kind="shuffled",Seed=8374837L,Systems=shuffledSystems,Bodies=shuffledBodies,SpeciesIds=species,Expected=shuffledHomes}}};
    File.WriteAllText(args[0],JsonSerializer.Serialize(payload,new JsonSerializerOptions { IncludeFields=true })+Environment.NewLine);
    Console.WriteLine($"Exported {profiles.Length} profiles, {environmentCases.Count} environmental cases, {assignmentCases.Length} assignments, {planetCases.Count} body assessments and {homes.Count} homes.");
    return 0;
}
catch(Exception exception) { Console.Error.WriteLine($"Species parity fixture failed: {exception}"); return 1; }
