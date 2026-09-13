using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
var diagnosticRoot = args.Length > 0 ? args[0] : "<missing>";
var diagnosticFixture = args.Length > 1 ? args[1] : "<missing>";
try
{
    if (args.Length != 2)
        throw new ArgumentException("Expected canonical research directory and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);

    string Fingerprint(string directory)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(directory, "*.json")
                     .OrderBy(path => Path.GetFileName(path), StringComparer.Ordinal))
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    object Error(Exception? error) => error is null
        ? null!
        : new { Type = error.GetType().Name, error.Message };
    object Number(double value) => double.IsNaN(value) ? "NaN"
        : value == double.PositiveInfinity ? "Infinity"
        : value == double.NegativeInfinity ? "-Infinity" : value;
    var fixtureJsonOptions = new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        Converters = { new System.Text.Json.Serialization.JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    object SnapshotInput(AdaptiveResearchStateSnapshotV5 snapshot) => new
    {
        snapshot.SchemaVersion,
        snapshot.CatalogId,
        Research = JsonSerializer.SerializeToNode(snapshot.Research, fixtureJsonOptions),
        Outcomes = new
        {
            Summaries = snapshot.Outcomes.Summaries.Select(value => new
            {
                value.NodeId, value.Attempts, value.Setbacks,
                value.PartialSuccesses, value.Refinements, value.Disproofs,
                value.Anomalies, value.Hazards, value.SideDiscoveries,
                value.LastOutcomeId, LastOutcomeYear = Number(value.LastOutcomeYear),
            }).ToArray(),
            RecentRecords = snapshot.Outcomes.RecentRecords.Select(value => new
            {
                value.Sequence, value.NodeId, value.CheckpointId,
                value.AttemptIndex, Outcome = (int)value.Outcome,
                value.SideDiscoveryNodeId, Year = Number(value.Year),
                value.Explanation,
            }).ToArray(),
        },
    };

    object Projection(AdaptiveResearchStrategicRuntime runtime,
                      AdaptiveResearchOutcomeSnapshotCodec codec,
                      AdaptiveResearchCivilizationState state)
    {
        var outcome = runtime.Outcomes.GetState(state);
        return new
        {
            state.CivilizationId,
            state.Revision,
            state.MaterializedViewRevision,
            ExpertiseRevision = state.Expertise.Revision,
            NodeRevisions = state.NodeStates.Values.Select(value => new { value.NodeId, value.Revision }).ToArray(),
            ProjectRevisions = state.ActiveProjects.Values.Select(value => new { value.NodeId, value.Revision }).ToArray(),
            OutcomeRevision = outcome.Revision,
            Summaries = outcome.Summaries.Values.Select(value => new
            {
                value.NodeId, value.Attempts, value.Setbacks,
                value.PartialSuccesses, value.Refinements, value.Disproofs,
                value.Anomalies, value.Hazards, value.SideDiscoveries,
                value.LastOutcomeId, LastOutcomeYear = Number(value.LastOutcomeYear),
            }).ToArray(),
            RecentRecords = outcome.RecentRecords.Select(value => new
            {
                value.Sequence, value.NodeId, value.CheckpointId,
                value.AttemptIndex, value.Outcome, value.SideDiscoveryNodeId,
                Year = Number(value.Year), value.Explanation,
            }).ToArray(),
        };
    }

    var beforeLoad = Fingerprint(root);
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var codec = new AdaptiveResearchOutcomeSnapshotCodec(runtime);
    var v4 = new AdaptiveResearchForeignTechnologySnapshotCodec(runtime);
    var v3 = new AdaptiveResearchStrategicSnapshotCodec(runtime);
    var v2 = new AdaptiveResearchSnapshotV2Codec(runtime.Authority);
    var v1 = new AdaptiveResearchSnapshotCodec(runtime.Authority.Kernel);
    var afterLoad = Fingerprint(root);
    if (beforeLoad != afterLoad) throw new InvalidOperationException("Loading changed canonical inputs.");

    var rows = new List<object>();
    var profiles = runtime.Authority.StartingProfiles.ReferenceProfileIds.ToArray();
    foreach (var (profile, index) in profiles.Select((value, index) => (value, index)))
    {
        var state = runtime.Authority.ComposeReferenceProfile(
            $"fixture:v5:{index}", profile, $"fixture:context:{index}", 2300 + index).State;
        var before = Fingerprint(root);
        Func<string> serializeCall = () => codec.Serialize(state);
        string? serialized = null;
        Exception? serializeError = null;
        try { serialized = serializeCall(); } catch (Exception error) { serializeError = error; }
        AdaptiveResearchCivilizationState? restored = null;
        Exception? restoreError = null;
        if (serialized is not null)
        {
            var owned = serialized;
            Func<AdaptiveResearchCivilizationState> restoreCall = () => codec.Deserialize(owned);
            try { restored = restoreCall(); } catch (Exception error) { restoreError = error; }
        }
        var after = Fingerprint(root);
        rows.Add(new
        {
            Name = "profile-" + profile, Kind = "canonical-roundtrip",
            BeforeFingerprint = before, AfterFingerprint = after,
            Serialized = serialized, SerializeError = Error(serializeError),
            RestoreError = Error(restoreError),
            Original = Projection(runtime, codec, state),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
        });
    }

    var fallback = runtime.Authority.ComposeReferenceProfile(
        "fixture:fallback", profiles[0], "fixture:fallback:context", 2310).State;
    void AddDeserialize(string name, string input, string kind = "json-deserialize")
    {
        var owned = input;
        var before = Fingerprint(root);
        Func<AdaptiveResearchCivilizationState> call = () => codec.Deserialize(owned);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint(root);
        rows.Add(new
        {
            Name = name, Kind = kind, Input = owned,
            BeforeFingerprint = before, AfterFingerprint = after,
            Error = Error(error),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
        });
    }
    AddDeserialize("schema1-fallback", v1.Serialize(fallback), "fallback");
    AddDeserialize("schema2-fallback", v2.Serialize(fallback), "fallback");
    AddDeserialize("schema3-fallback", v3.Serialize(fallback), "fallback");
    AddDeserialize("schema4-fallback", v4.Serialize(fallback), "fallback");

    var projectState = runtime.Authority.ComposeReferenceProfile(
        "fixture:outcome", profiles[0], "fixture:outcome:context", 2320).State;
    var candidate = runtime.Authority.BuildView(projectState).VisibleNodes.First(value =>
        value.State == ResearchMaturity.Investigable && value.Blockers.Count == 0 && value.MinimumLabs is not null);
    var start = runtime.Authority.StartDirectedResearch(
        projectState, candidate.NodeId, candidate.MinimumLabs!.Value, "fixture:outcome:context");
    if (!start.Accepted) throw new InvalidOperationException(start.Message);
    var baseSnapshot = codec.Capture(projectState);
    var sideNode = runtime.Authority.Catalog.Nodes.Keys.First(id => id != candidate.NodeId);
    var summary = new ResearchOutcomeNodeSummary(
        candidate.NodeId, 3, 1, 1, 0, 0, 0, 0, 0, "partial_success", 2319);
    var otherSummary = new ResearchOutcomeNodeSummary(
        sideNode, 1, 0, 0, 0, 0, 1, 0, 0, "anomalous_result", 2318);
    var records = new[]
    {
        new ResearchOutcomeHistoryRecord(2, candidate.NodeId, "checkpoint:b", 2,
            ResearchOutcomeKind.PartialSuccess, null, 2319, "second"),
        new ResearchOutcomeHistoryRecord(1, sideNode, "checkpoint:a", 1,
            ResearchOutcomeKind.AnomalousResult, null, 2318, "first"),
    };

    void AddRestore(string name, AdaptiveResearchStateSnapshotV5 snapshot,
                    bool planNext = false)
    {
        var owned = snapshot;
        var before = Fingerprint(root);
        Func<AdaptiveResearchCivilizationState> call = () => codec.Restore(owned);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        PlannedResearchOutcome? next = null;
        if (restored is not null && planNext)
            next = runtime.Outcomes.PlanOutcome(
                restored, candidate.NodeId, "fixture-seed", "checkpoint:next",
                "fixture:outcome:context");
        string? restoredSerialized = null;
        Exception? serializeError = null;
        if (restored is not null)
        {
            Func<string> serializeCall = () => codec.Serialize(restored);
            try { restoredSerialized = serializeCall(); }
            catch (Exception caught) { serializeError = caught; }
        }
        var after = Fingerprint(root);
        rows.Add(new
        {
            Name = name, Kind = "typed-restore",
            BeforeFingerprint = before, AfterFingerprint = after,
            Input = SnapshotInput(owned),
            Error = Error(error),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
            RestoredSerialized = restoredSerialized,
            SerializeError = Error(serializeError),
            NextOutcome = next,
        });
    }

    AddRestore("typed-valid-unsorted-history", baseSnapshot with
    {
        Outcomes = new AdaptiveResearchOutcomeSnapshot(
            new[] { summary, otherSummary }, records),
    }, true);
    AddRestore("typed-duplicate-summary-last-replaces-slot", baseSnapshot with
    {
        Outcomes = new AdaptiveResearchOutcomeSnapshot(
            new[] { summary, summary with { Attempts = 7, LastOutcomeYear = 2320 } },
            Array.Empty<ResearchOutcomeHistoryRecord>()),
    }, true);
    AddRestore("typed-schema", baseSnapshot with { SchemaVersion = 6 });
    AddRestore("typed-catalog", baseSnapshot with { CatalogId = "wrong" });
    AddRestore("typed-nested-schema3", baseSnapshot with
    {
        Research = baseSnapshot.Research with { SchemaVersion = 3 },
    });
    AddRestore("typed-summary-unknown-node", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { NodeId = "unknown" } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-summary-negative-counter", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { Setbacks = -1 } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-summary-unknown-outcome", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { LastOutcomeId = "unknown" } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-summary-nan-year", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { LastOutcomeYear = double.NaN } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-summary-positive-infinity-year", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { LastOutcomeYear = double.PositiveInfinity } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-summary-negative-infinity-accepted", baseSnapshot with
    {
        Outcomes = new(new[] { summary with { LastOutcomeYear = double.NegativeInfinity } }, Array.Empty<ResearchOutcomeHistoryRecord>()),
    });
    AddRestore("typed-history-duplicate-sequence", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(),
            new[] { records[0] with { Sequence = 1 }, records[1] }),
    });
    AddRestore("typed-history-zero-sequence", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), new[] { records[0] with { Sequence = 0 } }),
    });
    AddRestore("typed-history-unknown-node", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), new[] { records[0] with { NodeId = "unknown" } }),
    });
    AddRestore("typed-history-negative-attempt", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), new[] { records[0] with { AttemptIndex = -1 } }),
    });
    AddRestore("typed-history-nan-year", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), new[] { records[0] with { Year = double.NaN } }),
    });
    AddRestore("typed-history-unknown-side", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), new[] { records[0] with { SideDiscoveryNodeId = "unknown" } }),
    });
    var excess = Enumerable.Range(1, runtime.OutcomeCatalog.Policy.MaxRecentOutcomeRecords + 1)
        .Select(index => records[0] with { Sequence = index, AttemptIndex = index }).ToArray();
    AddRestore("typed-history-over-global-capacity", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(), excess),
    });
    AddRestore("typed-history-unnamed-enum-accepted", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(),
            new[] { records[0] with { Sequence = 1, Outcome = (ResearchOutcomeKind)99 } }),
    });
    AddRestore("typed-history-sequence-max-source-wrap", baseSnapshot with
    {
        Outcomes = new(Array.Empty<ResearchOutcomeNodeSummary>(),
            new[] { records[0] with { Sequence = long.MaxValue } }),
    });

    var validJson = JsonNode.Parse(codec.Serialize(projectState))!.AsObject();
    string With(Action<JsonObject> mutation)
    {
        var clone = JsonNode.Parse(validJson.ToJsonString())!.AsObject();
        mutation(clone);
        return clone.ToJsonString();
    }
    AddDeserialize("json-missing-schema", With(value => value.Remove("schemaVersion")));
    AddDeserialize("json-schema-string", With(value => value["schemaVersion"] = "5"));
    AddDeserialize("json-schema-wide", With(value => value["schemaVersion"] = 2147483648L));
    AddDeserialize("json-root-null", "null");
    AddDeserialize("json-null-outcomes", With(value => value["outcomes"] = null), "source-null-boundary");
    AddDeserialize("json-missing-summaries", With(value => value["outcomes"]!.AsObject().Remove("summaries")), "source-null-boundary");
    AddDeserialize("json-null-records", With(value => value["outcomes"]!["recentRecords"] = null), "source-null-boundary");
    AddDeserialize("json-summary-object", With(value => value["outcomes"]!["summaries"] = new JsonObject()));
    AddDeserialize("json-records-object", With(value => value["outcomes"]!["recentRecords"] = new JsonObject()));
    AddDeserialize("json-outcome-numeric-string", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = "1";
    }));
    AddDeserialize("json-outcome-plus-numeric-string", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = "+1";
    }));
    AddDeserialize("json-outcome-case-insensitive-name", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = "PaRtIaLsUcCeSs";
    }));
    AddDeserialize("json-outcome-comma-names", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = "progress, setback";
    }));
    AddDeserialize("json-outcome-ascii-whitespace-plus", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = " +1 ";
    }));
    AddDeserialize("json-outcome-unicode-whitespace-plus-rejected", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = "\u2003+1\u2003";
    }));
    AddDeserialize("json-outcome-unnamed-integer", With(value =>
    {
        value["outcomes"]!["recentRecords"] = JsonSerializer.SerializeToNode(new[] { records[0] }, fixtureJsonOptions);
        value["outcomes"]!["recentRecords"]![0]!["outcome"] = 99;
    }));
    AddDeserialize("json-nested-schema3", With(value => value["research"]!["schemaVersion"] = 3));
    AddDeserialize("json-nested-missing-schema", With(value => value["research"]!.AsObject().Remove("schemaVersion")));

    var ownedParent = Path.GetFullPath(Path.Combine(Path.GetTempPath(),
        "stellar-research-outcome-snapshot-063"));
    Directory.CreateDirectory(ownedParent);
    var ownedRoot = Path.GetFullPath(Path.Combine(ownedParent,
        "case-" + Guid.NewGuid().ToString("N")));
    if (!string.Equals(Path.GetDirectoryName(ownedRoot), ownedParent,
            StringComparison.OrdinalIgnoreCase) || Directory.Exists(ownedRoot))
        throw new InvalidOperationException("Owned scratch path validation failed.");
    Directory.CreateDirectory(ownedRoot);
    try
    {
        foreach (var sourcePath in Directory.GetFiles(root, "*.json"))
            File.Copy(sourcePath, Path.Combine(ownedRoot, Path.GetFileName(sourcePath)));
        const string changedRelativePath = "research_outcome_runtime_policy.json";
        if (changedRelativePath.Contains("..", StringComparison.Ordinal) ||
            Path.IsPathRooted(changedRelativePath))
            throw new InvalidOperationException("Changed relative path is unsafe.");
        var changedPath = Path.GetFullPath(Path.Combine(ownedRoot, changedRelativePath));
        if (!string.Equals(Path.GetDirectoryName(changedPath), ownedRoot,
                StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Changed file escaped owned scratch root.");
        var changedDocument = JsonNode.Parse(File.ReadAllText(changedPath))!.AsObject();
        changedDocument["history"]!["max_recent_outcome_records"] = -1;
        var changedBytes = Encoding.UTF8.GetBytes(changedDocument.ToJsonString());
        using (var changedOutput = new FileStream(changedPath, FileMode.Create,
                   FileAccess.Write, FileShare.None))
            changedOutput.Write(changedBytes);
        if (!File.ReadAllBytes(changedPath).SequenceEqual(changedBytes))
            throw new IOException("Changed scratch bytes did not persist exactly.");

        var scratchBeforeLoad = Fingerprint(ownedRoot);
        var scratchRuntime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(ownedRoot);
        var scratchCodec = new AdaptiveResearchOutcomeSnapshotCodec(scratchRuntime);
        if (Fingerprint(ownedRoot) != scratchBeforeLoad)
            throw new InvalidOperationException("Mutated runtime loading changed scratch inputs.");
        var scratchProfile = scratchRuntime.Authority.StartingProfiles.ReferenceProfileIds.First();
        var scratchState = scratchRuntime.Authority.ComposeReferenceProfile(
            "fixture:negative-capacity", scratchProfile,
            "fixture:negative-capacity:context", 2330).State;
        var scratchSnapshot = scratchCodec.Capture(scratchState);
        var scratchNode = scratchRuntime.Authority.Catalog.Nodes.Keys.First();
        scratchSnapshot = scratchSnapshot with
        {
            Outcomes = new AdaptiveResearchOutcomeSnapshot(
                Array.Empty<ResearchOutcomeNodeSummary>(),
                new[] { new ResearchOutcomeHistoryRecord(
                    1, scratchNode, "checkpoint:negative-capacity", 0,
                    ResearchOutcomeKind.Progress, null, 2330, "capacity") }),
        };
        var scratchBeforeCall = Fingerprint(ownedRoot);
        Func<AdaptiveResearchCivilizationState> scratchCall = () =>
            scratchCodec.Restore(scratchSnapshot);
        AdaptiveResearchCivilizationState? scratchRestored = null;
        Exception? scratchError = null;
        try { scratchRestored = scratchCall(); }
        catch (Exception caught) { scratchError = caught; }
        var scratchAfterCall = Fingerprint(ownedRoot);
        rows.Add(new
        {
            Name = "typed-negative-global-capacity",
            Kind = "mutated-catalog-typed-restore",
            BaseFingerprint = beforeLoad,
            ChangedRelativePath = changedRelativePath,
            ChangedBytesBase64 = Convert.ToBase64String(changedBytes),
            BeforeFingerprint = scratchBeforeCall,
            AfterFingerprint = scratchAfterCall,
            Input = SnapshotInput(scratchSnapshot),
            Error = Error(scratchError),
            Restored = scratchRestored is null ? null :
                Projection(scratchRuntime, scratchCodec, scratchRestored),
        });
    }
    finally
    {
        var resolvedRoot = Path.GetFullPath(ownedRoot);
        var resolvedParent = Path.GetFullPath(Path.GetDirectoryName(resolvedRoot)
            ?? throw new InvalidOperationException("Owned scratch has no parent."));
        if (!string.Equals(resolvedParent, ownedParent,
                StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Refusing unsafe scratch cleanup.");
        if (Directory.Exists(resolvedRoot))
            Directory.Delete(resolvedRoot, recursive: true);
    }

    var fixture = new
    {
        Source = "src/Game/Simulation/Research/Adaptive/AdaptiveResearchOutcomeSnapshot.cs",
        CanonicalFingerprint = beforeLoad,
        FinalFingerprint = Fingerprint(root),
        RowCount = rows.Count,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)
        ?? throw new InvalidOperationException("Output has no parent."));
    File.WriteAllText(output, JsonSerializer.Serialize(fixture));
}
catch (Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"researchRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode = 1;
}
