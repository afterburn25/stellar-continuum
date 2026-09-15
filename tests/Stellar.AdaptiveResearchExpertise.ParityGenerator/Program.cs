using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchExpertiseOracle <research-data> <fixture>");
    return 1;
}
var options = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
var root = Path.GetFullPath(args[0]);
string Fingerprint(string path)
{
    ulong hash = 14695981039346656037UL;
    void Add(ReadOnlySpan<byte> bytes)
    {
        foreach (var value in bytes) { hash ^= value; hash *= 1099511628211UL; }
    }
    foreach (var file in Directory.EnumerateFiles(path).OrderBy(Path.GetFileName, StringComparer.Ordinal))
    {
        Add(System.Text.Encoding.UTF8.GetBytes(Path.GetFileName(file)));
        Add(new byte[] { 0 });
        Add(File.ReadAllBytes(file));
    }
    return hash.ToString("X16", CultureInfo.InvariantCulture);
}
var canonicalFingerprintBefore = Fingerprint(root);
var canonicalFiles = Directory.EnumerateFiles(root).ToDictionary(file => Path.GetFileName(file)!, File.ReadAllBytes, StringComparer.Ordinal);
var baseCatalog = AdaptiveResearchCatalogLoader.LoadFromDirectory(root);
var facilities = AdaptiveResearchFacilityCatalog.LoadFromDirectory(root, baseCatalog);
var catalog = AdaptiveResearchExpertiseCatalog.LoadFromDirectory(root, baseCatalog, facilities);
var methods = typeof(AdaptiveResearchExpertiseState)
    .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
    .GroupBy(method => (method.Name, method.GetParameters().Length))
    .ToDictionary(group => group.Key, group => group.Single());

object Snapshot(AdaptiveResearchExpertiseState state) => new
{
    state.Revision,
    FieldCompetence = state.FieldCompetence.Values.ToArray(),
    Institutions = state.Institutions.Values.ToArray(),
    TacitAssets = state.TacitAssets.Values.ToArray(),
};

var catalogProjection = Freeze(new
{
    Fields = catalog.Fields.Values.ToArray(),
    StageWeights = catalog.StageWeights.Select(pair => new { Stage = pair.Key, pair.Value }).ToArray(),
    catalog.ReadinessWeights,
    TacitAssetTypes = catalog.TacitAssetTypes.Values.ToArray(),
    Institutions = catalog.Institutions.Values.ToArray(),
    RuntimePolicy = new
    {
        StagePracticeGain = catalog.RuntimePolicy.StagePracticeGain.Select(pair => new { Stage = pair.Key, pair.Value }).ToArray(),
        catalog.RuntimePolicy.RelatedFieldTransferFraction,
        catalog.RuntimePolicy.MinimumGainFactor,
        catalog.RuntimePolicy.AnnualAtrophyRates,
        catalog.RuntimePolicy.AtrophyGraceYears,
        catalog.RuntimePolicy.GeneralLabMatchingFactor,
        catalog.RuntimePolicy.SpecializedMatchingFactor,
        catalog.RuntimePolicy.NonmatchingSpecialistFactor,
        TacitAssimilationFactors = catalog.RuntimePolicy.TacitAssimilationFactors.Select(pair => new { Stage = pair.Key, pair.Value }).ToArray(),
        catalog.RuntimePolicy.TacitBestWeight,
        catalog.RuntimePolicy.TacitMeanWeight,
        catalog.RuntimePolicy.PreservationFloorFraction,
        catalog.RuntimePolicy.EstablishedKnowledgeTheoreticalFloorFraction,
    },
});

var vectorCases = new List<object>();
var sampleVector = new ResearchCompetenceVector(1.25, 2.5, 3.75);
foreach (var component in new[] { ResearchCompetenceComponent.Theoretical, ResearchCompetenceComponent.Experimental, ResearchCompetenceComponent.Engineering, (ResearchCompetenceComponent)99 })
{
    double? result = null;
    Exception? error = null;
    try { result = sampleVector.Get(component); }
    catch (Exception caught) { error = caught; }
    vectorCases.Add(new { Component = (int)component, Result = result, Error = error is null ? null : new { Type = error.GetType().Name, error.Message } });
}

var malformed = new List<object>();
foreach (var recipe in new[]
{
    "missing-field-id", "duplicate-field-valid", "duplicate-field-invalid-name",
    "root-null", "fields-not-array", "fields-bool", "related-fields-not-array",
    "related-field-null", "related-field-nonstring", "related-field-object", "unknown-related-field",
    "membership-before-related", "invalid-stage-weight-sum", "invalid-readiness-weight-sum", "competence-weight-bool",
    "tacit-types-not-array", "unknown-tacit-component", "duplicate-tacit-invalid-support",
    "facility-files-not-array", "facility-filename-bool", "institutions-not-array", "unknown-institution-field", "duplicate-institution",
    "runtime-order-tacit-before-gain-scalar", "runtime-order-bad-tacit-before-missing-atrophy", "runtime-catalog-mismatch",
})
{
    using var scratch = new ScratchLease(root);
    ApplyMalformedRecipe(scratch.Path, recipe);
    var changedFiles = Directory.EnumerateFiles(scratch.Path)
        .Where(file => !File.ReadAllBytes(file).SequenceEqual(canonicalFiles[Path.GetFileName(file)]))
        .ToArray();
    if (changedFiles.Length != 1)
        throw new InvalidOperationException($"Malformed recipe '{recipe}' changed {changedFiles.Length} files.");
    var relativePath = Path.GetFileName(changedFiles[0]);
    var contentHex = Convert.ToHexString(File.ReadAllBytes(changedFiles[0]));
    var beforeLoadFingerprint = Fingerprint(scratch.Path);
    Exception? error = null;
    try { _ = AdaptiveResearchExpertiseCatalog.LoadFromDirectory(scratch.Path, baseCatalog, facilities); }
    catch (Exception caught) { error = caught; }
    malformed.Add(new
    {
        Recipe = recipe,
        Input = new { RelativePath = relativePath, ContentHex = contentHex, Fingerprint = beforeLoadFingerprint },
        Error = error is null ? null : new { Type = error.GetType().Name, error.Message },
        AfterFingerprint = Fingerprint(scratch.Path),
    });
    if (!scratch.TryCleanup(out var cleanupError))
        throw new IOException($"research expertise oracle cleanup failure: {cleanupError}");
}

var state = new AdaptiveResearchExpertiseState();
var commands = new List<object>();
void Command(string name, object input, Func<object?> invoke)
{
    var frozenInput = Freeze(input);
    var before = Freeze(Snapshot(state));
    object? result = null;
    Exception? error = null;
    try { result = invoke(); }
    catch (Exception caught) { error = caught; }
    commands.Add(new
    {
        Name = name, Input = frozenInput, Before = before,
        Result = result is null ? (JsonElement?)null : Freeze(result),
        Error = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message }),
        After = Freeze(Snapshot(state)),
    });
}

Func<object?> Reflect(string name, params object?[] arguments)
{
    var method = methods[(name, arguments.Length)];
    return () =>
    {
        try { return method.Invoke(state, arguments); }
        catch (TargetInvocationException error)
        {
            ExceptionDispatchInfo.Capture(error.InnerException ?? error).Throw();
            throw;
        }
    };
}

var fieldIds = catalog.Fields.Keys.Take(8).ToArray();
var institutionDefinitions = catalog.Institutions.Values.Take(2).ToArray();
var tacitType = catalog.TacitAssetTypes.Values.First();

Command("get-missing-empty", new { FieldId = "field:missing" }, () => state.GetField("field:missing"));
var fieldOne = new ResearchFieldCompetenceRuntimeState(fieldIds[0], new(1, 2, 3), new(4, 5, 6), 1.5, double.NaN, double.PositiveInfinity, 999);
Command("set-field-first", fieldOne, Reflect("SetField", fieldOne));
var fieldTwo = new ResearchFieldCompetenceRuntimeState(fieldIds[1], new(0, 0, 0), new(0, 0, 0), double.NegativeInfinity, 0, 2, -10);
Command("set-field-zero", fieldTwo, Reflect("SetField", fieldTwo));
Command("get-missing-at-revision", new { FieldId = "field:later-missing" }, () => state.GetField("field:later-missing"));
Command("remove-nonzero-field-noop", new { FieldId = fieldIds[0] }, Reflect("RemoveFieldIfZero", fieldIds[0]));
Command("remove-zero-field", new { FieldId = fieldIds[1] }, Reflect("RemoveFieldIfZero", fieldIds[1]));
var fieldThree = new ResearchFieldCompetenceRuntimeState(fieldIds[2], new(7, 8, 9), new(10, 11, 12), 3, 4, 5, 0);
Command("reinsert-field-reuses-slot", fieldThree, Reflect("SetField", fieldThree));
var fieldReplacement = fieldOne with { Current = new(20, 21, 22), HistoricalPeak = new(23, 24, 25) };
Command("replace-field-always-revises", fieldReplacement, Reflect("SetField", fieldReplacement));
var zeroA = new ResearchFieldCompetenceRuntimeState(fieldIds[4], new(0, 0, 0), new(0, 0, 0), 0, 0, 0, 0);
var zeroB = new ResearchFieldCompetenceRuntimeState(fieldIds[5], new(0, 0, 0), new(0, 0, 0), 0, 0, 0, 0);
Command("set-field-zero-a", zeroA, Reflect("SetField", zeroA));
Command("set-field-zero-b", zeroB, Reflect("SetField", zeroB));
Command("remove-zero-field-a", new { FieldId = fieldIds[4] }, Reflect("RemoveFieldIfZero", fieldIds[4]));
Command("remove-zero-field-b", new { FieldId = fieldIds[5] }, Reflect("RemoveFieldIfZero", fieldIds[5]));
var reusedA = new ResearchFieldCompetenceRuntimeState(fieldIds[6], new(1, 0, 0), new(1, 0, 0), 0, 0, 0, 0);
var reusedB = new ResearchFieldCompetenceRuntimeState(fieldIds[7], new(2, 0, 0), new(2, 0, 0), 0, 0, 0, 0);
Command("reinsert-field-lifo-a", reusedA, Reflect("SetField", reusedA));
Command("reinsert-field-lifo-b", reusedB, Reflect("SetField", reusedB));
foreach (var (name, current, peak) in new[]
{
    ("invalid-current-before-peak", new ResearchCompetenceVector(double.NaN, 0, 0), new ResearchCompetenceVector(double.PositiveInfinity, 0, 0)),
    ("invalid-peak-after-valid-current", new ResearchCompetenceVector(0, 0, 0), new ResearchCompetenceVector(0, -1, 0)),
})
{
    var invalid = new ResearchFieldCompetenceRuntimeState(fieldIds[3], current, peak, 0, 0, 0, 0);
    Command(name, invalid, Reflect("SetField", invalid));
}

var institutionOne = new ResearchInstitutionRuntimeState("institution:one", institutionDefinitions[0].InstitutionArchetypeId, null, 3, 2, 0);
Command("set-institution", institutionOne, Reflect("SetInstitution", institutionOne));
var institutionTwo = new ResearchInstitutionRuntimeState("institution:two", institutionDefinitions[1].InstitutionArchetypeId, "", 2, 0, 0);
Command("set-inactive-institution", institutionTwo, Reflect("SetInstitution", institutionTwo));
Command("total-labs-includes-inactive-lookup", new { }, () => state.TotalActiveEffectiveLabUnits(catalog));
Command("zero-missing-institution-noop", new ResearchInstitutionRuntimeState("institution:missing", institutionDefinitions[0].InstitutionArchetypeId, null, 0, 0, 0), Reflect("SetInstitution", new ResearchInstitutionRuntimeState("institution:missing", institutionDefinitions[0].InstitutionArchetypeId, null, 0, 0, 0)));
Command("zero-existing-institution-removes", institutionTwo with { TotalCount = 0, ActiveCount = 0 }, Reflect("SetInstitution", institutionTwo with { TotalCount = 0, ActiveCount = 0 }));
Command("remove-institution-missing", new { Id = "institution:missing" }, Reflect("RemoveInstitution", "institution:missing"));
Command("remove-institution", new { Id = "institution:one" }, Reflect("RemoveInstitution", "institution:one"));
Command("invalid-institution-counts", new ResearchInstitutionRuntimeState("institution:bad", "unknown", null, 0, 1, 0), Reflect("SetInstitution", new ResearchInstitutionRuntimeState("institution:bad", "unknown", null, 0, 1, 0)));
var unknownInstitution = new ResearchInstitutionRuntimeState("institution:unknown", "archetype:unknown", null, 1, 0, 0);
Command("set-unknown-inactive-institution", unknownInstitution, Reflect("SetInstitution", unknownInstitution));
Command("total-labs-validates-inactive-archetype", new { }, () => state.TotalActiveEffectiveLabUnits(catalog));
Command("remove-unknown-institution", new { Id = "institution:unknown" }, Reflect("RemoveInstitution", "institution:unknown"));

var asset = new ResearchTacitAssetRuntimeState("asset:one", tacitType.Id, ResearchTacitScopeKind.KnowledgeField, fieldIds[0], ResearchTacitAssimilationStage.Access, 50, .5, .25, .75, "fixture", null, 0);
Command("set-tacit", asset, Reflect("SetTacitAsset", asset));
Command("replace-tacit", asset with { AssimilationStage = ResearchTacitAssimilationStage.NativePractice, Depth = 100, Availability = 1, TranslationContextQuality = 0, TrainingContinuity = 1, ContextId = "" }, Reflect("SetTacitAsset", asset with { AssimilationStage = ResearchTacitAssimilationStage.NativePractice, Depth = 100, Availability = 1, TranslationContextQuality = 0, TrainingContinuity = 1, ContextId = "" }));
foreach (var (name, bad) in new[]
{
    ("invalid-tacit-depth-first", asset with { Depth = double.NaN, Availability = double.PositiveInfinity }),
    ("invalid-tacit-availability", asset with { Availability = -1 }),
    ("invalid-tacit-translation", asset with { TranslationContextQuality = double.PositiveInfinity }),
    ("invalid-tacit-training", asset with { TrainingContinuity = 1.0001 }),
})
    Command(name, bad, Reflect("SetTacitAsset", bad));
Command("remove-tacit", new { Id = asset.AssetId }, Reflect("RemoveTacitAsset", asset.AssetId));
Command("remove-tacit-missing", new { Id = asset.AssetId }, Reflect("RemoveTacitAsset", asset.AssetId));

var output = new
{
    Schema = "stellar-adaptive-research-expertise-foundation-v1",
    Culture = "InvariantCulture",
    CatalogId = baseCatalog.Metadata.CatalogId,
    CanonicalFingerprintBefore = canonicalFingerprintBefore,
    CanonicalFingerprintAfter = Fingerprint(root),
    Catalog = catalogProjection,
    Malformed = malformed,
    VectorCases = vectorCases,
    Commands = commands,
};
File.WriteAllText(args[1], JsonSerializer.Serialize(output, options) + Environment.NewLine);
Console.WriteLine($"research expertise oracle: {catalog.Fields.Count} fields, {catalog.Institutions.Count} institutions, {commands.Count} state commands");
return 0;

static void ApplyMalformedRecipe(string root, string recipe)
{
    if (recipe is "root-null" or "missing-field-id" or "duplicate-field-valid" or "duplicate-field-invalid-name" or "fields-not-array" or "fields-bool" or "related-fields-not-array" or "related-field-null" or "related-field-nonstring" or "related-field-object" or "membership-before-related")
    {
        var path = Path.Combine(root, "knowledge_fields.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        if (recipe == "root-null") { File.WriteAllText(path, "null"); return; }
        if (recipe == "missing-field-id") json["fields"]!.AsArray()[0]!.AsObject().Remove("id");
        if (recipe == "duplicate-field-valid") json["fields"]!.AsArray().Add(json["fields"]!.AsArray()[0]!.DeepClone());
        if (recipe == "duplicate-field-invalid-name")
        {
            var duplicate = json["fields"]!.AsArray()[0]!.DeepClone().AsObject();
            duplicate.Remove("name");
            json["fields"]!.AsArray().Add(duplicate);
        }
        if (recipe == "fields-not-array") json["fields"] = new JsonObject();
        if (recipe == "fields-bool") json["fields"] = true;
        if (recipe == "related-fields-not-array") json["fields"]!.AsArray()[0]!["related_fields"] = "wrong";
        if (recipe == "related-field-null") json["fields"]!.AsArray()[0]!["related_fields"]!.AsArray().Add(null);
        if (recipe == "related-field-nonstring") json["fields"]!.AsArray()[0]!["related_fields"]!.AsArray().Add(7);
        if (recipe == "related-field-object") json["fields"]!.AsArray()[0]!["related_fields"]!.AsArray().Add(new JsonObject());
        if (recipe == "membership-before-related")
        {
            json["fields"]!.AsArray()[0]!["id"] = "field:unregistered";
            json["fields"]!.AsArray()[0]!["related_fields"]!.AsArray().Add("field:missing");
        }
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "unknown-related-field")
    {
        var path = Path.Combine(root, "knowledge_fields.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["fields"]!.AsArray()[0]!["related_fields"]!.AsArray().Add("field:missing");
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe is "invalid-stage-weight-sum" or "invalid-readiness-weight-sum" or "competence-weight-bool")
    {
        var path = Path.Combine(root, "research_competence_model.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        if (recipe == "invalid-stage-weight-sum") json["project_field_readiness"]!["stage_component_weights"]!["experimental"]!["theoretical"] = 9.0;
        else if (recipe == "invalid-readiness-weight-sum") json["project_readiness"]!["inputs"]!["field_competence"]!["weight"] = 9.0;
        else json["project_readiness"]!["inputs"]!["field_competence"]!["weight"] = true;
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe is "unknown-tacit-component" or "duplicate-tacit-invalid-support")
    {
        var path = Path.Combine(root, "tacit_knowledge_model.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        if (recipe == "unknown-tacit-component") json["knowledge_asset_types"]!.AsArray()[0]!["primary_support"]!.AsArray()[0] = "unknown_component";
        else
        {
            var duplicate = json["knowledge_asset_types"]!.AsArray()[0]!.DeepClone();
            duplicate!["primary_support"]!.AsArray()[0] = "unknown_component";
            json["knowledge_asset_types"]!.AsArray().Add(duplicate);
        }
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "tacit-types-not-array")
    {
        var path = Path.Combine(root, "tacit_knowledge_model.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["knowledge_asset_types"] = new JsonObject();
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "facility-files-not-array")
    {
        var path = Path.Combine(root, "research_facility_index.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["facility_catalog_files"] = "wrong";
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "facility-filename-bool")
    {
        var path = Path.Combine(root, "research_facility_index.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["facility_catalog_files"]!.AsArray()[0] = true;
        File.WriteAllText(path, json.ToJsonString()); return;
    }
    if (recipe == "institutions-not-array")
    {
        var index = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "research_facility_index.json")))!.AsObject();
        var file = index["facility_catalog_files"]!.AsArray()[0]!.GetValue<string>();
        var path = Path.Combine(root, file);
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["institution_archetypes"] = new JsonObject();
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "unknown-institution-field")
    {
        var index = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "research_facility_index.json")))!.AsObject();
        var file = index["facility_catalog_files"]!.AsArray()[0]!.GetValue<string>();
        var path = Path.Combine(root, file);
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["institution_archetypes"]!.AsArray()[0]!["specialized_fields"]!.AsArray().Add("field:missing");
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "duplicate-institution")
    {
        var index = JsonNode.Parse(File.ReadAllText(Path.Combine(root, "research_facility_index.json")))!.AsObject();
        var file = index["facility_catalog_files"]!.AsArray()[0]!.GetValue<string>();
        var path = Path.Combine(root, file);
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["institution_archetypes"]!.AsArray().Add(json["institution_archetypes"]!.AsArray()[0]!.DeepClone());
        File.WriteAllText(path, json.ToJsonString()); return;
    }
    if (recipe == "runtime-catalog-mismatch")
    {
        var path = Path.Combine(root, "research_runtime_expertise_policy.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["catalog_id"] = "catalog:mismatch";
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "runtime-order-tacit-before-gain-scalar")
    {
        var path = Path.Combine(root, "research_runtime_expertise_policy.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json["tacit_assimilation_factors"]!.AsObject().Remove("access");
        json["competence_gain_rules"]!.AsObject().Remove("related_field_transfer_fraction");
        File.WriteAllText(path, json.ToJsonString());
        return;
    }
    if (recipe == "runtime-order-bad-tacit-before-missing-atrophy")
    {
        var path = Path.Combine(root, "research_runtime_expertise_policy.json");
        var json = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        json.AsObject().Remove("atrophy_rules");
        json["tacit_assimilation_factors"]!["access"] = true;
        File.WriteAllText(path, json.ToJsonString()); return;
    }
    throw new InvalidOperationException($"Unknown malformed recipe '{recipe}'.");
}

sealed class ScratchLease : IDisposable
{
    private readonly string parent;
    private readonly string claim;
    private bool cleaned;
    public string Path { get; }
    public ScratchLease(string source)
    {
        var normalized = System.IO.Path.GetFullPath(source);
        parent = System.IO.Path.GetFullPath(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "stellar-research-expertise-scratch"));
        Directory.CreateDirectory(parent);
        claim = System.IO.Path.Combine(parent, $"{Guid.NewGuid():N}.claim");
        using (new FileStream(claim, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { }
        Path = claim + ".directory";
        if (Directory.Exists(Path)) throw new IOException($"Scratch directory already exists: {Path}");
        Directory.CreateDirectory(Path);
        if (!string.Equals(System.IO.Path.GetDirectoryName(Path), parent, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Scratch directory escaped its owned parent.");
        foreach (var file in Directory.EnumerateFiles(normalized))
            File.Copy(file, System.IO.Path.Combine(Path, System.IO.Path.GetFileName(file)));
    }
    public bool TryCleanup(out string? error)
    {
        error = null;
        if (cleaned) return true;
        try
        {
            if (!string.Equals(System.IO.Path.GetDirectoryName(Path), parent, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Refusing to clean a scratch directory outside its owned parent.");
            if (Directory.Exists(Path)) Directory.Delete(Path, recursive: true);
            if (File.Exists(claim)) File.Delete(claim);
            cleaned = true;
            return true;
        }
        catch (Exception caught) { error = caught.Message; return false; }
    }
    public void Dispose() => TryCleanup(out _);
}
