using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Economy;

if (args.Length != 1) throw new ArgumentException("Expected one fixture output path.");
var json = new JsonSerializerOptions { WriteIndented = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
var nodes = new[] {
    new LogisticsNode(1,1,10,"Home",LogisticsNodeKind.Homeworld), new LogisticsNode(2,1,10,"Hub",LogisticsNodeKind.OrbitalHub),
    new LogisticsNode(3,1,10,"Luna",LogisticsNodeKind.LunarSettlement), new LogisticsNode(4,1,10,"Mars",LogisticsNodeKind.PlanetarySettlement),
    new LogisticsNode(5,1,10,"Mine",LogisticsNodeKind.ResourceSite), new LogisticsNode(6,1,10,"Depot",LogisticsNodeKind.Depot),
    new LogisticsNode(7,1,10,"Shipyard",LogisticsNodeKind.Shipyard), new LogisticsNode(8,2,20,"Foreign",LogisticsNodeKind.Homeworld),
    new LogisticsNode(9,1,10,"Disconnected",LogisticsNodeKind.Depot) };
var links = new[] {
    new LogisticsLink(10,1,1,2,8,1), new LogisticsLink(11,1,2,3,5,1), new LogisticsLink(12,1,2,4,3,1),
    new LogisticsLink(13,1,3,5,5,1), new LogisticsLink(14,1,4,5,3,1), new LogisticsLink(15,1,5,6,4,1),
    new LogisticsLink(16,1,6,7,4,1), new LogisticsLink(17,1,1,7,1,9), new LogisticsLink(18,1,3,4,2,0),
    new LogisticsLink(19,1,4,7,0,1), new LogisticsLink(20,1,7,6,2,1,false), new LogisticsLink(21,1,1,6,4,2,true,false) };

string Snap<T>(T value) => JsonSerializer.Serialize(value, json);
static object Error(Exception error) => new { ExpectedError = error.GetType().Name };
static object FlowExpected(LogisticsFlowPlan plan) => new {
    plan.Allocations,
    UnmetDemandPerDay = plan.UnmetDemandPerDay.Select(x => new { NodeId=x.Key, PerDay=x.Value }).ToArray(),
    UnusedSupplyPerDay = plan.UnusedSupplyPerDay.Select(x => new { NodeId=x.Key, PerDay=x.Value }).ToArray(),
    plan.TotalAllocatedPerDay, plan.TotalUnmetDemandPerDay };

object ConstructorCase(string name, LogisticsNode[] ns, LogisticsLink[] ls, int capacity=3) {
    var input=new {Nodes=ns,Links=ls}; var before=Snap(input); object outcome;
    try { var planner=new LogisticsRoutePlanner(ns,ls,capacity); outcome=new {CacheCapacity=planner.CacheCapacity,CacheCount=planner.CachedRouteCount}; }
    catch(Exception e) { outcome=Error(e); }
    if(before!=Snap(input)) throw new InvalidOperationException($"{name}: mutated inputs");
    return new {Name=name,Kind="Constructor",Input=input,Arguments=new {CacheCapacity=capacity},Outcome=outcome}; }

object RouteCase(string name, LogisticsNode[] ns, LogisticsLink[] ls, int source, int destination, int capacity=3) {
    var input=new {Nodes=ns,Links=ls}; var before=Snap(input); object outcome;
    try { var planner=new LogisticsRoutePlanner(ns,ls,capacity); var route=planner.FindRoute(source,destination); outcome=new {Expected=route,CacheCount=planner.CachedRouteCount,CacheCapacity=planner.CacheCapacity}; }
    catch(Exception e) { outcome=Error(e); }
    if(before!=Snap(input)) throw new InvalidOperationException($"{name}: mutated inputs");
    return new {Name=name,Kind="Route",Input=input,Arguments=new {Source=source,Destination=destination,CacheCapacity=capacity},Outcome=outcome}; }

object SequenceCase(string name, LogisticsNode[] ns, LogisticsLink[] ls, Query[] queries, int capacity) {
    var input=new {Nodes=ns,Links=ls}; var before=Snap(input); object outcome;
    try { var planner=new LogisticsRoutePlanner(ns,ls,capacity); var answers=new List<LogisticsRoutePlan?>(); var counts=new List<int>();
        foreach(var q in queries) { answers.Add(planner.FindRoute(q.Source,q.Destination)); counts.Add(planner.CachedRouteCount); }
        outcome=new {Expected=answers,CacheCounts=counts,CacheCount=planner.CachedRouteCount,CacheCapacity=planner.CacheCapacity}; }
    catch(Exception e) { outcome=Error(e); }
    if(before!=Snap(input)) throw new InvalidOperationException($"{name}: mutated inputs");
    return new {Name=name,Kind="Sequence",Input=input,Arguments=new {Queries=queries,CacheCapacity=capacity},Outcome=outcome}; }

object OwnedCopyCase() {
    var ns=new[] {new LogisticsNode(1,1,1,"A",LogisticsNodeKind.Homeworld),new LogisticsNode(2,1,1,"B",LogisticsNodeKind.Depot)};
    var ls=new[] {new LogisticsLink(1,1,1,2,7,2)}; var originalNodes=ns.ToArray(); var originalLinks=ls.ToArray();
    var planner=new LogisticsRoutePlanner(ns,ls,2); ns[0]=new LogisticsNode(99,9,9,"Changed",LogisticsNodeKind.Shipyard); ls[0]=new LogisticsLink(99,9,99,2,0,99,false,false);
    return new {Name="planner-owns-graph-copy",Kind="OwnedCopy",Input=new {Nodes=originalNodes,Links=originalLinks},Arguments=new {CacheCapacity=2},Outcome=new {Expected=planner.FindRoute(1,2),CacheCount=planner.CachedRouteCount,CacheCapacity=planner.CacheCapacity}}; }

object FlowCase(string name, LogisticsNode[] ns, LogisticsLink[] plannerLinks, LogisticsLink[] allocatorLinks, LogisticsSupplyOffer[] offers, LogisticsDemand[] demands) {
    var input=new {Nodes=ns,PlannerLinks=plannerLinks,AllocatorLinks=allocatorLinks,Offers=offers,Demands=demands}; var before=Snap(input); object outcome;
    try { var planner=new LogisticsRoutePlanner(ns,plannerLinks); var allocator=new LogisticsFlowAllocator(planner,allocatorLinks); var plan=allocator.AllocateDaily(offers,demands); outcome=new {Expected=FlowExpected(plan),CacheCount=planner.CachedRouteCount}; }
    catch(Exception e) { outcome=Error(e); }
    if(before!=Snap(input)) throw new InvalidOperationException($"{name}: mutated inputs");
    return new {Name=name,Kind="Flow",Input=input,Outcome=outcome}; }

var directedNodes=new[] {new LogisticsNode(1,1,1,"A",LogisticsNodeKind.Homeworld),new LogisticsNode(2,1,1,"B",LogisticsNodeKind.Depot)};
var directedLinks=new[] {new LogisticsLink(1,1,1,2,5,1,false)};
var tieNodes=Enumerable.Range(1,8).Select(id=>new LogisticsNode(id,1,1,$"N{id}",id==1?LogisticsNodeKind.Homeworld:LogisticsNodeKind.Depot)).ToArray();
var tieLinks=Enumerable.Range(2,6).Select(id=>new LogisticsLink(100+id,1,1,id,10,1,false))
    .Concat(Enumerable.Range(3,5).Select(id=>new LogisticsLink(200+id,1,id,8,10,1,false))).Reverse().ToArray();
var staleNodes=Enumerable.Range(1,4).Select(id=>new LogisticsNode(id,1,1,$"N{id}",LogisticsNodeKind.Depot)).ToArray();
var staleLinks=new[] {new LogisticsLink(1,1,1,2,10,10,false),new LogisticsLink(2,1,1,3,10,1,false),new LogisticsLink(3,1,3,2,10,1,false),new LogisticsLink(4,1,2,4,10,1,false),new LogisticsLink(5,1,3,1,10,0,false)};
var saturationNodes=Enumerable.Range(1,5).Select(id=>new LogisticsNode(id,1,1,$"N{id}",LogisticsNodeKind.Depot)).ToArray();
var saturationLinks=new[] {new LogisticsLink(1,1,1,2,5,1,false),new LogisticsLink(2,1,2,4,5,1,false),new LogisticsLink(3,1,2,5,5,1,false),new LogisticsLink(4,1,1,3,5,2,false),new LogisticsLink(5,1,3,5,5,2,false)};

var cases=new List<object> {
    ConstructorCase("constructor-valid",nodes,links), ConstructorCase("constructor-invalid-cache",nodes,links,0),
    ConstructorCase("constructor-duplicate-node",new[]{nodes[0],nodes[0]},Array.Empty<LogisticsLink>()),
    ConstructorCase("constructor-duplicate-link",directedNodes,new[]{directedLinks[0],directedLinks[0]}),
    ConstructorCase("constructor-missing-source",directedNodes,new[]{new LogisticsLink(1,1,99,2,1,1,false)}),
    ConstructorCase("constructor-missing-destination",directedNodes,new[]{new LogisticsLink(1,1,1,99,1,1,false)}),
    ConstructorCase("constructor-cross-owner",new[]{nodes[0],nodes[7]},new[]{new LogisticsLink(1,1,1,8,1,1)}),
    ConstructorCase("constructor-disabled-invalid-capacity",directedNodes,new[]{new LogisticsLink(1,1,1,2,double.NaN,1,true,false)}),
    ConstructorCase("constructor-disabled-invalid-transit",directedNodes,new[]{new LogisticsLink(1,1,1,2,1,double.NegativeInfinity,true,false)}),
    RouteCase("shortest-route",nodes,links,1,5), RouteCase("same-node",nodes,links,1,1), RouteCase("cross-owner",nodes,links,1,8),
    RouteCase("unknown-source",nodes,links,99,1), RouteCase("unknown-destination",nodes,links,1,99),
    RouteCase("directed-forward",directedNodes,directedLinks,1,2), RouteCase("directed-reverse",directedNodes,directedLinks,2,1),
    RouteCase("equal-routes-quaternary-heap",tieNodes,tieLinks,1,8), RouteCase("stale-queue-entry-and-cycle",staleNodes,staleLinks,1,4), OwnedCopyCase(),
    SequenceCase("lru-null-hit-and-eviction",nodes,links,new[]{new Query(1,9),new Query(1,5),new Query(1,9),new Query(2,7),new Query(1,5)},2),
    SequenceCase("same-node-and-cross-owner-not-cached",nodes,links,new[]{new Query(1,1),new Query(1,8),new Query(1,9),new Query(1,8)},2),
    FlowCase("priority-shared-capacity",nodes,links,links,new[]{new LogisticsSupplyOffer(1,7),new LogisticsSupplyOffer(3,8)},new[]{new LogisticsDemand(7,7,10),new LogisticsDemand(5,8,1)}),
    FlowCase("multiple-suppliers-equal-time-order",nodes,links,links,new[]{new LogisticsSupplyOffer(5,2),new LogisticsSupplyOffer(1,5),new LogisticsSupplyOffer(3,3)},new[]{new LogisticsDemand(4,8)}),
    FlowCase("duplicates-self-and-zero-overwrite-order",nodes,links,links,new[]{new LogisticsSupplyOffer(1,2),new LogisticsSupplyOffer(1,3),new LogisticsSupplyOffer(5,10),new LogisticsSupplyOffer(9,0)},new[]{new LogisticsDemand(7,2,3),new LogisticsDemand(5,4,2),new LogisticsDemand(7,6,1),new LogisticsDemand(5,0,0)}),
    FlowCase("disconnected-and-zero",nodes,links,links,new[]{new LogisticsSupplyOffer(1,4)},new[]{new LogisticsDemand(9,2),new LogisticsDemand(7,0)}),
    FlowCase("saturated-shortest-does-not-reroute",saturationNodes,saturationLinks,saturationLinks,new[]{new LogisticsSupplyOffer(1,10)},new[]{new LogisticsDemand(4,5,10),new LogisticsDemand(5,5)}),
    FlowCase("invalid-supply-negative",nodes,links,links,new[]{new LogisticsSupplyOffer(1,-1)},new[]{new LogisticsDemand(5,1)}),
    FlowCase("invalid-supply-nan",nodes,links,links,new[]{new LogisticsSupplyOffer(1,double.NaN)},new[]{new LogisticsDemand(5,1)}),
    FlowCase("invalid-supply-positive-infinity",nodes,links,links,new[]{new LogisticsSupplyOffer(1,double.PositiveInfinity)},new[]{new LogisticsDemand(5,1)}),
    FlowCase("invalid-demand-negative",nodes,links,links,new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(5,-1)}),
    FlowCase("invalid-demand-nan",nodes,links,links,new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(5,double.NaN)}),
    FlowCase("invalid-demand-negative-infinity",nodes,links,links,new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(5,double.NegativeInfinity)}),
    FlowCase("unknown-supply-node",nodes,links,links,new[]{new LogisticsSupplyOffer(99,1)},new[]{new LogisticsDemand(5,1)}),
    FlowCase("unknown-demand-node",nodes,links,links,new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(99,1)}),
    FlowCase("allocator-duplicate-link",directedNodes,directedLinks,new[]{directedLinks[0],directedLinks[0]},new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(2,1)}),
    FlowCase("allocator-link-mismatch",directedNodes,directedLinks,Array.Empty<LogisticsLink>(),new[]{new LogisticsSupplyOffer(1,1)},new[]{new LogisticsDemand(2,1)}) };

File.WriteAllText(args[0],JsonSerializer.Serialize(new {Format="stellar-logistics-routing-oracle-v2",Cases=cases},json));
record Query(int Source,int Destination);
