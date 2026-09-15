using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Research.Adaptive;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
    };
    private static readonly PropertyInfo PreparedPayload = typeof(PreparedCampaignSave)
        .GetProperty("Payload", BindingFlags.Instance | BindingFlags.NonPublic)!;

    private sealed record Row(string Name, string InputJson, string BeforeSha256,
        string AfterSha256, JsonNode? Result, string? ErrorType,
        string? ErrorMessage, string? InnerType, string? InnerMessage);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args.Length is 4 or 5 && args[0] == "--verify")
            return VerifyNative(args[1], args[2], args[3],
                args.Length == 5 ? args[4] : "valid-current17");
        if (args.Length != 4)
        {
            Console.Error.WriteLine(
                "Usage: Player17JsonOracle <fixture> <Game source root> <research root> <Gate091 fixture>");
            return 1;
        }
        var output = Path.GetFullPath(args[0]);
        var sourceRoot = Path.GetFullPath(args[1]);
        var researchRoot = Path.GetFullPath(args[2]);
        var gate091 = Path.GetFullPath(args[3]);
        try
        {
            string[] files =
            {
                "Persistence/CampaignStatePersistenceService.cs",
                "Persistence/DiplomacyCampaignReferenceValidator.cs",
                "Persistence/CampaignSaveService.cs",
                "Simulation/Diplomacy/DiplomacySnapshotInvariantValidator.cs",
                "Simulation/Research/Adaptive/AdaptiveResearchCampaignState.cs",
            };
            var sourceBefore = files.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
            }).ToArray();
            var baselineDocument = JsonNode.Parse(File.ReadAllText(gate091))!.AsObject();
            var baseline = baselineDocument["Rows"]!.AsArray()
                .Select(node => node!.AsObject())
                .Single(row => row["Name"]!.GetValue<string>() == "capture-valid")["Input"]!
                .DeepClone().AsObject();
            var baselineText = baseline.ToJsonString(Json);
            var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(researchRoot);
            var service = new CampaignStatePersistenceService(adaptiveResearchRuntime: runtime);
            var rows = new List<Row>();
            var scratch = ExclusiveScratch();
            try
            {
                Add(rows, service, scratch, "valid-current17", baselineText);
                Add(rows, service, scratch, "valid-populated-research-dictionaries",
                    Change(baseline, root =>
                    {
                        var strategic = root["AdaptiveResearch"]!["Civilizations"]![0]!
                            ["Research"]!["Research"]!["Research"]!;
                        strategic["PressureSupport"]!["MetricSignals"] =
                            new JsonObject { ["habitat_capacity_utilization"] = 0.75 };
                        strategic["Agenda"]!["DomainPriorities"] =
                            new JsonObject { ["logistics"] = "important" };
                        strategic["Agenda"]!["FieldPriorities"] =
                            new JsonObject { ["materials"] = "strategic" };
                        strategic["Agenda"]!["ProblemPriorities"] =
                            new JsonObject { ["habitat_crowding"] = "important" };
                        strategic["Agenda"]!["CapabilityPriorities"] =
                            new JsonObject { ["spacecraft_construction"] = "strategic" };
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "duplicate-format-invalid-then-valid",
                    baselineText.Replace("\"FormatVersion\": 17",
                        "\"FormatVersion\": \"bad\",\n  \"FormatVersion\": 17",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-format-valid-then-invalid",
                    baselineText.Replace("\"FormatVersion\": 17",
                        "\"FormatVersion\": 17,\n  \"FormatVersion\": \"bad\"",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-format-valid-last",
                    baselineText.Replace("\"FormatVersion\": 17",
                        "\"FormatVersion\": 16,\n  \"FormatVersion\": 17",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-diplomacy-bad-then-valid",
                    baselineText.Replace("\"Diplomacy\": {",
                        "\"Diplomacy\": 5,\n  \"Diplomacy\": {",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-diplomacy-valid-then-bad",
                    AppendRoot(baselineText, "\"Diplomacy\": 5"));
                Add(rows, service, scratch, "duplicate-nested-diplomacy-member",
                    baselineText.Replace("\"NextClaimId\": 1",
                        "\"NextClaimId\": 1,\n    \"NextClaimId\": 2",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-nested-invalid-then-valid",
                    baselineText.Replace("\"NextClaimId\": 1",
                        "\"NextClaimId\": \"bad\",\n    \"NextClaimId\": 1",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "duplicate-nested-valid-then-invalid",
                    baselineText.Replace("\"NextClaimId\": 1",
                        "\"NextClaimId\": 1,\n    \"NextClaimId\": \"bad\"",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "malformed-json", "{");
                Add(rows, service, scratch, "array-root", "[]");
                Add(rows, service, scratch, "developer-envelope-marker",
                    AppendRoot(baselineText, "\"DeveloperFormatVersion\": null"));
                Add(rows, service, scratch, "missing-format",
                    Change(baseline, root => root.Remove("FormatVersion"))
                        .ToJsonString(Json));
                Add(rows, service, scratch, "unsupported-format-18",
                    Change(baseline, root => root["FormatVersion"] = 18)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "bad-type-format",
                    Change(baseline, root => root["FormatVersion"] = "bad")
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy",
                    Change(baseline, root => root.Remove("Diplomacy"))
                        .ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy",
                    Change(baseline, root => root["Diplomacy"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "bad-type-diplomacy",
                    Change(baseline, root => root["Diplomacy"] = 5)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-contacts",
                    Change(baseline, root => root["Diplomacy"]!.AsObject()
                        .Remove("Contacts")).ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy-contacts",
                    Change(baseline, root => root["Diplomacy"]!["Contacts"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-next-claim",
                    Change(baseline, root => root["Diplomacy"]!.AsObject()
                        .Remove("NextClaimId")).ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy-next-claim",
                    Change(baseline, root => root["Diplomacy"]!["NextClaimId"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "null-contacts-before-bad-counter",
                    Change(baseline, root =>
                    {
                        root["Diplomacy"]!["Contacts"] = null;
                        root["Diplomacy"]!["NextClaimId"] = "bad";
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "missing-contacts-before-bad-counter",
                    Change(baseline, root =>
                    {
                        root["Diplomacy"]!.AsObject().Remove("Contacts");
                        root["Diplomacy"]!["NextClaimId"] = "bad";
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "unknown-diplomacy-enum",
                    ReplaceWithin(baselineText, "\"Diplomacy\": {",
                        "\"Awareness\": 5", "\"Awareness\": 999"));
                Add(rows, service, scratch, "null-diplomacy-contact-id",
                    Change(baseline, root => root["Diplomacy"]!["Contacts"]![0]!["ContactId"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-contact-id",
                    Change(baseline, root => root["Diplomacy"]!["Contacts"]![0]!.AsObject()
                        .Remove("ContactId")).ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy-grievance-reason",
                    Change(baseline, root => root["Diplomacy"]!["Relationships"]![0]!["Grievances"]![0]!["Reason"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-grievance-reason",
                    Change(baseline, root => root["Diplomacy"]!["Relationships"]![0]!["Grievances"]![0]!.AsObject()
                        .Remove("Reason")).ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy-proposal-summary",
                    Change(baseline, root => root["Diplomacy"]!["Proposals"]![0]!["Summary"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-proposal-summary",
                    Change(baseline, root => root["Diplomacy"]!["Proposals"]![0]!.AsObject()
                        .Remove("Summary")).ToJsonString(Json));
                Add(rows, service, scratch, "null-diplomacy-history-summary",
                    Change(baseline, root => root["Diplomacy"]!["RecentHistory"]![0]!["Summary"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-diplomacy-history-summary",
                    Change(baseline, root => root["Diplomacy"]!["RecentHistory"]![0]!.AsObject()
                        .Remove("Summary")).ToJsonString(Json));
                Add(rows, service, scratch, "unsupported-galaxy-format",
                    Change(baseline, root => root["GalaxyFormatVersion"] = 15)
                        .ToJsonString(Json));

                Add(rows, service, scratch, "diplomacy-before-galaxy-and-research",
                    Change(baseline, root =>
                    {
                        root["Diplomacy"]!["NextClaimId"] = 0;
                        root.Remove("GalaxyFormatVersion");
                        root.Remove("AdaptiveResearch");
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "galaxy-format-before-research",
                    Change(baseline, root =>
                    {
                        root.Remove("GalaxyFormatVersion");
                        root.Remove("AdaptiveResearch");
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "galaxy-before-research",
                    Change(baseline, root =>
                    {
                        root["Galaxy"]!["Systems"] = null;
                        root.Remove("AdaptiveResearch");
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "references-before-research",
                    Change(baseline, root =>
                    {
                        root.Remove("AdaptiveResearch");
                        root["Diplomacy"]!["Contacts"] = new JsonArray(new JsonObject
                        {
                            ["ObserverCivilizationId"] = 999999,
                            ["ContactId"] = "unknown-contact",
                            ["TargetCivilizationId"] = null,
                            ["FirstObservedTick"] = 1,
                            ["LastObservedTick"] = 1,
                            ["LastObservedSystemId"] = null,
                            ["Awareness"] = 1,
                            ["Condition"] = 0,
                            ["CommunicationAvailable"] = false,
                            ["Confidence"] = .5,
                        });
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "research-after-valid-world",
                    Change(baseline, root => root.Remove("AdaptiveResearch"))
                        .ToJsonString(Json));
                Add(rows, service, scratch, "null-research-after-valid-world",
                    Change(baseline, root => root["AdaptiveResearch"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "bad-research-after-valid-world",
                    Change(baseline, root => root["AdaptiveResearch"] = 5)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "duplicate-research-valid-values",
                    DuplicateWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"SchemaVersion\": 2",
                        "\"SchemaVersion\": 2,\n    \"SchemaVersion\": 2"));
                Add(rows, service, scratch, "duplicate-research-invalid-then-valid",
                    DuplicateWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"SchemaVersion\": 2",
                        "\"SchemaVersion\": \"bad\",\n    \"SchemaVersion\": 2"));
                Add(rows, service, scratch, "duplicate-research-valid-then-invalid",
                    DuplicateWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"SchemaVersion\": 2",
                        "\"SchemaVersion\": 2,\n    \"SchemaVersion\": \"bad\""));
                Add(rows, service, scratch, "fractional-research-civilization-id",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"CivilizationId\": 0", "\"CivilizationId\": 0.5"));
                Add(rows, service, scratch, "overflow-research-civilization-id",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"CivilizationId\": 0",
                        "\"CivilizationId\": 2147483648"));
                Add(rows, service, scratch,
                    "duplicate-research-collection-invalid-then-valid",
                    DuplicateWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"Civilizations\": [",
                        "\"Civilizations\": 5,\n    \"Civilizations\": ["));
                Add(rows, service, scratch,
                    "duplicate-research-node-invalid-then-valid",
                    DuplicateWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"NodeId\": \"additive_manufacturing\"",
                        "\"NodeId\": 5,\n                    " +
                        "\"NodeId\": \"additive_manufacturing\""));
                Add(rows, service, scratch, "fractional-research-node-maturity",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"Maturity\": 3", "\"Maturity\": 3.5"));
                Add(rows, service, scratch, "unknown-research-enum",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"Maturity\": 3", "\"Maturity\": 999"));
                Add(rows, service, scratch, "bad-research-node-id",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"NodeId\": \"additive_manufacturing\"",
                        "\"NodeId\": 5"));
                Add(rows, service, scratch, "null-research-species-id",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"SpeciesId\": \"terran_baseline\"",
                        "\"SpeciesId\": null"));
                Add(rows, service, scratch, "null-research-node-id",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"NodeId\": \"additive_manufacturing\"",
                        "\"NodeId\": null"));
                Add(rows, service, scratch,
                    "null-unrepresentable-before-bad-funding",
                    ReplaceWithin(
                        ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                            "\"SpeciesId\": \"terran_baseline\"",
                            "\"SpeciesId\": null"),
                        "\"AdaptiveResearch\": {",
                        "\"ReservedMilestoneCredits\": 0.3",
                        "\"ReservedMilestoneCredits\": \"bad\""));
                Add(rows, service, scratch, "null-research-node-list-entry",
                    Change(baseline, root =>
                    {
                        var core = root["AdaptiveResearch"]!["Civilizations"]![0]!
                            ["Research"]!["Research"]!["Research"]!["Research"]!["Core"]!;
                        core["Nodes"]![0] = null;
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "null-research-active-pressure-id",
                    Change(baseline, root =>
                    {
                        var strategic = root["AdaptiveResearch"]!["Civilizations"]![0]!
                            ["Research"]!["Research"]!["Research"]!;
                        strategic["PressureSupport"]!["ActivePressureIds"]![0] = null;
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "null-research-context-trait-list",
                    Change(baseline, root =>
                    {
                        var core = root["AdaptiveResearch"]!["Civilizations"]![0]!
                            ["Research"]!["Research"]!["Research"]!["Research"]!["Core"]!;
                        core["ApplicabilityContexts"]!["species:terran_baseline"] = null;
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "null-research-priority-value",
                    Change(baseline, root =>
                    {
                        var strategic = root["AdaptiveResearch"]!["Civilizations"]![0]!
                            ["Research"]!["Research"]!["Research"]!;
                        strategic["Agenda"]!["DomainPriorities"] =
                            new JsonObject { ["logistics"] = null };
                    }).ToJsonString(Json));
                Add(rows, service, scratch, "null-research-project-funding",
                    Change(baseline, root => root["AdaptiveResearch"]!["Civilizations"]![0]!["ProjectFunding"] = null)
                        .ToJsonString(Json));
                Add(rows, service, scratch, "missing-research-project-funding",
                    Change(baseline, root => root["AdaptiveResearch"]!["Civilizations"]![0]!.AsObject()
                        .Remove("ProjectFunding")).ToJsonString(Json));
                Add(rows, service, scratch, "bad-research-funding-value",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"ReservedMilestoneCredits\": 0.3",
                        "\"ReservedMilestoneCredits\": \"bad\""));
                Add(rows, service, scratch, "bad-nested-research-schema",
                    ReplaceWithin(baselineText, "\"AdaptiveResearch\": {",
                        "\"SchemaVersion\": 5", "\"SchemaVersion\": 5.25"));
                Add(rows, service, scratch, "diagnostic-offset-long-property",
                    baselineText.Replace("\"NextClaimId\": 1",
                        "\"AnUnknownPropertyWithALongName\": true,\n" +
                        "                \"NextClaimId\" : \"bad\"",
                        StringComparison.Ordinal));
                Add(rows, service, scratch, "references-before-bad-research",
                    Change(baseline, root =>
                    {
                        root["AdaptiveResearch"] = 5;
                        root["Diplomacy"]!["Contacts"] = new JsonArray(new JsonObject
                        {
                            ["ObserverCivilizationId"] = 999999,
                            ["ContactId"] = "unknown-contact",
                            ["TargetCivilizationId"] = null,
                            ["FirstObservedTick"] = 1,
                            ["LastObservedTick"] = 1,
                            ["LastObservedSystemId"] = null,
                            ["Awareness"] = 1,
                            ["Condition"] = 0,
                            ["CommunicationAvailable"] = false,
                            ["Confidence"] = .5,
                        });
                    }).ToJsonString(Json));
            }
            finally { CheckedCleanup(scratch); }

            var sourceAfter = files.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
            }).ToArray();
            if (!sourceBefore.Select(source => source.Sha256)
                    .SequenceEqual(sourceAfter.Select(source => source.Sha256),
                        StringComparer.Ordinal))
                throw new IOException("Actual source changed during Gate092 capture.");
            var document = new
            {
                Schema = "stellar.player17-json.actual-source.v1",
                Authority = "actual CampaignStatePersistenceService.Load JSON entry point",
                Gate091FixtureSha256 = Sha(File.ReadAllBytes(gate091)),
                SourceBefore = sourceBefore,
                SourceAfter = sourceAfter,
                RowCount = rows.Count,
                Rows = rows,
            };
            File.WriteAllText(output,
                JsonSerializer.Serialize(document, Json) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"rows={rows.Count} fixture={Sha(File.ReadAllBytes(output))}");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Player17 JSON oracle failure: {error.GetType()}: {error.Message}");
            Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"output: {output}");
            Console.Error.WriteLine($"source root: {sourceRoot}");
            Console.Error.WriteLine($"research root: {researchRoot}");
            Console.Error.WriteLine($"Gate091 fixture: {gate091}");
            return 1;
        }
    }

    private static int VerifyNative(string nativePathArgument,
        string researchRootArgument, string fixtureArgument, string rowName)
    {
        var nativePath = Path.GetFullPath(nativePathArgument);
        var researchRoot = Path.GetFullPath(researchRootArgument);
        var fixture = Path.GetFullPath(fixtureArgument);
        try
        {
            var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(researchRoot);
            var service = new CampaignStatePersistenceService(adaptiveResearchRuntime: runtime);
            var loaded = service.Load(nativePath);
            var prepared = service.PrepareSave(loaded.Galaxy, loaded.SimulationDays,
                loaded.Diplomacy, loaded.AdaptiveResearch);
            var payload = PreparedPayload.GetValue(prepared)!;
            var result = JsonSerializer.SerializeToNode(payload, payload.GetType(), Json)!;
            result["SavedAtUtc"] = "2044-05-06T07:08:09+00:00";
            var evidence = JsonNode.Parse(File.ReadAllText(fixture))!.AsObject();
            var expected = evidence["Rows"]!.AsArray()
                .Select(node => node!.AsObject())
                .Single(row => row["Name"]!.GetValue<string>() == rowName)
                ["Result"]!;
            if (!JsonNode.DeepEquals(result, expected))
                throw new InvalidDataException(
                    "Native Player17 JSON changed values through actual-source Load/recapture.");
            Console.WriteLine($"nativeInterop=passed bytes={new FileInfo(nativePath).Length} " +
                $"sha256={Sha(File.ReadAllBytes(nativePath))}");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(
                $"Player17 native interoperability failure: {error.GetType()}: {error.Message}");
            Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"native input: {nativePath}");
            Console.Error.WriteLine($"research root: {researchRoot}");
            Console.Error.WriteLine($"fixture: {fixture}");
            return 1;
        }
    }

    private static void Add(List<Row> rows, CampaignStatePersistenceService service,
        string scratch, string name, string input)
    {
        var path = Path.Combine(scratch, name + ".json");
        var bytes = new UTF8Encoding(false).GetBytes(input);
        File.WriteAllBytes(path, bytes);
        JsonNode? result = null;
        Exception? error = null;
        try
        {
            var loaded = service.Load(path);
            var prepared = service.PrepareSave(loaded.Galaxy, loaded.SimulationDays,
                loaded.Diplomacy, loaded.AdaptiveResearch);
            var payload = PreparedPayload.GetValue(prepared)!;
            result = JsonSerializer.SerializeToNode(payload, payload.GetType(), Json);
            result!["SavedAtUtc"] = "2044-05-06T07:08:09+00:00";
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = File.ReadAllBytes(path);
        rows.Add(new(name, input, Sha(bytes), Sha(after), result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static JsonObject Change(JsonObject source, Action<JsonObject> change)
    {
        var result = source.DeepClone().AsObject();
        change(result);
        return result;
    }

    private static string AppendRoot(string input, string property)
    {
        var close = input.LastIndexOf('}');
        return input[..close] + ",\n  " + property + "\n" + input[close..];
    }

    private static string DuplicateWithin(string input, string anchor,
        string property, string replacement)
    {
        var anchorIndex = input.IndexOf(anchor, StringComparison.Ordinal);
        if (anchorIndex < 0) throw new InvalidDataException($"Missing anchor {anchor}.");
        var propertyIndex = input.IndexOf(property, anchorIndex, StringComparison.Ordinal);
        if (propertyIndex < 0) throw new InvalidDataException($"Missing property {property}.");
        return input[..propertyIndex] + replacement + input[(propertyIndex + property.Length)..];
    }

    private static string ReplaceWithin(string input, string anchor,
        string property, string replacement) =>
        DuplicateWithin(input, anchor, property, replacement);

    private static Exception Unwrap(Exception error) =>
        error is TargetInvocationException { InnerException: not null } target
            ? target.InnerException! : error;

    private static string Sha(byte[] bytes) =>
        Convert.ToHexString(SHA256.HashData(bytes));

    private static string ExclusiveScratch()
    {
        var parent = Path.GetFullPath(Path.GetTempPath());
        for (var attempt = 0; attempt < 32; ++attempt)
        {
            var path = Path.Combine(parent, "stellar-player092-" + Guid.NewGuid().ToString("N"));
            try { Directory.CreateDirectory(path); return path; }
            catch (IOException) { }
        }
        throw new IOException("Could not claim an exclusive Gate092 scratch directory.");
    }

    private static void CheckedCleanup(string path)
    {
        var full = Path.GetFullPath(path);
        var parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        if (!full.StartsWith(parent, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Refusing to clean an unowned scratch path.");
        Directory.Delete(full, recursive: true);
    }
}
