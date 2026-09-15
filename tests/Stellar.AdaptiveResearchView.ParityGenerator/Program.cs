using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchViewOracle <research-data> <fixture>");
    return 1;
}

var options = new JsonSerializerOptions
{
    WriteIndented = false,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);

var rootPath = Path.GetFullPath(args[0]);
var catalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(rootPath);
var applicability = AdaptiveResearchApplicabilityCatalog.LoadFromDirectory(rootPath, catalog);
var facilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(rootPath, catalog);
var progress = AdaptiveResearchProgressPolicy.LoadFromDirectory(rootPath, catalog);
var eligibility = new AdaptiveResearchEligibilityEvaluator(catalog, applicability, facilities);
var viewBuilder = new AdaptiveResearchViewBuilder(catalog, eligibility, progress);
var methodCache = typeof(AdaptiveResearchCivilizationState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .GroupBy(method => (method.Name, method.GetParameters().Length))
    .ToDictionary(group => group.Key, group => group.Single());

object Snapshot(AdaptiveResearchCivilizationState state) => new
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
    ApplicabilityContexts = state.ApplicabilityContexts
        .Select(pair => new { ContextId = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    ActiveProjects = state.ActiveProjects.Values.ToArray(),
};

var cases = new List<object>();
void Case(string name, StateBuilder state, string? defaultContext = null, bool contextPresent = false)
{
    var input = Freeze(new
    {
        DefaultContextPresent = contextPresent,
        DefaultContext = defaultContext,
        Setup = state.Setup,
        State = Snapshot(state.State),
    });
    var before = Freeze(Snapshot(state.State));
    AdaptiveResearchView? result = null;
    Exception? error = null;
    try
    {
        result = viewBuilder.Build(state.State, contextPresent ? defaultContext : null);
    }
    catch (Exception caught)
    {
        error = caught;
    }
    var frozenResult = result is null ? (JsonElement?)null : Freeze(result);
    var frozenError = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message });
    cases.Add(new
    {
        Name = name,
        Input = input,
        Result = frozenResult,
        Error = frozenError,
        Before = before,
        After = Freeze(Snapshot(state.State)),
    });
}

var empty = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:empty");
Case("empty", empty);

var publicNodes = catalog.Nodes.Values.Where(node => node.PublicNormalResearch).ToArray();
var mixed = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:mixed");
var maturities = new[]
{
    ResearchMaturity.Rumored, ResearchMaturity.Hypothesized, ResearchMaturity.Investigable,
    ResearchMaturity.Experimental, ResearchMaturity.Demonstrated, ResearchMaturity.Engineering,
    ResearchMaturity.Mature, ResearchMaturity.Archived,
};
for (var index = 0; index < maturities.Length; ++index)
    mixed.Node(publicNodes[index].Id, maturities[index], maturities[index] >= ResearchMaturity.Mature ? "Established" : null);
mixed.Labs(19.5);
Case("mixed-maturities", mixed, "context:default", contextPresent: true);

var allEdge = publicNodes.First(node => node.Prerequisites.AllOf.Count > 0);
var anyEdge = publicNodes.First(node => node.Prerequisites.AnyOf.Count > 0);
var edges = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:edges");
edges.Node(allEdge.Id, ResearchMaturity.Investigable);
foreach (var id in allEdge.Prerequisites.AllOf)
    edges.Node(id, ResearchMaturity.Mature, "Established");
edges.Node(anyEdge.Id, ResearchMaturity.Investigable);
foreach (var id in anyEdge.Prerequisites.AnyOf)
    edges.Node(id, ResearchMaturity.Mature, "Established");
Case("visible-edge-order", edges);

var hiddenPrerequisiteNode = publicNodes.First(node => node.Prerequisites.AllOf.Count > 0);
var privacy = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:privacy");
privacy.Node(hiddenPrerequisiteNode.Id, ResearchMaturity.Hypothesized);
foreach (var pressure in hiddenPrerequisiteNode.PressureAffinities)
    privacy.Pressure(pressure, pressure == hiddenPrerequisiteNode.PressureAffinities[0] ? double.NaN : 0.0);
privacy.Pressure("pressure:hidden-unrelated", 99.0);
Case("hidden-prerequisite-and-pressure-privacy", privacy);

var pressureNode = publicNodes.First(node =>
    node.ProjectRequirements.RequiredPressure.Count > 0 ||
    node.ProjectRequirements.RequiredPressureAny.Count > 0);
var pressureState = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:pressures");
pressureState.Node(pressureNode.Id, ResearchMaturity.Investigable);
foreach (var id in pressureNode.PressureAffinities)
    pressureState.Pressure(id, id == pressureNode.PressureAffinities[0] ? 0.0 : 2.5);
foreach (var pair in pressureNode.ProjectRequirements.RequiredPressure)
    pressureState.Pressure(pair.Key, pair.Key.Contains("temperature", StringComparison.Ordinal) ? double.NaN : pair.Value);
foreach (var pair in pressureNode.ProjectRequirements.RequiredPressureAny)
    pressureState.Pressure(pair.Key, pair.Value);
pressureState.Pressure("pressure:not-visible", double.PositiveInfinity);
Case("recognized-pressure-hard-targets", pressureState);

var projectNodes = publicNodes.Where(node => node.Prerequisites.AllOf.Count == 0 && node.Prerequisites.AnyOf.Count == 0).Take(3).ToArray();
var projects = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:projects");
projects.Labs(30.0);
for (var index = 0; index < projectNodes.Length; ++index)
{
    var stage = (ResearchMaturity)((int)ResearchMaturity.Experimental + index);
    projects.Node(projectNodes[index].Id, stage);
    projects.Project(new ResearchProjectRuntimeState(
        projectNodes[index].Id, stage, index == 0 ? null : index == 1 ? "" : "context:project",
        index == 0 ? 3.5 : index == 1 ? 7.0 : 2.0,
        index == 0 ? 0.350001 : index == 1 ? 0.750001 : 1.000002,
        index == 1, index == 1 ? "fixture pause" : null,
        index == 0 ? -1.0 : index == 1 ? double.PositiveInfinity : 1.0,
        index == 0 ? double.NaN : index == 1 ? double.NegativeInfinity : 1e300, 0));
}
Case("projects-context-progress-capacity", projects, "context:fallback", contextPresent: true);

var activeBlockedNode = publicNodes.First(node =>
    node.Prerequisites.AllOf.Count > 0 &&
    facilities.GetStageRequirement(node.Id, ResearchMaturity.Experimental) is not null);
var activeBlocked = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:active-blockers");
activeBlocked.Labs(0.0);
activeBlocked.Node(activeBlockedNode.Id, ResearchMaturity.Experimental);
activeBlocked.Project(new ResearchProjectRuntimeState(
    activeBlockedNode.Id, ResearchMaturity.Experimental, null, 0.0, 0.5,
    false, null, 0.0, 0.0, 0));
Case("active-scientific-and-facility-blockers-only", activeBlocked);

var readinessValues = new[] { 0.35, 0.350002, 0.55, 0.550002, 0.75, 0.750002, 1.0, 1.000002, double.NaN };
for (var index = 0; index < readinessValues.Length; ++index)
{
    var node = projectNodes[index % projectNodes.Length];
    var state = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, $"civ:readiness:{index}");
    state.Node(node.Id, ResearchMaturity.Experimental);
    state.Project(new ResearchProjectRuntimeState(node.Id, ResearchMaturity.Experimental, null, 1.0,
        readinessValues[index], false, null, index == 0 ? 0.0 : 1.0, index == 8 ? double.NaN : 1.0, 0));
    Case($"readiness:{index}", state, "fallback", contextPresent: true);
}

var unknown = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:unknown-node");
unknown.Node("node:unknown", ResearchMaturity.Investigable);
unknown.Stage("stage:unknown");
Case("unknown-node-precedes-invalid-stage", unknown);

var twoUnknown = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:two-unknown");
twoUnknown.Node("node:first-unknown", ResearchMaturity.Investigable);
twoUnknown.Node("node:second-unknown", ResearchMaturity.Investigable);
Case("first-unknown-in-state-enumeration", twoUnknown);

var laterUnknown = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:later-unknown");
laterUnknown.Node(publicNodes[0].Id, ResearchMaturity.Mature, "Established");
laterUnknown.Node("node:later-unknown", ResearchMaturity.Investigable);
laterUnknown.Stage("stage:unknown");
Case("later-unknown-precedes-invalid-stage", laterUnknown);

var invalidStage = new StateBuilder(catalog.Metadata.StartingDirectedProgramStageId, methodCache, "civ:invalid-stage");
invalidStage.Node(publicNodes[0].Id, ResearchMaturity.Mature, "Established");
invalidStage.Stage("stage:unknown");
Case("invalid-directed-stage-built-last", invalidStage);

var sourceOnly = new List<object>();
Exception? nullError = null;
try
{
    viewBuilder.Build(null!);
}
catch (Exception caught)
{
    nullError = caught;
}
sourceOnly.Add(new
{
    Name = "null-state",
    NativeParity = false,
    Reason = "C# reference-null boundary has no C++ reference equivalent.",
    Error = nullError is null ? null : new { Type = nullError.GetType().Name, nullError.Message },
});

var output = new
{
    Schema = "stellar-adaptive-research-view-v1",
    Culture = "InvariantCulture",
    CatalogId = catalog.Metadata.CatalogId,
    CanonicalNodeCount = catalog.Nodes.Count,
    Coverage = new
    {
        AllMaturities = true,
        HiddenStatePrivacy = true,
        AllOfAndAnyOfEdges = true,
        NullEmptyAndFallbackContexts = true,
        PausedCapacity = true,
        ActiveScientificAndFacilityBlockersOnly = true,
        StoredZeroAndNamedNonfiniteNumbers = true,
        UnknownNodeBeforeInvalidStage = true,
        MultipleUnknownNodesUseStateEnumerationOrder = true,
        CapacityBuiltLast = true,
    },
    Cases = cases,
    SourceOnly = sourceOnly,
};
File.WriteAllText(args[1], JsonSerializer.Serialize(output, options) + Environment.NewLine);
Console.WriteLine($"research view oracle: {cases.Count} native cases; {sourceOnly.Count} source-only null case");
return 0;

sealed class StateBuilder
{
    private readonly Dictionary<(string, int), MethodInfo> methods;
    public AdaptiveResearchCivilizationState State { get; }
    public List<object> Setup { get; } = new();

    public StateBuilder(string stageId, Dictionary<(string, int), MethodInfo> methods, string civilizationId)
    {
        this.methods = methods;
        State = new AdaptiveResearchCivilizationState(civilizationId, stageId);
    }

    private void Invoke(string op, object input, string method, params object?[] values)
    {
        Setup.Add(new { Op = op, Input = input });
        try
        {
            methods[(method, values.Length)].Invoke(State, values);
        }
        catch (TargetInvocationException error)
        {
            throw error.InnerException ?? error;
        }
    }

    public void Labs(double value) => Invoke("SetLabs", new { Value = value }, "SetTotalEffectiveResearchLabs", value);
    public void Pressure(string id, double value) => Invoke("SetPressure", new { Id = id, Value = value }, "SetPressure", id, value);
    public void Node(string id, ResearchMaturity maturity, string? resolution = null) =>
        Invoke("SetNode", new ResearchNodeRuntimeState(id, maturity, resolution, 0.0, 0.0, 0), "SetNodeState",
            new ResearchNodeRuntimeState(id, maturity, resolution, 0.0, 0.0, 0));
    public void Stage(string id) => Invoke("SetStage", new { Id = id }, "SetDirectedProgramStage", id);
    public void Project(ResearchProjectRuntimeState project) => Invoke("SetProject", project, "SetProject", project);
}
