using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";
try {
if (args.Length != 2)
    throw new ArgumentException("Expected canonical research directory and output fixture path.");

var root = Path.GetFullPath(args[0]);
var output = Path.GetFullPath(args[1]);
var options = new JsonSerializerOptions {
    WriteIndented = false,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};

string Fingerprint(string directory) {
    using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
    foreach (var path in Directory.GetFiles(directory, "*.json")
                 .OrderBy(path => Path.GetFileName(path), StringComparer.Ordinal)) {
        hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
        hash.AppendData(File.ReadAllBytes(path));
    }
    return Convert.ToHexString(hash.GetHashAndReset());
}

object ProjectState(AdaptiveResearchCivilizationState state) => new {
    state.CivilizationId,
    state.Revision,
    state.MaterializedViewRevision,
    state.DirectedProgramStageId,
    state.TotalEffectiveResearchLabs,
    state.AssignedEffectiveLabs,
    state.FreeEffectiveLabs,
    Nodes = state.NodeStates.Values.ToArray(),
    Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
    Evidence = state.EvidenceInstances.Values.ToArray(),
    CivilizationTraits = state.CivilizationTraits.ToArray(),
    Contexts = state.ApplicabilityContexts.Select(pair =>
        new { Id = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
    Capabilities = state.Capabilities.ToArray(),
    FacilityCapabilities = state.FacilityCapabilities.ToArray(),
    DeploymentEvents = state.EnabledDeploymentEventIds.ToArray(),
    Projects = state.ActiveProjects.Values.ToArray(),
};

object ProjectResult(AdaptiveResearchStartingCompositionResult result) => new {
    State = ProjectState(result.State),
    Deferred = new {
        FieldCompetence = result.Deferred.FieldCompetence.Select(pair =>
            new { Id = pair.Key, pair.Value.Theoretical, pair.Value.Experimental, pair.Value.Engineering }).ToArray(),
        ResearchInstitutions = result.Deferred.ResearchInstitutions,
        TacitAssets = result.Deferred.TacitAssets,
        SelectedFragmentIds = result.Deferred.SelectedFragmentIds,
        result.Deferred.ReferenceProfileId,
        result.Deferred.HistoricalNotes,
    },
    InitialHorizonEvents = result.InitialHorizonEvents,
};

object Error(Exception error) => new { Type = error.GetType().Name, error.Message };

var loadBefore = Fingerprint(root);
var runtime = AdaptiveResearchRuntime.LoadFromDirectory(root);
var composer = new AdaptiveResearchStartingProfileComposer(runtime, root);
var loadAfter = Fingerprint(root);
if (!string.Equals(loadBefore, loadAfter, StringComparison.Ordinal))
    throw new InvalidOperationException("Production loading changed canonical inputs.");

var rows = new List<object>();
var profileIds = composer.ReferenceProfileIds.ToArray();
for (var index = 0; index < profileIds.Length; ++index) {
    var profileId = profileIds[index];
    var civilizationId = $"fixture:civilization:{index}";
    var contextId = $"fixture:context:{index}";
    Func<AdaptiveResearchStartingCompositionResult> call =
        () => composer.ComposeReferenceProfile(civilizationId, profileId, contextId);
    var before = Fingerprint(root);
    AdaptiveResearchStartingCompositionResult? value = null;
    Exception? error = null;
    try {
        value = call();
    }
    catch (Exception caught) {
        error = caught;
    }
    var after = Fingerprint(root);
    if (!string.Equals(before, after, StringComparison.Ordinal))
        throw new InvalidOperationException($"Production call '{profileId}' changed canonical inputs.");
    rows.Add(new {
        Name = $"canonical-{profileId}",
        ProfileId = profileId,
        CivilizationId = civilizationId,
        ContextId = contextId,
        BeforeFingerprint = before,
        AfterFingerprint = after,
        Result = value is null ? null : ProjectResult(value),
        Error = error is null ? null : Error(error),
    });
}

var startingFiles = new[] {
    "starting_profile_index.json",
    "starting_research_fragments.json",
    "starting_biochemical_fragments.json",
    "starting_reference_profiles.json",
    "starting_biochemical_reference_profiles.json",
};

void Mutate(string scratch, string mutation) {
    var indexPath = Path.Combine(scratch, "starting_profile_index.json");
    var fragmentsPath = Path.Combine(scratch, "starting_research_fragments.json");
    var profilesPath = Path.Combine(scratch, "starting_reference_profiles.json");
    if (mutation == "duplicate-pressure-key") {
        var text = File.ReadAllText(profilesPath);
        const string needle = "\"long_duration_mission\":18";
        if (!text.Contains(needle, StringComparison.Ordinal))
            throw new InvalidOperationException("Duplicate-key mutation anchor was absent.");
        File.WriteAllText(profilesPath, text.Replace(
            needle, needle + ",\"long_duration_mission\":19",
            StringComparison.Ordinal));
        return;
    }
    if (mutation == "named-nonfinite-json") {
        var text = File.ReadAllText(profilesPath);
        const string needle = "\"historical_notes\":";
        var position = text.IndexOf(needle, StringComparison.Ordinal);
        if (position < 0)
            throw new InvalidOperationException("Nonfinite mutation anchor was absent.");
        text = text.Insert(position,
            "\"additional_starting_evidence\":[{\"evidence_type_id\":\"alien_signal\",\"quality\":NaN}],");
        File.WriteAllText(profilesPath, text);
        return;
    }
    if (mutation == "duplicate-scalar-last") {
        var text = File.ReadAllText(indexPath);
        var position = text.IndexOf("\"catalog_id\"", StringComparison.Ordinal);
        if (position < 0)
            throw new InvalidOperationException("Duplicate-scalar mutation anchor was absent.");
        text = text.Insert(position, "\"catalog_id\":\"wrong-catalog\",");
        File.WriteAllText(indexPath, text);
        return;
    }
    var targetPath = mutation is "missing-catalog" or "catalog-mismatch" or
        "fragment-count" or "profile-count" ? indexPath :
        mutation is "duplicate-fragment" or "unknown-maturity" or
        "competence-negative" or "institution-zero" or
        "institution-sum-overflow" or "shape-node-map" or
        "shape-competence-map" or "shape-traits" or "shape-capabilities" or
        "shape-evidence" or "shape-institutions" or "shape-tacit" or
        "shape-fragments" or "reverse-node-input" or
        "unicode-institution-order" ? fragmentsPath : profilesPath;
    var document = JsonNode.Parse(File.ReadAllText(targetPath))!.AsObject();
    switch (mutation) {
        case "missing-catalog": document.Remove("catalog_id"); break;
        case "catalog-mismatch": document["catalog_id"] = "wrong-catalog"; break;
        case "fragment-count": document["counts"]!["history_fragments"] = 16; break;
        case "profile-count": document["counts"]!["reference_profiles"] = 9; break;
        case "duplicate-fragment": {
            var values = document["fragments"]!.AsArray();
            values.Add(values[0]!.DeepClone());
            break;
        }
        case "duplicate-profile": {
            var values = document["profiles"]!.AsArray();
            values.Add(values[0]!.DeepClone());
            break;
        }
        case "unknown-maturity": {
            var states = document["fragments"]![0]!["starting_node_states"]!.AsObject();
            states[states.First().Key] = "future";
            break;
        }
        case "competence-negative": {
            var values = document["fragments"]![0]!["starting_field_competence"]!.AsObject();
            values.First().Value!["theoretical"] = -1;
            break;
        }
        case "institution-zero":
            document["fragments"]![0]!["starting_research_institutions"]![0]!["count"] = 0;
            break;
        case "institution-sum-overflow": {
            document["fragments"]![0]!["starting_research_institutions"]![0]!["count"] = int.MaxValue;
            document["fragments"]![1]!["starting_research_institutions"] =
                new JsonArray(new JsonObject {
                    ["institution_archetype_id"] = "general_research_laboratory",
                    ["count"] = 1,
                });
            break;
        }
        case "unknown-fragment":
            document["profiles"]![0]!["fragment_ids"]![0] = "unknown-fragment";
            break;
        case "no-base":
            document["profiles"]![0]!["fragment_ids"]![0] =
                document["profiles"]![0]!["fragment_ids"]![1]!.GetValue<string>();
            break;
        case "unknown-pressure":
            document["profiles"]![0]!["additional_starting_pressure_state"]!["unknown-pressure"] = 1;
            break;
        case "unknown-evidence": {
            document["profiles"]![0]!["additional_starting_evidence"] = new JsonArray();
            var values = document["profiles"]![0]!["additional_starting_evidence"]!.AsArray();
            values.Add(new JsonObject { ["evidence_type_id"] = "unknown-evidence" });
            break;
        }
        case "unknown-capability": {
            document["profiles"]![0]!["additional_starting_capabilities"] = new JsonArray();
            var values = document["profiles"]![0]!["additional_starting_capabilities"]!.AsArray();
            values.Add("unknown-capability");
            break;
        }
        case "optional-wrong-types":
            document["profiles"]![0]!["additional_starting_evidence"] =
                new JsonArray(new JsonObject {
                    ["evidence_instance_id"] = "fixture:optional",
                    ["evidence_type_id"] = "alien_signal",
                    ["provenance"] = null,
                    ["quality"] = "NaN",
                    ["confidence"] = null,
                    ["context_id"] = 7,
                });
            break;
        case "duplicate-first-seeds":
            document["profiles"]![0]!["additional_starting_capabilities"] =
                new JsonArray("spacecraft_construction", "spacecraft_construction");
            document["profiles"]![0]!["additional_starting_evidence"] =
                new JsonArray(
                    new JsonObject {
                        ["evidence_instance_id"] = "fixture:first-evidence",
                        ["evidence_type_id"] = "alien_signal",
                        ["provenance"] = "first",
                    },
                    new JsonObject {
                        ["evidence_instance_id"] = "fixture:first-evidence",
                        ["evidence_type_id"] = "unknown-evidence",
                        ["provenance"] = "second-must-not-win",
                    });
            break;
        case "mixed-reference-errors":
            document["profiles"]![0]!["additional_starting_pressure_state"]!["unknown-pressure"] = 1;
            document["profiles"]![0]!["additional_starting_evidence"] =
                new JsonArray(new JsonObject { ["evidence_type_id"] = "unknown-evidence" });
            document["profiles"]![0]!["additional_starting_capabilities"] =
                new JsonArray("unknown-capability");
            break;
        case "shape-node-map":
            document["fragments"]![0]!["starting_node_states"] = new JsonArray();
            break;
        case "shape-competence-map":
            document["fragments"]![0]!["starting_field_competence"] = new JsonArray();
            break;
        case "shape-traits":
            document["fragments"]![0]!["starting_applicability_traits"] = new JsonObject();
            break;
        case "shape-capabilities":
            document["fragments"]![0]!["starting_capabilities"] = new JsonObject();
            break;
        case "shape-evidence":
            document["fragments"]![0]!["starting_evidence"] = new JsonObject();
            break;
        case "shape-institutions":
            document["fragments"]![0]!["starting_research_institutions"] = new JsonObject();
            break;
        case "shape-tacit":
            document["fragments"]![0]!["starting_tacit_assets"] = new JsonObject();
            break;
        case "shape-fragments":
            document["fragments"] = new JsonObject();
            break;
        case "shape-pressure-map":
            document["profiles"]![0]!["additional_starting_pressure_state"] = new JsonArray();
            break;
        case "shape-fragment-ids":
            document["profiles"]![0]!["fragment_ids"] = new JsonObject();
            break;
        case "reverse-node-input": {
            var states = document["fragments"]![0]!["starting_node_states"]!.AsObject();
            var reversed = new JsonObject();
            foreach (var pair in states.Reverse())
                reversed.Add(pair.Key, pair.Value!.DeepClone());
            document["fragments"]![0]!["starting_node_states"] = reversed;
            break;
        }
        case "unicode-institution-order": {
            var values = document["fragments"]![0]!["starting_research_institutions"]!.AsArray();
            values.Add(new JsonObject {
                ["institution_archetype_id"] = "\ue000",
                ["count"] = 1,
            });
            values.Add(new JsonObject {
                ["institution_archetype_id"] = "\U00010000",
                ["count"] = 1,
            });
            break;
        }
        default: throw new InvalidOperationException("Unknown fixture mutation " + mutation);
    }
    File.WriteAllText(targetPath, document.ToJsonString());
}

var malformed = new[] {
    ("load-missing-catalog", "Load", "missing-catalog"),
    ("load-catalog-mismatch", "Load", "catalog-mismatch"),
    ("load-fragment-count", "Load", "fragment-count"),
    ("load-profile-count", "Load", "profile-count"),
    ("load-duplicate-fragment", "Load", "duplicate-fragment"),
    ("load-duplicate-profile", "Load", "duplicate-profile"),
    ("load-unknown-maturity", "Load", "unknown-maturity"),
    ("load-negative-competence", "Load", "competence-negative"),
    ("load-zero-institution", "Load", "institution-zero"),
    ("load-duplicate-pressure-key", "Load", "duplicate-pressure-key"),
    ("compose-institution-sum-overflow", "Compose", "institution-sum-overflow"),
    ("compose-unknown-fragment", "Compose", "unknown-fragment"),
    ("compose-no-base", "Compose", "no-base"),
    ("compose-unknown-pressure", "Compose", "unknown-pressure"),
    ("compose-unknown-evidence", "Compose", "unknown-evidence"),
    ("compose-unknown-capability", "Compose", "unknown-capability"),
    ("compose-optional-wrong-types", "ComposeValid", "optional-wrong-types"),
    ("load-named-nonfinite-json", "LoadJson", "named-nonfinite-json"),
    ("compose-first-duplicate-seeds", "ComposeValid", "duplicate-first-seeds"),
    ("compose-mixed-reference-error-order", "Compose", "mixed-reference-errors"),
    ("load-shape-node-map", "LoadShape", "shape-node-map"),
    ("load-shape-competence-map", "LoadShape", "shape-competence-map"),
    ("load-shape-traits", "LoadShape", "shape-traits"),
    ("load-shape-capabilities", "LoadShape", "shape-capabilities"),
    ("load-shape-evidence", "LoadShape", "shape-evidence"),
    ("load-shape-institutions", "LoadShape", "shape-institutions"),
    ("load-shape-tacit", "LoadShape", "shape-tacit"),
    ("load-shape-fragments", "LoadShape", "shape-fragments"),
    ("load-shape-pressure-map", "LoadShape", "shape-pressure-map"),
    ("load-shape-fragment-ids", "LoadShape", "shape-fragment-ids"),
    ("compose-reversed-node-input", "ComposeValid", "reverse-node-input"),
    ("compose-utf16-institution-order", "Compose", "unicode-institution-order"),
    ("compose-duplicate-scalar-last-wins", "ComposeValid", "duplicate-scalar-last"),
};

foreach (var test in malformed) {
    var parent = Path.Combine(Path.GetTempPath(), "stellar-gate052-owned");
    Directory.CreateDirectory(parent);
    var scratch = Path.GetFullPath(Path.Combine(parent, Guid.NewGuid().ToString("N")));
    if (!scratch.StartsWith(Path.GetFullPath(parent) + Path.DirectorySeparatorChar,
                            StringComparison.OrdinalIgnoreCase) || Directory.Exists(scratch))
        throw new InvalidOperationException("Scratch ownership validation failed.");
    Directory.CreateDirectory(scratch);
    try {
        foreach (var file in startingFiles)
            File.Copy(Path.Combine(root, file), Path.Combine(scratch, file));
        Mutate(scratch, test.Item3);
        var changedFiles = startingFiles
            .Where(file => !File.ReadAllBytes(Path.Combine(root, file))
                .SequenceEqual(File.ReadAllBytes(Path.Combine(scratch, file))))
            .Select(file => new {
                RelativePath = file,
                ContentBase64 = Convert.ToBase64String(
                    File.ReadAllBytes(Path.Combine(scratch, file))),
            })
            .ToArray();
        var before = Fingerprint(scratch);
        object? value = null;
        Exception? error = null;
        if (test.Item2 is "Load" or "LoadJson" or "LoadShape") {
            Func<object> call = () => new AdaptiveResearchStartingProfileComposer(runtime, scratch);
            try { value = call(); } catch (Exception caught) { error = caught; }
        } else {
            var malformedComposer = new AdaptiveResearchStartingProfileComposer(runtime, scratch);
            Func<AdaptiveResearchStartingCompositionResult> call = () =>
                malformedComposer.ComposeReferenceProfile(
                    "fixture:malformed", profileIds[0], "fixture:context:malformed");
            AdaptiveResearchStartingCompositionResult? composed = null;
            try { composed = call(); } catch (Exception caught) { error = caught; }
            value = composed is null ? null : ProjectResult(composed);
        }
        var after = Fingerprint(scratch);
        if (!string.Equals(before, after, StringComparison.Ordinal))
            throw new InvalidOperationException($"Production call '{test.Item1}' changed malformed inputs.");
        rows.Add(new {
            Name = test.Item1,
            Phase = test.Item2,
            Mutation = test.Item3,
            ChangedFiles = changedFiles,
            ProfileId = profileIds[0],
            CivilizationId = "fixture:malformed",
            ContextId = "fixture:context:malformed",
            BeforeFingerprint = before,
            AfterFingerprint = after,
            Result = value,
            Error = error is null ? null : Error(error),
        });
    } finally {
        if (Directory.Exists(scratch) &&
            scratch.StartsWith(Path.GetFullPath(parent) + Path.DirectorySeparatorChar,
                               StringComparison.OrdinalIgnoreCase))
            Directory.Delete(scratch, recursive: true);
    }
}

var invocationCases = new[] {
    ("compose-unknown-profile", "fixture:civ", "unknown-profile", "fixture:context"),
    ("compose-blank-civilization", " ", profileIds[0], "fixture:context"),
    ("compose-blank-profile", "fixture:civ", "\t", "fixture:context"),
    ("compose-blank-context", "fixture:civ", profileIds[0], "\r\n"),
    ("compose-blank-vt", "\v", profileIds[0], "fixture:context"),
    ("compose-blank-ff", "\f", profileIds[0], "fixture:context"),
    ("compose-blank-nbsp", "\u00a0", profileIds[0], "fixture:context"),
    ("compose-blank-u2000", "\u2000", profileIds[0], "fixture:context"),
    ("compose-blank-u3000", "\u3000", profileIds[0], "fixture:context"),
};
foreach (var test in invocationCases) {
    var before = Fingerprint(root);
    Func<AdaptiveResearchStartingCompositionResult> call = () =>
        composer.ComposeReferenceProfile(test.Item2, test.Item3, test.Item4);
    AdaptiveResearchStartingCompositionResult? value = null;
    Exception? error = null;
    try { value = call(); } catch (Exception caught) { error = caught; }
    var after = Fingerprint(root);
    if (!string.Equals(before, after, StringComparison.Ordinal))
        throw new InvalidOperationException($"Production call '{test.Item1}' changed canonical inputs.");
    rows.Add(new {
        Name = test.Item1,
        Phase = "Invoke",
        Mutation = "none",
        ChangedFiles = Array.Empty<object>(),
        ProfileId = test.Item3,
        CivilizationId = test.Item2,
        ContextId = test.Item4,
        BeforeFingerprint = before,
        AfterFingerprint = after,
        Result = value is null ? null : ProjectResult(value),
        Error = error is null ? null : Error(error),
    });
}

var fixture = new {
    Generator = "actual C# AdaptiveResearchStartingProfileComposer",
    CanonicalFingerprint = loadBefore,
    ReferenceProfileIds = profileIds,
    FragmentIds = composer.FragmentIds.ToArray(),
    Rows = rows,
};
Directory.CreateDirectory(Path.GetDirectoryName(output)!);
File.WriteAllText(output, JsonSerializer.Serialize(fixture, options) + Environment.NewLine);
Console.WriteLine($"generated {rows.Count} actual-source rows");
Console.WriteLine($"canonical fingerprint {loadBefore}");
return 0;
} catch (Exception error) {
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Directory.GetCurrentDirectory()}");
    Console.Error.WriteLine($"research-root={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    return 1;
}
