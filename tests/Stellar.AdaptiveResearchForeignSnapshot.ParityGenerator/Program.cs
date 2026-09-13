using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var options = new JsonSerializerOptions
{
    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
};
JsonElement Freeze(object value) => JsonSerializer.SerializeToElement(value, options).Clone();

try
{
    if (args.Length != 2)
    {
        Console.Error.WriteLine("usage: ResearchForeignSnapshotOracle <research-data> <fixture>");
        return 1;
    }
    var root = Path.GetFullPath(args[0]);
    var fixture = Path.GetFullPath(args[1]);
    using var canonicalHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
    foreach (var path in Directory.GetFiles(root, "*.json").OrderBy(Path.GetFileName, StringComparer.Ordinal))
    {
        canonicalHash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
        canonicalHash.AppendData(File.ReadAllBytes(path));
    }
    var canonicalFingerprint = Convert.ToHexString(canonicalHash.GetHashAndReset());
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var codec = new AdaptiveResearchForeignTechnologySnapshotCodec(runtime);
    var v3 = new AdaptiveResearchStrategicSnapshotCodec(runtime);
    var v2 = new AdaptiveResearchSnapshotV2Codec(runtime.Authority);
    var v1 = new AdaptiveResearchSnapshotCodec(runtime.Authority.Kernel);
    var state = runtime.Authority.CreateCivilizationState("fixture:schema4");
    var constraint = runtime.ForeignTechnologyCatalog.ConstraintIds.First();
    var component = runtime.ForeignTechnologyCatalog.Components.Values.First(value => value.TacitAssetTypeIds.Count > 0);
    var right = runtime.ForeignTechnologyCatalog.Rights.First();
    var field = runtime.Authority.ExpertiseCatalog.Fields.Keys.First();
    var evidenceType = runtime.Authority.Catalog.EvidenceTypeIds.First(id =>
        runtime.Authority.Catalog.NodesByEvidence.TryGetValue(id, out var nodes) && nodes.Count > 0);
    var package = new ForeignTechnologyPackageInput(
        "package:schema4", "foreign:schema4", "lineage:schema4",
        new[] { component.Id }, new[] { right }, new[] { field },
        new[] { new ForeignTechnologyEvidenceTransfer("evidence:schema4", evidenceType, "fixture", .8, .9) },
        new[] { constraint }, "fixture", .85, .9, .8, 2200, null);
    runtime.ForeignTechnology.AcquirePackage(state, package);
    runtime.ForeignTechnology.RecordAnalysisResult(
        state, "foreign:schema4", ForeignUnderstandingState.PrincipleUnderstood,
        .95, 2201, new[] { constraint });

    var canonical = codec.Capture(state);
    var canonicalJson = codec.Serialize(state);
    var rows = new List<object>();
    void Typed(string name, AdaptiveResearchStateSnapshotV4 input,
               Action<AdaptiveResearchCivilizationState>? after = null)
    {
        var before = Freeze(input);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = codec.Restore(input); }
        catch (Exception caught) { error = caught; }
        string? serialized = null;
        if (restored is not null)
        {
            after?.Invoke(restored);
            serialized = codec.Serialize(restored);
        }
        rows.Add(new
        {
            Name = name, Kind = "Typed", Input = before,
            InputAfter = Freeze(input), Serialized = serialized,
            ForeignRevision = restored is null ? (long?)null : runtime.ForeignTechnology.GetState(restored).Revision,
            Error = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message }),
        });
    }
    void Text(string name, string input)
    {
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = codec.Deserialize(input); }
        catch (Exception caught) { error = caught; }
        rows.Add(new
        {
            Name = name, Kind = "Text", Input = input,
            Serialized = restored is null ? null : codec.Serialize(restored),
            ForeignRevision = restored is null ? (long?)null : runtime.ForeignTechnology.GetState(restored).Revision,
            Error = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message }),
        });
    }

    Typed("roundtrip", canonical);
    Typed("restored-next-mutation", canonical, restored =>
        runtime.ForeignTechnology.ConfirmConstraint(restored, "foreign:schema4", constraint, 2202));
    Typed("nested-schema2", canonical with { Research = canonical.Research with { SchemaVersion = 2 } });
    Typed("nested-schema0", canonical with { Research = canonical.Research with { SchemaVersion = 0 } });
    Typed("packages-before-assessments", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { ComponentIds = new[] { "missing:component" } } : value).ToArray(),
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { KnownConstraintIds = new[] { "missing:constraint" } } : value).ToArray(),
    });
    Typed("assessment-unknown-constraint", canonical with
    {
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { KnownConstraintIds = new[] { "missing:constraint" } } : value).ToArray(),
    });
    Typed("package-empty-reference", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { PackageId = "\u2003" } : value).ToArray(),
    });
    Typed("package-invalid-integrity", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { Integrity = -0.25 } : value).ToArray(),
    });
    Typed("package-unknown-right", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { RightIds = new[] { "missing:right" } } : value).ToArray(),
    });
    Typed("package-unknown-field", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { KnowledgeFieldIds = new[] { "missing:field" } } : value).ToArray(),
    });
    Typed("package-missing-evidence", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { EvidenceRefs = new[] { "missing:evidence" } } : value).ToArray(),
    });
    Typed("package-missing-tacit", canonical with
    {
        ForeignPackages = canonical.ForeignPackages.Select((value, index) => index == 0
            ? value with { TacitAssetRefs = new[] { "missing:tacit" } } : value).ToArray(),
    });
    Typed("assessment-missing-evidence", canonical with
    {
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { EvidenceRefs = new[] { "missing:evidence" } } : value).ToArray(),
    });
    Typed("assessment-missing-tacit", canonical with
    {
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { TacitAssetRefs = new[] { "missing:tacit" } } : value).ToArray(),
    });
    Typed("unnamed-enums", canonical with
    {
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { Understanding = (ForeignUnderstandingState)91, Operability = (ForeignOperabilityState)(-7),
                           Reproduction = (ForeignReproductionState)73, Adaptation = (ForeignAdaptationState)44 } : value).ToArray(),
    });

    var missingSchema = JsonNode.Parse(canonicalJson)!.AsObject();
    missingSchema.Remove("schemaVersion");
    Text("missing-schema", missingSchema.ToJsonString());
    var wrongSchema = JsonNode.Parse(canonicalJson)!.AsObject();
    wrongSchema["schemaVersion"] = 5;
    Text("unsupported-schema", wrongSchema.ToJsonString());
    var nestedJsonSchema2 = JsonNode.Parse(canonicalJson)!.AsObject();
    nestedJsonSchema2["research"]!["schemaVersion"] = 2;
    Text("text-nested-schema2", nestedJsonSchema2.ToJsonString());
    var uppercaseEnum = JsonNode.Parse(canonicalJson)!.AsObject();
    uppercaseEnum["foreignAssessments"]![0]!["understanding"] = "PRINCIPLEUNDERSTOOD";
    Text("uppercase-enum-name", uppercaseEnum.ToJsonString());
    var numericStringEnum = JsonNode.Parse(canonicalJson)!.AsObject();
    numericStringEnum["foreignAssessments"]![0]!["adaptation"] = "-7";
    Text("numeric-string-enum", numericStringEnum.ToJsonString());
    var spacedPlusEnum = JsonNode.Parse(canonicalJson)!.AsObject();
    spacedPlusEnum["foreignAssessments"]![0]!["adaptation"] = "\u2003+3\u2003";
    Text("unicode-space-plus-enum", spacedPlusEnum.ToJsonString());
    var combinedEnum = JsonNode.Parse(canonicalJson)!.AsObject();
    combinedEnum["foreignAssessments"]![0]!["understanding"] = " observed , characterized ";
    Text("combined-symbolic-enum", combinedEnum.ToJsonString());
    Text("legacy-schema1-fallback", v1.Serialize(state));
    Text("legacy-schema2-fallback", v2.Serialize(state));
    Text("legacy-schema3-fallback", v3.Serialize(state));

    Exception? nonfiniteError = null;
    var nonfiniteRestored = codec.Restore(canonical with
    {
        ForeignAssessments = canonical.ForeignAssessments.Select((value, index) => index == 0
            ? value with { LastAssessmentYear = double.PositiveInfinity } : value).ToArray(),
    });
    try { _ = codec.Serialize(nonfiniteRestored); }
    catch (Exception caught) { nonfiniteError = caught; }

    File.WriteAllText(fixture, JsonSerializer.Serialize(new
    {
        Generator = "actual C# AdaptiveResearchForeignTechnologySnapshotCodec",
        CanonicalFingerprint = canonicalFingerprint,
        Controls = new { constraint, component = component.Id, right, field, evidenceType },
        Canonical = Freeze(canonical), CanonicalJson = canonicalJson, Rows = rows,
        NonfiniteSerialize = new
        {
            RestoreSucceeded = true,
            Error = nonfiniteError is null ? (JsonElement?)null : Freeze(new { Type = nonfiniteError.GetType().Name, nonfiniteError.Message }),
        },
    }, options));
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine($"ResearchForeignSnapshotOracle failed: {error}\nCWD: {Environment.CurrentDirectory}\nResearch root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}\nFixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
