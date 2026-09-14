using System.Diagnostics;
using System.Text.Json;
using Game.Presentation.PlanetIdentity;
using Game.Presentation.Spatial;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

namespace Game.Quality.Validation;

internal static class PlanetIdentityChecks
{
    private static void Require(bool condition,string message){if(!condition)throw new InvalidOperationException(message);}
    [Game.Validation.RegressionCheck]
    private static void EveryClassHasPhysicalEvidenceAndFiveDistinctVariants()
    {
        var ids=new HashSet<string>();
        foreach(var c in Enum.GetValues<PlanetClass>()){
            var body=PlanetIdentityExamples.Body(c);
            Require(PlanetClassifier.Classify(body).Class==c,"Physical example does not classify as "+c);
            var family=PlanetVisualCatalog.Get(c);
            Require(family.Variants.Count==5,"Missing variant coverage: "+c);
            foreach(var v in family.Variants)Require(ids.Add(v.Id),"Duplicate variant "+v.Id);
            var p=PlanetPresentationResolver.Resolve(body,PlanetIdentityExamples.System(),987)!;
            Require(p.CanShowSolidSurface==body.Environment.HasSolidSurface,"Surface gate changed");
            Require(p.HasAtmosphere==(body.Environment.Atmosphere!=PlanetaryAtmosphereRegime.Vacuum&&body.Environment.PressureKPa>=.1),"Atmosphere mismatch");
            Require(p.Development==0&&!p.Modifiers.HasFlag(PlanetVisualModifier.Developed),"Uninhabited cities");
        }
        Require(ids.Count==100,"Manifest must have 100 variants");
        Console.WriteLine("PASS: all 20 physical classes and 100 unique variants");
    }
    [Game.Validation.RegressionCheck]
    private static void PrecedenceAndSolRemainPhysical()
    {
        var hot=PlanetIdentityExamples.Body(PlanetClass.AirlessRocky,1,0) with {Name="Mercury"};
        Require(PlanetClassifier.Classify(hot,true).Class==PlanetClass.AirlessRocky,"Mercury incorrectly volcanic/hot generic");
        var venus=PlanetIdentityExamples.Body(PlanetClass.Greenhouse,2,0) with {Name="Venus"};
        Require(PlanetClassifier.Classify(venus,true).Class==PlanetClass.Greenhouse,"Venus incorrectly generic toxic");
        var system=PlanetIdentityExamples.System(0) with {CatalogPresetId="sol-v1"};
        Require(PlanetPresentationResolver.Resolve(hot,system,4)!.CanonicalKey=="mercury","Mercury art changed");
        Require(PlanetPresentationResolver.Resolve(venus,system,4)!.CanonicalKey=="venus","Venus art changed");
        var impostor=hot with {Id=555};
        Require(PlanetPresentationResolver.Resolve(impostor,system,4)!.CanonicalKey is null,"Canonical lookup spoofed by name");
        var water=PlanetIdentityExamples.Body(PlanetClass.Ocean);
        Require(PlanetClassifier.Classify(water with {Environment=water.Environment with {PressureKPa=249.999}}).Class==PlanetClass.Ocean,"Ocean threshold below");
        Require(PlanetClassifier.Classify(water with {Environment=water.Environment with {PressureKPa=250,GravityG=2}}).Class==PlanetClass.HighPressureOcean,"Ocean precedence over gravity");
        Require(PlanetClassifier.Classify(hot with {Environment=hot.Environment with {TemperatureKelvin=1300}}).Class==PlanetClass.Volcanic,"Thermal precedence");
        var low=hot with {Environment=hot.Environment with {Atmosphere=PlanetaryAtmosphereRegime.Other,PressureKPa=.0999}};
        Require(!PlanetPresentationResolver.Resolve(low,system,4)!.HasAtmosphere,"Near vacuum acquired atmosphere");
        Console.WriteLine("PASS: classification boundaries, Mercury/Venus and canonical identity");
    }
    [Game.Validation.RegressionCheck]
    private static void ReloadAndTerraformingInvalidateOnlyDerivedIdentity()
    {
        var body=PlanetIdentityExamples.Body(PlanetClass.Terran);var system=PlanetIdentityExamples.System();
        var json=JsonSerializer.Serialize(body);
        var first=PlanetPresentationResolver.Resolve(body,system,long.MaxValue)!;
        var reload=PlanetPresentationResolver.Resolve(JsonSerializer.Deserialize<PlanetaryBodyState>(json)!,system,long.MaxValue)!;
        Require(first==reload,"Identity changed on body save/load round trip");
        var altered=body with {Environment=body.Environment with {TemperatureKelvin=120,PressureKPa=3}};
        var after=PlanetPresentationResolver.Resolve(altered,system,long.MaxValue)!;
        Require(after.Identity!=first.Identity&&after.Classification.Class==PlanetClass.Ice,"Terraforming did not update identity");
        Require(JsonSerializer.Serialize(body)==json,"Presentation mutated physical facts");
        var inhabited=PlanetPresentationResolver.WithDevelopment(first,8000);
        Require(inhabited.Identity==first.Identity&&inhabited.Development>0,"Population changed morphology identity");
        Require(PlanetPresentationResolver.WithDevelopment(inhabited,0).Development==0,"Abandoned city lights persisted");
        Require(first.Identity!=PlanetPresentationResolver.Resolve(body,system,123)!.Identity,"Campaign seed ignored");
        Console.WriteLine("PASS: save/load, physical changes and population gates");
    }
    [Game.Validation.RegressionCheck]
    private static void ObserverFilteringDoesNotExposeTrueIdentity()
    {
        Require(PlanetPresentationResolver.Resolve(null!,null!,0,fullySurveyed:false) is null,"Hidden branch touched facts");
        var hidden=new PlanetaryBodyExplorationView(100,null,2,"Unconfirmed",PlanetaryBodyKind.Planet,1,
            null,null,null,null,null,null,null,null,null,null,null,null,null,null,null);
        var system=new KnownSystemExplorationView(42,"Detected system",SystemSurveyLevel.PartiallySurveyed,.4,
            null,null,null,null,null,null,null,new[]{hidden});
        var partial=new SystemSpatialProjection().Build(system,123);
        Require(partial.Bodies.Single().Presentation is null,"Partial survey leaked class");
        Require(partial.Bodies.Single().AvailableSolvent is null&&partial.Bodies.Single().RadiationHazard is null,"Partial survey leaked environment");
        Require(partial.Sky!.Primary.Class is null&&partial.Sky.Secondary is null,"Hidden stellar facts leaked");
        Console.WriteLine("PASS: observer-filtered identity and sky");
    }
    [Game.Validation.RegressionCheck]
    private static void LargeGalaxyIdentityHasNoTextureAllocationOrSmallSeedCycle()
    {
        var timer=Stopwatch.StartNew();var identities=new HashSet<ulong>();var seeds=new HashSet<float>();var variants=new HashSet<string>();
        var before=GC.GetTotalAllocatedBytes(true);
        for(var i=0;i<2500;i++){
            var system=PlanetIdentityExamples.System(i);
            for(var j=0;j<8;j++){
                var body=PlanetIdentityExamples.Body((PlanetClass)((i*8+j)%20),i*8+j,i);
                var p=PlanetPresentationResolver.Resolve(body,system,234991)!;
                Require(identities.Add(p.Identity),"Repeated identity across 20,000 bodies");seeds.Add(p.ShaderSeed);variants.Add(p.Variant.Id);
            }
        }
        Require(seeds.Count>19000,"Small procedural seed cycle");
        Require(variants.Count==100,"Large catalog lost variants");
        Require(GC.GetTotalAllocatedBytes(false)-before<100_000_000,"Identity allocation unexpectedly high");
        var sparse=SystemSkyResolver.Resolve(1,8,StellarPrimaryClass.HotBlueStar,radialFraction:.95);
        var dense=SystemSkyResolver.Resolve(1,8,StellarPrimaryClass.HotBlueStar,radialFraction:.1);
        Require(sparse.StarDensity<dense.StarDensity&&dense.StarDensity<=1600,"Sky population not bounded");
        Require(SystemSkyResolver.Light(StellarPrimaryClass.MRedDwarf).Color!=sparse.Primary.Color,"Stellar lighting lacks diversity");
        Console.WriteLine($"PASS: 2,500 systems / 20,000 bodies / {seeds.Count} distinct shader seeds / {timer.ElapsedMilliseconds} ms");
    }
}
