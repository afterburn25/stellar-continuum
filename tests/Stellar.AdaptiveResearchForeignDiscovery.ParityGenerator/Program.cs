using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchForeignDiscoveryOracle <research-data> <fixture>");
    return 1;
}

try
{
    var options = new JsonSerializerOptions
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
    var rootPath = Path.GetFullPath(args[0]);
    string Fingerprint(string directory)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(directory, "*.json")
                     .OrderBy(Path.GetFileName, StringComparer.Ordinal))
        {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }
    var canonicalFingerprint = Fingerprint(rootPath);
    var authority = AdaptiveResearchAuthority.LoadFromDirectory(rootPath);
    var foreignCatalog = AdaptiveResearchForeignTechnologyCatalog.LoadFromDirectory(
        rootPath, authority.Catalog, authority.ExpertiseCatalog);
    var foreign = new AdaptiveResearchForeignTechnologyRuntime(authority, foreignCatalog);
    var catalog = AdaptiveResearchForeignDiscoveryCatalog.LoadFromDirectory(
        rootPath, authority.Catalog);
    var runtime = new AdaptiveResearchForeignDiscoveryRuntime(authority, foreign, catalog);

    object Expertise(AdaptiveResearchExpertiseState value) => new
    {
        value.Revision,
        FieldCompetence = value.FieldCompetence.Values.ToArray(),
        Institutions = value.Institutions.Values.ToArray(),
        TacitAssets = value.TacitAssets.Values.ToArray(),
    };
    object Core(AdaptiveResearchCivilizationState value) => new
    {
        value.CivilizationId,
        value.Revision,
        value.MaterializedViewRevision,
        value.DirectedProgramStageId,
        value.TotalEffectiveResearchLabs,
        value.AssignedEffectiveLabs,
        value.FreeEffectiveLabs,
        NodeStates = value.NodeStates.Values.ToArray(),
        Pressures = value.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
        EvidenceInstances = value.EvidenceInstances.Values.ToArray(),
        CivilizationTraits = value.CivilizationTraits.ToArray(),
        ApplicabilityContexts = value.ApplicabilityContexts
            .Select(pair => new { Id = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
        Capabilities = value.Capabilities.ToArray(),
        FacilityCapabilities = value.FacilityCapabilities.ToArray(),
        EnabledDeploymentEventIds = value.EnabledDeploymentEventIds.ToArray(),
        ActiveProjects = value.ActiveProjects.Values.ToArray(),
        Expertise = Expertise(value.Expertise),
    };
    object ForeignState(AdaptiveResearchCivilizationState value)
    {
        var state = foreign.GetState(value);
        return new
        {
            state.Revision,
            Assessments = state.Assessments.Values.ToArray(),
            Packages = state.Packages.Values.ToArray(),
        };
    }
    object CatalogProjection(AdaptiveResearchForeignDiscoveryCatalog value) => new
    {
        value.ObservedEvidenceState,
        value.CharacterizedEvidenceState,
        MethodRules = value.MethodRules.ToArray(),
        ParseAdaptation = new[]
        {
            "none", "conceptual_inspiration", "interface_adaptation",
            "native_derivative", "hybrid_lineage",
        }.Select(id => new
        {
            Id = id,
            Value = (int)AdaptiveResearchForeignDiscoveryCatalog.ParseAdaptation(id),
        }).ToArray(),
    };

    var state = authority.CreateCivilizationState("fixture:foreign-discovery");
    var setNode = typeof(AdaptiveResearchCivilizationState).GetMethod(
        "SetNodeState", BindingFlags.Instance | BindingFlags.NonPublic)!;
    object InvokeInternal(MethodInfo method, object argument)
    {
        try
        {
            method.Invoke(state, new[] { argument });
            return true;
        }
        catch (TargetInvocationException exception) when (exception.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            throw;
        }
    }
    var sequence = new List<object>();
    object ProjectResult(object value) => value switch
    {
        ValueTuple<ForeignTechnologyAssessmentRuntimeState,
            IReadOnlyList<ForeignResearchAwarenessEvent>> pair => new
        {
            Assessment = pair.Item1,
            AwarenessEvents = pair.Item2.ToArray(),
        },
        IReadOnlyList<ForeignResearchAwarenessEvent> events => events.ToArray(),
        _ => value,
    };
    void Step(string name, object input, Func<object?> invoke)
    {
        var frozenInput = Freeze(input);
        var coreBefore = Freeze(Core(state));
        var foreignBefore = Freeze(ForeignState(state));
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke();
        }
        catch (Exception exception)
        {
            error = exception;
        }
        sequence.Add(new
        {
            Name = name,
            Input = frozenInput,
            CoreBefore = coreBefore,
            ForeignBefore = foreignBefore,
            Result = result is null ? (JsonElement?)null : Freeze(ProjectResult(result)),
            Error = error is null ? (JsonElement?)null
                : Freeze(new { Type = error.GetType().Name, error.Message }),
            CoreAfter = Freeze(Core(state)),
            ForeignAfter = Freeze(ForeignState(state)),
        });
    }

    var constraint = foreignCatalog.ConstraintIds.First();
    var field = authority.ExpertiseCatalog.Fields.Keys.First();
    var evidenceType = authority.Catalog.EvidenceTypeIds
        .First(id => authority.Catalog.NodesByEvidence.TryGetValue(id, out var nodes) && nodes.Count > 0);
    var component = foreignCatalog.Components.Values.First(value => value.TacitAssetTypeIds.Count > 0);
    var right = foreignCatalog.Rights.First();
    Step("reevaluate-missing",
        new { Op = "Reevaluate", Reference = "foreign:missing", Context = (string?)null },
        () => runtime.Reevaluate(state, "foreign:missing"));
    Step("observe",
        new { Op = "Observe", Reference = "foreign:discovery", Lineage = "lineage:discovery",
              Confidence = .4, Year = 2100.0, Constraints = new[] { constraint, constraint },
              Context = (string?)null },
        () => runtime.Observe(state, "foreign:discovery", "lineage:discovery", .4, 2100,
            new[] { constraint, constraint }));
    Step("analysis-characterized",
        new { Op = "Analysis", Reference = "foreign:discovery",
              Understanding = (int)ForeignUnderstandingState.Characterized,
              Confidence = .8, Year = 2101.0, Constraints = Array.Empty<string>(),
              Context = (string?)null },
        () => runtime.RecordAnalysisResult(state, "foreign:discovery",
            ForeignUnderstandingState.Characterized, .8, 2101));
    Step("analysis-principle",
        new { Op = "Analysis", Reference = "foreign:discovery",
              Understanding = (int)ForeignUnderstandingState.PrincipleUnderstood,
              Confidence = .9, Year = 2102.0, Constraints = Array.Empty<string>(),
              Context = (string?)null },
        () => runtime.RecordAnalysisResult(state, "foreign:discovery",
            ForeignUnderstandingState.PrincipleUnderstood, .9, 2102));
    Step("adaptation-native-derivative",
        new { Op = "Adaptation", Reference = "foreign:discovery",
              Adaptation = (int)ForeignAdaptationState.NativeDerivative,
              Confidence = .9, Year = 2103.0, Context = (string?)null },
        () => runtime.RecordAdaptationResult(state, "foreign:discovery",
            ForeignAdaptationState.NativeDerivative, .9, 2103));
    var package = new ForeignTechnologyPackageInput(
        "package:discovery", "foreign:evidence", "lineage:evidence",
        new[] { component.Id }, new[] { right }, new[] { field },
        new[] { new ForeignTechnologyEvidenceTransfer(
            "evidence:discovery", evidenceType, "fixture", .9, .9) },
        new[] { constraint }, "fixture", .9, .9, .9, 2104, null);
    Step("acquire-evidence", new { Op = "Acquire", Package = package },
        () => runtime.AcquirePackage(state, package));
    Step("reevaluate-noop",
        new { Op = "Reevaluate", Reference = "foreign:evidence", Context = (string?)null },
        () => runtime.Reevaluate(state, "foreign:evidence"));

    var tieEvidenceType = "captured_foreign_device";
    Step("tie-direct-observe",
        new { Op = "ForeignObserve", Reference = "foreign:tie", Lineage = "lineage:tie",
              Confidence = .7, Year = 2105.0, Constraints = Array.Empty<string>() },
        () => foreign.Observe(state, "foreign:tie", "lineage:tie", .7, 2105));
    Step("tie-direct-analysis",
        new { Op = "ForeignAnalysis", Reference = "foreign:tie",
              Understanding = (int)ForeignUnderstandingState.Characterized,
              Confidence = .8, Year = 2105.5, Constraints = Array.Empty<string>() },
        () => foreign.RecordAnalysisResult(state, "foreign:tie",
            ForeignUnderstandingState.Characterized, .8, 2105.5));
    var tiePackage = package with
    {
        PackageId = "package:tie",
        ForeignTechnologyReference = "foreign:tie",
        SourceLineageReference = "lineage:tie",
        Evidence = new[] { new ForeignTechnologyEvidenceTransfer(
            "evidence:tie", tieEvidenceType, "fixture:tie", .9, .9) },
        AcquiredYear = 2106,
    };
    Step("acquire-tie-first-evidence-reason", new { Op = "Acquire", Package = tiePackage },
        () => runtime.AcquirePackage(state, tiePackage));

    var protectedNode = catalog.MethodRules[0].NodeIds[0];
    foreach (var (name, maturity, resolution) in new[]
    {
        ("protected-experimental", ResearchMaturity.Experimental, (string?)null),
        ("protected-mature", ResearchMaturity.Mature, "mature_history"),
        ("protected-archive", ResearchMaturity.Archived, "superseded"),
        ("reopen-disproven", ResearchMaturity.Archived, "DiSpRoVeN"),
    })
    {
        var node = new ResearchNodeRuntimeState(protectedNode, maturity, resolution, 7.5, 31.25, 0);
        Step(name + "-seed", new { Op = "SetNode", Value = node },
            () => InvokeInternal(setNode, node));
        Step(name,
            new { Op = "Reevaluate", Reference = "foreign:discovery", Context = (string?)null },
            () => runtime.Reevaluate(state, "foreign:discovery"));
    }

    var partialState = authority.CreateCivilizationState("fixture:discovery-partial");
    foreign.Observe(partialState, "foreign:partial", "lineage:partial", .7, 2110);
    var invalidRule = new ForeignResearchMethodCandidateRule(
        ForeignDiscoveryTriggerAxis.Understanding,
        (int)ForeignUnderstandingState.Characterized,
        new[] { protectedNode },
        (ResearchMaturity)999);
    var discoveryConstructor = typeof(AdaptiveResearchForeignDiscoveryCatalog)
        .GetConstructors(BindingFlags.Instance | BindingFlags.NonPublic).Single();
    var invalidCatalog = (AdaptiveResearchForeignDiscoveryCatalog)discoveryConstructor.Invoke(
        new object[]
        {
            ResearchMaturity.Rumored,
            ResearchMaturity.Hypothesized,
            new List<ForeignResearchMethodCandidateRule> { invalidRule }.AsReadOnly(),
        });
    var invalidRuntime = new AdaptiveResearchForeignDiscoveryRuntime(
        authority, foreign, invalidCatalog);
    var partialCoreBefore = Freeze(Core(partialState));
    var partialForeignBefore = Freeze(ForeignState(partialState));
    object? partialResult = null;
    Exception? partialError = null;
    try
    {
        partialResult = invalidRuntime.RecordAnalysisResult(
            partialState, "foreign:partial",
            ForeignUnderstandingState.Characterized, .8, 2111);
    }
    catch (Exception exception)
    {
        partialError = exception;
    }
    var partialDelegation = new
    {
        Input = Freeze(new
        {
            Setup = new { Lineage = "lineage:partial", Confidence = .7, Year = 2110.0 },
            Reference = "foreign:partial",
            Understanding = (int)ForeignUnderstandingState.Characterized,
            Confidence = .8,
            Year = 2111.0,
            Rule = invalidRule,
        }),
        CoreBefore = partialCoreBefore,
        ForeignBefore = partialForeignBefore,
        Result = partialResult is null ? (JsonElement?)null
            : Freeze(ProjectResult(partialResult)),
        Error = partialError is null ? (JsonElement?)null
            : Freeze(new { Type = partialError.GetType().Name, partialError.Message }),
        CoreAfter = Freeze(Core(partialState)),
        ForeignAfter = Freeze(ForeignState(partialState)),
    };

    byte[] canonicalPolicy = File.ReadAllBytes(
        Path.Combine(rootPath, "foreign_research_materialization_policy.json"));
    byte[] ChangeJson(Action<JsonObject> change)
    {
        var value = JsonNode.Parse(canonicalPolicy)!.AsObject();
        change(value);
        return Encoding.UTF8.GetBytes(value.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
    }
    var catalogCases = new List<object>();
    void CatalogCase(string name, byte[] changedBytes)
    {
        var parent = Path.GetFullPath(Path.GetTempPath());
        var scratch = Path.Combine(parent, "stellar-discovery-059-" + Guid.NewGuid().ToString("N"));
        if (Directory.Exists(scratch))
            throw new InvalidOperationException("Exclusive discovery scratch already exists.");
        Directory.CreateDirectory(scratch);
        AdaptiveResearchForeignDiscoveryCatalog? loaded = null;
        Exception? error = null;
        string? before = null;
        string? after = null;
        try
        {
            File.WriteAllBytes(Path.Combine(scratch, "foreign_research_materialization_policy.json"), changedBytes);
            before = Fingerprint(scratch);
            try
            {
                loaded = AdaptiveResearchForeignDiscoveryCatalog.LoadFromDirectory(scratch, authority.Catalog);
            }
            catch (Exception exception)
            {
                error = exception;
            }
            after = Fingerprint(scratch);
        }
        finally
        {
            var normalized = Path.GetFullPath(scratch);
            if (!normalized.StartsWith(parent, StringComparison.OrdinalIgnoreCase) ||
                !Path.GetFileName(normalized).StartsWith("stellar-discovery-059-", StringComparison.Ordinal))
                throw new InvalidOperationException("Refusing unsafe discovery scratch cleanup.");
            try { Directory.Delete(normalized, true); } catch { }
        }
        catalogCases.Add(new
        {
            Name = name,
            Input = new
            {
                BytesBase64 = Convert.ToBase64String(changedBytes),
                BytesSha256 = Convert.ToHexString(SHA256.HashData(changedBytes)),
                BeforeFingerprint = before,
                AfterFingerprint = after,
            },
            Result = loaded is null ? (JsonElement?)null : Freeze(CatalogProjection(loaded)),
            Error = error is null ? (JsonElement?)null
                : Freeze(new { Type = error.GetType().Name, error.Message }),
        });
    }
    CatalogCase("catalog-id", ChangeJson(value => value["catalog_id"] = "wrong"));
    CatalogCase("catalog-id-unicode-whitespace", ChangeJson(value => value["catalog_id"] = "\u2003"));
    CatalogCase("observed-state-contract", ChangeJson(value =>
        value["evidence_index_rules"]!["observed_minimum_state"] = "hypothesized"));
    CatalogCase("unknown-axis", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["trigger_axis"] = "unknown"));
    CatalogCase("unknown-axis-before-missing-minimum", ChangeJson(value =>
    {
        var rule = value["cross_lineage_method_candidates"]![0]!.AsObject();
        rule["trigger_axis"] = "unknown-mixed";
        rule.Remove("minimum_state");
    }));
    CatalogCase("unknown-understanding", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["minimum_state"] = "unknown-rank"));
    CatalogCase("invalid-awareness", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["awareness_state"] = "investigable"));
    CatalogCase("null-node", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["node_ids"]![0] = null));
    CatalogCase("unknown-node", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["node_ids"]![0] = "missing:node"));
    CatalogCase("node-null-before-unknown-validation", ChangeJson(value =>
    {
        var nodes = new JsonArray();
        nodes.Add(JsonValue.Create("missing:node"));
        nodes.Add((JsonNode?)null);
        value["cross_lineage_method_candidates"]![0]!["node_ids"] = nodes;
    }));
    CatalogCase("duplicate-node-dedup", ChangeJson(value =>
    {
        var nodes = value["cross_lineage_method_candidates"]![0]!["node_ids"]!.AsArray();
        nodes.Add(nodes[0]!.DeepClone());
    }));
    CatalogCase("rules-object-kind", ChangeJson(value =>
        value["cross_lineage_method_candidates"] = new JsonObject()));
    CatalogCase("rules-false-kind", ChangeJson(value =>
        value["cross_lineage_method_candidates"] = false));
    CatalogCase("node-ids-false-kind", ChangeJson(value =>
        value["cross_lineage_method_candidates"]![0]!["node_ids"] = false));

    if (Fingerprint(rootPath) != canonicalFingerprint)
        throw new InvalidOperationException("Discovery oracle changed canonical research data.");
    var output = new
    {
        Generator = "actual C# AdaptiveResearchForeignDiscovery",
        CanonicalFingerprint = canonicalFingerprint,
        Catalog = CatalogProjection(catalog),
        CatalogCases = catalogCases,
        Sequence = sequence,
        PartialDelegation = partialDelegation,
        Controls = new { Constraint = constraint, Field = field, EvidenceType = evidenceType,
                         TieEvidenceType = tieEvidenceType, Component = component.Id,
                         Right = right, ProtectedNode = protectedNode },
    };
    File.WriteAllText(Path.GetFullPath(args[1]),
        JsonSerializer.Serialize(output, options) + Environment.NewLine);
    return 0;
}
catch (Exception exception)
{
    Console.Error.WriteLine(
        $"ResearchForeignDiscoveryOracle failed: {exception}\nCWD: {Environment.CurrentDirectory}\nResearch root: {Path.GetFullPath(args[0])}\nFixture: {Path.GetFullPath(args[1])}");
    return 1;
}
