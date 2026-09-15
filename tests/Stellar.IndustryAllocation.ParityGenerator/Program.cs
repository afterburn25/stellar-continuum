using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Industry;
using Game.Simulation.Economy;
using Game.Simulation.Models;

if (args.Length != 1) throw new ArgumentException("Expected output fixture path");
var cases = new List<object>();
var json = new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
GalaxyState Galaxy(params CivilizationEconomyState[] economies) => new() {
    Seed=1, Systems=Array.Empty<StarSystemState>(), PlanetaryBodies=Array.Empty<PlanetaryBodyState>(),
    Civilizations=new List<CivilizationState>(), Fleets=new List<FleetState>(), Colonies=new List<ColonyState>(),
    Economies=economies, Technologies=new List<Game.Simulation.Research.TechnologyState>(),
    ConstructionStates=new List<Game.Simulation.Construction.ConstructionState>(), ShipyardStates=new List<Game.Simulation.Shipbuilding.ShipyardState>(),
    PlayerCivilizationId=1, Knowledge=new Game.Simulation.Knowledge.CivilizationKnowledgeState()
};
CivilizationEconomyState Economy(int id, IndustryPriority? priority = null, double funding = 1) => new() { CivilizationId=id, IndustryPriority=priority, LastBaseOperationsFundingFraction=funding };
void Allocation(string name, double available, double construction, double shipbuilding, double cw=1, double sw=1) {
    var context=new IndustryAllocationContext(7,available,construction,shipbuilding);
    try { var actual=new WeightedFairIndustryAllocationPolicy(new FixedIndustryPriorityProvider(cw,sw)).Allocate(context); cases.Add(new { Name=name, Kind="Allocation", Context=context, Weights=new IndustryPriorityWeights(cw,sw), Expected=actual }); }
    catch(Exception error) { cases.Add(new { Name=name, Kind="Allocation", Context=context, Weights=new IndustryPriorityWeights(cw,sw), ExpectedError=error.Message }); }
}
Allocation("balanced-scarcity",40,40,40);
Allocation("oversupply",100,20,30,3,1);
Allocation("construction-reflow",40,5,40,3,1);
Allocation("shipbuilding-reflow",40,40,5,1,3);
Allocation("construction-only",12,20,0,3,1);
Allocation("zero-consumers",12,0,0);
Allocation("zero-available",0,20,20);
Allocation("invalid-negative",-1,1,1);
Allocation("invalid-nan",double.NaN,1,1);
Allocation("invalid-construction-weight",1,1,1,0,1);
Allocation("invalid-shipbuilding-weight",1,1,1,1,double.PositiveInfinity);
void Weights(string name, CivilizationEconomyState[] economies, int civilization, IndustryPriorityWeights fallback) {
    var galaxy=Galaxy(economies); var provider=new CampaignIndustryPriorityProvider(new FixedIndustryPriorityProvider(fallback.ConstructionWeight,fallback.ShipbuildingWeight)); provider.Bind(galaxy);
    try { cases.Add(new { Name=name, Kind="Weights", Economies=economies, CivilizationId=civilization, Fallback=fallback, Expected=provider.GetWeights(civilization) }); }
    catch(Exception error) { cases.Add(new { Name=name, Kind="Weights", Economies=economies, CivilizationId=civilization, Fallback=fallback, ExpectedError=error.Message }); }
}
Weights("persisted-infrastructure",new[]{Economy(1,IndustryPriority.InfrastructureFirst)},1,new(2,1));
Weights("persisted-shipbuilding",new[]{Economy(1,IndustryPriority.ShipbuildingFirst)},1,new(2,1));
Weights("fallback-first-economy",new[]{Economy(1),Economy(1,IndustryPriority.ShipbuildingFirst)},1,new(2,1));
Weights("invalid-persisted",new[]{Economy(1,(IndustryPriority)99)},1,new(1,1));
void Command(string name, CivilizationEconomyState[] economies, int actor, int target, IndustryPriority priority) {
    var galaxy=Galaxy(economies);
    var before=JsonSerializer.Serialize(galaxy,json);
    var expected=IndustryPriorityCommands.Set(galaxy,actor,target,priority);
    var after=JsonSerializer.Serialize(galaxy.Economies,json);
    using var beforeDocument=JsonDocument.Parse(before);
    using var afterDocument=JsonDocument.Parse(after);
    cases.Add(new { Name=name, Kind="Command", Economies=beforeDocument.RootElement.GetProperty("Economies").Clone(), Actor=actor, Target=target, Priority=(int)priority, Before=beforeDocument.RootElement.Clone(), Expected=expected, After=afterDocument.RootElement.Clone() });
}
Command("command-accepted",new[]{Economy(1)},1,1,IndustryPriority.InfrastructureFirst);
Command("command-unknown-enum",new[]{Economy(1)},1,1,(IndustryPriority)99);
Command("command-foreign",new[]{Economy(1)},2,1,IndustryPriority.Balanced);
Command("command-missing",new[]{Economy(1)},2,2,IndustryPriority.Balanced);
Command("command-unknown-first",new[]{Economy(1)},2,1,(IndustryPriority)99);
void Funding(string name, CivilizationEconomyState[] economies, int id) => cases.Add(new { Name=name, Kind="Funding", Economies=economies, CivilizationId=id, Expected=CivilizationOperatingCapacity.GetFundingFraction(Galaxy(economies),id) });
Funding("funding-default",Array.Empty<CivilizationEconomyState>(),1); Funding("funding-clamp-low",new[]{Economy(1,funding:-.2)},1); Funding("funding-clamp-high",new[]{Economy(1,funding:2)},1); Funding("funding-nonfinite",new[]{Economy(1,funding:double.NaN)},1);
File.WriteAllText(args[0],JsonSerializer.Serialize(new { Format="stellar-industry-allocation-parity-v1", Cases=cases },json)+Environment.NewLine);
