using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchReadinessOracle <research-data> <fixture>");
    return 1;
}
try
{
var options = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
var root = Path.GetFullPath(args[0]);
var baseCatalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(root);
var facilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(root, baseCatalog);
var progress = AdaptiveResearchProgressPolicy.LoadFromDirectory(root, baseCatalog);
var expertiseCatalog = AdaptiveResearchExpertiseCatalog.LoadFromDirectory(root, baseCatalog, facilities);
var readiness = new AdaptiveResearchReadinessCalculator(baseCatalog, facilities, expertiseCatalog, progress);
var service = new AdaptiveResearchExpertiseService(baseCatalog, expertiseCatalog, readiness);
var coreMethods = typeof(AdaptiveResearchCivilizationState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .GroupBy(method => (method.Name, method.GetParameters().Length))
    .ToDictionary(group => group.Key, group => group.Single());
var expertiseMethods = typeof(AdaptiveResearchExpertiseState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .GroupBy(method => (method.Name, method.GetParameters().Length))
    .ToDictionary(group => group.Key, group => group.Single());

object ExpertiseSnapshot(AdaptiveResearchExpertiseState expertise) => new
{
    expertise.Revision,
    FieldCompetence = expertise.FieldCompetence.Values.ToArray(),
    Institutions = expertise.Institutions.Values.ToArray(),
    TacitAssets = expertise.TacitAssets.Values.ToArray(),
};
object StateSnapshot(AdaptiveResearchCivilizationState state) => new
{
    state.CivilizationId,
    state.Revision,
    state.MaterializedViewRevision,
    state.DirectedProgramStageId,
    state.TotalEffectiveResearchLabs,
    state.AssignedEffectiveLabs,
    state.FreeEffectiveLabs,
    NodeStates = state.NodeStates.Values.ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    EvidenceInstances = state.EvidenceInstances.Values.ToArray(),
    CivilizationTraits = state.CivilizationTraits.ToArray(),
    Capabilities = state.Capabilities.ToArray(),
    FacilityCapabilities = state.FacilityCapabilities.ToArray(),
    EnabledDeploymentEventIds = state.EnabledDeploymentEventIds.ToArray(),
    ApplicabilityContexts = state.ApplicabilityContexts.Select(pair => new { Id = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    ActiveProjects = state.ActiveProjects.Values.ToArray(),
    Expertise = ExpertiseSnapshot(state.Expertise),
};
void Core(AdaptiveResearchCivilizationState state, string name, params object?[] values)
{
    try { _ = coreMethods[(name, values.Length)].Invoke(state, values); }
    catch (TargetInvocationException caught) when (caught.InnerException is not null)
    {
        ExceptionDispatchInfo.Capture(caught.InnerException).Throw();
    }
}
void Expertise(AdaptiveResearchCivilizationState state, string name, params object?[] values)
{
    try { _ = expertiseMethods[(name, values.Length)].Invoke(state.Expertise, values); }
    catch (TargetInvocationException caught) when (caught.InnerException is not null)
    {
        ExceptionDispatchInfo.Capture(caught.InnerException).Throw();
    }
}
AdaptiveResearchCivilizationState NewState() => new("fixture", baseCatalog.Metadata.StartingDirectedProgramStageId);

var plainNode = baseCatalog.Nodes.Values.First(node => node.KnowledgeFields.Count > 0 && node.Applicability.EvidenceTypes.Count == 0);
var evidenceNode = baseCatalog.Nodes.Values.First(node => node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence).Any());
var xenoNode = baseCatalog.Nodes.Values.First(node => node.KnowledgeFields.Contains("xenoscience", StringComparer.Ordinal));
var facilityNode = baseCatalog.Nodes.Values.First(node => facilities.GetStageRequirement(node.Id, ResearchMaturity.Experimental) is not null);
var institution = expertiseCatalog.Institutions.Values.First();
var specializedInstitution = expertiseCatalog.Institutions.Values.First(value => value.SpecializedFieldIds.Any(field => plainNode.KnowledgeFields.Contains(field, StringComparer.Ordinal)));
var tacitType = expertiseCatalog.TacitAssetTypes.Values.First();
var evidenceType = evidenceNode.Applicability.EvidenceTypes.Concat(evidenceNode.ProjectRequirements.RequiredEvidence).First();

var readinessCases = new List<object>();
void ReadinessCase(string name, AdaptiveResearchCivilizationState state, object[] setup,
                   string nodeId, ResearchMaturity stage, double labs, string? context)
{
    var input = Freeze(new { NodeId = nodeId, Stage = (int)stage, AssignedEffectiveLabs = labs, Context = context, Setup = setup, State = StateSnapshot(state) });
    var before = Freeze(StateSnapshot(state));
    ResearchReadinessBreakdown? result = null;
    Exception? error = null;
    try { result = readiness.Calculate(state, state.Expertise, nodeId, stage, labs, context); }
    catch (Exception caught) { error = caught; }
    var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    var after = Freeze(StateSnapshot(state));
    readinessCases.Add(new { Name = name, Input = input, Result = frozenResult, Error = frozenError, Before = before, After = after });
}

ReadinessCase("empty-baseline", NewState(), [], plainNode.Id, ResearchMaturity.Experimental, 1.0, null);
foreach (var (name, labs) in new[] { ("zero-labs", 0.0), ("negative-labs", -1.0), ("nan-labs", double.NaN), ("positive-infinity-labs", double.PositiveInfinity) })
    ReadinessCase(name, NewState(), [], plainNode.Id, ResearchMaturity.Experimental, labs, null);
ReadinessCase("unknown-stage-before-node", NewState(), [], "missing-node", (ResearchMaturity)99, 1.0, null);
ReadinessCase("unknown-node", NewState(), [], "missing-node", ResearchMaturity.Experimental, 1.0, null);

var competenceState = NewState();
foreach (var field in plainNode.KnowledgeFields)
    service.SeedFieldCompetence(competenceState, field, new ResearchCompetenceVector(80, 40, 20), 5);
service.SetInstitution(competenceState, "specialized", specializedInstitution.InstitutionArchetypeId, 3, 2);
var competenceSetup = plainNode.KnowledgeFields.Select(field => (object)new { Op = "SeedField", FieldId = field, Value = new ResearchCompetenceVector(80, 40, 20), ActivityYear = 5.0 }).ToList();
competenceSetup.Add(new { Op = "SetInstitution", InstanceId = "specialized", ArchetypeId = specializedInstitution.InstitutionArchetypeId, Total = 3, Active = 2, Context = (string?)null });
ReadinessCase("competence-and-specialized-facility", competenceState, competenceSetup.ToArray(), plainNode.Id, ResearchMaturity.Experimental, 3.75, null);
ReadinessCase("competence-demonstrated", competenceState, competenceSetup.ToArray(), plainNode.Id, ResearchMaturity.Demonstrated, 3.75, null);
ReadinessCase("competence-engineering", competenceState, competenceSetup.ToArray(), plainNode.Id, ResearchMaturity.Engineering, 3.75, null);

var evidenceState = NewState();
Core(evidenceState, "AddEvidence", new ResearchEvidenceInstance("e:null", evidenceType, "fixture", .8, .5, null, 0));
Core(evidenceState, "AddEvidence", new ResearchEvidenceInstance("e:exact", evidenceType, "fixture", .5, .5, "target", 0));
Core(evidenceState, "AddEvidence", new ResearchEvidenceInstance("e:other", evidenceType, "fixture", 1, 1, "other", 0));
object[] evidenceSetup = [
    new { Op = "AddEvidence", InstanceId = "e:null", TypeId = evidenceType, Quality = .8, Confidence = .5, Context = (string?)null },
    new { Op = "AddEvidence", InstanceId = "e:exact", TypeId = evidenceType, Quality = .5, Confidence = .5, Context = "target" },
    new { Op = "AddEvidence", InstanceId = "e:other", TypeId = evidenceType, Quality = 1.0, Confidence = 1.0, Context = "other" },
];
ReadinessCase("evidence-null-context", evidenceState, evidenceSetup, evidenceNode.Id, ResearchMaturity.Experimental, 2, null);
ReadinessCase("evidence-target-context", evidenceState, evidenceSetup, evidenceNode.Id, ResearchMaturity.Experimental, 2, "target");
ReadinessCase("evidence-empty-context", evidenceState, evidenceSetup, evidenceNode.Id, ResearchMaturity.Experimental, 2, "");
foreach (var (name, quality) in new[] { ("evidence-nan", double.NaN), ("evidence-positive-infinity", double.PositiveInfinity), ("evidence-negative-infinity", double.NegativeInfinity) })
{
    var nonfiniteEvidenceState = NewState();
    Core(nonfiniteEvidenceState, "AddEvidence", new ResearchEvidenceInstance("e:nonfinite", evidenceType, "fixture", quality, 1, null, 0));
    object[] nonfiniteSetup = [new { Op = "AddEvidence", InstanceId = "e:nonfinite", TypeId = evidenceType, Quality = quality, Confidence = 1.0, Context = (string?)null }];
    ReadinessCase(name, nonfiniteEvidenceState, nonfiniteSetup, evidenceNode.Id, ResearchMaturity.Experimental, 2, null);
}

var contextInstitutionState = NewState();
service.SetInstitution(contextInstitutionState, "global", institution.InstitutionArchetypeId, 2, 2, null);
service.SetInstitution(contextInstitutionState, "target", institution.InstitutionArchetypeId, 2, 2, "target");
service.SetInstitution(contextInstitutionState, "other", institution.InstitutionArchetypeId, 2, 2, "other");
object[] institutionSetup = [
    new { Op = "SetInstitution", InstanceId = "global", ArchetypeId = institution.InstitutionArchetypeId, Total = 2, Active = 2, Context = (string?)null },
    new { Op = "SetInstitution", InstanceId = "target", ArchetypeId = institution.InstitutionArchetypeId, Total = 2, Active = 2, Context = "target" },
    new { Op = "SetInstitution", InstanceId = "other", ArchetypeId = institution.InstitutionArchetypeId, Total = 2, Active = 2, Context = "other" },
];
ReadinessCase("institution-context-filter", contextInstitutionState, institutionSetup, facilityNode.Id, ResearchMaturity.Experimental, 4, "target");

var tacitState = NewState();
service.SetTacitAsset(tacitState, "field", tacitType.Id, ResearchTacitScopeKind.KnowledgeField,
    xenoNode.KnowledgeFields[0], ResearchTacitAssimilationStage.Access, 80, .5, .75, .5, "fixture");
service.SetTacitAsset(tacitState, "node", tacitType.Id, ResearchTacitScopeKind.TechnologyNode,
    xenoNode.Id, ResearchTacitAssimilationStage.Codified, 70, .8, .7, .6, "fixture", "target");
service.SetTacitAsset(tacitState, "family", tacitType.Id, ResearchTacitScopeKind.SolutionFamily,
    xenoNode.SolutionFamily, ResearchTacitAssimilationStage.Trained, 60, .9, .8, .7, "fixture", "other");
service.SetTacitAsset(tacitState, "foreign", tacitType.Id, ResearchTacitScopeKind.ForeignLineage,
    "unknown-lineage", ResearchTacitAssimilationStage.NativePractice, 50, 1, 1, 1, "fixture");
object[] tacitSetup = [
    new { Op = "SetTacit", AssetId = "field", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.KnowledgeField, ScopeRef = xenoNode.KnowledgeFields[0], Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 80.0, Availability = .5, Translation = .75, Training = .5, Provenance = "fixture", Context = (string?)null },
    new { Op = "SetTacit", AssetId = "node", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.TechnologyNode, ScopeRef = xenoNode.Id, Assimilation = (int)ResearchTacitAssimilationStage.Codified, Depth = 70.0, Availability = .8, Translation = .7, Training = .6, Provenance = "fixture", Context = "target" },
    new { Op = "SetTacit", AssetId = "family", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.SolutionFamily, ScopeRef = xenoNode.SolutionFamily, Assimilation = (int)ResearchTacitAssimilationStage.Trained, Depth = 60.0, Availability = .9, Translation = .8, Training = .7, Provenance = "fixture", Context = "other" },
    new { Op = "SetTacit", AssetId = "foreign", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "unknown-lineage", Assimilation = (int)ResearchTacitAssimilationStage.NativePractice, Depth = 50.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null },
];
ReadinessCase("tacit-null-context", tacitState, tacitSetup, xenoNode.Id, ResearchMaturity.Experimental, 2, null);
ReadinessCase("tacit-target-context", tacitState, tacitSetup, xenoNode.Id, ResearchMaturity.Experimental, 2, "target");
ReadinessCase("xenoscience-no-tacit-material-zero", NewState(), [], xenoNode.Id, ResearchMaturity.Experimental, 2, null);

var facilityRequirement = facilities.GetStageRequirement(facilityNode.Id, ResearchMaturity.Experimental)!;
var processRef = facilityRequirement.AllOf.Concat(facilityRequirement.AnyOf).First();
var processTacitState = NewState();
service.SetTacitAsset(processTacitState, "process", tacitType.Id, ResearchTacitScopeKind.FacilityOrProcess,
    processRef, ResearchTacitAssimilationStage.Interpreted, 45, .8, .7, .6, "fixture");
object[] processSetup = [new { Op = "SetTacit", AssetId = "process", AssetTypeId = tacitType.Id,
    Scope = (int)ResearchTacitScopeKind.FacilityOrProcess, ScopeRef = processRef,
    Assimilation = (int)ResearchTacitAssimilationStage.Interpreted, Depth = 45.0,
    Availability = .8, Translation = .7, Training = .6, Provenance = "fixture", Context = (string?)null }];
ReadinessCase("facility-process-tacit", processTacitState, processSetup, facilityNode.Id, ResearchMaturity.Experimental, 2, null);

var unknownAssimilationState = NewState();
var unknownAssimilationAsset = new ResearchTacitAssetRuntimeState("unknown-assimilation", tacitType.Id,
    ResearchTacitScopeKind.TechnologyNode, xenoNode.Id, (ResearchTacitAssimilationStage)99,
    20, 1, 1, 1, "fixture", null, 0);
Expertise(unknownAssimilationState, "SetTacitAsset", unknownAssimilationAsset);
object[] unknownAssimilationSetup = [new { Op = "InjectTacit", Value = unknownAssimilationAsset }];
ReadinessCase("relevant-unknown-assimilation", unknownAssimilationState, unknownAssimilationSetup,
    xenoNode.Id, ResearchMaturity.Experimental, 2, null);

var otherNode = baseCatalog.Nodes.Values.First(node => node.Id != xenoNode.Id);
var irrelevantUnknownState = NewState();
var irrelevantUnknownAsset = unknownAssimilationAsset with { AssetId = "irrelevant-unknown", ScopeRef = otherNode.Id };
Expertise(irrelevantUnknownState, "SetTacitAsset", irrelevantUnknownAsset);
object[] irrelevantUnknownSetup = [new { Op = "InjectTacit", Value = irrelevantUnknownAsset }];
ReadinessCase("irrelevant-unknown-assimilation-skipped", irrelevantUnknownState, irrelevantUnknownSetup,
    xenoNode.Id, ResearchMaturity.Experimental, 2, null);

var commands = new List<object>();
var commandState = NewState();
string CommandOperation(string name) => name switch
{
    "seed-field" or "seed-field-preserves-greater" or "seed-field-nan" or "seed-unknown-field" => "SeedFieldCompetence",
    "set-institution" or "replace-institution-inactive" or "set-institution-invalid-count" or "set-institution-unknown-before-count" or "set-zero-missing-institution" or "set-institution-long-context" => "SetInstitution",
    "remove-institution" or "remove-institution-missing" => "RemoveInstitution",
    "set-tacit-foreign-unknown-ref" or "set-tacit-unknown-scope" or "set-tacit-blank-scope" or
    "set-tacit-unknown-type-before-blank" or "set-tacit-unknown-node" or "set-tacit-unknown-family" or
    "set-tacit-unknown-process" or "set-tacit-invalid-depth" or "set-tacit-access" or
    "set-tacit-interpreted" or "set-tacit-codified" or "set-tacit-trained" or "set-tacit-native" => "SetTacitAsset",
    "remove-tacit" or "remove-tacit-missing" => "RemoveTacitAsset",
    "practice-unsupported-stage-noop" or "practice-experimental" or "practice-repeat" => "ApplyCompletedStagePractice",
    "atrophy-zero-noop" or "atrophy-negative-noop" or "atrophy-nan-noop" or "atrophy-infinity-noop" or "atrophy-valid" => "ApplyCompetenceAtrophy",
    "readiness-through-service" => "CalculateProjectReadiness",
    _ => throw new InvalidOperationException($"Unknown fixture command '{name}'."),
};
void Command(string name, object inputValue, Func<object?> invoke)
{
    var input = Freeze(new { Op = CommandOperation(name), Args = inputValue });
    var before = Freeze(StateSnapshot(commandState));
    object? result = null;
    Exception? error = null;
    try { result = invoke(); }
    catch (Exception caught) { error = caught; }
    var frozenResult = Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    var after = Freeze(StateSnapshot(commandState));
    commands.Add(new { Name = name, Input = input, Result = frozenResult, Error = frozenError, Before = before, After = after });
}
var fieldId = plainNode.KnowledgeFields[0];
Command("seed-field", new { FieldId = fieldId, Value = new ResearchCompetenceVector(20, 30, 40), ActivityYear = 10.0 }, () => { service.SeedFieldCompetence(commandState, fieldId, new(20, 30, 40), 10); return null; });
Command("seed-field-preserves-greater", new { FieldId = fieldId, Value = new ResearchCompetenceVector(10, 50, 20), ActivityYear = 5.0 }, () => { service.SeedFieldCompetence(commandState, fieldId, new(10, 50, 20), 5); return null; });
Command("seed-field-nan", new { FieldId = fieldId, Value = new ResearchCompetenceVector(double.NaN, 1, 1), ActivityYear = 1.0 }, () => { service.SeedFieldCompetence(commandState, fieldId, new(double.NaN, 1, 1), 1); return null; });
Command("seed-unknown-field", new { FieldId = "missing-field", Value = new ResearchCompetenceVector(1, 2, 3), ActivityYear = 1.0 }, () => { service.SeedFieldCompetence(commandState, "missing-field", new(1, 2, 3), 1); return null; });
Command("set-institution", new { InstanceId = "lab:one", ArchetypeId = specializedInstitution.InstitutionArchetypeId, Total = 3, Active = 2, Context = (string?)null }, () => { service.SetInstitution(commandState, "lab:one", specializedInstitution.InstitutionArchetypeId, 3, 2); return null; });
Command("replace-institution-inactive", new { InstanceId = "lab:one", ArchetypeId = specializedInstitution.InstitutionArchetypeId, Total = 3, Active = 0, Context = "" }, () => { service.SetInstitution(commandState, "lab:one", specializedInstitution.InstitutionArchetypeId, 3, 0, ""); return null; });
Command("set-institution-invalid-count", new { InstanceId = "lab:bad", ArchetypeId = specializedInstitution.InstitutionArchetypeId, Total = 0, Active = 1, Context = (string?)null }, () => { service.SetInstitution(commandState, "lab:bad", specializedInstitution.InstitutionArchetypeId, 0, 1); return null; });
Command("set-institution-unknown-before-count", new { InstanceId = "lab:bad", ArchetypeId = "missing-archetype", Total = 0, Active = 1, Context = (string?)null }, () => { service.SetInstitution(commandState, "lab:bad", "missing-archetype", 0, 1); return null; });
Command("remove-institution", new { InstanceId = "lab:one" }, () => service.RemoveInstitution(commandState, "lab:one"));
Command("remove-institution-missing", new { InstanceId = "lab:one" }, () => service.RemoveInstitution(commandState, "lab:one"));
Command("set-zero-missing-institution", new { InstanceId = "lab:zero", ArchetypeId = specializedInstitution.InstitutionArchetypeId, Total = 0, Active = 0, Context = (string?)null }, () => { service.SetInstitution(commandState, "lab:zero", specializedInstitution.InstitutionArchetypeId, 0, 0); return null; });
var longContext = "context:" + new string('x', 96);
Command("set-institution-long-context", new { InstanceId = "lab:long-context", ArchetypeId = institution.InstitutionArchetypeId, Total = 1, Active = 1, Context = longContext }, () => { service.SetInstitution(commandState, "lab:long-context", institution.InstitutionArchetypeId, 1, 1, longContext); return null; });
Command("set-tacit-foreign-unknown-ref", new { AssetId = "asset:foreign", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "unknown", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 25.0, Availability = .5, Translation = .5, Training = .5, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:foreign", tacitType.Id, ResearchTacitScopeKind.ForeignLineage, "unknown", ResearchTacitAssimilationStage.Access, 25, .5, .5, .5, "fixture"); return null; });
Command("set-tacit-unknown-scope", new { AssetId = "asset:unknown-scope", AssetTypeId = tacitType.Id, Scope = 99, ScopeRef = "opaque", Assimilation = (int)ResearchTacitAssimilationStage.Interpreted, Depth = 25.0, Availability = .5, Translation = .5, Training = .5, Provenance = "fixture", Context = "" }, () => { service.SetTacitAsset(commandState, "asset:unknown-scope", tacitType.Id, (ResearchTacitScopeKind)99, "opaque", ResearchTacitAssimilationStage.Interpreted, 25, .5, .5, .5, "fixture", ""); return null; });
Command("set-tacit-blank-scope", new { AssetId = "asset:blank", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "\u2003", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 1.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:blank", tacitType.Id, ResearchTacitScopeKind.ForeignLineage, "\u2003", ResearchTacitAssimilationStage.Access, 1, 1, 1, 1, "fixture"); return null; });
Command("set-tacit-unknown-type-before-blank", new { AssetId = "asset:bad", AssetTypeId = "missing-type", Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 1.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:bad", "missing-type", ResearchTacitScopeKind.ForeignLineage, "", ResearchTacitAssimilationStage.Access, 1, 1, 1, 1, "fixture"); return null; });
Command("set-tacit-unknown-node", new { AssetId = "asset:bad", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.TechnologyNode, ScopeRef = "missing-node", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 1.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:bad", tacitType.Id, ResearchTacitScopeKind.TechnologyNode, "missing-node", ResearchTacitAssimilationStage.Access, 1, 1, 1, 1, "fixture"); return null; });
Command("set-tacit-unknown-family", new { AssetId = "asset:bad", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.SolutionFamily, ScopeRef = "missing-family", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 1.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:bad", tacitType.Id, ResearchTacitScopeKind.SolutionFamily, "missing-family", ResearchTacitAssimilationStage.Access, 1, 1, 1, 1, "fixture"); return null; });
Command("set-tacit-unknown-process", new { AssetId = "asset:bad", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.FacilityOrProcess, ScopeRef = "missing-process", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = 1.0, Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:bad", tacitType.Id, ResearchTacitScopeKind.FacilityOrProcess, "missing-process", ResearchTacitAssimilationStage.Access, 1, 1, 1, 1, "fixture"); return null; });
Command("set-tacit-invalid-depth", new { AssetId = "asset:bad", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "lineage", Assimilation = (int)ResearchTacitAssimilationStage.Access, Depth = double.NaN, Availability = 2.0, Translation = 2.0, Training = 2.0, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:bad", tacitType.Id, ResearchTacitScopeKind.ForeignLineage, "lineage", ResearchTacitAssimilationStage.Access, double.NaN, 2, 2, 2, "fixture"); return null; });
foreach (var (name, stage) in new[] { ("set-tacit-access", ResearchTacitAssimilationStage.Access), ("set-tacit-interpreted", ResearchTacitAssimilationStage.Interpreted), ("set-tacit-codified", ResearchTacitAssimilationStage.Codified), ("set-tacit-trained", ResearchTacitAssimilationStage.Trained), ("set-tacit-native", ResearchTacitAssimilationStage.NativePractice) })
    Command(name, new { AssetId = "asset:stages", AssetTypeId = tacitType.Id, Scope = (int)ResearchTacitScopeKind.ForeignLineage, ScopeRef = "lineage", Assimilation = (int)stage, Depth = 10.0, Availability = .5, Translation = .5, Training = .5, Provenance = "fixture", Context = (string?)null }, () => { service.SetTacitAsset(commandState, "asset:stages", tacitType.Id, ResearchTacitScopeKind.ForeignLineage, "lineage", stage, 10, .5, .5, .5, "fixture"); return null; });
Command("remove-tacit", new { AssetId = "asset:foreign" }, () => service.RemoveTacitAsset(commandState, "asset:foreign"));
Command("remove-tacit-missing", new { AssetId = "asset:foreign" }, () => service.RemoveTacitAsset(commandState, "asset:foreign"));
Command("practice-unsupported-stage-noop", new { NodeId = plainNode.Id, Stage = (int)ResearchMaturity.Rumored, ActivityYear = 20.0 }, () => { service.ApplyCompletedStagePractice(commandState, plainNode.Id, ResearchMaturity.Rumored, 20); return null; });
Command("practice-experimental", new { NodeId = plainNode.Id, Stage = (int)ResearchMaturity.Experimental, ActivityYear = 20.0 }, () => { service.ApplyCompletedStagePractice(commandState, plainNode.Id, ResearchMaturity.Experimental, 20); return null; });
Command("practice-repeat", new { NodeId = plainNode.Id, Stage = (int)ResearchMaturity.Experimental, ActivityYear = 21.0 }, () => { service.ApplyCompletedStagePractice(commandState, plainNode.Id, ResearchMaturity.Experimental, 21); return null; });
Command("atrophy-zero-noop", new { CurrentYear = 100.0, ElapsedYears = 0.0 }, () => { service.ApplyCompetenceAtrophy(commandState, 100, 0); return null; });
Command("atrophy-negative-noop", new { CurrentYear = 100.0, ElapsedYears = -1.0 }, () => { service.ApplyCompetenceAtrophy(commandState, 100, -1); return null; });
Command("atrophy-nan-noop", new { CurrentYear = 100.0, ElapsedYears = double.NaN }, () => { service.ApplyCompetenceAtrophy(commandState, 100, double.NaN); return null; });
Command("atrophy-infinity-noop", new { CurrentYear = 100.0, ElapsedYears = double.PositiveInfinity }, () => { service.ApplyCompetenceAtrophy(commandState, 100, double.PositiveInfinity); return null; });
Command("atrophy-valid", new { CurrentYear = 100.0, ElapsedYears = 10.0 }, () => { service.ApplyCompetenceAtrophy(commandState, 100, 10); return null; });
Command("readiness-through-service", new { NodeId = plainNode.Id, Stage = (int)ResearchMaturity.Experimental, AssignedEffectiveLabs = 2.0, Context = (string?)null }, () => service.CalculateProjectReadiness(commandState, plainNode.Id, ResearchMaturity.Experimental, 2));

var serviceCases = new List<object>();
void ServiceCase(string name, AdaptiveResearchCivilizationState state, object[] setup,
                 string op, object argsValue, Func<object?> invoke)
{
    var input = Freeze(new { Setup = setup, Op = op, Args = argsValue });
    var before = Freeze(StateSnapshot(state));
    object? result = null;
    Exception? error = null;
    try { result = invoke(); }
    catch (Exception caught) { error = caught; }
    var frozenResult = Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    serviceCases.Add(new { Name = name, Input = input, Result = frozenResult, Error = frozenError,
        Before = before, After = Freeze(StateSnapshot(state)) });
}

var partialState = NewState();
var unknownInstitution = new ResearchInstitutionRuntimeState("unknown:active", "missing-archetype", null, 1, 1, 0);
Expertise(partialState, "SetInstitution", unknownInstitution);
object[] partialSetup = [new { Op = "InjectInstitution", Value = unknownInstitution }];
var partialArgs = new { InstanceId = "valid:after-unknown", ArchetypeId = institution.InstitutionArchetypeId,
    Total = 1, Active = 1, Context = (string?)null };
ServiceCase("institution-sync-partial-failure", partialState, partialSetup, "SetInstitution", partialArgs,
    () => { service.SetInstitution(partialState, partialArgs.InstanceId, partialArgs.ArchetypeId, partialArgs.Total, partialArgs.Active); return null; });

var graceState = NewState();
service.SeedFieldCompetence(graceState, fieldId, new(80, 70, 60), 98);
object[] graceSetup = [new { Op = "SeedField", FieldId = fieldId, Value = new ResearchCompetenceVector(80, 70, 60), ActivityYear = 98.0 }];
ServiceCase("atrophy-grace-dirties-view", graceState, graceSetup, "ApplyCompetenceAtrophy",
    new { CurrentYear = 100.0, ElapsedYears = 10.0 }, () => { service.ApplyCompetenceAtrophy(graceState, 100, 10); return null; });

var floorState = NewState();
service.SeedFieldCompetence(floorState, fieldId, new(80, 70, 60), 0);
var matureNodeState = new ResearchNodeRuntimeState(plainNode.Id, ResearchMaturity.Mature, null, 0, 0, 0);
Core(floorState, "SetNodeState", matureNodeState);
var floorAssetType = expertiseCatalog.TacitAssetTypes.Values.First(type => type.SupportedComponents.Count == 3);
service.SetTacitAsset(floorState, "floor:asset", floorAssetType.Id, ResearchTacitScopeKind.KnowledgeField,
    fieldId, ResearchTacitAssimilationStage.NativePractice, 50, 1, 1, 1, "fixture");
object[] floorSetup = [
    new { Op = "SeedField", FieldId = fieldId, Value = new ResearchCompetenceVector(80, 70, 60), ActivityYear = 0.0 },
    new { Op = "SetNode", Value = matureNodeState },
    new { Op = "SetTacit", AssetId = "floor:asset", AssetTypeId = floorAssetType.Id,
        Scope = (int)ResearchTacitScopeKind.KnowledgeField, ScopeRef = fieldId,
        Assimilation = (int)ResearchTacitAssimilationStage.NativePractice, Depth = 50.0,
        Availability = 1.0, Translation = 1.0, Training = 1.0, Provenance = "fixture", Context = (string?)null },
];
ServiceCase("atrophy-established-and-tacit-floors", floorState, floorSetup, "ApplyCompetenceAtrophy",
    new { CurrentYear = 100.0, ElapsedYears = 100.0 }, () => { service.ApplyCompetenceAtrophy(floorState, 100, 100); return null; });

var nanYearState = NewState();
service.SeedFieldCompetence(nanYearState, fieldId, new(20, 20, 20), 0);
object[] nanYearSetup = [new { Op = "SeedField", FieldId = fieldId, Value = new ResearchCompetenceVector(20, 20, 20), ActivityYear = 0.0 }];
ServiceCase("atrophy-nan-current-year", nanYearState, nanYearSetup, "ApplyCompetenceAtrophy",
    new { CurrentYear = double.NaN, ElapsedYears = 2.0 }, () => { service.ApplyCompetenceAtrophy(nanYearState, double.NaN, 2); return null; });

var practiceUnknownState = NewState();
ServiceCase("practice-supported-unknown-node", practiceUnknownState, [], "ApplyCompletedStagePractice",
    new { NodeId = "missing-node", Stage = (int)ResearchMaturity.Experimental, ActivityYear = 1.0 },
    () => { service.ApplyCompletedStagePractice(practiceUnknownState, "missing-node", ResearchMaturity.Experimental, 1); return null; });
var practiceBypassState = NewState();
ServiceCase("practice-unsupported-unknown-node-bypasses", practiceBypassState, [], "ApplyCompletedStagePractice",
    new { NodeId = "missing-node", Stage = (int)ResearchMaturity.Rumored, ActivityYear = 1.0 },
    () => { service.ApplyCompletedStagePractice(practiceBypassState, "missing-node", ResearchMaturity.Rumored, 1); return null; });

var output = new
{
    Metadata = new
    {
        Source = "AdaptiveResearchReadiness.cs + AdaptiveResearchExpertiseRuntime.cs + AdaptiveResearchStateExtensions.cs",
        Culture = "InvariantCulture",
        ReadinessCases = readinessCases.Count,
        Commands = commands.Count,
        ServiceCases = serviceCases.Count,
        PlainNode = plainNode.Id,
        EvidenceNode = evidenceNode.Id,
        XenoscienceNode = xenoNode.Id,
        FacilityNode = facilityNode.Id,
    },
    ReadinessCases = readinessCases,
    Commands = commands,
    ServiceCases = serviceCases,
};
File.WriteAllText(args[1], JsonSerializer.Serialize(output, options) + Environment.NewLine);
Console.WriteLine($"research readiness oracle: {readinessCases.Count} readiness cases, {commands.Count} service commands, {serviceCases.Count} focused service cases");
return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine("research readiness oracle failure");
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd: {Directory.GetCurrentDirectory()}");
    Console.Error.WriteLine($"research_root: {(args.Length > 0 ? args[0] : "<missing>")}");
    Console.Error.WriteLine($"fixture: {(args.Length > 1 ? args[1] : "<missing>")}");
    return 1;
}
