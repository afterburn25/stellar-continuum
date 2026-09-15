using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
    throw new ArgumentException("Expected canonical research directory and output fixture path.");
var root = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var json = new JsonSerializerOptions {
    WriteIndented = false,
    NumberHandling = System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, json);
object Error(Exception error) => new { Type = error.GetType().Name, error.Message };
object State(AdaptiveResearchCivilizationState state) => new {
    state.CivilizationId, state.Revision, state.MaterializedViewRevision,
    state.DirectedProgramStageId, state.TotalEffectiveResearchLabs,
    state.AssignedEffectiveLabs, state.FreeEffectiveLabs,
    Nodes = state.NodeStates.Values.ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    Evidence = state.EvidenceInstances.Values.ToArray(),
    CivilizationTraits = state.CivilizationTraits.ToArray(),
    Contexts = state.ApplicabilityContexts.Select(pair => new { Id = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    Capabilities = state.Capabilities.ToArray(),
    FacilityCapabilities = state.FacilityCapabilities.ToArray(),
    DeploymentEvents = state.EnabledDeploymentEventIds.ToArray(),
    Projects = state.ActiveProjects.Values.ToArray(),
};
string Fingerprint() {
    using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
    foreach (var path in Directory.GetFiles(root, "*.json").OrderBy(path => Path.GetFileName(path), StringComparer.Ordinal)) {
        hash.AppendData(System.Text.Encoding.UTF8.GetBytes(Path.GetFileName(path)));
        hash.AppendData(File.ReadAllBytes(path));
    }
    return Convert.ToHexString(hash.GetHashAndReset());
}
var loadFingerprintBefore = Fingerprint();
var runtime = AdaptiveResearchRuntime.LoadFromDirectory(root);
var loadFingerprintAfter = Fingerprint();
object? Internal(AdaptiveResearchCivilizationState state, string name, params object?[] values) =>
    typeof(AdaptiveResearchCivilizationState).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(state, values);

var simple = runtime.Catalog.Nodes.Values.First(node =>
    node.PublicNormalResearch && node.Prerequisites.AllOf.Count == 0 && node.Prerequisites.AnyOf.Count == 0 &&
    node.Applicability.Traits.Count == 0 && node.Applicability.EvidenceTypes.Count == 0 &&
    node.CapabilityRequirements.AllOf.Count == 0 && node.CapabilityRequirements.AnyOf.Count == 0);
var secondSimple = runtime.Catalog.Nodes.Values.First(node => node.Id != simple.Id &&
    node.PublicNormalResearch && node.Prerequisites.AllOf.Count == 0 && node.Prerequisites.AnyOf.Count == 0 &&
    node.Applicability.Traits.Count == 0 && node.Applicability.EvidenceTypes.Count == 0 &&
    node.CapabilityRequirements.AllOf.Count == 0 && node.CapabilityRequirements.AnyOf.Count == 0);
var hypothesis = runtime.Catalog.Nodes.Values.First(node => node.IsHypothesis);
var pressure = runtime.Catalog.PressureIds.First();
var evidenceType = runtime.Catalog.EvidenceTypeIds.First();
var civTrait = runtime.Applicability.Traits.Values.First(t => t.Scope == ResearchApplicabilityTraitScope.Civilization).Id;
var popTrait = runtime.Applicability.Traits.Values.First(t => t.Scope == ResearchApplicabilityTraitScope.PopulationOrSpecies).Id;
var civilizationCapability = runtime.Catalog.Capabilities.Values.First(c => c.Scope == ResearchCapabilityScope.Civilization).Id;
var scopedCapability = runtime.Catalog.Capabilities.Values.First(c => c.Scope != ResearchCapabilityScope.Civilization).Id;
var facility = runtime.Facilities.FacilityCapabilityIds.First();
var allFacilities = runtime.Facilities.FacilityCapabilityIds.ToArray();
var simpleExperimentalFacilities = runtime.Facilities.GetStageRequirement(simple.Id, ResearchMaturity.Experimental);
var requiredSimpleFacility = simpleExperimentalFacilities?.AllOf.FirstOrDefault()
    ?? simpleExperimentalFacilities?.AnyOf.FirstOrDefault()
    ?? facility;
void ReadyEngineering(AdaptiveResearchCivilizationState state, string nodeId) {
    const string context = "context:grant";
    runtime.SetTotalEffectiveResearchLabs(state, 100);
    foreach (var id in allFacilities) runtime.AddFacilityCapability(state, id);
    var established = new HashSet<string>(StringComparer.Ordinal);
    void Establish(string id) {
        if (!established.Add(id)) return;
        var definition = runtime.Catalog.GetNode(id);
        foreach (var prerequisite in definition.Prerequisites.AllOf) Establish(prerequisite);
        if (definition.Prerequisites.AnyOf.Count > 0) Establish(definition.Prerequisites.AnyOf[0]);
        Internal(state, "SetNodeState", new ResearchNodeRuntimeState(id, ResearchMaturity.Mature, null, 0, definition.ProjectRequirements.BaseResearchPoints, state.Revision + 1));
    }
    var node = runtime.Catalog.GetNode(nodeId);
    foreach (var prerequisite in node.Prerequisites.AllOf) Establish(prerequisite);
    if (node.Prerequisites.AnyOf.Count > 0) Establish(node.Prerequisites.AnyOf[0]);
    foreach (var traitId in node.Applicability.Traits) {
        var trait = runtime.Applicability.GetTrait(traitId);
        if (trait.Scope == ResearchApplicabilityTraitScope.Civilization) Internal(state, "AddCivilizationTrait", traitId);
        else Internal(state, "AddApplicabilityTrait", context, traitId);
    }
    foreach (var evidenceId in node.Applicability.EvidenceTypes.Concat(node.ProjectRequirements.RequiredEvidence).Distinct(StringComparer.Ordinal))
        Internal(state, "AddEvidence", new ResearchEvidenceInstance("fixture:" + evidenceId, evidenceId, "actual-source setup", 1, 1, context, state.Revision + 1));
    foreach (var pair in node.ProjectRequirements.RequiredPressure) Internal(state, "SetPressure", pair.Key, pair.Value);
    if (node.ProjectRequirements.RequiredPressureAny.Count > 0) { var pair = node.ProjectRequirements.RequiredPressureAny.First(); Internal(state, "SetPressure", pair.Key, pair.Value); }
    foreach (var capabilityId in node.CapabilityRequirements.AllOf.Concat(node.CapabilityRequirements.AnyOf.Take(1))) {
        var capability = runtime.Catalog.Capabilities[capabilityId];
        Internal(state, "AddCapability", capabilityId, capability.Scope == ResearchCapabilityScope.Civilization ? null : context);
    }
    var stageWork = runtime.ProgressPolicy.GetStageWork(node, ResearchMaturity.Engineering);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(nodeId, ResearchMaturity.Engineering, null, Math.Max(0, stageWork - 1), node.ProjectRequirements.BaseResearchPoints - 1, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(nodeId, ResearchMaturity.Engineering, context, Math.Max(node.ProjectRequirements.MinimumLabs, node.ProjectRequirements.RecommendedLabs), 1, false, null, Math.Max(0, stageWork - 1), node.ProjectRequirements.BaseResearchPoints - 1, state.Revision + 1));
}

var scenarios = new List<(string Name, Action<AdaptiveResearchCivilizationState> Setup, object[] SetupDescription, (string Op, object[] Args)[] Commands)>();
void Add(string name, Action<AdaptiveResearchCivilizationState> setup, object[] setupDescription, params (string, object[])[] commands) =>
    scenarios.Add((name, setup, setupDescription, commands));
Action<AdaptiveResearchCivilizationState> BasicSetup = state => {
    runtime.SetTotalEffectiveResearchLabs(state, 100);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(simple.Id, ResearchMaturity.Investigable, null, 0, 7.5, state.Revision + 1));
    foreach (var id in allFacilities) runtime.AddFacilityCapability(state, id);
};
var basicSetupDescription = new object[] {
    new { Op = "Labs", Value = 100.0 },
    new { Op = "Node", NodeId = simple.Id, Maturity = ResearchMaturity.Investigable, Resolution = (string?)null, StageRp = 0.0, TotalRp = 7.5 },
    new { Op = "AllFacilities" },
};
Add("project-lifecycle-and-multistage", BasicSetup, basicSetupDescription,
    ("Start", new object[] { simple.Id, (double)simple.ProjectRequirements.MinimumLabs, 60.0, null! }),
    ("Pause", new object[] { simple.Id }), ("Pause", new object[] { simple.Id }),
    ("Resume", new object[] { simple.Id, (double)simple.ProjectRequirements.MinimumLabs, 80.0 }),
    ("Reallocate", new object[] { simple.Id, (double)simple.ProjectRequirements.RecommendedLabs }),
    ("Reallocate", new object[] { simple.Id, simple.ProjectRequirements.MinimumLabs + 0.25 }),
    ("Readiness", new object[] { simple.Id, 100.0 }),
    ("Advance", new object[] { 1000000.0 }));
Add("changed-facility-pauses-before-spend", BasicSetup, basicSetupDescription,
    ("Start", new object[] { simple.Id, (double)simple.ProjectRequirements.MinimumLabs, 60.0, null! }),
    ("RemoveFacility", new object[] { requiredSimpleFacility }),
    ("Advance", new object[] { 1.0 }));
Action<AdaptiveResearchCivilizationState> PausedCapacitySetup = state => {
    runtime.SetTotalEffectiveResearchLabs(state, 100);
    foreach (var id in allFacilities) runtime.AddFacilityCapability(state, id);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(simple.Id, ResearchMaturity.Experimental, null, 0, 0, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(simple.Id, ResearchMaturity.Experimental, null, 1, 1, true, "paused_by_order", 0, 0, state.Revision + 1));
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(secondSimple.Id, ResearchMaturity.Experimental, null, 0, 0, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(secondSimple.Id, ResearchMaturity.Experimental, null, 1, 1, false, null, 0, 0, state.Revision + 1));
};
var pausedCapacityDescription = new object[] {
    new { Op = "Labs", Value = 100.0 }, new { Op = "AllFacilities" },
    new { Op = "Node", NodeId = simple.Id, Maturity = ResearchMaturity.Experimental, Resolution = (string?)null, StageRp = 0.0, TotalRp = 0.0 },
    new { Op = "Project", NodeId = simple.Id, Stage = ResearchMaturity.Experimental, Context = (string?)null, Labs = 1.0, Readiness = 1.0, Paused = true, Reason = "paused_by_order", StageRp = 0.0, TotalRp = 0.0 },
    new { Op = "Node", NodeId = secondSimple.Id, Maturity = ResearchMaturity.Experimental, Resolution = (string?)null, StageRp = 0.0, TotalRp = 0.0 },
    new { Op = "Project", NodeId = secondSimple.Id, Stage = ResearchMaturity.Experimental, Context = (string?)null, Labs = 1.0, Readiness = 1.0, Paused = false, Reason = (string?)null, StageRp = 0.0, TotalRp = 0.0 },
};
Add("paused-project-does-not-consume-labs-but-capacity-blocks-resume", PausedCapacitySetup, pausedCapacityDescription,
    ("Resume", new object[] { simple.Id, (double)simple.ProjectRequirements.MinimumLabs, 60.0 }));
Action<AdaptiveResearchCivilizationState> WideAllocationSetup = state => {
    runtime.SetTotalEffectiveResearchLabs(state, double.MaxValue);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(simple.Id, ResearchMaturity.Experimental, null, 0, 0, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(simple.Id, ResearchMaturity.Experimental, null, simple.ProjectRequirements.MinimumLabs, 1, false, null, 0, 0, state.Revision + 1));
};
var wideAllocationDescription = new object[] {
    new { Op = "Labs", Value = double.MaxValue },
    new { Op = "Node", NodeId = simple.Id, Maturity = ResearchMaturity.Experimental, Resolution = (string?)null, StageRp = 0.0, TotalRp = 0.0 },
    new { Op = "Project", NodeId = simple.Id, Stage = ResearchMaturity.Experimental, Context = (string?)null, Labs = (double)simple.ProjectRequirements.MinimumLabs, Readiness = 1.0, Paused = false, Reason = (string?)null, StageRp = 0.0, TotalRp = 0.0 },
};
Add("reallocation-source-number-formatting", WideAllocationSetup, wideAllocationDescription,
    ("Reallocate", new object[] { simple.Id, 1e300 }),
    ("Reallocate", new object[] { simple.Id, simple.ProjectRequirements.MinimumLabs + 0.125 }),
    ("Reallocate", new object[] { simple.Id, simple.ProjectRequirements.MinimumLabs + 0.135 }),
    ("Reallocate", new object[] { simple.Id, 1.005 }),
    ("Reallocate", new object[] { simple.Id, 2.675 }),
    ("Reallocate", new object[] { simple.Id, 1.2345678901234567e20 }),
    ("Reallocate", new object[] { simple.Id, double.NaN }),
    ("Reallocate", new object[] { simple.Id, double.PositiveInfinity }),
    ("Reallocate", new object[] { simple.Id, double.NegativeInfinity }));
Add("pressure-change-noop-remove-and-error", _ => { }, Array.Empty<object>(),
    ("Pressure", new object[] { pressure, 20.0, null! }),
    ("Pressure", new object[] { pressure, 20.0, null! }),
    ("Pressure", new object[] { pressure, 0.0, null! }),
    ("Pressure", new object[] { "unknown-pressure", 1.0, null! }));
Add("evidence-add-duplicate-and-errors", _ => { }, Array.Empty<object>(),
    ("Evidence", new object[] { "e:1", evidenceType, "actual lab", .8, .7, null! }),
    ("Evidence", new object[] { "e:1", evidenceType, "duplicate", .1, .1, null! }),
    ("Evidence", new object[] { "e:2", "unknown-evidence", "lab", .5, .5, null! }),
    ("Evidence", new object[] { "e:3", evidenceType, "lab", -1.0, .5, null! }),
    ("Evidence", new object[] { "e:4", evidenceType, "lab", .5, double.PositiveInfinity, null! }));
Add("traits-context-and-validation", _ => { }, Array.Empty<object>(),
    ("CivilizationTrait", new object[] { civTrait }),
    ("CivilizationTrait", new object[] { civTrait }),
    ("CivilizationTrait", new object[] { "unknown-trait" }),
    ("CivilizationTrait", new object[] { popTrait }),
    ("ContextTraits", new object[] { "species:one", new[] { popTrait, popTrait } }),
    ("ContextTraits", new object[] { "species:one", new[] { popTrait } }),
    ("ContextTraits", new object[] { "species:unknown", new[] { "unknown-trait" } }),
    ("ContextTraits", new object[] { "species:bad", new[] { civTrait } }));
Add("capability-repeat-context-errors", _ => { }, Array.Empty<object>(),
    ("Capability", new object[] { civilizationCapability, null! }),
    ("Capability", new object[] { civilizationCapability, null! }),
    ("Capability", new object[] { civilizationCapability, "wrong-context" }),
    ("Capability", new object[] { scopedCapability, null! }),
    ("Capability", new object[] { scopedCapability, "species:one" }));
Add("facility-add-remove-and-error", _ => { }, Array.Empty<object>(),
    ("AddFacility", new object[] { facility }), ("AddFacility", new object[] { facility }),
    ("RemoveFacility", new object[] { facility }), ("RemoveFacility", new object[] { facility }),
    ("AddFacility", new object[] { "unknown-facility" }));
Add("start-rejections", _ => { }, Array.Empty<object>(),
    ("Start", new object[] { simple.Id, 1.0, 60.0, null! }),
    ("Start", new object[] { "unknown-node", 1.0, 60.0, null! }));
Add("advance-validation-zero", _ => { }, Array.Empty<object>(),
    ("Advance", new object[] { 0.0 }), ("Advance", new object[] { -1.0 }),
    ("Advance", new object[] { double.NaN }), ("Advance", new object[] { double.PositiveInfinity }));
Action<AdaptiveResearchCivilizationState> HypothesisSetup(bool supported) => state => {
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(hypothesis.Id, ResearchMaturity.Experimental, null, 10, 15, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(hypothesis.Id, ResearchMaturity.Experimental, null, 2, 1, true, "hypothesis_resolution_required", 10, 15, state.Revision + 1));
};
var hypothesisSetup = new object[] {
    new { Op = "Node", NodeId = hypothesis.Id, Maturity = ResearchMaturity.Experimental, Resolution = (string?)null, StageRp = 10.0, TotalRp = 15.0 },
    new { Op = "Project", NodeId = hypothesis.Id, Stage = ResearchMaturity.Experimental, Context = (string?)null, Labs = 2.0, Readiness = 1.0, Paused = true, Reason = "hypothesis_resolution_required", StageRp = 10.0, TotalRp = 15.0 },
};
Add("hypothesis-disproved", HypothesisSetup(false), hypothesisSetup, ("Resolve", new object[] { hypothesis.Id, false }));
Add("hypothesis-supported", HypothesisSetup(true), hypothesisSetup, ("Resolve", new object[] { hypothesis.Id, true }));
Add("review-bounded-order-duplicates", _ => { }, Array.Empty<object>(),
    ("Review", new object[] { new[] { simple.Id, simple.Id }, null! }));
foreach (var grantNodeId in new[] { "biofabrication", "coordinated_research_networks", "synthetic_cognition" }) {
    var capturedId = grantNodeId;
    Add("mature-special-grant-" + capturedId,
        state => ReadyEngineering(state, capturedId),
        new object[] { new { Op = "ReadyEngineering", NodeId = capturedId } },
        ("Advance", new object[] { 1.0 }));
}
var hypothesisExperimentalWork = runtime.ProgressPolicy.GetStageWork(hypothesis, ResearchMaturity.Experimental);
Action<AdaptiveResearchCivilizationState> HypothesisBoundarySetup = state => {
    ReadyEngineering(state, hypothesis.Id);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(hypothesis.Id, ResearchMaturity.Experimental, null, Math.Max(0, hypothesisExperimentalWork - 1), hypothesis.ProjectRequirements.BaseResearchPoints - 1, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(hypothesis.Id, ResearchMaturity.Experimental, "context:grant", Math.Max(hypothesis.ProjectRequirements.MinimumLabs, hypothesis.ProjectRequirements.RecommendedLabs), 1, false, null, Math.Max(0, hypothesisExperimentalWork - 1), hypothesis.ProjectRequirements.BaseResearchPoints - 1, state.Revision + 1));
};
Add("hypothesis-advance-requires-resolution", HypothesisBoundarySetup,
    new object[] {
        new { Op = "ReadyEngineering", NodeId = hypothesis.Id },
        new { Op = "Node", NodeId = hypothesis.Id, Maturity = ResearchMaturity.Experimental, Resolution = (string?)null, StageRp = Math.Max(0, hypothesisExperimentalWork - 1), TotalRp = hypothesis.ProjectRequirements.BaseResearchPoints - 1 },
        new { Op = "Project", NodeId = hypothesis.Id, Stage = ResearchMaturity.Experimental, Context = "context:grant", Labs = (double)Math.Max(hypothesis.ProjectRequirements.MinimumLabs, hypothesis.ProjectRequirements.RecommendedLabs), Readiness = 1.0, Paused = false, Reason = (string?)null, StageRp = Math.Max(0, hypothesisExperimentalWork - 1), TotalRp = hypothesis.ProjectRequirements.BaseResearchPoints - 1 },
    },
    ("Advance", new object[] { 1.0 }));
Action<AdaptiveResearchCivilizationState> NonFiniteProgressSetup = state => {
    ReadyEngineering(state, simple.Id);
    Internal(state, "SetNodeState", new ResearchNodeRuntimeState(simple.Id, ResearchMaturity.Engineering, null, double.NaN, 0, state.Revision + 1));
    Internal(state, "SetProject", new ResearchProjectRuntimeState(simple.Id, ResearchMaturity.Engineering, "context:grant", simple.ProjectRequirements.RecommendedLabs, 1, false, null, double.NaN, 0, state.Revision + 1));
};
Add("advance-source-nan-stage-progress", NonFiniteProgressSetup,
    new object[] {
        new { Op = "ReadyEngineering", NodeId = simple.Id },
        new { Op = "Node", NodeId = simple.Id, Maturity = ResearchMaturity.Engineering, Resolution = (string?)null, StageRp = double.NaN, TotalRp = 0.0 },
        new { Op = "Project", NodeId = simple.Id, Stage = ResearchMaturity.Engineering, Context = "context:grant", Labs = (double)simple.ProjectRequirements.RecommendedLabs, Readiness = 1.0, Paused = false, Reason = (string?)null, StageRp = double.NaN, TotalRp = 0.0 },
    },
    ("Advance", new object[] { 1.0 }));
var simpleEngineeringWork = runtime.ProgressPolicy.GetStageWork(simple, ResearchMaturity.Engineering);
Action<AdaptiveResearchCivilizationState> OverflowBudgetSetup = state => {
    ReadyEngineering(state, simple.Id);
    runtime.SetTotalEffectiveResearchLabs(state, double.MaxValue);
    Internal(state, "SetProject", new ResearchProjectRuntimeState(simple.Id, ResearchMaturity.Engineering, "context:grant", double.MaxValue, 1, false, null, Math.Max(0, simpleEngineeringWork - 1), simple.ProjectRequirements.BaseResearchPoints - 1, state.Revision + 1));
};
Add("advance-overflow-derived-budget", OverflowBudgetSetup,
    new object[] {
        new { Op = "ReadyEngineering", NodeId = simple.Id },
        new { Op = "Labs", Value = double.MaxValue },
        new { Op = "Project", NodeId = simple.Id, Stage = ResearchMaturity.Engineering, Context = "context:grant", Labs = double.MaxValue, Readiness = 1.0, Paused = false, Reason = (string?)null, StageRp = Math.Max(0, simpleEngineeringWork - 1), TotalRp = simple.ProjectRequirements.BaseResearchPoints - 1 },
    },
    ("Advance", new object[] { double.MaxValue }));

Func<object> Prepare(string op, object[] values, AdaptiveResearchCivilizationState state) {
    if (op == "Start") { var id=(string)values[0];var labs=(double)values[1];var ready=(double)values[2];var context=(string?)values[3];return ()=>runtime.StartDirectedResearch(state,id,labs,ready,context); }
    if (op == "Pause") { var id=(string)values[0];return ()=>runtime.PauseDirectedResearch(state,id); }
    if (op == "Resume") { var id=(string)values[0];var labs=(double)values[1];var ready=(double)values[2];return ()=>runtime.ResumeDirectedResearch(state,id,labs,ready); }
    if (op == "Reallocate") { var id=(string)values[0];var labs=(double)values[1];return ()=>runtime.ReallocateResearchLabs(state,id,labs); }
    if (op == "Readiness") { var id=(string)values[0];var ready=(double)values[1];return ()=>runtime.SetProjectReadiness(state,id,ready); }
    if (op == "Advance") { var years=(double)values[0];return ()=>runtime.AdvanceProjects(state,years); }
    if (op == "Pressure") { var id=(string)values[0];var value=(double)values[1];var context=(string?)values[2];return ()=>runtime.SetPressure(state,id,value,context); }
    if (op == "Evidence") { var id=(string)values[0];var type=(string)values[1];var provenance=(string)values[2];var quality=(double)values[3];var confidence=(double)values[4];var context=(string?)values[5];return ()=>runtime.AddEvidence(state,id,type,provenance,quality,confidence,context); }
    if (op == "CivilizationTrait") { var id=(string)values[0];return ()=>runtime.AddCivilizationTrait(state,id); }
    if (op == "ContextTraits") { var id=(string)values[0];var traits=(string[])values[1];return ()=>runtime.SetApplicabilityContextTraits(state,id,traits); }
    if (op == "Capability") { var id=(string)values[0];var context=(string?)values[1];return ()=>runtime.AddCapability(state,id,context); }
    if (op == "AddFacility") { var id=(string)values[0];return ()=>InvokeVoid(()=>runtime.AddFacilityCapability(state,id)); }
    if (op == "RemoveFacility") { var id=(string)values[0];return ()=>InvokeVoid(()=>runtime.RemoveFacilityCapability(state,id)); }
    if (op == "Resolve") { var id=(string)values[0];var supported=(bool)values[1];return ()=>runtime.ResolveHypothesis(state,id,supported); }
    if (op == "Review") { var ids=(string[])values[0];var context=(string?)values[1];return ()=>runtime.ReviewBasicScienceCandidates(state,ids,context); }
    throw new InvalidOperationException($"Unknown fixture operation '{op}'.");
}
object InvokeVoid(Action action) { action(); return new { Void = true }; }

var records = new List<object>();
foreach (var scenario in scenarios) {
    var state = runtime.CreateCivilizationState("civ:" + scenario.Name);
    scenario.Setup(state); // Approved writer-equivalent setup remains outside operation catches.
    var outcomes = new List<object>();
    foreach (var command in scenario.Commands) {
        var before = Freeze(State(state));
        var fingerprintBefore = Fingerprint();
        var operation = Prepare(command.Op, command.Args, state);
        object? result = null; Exception? failure = null;
        try { result = operation(); }
        catch (Exception error) { failure = error; }
        var fingerprintAfter = Fingerprint();
        outcomes.Add(new {
            command.Op, Args = Freeze(command.Args), Before = before,
            After = Freeze(State(state)),
            Result = failure is null ? Freeze(result) : (JsonElement?)null,
            Error = failure is null ? (JsonElement?)null : Freeze(Error(failure)),
            FingerprintBefore = fingerprintBefore, FingerprintAfter = fingerprintAfter,
        });
    }
    records.Add(new { scenario.Name, Setup = scenario.SetupDescription, Outcomes = outcomes, FinalState = Freeze(State(state)) });
}
Directory.CreateDirectory(Path.GetDirectoryName(output)!);
File.WriteAllText(output, JsonSerializer.Serialize(new {
    Schema = "stellar-adaptive-research-runtime-oracle-v1",
    runtime.Catalog.Metadata.CatalogId,
    Ids = new { SimpleNode = simple.Id, SecondSimpleNode = secondSimple.Id, HypothesisNode = hypothesis.Id, Pressure = pressure, EvidenceType = evidenceType, CivilizationTrait = civTrait, PopulationTrait = popTrait, CivilizationCapability = civilizationCapability, ScopedCapability = scopedCapability, Facility = facility, RequiredSimpleFacility = requiredSimpleFacility },
    Records = records,
    Metadata = new { Source = "actual retained C# AdaptiveResearchRuntime", Culture = "InvariantCulture", Scope = "runtime kernel only; no funding, campaign envelope, sidecars, deployment bridge, or save-v16 claim", LoadFingerprintBefore = loadFingerprintBefore, LoadFingerprintAfter = loadFingerprintAfter },
}, json) + Environment.NewLine);
