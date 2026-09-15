using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions { WriteIndented = false, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);

object Snapshot(AdaptiveResearchCivilizationState state) => new {
    state.CivilizationId, state.Revision, state.MaterializedViewRevision,
    state.DirectedProgramStageId, state.TotalEffectiveResearchLabs,
    state.AssignedEffectiveLabs, state.FreeEffectiveLabs,
    NodeStates = state.NodeStates.Values.ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    EvidenceInstances = state.EvidenceInstances.Values.ToArray(),
    CivilizationTraits = state.CivilizationTraits.ToArray(),
    Capabilities = state.Capabilities.ToArray(),
    FacilityCapabilities = state.FacilityCapabilities.ToArray(),
    EnabledDeploymentEventIds = state.EnabledDeploymentEventIds.ToArray(),
    ApplicabilityContexts = state.ApplicabilityContexts.Select(pair => new { ContextId = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    ActiveProjects = state.ActiveProjects.Values.ToArray()
};

MethodInfo Internal(string name, int count) => typeof(AdaptiveResearchCivilizationState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .Single(method => method.Name == name && method.GetParameters().Length == count);

var state = new AdaptiveResearchCivilizationState("civ:alpha", "directed:initial");
var commands = new List<object>();
void Run(string op, object input, Func<object?> invoke) {
    var frozenInput = Freeze(input); var before = Freeze(Snapshot(state)); object? typed = null; Exception? caught = null;
    try { typed = invoke(); }
    catch (TargetInvocationException error) { caught = error.InnerException ?? error; }
    catch (Exception error) { caught = error; }
    var result = caught is null ? Freeze(typed) : (JsonElement?)null;
    var failure = caught is null ? (JsonElement?)null : Freeze(new { Type = caught.GetType().Name, caught.Message });
    commands.Add(new { Op = op, Input = frozenInput, Result = result, Error = failure, Before = before, After = Freeze(Snapshot(state)) });
}
void Mutate(string op, object input, string method, params object?[] values) { var target = Internal(method, values.Length); Run(op, input, () => target.Invoke(state, values)); }

Mutate("SetLabs", new { Value = 12.5 }, "SetTotalEffectiveResearchLabs", 12.5);
Mutate("SetLabs", new { Value = 12.50000005 }, "SetTotalEffectiveResearchLabs", 12.50000005);
Mutate("SetLabs", new { Value = double.NaN }, "SetTotalEffectiveResearchLabs", double.NaN);
foreach (var (id,value) in new[]{("p1",1.0),("p2",2.0),("p3",double.NaN),("p1",1.00000005),("p2",0.0),("p4",double.PositiveInfinity),("p5",double.NegativeInfinity)})
    Mutate("SetPressure", new { PressureId=id, Value=value }, "SetPressure", id, value);
var e1=new ResearchEvidenceInstance("e1","type:a","first",.7,.8,null,4);
var e2=new ResearchEvidenceInstance("e2","type:a","second",.6,.9,"",5);
var e3=new ResearchEvidenceInstance("e3","type:b","third",double.NaN,double.PositiveInfinity,"ctx",6);
foreach(var evidence in new[]{e1,e2,e3,e1}) Mutate("AddEvidence",evidence,"AddEvidence",evidence);
Mutate("RemoveEvidence",new{Id="e2"},"RemoveEvidence","e2");
Mutate("AddEvidence",new ResearchEvidenceInstance("e4","type:a","fourth",.2,.3,"other",7),"AddEvidence",new ResearchEvidenceInstance("e4","type:a","fourth",.2,.3,"other",7));
Run("HasEvidence",new{Type="type:a",Context=(string?)null},()=>state.HasEvidenceType("type:a",null));
Run("HasEvidence",new{Type="type:a",Context="ctx"},()=>state.HasEvidenceType("type:a","ctx"));
Run("HasEvidence",new{Type="type:a",Context=""},()=>state.HasEvidenceType("type:a",""));
foreach(var id in new[]{"trait:a","trait:b","trait:c","trait:a"}) Mutate("AddTrait",new{Id=id},"AddCivilizationTrait",id);
Mutate("RemoveTrait",new{Id="trait:b"},"RemoveCivilizationTrait","trait:b"); Mutate("AddTrait",new{Id="trait:d"},"AddCivilizationTrait","trait:d");
Mutate("SetContext",new{Context="planet:1",Traits=new[]{"z","a","z"}},"SetApplicabilityContextTraits","planet:1",new[]{"z","a","z"});
Mutate("SetContext",new{Context="planet:1",Traits=new[]{"a","z"}},"SetApplicabilityContextTraits","planet:1",new[]{"a","z"});
Mutate("AddContextTrait",new{Context="",Trait="empty"},"AddApplicabilityTrait","","empty");
Mutate("AddContextTrait",new{Context="\u00a0",Trait="nbsp"},"AddApplicabilityTrait","\u00a0","nbsp");
Mutate("AddContextTrait",new{Context="\u2003",Trait="em-space"},"AddApplicabilityTrait","\u2003","em-space");
Mutate("AddContextTrait",new{Context="planet:2",Trait="cold"},"AddApplicabilityTrait","planet:2","cold");
Mutate("SetContext",new{Context="unicode",Traits=new[]{"\ue000","\U00010000","a"}},"SetApplicabilityContextTraits","unicode",new[]{"\ue000","\U00010000","a"});
Mutate("RemoveContextTrait",new{Context="planet:1",Trait="z"},"RemoveApplicabilityTrait","planet:1","z");
foreach(var key in new[]{new ResearchCapabilityKey("cap",null),new ResearchCapabilityKey("cap",""),new ResearchCapabilityKey("cap","ctx"),new ResearchCapabilityKey("cap",null)}) Mutate("AddCapability",key,"AddCapability",key.CapabilityId,key.ContextId);
Mutate("RemoveCapability",new{CapabilityId="cap",ContextId=""},"RemoveCapability","cap","");
foreach(var id in new[]{"lab:a","lab:b","lab:a"}) Mutate("AddFacility",new{Id=id},"AddFacilityCapability",id);
Mutate("RemoveFacility",new{Id="lab:a"},"RemoveFacilityCapability","lab:a"); Mutate("AddFacility",new{Id="lab:c"},"AddFacilityCapability","lab:c");
foreach(var id in new[]{"deploy:a","deploy:b","deploy:a"}) Mutate("AddDeployment",new{Id=id},"AddEnabledDeploymentEvent",id);
Mutate("SetStage",new{Id="directed:initial"},"SetDirectedProgramStage","directed:initial"); Mutate("SetStage",new{Id="directed:next"},"SetDirectedProgramStage","directed:next");
var n1=new ResearchNodeRuntimeState("n1",ResearchMaturity.Mature,"x",1,2,99); var n2=new ResearchNodeRuntimeState("n2",ResearchMaturity.Archived,"MATURE_HISTORY",3,4,99); var n3=new ResearchNodeRuntimeState("n3",ResearchMaturity.Archived,"Superseded",double.NaN,double.PositiveInfinity,99);
foreach(var node in new[]{n1,n2,n3,n1}) Mutate("SetNode",node,"SetNodeState",node);
Run("Established",new{Id="n2"},()=>state.HasEstablishedKnowledge("n2")); Run("Established",new{Id="n3"},()=>state.HasEstablishedKnowledge("n3"));
Mutate("RemoveNode",new{Id="n2"},"RemoveNodeState","n2"); Mutate("SetNode",new ResearchNodeRuntimeState("n4",ResearchMaturity.Archived,"failed",5,6,0),"SetNodeState",new ResearchNodeRuntimeState("n4",ResearchMaturity.Archived,"failed",5,6,0));
var p1=new ResearchProjectRuntimeState("n1",ResearchMaturity.Experimental,null,3,.8,false,null,1,2,0); var p2=new ResearchProjectRuntimeState("n2",ResearchMaturity.Demonstrated,"",5,.7,true,"paused",3,4,0); var p3=new ResearchProjectRuntimeState("n3",ResearchMaturity.Engineering,"ctx",double.NaN,double.PositiveInfinity,false,null,5,6,0);
foreach(var project in new[]{p1,p2,p3,p1}) Mutate("SetProject",project,"SetProject",project);
Mutate("RemoveProject",new{Id="n2"},"RemoveProject","n2"); Mutate("SetProject",new ResearchProjectRuntimeState("n4",ResearchMaturity.Experimental,null,2,1,false,null,0,0,0),"SetProject",new ResearchProjectRuntimeState("n4",ResearchMaturity.Experimental,null,2,1,false,null,0,0,0));
Mutate("MarkViewDirty",new{},"MarkViewDirty");
Run("GetPressure",new{Id="p4"},()=>state.GetPressure("p4")); Run("GetPressure",new{Id="missing"},()=>state.GetPressure("missing"));
Run("HasTrait",new{Id="trait:d"},()=>state.HasTrait("trait:d")); Run("HasTrait",new{Id="trait:b"},()=>state.HasTrait("trait:b"));
Run("HasApplicabilityTrait",new{Context="planet:1",Trait="a"},()=>state.HasApplicabilityTrait("planet:1","a"));
Run("GetApplicabilityTraits",new{Context="planet:1"},()=>state.GetApplicabilityTraits("planet:1"));
Run("GetApplicabilityTraits",new{Context="unicode"},()=>state.GetApplicabilityTraits("unicode"));
Run("HasFacility",new{Id="lab:c"},()=>state.HasFacilityCapability("lab:c"));
Run("HasCapability",new{Id="cap",Context=(string?)null},()=>state.HasCapability("cap",null));
Run("HasCapability",new{Id="cap",Context=""},()=>state.HasCapability("cap",""));
Run("HasDeployment",new{Id="deploy:b"},()=>state.IsDeploymentEventEnabled("deploy:b"));

var constructors=new List<object>();
foreach(var values in new[]{new[]{"","stage"},new[]{"  ","stage"},new[]{"\u00a0","stage"},new[]{"\u2003","stage"},new[]{"civ",""},new[]{"civ","\u00a0"},new[]{"civ","\u2003"}}){AdaptiveResearchCivilizationState? made=null;Exception? error=null;try{made=new(values[0],values[1]);}catch(Exception caught){error=caught;}constructors.Add(new{Input=new{CivilizationId=values[0],StageId=values[1]},Result=made is null?(JsonElement?)null:Freeze(Snapshot(made)),Error=error is null?(JsonElement?)null:Freeze(new{Type=error.GetType().Name,error.Message})});}
File.WriteAllText(args[0],JsonSerializer.Serialize(new{Schema="stellar-adaptive-research-state-v1",Culture="InvariantCulture",ExpertiseSidecar="pending-separate-gate",Commands=commands,ConstructorCases=constructors},options)+Environment.NewLine);
Console.WriteLine($"research state oracle: {commands.Count} operations, {constructors.Count} constructor cases");
