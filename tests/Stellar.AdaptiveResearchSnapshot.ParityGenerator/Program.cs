using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;
if(args.Length!=2)throw new ArgumentException("Expected canonical research directory and output fixture path.");
var root=Path.GetFullPath(args[0]);var output=Path.GetFullPath(args[1]);
var outer=new JsonSerializerOptions{WriteIndented=false,NumberHandling=System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals};
JsonElement Freeze(object value)=>JsonSerializer.SerializeToElement(value,outer);
object State(AdaptiveResearchCivilizationState s)=>new{s.CivilizationId,s.Revision,s.MaterializedViewRevision,s.DirectedProgramStageId,s.TotalEffectiveResearchLabs,s.AssignedEffectiveLabs,s.FreeEffectiveLabs,Nodes=s.NodeStates.Values.ToArray(),Pressures=s.Pressures.ToArray(),Evidence=s.EvidenceInstances.Values.ToArray(),CivilizationTraits=s.CivilizationTraits.ToArray(),ApplicabilityContexts=s.ApplicabilityContexts.Select(x=>new{x.Key,Traits=x.Value.ToArray()}).ToArray(),Capabilities=s.Capabilities.ToArray(),FacilityCapabilities=s.FacilityCapabilities.ToArray(),DeploymentEvents=s.EnabledDeploymentEventIds.ToArray(),Projects=s.ActiveProjects.Values.ToArray(),Expertise="pending-separate-gate"};
object Error(Exception e)=>new{Type=e.GetType().Name,e.Message};
object? Invoke(AdaptiveResearchCivilizationState state,string method,params object?[] arguments)=>typeof(AdaptiveResearchCivilizationState).GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(state,arguments);
AdaptiveResearchCivilizationState BuildState(AdaptiveResearchStateSnapshot snapshot)
{
 var state=new AdaptiveResearchCivilizationState(snapshot.CivilizationId,snapshot.DirectedProgramStageId);
 Invoke(state,"SetTotalEffectiveResearchLabs",snapshot.TotalEffectiveResearchLabs);
 foreach(var pair in snapshot.Pressures)Invoke(state,"SetPressure",pair.Key,pair.Value);
 foreach(var evidence in snapshot.Evidence)Invoke(state,"AddEvidence",new ResearchEvidenceInstance(evidence.EvidenceInstanceId,evidence.EvidenceTypeId,evidence.Provenance,evidence.Quality,evidence.Confidence,evidence.ContextId,state.Revision+1));
 foreach(var trait in snapshot.CivilizationTraits)Invoke(state,"AddCivilizationTrait",trait);
 foreach(var context in snapshot.ApplicabilityContexts)Invoke(state,"SetApplicabilityContextTraits",context.Key,context.Value);
 foreach(var capability in snapshot.Capabilities)Invoke(state,"AddCapability",capability.CapabilityId,capability.ContextId);
 foreach(var facility in snapshot.FacilityCapabilities)Invoke(state,"AddFacilityCapability",facility);
 foreach(var deployment in snapshot.EnabledDeploymentEventIds)Invoke(state,"AddEnabledDeploymentEvent",deployment);
 foreach(var node in snapshot.Nodes)Invoke(state,"SetNodeState",new ResearchNodeRuntimeState(node.NodeId,node.Maturity,node.Resolution,node.StageResearchPoints,node.TotalResearchPoints,state.Revision+1));
 foreach(var project in snapshot.ActiveProjects)Invoke(state,"SetProject",new ResearchProjectRuntimeState(project.NodeId,project.Stage,project.TargetApplicabilityContextId,project.AssignedEffectiveLabs,project.ReadinessEfficiency,project.Paused,project.PauseReason,project.StageResearchPoints,project.TotalResearchPoints,state.Revision+1));
 return state;
}
string InsertDuplicateProperty(string json,string objectProperty,string key,string rawValue)
{
 var marker=$"\"{objectProperty}\":{{";var start=json.IndexOf(marker,StringComparison.Ordinal)+marker.Length;
 if(start<marker.Length)throw new InvalidOperationException($"Missing JSON object '{objectProperty}'.");
 var end=json.IndexOf('}',start);if(end<0)throw new InvalidOperationException($"Unterminated JSON object '{objectProperty}'.");
 return json.Insert(end,$",{JsonSerializer.Serialize(key)}:{rawValue}");
}
var runtime=AdaptiveResearchRuntime.LoadFromDirectory(root);var codec=new AdaptiveResearchSnapshotCodec(runtime);
var civTrait=runtime.Applicability.Traits.Values.First(x=>x.Scope==ResearchApplicabilityTraitScope.Civilization).Id;
var popTrait=runtime.Applicability.Traits.Values.First(x=>x.Scope==ResearchApplicabilityTraitScope.PopulationOrSpecies).Id;
var civCap=runtime.Catalog.Capabilities.Values.First(x=>x.Scope==ResearchCapabilityScope.Civilization).Id;
var scopedCap=runtime.Catalog.Capabilities.Values.First(x=>x.Scope!=ResearchCapabilityScope.Civilization).Id;
var facility=runtime.Facilities.FacilityCapabilityIds.First();var deployment=runtime.Catalog.DeploymentEvents.Keys.First();
var pressureIds=runtime.Catalog.PressureIds.Take(2).ToArray();var evidenceType=runtime.Catalog.EvidenceTypeIds.First();
var projectNodes=runtime.Catalog.Nodes.Values.Where(x=>x.ProjectRequirements.MinimumLabs<=20).Take(2).ToArray();var n1=projectNodes[0];var n2=projectNodes[1];
var seed=new AdaptiveResearchStateSnapshot(1,runtime.Catalog.Metadata.CatalogId,"snapshot-civ",runtime.Catalog.Metadata.StartingDirectedProgramStageId,100,
 new[]{new AdaptiveResearchNodeSnapshot(n2.Id,ResearchMaturity.Experimental,null,2,3),new AdaptiveResearchNodeSnapshot(n1.Id,ResearchMaturity.Experimental,"",1,2),new AdaptiveResearchNodeSnapshot(n1.Id,ResearchMaturity.Experimental,"",4,5)},
 new Dictionary<string,double>(StringComparer.Ordinal){{pressureIds[1],22.5},{pressureIds[0],11.25}},
 new[]{new AdaptiveResearchEvidenceSnapshot("z-evidence",evidenceType,"lab-z",.8,.7,null),new AdaptiveResearchEvidenceSnapshot("a-evidence",evidenceType,"lab-a",.6,.5,"")},
 new[]{civTrait,civTrait},new Dictionary<string,IReadOnlyList<string>>(StringComparer.Ordinal){{"species-z",new[]{popTrait,popTrait}},{"species-a",new[]{popTrait}}},
 new[]{new AdaptiveResearchCapabilitySnapshot(scopedCap,""),new AdaptiveResearchCapabilitySnapshot(civCap,null),new AdaptiveResearchCapabilitySnapshot(civCap,null)},
 new[]{facility,facility},new[]{deployment,deployment},
 new[]{new AdaptiveResearchProjectSnapshot(n2.Id,ResearchMaturity.Experimental,null,n2.ProjectRequirements.MinimumLabs,1,false,null,2,3),new AdaptiveResearchProjectSnapshot(n1.Id,ResearchMaturity.Experimental,"",n1.ProjectRequirements.MinimumLabs,0.75,true,null,4,5),new AdaptiveResearchProjectSnapshot(n1.Id,ResearchMaturity.Experimental,"",n1.ProjectRequirements.MinimumLabs,0.8,true,"",6,7)});

var records=new List<object>();
object RestoreRecord(string name,AdaptiveResearchStateSnapshot snapshot){var before=Freeze(snapshot);AdaptiveResearchCivilizationState? result=null;Exception? failure=null;try{result=codec.Restore(snapshot);}catch(Exception e){failure=e;}var after=Freeze(snapshot);return new{Name=name,Kind="Restore",Input=before,InputAfter=after,Result=result is null?(JsonElement?)null:Freeze(State(result)),Error=failure is null?(JsonElement?)null:Freeze(Error(failure))};}
object DeserializeRecord(string name,string input){AdaptiveResearchCivilizationState? result=null;Exception? failure=null;try{result=codec.Deserialize(input);}catch(Exception e){failure=e;}return new{Name=name,Kind="Deserialize",Input=input,Result=result is null?(JsonElement?)null:Freeze(State(result)),Error=failure is null?(JsonElement?)null:Freeze(Error(failure))};}
object SerializeRecord(string name,AdaptiveResearchStateSnapshot setup){var state=BuildState(setup);var before=Freeze(State(state));string? result=null;Exception? failure=null;try{result=codec.Serialize(state);}catch(Exception e){failure=e;}return new{Name=name,Kind="Serialize",Setup=Freeze(setup),Input=before,InputAfter=Freeze(State(state)),Result=result,Error=failure is null?(JsonElement?)null:Freeze(Error(failure))};}
var seedRecord=RestoreRecord("restore-valid-unsorted-duplicates",seed);records.Add(seedRecord);var baseState=BuildState(seed);var beforeCapture=Freeze(State(baseState));AdaptiveResearchStateSnapshot captured;try{captured=codec.Capture(baseState);}catch{throw;}records.Add(new{Name="capture-sorted",Kind="Capture",Input=beforeCapture,InputAfter=Freeze(State(baseState)),Result=Freeze(captured),Error=(JsonElement?)null});
var beforeSerialize=Freeze(State(baseState));string serialized;try{serialized=codec.Serialize(baseState);}catch{throw;}records.Add(new{Name="serialize-deterministic",Kind="Serialize",Input=beforeSerialize,InputAfter=Freeze(State(baseState)),Result=serialized,Error=(JsonElement?)null});
records.Add(DeserializeRecord("deserialize-valid",serialized));
var withUnknown=JsonNode.Parse(serialized)!.AsObject();withUnknown["futureField"]=new JsonObject{{"ignored",true}};records.Add(DeserializeRecord("deserialize-unknown-property",withUnknown.ToJsonString()));
var numericEnum=JsonNode.Parse(serialized)!.AsObject();numericEnum["nodes"]![0]!["maturity"]=4;records.Add(DeserializeRecord("deserialize-numeric-enum",numericEnum.ToJsonString()));
var caseInsensitiveEnum=JsonNode.Parse(serialized)!.AsObject();caseInsensitiveEnum["nodes"]![0]!["maturity"]="ExPeRiMeNtAl";records.Add(DeserializeRecord("deserialize-case-insensitive-enum",caseInsensitiveEnum.ToJsonString()));
var numericStringEnum=JsonNode.Parse(serialized)!.AsObject();numericStringEnum["nodes"]![0]!["maturity"]="4";records.Add(DeserializeRecord("deserialize-numeric-string-enum",numericStringEnum.ToJsonString()));
var whitespaceEnum=JsonNode.Parse(serialized)!.AsObject();whitespaceEnum["nodes"]![0]!["maturity"]="  experimental  ";records.Add(DeserializeRecord("deserialize-whitespace-enum",whitespaceEnum.ToJsonString()));
records.Add(DeserializeRecord("deserialize-duplicate-pressure-key",InsertDuplicateProperty(serialized,"pressures",pressureIds[0],"91.5")));
records.Add(DeserializeRecord("deserialize-duplicate-context-key",InsertDuplicateProperty(serialized,"applicabilityContexts","species-a","[]")));
records.Add(RestoreRecord("restore-pressure-clamp-and-remove",captured with{Pressures=new Dictionary<string,double>{{pressureIds[0],-1},{pressureIds[1],150}}}));
var unknownMaturity=captured with{Nodes=captured.Nodes.Select((x,i)=>i==0?x with{Maturity=(ResearchMaturity)99}:x).ToArray(),ActiveProjects=Array.Empty<AdaptiveResearchProjectSnapshot>()};records.Add(SerializeRecord("serialize-unknown-enum-as-integer",unknownMaturity));

void Bad(string name,AdaptiveResearchStateSnapshot value)=>records.Add(RestoreRecord(name,value));
Bad("restore-schema",captured with{SchemaVersion=2});
Bad("restore-catalog",captured with{CatalogId="wrong-catalog"});
Bad("restore-stage",captured with{DirectedProgramStageId="unknown-stage"});
Bad("restore-civilization-empty",captured with{CivilizationId=""});
Bad("restore-total-labs",captured with{TotalEffectiveResearchLabs=-1});
Bad("restore-total-labs-nan",captured with{TotalEffectiveResearchLabs=double.NaN});
Bad("restore-pressure-unknown",captured with{Pressures=new Dictionary<string,double>{{"unknown-pressure",1}}});
Bad("restore-evidence-type",captured with{Evidence=new[]{captured.Evidence[0] with{EvidenceTypeId="unknown-evidence"}}});
Bad("restore-evidence-quality",captured with{Evidence=new[]{captured.Evidence[0] with{Quality=-.25}}});
Bad("restore-evidence-quality-nan",captured with{Evidence=new[]{captured.Evidence[0] with{Quality=double.NaN}}});
Bad("restore-evidence-confidence",captured with{Evidence=new[]{captured.Evidence[0] with{Confidence=1.25}}});
Bad("restore-evidence-duplicate",captured with{Evidence=new[]{captured.Evidence[0],captured.Evidence[0]}});
Bad("restore-civilization-trait-unknown",captured with{CivilizationTraits=new[]{"unknown-trait"}});
Bad("restore-civilization-trait-wrong-scope",captured with{CivilizationTraits=new[]{popTrait}});
Bad("restore-context-trait-unknown",captured with{ApplicabilityContexts=new Dictionary<string,IReadOnlyList<string>>{{"ctx",new[]{"unknown-trait"}}}});
Bad("restore-context-trait-wrong-scope",captured with{ApplicabilityContexts=new Dictionary<string,IReadOnlyList<string>>{{"ctx",new[]{civTrait}}}});
Bad("restore-context-empty-id",captured with{ApplicabilityContexts=new Dictionary<string,IReadOnlyList<string>>{{"",new[]{popTrait}}}});
Bad("restore-capability-unknown",captured with{Capabilities=new[]{new AdaptiveResearchCapabilitySnapshot("unknown-capability",null)}});
Bad("restore-civilization-capability-context",captured with{Capabilities=new[]{new AdaptiveResearchCapabilitySnapshot(civCap,"")}});
Bad("restore-scoped-capability-null",captured with{Capabilities=new[]{new AdaptiveResearchCapabilitySnapshot(scopedCap,null)}});
Bad("restore-facility-unknown",captured with{FacilityCapabilities=new[]{"unknown-facility"}});
Bad("restore-deployment-unknown",captured with{EnabledDeploymentEventIds=new[]{"unknown-deployment"}});
Bad("restore-node-unknown",captured with{Nodes=new[]{captured.Nodes[0] with{NodeId="unknown-node"}},ActiveProjects=Array.Empty<AdaptiveResearchProjectSnapshot>()});
Bad("restore-node-maturity",captured with{Nodes=new[]{captured.Nodes[0] with{Maturity=(ResearchMaturity)0}},ActiveProjects=Array.Empty<AdaptiveResearchProjectSnapshot>()});
Bad("restore-node-negative-progress",captured with{Nodes=new[]{captured.Nodes[0] with{StageResearchPoints=-1}},ActiveProjects=Array.Empty<AdaptiveResearchProjectSnapshot>()});
var project=captured.ActiveProjects.First();var matchingNode=captured.Nodes.First(x=>x.NodeId==project.NodeId);
Bad("restore-project-node-unknown",captured with{ActiveProjects=new[]{project with{NodeId="unknown-node"}}});
Bad("restore-project-stage",captured with{ActiveProjects=new[]{project with{Stage=ResearchMaturity.Mature}}});
Bad("restore-project-no-node",captured with{Nodes=captured.Nodes.Where(x=>x.NodeId!=project.NodeId).ToArray(),ActiveProjects=new[]{project}});
Bad("restore-project-stage-mismatch",captured with{Nodes=captured.Nodes.Select(x=>x.NodeId==project.NodeId?x with{Maturity=ResearchMaturity.Demonstrated}:x).ToArray(),ActiveProjects=new[]{project}});
Bad("restore-project-min-labs",captured with{ActiveProjects=new[]{project with{AssignedEffectiveLabs=-1}}});
Bad("restore-project-readiness",captured with{ActiveProjects=new[]{project with{ReadinessEfficiency=0}}});
Bad("restore-project-readiness-nan",captured with{ActiveProjects=new[]{project with{ReadinessEfficiency=double.NaN}}});
Bad("restore-project-negative-progress",captured with{ActiveProjects=new[]{project with{StageResearchPoints=-1}}});
var activeProject=project with{Paused=false,AssignedEffectiveLabs=100};Bad("restore-project-total-labs",captured with{TotalEffectiveResearchLabs=projectNodes[0].ProjectRequirements.MinimumLabs,ActiveProjects=new[]{activeProject}});
records.Add(RestoreRecord("restore-retry-after-failure",captured));
var nanSnapshot=captured with{Pressures=new Dictionary<string,double>{{pressureIds[0],double.NaN}},Nodes=captured.Nodes.Select((x,i)=>i==0?x with{StageResearchPoints=double.NaN,TotalResearchPoints=double.NaN}:x).ToArray(),ActiveProjects=captured.ActiveProjects.Select((x,i)=>i==0?x with{AssignedEffectiveLabs=double.NaN,StageResearchPoints=double.NaN,TotalResearchPoints=double.NaN}:x).ToArray()};records.Add(RestoreRecord("restore-typed-nan-accepted",nanSnapshot));
records.Add(SerializeRecord("serialize-typed-nan-rejected",nanSnapshot));

records.Add(DeserializeRecord("deserialize-null","null"));
var schemaOverflow=JsonNode.Parse(serialized)!.AsObject();schemaOverflow["schemaVersion"]=2147483648L;records.Add(DeserializeRecord("deserialize-schema-int32-overflow",schemaOverflow.ToJsonString()));
var maturityOverflow=JsonNode.Parse(serialized)!.AsObject();maturityOverflow["nodes"]![0]!["maturity"]=2147483648L;records.Add(DeserializeRecord("deserialize-maturity-int32-overflow",maturityOverflow.ToJsonString()));
var unknownEnum=JsonNode.Parse(serialized)!.AsObject();unknownEnum["nodes"]![0]!["maturity"]="future";records.Add(DeserializeRecord("deserialize-unknown-enum",unknownEnum.ToJsonString()));
var numberString=JsonNode.Parse(serialized)!.AsObject();numberString["totalEffectiveResearchLabs"]="100";records.Add(DeserializeRecord("deserialize-number-string",numberString.ToJsonString()));
var missing=JsonNode.Parse(serialized)!.AsObject();missing.Remove("nodes");records.Add(DeserializeRecord("deserialize-missing-nodes",missing.ToJsonString()));
var nullNodes=JsonNode.Parse(serialized)!.AsObject();nullNodes["nodes"]=null;records.Add(DeserializeRecord("deserialize-null-nodes",nullNodes.ToJsonString()));
var nullPressures=JsonNode.Parse(serialized)!.AsObject();nullPressures["pressures"]=null;records.Add(DeserializeRecord("deserialize-null-pressures",nullPressures.ToJsonString()));
var schemaBeforeNullCollection=JsonNode.Parse(serialized)!.AsObject();schemaBeforeNullCollection["schemaVersion"]=2;schemaBeforeNullCollection.Remove("nodes");records.Add(DeserializeRecord("deserialize-schema-before-null-collection",schemaBeforeNullCollection.ToJsonString()));
var catalogBeforeNullCollection=JsonNode.Parse(serialized)!.AsObject();catalogBeforeNullCollection["catalogId"]="wrong-catalog";catalogBeforeNullCollection["pressures"]=null;records.Add(DeserializeRecord("deserialize-catalog-before-null-collection",catalogBeforeNullCollection.ToJsonString()));
var missingOptional=JsonNode.Parse(serialized)!.AsObject();missingOptional["nodes"]![0]!.AsObject().Remove("resolution");missingOptional["nodes"]![0]!.AsObject().Remove("stageResearchPoints");missingOptional["nodes"]![0]!.AsObject().Remove("totalResearchPoints");records.Add(DeserializeRecord("deserialize-missing-node-defaults",missingOptional.ToJsonString()));
var missingSchema=JsonNode.Parse(serialized)!.AsObject();missingSchema.Remove("schemaVersion");records.Add(DeserializeRecord("deserialize-missing-schema-default",missingSchema.ToJsonString()));
var missingCatalog=JsonNode.Parse(serialized)!.AsObject();missingCatalog.Remove("catalogId");records.Add(DeserializeRecord("deserialize-missing-catalog-default",missingCatalog.ToJsonString()));
var nullCatalog=JsonNode.Parse(serialized)!.AsObject();nullCatalog["catalogId"]=null;records.Add(DeserializeRecord("deserialize-null-catalog-default",nullCatalog.ToJsonString()));
var missingTotalLabs=JsonNode.Parse(serialized)!.AsObject();missingTotalLabs.Remove("totalEffectiveResearchLabs");records.Add(DeserializeRecord("deserialize-missing-total-labs-default",missingTotalLabs.ToJsonString()));
var missingMaturity=JsonNode.Parse(serialized)!.AsObject();missingMaturity["nodes"]![0]!.AsObject().Remove("maturity");records.Add(DeserializeRecord("deserialize-missing-maturity-default",missingMaturity.ToJsonString()));
var missingEvidenceDefaults=JsonNode.Parse(serialized)!.AsObject();missingEvidenceDefaults["evidence"]![0]!.AsObject().Remove("quality");missingEvidenceDefaults["evidence"]![0]!.AsObject().Remove("confidence");missingEvidenceDefaults["evidence"]![0]!.AsObject().Remove("contextId");records.Add(DeserializeRecord("deserialize-missing-evidence-defaults",missingEvidenceDefaults.ToJsonString()));
var missingProjectOptional=JsonNode.Parse(serialized)!.AsObject();missingProjectOptional["activeProjects"]![0]!.AsObject().Remove("targetApplicabilityContextId");missingProjectOptional["activeProjects"]![0]!.AsObject().Remove("paused");missingProjectOptional["activeProjects"]![0]!.AsObject().Remove("pauseReason");records.Add(DeserializeRecord("deserialize-missing-project-defaults",missingProjectOptional.ToJsonString()));
var missingProjectLabs=JsonNode.Parse(serialized)!.AsObject();missingProjectLabs["activeProjects"]![0]!.AsObject().Remove("assignedEffectiveLabs");records.Add(DeserializeRecord("deserialize-missing-project-labs-default",missingProjectLabs.ToJsonString()));
var missingProjectReadiness=JsonNode.Parse(serialized)!.AsObject();missingProjectReadiness["activeProjects"]![0]!.AsObject().Remove("readinessEfficiency");records.Add(DeserializeRecord("deserialize-missing-project-readiness-default",missingProjectReadiness.ToJsonString()));
var missingCapabilityContext=JsonNode.Parse(serialized)!.AsObject();var civilizationCapabilityRow=missingCapabilityContext["capabilities"]!.AsArray().First(x=>x!["capabilityId"]!.GetValue<string>()==civCap)!;civilizationCapabilityRow.AsObject().Remove("contextId");records.Add(DeserializeRecord("deserialize-missing-capability-context-default",missingCapabilityContext.ToJsonString()));
var wrongCollection=JsonNode.Parse(serialized)!.AsObject();wrongCollection["nodes"]=new JsonObject{{"unexpected",true}};records.Add(DeserializeRecord("deserialize-wrong-collection-type",wrongCollection.ToJsonString()));
records.Add(DeserializeRecord("deserialize-malformed-syntax","{\"schemaVersion\":1"));

var sourceOnlyNullString=JsonNode.Parse(serialized)!.AsObject();sourceOnlyNullString["evidence"]![0]!["provenance"]=null;var sourceOnlyCases=new[]{DeserializeRecord("source-only-null-nonnullable-evidence-provenance",sourceOnlyNullString.ToJsonString())};
File.WriteAllText(output,JsonSerializer.Serialize(new{Schema="stellar-adaptive-research-snapshot-oracle-v1",Seed=seed,Ids=new{CivilizationTrait=civTrait,PopulationTrait=popTrait,CivilizationCapability=civCap,ScopedCapability=scopedCap,Facility=facility,Deployment=deployment,PressureIds=pressureIds,EvidenceType=evidenceType,ProjectNodes=projectNodes.Select(x=>new{x.Id,x.ProjectRequirements.MinimumLabs}).ToArray()},Records=records,SourceOnlyCases=sourceOnlyCases,Metadata=new{Source="actual retained C# AdaptiveResearchSnapshotCodec",Culture="InvariantCulture",Expertise="pending-separate-gate",SourceOnlyBoundary="System.Text.Json can materialize null into non-nullable record string slots; the native owning-string DTO explicitly rejects that malformed input instead of collapsing null to empty."}},outer)+Environment.NewLine);
