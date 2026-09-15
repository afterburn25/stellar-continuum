using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Text.Json;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

try {
    CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
    if(args.Length!=1) throw new ArgumentException("Expected output fixture path");
    object Projection(StarSystemState s)=>new {s.Id,s.Name,X=s.Position.X,Y=s.Position.Y,Depth=s.GalacticDepthLightYears,
        Primary=s.StellarClass,Secondary=s.SecondaryStellarClass,Tertiary=s.TertiaryStellarClass,s.CatalogPresetId,s.StellarCatalogId,
        s.Archetype,s.HasHabitableWorld,s.HasAnomaly,s.HasRareResource,s.HasPreWarpCivilization};
    object[] CompactBody(PlanetaryBodyState b)=>new object[]{b.Id,b.SystemId,b.ParentBodyId!,b.OrbitIndex,b.Name,(int)b.Kind,b.RadiusEarth,b.MassEarth,
        b.Environment.GravityG,b.Environment.TemperatureKelvin,b.Environment.PressureKPa,(int)b.Environment.Atmosphere,(int)b.Environment.AvailableSolvent,
        b.Environment.RadiationHazard,b.Environment.IsImmersedEnvironment,b.Environment.HasSolidSurface,b.LegacyColonizationCandidate,b.HasRareResource,b.HasAnomaly,
        b.HasPreWarpCivilization,b.OrbitalEccentricity,b.OrbitalInclinationDegrees};
    StarSystemState ReadSystem(JsonElement e)=>new(e.GetProperty("Id").GetInt32(),e.GetProperty("Name").GetString()!,
        new(e.GetProperty("X").GetSingle(),e.GetProperty("Y").GetSingle()),(StarArchetype)e.GetProperty("Archetype").GetInt32(),
        e.GetProperty("HasHabitableWorld").GetBoolean(),e.GetProperty("HasAnomaly").GetBoolean(),e.GetProperty("HasRareResource").GetBoolean(),e.GetProperty("HasPreWarpCivilization").GetBoolean(),
        e.TryGetProperty("CatalogPresetId",out var p)&&p.ValueKind!=JsonValueKind.Null?p.GetString():null,
        e.TryGetProperty("Primary",out p)&&p.ValueKind!=JsonValueKind.Null?(StellarPrimaryClass)p.GetInt32():null,
        e.TryGetProperty("Secondary",out p)&&p.ValueKind!=JsonValueKind.Null?(StellarPrimaryClass)p.GetInt32():null,
        e.TryGetProperty("Tertiary",out p)&&p.ValueKind!=JsonValueKind.Null?(StellarPrimaryClass)p.GetInt32():null,
        e.TryGetProperty("Depth",out p)&&p.ValueKind!=JsonValueKind.Null?p.GetDouble():null,
        e.TryGetProperty("StellarCatalogId",out p)&&p.ValueKind!=JsonValueKind.Null?p.GetString():null);
    var galaxyFixture=Path.Combine(Directory.GetCurrentDirectory(),"native-tests","fixtures","galaxy-catalog.json");
    using var input=JsonDocument.Parse(File.ReadAllText(galaxyFixture));
    var physical=input.RootElement.GetProperty("Cases").EnumerateArray().Where(c=>new[]{250,500,1000,2500}.Contains(c.GetProperty("Count").GetInt32()))
        .GroupBy(c=>c.GetProperty("Count").GetInt32()).Select(g=>g.First()).OrderBy(c=>c.GetProperty("Count").GetInt32()).ToArray();
    var cases=new List<object>();
    var seeder=new CivilizationSeeder(); var bodiesGenerator=new PlanetaryBodyGenerator();
    var players=new[]{SpeciesCatalog.TerranBaselineId,SpeciesCatalog.PelagicHighPressureId,SpeciesCatalog.CompactHighGravityId,SpeciesCatalog.CryogenicHydrocarbonId};
    var seeds=new[]{0L,-1L,long.MinValue,long.MaxValue};
    // One direct seeding matrix covers every player identity and signed seed boundary without
    // duplicating four enormous physical catalogs; full founding cases below cover all map sizes.
    foreach(var entry in physical.Take(1)) {
        var count=entry.GetProperty("Count").GetInt32(); var systems=entry.GetProperty("Systems").EnumerateArray().Select(ReadSystem).ToArray();
        var seed=seeds[Array.IndexOf(new[]{250,500,1000,2500},count)]; var bodies=bodiesGenerator.Generate(seed,systems);
        foreach(var (player,caseSeed) in players.Zip(seeds)) {
            var pre=player==SpeciesCatalog.TerranBaselineId?6:1;
            try { var output=seeder.Seed(systems,bodies,pre,1,caseSeed,player); cases.Add(new {Name=$"seed-{count}-{player}",Kind="Seed",Seed=caseSeed,Systems=systems.Select(Projection),Bodies=bodies.Select(CompactBody),PreWarpCount=pre,AncientCount=1,PlayerSpeciesId=player,ExpectedCivilizations=output}); }
            catch(Exception x) { cases.Add(new {Name=$"seed-{count}-{player}",Kind="Seed",Seed=caseSeed,Systems=systems.Select(Projection),Bodies=bodies.Select(CompactBody),PreWarpCount=pre,AncientCount=1,PlayerSpeciesId=player,ExpectedError=x.Message}); }
        }
    }
    var zeroAncientSystems=physical[0].GetProperty("Systems").EnumerateArray().Select(ReadSystem).ToArray(); var zeroAncientBodies=bodiesGenerator.Generate(1,zeroAncientSystems);
    cases.Add(new {Name="seed-zero-ancients",Kind="Seed",Seed=1L,Systems=zeroAncientSystems.Select(Projection),Bodies=zeroAncientBodies.Select(CompactBody),PreWarpCount=1,AncientCount=0,PlayerSpeciesId=SpeciesCatalog.TerranBaselineId,ExpectedCivilizations=seeder.Seed(zeroAncientSystems,zeroAncientBodies,1,0,1)});
    // Maximum template deck and legacy physical catalog retain the two boundary policies.
    var maxSystems=physical[1].GetProperty("Systems").EnumerateArray().Select(ReadSystem).ToArray(); var maxBodies=bodiesGenerator.Generate(8374837,maxSystems);
    cases.Add(new {Name="seed-max-13-plus-3",Kind="Seed",Seed=8374837L,Systems=maxSystems.Select(Projection),Bodies=maxBodies.Select(CompactBody),PreWarpCount=13,AncientCount=3,PlayerSpeciesId=SpeciesCatalog.TerranBaselineId,ExpectedCivilizations=seeder.Seed(maxSystems,maxBodies,13,3,8374837)});
    var legacy=maxSystems.Select(s=>s with {CatalogPresetId=null,StellarCatalogId=null,StellarClass=null,SecondaryStellarClass=null,TertiaryStellarClass=null}).Reverse().ToArray(); var legacyBodies=bodiesGenerator.Generate(-1,legacy);
    cases.Add(new {Name="seed-legacy",Kind="Seed",Seed=-1L,Systems=legacy.Select(Projection),Bodies=legacyBodies.Select(CompactBody),PreWarpCount=6,AncientCount=1,PlayerSpeciesId=SpeciesCatalog.PelagicHighPressureId,ExpectedCivilizations=seeder.Seed(legacy,legacyBodies,6,1,-1,SpeciesCatalog.PelagicHighPressureId)});
    // Founding expectations invoke the complete authoritative GalaxyGenerator, not a reconstructed pipeline.
    foreach(var (count,player,pre,ancient) in new[]{(250,SpeciesCatalog.TerranBaselineId,6,1),(500,SpeciesCatalog.PelagicHighPressureId,1,1),(1000,SpeciesCatalog.CompactHighGravityId,6,1),(2500,SpeciesCatalog.CryogenicHydrocarbonId,6,1)}) {
        var settings=(GalaxyGenerationMetadata.FullGalaxy500("fixture",8374837,player,count) with {OtherCivilizations=pre-1,AncientCivilizations=ancient==0?"None":"Rare"}).ToSettings();
        var galaxy=new GalaxyGenerator().Generate(8374837,settings);
        cases.Add(new {Name=$"founding-full-{count}-{player}",Kind="Founding",Seed=8374837L,Systems=physical.First(x=>x.GetProperty("Count").GetInt32()==count).GetProperty("Systems").EnumerateArray().Select(ReadSystem).Select(Projection),PreWarpCount=pre,AncientCount=ancient,PlayerSpeciesId=player,ExpectedSystems=galaxy.Systems.Select(Projection),ExpectedBodies=galaxy.PlanetaryBodies.Select(CompactBody),ExpectedCivilizations=galaxy.Civilizations});
    }
    PlanetaryBodyState World(int id,int system,string name,bool pre=false,double gravity=1,double radius=1)=>new(id,system,null,0,name,PlanetaryBodyKind.Planet,radius,gravity*radius*radius,new(gravity,288,101.3,PlanetaryAtmosphereRegime.OxygenNitrogen,PlanetarySolventRegime.Water,.06,false,true),false,false,false,pre);
    CivilizationState Civ(int id,int home)=>new(id,$"Civ {id}",home,CivilizationArchetype.Adaptive,new(.2,.2,.2,.2,.2,1),id==0,CivilizationDevelopmentStage.PreWarp,SpeciesId:SpeciesCatalog.TerranBaselineId);
    object Guarantee(string name,StarSystemState[] systems,PlanetaryBodyState[] bodies,CivilizationState[] civs,int count=2,bool assertGlobal=false) {
        var mutable=systems.ToList(); try {
            if(assertGlobal) {
                var greedy=typeof(NearbyHabitableWorldGuaranteePolicy).GetMethod("ApplyGreedy",BindingFlags.Instance|BindingFlags.NonPublic)!;
                try { greedy.Invoke(new NearbyHabitableWorldGuaranteePolicy(),new object[]{7L,systems.ToList(),bodies,civs,count}); throw new InvalidOperationException("Advertised global fixture did not make the source greedy policy fail."); }
                catch(TargetInvocationException invocation) when(invocation.InnerException is InvalidOperationException failure && failure.Message.StartsWith("Could not place ",StringComparison.Ordinal)) { }
            }
            var output=new NearbyHabitableWorldGuaranteePolicy().Apply(7,mutable,bodies,civs,count);
            if(assertGlobal && !output.Zip(bodies).Any(pair=>pair.First.MassEarth!=pair.Second.MassEarth || pair.First.Environment!=pair.Second.Environment)) throw new InvalidOperationException("Global fallback fixture did not mutate a fallback body.");
            return new {Name=name,Kind="Guarantee",Seed=7L,Systems=systems.Select(Projection),Bodies=bodies.Select(CompactBody),Civilizations=civs,GuaranteedCount=count,ExpectedSystems=mutable.Select(Projection),ExpectedBodies=output.Select(CompactBody)}; }
        catch(Exception x) { return new {Name=name,Kind="Guarantee",Seed=7L,Systems=systems.Select(Projection),Bodies=bodies.Select(CompactBody),Civilizations=civs,GuaranteedCount=count,ExpectedError=x.Message,ExpectedSystems=mutable.Select(Projection)}; }
    }
    StarSystemState Stable(int id,string name,float x,double? depth=null)=>new(id,name,new(x,0),StarArchetype.Standard,false,false,false,false,StellarClass:StellarPrimaryClass.GYellowDwarf,GalacticDepthLightYears:depth);
    var guaranteeSystems=new[]{Stable(0,"Home A",0),Stable(1,"Home B",500),Stable(2,"Shared near",250),Stable(3,"Shared far",260),Stable(4,"A fallback 1",-100),Stable(5,"A fallback 2",-120),Stable(6,"At 340",340),Stable(7,"Beyond 340",841),Stable(8,"Native prewarp",80),new StarSystemState(9,"Unstable",new(70,0),StarArchetype.Standard,false,false,false,false,StellarClass:StellarPrimaryClass.BlackHole)};
    cases.Add(Guarantee("guarantee-global-reassignment-and-fallback",guaranteeSystems,new[]{World(0,0,"A"),World(1,1,"B"),World(2,2,"Shared near"),World(3,3,"Shared far"),World(4,4,"Fallback A 1",gravity:6,radius:.1),World(5,5,"Fallback A 2",gravity:6,radius:.1),World(6,6,"At 340"),World(7,7,"Beyond 340"),World(8,8,"Native",pre:true),World(9,9,"Unstable")},new[]{Civ(0,0),Civ(1,1)},assertGlobal:true));
    cases.Add(Guarantee("guarantee-impossible-restores",guaranteeSystems.Take(3).ToArray(),new[]{World(0,0,"A"),World(1,1,"B"),World(2,2,"Shared")},new[]{Civ(0,0),Civ(1,1)}));
    var planner=new SpeciesHomeworldPlanner(); var constrainedSystems=maxSystems; var constrainedBodies=bodiesGenerator.Generate(8374837,constrainedSystems); var species=new[]{SpeciesCatalog.TerranBaselineId,SpeciesCatalog.PelagicHighPressureId};
    foreach(var (name,budget) in new[]{("constrained-success",100000),("constrained-budget",1)}) try { var homes=planner.PlanWithNearbyExpansionGuarantees(constrainedSystems,constrainedBodies,species,2,budget); cases.Add(new {Name=name,Kind="Constrained",Seed=8374837L,Systems=constrainedSystems.Select(Projection),Bodies=constrainedBodies.Select(CompactBody),SpeciesIds=species,MajorCount=2,SearchLimit=budget,ExpectedHomes=homes}); } catch(Exception x) { cases.Add(new {Name=name,Kind="Constrained",Seed=8374837L,Systems=constrainedSystems.Select(Projection),Bodies=constrainedBodies.Select(CompactBody),SpeciesIds=species,MajorCount=2,SearchLimit=budget,ExpectedError=x.Message}); }
    var impossibleSystems=Enumerable.Range(0,4).Select(i=>new StarSystemState(i,$"Sparse {i}",new(i*20,0),StarArchetype.Standard,false,false,false,false,StellarClass:StellarPrimaryClass.GYellowDwarf)).ToArray();
    var impossibleBodies=Enumerable.Range(0,4).Select(i=>World(100+i,i,$"Sparse world {i}")).ToArray();
    try { var homes=planner.PlanWithNearbyExpansionGuarantees(impossibleSystems,impossibleBodies,new[]{SpeciesCatalog.TerranBaselineId,SpeciesCatalog.TerranBaselineId},2); cases.Add(new {Name="constrained-impossible",Kind="Constrained",Seed=0L,Systems=impossibleSystems.Select(Projection),Bodies=impossibleBodies.Select(CompactBody),SpeciesIds=new[]{SpeciesCatalog.TerranBaselineId,SpeciesCatalog.TerranBaselineId},MajorCount=2,SearchLimit=100000,ExpectedHomes=homes}); } catch(Exception x) { cases.Add(new {Name="constrained-impossible",Kind="Constrained",Seed=0L,Systems=impossibleSystems.Select(Projection),Bodies=impossibleBodies.Select(CompactBody),SpeciesIds=new[]{SpeciesCatalog.TerranBaselineId,SpeciesCatalog.TerranBaselineId},MajorCount=2,SearchLimit=100000,ExpectedError=x.Message}); }
    try { var resolved=planner.ResolveWithinSystem(0,SpeciesCatalog.TerranBaselineId,0,constrainedBodies); cases.Add(new {Name="resolve-earth",Kind="Resolve",Seed=8374837L,Systems=Array.Empty<object>(),Bodies=constrainedBodies.Select(CompactBody),CivilizationId=0,SpeciesId=SpeciesCatalog.TerranBaselineId,SystemId=0,ExpectedHomes=new[]{resolved}}); } catch(Exception x) { throw new InvalidOperationException("Canonical resolve fixture failed",x); }
    var payload=new {Format="stellar-civilization-parity-v1",Cases=cases}; File.WriteAllText(args[0],JsonSerializer.Serialize(payload)+Environment.NewLine);
    Console.WriteLine($"Exported {cases.Count} civilization parity cases."); return 0;
} catch(Exception x) { Console.Error.WriteLine($"Civilization parity fixture failed: {x}"); return 1; }
