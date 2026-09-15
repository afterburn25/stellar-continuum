using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    var json = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
    object[] Compact(PlanetaryBodyState b) => new object[] { b.Id,b.SystemId,b.ParentBodyId!,b.OrbitIndex,b.Name,(int)b.Kind,b.RadiusEarth,b.MassEarth,
        b.Environment.GravityG,b.Environment.TemperatureKelvin,b.Environment.PressureKPa,(int)b.Environment.Atmosphere,(int)b.Environment.AvailableSolvent,
        b.Environment.RadiationHazard,b.Environment.IsImmersedEnvironment,b.Environment.HasSolidSurface,b.LegacyColonizationCandidate,b.HasRareResource,
        b.HasAnomaly,b.HasPreWarpCivilization,b.OrbitalEccentricity,b.OrbitalInclinationDegrees };
    ColonyState Clone(ColonyState c) => new() { Id=c.Id,CivilizationId=c.CivilizationId,SystemId=c.SystemId,PlanetaryBodyId=c.PlanetaryBodyId,Name=c.Name,Kind=c.Kind,
        PopulationSpeciesId=c.PopulationSpeciesId,PopulationMillions=c.PopulationMillions,Infrastructure=c.Infrastructure,Stability=c.Stability,
        StoredFoodPopulationDaysMillions=c.StoredFoodPopulationDaysMillions,StoredWaterPopulationDaysMillions=c.StoredWaterPopulationDaysMillions,
        StoredExtractedMaterials=c.StoredExtractedMaterials,RemainingExtractableMaterials=c.RemainingExtractableMaterials,SurfaceHubLevel=c.SurfaceHubLevel,
        SurfaceHubUpgradeDaysRemaining=c.SurfaceHubUpgradeDaysRemaining,SurfaceBuildings=c.SurfaceBuildings.Select(x => new SurfaceBuildingState { Id=x.Id,TypeId=x.TypeId,X=x.X,Z=x.Z,RotationDegrees=x.RotationDegrees,IndustryProgress=x.IndustryProgress,IsComplete=x.IsComplete,IsEnabled=x.IsEnabled,PendingUpgradeTypeId=x.PendingUpgradeTypeId,UpgradeDaysRemaining=x.UpgradeDaysRemaining,OperatingPriority=x.OperatingPriority,Condition=x.Condition,StoredPowerDays=x.StoredPowerDays }).ToList() };
    GalaxyState Galaxy(IReadOnlyList<PlanetaryBodyState> bodies, ColonyState colony) => new() { Seed=0, Systems=Array.Empty<StarSystemState>(),PlanetaryBodies=bodies,
        Civilizations=new List<CivilizationState>(),Fleets=new List<FleetState>(),Colonies=new List<ColonyState> { colony },Economies=Array.Empty<CivilizationEconomyState>(),
        Technologies=new List<TechnologyState>(),ConstructionStates=new List<ConstructionState>(),ShipyardStates=new List<ShipyardState>(),PlayerCivilizationId=0,
        Knowledge=new Game.Simulation.Knowledge.CivilizationKnowledgeState() };
    ColonyState Colony(string species, double population=1000, int? body=null, int system=7, int id=31, int civilization=4) => new() { Id=id,CivilizationId=civilization,SystemId=system,PlanetaryBodyId=body,Name="Biology fixture",PopulationSpeciesId=species,PopulationMillions=population,Infrastructure=1,Stability=1 };
    PlanetaryAtmosphereRegime Atmosphere(AtmosphereClass a) => a switch { AtmosphereClass.OxygenNitrogen=>PlanetaryAtmosphereRegime.OxygenNitrogen,AtmosphereClass.OxygenRich=>PlanetaryAtmosphereRegime.OxygenRich,AtmosphereClass.Reducing=>PlanetaryAtmosphereRegime.Reducing,_=>PlanetaryAtmosphereRegime.Other };
    PlanetarySolventRegime Solvent(SolventClass s) => s switch { SolventClass.Water=>PlanetarySolventRegime.Water,SolventClass.Hydrocarbon=>PlanetarySolventRegime.Hydrocarbon, _=>PlanetarySolventRegime.Other };
    var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
    PlanetaryBodyState Ideal(int id, string species, bool legacy=false, bool preWarp=false, bool solid=true) { var s=SpeciesCatalog.Get(species); return new(id,7,null,0,"Ideal "+species,PlanetaryBodyKind.Planet,1,1,
        new(s.Environment.GravityG.Preferred,s.Environment.TemperatureKelvin.Preferred,s.Environment.PressureKPa.Preferred,Atmosphere(s.Environment.PreferredAtmosphere),Solvent(s.Environment.BiologicalSolvent),s.Physiology.RadiationTolerance,s.Environment.RequiresImmersion,solid),legacy,false,false,preWarp); }
    object ColonizationExpected(PlanetaryBodyState body, string species) { var a=habitability.Evaluate(body,species); var e=a.Environment; return new {a.PlanetaryBodyId,a.SpeciesId,
        Environment=new {e.NaturalHabitability,e.UnprotectedOperationalCapacity,e.GravitySuitability,e.TemperatureSuitability,e.PressureSuitability,e.AtmosphereSuitability,e.SolventSuitability,e.ImmersionSuitability,e.RadiationSuitability,e.LimitingFactor,e.RequiresGravityMitigation,e.RequiresThermalControl,e.RequiresPressureControl,e.RequiresSealedHabitat,e.RequiresArtificialBiosphere,e.RequiresRadiationShielding},
        a.Viability,a.HasSolidSurface,a.HasNativePreWarpCivilization,a.CanFoundCurrentColony}; }
    var profiles = SpeciesCatalog.All.Select(s => new { s.Id,s.Physiology.TypicalAdultMassKg,s.Physiology.BaselineLifespanYears,s.Physiology.BaselineMetabolicDemand,
        ReproductiveMaturityYears=s.LifeHistory.ReproductiveMaturityYears,s.LifeHistory.TypicalOffspringPerEvent,s.LifeHistory.MinimumInterEventYears,
        s.LifeHistory.DependentDevelopmentYears,s.LifeHistory.ReproductiveSpanYears,s.LifeHistory.BaselineGenerationYears,s.Metabolism.RestingMetabolicFraction,
        s.Metabolism.PeakActivityMetabolicMultiplier,s.Metabolism.TypicalRestFractionOfDay,DormancyMode=(int)s.Metabolism.Dormancy.Mode,
        DormancyMetabolicDemandFraction=s.Metabolism.Dormancy.MetabolicDemandFraction,MaximumDormancyDays=s.Metabolism.Dormancy.MaximumContinuousDays,
        TypicalDormancyRecoveryDays=s.Metabolism.Dormancy.TypicalRecoveryDays }).ToArray();
    var cases = new List<object>();
    foreach (var species in SpeciesCatalog.All.Select(x=>x.Id)) {
        cases.Add(new {Name="demographic-"+species,Kind="Demographic",SpeciesId=species,Expected=SpeciesDemographicPressureEvaluator.Evaluate(species)});
        foreach (var population in new[] { .000001d,1000d }) cases.Add(new {Name=$"metabolic-{species}-{population.ToString("R",CultureInfo.InvariantCulture)}",Kind="Metabolic",SpeciesId=species,PopulationMillions=population,Expected=SpeciesMetabolicEnvelopeEvaluator.Evaluate(SpeciesPopulationCohort.Founding(species,population))});
    }
    var solSystem = new StarSystemState(0,"Sol",System.Numerics.Vector2.Zero,StarArchetype.Standard,true,false,false,false,SolCatalogPreset.PresetId,StellarPrimaryClass.GYellowDwarf);
    var sol = SolCatalogPreset.Create(solSystem);
    foreach (var species in SpeciesCatalog.All.Select(x=>x.Id)) foreach (var body in sol.Where(x=>x.Id is 3 or 4 or 9)) cases.Add(new {Name=$"colonization-sol-{species}-{body.Name}",Kind="Colonization",SpeciesId=species,Body=Compact(body),Expected=ColonizationExpected(body,species)});
    foreach (var species in SpeciesCatalog.All.Select(x=>x.Id)) {
        var ideal=Ideal(700+cases.Count,species); cases.Add(new {Name="colonization-ideal-"+species,Kind="Colonization",SpeciesId=species,Body=Compact(ideal),Expected=ColonizationExpected(ideal,species)});
    }
    var terran=SpeciesCatalog.TerranBaselineId;
    var stressed=Ideal(800,terran) with { Environment=Ideal(800,terran).Environment with { GravityG=1.35 } };
    var fallback=stressed with { Id=801,LegacyColonizationCandidate=true,Environment=stressed.Environment with { GravityG=2.0 } };
    var unsuitable=fallback with { Id=802,LegacyColonizationCandidate=false };
    var prewarp=fallback with { Id=803,HasPreWarpCivilization=true };
    var nonsolid=fallback with { Id=804,Environment=fallback.Environment with { HasSolidSurface=false } };
    if (habitability.Evaluate(stressed,terran).Viability != SpeciesColonizationViability.NaturallyViable ||
        habitability.Evaluate(fallback,terran).Viability != SpeciesColonizationViability.HabitatSupportedFallback ||
        habitability.Evaluate(unsuitable,terran).Viability != SpeciesColonizationViability.Unsuitable ||
        habitability.Evaluate(prewarp,terran).CanFoundCurrentColony || habitability.Evaluate(nonsolid,terran).CanFoundCurrentColony)
        throw new InvalidOperationException("Colony biology policy fixture scenarios did not activate their intended policy branches.");
    foreach(var body in new[]{stressed,fallback,unsuitable,prewarp,nonsolid}) cases.Add(new {Name="colonization-policy-"+body.Id,Kind="Colonization",SpeciesId=terran,Body=Compact(body),Expected=ColonizationExpected(body,terran)});
    var burdenView=new CurrentColonyHabitatSupportBurdenView(); var turnoverView=new CurrentColonyPopulationTurnoverPressureView();
    void AddViews(string name, ColonyState colony, params PlanetaryBodyState[] bodies) {
        var burdenColony=Clone(colony); var burdenGalaxy=Galaxy(bodies,burdenColony); try { cases.Add(new {Name=name,Kind="Burden",Colony=Clone(colony),Bodies=bodies.Select(Compact),Expected=burdenView.Build(burdenGalaxy,burdenColony.Id)}); } catch(Exception x) { cases.Add(new {Name=name,Kind="Burden",Colony=Clone(colony),Bodies=bodies.Select(Compact),ExpectedError=x.Message}); }
        var turnoverColony=Clone(colony); var turnoverGalaxy=Galaxy(bodies,turnoverColony); try { cases.Add(new {Name=name,Kind="Turnover",Colony=Clone(colony),Bodies=bodies.Select(Compact),Expected=turnoverView.Build(turnoverGalaxy,turnoverColony)}); } catch(Exception x) { cases.Add(new {Name=name,Kind="Turnover",Colony=Clone(colony),Bodies=bodies.Select(Compact),ExpectedError=x.Message}); }
    }
    foreach(var body in new[]{stressed,fallback,unsuitable,prewarp,nonsolid}) AddViews("views-policy-"+body.Id,Colony(terran,1000,body.Id),body);
    foreach(var species in SpeciesCatalog.All.Select(x=>x.Id)) { var body=Ideal(900+cases.Count,species); AddViews("views-ideal-"+species,Colony(species,1000,body.Id),body); }
    var seededSolBodies = SolCatalogPreset.Create(solSystem);
    var seededTerrans = new ColonySeeder().Seed(new List<CivilizationState> {
        new(1,"Terrans",SolCatalogPreset.SystemId,CivilizationArchetype.Scientific,Game.Simulation.AI.CivilizationTraits.Balanced,true,CivilizationDevelopmentStage.WarpCapable,SpeciesId: terran)
    }, seededSolBodies);
    foreach (var seeded in seededTerrans.Where(x => x.PlanetaryBodyId is SolCatalogPreset.EarthBodyId or SolCatalogPreset.MoonBodyId or 4))
        AddViews("views-seeded-"+seeded.Name, seeded, seededSolBodies.ToArray());
    AddViews("views-legacy-neutral",Colony(terran,1000));
    AddViews("views-missing-body",Colony(terran,1000,999));
    var wrongSystem=Ideal(999,terran) with { SystemId=8 }; AddViews("views-wrong-system",Colony(terran,1000,999,7),wrongSystem);
    foreach (var (name,population) in new[] { ("negative",-1d),("zero",0d),("nan",double.NaN),("infinity",double.PositiveInfinity) })
        AddViews("views-invalid-population-"+name,Colony(terran,population));
    foreach (var species in new[] { "", "   ", "unknown" }) AddViews("views-invalid-species-"+(species.Length == 0 ? "empty" : string.IsNullOrWhiteSpace(species) ? "whitespace" : species),Colony(species,1000));
    AddViews("views-negative-id",Colony(terran,1000,null,7,-1));
    AddViews("views-negative-civilization",Colony(terran,1000,null,7,civilization:-1));
    AddViews("views-negative-system",Colony(terran,1000,null,-1));
    AddViews("views-overflow-population",Colony(terran,double.MaxValue));
    File.WriteAllText(args[0],JsonSerializer.Serialize(new {Format="stellar-colony-biology-parity-v1",Profiles=profiles,Cases=cases},json)+Environment.NewLine);
    Console.WriteLine($"Exported {profiles.Length} biology profiles and {cases.Count} biology parity cases.");
    return 0;
}
catch(Exception exception) { Console.Error.WriteLine($"Colony biology parity fixture failed: {exception}"); return 1; }
