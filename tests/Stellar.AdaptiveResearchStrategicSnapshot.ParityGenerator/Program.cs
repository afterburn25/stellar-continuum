using System.Globalization;
using System.Reflection;
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
    var jsonOptions = new JsonSerializerOptions { WriteIndented = false };

    string Fingerprint()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(root, "*.json")
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

    object Projection(AdaptiveResearchStrategicRuntime runtime,
                      AdaptiveResearchStrategicSnapshotCodec codec,
                      AdaptiveResearchCivilizationState state)
    {
        var snapshot = codec.Capture(state);
        var support = runtime.Pressure.GetSupportState(state);
        var agenda = runtime.Agenda.GetState(state);
        return new
        {
            state.CivilizationId,
            state.Revision,
            state.MaterializedViewRevision,
            state.TotalEffectiveResearchLabs,
            NodeStates = state.NodeStates.Values.ToArray(),
            Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
            EvidenceInstances = state.EvidenceInstances.Values.ToArray(),
            Capabilities = state.Capabilities.ToArray(),
            FacilityCapabilities = state.FacilityCapabilities.ToArray(),
            EnabledDeploymentEventIds = state.EnabledDeploymentEventIds.ToArray(),
            ActiveProjects = state.ActiveProjects.Values.ToArray(),
            ExpertiseRevision = state.Expertise.Revision,
            Pressure = new
            {
                support.Revision,
                MetricSignals = support.MetricSignals.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
                ActivePressureIds = support.ActivePressureIds.ToArray(),
            },
            Agenda = new
            {
                agenda.Revision,
                Domain = agenda.DomainPriorities.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
                Field = agenda.FieldPriorities.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
                Problem = agenda.ProblemPriorities.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
                Capability = agenda.CapabilityPriorities.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
                agenda.Orientations,
                Culture = agenda.CultureAxes.Select(pair => new { Id = pair.Key, Value = pair.Value }).ToArray(),
                LastMajorReviewYear = double.IsFinite(agenda.LastMajorReviewYear)
                    ? (double?)agenda.LastMajorReviewYear : null,
                agenda.PolicyProvenance,
            },
            Snapshot = snapshot,
        };
    }

    var loadBefore = Fingerprint();
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
    var codec = new AdaptiveResearchStrategicSnapshotCodec(runtime);
    var v2 = new AdaptiveResearchSnapshotV2Codec(runtime.Authority);
    var v1 = new AdaptiveResearchSnapshotCodec(runtime.Authority.Kernel);
    var loadAfter = Fingerprint();
    if (loadBefore != loadAfter) throw new InvalidOperationException("Loading changed canonical inputs.");

    var rows = new List<object>();
    var profiles = runtime.Authority.StartingProfiles.ReferenceProfileIds.ToArray();
    var metricId = runtime.PressureCatalog.PressuresByMetricSignal.Keys.First();
    var pressureId = runtime.PressureCatalog.PressuresByMetricSignal[metricId].First();
    var domainId = runtime.Authority.Catalog.Nodes.Values.First().DomainId;
    var fieldId = runtime.Authority.ExpertiseCatalog.Fields.Keys.First();
    var capabilityId = runtime.Authority.Catalog.Capabilities.Keys.First();
    var priorityId = runtime.AgendaCatalog.Priorities.Keys.First(id => id != runtime.AgendaCatalog.RuntimePolicy.DefaultPriorityId);
    var axisId = runtime.AgendaCatalog.CultureAxes.Keys.First();

    for (var index = 0; index < profiles.Length; ++index)
    {
        var state = runtime.Authority.ComposeReferenceProfile(
            $"fixture:v3:{index}", profiles[index], $"fixture:context:{index}", 2200 + index).State;
        if (index == 0)
        {
            runtime.Pressure.ReportMetricSignal(state, metricId, .625);
            runtime.Agenda.SetDomainPriority(state, domainId, priorityId);
            runtime.Agenda.SetFieldPriority(state, fieldId, priorityId);
            runtime.Agenda.SetProblemPriority(state, pressureId, priorityId);
            runtime.Agenda.SetCapabilityPriority(state, capabilityId, priorityId);
            runtime.Agenda.SetOrientations(state, new(11, 22, 33, 44));
            runtime.Agenda.SetScientificCultureAxis(state, axisId, 77);
            runtime.Agenda.ApplyRecommendation(
                state, new(domainId, priorityId, 1, 2, 3, "fixture"), 2234.5, "fixture provenance");
        }
        var before = Fingerprint();
        Func<string> serializeCall = () => codec.Serialize(state);
        string? serialized = null;
        Exception? serializeError = null;
        try { serialized = serializeCall(); } catch (Exception error) { serializeError = error; }
        AdaptiveResearchCivilizationState? restored = null;
        Exception? restoreError = null;
        if (serialized is not null)
        {
            var ownedJson = serialized;
            Func<AdaptiveResearchCivilizationState> restoreCall = () => codec.Deserialize(ownedJson);
            try { restored = restoreCall(); } catch (Exception error) { restoreError = error; }
        }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Snapshot calls changed canonical inputs.");
        rows.Add(new
        {
            Name = "profile-" + profiles[index], Kind = "canonical-roundtrip",
            BeforeFingerprint = before, AfterFingerprint = after,
            Serialized = serialized,
            SerializeError = Error(serializeError), RestoreError = Error(restoreError),
            Original = Projection(runtime, codec, state),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
            NextMetricTarget = restored is null ? (double?)null : runtime.Pressure.GetMetricTarget(restored, pressureId),
            NextShortlist = restored is null ? null : runtime.Agenda.BuildVisibleShortlist(restored)
                .Take(3).Select(candidate => new { candidate.NodeId, candidate.UtilityScore }).ToArray(),
        });
    }

    var fallback = runtime.Authority.ComposeReferenceProfile(
        "fixture:fallback", profiles[0], "fixture:fallback:context", 2250).State;
    runtime.Authority.Kernel.SetPressure(fallback, pressureId, 12.5);

    void AddDeserialize(string name, string input, string kind = "json-deserialize")
    {
        var ownedInput = input;
        var before = Fingerprint();
        Func<AdaptiveResearchCivilizationState> call = () => codec.Deserialize(ownedInput);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Deserialize changed canonical inputs.");
        rows.Add(new
        {
            Name = name, Kind = kind, Input = ownedInput,
            BeforeFingerprint = before, AfterFingerprint = after,
            Error = Error(error),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
            NextMetricTarget = restored is null ? (double?)null : runtime.Pressure.GetMetricTarget(restored, pressureId),
            NextShortlist = restored is null ? null : runtime.Agenda.BuildVisibleShortlist(restored)
                .Take(3).Select(candidate => new { candidate.NodeId, candidate.UtilityScore }).ToArray(),
        });
    }

    AddDeserialize("schema1-fallback", v1.Serialize(fallback), "fallback");
    AddDeserialize("schema2-fallback", v2.Serialize(fallback), "fallback");

    var boundaryState = runtime.Authority.ComposeReferenceProfile(
        "fixture:boundary", profiles[0], "fixture:boundary:context", 2260).State;
    var boundary = codec.Capture(boundaryState);

    void AddRestore(string name, AdaptiveResearchStateSnapshotV3 snapshot)
    {
        var owned = snapshot;
        var before = Fingerprint();
        Func<AdaptiveResearchCivilizationState> call = () => codec.Restore(owned);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Restore changed canonical inputs.");
        rows.Add(new
        {
            Name = name, Kind = "typed-restore",
            BeforeFingerprint = before, AfterFingerprint = after,
            Error = Error(error),
            Restored = restored is null ? null : Projection(runtime, codec, restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
            NextMetricTarget = restored is null ? (double?)null : runtime.Pressure.GetMetricTarget(restored, pressureId),
            NextShortlist = restored is null ? null : runtime.Agenda.BuildVisibleShortlist(restored)
                .Take(3).Select(candidate => new { candidate.NodeId, candidate.UtilityScore }).ToArray(),
        });
    }

    var emptyPressure = new AdaptiveResearchPressureSupportSnapshot(
        new Dictionary<string, double>(StringComparer.Ordinal), Array.Empty<string>());
    var emptyAgenda = boundary.Agenda with
    {
        DomainPriorities = new Dictionary<string, string>(StringComparer.Ordinal),
        FieldPriorities = new Dictionary<string, string>(StringComparer.Ordinal),
        ProblemPriorities = new Dictionary<string, string>(StringComparer.Ordinal),
        CapabilityPriorities = new Dictionary<string, string>(StringComparer.Ordinal),
        CultureAxes = new Dictionary<string, double>(StringComparer.Ordinal),
        LastMajorReviewYear = null,
    };
    AddRestore("typed-valid-empty-support", boundary with { PressureSupport = emptyPressure, Agenda = emptyAgenda });
    AddRestore("typed-schema", boundary with { SchemaVersion = 4 });
    AddRestore("typed-catalog", boundary with { CatalogId = "wrong" });
    AddRestore("typed-catalog-before-support", boundary with
    {
        CatalogId = "wrong",
        PressureSupport = emptyPressure with { MetricSignals = new Dictionary<string, double> { ["unknown"] = 1 } },
    });
    AddRestore("typed-unknown-metric", boundary with
    {
        PressureSupport = emptyPressure with { MetricSignals = new Dictionary<string, double> { ["unknown"] = .5 } },
    });
    foreach (var pair in new[]
             {
                 ("zero", 0.0), ("negative", -1e-5), ("over-one", 1.0001),
                 ("nan", double.NaN), ("positive-infinity", double.PositiveInfinity),
             })
        AddRestore("typed-metric-" + pair.Item1, boundary with
        {
            PressureSupport = emptyPressure with
            {
                MetricSignals = new Dictionary<string, double> { [metricId] = pair.Item2 },
            },
        });
    AddRestore("typed-unknown-active-pressure", boundary with
    {
        PressureSupport = emptyPressure with { ActivePressureIds = new[] { "unknown" } },
    });
    AddRestore("typed-unknown-domain", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { DomainPriorities = new Dictionary<string, string> { ["unknown"] = priorityId } },
    });
    AddRestore("typed-invalid-domain-priority", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { DomainPriorities = new Dictionary<string, string> { [domainId] = "unknown" } },
    });
    AddRestore("typed-unknown-field", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { FieldPriorities = new Dictionary<string, string> { ["unknown"] = priorityId } },
    });
    AddRestore("typed-unknown-problem", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { ProblemPriorities = new Dictionary<string, string> { ["unknown"] = priorityId } },
    });
    AddRestore("typed-unknown-capability", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { CapabilityPriorities = new Dictionary<string, string> { ["unknown"] = priorityId } },
    });
    AddRestore("typed-invalid-orientation", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { Orientations = new(-1, 0, 0, 0) },
    });
    AddRestore("typed-unknown-axis", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { CultureAxes = new Dictionary<string, double> { ["unknown"] = 50 } },
    });
    AddRestore("typed-invalid-axis", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { CultureAxes = new Dictionary<string, double> { [axisId] = double.NaN } },
    });
    AddRestore("typed-nonfinite-review-captured-null", boundary with
    {
        PressureSupport = emptyPressure,
        Agenda = emptyAgenda with { LastMajorReviewYear = double.PositiveInfinity, PolicyProvenance = "infinity" },
    });

    var nonfiniteState = runtime.Authority.ComposeReferenceProfile(
        "fixture:nonfinite", profiles[0], "fixture:nonfinite:context", 2261).State;
    var nonfiniteAgenda = runtime.Agenda.GetState(nonfiniteState);
    var reviewField = typeof(AdaptiveResearchAgendaState).GetField(
        "<LastMajorReviewYear>k__BackingField", BindingFlags.Instance | BindingFlags.NonPublic)
        ?? throw new MissingFieldException(nameof(AdaptiveResearchAgendaState), "LastMajorReviewYear");
    reviewField.SetValue(nonfiniteAgenda, double.PositiveInfinity);
    var nonfiniteBefore = Fingerprint();
    Func<string> nonfiniteCall = () => codec.Serialize(nonfiniteState);
    string? nonfiniteSerialized = null;
    Exception? nonfiniteError = null;
    try { nonfiniteSerialized = nonfiniteCall(); } catch (Exception caught) { nonfiniteError = caught; }
    var nonfiniteAfter = Fingerprint();
    rows.Add(new
    {
        Name = "capture-nonfinite-review-as-null", Kind = "capture-nonfinite",
        BeforeFingerprint = nonfiniteBefore, AfterFingerprint = nonfiniteAfter,
        Serialized = nonfiniteSerialized, Error = Error(nonfiniteError),
    });

    var orderedDomains = runtime.Authority.Catalog.Nodes.Values.Select(node => node.DomainId)
        .Distinct(StringComparer.Ordinal).Take(2).ToArray();
    if (orderedDomains.Length == 2)
        AddRestore("typed-preserves-agenda-insertion-order", boundary with
        {
            PressureSupport = emptyPressure,
            Agenda = emptyAgenda with
            {
                DomainPriorities = new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    [orderedDomains[1]] = priorityId,
                    [orderedDomains[0]] = priorityId,
                },
            },
        });

    var validJson = JsonNode.Parse(codec.Serialize(boundaryState))!.AsObject();
    string With(Action<JsonObject> mutation)
    {
        var clone = JsonNode.Parse(validJson.ToJsonString())!.AsObject();
        mutation(clone);
        return clone.ToJsonString();
    }
    AddDeserialize("json-missing-schema", With(rootNode => rootNode.Remove("schemaVersion")));
    AddDeserialize("json-schema-string", With(rootNode => rootNode["schemaVersion"] = "3"));
    AddDeserialize("json-schema-wide", With(rootNode => rootNode["schemaVersion"] = 2147483648L));
    AddDeserialize("json-schema-fraction", With(rootNode => rootNode["schemaVersion"] = 3.5));
    AddDeserialize("json-root-null", "null");
    AddDeserialize("json-root-array", "[]");
    AddDeserialize("json-v3-null", "null", "source-null-boundary");
    AddDeserialize("json-missing-pressure-support", With(rootNode => rootNode.Remove("pressureSupport")), "source-null-boundary");
    AddDeserialize("json-null-pressure-support", With(rootNode => rootNode["pressureSupport"] = null), "source-null-boundary");
    AddDeserialize("json-metric-signals-array", With(rootNode => rootNode["pressureSupport"]!["metricSignals"] = new JsonArray()));
    AddDeserialize("json-active-ids-object", With(rootNode => rootNode["pressureSupport"]!["activePressureIds"] = new JsonObject()));
    AddDeserialize("json-null-agenda", With(rootNode => rootNode["agenda"] = null), "source-null-boundary");
    AddDeserialize("json-null-orientations", With(rootNode => rootNode["agenda"]!["orientations"] = null), "source-null-boundary");
    AddDeserialize("json-nested-schema1", With(rootNode =>
        rootNode["research"] = JsonNode.Parse(v1.Serialize(boundaryState))));
    AddDeserialize("json-nested-missing-schema", With(rootNode =>
        rootNode["research"]!.AsObject().Remove("schemaVersion")));
    AddDeserialize("json-nested-schema-string", With(rootNode =>
        rootNode["research"]!["schemaVersion"] = "2"));
    AddDeserialize("json-nested-schema-wide", With(rootNode =>
        rootNode["research"]!["schemaVersion"] = 2147483648L));
    AddDeserialize("json-nested-schema2-missing-core", With(rootNode =>
        rootNode["research"]!.AsObject().Remove("core")), "source-null-boundary");
    AddDeserialize("json-nested-schema2-missing-expertise", With(rootNode =>
        rootNode["research"]!.AsObject().Remove("expertise")), "source-null-boundary");
    AddDeserialize("json-named-nan-metric", With(rootNode =>
        rootNode["pressureSupport"]!["metricSignals"]![metricId] = "NaN"));
    AddDeserialize("json-duplicate-scalar-last-wins",
        codec.Serialize(boundaryState).Replace("\"schemaVersion\":3", "\"schemaVersion\":4,\"schemaVersion\":3"),
        "duplicate-scalar");

    var finalFingerprint = Fingerprint();
    var fixture = new
    {
        Source = "src/Game/Simulation/Research/Adaptive/AdaptiveResearchStrategicSnapshot.cs",
        CanonicalFingerprint = loadBefore,
        FinalFingerprint = finalFingerprint,
        RowCount = rows.Count,
        Rows = rows,
    };
    var parent = Path.GetDirectoryName(output) ?? throw new InvalidOperationException("Output has no parent.");
    Directory.CreateDirectory(parent);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, jsonOptions));
}
catch (Exception error)
{
    Console.Error.WriteLine(error.ToString());
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"researchRoot={diagnosticRoot}");
    Console.Error.WriteLine($"fixture={diagnosticFixture}");
    Environment.ExitCode = 1;
}
