using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
if(args.Length!=1)throw new ArgumentException("Expected output path.");
var json=new JsonSerializerOptions{IncludeFields=true,NumberHandling=JsonNumberHandling.AllowNamedFloatingPointLiterals};
JsonElement Freeze(object?o)=>JsonSerializer.SerializeToElement(o,json);
var cases=new List<object>();
KnownCivilization Known(int id=2,double trust=.1,double low=80,double high=120,double confidence=.8,long observed=100,bool border=true,double trade=.2,double exhaustion=.1,bool war=false,bool treaty=false,bool estimate=true)=>new(id,trust,low,high,confidence,observed,border,trade,exhaustion,war,treaty){HasMilitaryEstimate=estimate};
CivilizationTraits Traits(double aggression=.45,double territory=.35,double greed=.35,double science=.55,double risk=.45,double survival=1,bool honor=false)=>new(aggression,territory,greed,science,risk,survival,honor);
CivilizationOwnState Own(double military=100,double supply=1,double industry=200,double research=10,bool available=true,bool unexplored=true,bool colony=true,bool ships=true,bool shortfall=true)=>new(military,supply,industry,research,available,unexplored,colony,ships,shortfall);
KnowledgeSnapshot Knowledge(long observed=100,params (int Key,KnownCivilization Value)[] entries){var d=new Dictionary<int,KnownCivilization>();foreach(var e in entries)d.Add(e.Key,e.Value);return new(){ObservedAtTick=observed,Civilizations=d};}

void Fresh(string name,KnownCivilization known,long now,long stale){var before=Freeze(known);object?result=null;Exception?caught=null;try{result=new{Midpoint=known.EstimatedMilitaryMidpoint,Freshness=known.Freshness(now,stale)};}catch(Exception e){caught=e;}var output=result is null?(JsonElement?)null:Freeze(result);var error=caught is null?(JsonElement?)null:Freeze(new{Type=caught.GetType().Name,caught.Message});cases.Add(new{Name=name,Kind="Freshness",Arguments=Freeze(new{Known=known,NowTick=now,StaleAfterTicks=stale}),Result=output,Error=error,Before=before,After=Freeze(known)});}
var tickPairs=new[]{(100L,365L),(99,365),(465,365),(466,365),(long.MaxValue,long.MaxValue),(long.MinValue,long.MaxValue),(long.MaxValue,long.MinValue),(long.MinValue,-1L),(0,0L),(-10,365L),(long.MaxValue,-1L),(long.MinValue,1L)};
for(var i=0;i<tickPairs.Length;i++)Fresh($"fresh-{i}",Known(observed:i%3==0?long.MaxValue:100,low:i==10?double.NaN:80,high:i==11?double.PositiveInfinity:120),tickPairs[i].Item1,tickPairs[i].Item2);

void War(string name,CivilizationTraits traits,double own,KnownCivilization known,long now,bool compel=false){var before=Freeze(new{traits,own,known});WarAssessment?result=null;Exception?caught=null;try{result=new StrategicDecisionEvaluator().EvaluateWar(traits,own,known,now,compel);}catch(Exception e){caught=e;}var output=result is null?(JsonElement?)null:Freeze(result);var error=caught is null?(JsonElement?)null:Freeze(new{Type=caught.GetType().Name,caught.Message});cases.Add(new{Name=name,Kind="War",Arguments=Freeze(new{Traits=traits,OwnMilitary=own,Known=known,NowTick=now,HonorCompels=compel}),Result=output,Error=error,Before=before,After=Freeze(new{traits,own,known})});}
War("war-no-estimate",Traits(),100,Known(estimate:false),100);
War("war-no-estimate-nan",Traits(honor:true),double.NaN,Known(low:double.NaN,estimate:false),long.MaxValue,true);
var owns=new[]{0d,.5,50,70,100,200,1000,double.NaN,double.PositiveInfinity,double.NegativeInfinity};
for(var i=0;i<owns.Length;i++)War($"war-own-{i}",Traits(),owns[i],Known(),100);
var variants=new[]{Known(trust:-2),Known(trust:2),Known(confidence:0),Known(confidence:2),Known(observed:-1000),Known(border:false),Known(trade:-1),Known(trade:2),Known(exhaustion:-1),Known(exhaustion:2),Known(treaty:true),Known(low:double.NaN),Known(high:double.PositiveInfinity),Known(low:double.NegativeInfinity,high:double.NegativeInfinity)};
for(var i=0;i<variants.Length;i++)War($"war-variant-{i}",Traits(),100,variants[i],500);
War("war-honor-compelled",Traits(honor:true),1,Known(),100,true);War("war-honor-not-compelled",Traits(honor:true),1,Known(),100,false);War("war-extreme-traits",Traits(double.NaN,double.PositiveInfinity,-1,2,double.NegativeInfinity,double.NaN,true),100,Known(),100,true);

object Observe(Command c)=>new{c.Op,c.CivilizationId,c.NowTick,c.Force,c.Traits,c.Own,Knowledge=c.Knowledge is null?null:new{c.Knowledge.ObservedAtTick,Civilizations=c.Knowledge.Civilizations.Select(e=>new{Key=e.Key,Value=e.Value}).ToArray()}};
void Planner(string name,long interval,params Command[] commands){var planner=new CivilizationStrategicPlanner(reviewIntervalTicks:interval);var before=Freeze(commands.Select(Observe).ToArray());var results=new List<object>();var references=new List<object>();CivilizationStrategicPlan?last=null;for(var index=0;index<commands.Length;index++){var c=commands[index];if(c.Op is not ("Get" or "Invalidate" or "Remove" or "Clear"))throw new InvalidOperationException("generator command");CivilizationStrategicPlan?plan=null;Exception?caught=null;try{if(c.Op=="Get")plan=planner.GetPlan(c.CivilizationId,c.Traits!,c.Own!,c.Knowledge!,c.NowTick,c.Force);else if(c.Op=="Invalidate")planner.Invalidate(c.CivilizationId);else if(c.Op=="Remove")planner.RemoveCivilization(c.CivilizationId);else planner.Clear();}catch(Exception e){caught=e;}var same=plan is not null&&ReferenceEquals(plan,last);references.Add(new{CommandIndex=index,SameReferenceAsPrevious=same});if(plan is not null)last=plan;var output=plan is null?(JsonElement?)null:Freeze(plan);var error=caught is null?(JsonElement?)null:Freeze(new{Type=caught.GetType().Name,caught.Message});results.Add(new{Command=Freeze(Observe(c)),Result=output,Error=error});}cases.Add(new{Name=name,Kind="Planner",Arguments=Freeze(new{ReviewIntervalTicks=interval,Commands=commands.Select(Observe).ToArray()}),Result=Freeze(results),SourceReferenceObservations=Freeze(references),Error=(object?)null,Before=before,After=Freeze(commands.Select(Observe).ToArray())});}
Command Get(long tick=100,bool force=false,int civ=1,CivilizationTraits?traits=null,CivilizationOwnState?own=null,KnowledgeSnapshot?knowledge=null)=>new("Get",civ,tick,force,traits??Traits(),own??Own(),knowledge??Knowledge(100,(9,Known())));
var states=new[]{Own(supply:.5),Own(supply:.95),Own(supply:1),Own(industry:-1),Own(industry:double.NaN),Own(available:false),Own(research:0),Own(research:double.NaN),Own(unexplored:false),Own(colony:false),Own(ships:false),Own(shortfall:false),Own(military:double.NaN)};
for(var i=0;i<states.Length;i++)Planner($"planner-state-{i}",30,Get(own:states[i]));
Planner("planner-empty-knowledge",30,Get(knowledge:Knowledge()));
Planner("planner-key-record-mismatch",30,Get(knowledge:Knowledge(100,(99,Known(2)),(1,Known(7,low:60,high:70)))));
Planner("planner-threat-insertion-tie-a",30,Get(knowledge:Knowledge(100,(9,Known(8)),(3,Known(2)))));
Planner("planner-threat-insertion-tie-b",30,Get(knowledge:Knowledge(100,(3,Known(2)),(9,Known(8)))));
Planner("planner-relation-insertion-tie",30,Get(knowledge:Knowledge(100,(8,Known(8,trust:.2,trade:.3,estimate:false)),(2,Known(2,trust:.1,trade:.4,estimate:false)))));
Planner("planner-unknown-war-lowest-record-id",30,Get(knowledge:Knowledge(100,(1,Known(20,war:true,estimate:false)),(2,Known(3,war:true,estimate:false)))));
Planner("planner-known-war-multiplier",30,Get(knowledge:Knowledge(100,(1,Known(war:true)))));
Planner("planner-priority-nan-sort",30,Get(own:Own(industry:double.NaN,research:double.NaN)));
Planner("cache-before-due",30,Get(100),Get(129,own:Own(supply:.1)));
Planner("cache-at-due",30,Get(100),Get(130,own:Own(supply:.1)));
Planner("cache-force",30,Get(100),Get(101,true,own:Own(supply:.1)));
Planner("cache-force-same-tick-same-value",30,Get(100),Get(100,true));
Planner("cache-negative-interval-clamped",-9,Get(5),Get(5),Get(6));
Planner("cache-invalidate",30,Get(100),new("Invalidate",CivilizationId:1),Get(101,own:Own(supply:.1)));
Planner("cache-remove-other",30,Get(100),new("Remove",CivilizationId:2),Get(101,own:Own(supply:.1)));
Planner("cache-remove-own",30,Get(100),new("Remove",CivilizationId:1),Get(101,own:Own(supply:.1)));
Planner("cache-clear",30,Get(100),Get(100,civ:2),new("Clear"),Get(101,own:Own(supply:.1)));
Planner("cache-overflow-then-old-survives",30,Get(long.MaxValue-31),Get(long.MaxValue,force:true),Get(long.MaxValue-2));
Planner("cache-overflow-empty-retry",30,Get(long.MaxValue),Get(0));
Planner("planner-long-max-exact-ticks",30,Get(long.MaxValue-31));
Planner("cache-negative-ticks",30,Get(-100),Get(-71),Get(-70));

var nulls=new List<object>();foreach(var label in new[]{"war-traits","war-target","plan-traits","plan-own","plan-knowledge"}){Exception?caught=null;try{if(label=="war-traits")_=new StrategicDecisionEvaluator().EvaluateWar(null!,1,Known(),0);else if(label=="war-target")_=new StrategicDecisionEvaluator().EvaluateWar(Traits(),1,null!,0);else if(label=="plan-traits")_=new CivilizationStrategicPlanner().GetPlan(1,null!,Own(),Knowledge(),0);else if(label=="plan-own")_=new CivilizationStrategicPlanner().GetPlan(1,Traits(),null!,Knowledge(),0);else _=new CivilizationStrategicPlanner().GetPlan(1,Traits(),Own(),null!,0);}catch(Exception e){caught=e;}nulls.Add(new{Label=label,Error=caught is null?null:new{Type=caught.GetType().Name,caught.Message}});}
File.WriteAllText(args[0],JsonSerializer.Serialize(new{Format="stellar-strategic-planning-oracle-v1",Cases=cases,SourceOnlyNulls=nulls,CaseCount=cases.Count},json)+Environment.NewLine);
Console.WriteLine($"strategic planning oracle: {cases.Count} cases, {nulls.Count} source-only null observations");
sealed record Command(string Op,int CivilizationId=1,long NowTick=100,bool Force=false,CivilizationTraits? Traits=null,CivilizationOwnState? Own=null,KnowledgeSnapshot? Knowledge=null);
