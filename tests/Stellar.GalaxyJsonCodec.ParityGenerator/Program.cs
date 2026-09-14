using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length > 0 && args[0] == "verify-native")
    {
        if (args.Length != 4)
            throw new ArgumentException(
                "Expected verify-native, native JSON, actual-source fixture, and report paths.");
        var nativePath = Path.GetFullPath(args[1]);
        var fixturePath = Path.GetFullPath(args[2]);
        var reportPath = Path.GetFullPath(args[3]);
        var verifyOptions = new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNameCaseInsensitive = false,
        };
        var nativeBytes = File.ReadAllBytes(nativePath);
        var envelope = JsonSerializer.Deserialize<CampaignSaveEnvelope>(nativeBytes, verifyOptions)
            ?? throw new InvalidOperationException("Native JSON produced a null envelope.");
        var fixtureDocument = JsonNode.Parse(File.ReadAllText(fixturePath))!.AsObject();
        var verificationBaseline = fixtureDocument["Rows"]!.AsArray().Select(node => node!.AsObject())
            .Single(row => row["Name"]!.GetValue<string>() == "field-complete-current16");
        var canonical = JsonNode.Parse(JsonSerializer.Serialize(envelope, verifyOptions));
        var expectedCanonical = JsonNode.Parse(
            verificationBaseline["SourceCanonicalJson"]!.GetValue<string>());
        if (!JsonNode.DeepEquals(canonical, expectedCanonical))
            throw new InvalidOperationException(
                "Actual System.Text.Json did not recover the source canonical envelope values.");

        var service = new CampaignSaveService();
        var loaded = service.Load(nativePath);
        var capturePayload = typeof(CampaignSaveService).GetMethod(
            "CapturePayload", BindingFlags.NonPublic | BindingFlags.Instance)!;
        var captured = (JsonObject)capturePayload.Invoke(service,
            new object[] { loaded.Galaxy, loaded.SimulationDays, false })!;
        var expectedRestored = verificationBaseline["ExpectedRestored"]!.AsObject();
        if (!JsonNode.DeepEquals(captured["Galaxy"], expectedRestored["Galaxy"]))
            throw new InvalidOperationException(
                "Actual CampaignSaveService restore/capture changed the native galaxy values.");
        if (loaded.SimulationDays != expectedRestored["SimulationDays"]!.GetValue<double>() ||
            loaded.GameVersion != expectedRestored["GameVersion"]!.GetValue<string>())
            throw new InvalidOperationException(
                "Actual CampaignSaveService restore changed native envelope values.");

        var report = new
        {
            Schema = "stellar.galaxy-json-codec.native-interop.v1",
            NativeJsonSha256 = Convert.ToHexString(SHA256.HashData(nativeBytes)),
            SystemTextJsonDeserialize = true,
            CanonicalEnvelopeValuesMatch = true,
            CampaignSaveServiceRestore = true,
            CompleteGalaxyRecaptureMatches = true,
        };
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);
        File.WriteAllText(reportPath,
            JsonSerializer.Serialize(report, verifyOptions) + Environment.NewLine,
            new UTF8Encoding(false));
        Console.WriteLine("native interoperability: deserialize/restore/recapture passed");
        return 0;
    }
    if (args.Length != 3)
        throw new ArgumentException("Expected source root, Gate087 fixture, and output fixture.");
    var root = Path.GetFullPath(args[0]);
    var gate087Path = Path.GetFullPath(args[1]);
    var output = Path.GetFullPath(args[2]);
    var options = new JsonSerializerOptions
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
    };
    string[] sourceFiles =
    {
        "src/Game/Persistence/CampaignSaveService.cs",
        "src/Game/Simulation/Generation/GalaxyGenerationSettings.cs",
        "src/Game/Simulation/Combat/Massive/MassiveCombatState.cs",
        "src/Game/Simulation/Combat/Massive/MassiveCombatEquipment.cs",
        "src/Game/Simulation/Combat/Massive/MassiveCombatContracts.cs",
        "src/Game/Simulation/Combat/CampaignMassiveCombat.cs",
    };
    Dictionary<string, string> SourceHashes() => sourceFiles.ToDictionary(path => path, path =>
        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(root, path)))));
    var sourceBefore = SourceHashes();
    var gate087 = JsonNode.Parse(File.ReadAllText(gate087Path))!.AsObject();
    var baselineRow = gate087["Rows"]!.AsArray().Select(node => node!.AsObject())
        .Single(row => row["Name"]!.GetValue<string>() == "valid-current16");
    var baseline = baselineRow["Input"]!.DeepClone().AsObject();
    var restored = baselineRow["Result"]!.DeepClone();
    var rows = new List<object>();

    JsonObject Change(Action<JsonObject> change)
    {
        var value = baseline.DeepClone().AsObject();
        change(value);
        return value;
    }

    string AppendRootProperty(string input, string propertyJson)
    {
        if (input == "{}")
            return "{\n  " + propertyJson + "\n}";
        var closing = input.LastIndexOf('}');
        if (closing < 0)
            throw new InvalidOperationException("Baseline envelope has no root close token.");
        return input[..closing] + ",\n  " + propertyJson + "\n" + input[closing..];
    }

    string NestedArrays(int count) => new string('[', count) + new string(']', count);
    string NestedArraysWithScalar(int count) =>
        new string('[', count) + "0" + new string(']', count);
    string NestedObjects(int count)
    {
        var builder = new StringBuilder();
        for (var index = 1; index < count; ++index)
            builder.Append("{\"Nested\":");
        builder.Append("{}");
        for (var index = 1; index < count; ++index)
            builder.Append('}');
        return builder.ToString();
    }
    string NestedObjectsWithScalar(int count)
    {
        var builder = new StringBuilder();
        for (var index = 0; index < count; ++index)
            builder.Append("{\"Nested\":");
        builder.Append('0');
        for (var index = 0; index < count; ++index)
            builder.Append('}');
        return builder.ToString();
    }

    void Evaluate(string name, string input, string nativePhase = "Success",
        string? nativePath = null, JsonNode? expectedRestored = null,
        bool sourceFileBom = false)
    {
        string? canonical = null;
        object? deserializeError = null;
        object? serializeError = null;
        CampaignSaveEnvelope? envelope = null;
        try
        {
            var sourceInput = sourceFileBom && input.Length > 0 && input[0] == '\uFEFF'
                ? input[1..]
                : input;
            envelope = JsonSerializer.Deserialize<CampaignSaveEnvelope>(sourceInput, options);
        }
        catch (Exception exception)
        {
            deserializeError = new { Type = exception.GetType().Name, exception.Message };
        }
        if (deserializeError is null)
        {
            try { canonical = envelope is null ? "null" : JsonSerializer.Serialize(envelope, options); }
            catch (Exception exception)
            {
                serializeError = new { Type = exception.GetType().Name, exception.Message };
            }
        }
        rows.Add(new
        {
            Name = name,
            InputJson = input,
            SourceFileBom = sourceFileBom,
            SourceCanonicalJson = canonical,
            SourceDeserializeError = deserializeError,
            SourceSerializeError = serializeError,
            NativeExpectation = new { Phase = nativePhase, Path = nativePath },
            ExpectedRestored = expectedRestored,
        });
    }

    var baselineText = baseline.ToJsonString(options);
    Evaluate("field-complete-current16", baselineText, expectedRestored: restored);
    Evaluate("utf8-bom-field-complete", "\uFEFF" + baselineText,
        expectedRestored: restored, sourceFileBom: true);
    Evaluate("minimal-default-envelope", "{}");
    Evaluate("wrong-case-format-is-unknown", "{\"formatVersion\":16}");
    Evaluate("unknown-enum-number-deserializes", Change(value =>
        value["Galaxy"]!["Systems"]![0]!["Archetype"] = 987654).ToJsonString(options));
    Evaluate("negative-zero-double", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": -0.0", StringComparison.Ordinal));
    Evaluate("unicode-html-escaping", Change(value =>
        value["GameVersion"] = "<Ω&+雪>").ToJsonString(options));
    Evaluate("duplicate-valid-last-wins", baselineText.Replace(
        "\"FormatVersion\": 16", "\"FormatVersion\": 12,\n  \"FormatVersion\": 16", StringComparison.Ordinal));
    Evaluate("duplicate-invalid-earlier-still-fails", baselineText.Replace(
        "\"FormatVersion\": 16", "\"FormatVersion\": \"bad\",\n  \"FormatVersion\": 16", StringComparison.Ordinal), "Parse", "$.FormatVersion");
    Evaluate("duplicate-invalid-later-fails", baselineText.Replace(
        "\"FormatVersion\": 16", "\"FormatVersion\": 16,\n  \"FormatVersion\": \"bad\"", StringComparison.Ordinal), "Parse", "$.FormatVersion");
    Evaluate("duplicate-null-galaxy-then-valid", baselineText.Replace(
        "\"Galaxy\": {", "\"Galaxy\": null,\n  \"Galaxy\": {", StringComparison.Ordinal));
    Evaluate("duplicate-null-element-list-then-valid", baselineText.Replace(
        "\"Systems\": [", "\"Systems\": [null],\n    \"Systems\": [", StringComparison.Ordinal));
    Evaluate("duplicate-populated-galaxy-then-empty",
        AppendRootProperty(baselineText, "\"Galaxy\": {}"));
    Evaluate("duplicate-populated-galaxy-then-partial",
        AppendRootProperty(baselineText, "\"Galaxy\": {\"Seed\": 2468}"));
    Evaluate("int32-overflow", baselineText.Replace(
        "\"FormatVersion\": 16", "\"FormatVersion\": 2147483648", StringComparison.Ordinal), "Parse", "$.FormatVersion");
    Evaluate("int64-overflow", baselineText.Replace(
        "\"Seed\": 871603", "\"Seed\": 9223372036854775808", StringComparison.Ordinal), "Parse", "$.Galaxy.Seed");
    Evaluate("double-overflow", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": 1e400", StringComparison.Ordinal), "Encode", "$.SimulationDays");
    Evaluate("double-underflow", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": 1e-400", StringComparison.Ordinal));
    Evaluate("huge-positive-exponent", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": 1e9223372036854775807", StringComparison.Ordinal),
        "Encode", "$.SimulationDays");
    Evaluate("huge-negative-exponent", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": -1e-9223372036854775808", StringComparison.Ordinal));
    Evaluate("zero-huge-positive-exponent", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": 0e922337203685477580799", StringComparison.Ordinal));
    Evaluate("negative-zero-huge-negative-exponent", baselineText.Replace(
        "\"SimulationDays\": 37.25", "\"SimulationDays\": -0e-922337203685477580899", StringComparison.Ordinal));
    Evaluate("single-overflow", Change(value =>
        value["Galaxy"]!["Systems"]![0]!["X"] = 3.5e38).ToJsonString(options),
        "Encode", "$.Galaxy.Systems[0].X");
    Evaluate("single-direct-rounding", Change(value =>
        value["Galaxy"]!["Systems"]![0]!["X"] = 1.0000000596046448).ToJsonString(options));
    Evaluate("timestamp-offset-fraction", Change(value =>
        value["SavedAtUtc"] = "2042-03-04T05:06:07.1234000-05:30").ToJsonString(options));
    Evaluate("timestamp-eight-fraction-digits", Change(value =>
        value["SavedAtUtc"] = "2042-03-04T05:06:07.12345678+00:00").ToJsonString(options));
    Evaluate("timestamp-lowercase-tz", Change(value =>
        value["SavedAtUtc"] = "2042-03-04t05:06:07z").ToJsonString(options),
        "DateTimeOffset", "$.SavedAtUtc");
    Evaluate("timestamp-no-offset", Change(value =>
        value["SavedAtUtc"] = "2042-03-04T05:06:07").ToJsonString(options),
        "Representability", "$.SavedAtUtc");
    Evaluate("timestamp-date-only", Change(value =>
        value["SavedAtUtc"] = "2042-03-04").ToJsonString(options),
        "Representability", "$.SavedAtUtc");
    Evaluate("timestamp-negative-zero-offset", Change(value =>
        value["SavedAtUtc"] = "2042-03-04T05:06:07-00:00").ToJsonString(options));
    Evaluate("timestamp-minimum-positive-offset", Change(value =>
        value["SavedAtUtc"] = "0001-01-01T00:00:00+14:00").ToJsonString(options),
        "DateTimeOffset", "$.SavedAtUtc");
    Evaluate("timestamp-maximum-negative-offset", Change(value =>
        value["SavedAtUtc"] = "9999-12-31T23:59:59-14:00").ToJsonString(options),
        "DateTimeOffset", "$.SavedAtUtc");
    Evaluate("timestamp-invalid-month", Change(value =>
        value["SavedAtUtc"] = "2042-13-04T05:06:07+00:00").ToJsonString(options), "DateTimeOffset", "$.SavedAtUtc");
    Evaluate("timestamp-invalid-offset", Change(value =>
        value["SavedAtUtc"] = "2042-03-04T05:06:07+15:00").ToJsonString(options), "DateTimeOffset", "$.SavedAtUtc");
    Evaluate("missing-required-body-name", Change(value =>
        value["Galaxy"]!["PlanetaryBodies"]![0]!.AsObject().Remove("Name")).ToJsonString(options), "RequiredMember", "$.Galaxy.PlanetaryBodies[0]");
    Evaluate("explicit-null-required-body-name", Change(value =>
        value["Galaxy"]!["PlanetaryBodies"]![0]!["Name"] = null).ToJsonString(options));
    Evaluate("null-galaxy-unrepresentable", Change(value =>
        value["Galaxy"] = null).ToJsonString(options), "Representability", "$.Galaxy");
    Evaluate("null-system-element-unrepresentable", Change(value =>
        value["Galaxy"]!["Systems"]![0] = null).ToJsonString(options), "Representability", "$.Galaxy.Systems[0]");
    Evaluate("null-civilization-element-unrepresentable", Change(value =>
        value["Galaxy"]!["Civilizations"]![0] = null).ToJsonString(options), "Representability", "$.Galaxy.Civilizations[0]");
    Evaluate("null-combat-intelligence-element-unrepresentable", Change(value =>
        value["Galaxy"]!["CombatIntelligence"]![0] = null).ToJsonString(options), "Representability", "$.Galaxy.CombatIntelligence[0]");
    Evaluate("null-game-version-unrepresentable", Change(value =>
        value["GameVersion"] = null).ToJsonString(options), "Representability", "$.GameVersion");
    Evaluate("missing-metadata-string-unrepresentable", Change(value =>
        value["Galaxy"]!["GenerationMetadata"] = new JsonObject()).ToJsonString(options),
        "Representability", "$.Galaxy.GenerationMetadata.EnteredSeed");
    Evaluate("missing-core-string-unrepresentable", Change(value =>
        value["Galaxy"]!["GalacticCore"] = new JsonObject()).ToJsonString(options),
        "Representability", "$.Galaxy.GalacticCore.LandmarkKey");
    Evaluate("missing-character-string-unrepresentable", Change(value =>
    {
        var leadership = value["Galaxy"]!["Civilizations"]![0]!["Leadership"]!.AsObject();
        leadership[leadership.First().Key] = new JsonObject();
    }).ToJsonString(options), "Representability");
    Evaluate("nonnull-diplomacy-unrepresentable",
        AppendRootProperty(baselineText, "\"Diplomacy\": {}"),
        "Representability", "$.Diplomacy");
    Evaluate("bad-type-diplomacy", AppendRootProperty(baselineText,
        "\"Diplomacy\": 5"), "Parse", "$.Diplomacy");
    Evaluate("null-diplomacy-supported",
        AppendRootProperty(baselineText, "\"Diplomacy\": null"));
    Evaluate("nonnull-adaptive-research-unrepresentable",
        AppendRootProperty(baselineText, "\"AdaptiveResearch\": {}"),
        "Representability", "$.AdaptiveResearch");
    Evaluate("bad-type-adaptive-research", AppendRootProperty(baselineText,
        "\"AdaptiveResearch\": false"), "Parse", "$.AdaptiveResearch");
    Evaluate("null-adaptive-research-supported",
        AppendRootProperty(baselineText, "\"AdaptiveResearch\": null"));
    Evaluate("maximum-depth-array", AppendRootProperty("{}",
        "\"Unknown\": " + NestedArrays(63)));
    Evaluate("maximum-depth-array-scalar-leaf", AppendRootProperty("{}",
        "\"Unknown\": " + NestedArraysWithScalar(63)));
    Evaluate("exceeded-depth-array", AppendRootProperty("{}",
        "\"Unknown\": " + NestedArrays(64)), "Parse");
    Evaluate("maximum-depth-object", AppendRootProperty("{}",
        "\"Unknown\": " + NestedObjects(63)));
    Evaluate("maximum-depth-object-scalar-leaf", AppendRootProperty("{}",
        "\"Unknown\": " + NestedObjectsWithScalar(63)));
    Evaluate("exceeded-depth-object", AppendRootProperty("{}",
        "\"Unknown\": " + NestedObjects(64)), "Parse");
    Evaluate("malformed-truncated", baselineText[..(baselineText.Length / 2)], "Parse");
    Evaluate("trailing-json", baselineText + " false", "Parse");

    var sourceAfter = SourceHashes();
    if (!sourceBefore.OrderBy(pair => pair.Key).SequenceEqual(
            sourceAfter.OrderBy(pair => pair.Key)))
        throw new InvalidOperationException("Authoritative source files changed during oracle generation.");
    var fixture = new
    {
        Schema = "stellar.galaxy-json-codec.actual-source.v1",
        Authority = "System.Text.Json CampaignSaveEnvelope with CampaignSaveService JsonOptions",
        Gate087FixtureSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(gate087Path))),
        SourceHashesBefore = sourceBefore,
        SourceHashesAfter = sourceAfter,
        RowCount = rows.Count,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, options) + Environment.NewLine,
        new UTF8Encoding(false));
    Console.WriteLine($"rows={rows.Count} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
}
catch (Exception error)
{
    Console.Error.WriteLine($"galaxy JSON codec oracle failure: {error.GetType()}: {error.Message}");
    Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"Gate087 fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    Console.Error.WriteLine($"output fixture: {(args.Length > 2 ? Path.GetFullPath(args[2]) : "<missing>")}");
    return 1;
}
return 0;
