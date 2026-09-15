using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Knowledge;

try {
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
    var json = new JsonSerializerOptions { WriteIndented = true, PreferredObjectCreationHandling = JsonObjectCreationHandling.Populate };
    T Clone<T>(T value) {
        var clone = JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(value, json), json)!;
        if (value is GalaxyState source && clone is GalaxyState destination) {
            for (var i = 0; i < source.ConstructionStates.Count; ++i) {
                foreach (var id in source.ConstructionStates[i].CompletedProjectIds) destination.ConstructionStates[i].CompletedProjectIds.Add(id);
                if (!destination.ConstructionStates[i].CompletedProjectIds.SetEquals(source.ConstructionStates[i].CompletedProjectIds)) throw new InvalidOperationException("Galaxy clone lost completed construction project IDs.");
            }
        }
        return clone;
    }
    CivilizationState Civ(int id, int home = 0, bool ancient = false) => new(id, "Civ " + id, home, CivilizationArchetype.Scientific, new(0,0,0,0,0,0,false), true, ancient ? CivilizationDevelopmentStage.AncientSpacefaring : CivilizationDevelopmentStage.WarpCapable, ancient);
    GalaxyState World(IEnumerable<ColonyState>? colonies = null, IEnumerable<string>? projects = null, double throughput = 1, bool ancient = false, bool twoCivs = false) {
        var civs = new List<CivilizationState>{ Civ(1, 0, ancient) }; if (twoCivs) civs.Add(Civ(2, 8));
        var construction = new ConstructionState { CivilizationId = 1 }; foreach (var id in projects ?? Array.Empty<string>()) construction.CompletedProjectIds.Add(id);
        return new GalaxyState { Seed=1, Systems=Array.Empty<StarSystemState>(), PlanetaryBodies=Array.Empty<PlanetaryBodyState>(), Civilizations=civs, Fleets=new List<FleetState>(), Colonies=(colonies ?? new[]{new ColonyState {Id=1,CivilizationId=1,SystemId=0,Name="Earth",PopulationMillions=1000,Infrastructure=1,Stability=1}}).ToList(), Economies=new[]{new CivilizationEconomyState{CivilizationId=1,LastIndustryPerSecond=throughput}, new CivilizationEconomyState{CivilizationId=2,LastIndustryPerSecond=throughput}}, Technologies=new List<TechnologyState>(), ConstructionStates=new List<ConstructionState>{construction, new(){CivilizationId=2}}, ShipyardStates=new List<ShipyardState>(), PlayerCivilizationId=1, Knowledge=new CivilizationKnowledgeState() };
    }
    var cases = new List<object>();
    CivilizationLogisticsSnapshot Add(string name, GalaxyState world) {
        var before=Clone(world); var view=new PrototypeEconomyLogisticsView(); var home=new PrototypeHomeSystemLogisticsNetworkView(view); var coverage=new PrototypeCivilizationLogisticsCoverageView(view,home);
        var expectedLogistics=view.GetSnapshot(world,1); var expectedHome=home.Build(world,1); var expectedCoverage=coverage.Build(world,1);
        cases.Add(new {Name=name, World=before, Arguments=new { CivilizationId=1 }, ExpectedLogistics=expectedLogistics, ExpectedHome=expectedHome, ExpectedCoverage=expectedCoverage, After=Clone(world)});
        return expectedLogistics;
    }
    void AddError(string name, GalaxyState world, int civilizationId) {
        var view = new PrototypeEconomyLogisticsView();
        Exception? actualError = null;
        try { view.GetSnapshot(world, civilizationId); new PrototypeHomeSystemLogisticsNetworkView(view).Build(world, civilizationId); new PrototypeCivilizationLogisticsCoverageView(view).Build(world, civilizationId); }
        catch (Exception error) { actualError = error; }
        if (actualError is null) throw new InvalidOperationException($"{name} did not fail.");
        cases.Add(new { Name=name, World=Clone(world), Arguments=new { CivilizationId=civilizationId }, ExpectedError=actualError.Message });
    }
    void AddCategoryError(string name, GalaxyState world, int civilizationId, string category) {
        var view = new PrototypeEconomyLogisticsView(); Exception? actualError = null;
        try { view.GetSnapshot(world, civilizationId); new PrototypeHomeSystemLogisticsNetworkView(view).Build(world, civilizationId); new PrototypeCivilizationLogisticsCoverageView(view).Build(world, civilizationId); }
        catch (Exception error) { actualError = error; }
        if (actualError is null) throw new InvalidOperationException($"{name} did not fail.");
        cases.Add(new { Name=name, World=Clone(world), Arguments=new { CivilizationId=civilizationId }, ExpectedErrorCategory=category });
    }
    Add("baseline-shortage", World());
    Add("thresholds-and-clamps", World(new[]{new ColonyState{Id=9,CivilizationId=1,SystemId=0,Name="Clamp",PopulationMillions=0,Infrastructure=99,Stability=-5},new ColonyState{Id=2,CivilizationId=1,SystemId=0,Name="Low",PopulationMillions=.001,Infrastructure=.1,Stability=.1}}, throughput:0));
    var boundarySnapshots = Add("coverage-boundaries", World(new[]{
        new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Below 70",PopulationMillions=1,Infrastructure=1.5,Stability=.70/(.72*1.5/(.95+.30/1.5))-.000001},
        new ColonyState{Id=2,CivilizationId=1,SystemId=0,Name="At 70",PopulationMillions=1,Infrastructure=1.5,Stability=(.70+1e-13)/(.72*1.5/(.95+.30/1.5))},
        new ColonyState{Id=3,CivilizationId=1,SystemId=0,Name="Below 95",PopulationMillions=1,Infrastructure=1.5,Stability=.95/(.72*1.5/(.95+.30/1.5))-.000001},
        new ColonyState{Id=4,CivilizationId=1,SystemId=0,Name="At 95",PopulationMillions=1,Infrastructure=1.5,Stability=(.95+1e-13)/(.72*1.5/(.95+.30/1.5))},
        new ColonyState{Id=5,CivilizationId=1,SystemId=0,Name="Above 95",PopulationMillions=1,Infrastructure=1.5,Stability=.95/(.72*1.5/(.95+.30/1.5))+.000001}}, throughput:0));
    {
        var snapshots = boundarySnapshots.Colonies;
        if (snapshots.Select(x => x.Condition).SequenceEqual(new[]{SupplyCondition.Critical, SupplyCondition.Strained, SupplyCondition.Strained, SupplyCondition.Healthy, SupplyCondition.Healthy}) == false
            || snapshots[1].CoverageRatio < .70 || snapshots[1].CoverageRatio - .70 > 1e-12 || snapshots[3].CoverageRatio < .95 || snapshots[3].CoverageRatio - .95 > 1e-12)
            throw new InvalidOperationException("Coverage boundary fixture did not activate exact threshold states.");
    }
    Add("projects-represented-hub-shipyard-resource", World(new[]{new ColonyState{Id=8,CivilizationId=1,SystemId=0,Name="Mars",PopulationMillions=100,Infrastructure=5,Stability=1.2},new ColonyState{Id=2,CivilizationId=1,SystemId=0,Name="Luna",PopulationMillions=10,Infrastructure=2,Stability=1},new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Earth",PopulationMillions=1000,Infrastructure=1,Stability=1}}, new[]{"orbital_launch_complex","orbital_shipyard","asteroid_resource_network","industrial_automation"}, 4));
    Add("fallback-home-links-sorted", World(new[]{new ColonyState{Id=5,CivilizationId=1,SystemId=0,Name="Later",PopulationMillions=10,Infrastructure=1,Stability=1},new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="First",PopulationMillions=1000,Infrastructure=5,Stability=1.2},new ColonyState{Id=3,CivilizationId=1,SystemId=0,Name="Middle",PopulationMillions=20,Infrastructure=.1,Stability=.1}}, throughput:2));
    Add("remote-systems-no-interstellar-corridors", World(new[]{new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Earth",PopulationMillions=1000,Infrastructure=2,Stability=1},new ColonyState{Id=17,CivilizationId=1,SystemId=4,Name="Remote B late",PopulationMillions=.001,Infrastructure=.2,Stability=.2},new ColonyState{Id=7,CivilizationId=1,SystemId=4,Name="Remote B early",PopulationMillions=5000,Infrastructure=5,Stability=1.2},new ColonyState{Id=3,CivilizationId=1,SystemId=2,Name="Remote A",PopulationMillions=50,Infrastructure=5,Stability=1.2},new ColonyState{Id=4,CivilizationId=2,SystemId=3,Name="Foreign",PopulationMillions=999,Infrastructure=5,Stability=1.2}}, throughput:3, twoCivs:true));
    Add("no-home-colonies-remote-only", World(new[]{new ColonyState{Id=11,CivilizationId=1,SystemId=7,Name="Remote Only",PopulationMillions=3,Infrastructure=.1,Stability=.1}}, throughput:.1));
    for (var mask = 0; mask < 16; ++mask) {
        var ids = new List<string>(); if ((mask & 1) != 0) ids.Add("orbital_launch_complex"); if ((mask & 2) != 0) ids.Add("orbital_shipyard"); if ((mask & 4) != 0) ids.Add("asteroid_resource_network"); if ((mask & 8) != 0) ids.Add("industrial_automation");
        Add("project-combination-" + mask, World(new[]{new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Project Home",PopulationMillions=1,Infrastructure=1,Stability=1}}, ids, 1));
    }
    Add("projects-without-home-colonies", World(new[]{new ColonyState{Id=1,CivilizationId=1,SystemId=9,Name="Remote",PopulationMillions=1,Infrastructure=1,Stability=1}}, new[]{"orbital_launch_complex","orbital_shipyard","asteroid_resource_network"}, 1));
    Add("ancient-projects", World(new[]{new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Ancient",PopulationMillions=12000,Infrastructure=3,Stability=1}}, ConstructionRegistry.All.Select(x=>x.Id), 12, true));
    var sol=new StarSystemState(0,"Sol",Vector2.Zero,StarArchetype.Standard,true,false,false,false,SolCatalogPreset.PresetId); var civs=new List<CivilizationState>{Civ(1)}; var seeder=new ColonySeeder(); var solWorld=World(seeder.Seed(civs,SolCatalogPreset.Create(sol)), new[]{"orbital_launch_complex"}, 5); Add("colony-seeder-earth-luna-mars",solWorld);
    var missingEconomy=World(); missingEconomy = new GalaxyState { Seed=missingEconomy.Seed,Systems=missingEconomy.Systems,PlanetaryBodies=missingEconomy.PlanetaryBodies,Civilizations=missingEconomy.Civilizations,Fleets=missingEconomy.Fleets,Colonies=missingEconomy.Colonies,Economies=Array.Empty<CivilizationEconomyState>(),Technologies=missingEconomy.Technologies,ConstructionStates=missingEconomy.ConstructionStates,ShipyardStates=missingEconomy.ShipyardStates,PlayerCivilizationId=1,Knowledge=missingEconomy.Knowledge}; AddError("missing-economy-state",missingEconomy,1);
    var missingConstruction=World(); missingConstruction = new GalaxyState { Seed=missingConstruction.Seed,Systems=missingConstruction.Systems,PlanetaryBodies=missingConstruction.PlanetaryBodies,Civilizations=missingConstruction.Civilizations,Fleets=missingConstruction.Fleets,Colonies=missingConstruction.Colonies,Economies=missingConstruction.Economies,Technologies=missingConstruction.Technologies,ConstructionStates=new List<ConstructionState>(),ShipyardStates=missingConstruction.ShipyardStates,PlayerCivilizationId=1,Knowledge=missingConstruction.Knowledge}; AddError("missing-construction-state",missingConstruction,1);
    AddError("missing-civilization",World(),2);
    AddCategoryError("duplicate-colony-id", World(new[]{new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="First",PopulationMillions=1,Infrastructure=1,Stability=1},new ColonyState{Id=1,CivilizationId=1,SystemId=0,Name="Second",PopulationMillions=2,Infrastructure=1,Stability=1}}, new[]{"orbital_launch_complex"}, 1), 1, "Duplicate colony logistics snapshot ID");
    File.WriteAllText(args[0],JsonSerializer.Serialize(new {Format="stellar-logistics-views-oracle-v1",Cases=cases},json)+Environment.NewLine);
    Console.WriteLine($"Exported {cases.Count} logistics view oracle cases.");
    return 0;
} catch(Exception exception) { Console.Error.WriteLine(exception); return 1; }
