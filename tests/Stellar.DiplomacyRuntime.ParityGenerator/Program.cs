using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

try
{
CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Usage: generator <fixture-path>");
var json = new JsonSerializerOptions { WriteIndented = true };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, json);
object Error(Exception e) => new { Type=e.GetType().Name, e.Message };
object State(DiplomacyState s) => s.Snapshot();
FirstContactOpportunity Contact(int observer,int target,long tick,string? id=null,ContactAwareness awareness=ContactAwareness.CommunicationAvailable) =>
  new(observer,id??$"contact-{observer}-{target}",target,tick,7,awareness,ContactCondition.Active,true,1);
ExplorationEvent Exploration(ExplorationEventType type,int observer,int? target=2) => new(type,observer,80,7,"explore",target);
CombatEvent Combat(CombatEventType type,int actor=1,int? target=2,int? system=7) => new(type,system,actor,10,target,20,0,0,0,"combat");
var rows=new List<object>();
object ArgumentsFor(string name)
{
  object payload = name switch
{
  "exploration-ignored" => new { Events=new[]{Exploration(ExplorationEventType.SystemDetected,1)}, Tick=4L },
  "exploration-first-contact" => new { Events=new[]{Exploration(ExplorationEventType.FirstContact,1)}, Tick=4L },
  "exploration-missing-target-after-success" => new { Events=new[]{Exploration(ExplorationEventType.FirstContact,1),Exploration(ExplorationEventType.FirstContact,3,null)}, Tick=4L },
  "exploration-negative-tick" => new { Events=Array.Empty<ExplorationEvent>(), Tick=-1L },
  "combat-hidden" or "combat-engagement" or "combat-any-identified-contact" => new { Events=new[]{Combat(CombatEventType.EngagementStarted)}, Tick=5L },
  "combat-self-and-transient" => new { Events=new[]{Combat(CombatEventType.EngagementStarted,1,1),Combat(CombatEventType.DamageApplied)}, Tick=5L },
  "combat-destruction-unknown-system" => new { Events=new[]{Combat(CombatEventType.FleetDestroyed,system:null)}, Tick=6L },
  "combat-negative-tick" => new { Events=Array.Empty<CombatEvent>(), Tick=-1L },
  "knowledge-invalid-observer-first" => new { Observer=-1, Tick=-1L },
  "knowledge-invalid-tick" => new { Observer=1, Tick=-1L },
  "knowledge-private-distinct-war" => new { Observer=1, Tick=9L },
  "runtime-success-equal-and-backward" => new { Policy=new{ReviewIntervalTicks=1000L,ContactStaleAfterTicks=2000L,ProposalLifetimeTicks=1000L}, ResetDays=0.0,ResetImmediate=false,Steps=new[]{new{Days=1.0,ExplorationEvents=new[]{Exploration(ExplorationEventType.FirstContact,1,2)},CombatEvents=Array.Empty<CombatEvent>()},new{Days=1.0,ExplorationEvents=Array.Empty<ExplorationEvent>(),CombatEvents=Array.Empty<CombatEvent>()},new{Days=.5,ExplorationEvents=Array.Empty<ExplorationEvent>(),CombatEvents=Array.Empty<CombatEvent>()}} },
  "runtime-partial-exploration" => new { Policy=new{ReviewIntervalTicks=1000L,ContactStaleAfterTicks=2000L,ProposalLifetimeTicks=1000L}, ResetDays=0.0,ResetImmediate=false,Steps=new[]{new{Days=1.0,ExplorationEvents=new[]{Exploration(ExplorationEventType.FirstContact,1,2),Exploration(ExplorationEventType.FirstContact,3,null)},CombatEvents=new[]{Combat(CombatEventType.EngagementStarted)}}} },
  "runtime-reset-immediate" => new { ResetDays=2.0, ResetImmediate=true },
  "command-preview-shared-hostility" => new { Systems=new[]{new{Id=0,Name="system"}}, Fleets=new[]{new{Id=10,CivilizationId=1,Name="f10",SystemId=0},new{Id=20,CivilizationId=2,Name="f20",SystemId=0}}, Order=new MilitaryOrder(MilitaryOrderType.Attack,20), Sequence=new[]{"PreviewPeace","SetHostile","PreviewHostile","IssueHostile"} },
  _ when name.StartsWith("hostility-",StringComparison.Ordinal) => new { First=1,Second=2,Self=1 },
  _ => throw new InvalidOperationException($"Missing retained arguments for {name}.")
  };
  return new { Case=name, Payload=payload };
}
void Add(string name,string operation,Action<DiplomacyState>? setup,Func<DiplomacyState,object?> invoke) {
  var state=new DiplomacyState(); setup?.Invoke(state); var before=Freeze(State(state)); var arguments=Freeze(ArgumentsFor(name)); var inputBefore=Freeze(arguments); object? result=null; Exception? caught=null;
  try { result=invoke(state); } catch(Exception e) { caught=e; }
  JsonElement? error = caught is null ? null : Freeze(Error(caught));
  rows.Add(new{Name=name,Operation=operation,Arguments=arguments,InputBefore=inputBefore,InputAfter=Freeze(arguments),Before=before,Result=Freeze(result),Error=error,After=Freeze(State(state))});
}
void AddState(string name,string operation,DiplomacyState state,Func<DiplomacyState,object?> invoke) {
  var before=Freeze(State(state)); var arguments=Freeze(ArgumentsFor(name)); var inputBefore=Freeze(arguments); object? result=null; Exception? caught=null;
  try { result=invoke(state); } catch(Exception e) { caught=e; }
  JsonElement? error=caught is null?null:Freeze(Error(caught));
  rows.Add(new{Name=name,Operation=operation,Arguments=arguments,InputBefore=inputBefore,InputAfter=Freeze(arguments),Before=before,Result=Freeze(result),Error=error,After=Freeze(State(state))});
}
void Identified(DiplomacyState s,int observer=2,int target=1){new DiplomacySimulation(s).ProcessContactOpportunity(Contact(observer,target,1));}

Add("exploration-ignored","Exploration",null,s=>new ExplorationDiplomacyBridge(new DiplomacySimulation(s)).Process(new[]{Exploration(ExplorationEventType.SystemDetected,1)},4));
Add("exploration-first-contact","Exploration",null,s=>new ExplorationDiplomacyBridge(new DiplomacySimulation(s)).Process(new[]{Exploration(ExplorationEventType.FirstContact,1)},4));
Add("exploration-missing-target-after-success","Exploration",null,s=>new ExplorationDiplomacyBridge(new DiplomacySimulation(s)).Process(new[]{Exploration(ExplorationEventType.FirstContact,1),Exploration(ExplorationEventType.FirstContact,3,null)},4));
Add("exploration-negative-tick","Exploration",null,s=>new ExplorationDiplomacyBridge(new DiplomacySimulation(s)).Process(Array.Empty<ExplorationEvent>(),-1));
Add("combat-hidden","Combat",null,s=>new CombatDiplomacyBridge(s).Process(new[]{Combat(CombatEventType.EngagementStarted)},5));
Add("combat-self-and-transient","Combat",s=>Identified(s),s=>new CombatDiplomacyBridge(s).Process(new[]{Combat(CombatEventType.EngagementStarted,1,1),Combat(CombatEventType.DamageApplied)},5));
Add("combat-engagement","Combat",s=>Identified(s),s=>new CombatDiplomacyBridge(s).Process(new[]{Combat(CombatEventType.EngagementStarted)},5));
var duplicateContactState=DiplomacyState.Restore(new(
  new[]{new DiplomaticContactSnapshot(2,"older-identified",1,1,1,7,ContactAwareness.Identified,ContactCondition.Active,false,1),new DiplomaticContactSnapshot(2,"newer-unidentified",1,2,2,7,ContactAwareness.DetectedUnidentified,ContactCondition.Active,false,.5)},
  Array.Empty<DiplomaticRelationshipSnapshot>(),Array.Empty<DiplomaticAccessSnapshot>(),Array.Empty<TerritorialClaimSnapshot>(),Array.Empty<TerritorialClaimResponseSnapshot>(),Array.Empty<DiplomaticAgreementSnapshot>(),Array.Empty<DiplomaticProposalSnapshot>(),Array.Empty<DiplomaticHistoryEventSnapshot>(),1,1,1,1));
AddState("combat-any-identified-contact","Combat",duplicateContactState,s=>new CombatDiplomacyBridge(s).Process(new[]{Combat(CombatEventType.EngagementStarted)},5));
Add("combat-destruction-unknown-system","Combat",s=>Identified(s),s=>new CombatDiplomacyBridge(s).Process(new[]{Combat(CombatEventType.FleetDestroyed,system:null)},6));
Add("combat-negative-tick","Combat",null,s=>new CombatDiplomacyBridge(s).Process(Array.Empty<CombatEvent>(),-1));
foreach(var political in new[]{DiplomaticPoliticalState.Unknown,DiplomaticPoliticalState.Peace,DiplomaticPoliticalState.Hostile,DiplomaticPoliticalState.AtWar,DiplomaticPoliticalState.Ceasefire})
 Add("hostility-"+political,"Hostility",s=>{Identified(s,1,2);Identified(s,2,1);var sim=new DiplomacySimulation(s);if(political==DiplomaticPoliticalState.Hostile)sim.SetHostile(1,2,3,"hostile");else if(political==DiplomaticPoliticalState.AtWar)sim.DeclareWar(1,2,3);},s=>new{Other=new DiplomacyCombatHostilityView(s).AreHostile(1,2),Self=new DiplomacyCombatHostilityView(s).AreHostile(1,1)});
Add("knowledge-private-distinct-war","Knowledge",s=>{var sim=new DiplomacySimulation(s);sim.ProcessContactOpportunity(Contact(1,2,1,"a"));sim.ProcessContactOpportunity(Contact(1,2,2,"b"));sim.ProcessContactOpportunity(Contact(2,1,1));sim.ProcessContactOpportunity(Contact(2,3,1));sim.ApplyRelationshipImpact(1,2,new(.8,.2,0,0,0,0,"sentiment"),3);sim.DeclareWar(2,1,4);},s=>new DiplomacyStrategicKnowledgeProvider(s).Build(1,9));
Add("knowledge-invalid-observer-first","Knowledge",null,s=>new DiplomacyStrategicKnowledgeProvider(s).Build(-1,-1));
Add("knowledge-invalid-tick","Knowledge",null,s=>new DiplomacyStrategicKnowledgeProvider(s).Build(1,-1));
Add("runtime-success-equal-and-backward","Runtime",null,s=>{var r=new DiplomacyCampaignRuntimeCoordinator(s,new(1000,2000,1000));r.Reset(0,false);var first=r.Process(new[]{Exploration(ExplorationEventType.FirstContact,1,2)},Array.Empty<CombatEvent>(),1);var equal=r.Process(Array.Empty<ExplorationEvent>(),Array.Empty<CombatEvent>(),1);Exception? error=null;try{r.Process(Array.Empty<ExplorationEvent>(),Array.Empty<CombatEvent>(),.5);}catch(Exception e){error=e;}return new{First=first,Equal=equal,Backward=error is null?null:Error(error),r.LastProcessedTick,r.NextMaintenanceReviewTick};});
Add("runtime-partial-exploration","Runtime",null,s=>{var r=new DiplomacyCampaignRuntimeCoordinator(s,new(1000,2000,1000));r.Reset(0,false);Exception? error=null;try{r.Process(new[]{Exploration(ExplorationEventType.FirstContact,1,2),Exploration(ExplorationEventType.FirstContact,3,null)},new[]{Combat(CombatEventType.EngagementStarted)},1);}catch(Exception e){error=e;}return new{Error=error is null?null:Error(error),r.LastProcessedTick,r.NextMaintenanceReviewTick};});
Add("runtime-reset-immediate","Runtime",null,s=>{var r=new DiplomacyCampaignRuntimeCoordinator(s);r.Reset(2,true);return new{r.LastProcessedTick,r.NextMaintenanceReviewTick};});
Add("command-preview-shared-hostility","Preview",s=>{Identified(s,1,2);Identified(s,2,1);},s=>{var galaxy=new GalaxyGenerator().Generate(17,new(){SystemCount=20,PreWarpCivilizationCount=2,AncientCivilizationCount=0,Radius=300});galaxy.Fleets.Clear();var sys=galaxy.Systems[0];FleetState F(int id,int civ)=>new(){Id=id,CivilizationId=civ,Name=$"f{id}",Role=FleetRole.Military,Position=sys.Position,CurrentSystemId=sys.Id,IsActive=true,Combat=CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1,FleetRole.Military)};galaxy.Fleets.Add(F(10,1));galaxy.Fleets.Add(F(20,2));var runtime=new DiplomacyCampaignRuntimeCoordinator(s);var commands=runtime.CreateCombatCommandRuntime();var order=new MilitaryOrder(MilitaryOrderType.Attack,20);var peace=commands.PreviewOrder(galaxy,1,10,order);new DiplomacySimulation(s).SetHostile(1,2,3,"hostile");var hostile=commands.PreviewOrder(galaxy,1,10,order);var issued=commands.IssueOrder(galaxy,1,10,order);return new{Peace=peace,Hostile=hostile,Issued=issued,FleetOrder=galaxy.Fleets[0].Combat?.Order,FleetTarget=galaxy.Fleets[0].Combat?.TargetFleetId};});

var sourceRoot=Path.GetFullPath("../stellar-engine-migration");
var sourcePaths=new[]{"src/Game/Simulation/Diplomacy/ExplorationDiplomacyBridge.cs","src/Game/Simulation/Diplomacy/CombatDiplomacyBridge.cs","src/Game/Simulation/Diplomacy/DiplomacyCampaignRuntimeCoordinator.cs","src/Game/Simulation/Combat/DiplomacyCombatHostilityView.cs","src/Game/Simulation/AI/StrategicKnowledgeProvider.cs"};
var sources=sourcePaths.Select(path=>new{Path=path,Sha256=Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot,path))))}).ToArray();
var output=new{Schema="stellar-diplomacy-runtime-oracle-v1",Culture="InvariantCulture",SourceFiles=sources,Rows=rows,RowCount=rows.Count};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0]))!);
File.WriteAllText(args[0],JsonSerializer.Serialize(output,json)+Environment.NewLine);
Console.WriteLine($"diplomacy runtime oracle: {rows.Count} rows");
}
catch (Exception exception)
{
    Console.Error.WriteLine(exception);
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"fixture={(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Environment.ExitCode = 1;
}
