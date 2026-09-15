using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using System.Security.Cryptography;
using Game.Simulation.Research.Adaptive;

System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;

if (args.Length != 2)
    throw new ArgumentException("usage: AdaptiveCatalogOracle <research-directory> <output>");

var root = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var cases = new List<object>();
using var scratch = new OwnedScratchDirectory();
var options = new JsonSerializerOptions
{
    WriteIndented = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals
};

RunLoad("canonical-full-catalog", root);
foreach (var nodeId in new[]
{
    "systems_engineering", "prototype_warp_drive", "alien_signal_analysis",
    "coordinated_research_networks", "field_defense_hypothesis",
    "self_sustaining_colony_biosphere", "cross_biochemistry_biofabrication"
})
    RunNodeLookup($"node-{nodeId}", root, nodeId);
RunNodeLookup("node-case-sensitive-miss", root, "Prototype_Warp_Drive");
RunNodeLookup("node-unknown", root, "missing_node");

foreach (var stageId in new[]
{
    "single_priority_program", "coordinated_research_networks",
    "distributed_scientific_portfolios", "autonomous_research_portfolios"
})
    RunStageLookup($"stage-{stageId}", root, stageId);
RunStageLookup("stage-case-sensitive-miss", root, "Single_Directed_Program");
RunStageLookup("stage-unknown", root, "missing_stage");

foreach (var values in new (string Name, double Assigned, double Recommended)[]
{
    ("scale-zero-assigned", 0, 8), ("scale-negative-assigned", -1, 8),
    ("scale-zero-recommended", 4, 0), ("scale-negative-recommended", 4, -1),
    ("scale-below", 3, 8), ("scale-at-recommended", 8, 8),
    ("scale-between", 12, 8), ("scale-at-twice", 16, 8),
    ("scale-above-twice", 24, 8), ("scale-fractional", 2.5, 1.25),
    ("scale-nan-assigned", double.NaN, 8),
    ("scale-positive-infinity", double.PositiveInfinity, 8),
    ("scale-negative-infinity", double.NegativeInfinity, 8),
})
    RunScale(values.Name, root, values.Assigned, values.Recommended);

var catalogMismatch = JsonSerializer.Serialize("wrong.catalog");
var mutations = new (string Name, Mutation[] Mutations)[]
{
    ("round-half-to-even-down", [Set("research_economy.json", "/complexity_defaults/foundation/base_research_points", "12.5")]),
    ("round-half-to-even-up", [Set("research_economy.json", "/complexity_defaults/foundation/base_research_points", "13.5")]),
    ("capability-context-null-defaults-civilization", [Set("foundations.json", "/nodes/0/capability_requirements", "{\"all_of\":[],\"any_of\":[],\"context\":null}")]),
    ("optional-node-arrays-null-default-empty", [Set("foundations.json", "/nodes/0/knowledge_fields", "null"), Set("foundations.json", "/nodes/0/awareness_sources", "null"), Set("foundations.json", "/nodes/0/pressure_affinities", "null"), Set("foundations.json", "/nodes/0/capabilities", "null")]),
    ("required-int32-min", [Set("index.json", "/schema_version", "-2147483648")]),
    ("required-int32-max", [Set("index.json", "/schema_version", "2147483647")]),
    ("required-int32-below-min", [Set("index.json", "/schema_version", "-2147483649")]),
    ("required-int32-above-max", [Set("index.json", "/schema_version", "2147483648")]),
    ("required-int32-unsigned-above-int64", [Set("index.json", "/schema_version", "9223372036854775808")]),
    ("required-int32-fractional", [Set("index.json", "/schema_version", "1.5")]),
    ("optional-int32-null", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "null")]),
    ("optional-int32-min", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "-2147483648")]),
    ("optional-int32-max", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "2147483647")]),
    ("optional-int32-below-min", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "-2147483649")]),
    ("optional-int32-above-max", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "2147483648")]),
    ("optional-int32-fractional", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "1.5")]),
    ("optional-int32-wrong-type", [Set("research_economy.json", "/directed_program_concurrency/progression/0/directed_program_limit", "\"one\"")]),
    ("missing-index", [Delete("index.json")]),
    ("missing-pressure", [Delete("pressure_dynamics.json")]),
    ("missing-traits", [Delete("applicability_traits.json")]),
    ("missing-evidence", [Delete("evidence_types.json")]),
    ("missing-fields", [Delete("knowledge_fields.json")]),
    ("missing-capabilities", [Delete("capability_model.json")]),
    ("missing-economy", [Delete("research_economy.json")]),
    ("missing-grants", [Delete("technology_grants.json")]),
    ("malformed-index", [Raw("index.json", "{")]),
    ("malformed-pressure", [Raw("pressure_dynamics.json", "[")]),
    ("malformed-domain", [Raw("foundations.json", "not-json")]),
    ("malformed-economy", [Raw("research_economy.json", "{")]),
    ("malformed-grants", [Raw("technology_grants.json", "{")]),
    ("index-missing-catalog-id", [Remove("index.json", "/catalog_id")]),
    ("index-missing-schema-version", [Remove("index.json", "/schema_version")]),
    ("index-missing-node-count", [Remove("index.json", "/node_count")]),
    ("index-missing-domain-files", [Remove("index.json", "/domain_files")]),
    ("index-missing-domains", [Remove("index.json", "/domains")]),
    ("index-domain-files-wrong-type", [Set("index.json", "/domain_files", "[]")]),
    ("index-catalog-id-wrong-type", [Set("index.json", "/catalog_id", "7")]),
    ("index-schema-version-wrong-type", [Set("index.json", "/schema_version", "\"one\"")]),
    ("index-node-count-wrong-type", [Set("index.json", "/node_count", "1.5")]),
    ("index-domain-file-null", [Set("index.json", "/domain_files/foundations", "null")]),
    ("index-domains-wrong-type", [Set("index.json", "/domains", "{}")]),
    ("domain-row-count-mismatch", [Remove("index.json", "/domains/20")]),
    ("domain-mapping-missing", [Set("index.json", "/domains/0/id", "\"missing_domain\"")]),
    ("pressure-catalog-mismatch", [Set("pressure_dynamics.json", "/catalog_id", catalogMismatch)]),
    ("traits-catalog-mismatch", [Set("applicability_traits.json", "/catalog_id", catalogMismatch)]),
    ("evidence-catalog-mismatch", [Set("evidence_types.json", "/catalog_id", catalogMismatch)]),
    ("fields-catalog-mismatch", [Set("knowledge_fields.json", "/catalog_id", catalogMismatch)]),
    ("capability-catalog-mismatch", [Set("capability_model.json", "/catalog_id", catalogMismatch)]),
    ("economy-catalog-mismatch", [Set("research_economy.json", "/catalog_id", catalogMismatch)]),
    ("domain-metadata-mismatch", [Set("foundations.json", "/domain", "\"wrong_domain\"")]),
    ("node-domain-mismatch", [Set("foundations.json", "/nodes/0/domain", "\"wrong_domain\"")]),
    ("duplicate-node-id", [CopyAppend("foundations.json", "/nodes/0", "/nodes")]),
    ("declared-node-count-mismatch", [Set("index.json", "/node_count", "369")]),
    ("unknown-complexity", [Set("foundations.json", "/nodes/0/complexity", "\"missing_complexity\"")]),
    ("unknown-prerequisite", [Set("foundations.json", "/nodes/0/prerequisites/all_of", "[\"missing_node\"]")]),
    ("unknown-pressure", [Set("foundations.json", "/nodes/0/pressure_affinities", "[\"missing_pressure\"]")]),
    ("unknown-applicability-trait", [Set("foundations.json", "/nodes/0/applicability/requires_traits", "[\"missing_trait\"]")]),
    ("unknown-evidence", [Set("foundations.json", "/nodes/0/applicability/requires_evidence", "[\"missing_evidence\"]")]),
    ("unknown-knowledge-field", [Set("foundations.json", "/nodes/0/knowledge_fields", "[\"missing_field\"]")]),
    ("unknown-capability-requirement", [Set("foundations.json", "/nodes/0/capability_requirements", "{\"all_of\":[\"missing_capability\"]}")]),
    ("unknown-capability-scope", [Set("capability_model.json", "/cross_lineage_capabilities/0/scope", "\"unknown_scope\"")]),
    ("duplicate-capability-id", [CopyAppend("capability_model.json", "/cross_lineage_capabilities/0", "/cross_lineage_capabilities")]),
    ("unknown-implication-from", [Set("capability_model.json", "/implications/0/from", "\"missing_capability\"")]),
    ("unknown-implication-to", [Set("capability_model.json", "/implications/0/to", "\"missing_capability\"")]),
    ("override-unknown-pressure", [Set("research_economy.json", "/node_requirement_overrides/high_g_cardiovascular/required_pressure", "{\"missing_pressure\":1}")]),
    ("override-unknown-evidence", [Set("research_economy.json", "/node_requirement_overrides/alien_signal_analysis/required_evidence", "[\"missing_evidence\"]")]),
    ("starting-stage-unknown", [Set("research_economy.json", "/directed_program_concurrency/starting_stage/id", "\"missing_stage\"")]),
    ("duplicate-directed-stage", [CopyAppend("research_economy.json", "/directed_program_concurrency/progression/0", "/directed_program_concurrency/progression")]),
    ("grant-unknown-node", [CopyProperty("technology_grants.json", "/on_demonstrated/prototype_warp_drive", "/on_demonstrated/missing_node")]),
    ("grant-unknown-capability", [Set("technology_grants.json", "/on_demonstrated/prototype_warp_drive/grant_capabilities", "[\"missing_capability\"]")]),
    ("grant-unknown-trait", [Set("technology_grants.json", "/on_mature/biofabrication/add_civilization_traits", "[\"missing_trait\"]")]),
    ("grant-unknown-stage", [Set("technology_grants.json", "/on_mature/coordinated_research_networks/set_research_capacity_stage", "\"missing_stage\"")]),
    ("deployment-unknown-node", [Set("technology_grants.json", "/deployment_events/persistent_machine_cognition_instantiated/requires_any_mature_technology", "[\"missing_node\"]")]),
    ("deployment-unknown-trait", [Set("technology_grants.json", "/deployment_events/persistent_machine_cognition_instantiated/add_civilization_traits", "[\"missing_trait\"]")]),
};
foreach (var mutationCase in mutations)
    RunMutatedLoad(mutationCase.Name, root, mutationCase.Mutations);

var fixture = new
{
    Schema = "stellar-adaptive-research-catalog-oracle-v1",
    Cases = cases,
    SourceOnlyObservations = new object[]
    {
        new { Name = "dictionary-order", Value = "Nodes and index keys enumerate in insertion order; index values are explicitly sorted ordinal." },
        new { Name = "declared-capability-validation", Value = "Node output markers are intentionally not required to exist in the cross-lineage capability registry." },
        new { Name = "string-identity", Value = "Catalog IDs and references use StringComparer.Ordinal." },
    },
    Metadata = new { Generator = "actual AdaptiveResearchCatalogLoader", CanonicalRoot = "data/research/v1" }
};

Directory.CreateDirectory(Path.GetDirectoryName(output)!);
File.WriteAllText(output, JsonSerializer.Serialize(fixture, options) + Environment.NewLine);
Console.WriteLine($"adaptive research catalog oracle: {cases.Count} cases");

void RunLoad(string name, string directory)
{
    var before = Fingerprint(directory);
    AdaptiveResearchCatalog? result = null;
    Exception? error = null;
    try
    {
        result = AdaptiveResearchCatalogLoader.LoadFromDirectory(directory);
    }
    catch (Exception caught) when (caught is ArgumentException or DirectoryNotFoundException or FileNotFoundException or InvalidDataException or InvalidOperationException or FormatException or OverflowException or JsonException)
    {
        error = caught;
    }
    var inputUnchanged = before == Fingerprint(directory);
    cases.Add(new
    {
        Name = name,
        Kind = "Load",
        Arguments = new { Directory = "canonical" },
        Result = result is null ? null : ProjectCatalog(result),
        Error = ProjectError(error, directory),
        ErrorComparison = ErrorComparison(error),
        InputUnchanged = inputUnchanged
    });
}

void RunMutatedLoad(string name, string directory, Mutation[] caseMutations)
{
    var temporary = Path.Combine(scratch.Path, $"case-{cases.Count}");
    CopyDirectory(directory, temporary);
    foreach (var mutation in caseMutations)
        ApplyMutation(temporary, mutation);
    var before = Fingerprint(temporary);

    AdaptiveResearchCatalog? result = null;
    Exception? error = null;
    try
    {
        result = AdaptiveResearchCatalogLoader.LoadFromDirectory(temporary);
    }
    catch (Exception caught) when (caught is ArgumentException or DirectoryNotFoundException or FileNotFoundException or InvalidDataException or InvalidOperationException or FormatException or OverflowException or JsonException)
    {
        error = caught;
    }
    var projectedResult = result is null ? null : ProjectCatalog(result);
    var projectedError = ProjectError(error, temporary);
    var inputUnchanged = before == Fingerprint(temporary);
    cases.Add(new
    {
        Name = name,
        Kind = "Load",
        Arguments = new { Directory = "generated-copy", Mutations = caseMutations },
        Result = projectedResult,
        Error = projectedError,
        ErrorComparison = ErrorComparison(error),
        InputUnchanged = inputUnchanged
    });
}

void RunNodeLookup(string name, string directory, string nodeId)
{
    var before = Fingerprint(directory);
    var catalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(directory);
    AdaptiveResearchNodeDefinition? result = null;
    Exception? error = null;
    try
    {
        result = catalog.GetNode(nodeId);
    }
    catch (KeyNotFoundException caught)
    {
        error = caught;
    }
    var inputUnchanged = before == Fingerprint(directory);
    cases.Add(new
    {
        Name = name,
        Kind = "NodeLookup",
        Arguments = new { NodeId = nodeId },
        Result = result is null ? null : ProjectNode(result),
        Error = ProjectError(error, directory),
        ErrorComparison = "Exact",
        InputUnchanged = inputUnchanged
    });
}

void RunStageLookup(string name, string directory, string stageId)
{
    var before = Fingerprint(directory);
    var catalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(directory);
    DirectedResearchProgramStage? result = null;
    Exception? error = null;
    try
    {
        result = catalog.GetDirectedProgramStage(stageId);
    }
    catch (KeyNotFoundException caught)
    {
        error = caught;
    }
    var inputUnchanged = before == Fingerprint(directory);
    cases.Add(new
    {
        Name = name,
        Kind = "StageLookup",
        Arguments = new { StageId = stageId },
        Result = result is null ? null : ProjectStage(result),
        Error = ProjectError(error, directory),
        ErrorComparison = "Exact",
        InputUnchanged = inputUnchanged
    });
}

void RunScale(string name, string directory, double assigned, double recommended)
{
    var before = Fingerprint(directory);
    var scaling = AdaptiveResearchCatalogLoader.LoadFromDirectory(directory).LabScaling;
    double? result = null;
    Exception? error = null;
    try
    {
        result = scaling.ScaleAssignedLabs(assigned, recommended);
    }
    catch (ArithmeticException caught)
    {
        error = caught;
    }
    var inputUnchanged = before == Fingerprint(directory);
    cases.Add(new
    {
        Name = name,
        Kind = "ScaleLabs",
        Arguments = new { AssignedLabs = assigned, RecommendedLabs = recommended },
        Result = result,
        Error = ProjectError(error, directory),
        ErrorComparison = "Exact",
        InputUnchanged = inputUnchanged
    });
}

static object ProjectCatalog(AdaptiveResearchCatalog catalog) => new
{
    Metadata = catalog.Metadata,
    Nodes = catalog.Nodes.Values.Select(ProjectNode).ToArray(),
    Capabilities = catalog.Capabilities.Values.Select(value => new { value.Id, value.Name, Scope = (int)value.Scope }).ToArray(),
    CapabilityImplications = catalog.CapabilityImplications,
    PressureIds = catalog.PressureIds.ToArray(),
    TraitIds = catalog.TraitIds.ToArray(),
    EvidenceTypeIds = catalog.EvidenceTypeIds.ToArray(),
    KnowledgeFieldIds = catalog.KnowledgeFieldIds.ToArray(),
    LabScaling = catalog.LabScaling,
    DirectedProgramStages = catalog.DirectedProgramStages.Values.Select(ProjectStage).ToArray(),
    DemonstratedGrants = catalog.DemonstratedGrants.Select(value => new { NodeId = value.Key, Grant = value.Value }).ToArray(),
    MatureGrants = catalog.MatureGrants.Select(value => new { NodeId = value.Key, Grant = value.Value }).ToArray(),
    DeploymentEvents = catalog.DeploymentEvents.Values.ToArray(),
    ChildrenByPrerequisite = ProjectIndex(catalog.ChildrenByPrerequisite),
    NodesByPressure = ProjectIndex(catalog.NodesByPressure),
    NodesByEvidence = ProjectIndex(catalog.NodesByEvidence),
    NodesByTrait = ProjectIndex(catalog.NodesByTrait),
    NodesByCapabilityRequirement = ProjectIndex(catalog.NodesByCapabilityRequirement),
};

static object ProjectNode(AdaptiveResearchNodeDefinition value) => new
{
    value.Id, value.Name, value.DomainId, value.Complexity, value.GraphDepth,
    value.SolutionFamily, value.KnowledgeFields, value.AwarenessSources,
    value.PressureAffinities, value.Prerequisites, value.Applicability,
    value.CapabilityRequirements, value.DeclaredCapabilities,
    value.IsHypothesis, value.PublicNormalResearch,
    ProjectRequirements = new
    {
        value.ProjectRequirements.BaseResearchPoints,
        value.ProjectRequirements.MinimumLabs,
        value.ProjectRequirements.RecommendedLabs,
        RequiredPressure = value.ProjectRequirements.RequiredPressure.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
        RequiredPressureAny = value.ProjectRequirements.RequiredPressureAny.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
        value.ProjectRequirements.RequiredEvidence,
    }
};

static object ProjectStage(DirectedResearchProgramStage value) => new
{
    value.Id, value.DirectedProgramLimit, value.LabCapacityOnly, value.RequiredTechnologyId
};

static object[] ProjectIndex(IReadOnlyDictionary<string, IReadOnlyList<string>> index) =>
    index.Select(value => (object)new { Key = value.Key, NodeIds = value.Value }).ToArray();

static object? ProjectError(Exception? error, string root) => error is null ? null : new
{
    Type = error.GetType().Name,
    Message = error.Message.Replace(root, "<ROOT>", StringComparison.OrdinalIgnoreCase)
};

static string ErrorComparison(Exception? error) => error switch
{
    JsonException => "JsonParserCategory",
    InvalidOperationException => "JsonAccessCategory",
    FormatException => "NumericFormatCategory",
    _ => "Exact"
};

static Mutation Delete(string file) => new("DeleteFile", file, null, null, null);
static Mutation Raw(string file, string text) => new("RawFile", file, null, null, text);
static Mutation Set(string file, string pointer, string valueJson) => new("Set", file, pointer, valueJson, null);
static Mutation Remove(string file, string pointer) => new("Remove", file, pointer, null, null);
static Mutation CopyAppend(string file, string sourcePointer, string targetPointer) => new("CopyAppend", file, targetPointer, sourcePointer, null);
static Mutation CopyProperty(string file, string sourcePointer, string targetPointer) => new("CopyProperty", file, targetPointer, sourcePointer, null);

static void CopyDirectory(string source, string target)
{
    Directory.CreateDirectory(target);
    foreach (var file in Directory.EnumerateFiles(source))
        File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
}

static string Fingerprint(string directory)
{
    using var combined = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
    foreach (var path in Directory.EnumerateFiles(directory).OrderBy(value => System.IO.Path.GetFileName(value), StringComparer.Ordinal))
    {
        var name = System.IO.Path.GetFileName(path);
        combined.AppendData(System.Text.Encoding.UTF8.GetBytes(name));
        combined.AppendData(File.ReadAllBytes(path));
    }
    return Convert.ToHexString(combined.GetHashAndReset());
}

static void ApplyMutation(string root, Mutation mutation)
{
    if (Path.IsPathRooted(mutation.File) || mutation.File.Contains("..", StringComparison.Ordinal) || Path.GetFileName(mutation.File) != mutation.File)
        throw new InvalidOperationException("Mutation file must be a single relative file name.");
    var path = Path.GetFullPath(Path.Combine(root, mutation.File));
    if (!string.Equals(Path.GetDirectoryName(path), Path.GetFullPath(root), StringComparison.OrdinalIgnoreCase))
        throw new InvalidOperationException("Mutation file escapes the owned case directory.");
    if (mutation.Kind == "DeleteFile")
    {
        File.Delete(path);
        return;
    }
    if (mutation.Kind == "RawFile")
    {
        File.WriteAllText(path, mutation.Text!);
        return;
    }
    var document = JsonNode.Parse(File.ReadAllText(path))!;
    if (mutation.Kind == "Set")
        SetNode(document, mutation.Pointer!, JsonNode.Parse(mutation.Value!)!);
    else if (mutation.Kind == "Remove")
        RemoveNode(document, mutation.Pointer!);
    else if (mutation.Kind == "CopyAppend")
        Resolve(document, mutation.Pointer!).AsArray().Add(Resolve(document, mutation.Value!).DeepClone());
    else if (mutation.Kind == "CopyProperty")
        SetNode(document, mutation.Pointer!, Resolve(document, mutation.Value!).DeepClone());
    else
        throw new InvalidOperationException($"Unknown mutation kind {mutation.Kind}");
    File.WriteAllText(path, document.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
}

static JsonNode Resolve(JsonNode root, string pointer)
{
    var current = root;
    foreach (var segment in pointer.Split('/', StringSplitOptions.RemoveEmptyEntries))
        current = current is JsonArray array ? array[int.Parse(segment)]! : current[segment]!;
    return current;
}

static (JsonNode Parent, string Segment) ResolveParent(JsonNode root, string pointer)
{
    var segments = pointer.Split('/', StringSplitOptions.RemoveEmptyEntries);
    var parentPointer = "/" + string.Join('/', segments[..^1]);
    return (segments.Length == 1 ? root : Resolve(root, parentPointer), segments[^1]);
}

static void SetNode(JsonNode root, string pointer, JsonNode value)
{
    var (parent, segment) = ResolveParent(root, pointer);
    if (parent is JsonArray array)
        array[int.Parse(segment)] = value;
    else
        parent[segment] = value;
}

static void RemoveNode(JsonNode root, string pointer)
{
    var (parent, segment) = ResolveParent(root, pointer);
    if (parent is JsonArray array)
        array.RemoveAt(int.Parse(segment));
    else
        parent.AsObject().Remove(segment);
}

internal sealed record Mutation(string Kind, string File, string? Pointer, string? Value, string? Text);

internal sealed class OwnedScratchDirectory : IDisposable
{
    public OwnedScratchDirectory()
    {
        Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"stellar-adaptive-catalog-042-{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    public void Dispose()
    {
        var normalized = System.IO.Path.GetFullPath(Path);
        var parent = System.IO.Path.TrimEndingDirectorySeparator(System.IO.Path.GetTempPath());
        var leaf = System.IO.Path.GetFileName(normalized);
        if (string.Equals(System.IO.Path.GetDirectoryName(normalized), parent, StringComparison.OrdinalIgnoreCase) &&
            leaf.StartsWith("stellar-adaptive-catalog-042-", StringComparison.Ordinal))
            Directory.Delete(normalized, true);
    }
}
