using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Text.Json;
using Game.Simulation.Generation;
using Game.Simulation.Models;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    List<StarArchetype> BuildDefaultQuotaDeck(int count, Random random)
    {
        var method=typeof(GalaxyGenerator).GetMethod("BuildQuotaDeck",BindingFlags.NonPublic|BindingFlags.Static)
            ?? throw new InvalidOperationException("GalaxyGenerator.BuildQuotaDeck was not found.");
        var settings=GalaxyGenerationMetadata.FullGalaxy500("fixture",0,systemCount:count).ToSettings();
        return ((List<StarArchetype>)method.Invoke(null,new object[]{settings,random})!).ToList();
    }
    StarArchetype AlignCompact(StellarPrimaryClass stellarClass, StarArchetype archetype) => stellarClass switch
    {
        StellarPrimaryClass.BlackHole => StarArchetype.BlackHole,
        StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar => StarArchetype.NeutronPulsar,
        _ when archetype is StarArchetype.BlackHole or StarArchetype.NeutronPulsar => StarArchetype.Standard,
        _ => archetype,
    };
    object Projection(StarSystemState system) => new {
        system.Id, system.Name, X=system.Position.X, Y=system.Position.Y,
        Depth=system.GalacticDepthLightYears, Primary=system.StellarClass,
        Secondary=system.SecondaryStellarClass, Tertiary=system.TertiaryStellarClass,
        system.CatalogPresetId, system.StellarCatalogId, system.Archetype, system.HasHabitableWorld,
        system.HasAnomaly,system.HasRareResource,system.HasPreWarpCivilization,
    };
    var measured=FullGalaxyStellarPopulation.MeasuredStars;
    var cases=new List<object>();
    foreach(var (seed,count) in new (long,int)[] { (8374837,250),(8374837,500),(8374837,1000),(8374837,2500),(0,250),(-1,500),(long.MinValue,250),(long.MaxValue,250) })
    {
        // Actual preserved public stages; the pre-civilization projection is explicit.
        var core=GalacticCoreMetadata.CreateFullGalaxy(count);
        var classes=FullGalaxyStellarPopulation.BuildStellarClasses(seed,count);
        var generated=FullGalaxyStellarPopulation.BuildGeneratedPositions(seed,count,core);
        var names=ProceduralSystemNamer.Generate(seed,count);
        // Invoke the authoritative private helper. The same Random instance then continues
        // through the exact short-circuit flag draws in GalaxyGenerator.Generate.
        var traitRandom=new Random(unchecked((int)(seed^(seed>>32))));
        var archetypes=BuildDefaultQuotaDeck(count,traitRandom);
        var standardIndex=archetypes.IndexOf(StarArchetype.Standard);
        (archetypes[0],archetypes[standardIndex])=(archetypes[standardIndex],archetypes[0]);
        var systems=new List<StarSystemState>();
        for(int i=0;i<count;i++)
        {
            var archetype=AlignCompact(classes[i],archetypes[i]);
            var habitable=archetype==StarArchetype.HabitableRich||traitRandom.NextDouble()<.16;
            var anomaly=archetype==StarArchetype.AncientRuin||archetype==StarArchetype.Legendary||traitRandom.NextDouble()<.20;
            var rare=archetype==StarArchetype.ResourceRich||traitRandom.NextDouble()<.12;
            var preWarp=habitable&&traitRandom.NextDouble()<.04;
            var system=new StarSystemState(i,names[i],i<96?Vector2.Zero:generated[i-96],archetype,habitable,anomaly,rare,preWarp,StellarClass:classes[i]);
            if(i<96) system=NearbyStarCatalog.Apply(system,measured[i]);
            systems.Add(system);
        }
        systems[0]=new StarSystemState(SolCatalogPreset.SystemId,"Sol",Vector2.Zero,StarArchetype.Standard,true,false,false,false,
            SolCatalogPreset.PresetId,StellarPrimaryClass.GYellowDwarf);
        systems[0]=NearbyStarCatalog.Apply(systems[0],measured[0]);
        StellarCompanionGenerator.Apply(seed,systems);
        if(seed==8374837&&(count==250||count==500))
        {
            // Civilization seeding can rename non-catalog stars. Traits are physical facts,
            // so compare them independently of those expected post-civilization names.
            var generatedGalaxy=new GalaxyGenerator().Generate(seed,
                GalaxyGenerationMetadata.FullGalaxy500("fixture",seed,systemCount:count).ToSettings());
            var expectedTraits=systems.Select(system=>new {system.Id,system.Archetype,system.HasHabitableWorld,
                system.HasAnomaly,system.HasRareResource,system.HasPreWarpCivilization});
            var actualTraits=generatedGalaxy.Systems.Select(system=>new {system.Id,system.Archetype,system.HasHabitableWorld,
                system.HasAnomaly,system.HasRareResource,system.HasPreWarpCivilization});
            if(!expectedTraits.SequenceEqual(actualTraits))
                throw new InvalidOperationException($"Full-galaxy trait projection diverged from GalaxyGenerator.Generate for {count} systems.");
        }
        cases.Add(new { Seed=seed, Count=count, Radius=FullGalaxyStellarPopulation.RadiusFor(count),
            Core=new { core.X,core.Y,core.ExclusionRadius },
            Targets=Enumerable.Range(0,12).Select(i=>FullGalaxyStellarPopulation.TargetStellarClassCounts(count)[(StellarPrimaryClass)i]),
            Names=names, Systems=systems.Select(Projection).ToArray() });
    }
    var randomCases=new List<object>();
    foreach(var seed in new[]{0,1,-1,8374837,int.MinValue,int.MaxValue})
    {
        var random=new Random(seed); var values=new List<object>();
        for(int i=0;i<128;i++) values.Add(new { Whole=random.Next(),Bounded=random.Next(i%17),Unit=random.NextDouble() });
        randomCases.Add(new { Seed=seed,Values=values });
    }
    var spectra=new[]{""," ","G2V","K0IV","G8III","B2Ia","A0V","DA2","M3","L5","T8","Y1","?","sdM4","G2IIV","m4"," O9V "};
    var solSystem=new StarSystemState(0,"Sol",Vector2.Zero,StarArchetype.Standard,true,false,false,false,SolCatalogPreset.PresetId,StellarPrimaryClass.GYellowDwarf);
    var sol=SolCatalogPreset.Create(solSystem);
    object[] CompactBody(PlanetaryBodyState b) => new object[] {b.Id,b.SystemId,b.ParentBodyId!,b.OrbitIndex,b.Name,(int)b.Kind,
        b.RadiusEarth,b.MassEarth,b.Environment.GravityG,b.Environment.TemperatureKelvin,b.Environment.PressureKPa,
        (int)b.Environment.Atmosphere,(int)b.Environment.AvailableSolvent,b.Environment.RadiationHazard,
        b.Environment.IsImmersedEnvironment,b.Environment.HasSolidSurface,b.LegacyColonizationCandidate,
        b.HasRareResource,b.HasAnomaly,b.HasPreWarpCivilization,b.OrbitalEccentricity,b.OrbitalInclinationDegrees};
    var planetCases=new List<object>();
    foreach(var (seed,count,modern) in new (long,int,bool)[]{(8374837,100,true),(8374837,250,true),(8374837,500,true),
        (8374837,1000,true),(8374837,2500,true),(long.MinValue,40,false),(long.MaxValue,100,true),(0,100,true),(-1,100,true)})
    {
        var inputs=Enumerable.Range(0,count).Select(i=> i==0 ? solSystem : new StarSystemState(i,$"Reference-{i}",Vector2.Zero,
            (StarArchetype)(i%10),i%7==0,i%11==0,i%5==0,i%21==0,
            StellarClass:modern?(StellarPrimaryClass)(i%12):null)).ToArray();
        // Exercise stable ordering: sparse legacy input arrives in reverse order.
        if(!modern) Array.Reverse(inputs);
        var bodies=new PlanetaryBodyGenerator().Generate(seed,inputs);
        planetCases.Add(new{Seed=seed,Systems=inputs.Select(Projection).ToArray(),Bodies=bodies.Select(CompactBody).ToArray()});
    }
    var payload=new { Format="stellar-galaxy-parity-v1", Source="C# reference helpers at integration 97091aee; no rewritten random/math oracle", Cases=cases, Random=randomCases,
        Spectra=spectra.Select(s=>new { Input=s,Class=NearbyStarCatalog.Classify(s) }),
        Catalog=NearbyStarCatalog.Stars.Select((star,i)=>Projection(NearbyStarCatalog.Apply(new StarSystemState(i,"",Vector2.Zero,StarArchetype.Standard,false,false,false,false),star))),
        Sol=sol, UpgradedSol=SolCatalogPreset.UpgradeSavedCatalog(sol.Where(body=>body.Id!=SolCatalogPreset.PlutoBodyId).ToArray(),new[]{solSystem}),
        PlanetCases=planetCases, PlanetBodyFields="id,systemId,parentBodyId,orbitIndex,name,kind,radiusEarth,massEarth,gravityG,temperatureKelvin,pressureKPa,atmosphere,solvent,radiationHazard,immersed,solid,legacyCandidate,rare,anomaly,preWarp,eccentricity,inclination" };
    // Compact JSON avoids checking in tens of thousands of formatting-only lines.
    File.WriteAllText(args[0],JsonSerializer.Serialize(payload)+Environment.NewLine);
    Console.WriteLine($"Exported {cases.Count} physical-star cases, {planetCases.Count} planetary cases, {randomCases.Count*128} RNG triplets, 500 catalog records and Sol reference data.");
    return 0;
}
catch(Exception exception)
{
    Console.Error.WriteLine($"Galaxy parity fixture failed: {exception}\nWorking directory: {Environment.CurrentDirectory}");
    return 1;
}
