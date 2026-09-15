using System.Globalization;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ResearchPressureOracle <research-data> <fixture>");
    return 1;
}
try
{
    var options = new JsonSerializerOptions
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
    var root = Path.GetFullPath(args[0]);
    string Fingerprint()
    {
        using var h = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (
            var p in Directory
                .GetFiles(root, "*.json")
                .OrderBy(Path.GetFileName, StringComparer.Ordinal)
        )
        {
            h.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(p)));
            h.AppendData(File.ReadAllBytes(p));
        }
        return Convert.ToHexString(h.GetHashAndReset());
    }
    var fingerprint = Fingerprint();
    var kernel = AdaptiveResearchRuntime.LoadFromDirectory(root);
    var catalog = AdaptiveResearchPressureCatalog.LoadFromDirectory(root, kernel.Catalog);
    var runtime = new AdaptiveResearchPressureRuntime(kernel, catalog);
    object CatalogSnapshot() =>
        new
        {
            Rules = catalog.Rules.Values.ToArray(),
            MetricIndex = catalog
                .PressuresByMetricSignal.Select(p => new
                {
                    Id = p.Key,
                    PressureIds = p.Value.ToArray(),
                })
                .ToArray(),
            EventIndex = catalog
                .PressuresByEventSignal.Select(p => new
                {
                    Id = p.Key,
                    PressureIds = p.Value.ToArray(),
                })
                .ToArray(),
            catalog.RuntimePolicy,
        };
    var canonicalDynamics = File.ReadAllBytes(Path.Combine(root, "pressure_dynamics.json"));
    var canonicalPolicy = File.ReadAllBytes(
        Path.Combine(root, "research_pressure_runtime_policy.json")
    );
    var catalogCases = new List<object>();
    byte[] ChangeJson(byte[] source, Action<JsonObject> change)
    {
        var document = JsonNode.Parse(source)!.AsObject();
        change(document);
        return Encoding.UTF8.GetBytes(document.ToJsonString(options));
    }
    object CatalogCaseProjection(AdaptiveResearchPressureCatalog value, string ruleId) =>
        new
        {
            value.RuntimePolicy.StrongestSignalWeight,
            value.RuntimePolicy.MeanSignalWeight,
            Rule = value.Rules.TryGetValue(ruleId, out var rule) ? rule : null,
            DuplicateMetricIndex = value.PressuresByMetricSignal.TryGetValue(
                "fixture:duplicate-signal",
                out var pressures
            )
                ? pressures.ToArray()
                : Array.Empty<string>(),
        };
    void CatalogCase(
        string name,
        string fileName,
        byte[] changedBytes,
        string ruleId,
        bool sourceOnly = false
    )
    {
        var tempParent = Path.GetFullPath(Path.GetTempPath());
        var temp = Path.Combine(tempParent, "stellar-pressure-055-" + Guid.NewGuid().ToString("N"));
        if (Directory.Exists(temp))
            throw new InvalidOperationException("Exclusive pressure scratch directory already exists.");
        Directory.CreateDirectory(temp);
        AdaptiveResearchPressureCatalog? loaded = null;
        Exception? error = null;
        try
        {
            File.WriteAllBytes(Path.Combine(temp, "pressure_dynamics.json"), canonicalDynamics);
            File.WriteAllBytes(
                Path.Combine(temp, "research_pressure_runtime_policy.json"),
                canonicalPolicy
            );
            File.WriteAllBytes(Path.Combine(temp, fileName), changedBytes);
            try
            {
                loaded = AdaptiveResearchPressureCatalog.LoadFromDirectory(temp, kernel.Catalog);
            }
            catch (Exception e)
            {
                error = e;
            }
        }
        finally
        {
            var normalized = Path.GetFullPath(temp);
            if (
                !normalized.StartsWith(tempParent, StringComparison.OrdinalIgnoreCase)
                || !Path.GetFileName(normalized).StartsWith(
                    "stellar-pressure-055-",
                    StringComparison.Ordinal
                )
            )
                throw new InvalidOperationException("Refusing unsafe pressure scratch cleanup.");
            try
            {
                Directory.Delete(normalized, true);
            }
            catch
            {
                // Best-effort cleanup must not hide the source operation result.
            }
        }
        catalogCases.Add(
            new
            {
                Name = name,
                Input = new
                {
                    File = fileName,
                    BytesBase64 = Convert.ToBase64String(changedBytes),
                    BytesSha256 = Convert.ToHexString(SHA256.HashData(changedBytes)),
                    SourceOnly = sourceOnly,
                },
                Result = loaded is null
                    ? (JsonElement?)null
                    : Freeze(CatalogCaseProjection(loaded, ruleId)),
                Error = error is null
                    ? (JsonElement?)null
                    : Freeze(new { Type = error.GetType().Name, error.Message }),
            }
        );
    }
    foreach (var replacement in new JsonNode?[] { null, new JsonObject(), "ignored formula" })
    {
        var changed = ChangeJson(
            canonicalPolicy,
            value => value["metric_target"] = replacement?.DeepClone()
        );
        CatalogCase(
            "ignored-metric-target-" + (replacement?.GetValueKind().ToString() ?? "Null"),
            "research_pressure_runtime_policy.json",
            changed,
            "energy_shortage"
        );
    }
    var missingSignals = ChangeJson(
        canonicalDynamics,
        value =>
        {
            var rule = value["rules"]!["energy_shortage"]!.AsObject();
            rule.Remove("metric_signals");
            rule.Remove("event_signals");
        }
    );
    CatalogCase(
        "optional-signal-arrays-missing",
        "pressure_dynamics.json",
        missingSignals,
        "energy_shortage"
    );
    var duplicateSignals = ChangeJson(
        canonicalDynamics,
        value =>
        {
            value["rules"]!["energy_shortage"]!["metric_signals"] = new JsonArray(
                "fixture:duplicate-signal",
                "fixture:duplicate-signal"
            );
        }
    );
    CatalogCase(
        "duplicate-signals-retained-index-deduped",
        "pressure_dynamics.json",
        duplicateSignals,
        "energy_shortage"
    );
    var dynamicsText = Encoding.UTF8.GetString(canonicalDynamics);
    using (var dynamicsDocument = JsonDocument.Parse(canonicalDynamics))
    {
        var firstRule = dynamicsDocument.RootElement.GetProperty("rules").EnumerateObject().First();
        var rulesName = dynamicsText.IndexOf("\"rules\"", StringComparison.Ordinal);
        var rulesOpen = dynamicsText.IndexOf('{', rulesName);
        var duplicateRuleText = dynamicsText.Insert(
            rulesOpen + 1,
            $"\n    \"{firstRule.Name}\":{firstRule.Value.GetRawText()},"
        );
        CatalogCase(
            "duplicate-rule-key",
            "pressure_dynamics.json",
            Encoding.UTF8.GetBytes(duplicateRuleText),
            firstRule.Name
        );
    }
    var policyText = Encoding.UTF8.GetString(canonicalPolicy);
    var rootOpen = policyText.IndexOf('{');
    var duplicateScalarText = policyText.Insert(
        rootOpen + 1,
        "\n  \"catalog_id\": \"fixture:wrong-first-value\","
    );
    CatalogCase(
        "duplicate-scalar-last-value-wins",
        "research_pressure_runtime_policy.json",
        Encoding.UTF8.GetBytes(duplicateScalarText),
        "energy_shortage"
    );
    var overflowPolicyText = policyText.Replace(
        "\"rise_toward_metric_target_per_year\": 40",
        "\"rise_toward_metric_target_per_year\": 1e400",
        StringComparison.Ordinal
    );
    CatalogCase(
        "overflow-number-parser-boundary",
        "research_pressure_runtime_policy.json",
        Encoding.UTF8.GetBytes(overflowPolicyText),
        "energy_shortage",
        sourceOnly: true
    );
    var unknownPressure = ChangeJson(
        canonicalDynamics,
        value =>
        {
            value["rules"]!["fixture:unknown-pressure"] = value["rules"]![
                "energy_shortage"
            ]!.DeepClone();
        }
    );
    CatalogCase(
        "unknown-pressure",
        "pressure_dynamics.json",
        unknownPressure,
        "energy_shortage"
    );
    var missingCanonicalRule = ChangeJson(
        canonicalDynamics,
        value => value["rules"]!.AsObject().Remove("energy_shortage")
    );
    CatalogCase(
        "missing-canonical-rule",
        "pressure_dynamics.json",
        missingCanonicalRule,
        "energy_shortage"
    );
    var mismatchedCatalog = ChangeJson(
        canonicalDynamics,
        value => value["catalog_id"] = "fixture:mismatched-catalog"
    );
    CatalogCase(
        "mismatched-dynamics-catalog",
        "pressure_dynamics.json",
        mismatchedCatalog,
        "energy_shortage"
    );
    var negativeDecay = ChangeJson(
        canonicalDynamics,
        value => value["rules"]!["energy_shortage"]!["decay_per_year"] = -0.25
    );
    CatalogCase(
        "negative-decay",
        "pressure_dynamics.json",
        negativeDecay,
        "energy_shortage"
    );
    var excessiveFloor = ChangeJson(
        canonicalDynamics,
        value => value["rules"]!["energy_shortage"]!["memory_floor"] = 100.25
    );
    CatalogCase(
        "memory-floor-over-100",
        "pressure_dynamics.json",
        excessiveFloor,
        "energy_shortage"
    );
    var nonpositivePolicy = ChangeJson(
        canonicalPolicy,
        value => value["response"]!["rise_toward_metric_target_per_year"] = 0
    );
    CatalogCase(
        "nonpositive-runtime-policy",
        "research_pressure_runtime_policy.json",
        nonpositivePolicy,
        "energy_shortage"
    );
    var missingMetricTarget = ChangeJson(
        canonicalPolicy,
        value => value.Remove("metric_target")
    );
    CatalogCase(
        "missing-required-metric-target",
        "research_pressure_runtime_policy.json",
        missingMetricTarget,
        "energy_shortage"
    );
    object ExpertiseSnapshot(AdaptiveResearchExpertiseState e) =>
        new { e.Revision, FieldCompetence = e.FieldCompetence.Values.ToArray(), Institutions = e.Institutions.Values.ToArray(), TacitAssets = e.TacitAssets.Values.ToArray() };
    object StateSnapshot(AdaptiveResearchCivilizationState s) =>
        new
        {
            s.CivilizationId,
            s.Revision,
            s.MaterializedViewRevision,
            s.DirectedProgramStageId,
            s.TotalEffectiveResearchLabs,
            s.AssignedEffectiveLabs,
            s.FreeEffectiveLabs,
            NodeStates = s.NodeStates.Values.ToArray(),
            Pressures = s.Pressures.Select(p => new { Id = p.Key, p.Value }).ToArray(),
            EvidenceInstances = s.EvidenceInstances.Values.ToArray(),
            CivilizationTraits = s.CivilizationTraits.ToArray(),
            ApplicabilityContexts = s
                .ApplicabilityContexts.Select(p => new { Id = p.Key, Traits = p.Value.ToArray() })
                .ToArray(),
            Capabilities = s.Capabilities.ToArray(),
            FacilityCapabilities = s.FacilityCapabilities.ToArray(),
            EnabledDeploymentEventIds = s.EnabledDeploymentEventIds.ToArray(),
            ActiveProjects = s.ActiveProjects.Values.ToArray(),
            Expertise = ExpertiseSnapshot(s.Expertise),
        };
    object SupportSnapshot(AdaptiveResearchPressureState s) =>
        new
        {
            s.Revision,
            MetricSignals = s.MetricSignals.Select(p => new { Id = p.Key, p.Value }).ToArray(),
            ActivePressureIds = s.ActivePressureIds.ToArray(),
        };
    var pressureMethods = typeof(AdaptiveResearchPressureState)
        .GetMethods(BindingFlags.Instance | BindingFlags.NonPublic)
        .GroupBy(m => (m.Name, m.GetParameters().Length))
        .ToDictionary(g => g.Key, g => g.Single());
    object? PressureInvoke(
        AdaptiveResearchPressureState state,
        string name,
        params object?[] values
    )
    {
        try
        {
            return pressureMethods[(name, values.Length)].Invoke(state, values);
        }
        catch (TargetInvocationException e) when (e.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(e.InnerException).Throw();
            return null;
        }
    }
    var lowLevel = new List<object>();
    var low = new AdaptiveResearchPressureState();
    void Low(string name, object input, Func<object?> invoke)
    {
        var fi = Freeze(input);
        var before = Freeze(SupportSnapshot(low));
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke();
        }
        catch (Exception e)
        {
            error = e;
        }
        lowLevel.Add(
            new
            {
                Name = name,
                Input = fi,
                Before = before,
                Result = result is null ? (JsonElement?)null : Freeze(result),
                Error = error is null
                    ? (JsonElement?)null
                    : Freeze(new { Type = error.GetType().Name, error.Message }),
                After = Freeze(SupportSnapshot(low)),
            }
        );
    }
    Low("missing-query", new { Op = "Get", Id = "missing" }, () => low.GetMetricSignal("missing"));
    Low(
        "set-a",
        new
        {
            Op = "Set",
            Id = "metric:a",
            Value = .5,
        },
        () => PressureInvoke(low, "SetMetricSignal", "metric:a", .5)
    );
    Low(
        "epsilon-noop",
        new
        {
            Op = "Set",
            Id = "metric:a",
            Value = .50000001,
        },
        () => PressureInvoke(low, "SetMetricSignal", "metric:a", .50000001)
    );
    Low(
        "activate-a",
        new { Op = "Activate", Id = "pressure:a" },
        () => PressureInvoke(low, "ActivatePressure", "pressure:a")
    );
    Low(
        "activate-b",
        new { Op = "Activate", Id = "pressure:b" },
        () => PressureInvoke(low, "ActivatePressure", "pressure:b")
    );
    Low(
        "remove-a",
        new
        {
            Op = "Set",
            Id = "metric:a",
            Value = 0.0,
        },
        () => PressureInvoke(low, "SetMetricSignal", "metric:a", 0.0)
    );
    Low(
        "set-b-reuses-slot",
        new
        {
            Op = "Set",
            Id = "metric:b",
            Value = .25,
        },
        () => PressureInvoke(low, "SetMetricSignal", "metric:b", .25)
    );
    Low(
        "deactivate-a",
        new { Op = "Deactivate", Id = "pressure:a" },
        () => PressureInvoke(low, "DeactivatePressure", "pressure:a")
    );
    Low(
        "activate-c-reuses-slot",
        new { Op = "Activate", Id = "pressure:c" },
        () => PressureInvoke(low, "ActivatePressure", "pressure:c")
    );
    foreach (
        var (name, value) in new[]
        {
            ("negative", -1.0),
            ("nan", double.NaN),
            ("infinity", double.PositiveInfinity),
        }
    )
        Low(
            "set-invalid-" + name,
            new
            {
                Op = "Set",
                Id = "metric:invalid",
                Value = value,
            },
            () => PressureInvoke(low, "SetMetricSignal", "metric:invalid", value)
        );

    var metricSignals = catalog.PressuresByMetricSignal.Keys.Take(3).ToArray();
    var eventSignal = catalog.PressuresByEventSignal.Keys.First();
    var zeroFloorRule = catalog.Rules.Values.First(r =>
        r.MemoryFloor <= 0 && r.MetricSignalIds.Count > 0
    );
    var zeroFloorMetric = zeroFloorRule.MetricSignalIds[0];
    var sequence = new List<object>();
    var state = kernel.CreateCivilizationState("fixture:pressure");
    void Step(string name, object input, Func<object?> invoke)
    {
        var fi = Freeze(input);
        var supportBefore = Freeze(SupportSnapshot(runtime.GetSupportState(state)));
        var stateBefore = Freeze(StateSnapshot(state));
        object? result = null;
        Exception? error = null;
        try
        {
            result = invoke();
        }
        catch (Exception e)
        {
            error = e;
        }
        sequence.Add(
            new
            {
                Name = name,
                Input = fi,
                StateBefore = stateBefore,
                SupportBefore = supportBefore,
                Result = result is null ? (JsonElement?)null : Freeze(result),
                Error = error is null
                    ? (JsonElement?)null
                    : Freeze(new { Type = error.GetType().Name, error.Message }),
                StateAfter = Freeze(StateSnapshot(state)),
                SupportAfter = Freeze(SupportSnapshot(runtime.GetSupportState(state))),
            }
        );
    }
    Step(
        "metric-first",
        new
        {
            Op = "Metric",
            Id = metricSignals[0],
            Value = .8,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, metricSignals[0], .8);
            return true;
        }
    );
    Step(
        "metric-epsilon-noop",
        new
        {
            Op = "Metric",
            Id = metricSignals[0],
            Value = .80000001,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, metricSignals[0], .80000001);
            return true;
        }
    );
    Step(
        "target",
        new { Op = "Target", Id = catalog.PressuresByMetricSignal[metricSignals[0]][0] },
        () => runtime.GetMetricTarget(state, catalog.PressuresByMetricSignal[metricSignals[0]][0])
    );
    var bulk = new Dictionary<string, double>(StringComparer.Ordinal)
    {
        { metricSignals[1], .3 },
        { metricSignals[2], .6 },
    };
    Step(
        "metric-bulk",
        new { Op = "Metrics", Values = bulk.Select(p => new { Id = p.Key, p.Value }).ToArray() },
        () =>
        {
            runtime.ReportMetricSignals(state, bulk);
            return true;
        }
    );
    var partial = new Dictionary<string, double>(StringComparer.Ordinal)
    {
        { zeroFloorMetric, .4 },
        { "missing-metric", .2 },
    };
    Step(
        "metric-bulk-partial-error",
        new { Op = "Metrics", Values = partial.Select(p => new { Id = p.Key, p.Value }).ToArray() },
        () =>
        {
            runtime.ReportMetricSignals(state, partial);
            return true;
        }
    );
    Step(
        "event-zero",
        new
        {
            Op = "Event",
            Id = eventSignal,
            Value = 0.0,
            Context = "fixture:context",
        },
        () => runtime.ReportEventSignal(state, eventSignal, 0, "fixture:context").ToArray()
    );
    Step(
        "event-positive",
        new
        {
            Op = "Event",
            Id = eventSignal,
            Value = .75,
            Context = "fixture:context",
        },
        () => runtime.ReportEventSignal(state, eventSignal, .75, "fixture:context").ToArray()
    );
    Step(
        "advance-rise",
        new
        {
            Op = "Advance",
            Years = .25,
            Context = "fixture:context",
        },
        () => runtime.Advance(state, .25, "fixture:context").ToArray()
    );
    Step(
        "metric-zero",
        new
        {
            Op = "Metric",
            Id = zeroFloorMetric,
            Value = 0.0,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, zeroFloorMetric, 0);
            return true;
        }
    );
    Step(
        "advance-decay",
        new
        {
            Op = "Advance",
            Years = 1000.0,
            Context = (string?)null,
        },
        () => runtime.Advance(state, 1000, null).ToArray()
    );
    foreach (
        var (name, years) in new[]
        {
            ("zero", 0.0),
            ("negative", -1.0),
            ("nan", double.NaN),
            ("infinity", double.PositiveInfinity),
        }
    )
        Step(
            "advance-" + name,
            new
            {
                Op = "Advance",
                Years = years,
                Context = (string?)null,
            },
            () => runtime.Advance(state, years, null).ToArray()
        );
    Step(
        "unknown-before-value",
        new
        {
            Op = "Metric",
            Id = "missing",
            Value = double.NaN,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, "missing", double.NaN);
            return true;
        }
    );
    Step(
        "blank-before-value",
        new
        {
            Op = "Metric",
            Id = "\u2003",
            Value = double.NaN,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, "\u2003", double.NaN);
            return true;
        }
    );
    Step(
        "known-metric-negative",
        new
        {
            Op = "Metric",
            Id = metricSignals[0],
            Value = -0.125,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, metricSignals[0], -0.125);
            return true;
        }
    );
    Step(
        "known-metric-nan",
        new
        {
            Op = "Metric",
            Id = metricSignals[0],
            Value = double.NaN,
        },
        () =>
        {
            runtime.ReportMetricSignal(state, metricSignals[0], double.NaN);
            return true;
        }
    );
    Step(
        "known-event-infinity",
        new
        {
            Op = "Event",
            Id = eventSignal,
            Value = double.PositiveInfinity,
            Context = "fixture:context",
        },
        () =>
            runtime
                .ReportEventSignal(
                    state,
                    eventSignal,
                    double.PositiveInfinity,
                    "fixture:context"
                )
                .ToArray()
    );
    Step(
        "unknown-target",
        new { Op = "Target", Id = "missing-pressure" },
        () => runtime.GetMetricTarget(state, "missing-pressure")
    );

    var shared = kernel.CreateCivilizationState("fixture:same");
    var sameId = kernel.CreateCivilizationState("fixture:same");
    var otherRuntime = new AdaptiveResearchPressureRuntime(kernel, catalog);
    runtime.ReportMetricSignal(shared, metricSignals[0], .9);
    var independence = new
    {
        RuntimeOneStateOne = SupportSnapshot(runtime.GetSupportState(shared)),
        RuntimeTwoStateOne = SupportSnapshot(otherRuntime.GetSupportState(shared)),
        RuntimeOneEqualIdStateTwo = SupportSnapshot(runtime.GetSupportState(sameId)),
    };
    if (Fingerprint() != fingerprint)
        throw new InvalidOperationException("Pressure replay changed canonical research data.");
    var output = new
    {
        Generator = "actual C# AdaptiveResearchPressure",
        CanonicalFingerprint = fingerprint,
        Catalog = CatalogSnapshot(),
        CatalogCases = catalogCases,
        LowLevel = lowLevel,
        Sequence = sequence,
        Controls = new
        {
            MetricSignals = metricSignals,
            EventSignal = eventSignal,
            ZeroFloorPressureId = zeroFloorRule.Id,
            ZeroFloorMetric = zeroFloorMetric,
        },
        Independence = independence,
    };
    File.WriteAllText(
        Path.GetFullPath(args[1]),
        JsonSerializer.Serialize(output, options) + Environment.NewLine
    );
    return 0;
}
catch (Exception e)
{
    Console.Error.WriteLine(
        $"ResearchPressureOracle failed: {e}\nCWD: {Environment.CurrentDirectory}\nResearch root: {Path.GetFullPath(args[0])}\nFixture: {Path.GetFullPath(args[1])}"
    );
    return 1;
}
