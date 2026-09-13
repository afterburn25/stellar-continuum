using System.Globalization;
using System.Security.Cryptography;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Simulation.Research.Adaptive;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 2) {
  Console.Error.WriteLine(
      "usage: ResearchForeignTechnologyOracle <research-data> <fixture>");
  return 1;
}
try {
  var options = new JsonSerializerOptions {
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals
  };
    JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, options);
    var root = Path.GetFullPath(args[0]);
    string Fingerprint(string directory) {
      using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
      foreach (var path in Directory.GetFiles(directory, "*.json")
                   .OrderBy(Path.GetFileName, StringComparer.Ordinal)) {
        hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
        hash.AppendData(File.ReadAllBytes(path));
      }
      return Convert.ToHexString(hash.GetHashAndReset());
    }
    var beforeFingerprint = Fingerprint(root);
    var authority = AdaptiveResearchAuthority.LoadFromDirectory(root);
    var catalog = AdaptiveResearchForeignTechnologyCatalog.LoadFromDirectory(
        root, authority.Catalog, authority.ExpertiseCatalog);
    var runtime =
        new AdaptiveResearchForeignTechnologyRuntime(authority, catalog);
    object Expertise(AdaptiveResearchExpertiseState value) => new {
      value.Revision,
      FieldCompetence = value.FieldCompetence.Values.ToArray(),
      Institutions = value.Institutions.Values.ToArray(),
      TacitAssets = value.TacitAssets.Values.ToArray(),
    };
    object Core(AdaptiveResearchCivilizationState value) => new {
      value.CivilizationId,
      value.Revision,
      value.MaterializedViewRevision,
      value.DirectedProgramStageId,
      value.TotalEffectiveResearchLabs,
      value.AssignedEffectiveLabs,
      value.FreeEffectiveLabs,
      NodeStates = value.NodeStates.Values.ToArray(),
      Pressures =
          value.Pressures.Select(p => new { Id = p.Key, p.Value }).ToArray(),
      EvidenceInstances = value.EvidenceInstances.Values.ToArray(),
      CivilizationTraits = value.CivilizationTraits.ToArray(),
      ApplicabilityContexts =
          value.ApplicabilityContexts
              .Select(p => new { Id = p.Key, Traits = p.Value.ToArray() })
              .ToArray(),
      Capabilities = value.Capabilities.ToArray(),
      FacilityCapabilities = value.FacilityCapabilities.ToArray(),
      EnabledDeploymentEventIds = value.EnabledDeploymentEventIds.ToArray(),
      ActiveProjects = value.ActiveProjects.Values.ToArray(),
      Expertise = Expertise(value.Expertise),
    };
    object Foreign(AdaptiveResearchForeignTechnologyState value) => new {
      value.Revision,
      Assessments = value.Assessments.Values.ToArray(),
      Packages = value.Packages.Values.ToArray(),
    };
    object CatalogProjection(AdaptiveResearchForeignTechnologyCatalog value) => new {
      ConstraintIds = value.ConstraintIds.ToArray(),
      Components = value.Components.Values.ToArray(),
      Rights = value.Rights.ToArray(),
      value.RuntimePolicy,
      ParseUnderstanding =
          new[] { "unknown", "observed", "characterized",
                  "principle_understood", "engineering_understood" }
              .Select(id => new {
                Id = id, Value = (int)AdaptiveResearchForeignTechnologyCatalog
                                     .ParseUnderstanding(id)
              })
              .ToArray(),
    };
    object Catalog() => CatalogProjection(catalog);

    var canonicalForeign = File.ReadAllBytes(Path.Combine(root, "foreign_technology_model.json"));
    var canonicalExchange = File.ReadAllBytes(Path.Combine(root, "technology_exchange_model.json"));
    var canonicalPolicy = File.ReadAllBytes(Path.Combine(root, "foreign_technology_runtime_policy.json"));
    byte[] ChangeJson(byte[] source, Action<JsonObject> change) {
      var value = JsonNode.Parse(source)!.AsObject();
      change(value);
      return Encoding.UTF8.GetBytes(value.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
    }
    var catalogCases = new List<object>();
    void CatalogCase(string name, string fileName, byte[] changedBytes,
                     bool sourceOnly = false, string? sourceOnlyReason = null) {
      var tempParent = Path.GetFullPath(Path.GetTempPath());
      var temp = Path.Combine(tempParent, "stellar-foreign-057-" + Guid.NewGuid().ToString("N"));
      if (Directory.Exists(temp))
        throw new InvalidOperationException("Exclusive foreign-technology scratch directory already exists.");
      Directory.CreateDirectory(temp);
      AdaptiveResearchForeignTechnologyCatalog? loaded = null;
      Exception? error = null;
      string? inputBefore = null;
      string? inputAfter = null;
      try {
        File.WriteAllBytes(Path.Combine(temp, "foreign_technology_model.json"), canonicalForeign);
        File.WriteAllBytes(Path.Combine(temp, "technology_exchange_model.json"), canonicalExchange);
        File.WriteAllBytes(Path.Combine(temp, "foreign_technology_runtime_policy.json"), canonicalPolicy);
        File.WriteAllBytes(Path.Combine(temp, fileName), changedBytes);
        inputBefore = Fingerprint(temp);
        try {
          loaded = AdaptiveResearchForeignTechnologyCatalog.LoadFromDirectory(
              temp, authority.Catalog, authority.ExpertiseCatalog);
        } catch (Exception exception) {
          error = exception;
        }
        inputAfter = Fingerprint(temp);
      } finally {
        var normalized = Path.GetFullPath(temp);
        if (!normalized.StartsWith(tempParent, StringComparison.OrdinalIgnoreCase) ||
            !Path.GetFileName(normalized).StartsWith("stellar-foreign-057-", StringComparison.Ordinal))
          throw new InvalidOperationException("Refusing unsafe foreign-technology scratch cleanup.");
        try { Directory.Delete(normalized, true); } catch { }
      }
      catalogCases.Add(new {
        Name = name,
        Input = new {
          File = fileName,
          BytesBase64 = Convert.ToBase64String(changedBytes),
          BytesSha256 = Convert.ToHexString(SHA256.HashData(changedBytes)),
          SourceOnly = sourceOnly,
          SourceOnlyReason = sourceOnlyReason,
          BeforeFingerprint = inputBefore,
          AfterFingerprint = inputAfter,
        },
        Result = loaded is null ? (JsonElement?)null : Freeze(CatalogProjection(loaded)),
        Error = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message }),
      });
    }
    CatalogCase("axis-order-mismatch", "foreign_technology_model.json",
      ChangeJson(canonicalForeign, value => {
        var axis = value["understanding_axis"]!.AsArray();
        var first = axis[0]!.DeepClone();
        var second = axis[1]!.DeepClone();
        axis[0] = second;
        axis[1] = first;
      }));
    CatalogCase("constraint-count", "foreign_technology_model.json",
      ChangeJson(canonicalForeign, value => value["compatibility_constraints"]!.AsArray().RemoveAt(0)));
    CatalogCase("unknown-tacit-asset", "technology_exchange_model.json",
      ChangeJson(canonicalExchange, value =>
        value["transfer_package_components"]![0]!["creates_or_transfers_tacit_asset_types"] =
          new JsonArray("missing-tacit")));
    CatalogCase("rights-count", "technology_exchange_model.json",
      ChangeJson(canonicalExchange, value => value["rights"]!.AsArray().RemoveAt(0)));
    CatalogCase("catalog-id-mismatch", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value => value["catalog_id"] = "wrong.catalog"));
    CatalogCase("unknown-policy-component", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["package_intake"]!["minimum_understanding_by_component"]!["missing-component"] = "observed"));
    CatalogCase("incomplete-component-policy", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["package_intake"]!["minimum_understanding_by_component"]!.AsObject().Remove("observation_dossier")));
    CatalogCase("unknown-understanding", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["package_intake"]!["minimum_understanding_by_component"]!["observation_dossier"] = "mystery"));
    CatalogCase("policy-out-of-range", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["confidence"]!["new_assessment_from_observation"] = -0.01));
    CatalogCase("axis-object-kind", "foreign_technology_model.json",
      ChangeJson(canonicalForeign, value => value["understanding_axis"] = new JsonObject()));
    CatalogCase("axis-false-kind", "foreign_technology_model.json",
      ChangeJson(canonicalForeign, value => value["understanding_axis"] = false));
    CatalogCase("constraints-object-kind", "foreign_technology_model.json",
      ChangeJson(canonicalForeign, value => value["compatibility_constraints"] = new JsonObject()));
    CatalogCase("components-object-kind", "technology_exchange_model.json",
      ChangeJson(canonicalExchange, value => value["transfer_package_components"] = new JsonObject()));
    CatalogCase("rights-object-kind", "technology_exchange_model.json",
      ChangeJson(canonicalExchange, value => value["rights"] = new JsonObject()));
    CatalogCase("minimum-understanding-array-kind", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["package_intake"]!["minimum_understanding_by_component"] = new JsonArray()));
    CatalogCase("minimum-understanding-null-value", "foreign_technology_runtime_policy.json",
      ChangeJson(canonicalPolicy, value =>
        value["package_intake"]!["minimum_understanding_by_component"]!["observation_dossier"] = null));
    CatalogCase("duplicate-component-bad-bool-order", "technology_exchange_model.json",
      ChangeJson(canonicalExchange, value => {
        var values = value["transfer_package_components"]!.AsArray();
        var duplicate = values[0]!.DeepClone();
        duplicate!["creates_or_references_evidence"] = "not-bool";
        values.Add(duplicate);
      }));
    var duplicatePolicyText = Encoding.UTF8.GetString(canonicalPolicy).Replace(
      "\"observation_dossier\":\"observed\",",
      "\"observation_dossier\":\"observed\",\"observation_dossier\":\"characterized\",");
    if (duplicatePolicyText == Encoding.UTF8.GetString(canonicalPolicy))
      throw new InvalidOperationException("Could not construct duplicate policy-key fixture.");
    CatalogCase("duplicate-minimum-key-source-only", "foreign_technology_runtime_policy.json",
      Encoding.UTF8.GetBytes(duplicatePolicyText), sourceOnly: true,
      sourceOnlyReason: "The native ordered JSON DOM collapses duplicate object properties before catalog validation.");

    var lowState = new AdaptiveResearchForeignTechnologyState();
    var setAssessment = typeof(AdaptiveResearchForeignTechnologyState).GetMethod(
        "SetAssessment", BindingFlags.Instance | BindingFlags.NonPublic)!;
    var addPackage = typeof(AdaptiveResearchForeignTechnologyState).GetMethod(
        "AddPackage", BindingFlags.Instance | BindingFlags.NonPublic)!;
    object InvokeInternal(MethodInfo method, object argument) {
      try {
        method.Invoke(lowState, new[] { argument });
        return true;
      } catch (TargetInvocationException exception) when (exception.InnerException is not null) {
        ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
        throw;
      }
    }
    var lowLevel = new List<object>();
    void LowStep(string name, object input, Func<object?> invoke) {
      var frozenInput = Freeze(input);
      var before = Freeze(Foreign(lowState));
      object? result = null;
      Exception? error = null;
      try { result = invoke(); } catch (Exception exception) { error = exception; }
      lowLevel.Add(new {
        Name = name, Input = frozenInput, Before = before,
        Result = result is null ? (JsonElement?)null : Freeze(result),
        Error = error is null ? (JsonElement?)null : Freeze(new { Type = error.GetType().Name, error.Message }),
        After = Freeze(Foreign(lowState)),
      });
    }
    LowStep("default-query", new { Op = "Get", Reference = "foreign:low", Lineage = "lineage:low" },
      () => lowState.GetOrUnknown("foreign:low", "lineage:low"));
    var lowAssessment = new ForeignTechnologyAssessmentRuntimeState(
        "foreign:low", "lineage:low", ForeignUnderstandingState.Observed,
        ForeignOperabilityState.Unknown, ForeignReproductionState.None,
        ForeignAdaptationState.None, new[] { "z", "a", "z" },
        new[] { "e2", "e1", "e2" }, new[] { "t2", "t1", "t2" }, 12.5, .5, 999);
    LowStep("set-assessment-normalizes", new { Op = "SetAssessment", Value = lowAssessment },
      () => InvokeInternal(setAssessment, lowAssessment));
    var replacement = lowAssessment with {
      Understanding = (ForeignUnderstandingState)99, Confidence = .75,
      KnownConstraintIds = new[] { "b" }, Revision = -9,
    };
    LowStep("replace-assessment", new { Op = "SetAssessment", Value = replacement },
      () => InvokeInternal(setAssessment, replacement));
    var changedLineage = replacement with { SourceLineageReference = "lineage:changed" };
    LowStep("reject-lineage-change", new { Op = "SetAssessment", Value = changedLineage },
      () => InvokeInternal(setAssessment, changedLineage));
    var invalidAssessment = replacement with { Confidence = double.NaN };
    LowStep("reject-assessment-confidence", new { Op = "SetAssessment", Value = invalidAssessment },
      () => InvokeInternal(setAssessment, invalidAssessment));
    var lowPackage = new ForeignTechnologyPackageRuntimeState(
        "package:low", "foreign:low", "lineage:low",
        new[] { "c2", "c1", "c2" }, new[] { "r2", "r1", "r2" },
        new[] { "e2", "e1", "e2" }, new[] { "t2", "t1", "t2" },
        new[] { "f2", "f1", "f2" }, "low", .8, 888);
    LowStep("add-package-normalizes", new { Op = "AddPackage", Value = lowPackage },
      () => InvokeInternal(addPackage, lowPackage));
    LowStep("reject-duplicate-package", new { Op = "AddPackage", Value = lowPackage },
      () => InvokeInternal(addPackage, lowPackage));
    var invalidPackage = lowPackage with { PackageId = "package:invalid", Integrity = double.PositiveInfinity };
    LowStep("reject-package-integrity", new { Op = "AddPackage", Value = invalidPackage },
      () => InvokeInternal(addPackage, invalidPackage));
    var state = authority.CreateCivilizationState("fixture:foreign");
    var sequence = new List<object>();
    void Step(string name, object input, Func<object?> invoke) {
      var frozenInput = Freeze(input);
      var coreBefore = Freeze(Core(state));
      var foreignBefore = Freeze(Foreign(runtime.GetState(state)));
      object? result = null;
      Exception? error = null;
      try {
        result = invoke();
      } catch (Exception exception) {
        error = exception;
      }
      sequence.Add(new {
        Name = name,
        Input = frozenInput,
        CoreBefore = coreBefore,
        ForeignBefore = foreignBefore,
        Result = result is null ? (JsonElement?)null : Freeze(result),
        Error = error is null ? (JsonElement?)null
                              : Freeze(new { Type = error.GetType().Name,
                                             error.Message }),
        CoreAfter = Freeze(Core(state)),
        ForeignAfter = Freeze(Foreign(runtime.GetState(state))),
      });
    }
    var constraints = catalog.ConstraintIds.Take(2).ToArray();
    var component = catalog.Components.Values.First(
        value => value.TacitAssetTypeIds.Count > 0);
    var right = catalog.Rights.First();
    var field = authority.ExpertiseCatalog.Fields.Keys.First();
    var evidenceType = authority.Catalog.EvidenceTypeIds.First();
    Step("observe",
         new { Op = "Observe", Reference = "foreign:drive",
               Lineage = "lineage:a", Confidence = .2, Year = 2075.5,
               Constraints =
                   new[] { constraints[1], constraints[0], constraints[1] } },
         () => runtime.Observe(
             state, "foreign:drive", "lineage:a", .2, 2075.5,
             new[] { constraints[1], constraints[0], constraints[1] }));
    Step("observe-lineage-conflict",
         new { Op = "Observe", Reference = "foreign:drive",
               Lineage = "lineage:b", Confidence = .9, Year = 2076.0,
               Constraints = Array.Empty<string>() },
         () => runtime.Observe(state, "foreign:drive", "lineage:b", .9, 2076));
    Step("observe-unknown-constraint",
         new { Op = "Observe", Reference = "foreign:new",
               Lineage = "lineage:new", Confidence = .5, Year = 2076.0,
               Constraints = new[] { "missing-constraint" } },
         () => runtime.Observe(state, "foreign:new", "lineage:new", .5, 2076,
                               new[] { "missing-constraint" }));
    Step("analysis-unobserved",
         new { Op = "Analysis", Reference = "foreign:missing",
               Understanding = (int)ForeignUnderstandingState.Characterized,
               Confidence = .9, Year = 2076.5,
               Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(
             state, "foreign:missing", ForeignUnderstandingState.Characterized,
             .9, 2076.5));
    Step("analysis-characterized",
         new { Op = "Analysis", Reference = "foreign:drive",
               Understanding = (int)ForeignUnderstandingState.Characterized,
               Confidence = .7, Year = 2077.0,
               Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(
             state, "foreign:drive", ForeignUnderstandingState.Characterized,
             .7, 2077));
    Step("analysis-regression",
         new { Op = "Analysis", Reference = "foreign:drive",
               Understanding = (int)ForeignUnderstandingState.Observed,
               Confidence = .9, Year = 2078.0,
               Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(state, "foreign:drive",
                                            ForeignUnderstandingState.Observed,
                                            .9, 2078));
    Step("analysis-principle-low-confidence",
         new { Op = "Analysis", Reference = "foreign:drive",
               Understanding =
                   (int)ForeignUnderstandingState.PrincipleUnderstood,
               Confidence = 0.0, Year = 2078.0,
               Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(
             state, "foreign:drive",
             ForeignUnderstandingState.PrincipleUnderstood, 0, 2078));
    Step("analysis-principle",
         new { Op = "Analysis", Reference = "foreign:drive",
               Understanding =
                   (int)ForeignUnderstandingState.PrincipleUnderstood,
               Confidence =
                   catalog.RuntimePolicy.ControlledAnalysisConfidenceMinimum,
               Year = 2078.0, Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(
             state, "foreign:drive",
             ForeignUnderstandingState.PrincipleUnderstood,
             catalog.RuntimePolicy.ControlledAnalysisConfidenceMinimum, 2078));
    Step("operability-low-confidence",
         new { Op = "Operability", Reference = "foreign:missing",
               Value = (int)ForeignOperabilityState.Unusable, Confidence = 0.0,
               Year = 2078.0 },
         () => runtime.RecordOperabilityFact(state, "foreign:missing",
                                             ForeignOperabilityState.Unusable,
                                             0, 2078));
    Step("operability",
         new { Op = "Operability", Reference = "foreign:drive",
               Value = (int)ForeignOperabilityState.SupportedOperation,
               Confidence = .9, Year = 2078.0 },
         () => runtime.RecordOperabilityFact(
             state, "foreign:drive", ForeignOperabilityState.SupportedOperation,
             .9, 2078));
    Step("reproduction",
         new { Op = "Reproduction", Reference = "foreign:drive",
               Value = (int)ForeignReproductionState.ComponentReplication,
               Confidence = .9, Year = 2078.25 },
         () => runtime.RecordReproductionFact(
             state, "foreign:drive",
             ForeignReproductionState.ComponentReplication, .9, 2078.25));
    Step("confirm",
         new { Op = "Confirm", Reference = "foreign:drive",
               Constraint = constraints[0], Year = 2078.5 },
         () => runtime.ConfirmConstraint(state, "foreign:drive", constraints[0],
                                         2078.5));
    Step("resolve",
         new { Op = "Resolve", Reference = "foreign:drive",
               Constraint = constraints[0], Year = 2078.75 },
         () => runtime.ResolveConstraint(state, "foreign:drive", constraints[0],
                                         2078.75));
    Step("adaptation",
         new { Op = "Adaptation", Reference = "foreign:drive",
               Value = (int)ForeignAdaptationState.InterfaceAdaptation,
               Confidence = .9, Year = 2079.0 },
         () => runtime.RecordAdaptationResult(
             state, "foreign:drive", ForeignAdaptationState.InterfaceAdaptation,
             .9, 2079));
    Step("adaptation-regression",
         new { Op = "Adaptation", Reference = "foreign:drive",
               Value = (int)ForeignAdaptationState.ConceptualInspiration,
               Confidence = .9, Year = 2079.25 },
         () => runtime.RecordAdaptationResult(
             state, "foreign:drive",
             ForeignAdaptationState.ConceptualInspiration, .9, 2079.25));
    var package = new ForeignTechnologyPackageInput(
        "package:a", "foreign:drive", "lineage:a",
        new[] { component.Id, component.Id }, new[] { right, right },
        new[] { field, field },
        new[] { new ForeignTechnologyEvidenceTransfer(
            "evidence:foreign:a", evidenceType, "fixture", .8, .9) },
        constraints, "fixture:package", .75, .9, .9, 2080, "context:foreign");
    Step("acquire", new { Op = "Acquire", Package = package },
         () => runtime.AcquirePackage(state, package));
    Step("acquire-duplicate", new { Op = "Acquire", Package = package },
         () => runtime.AcquirePackage(state, package));
    var partialPackage = package with {
      PackageId = "package:partial",
      SourceLineageReference = "lineage:conflict-after-materialization",
      Evidence = new[] { new ForeignTechnologyEvidenceTransfer(
          "evidence:foreign:partial", evidenceType, "fixture:partial", .9,
          .9) },
    };
    Step("acquire-partial-lineage-failure",
         new { Op = "Acquire", Package = partialPackage },
         () => runtime.AcquirePackage(state, partialPackage));
    var emptyPackage = package with {
      PackageId = "package:empty",
      ForeignTechnologyReference = "foreign:empty",
      SourceLineageReference = "lineage:empty",
      ComponentIds = Array.Empty<string>(),
      Evidence = Array.Empty<ForeignTechnologyEvidenceTransfer>(),
      KnowledgeFieldIds = Array.Empty<string>(),
    };
    Step("acquire-empty-components",
         new { Op = "Acquire", Package = emptyPackage },
         () => runtime.AcquirePackage(state, emptyPackage));
    Step("assimilation-missing-package",
         new { Op = "Assimilate", PackageId = "package:missing",
               Stage = (int)ResearchTacitAssimilationStage.Interpreted },
         () => {
           runtime.AdvancePackageTacitAssimilation(
               state, "package:missing",
               ResearchTacitAssimilationStage.Interpreted);
           return true;
         });
    Step("assimilation-interpreted",
         new { Op = "Assimilate", PackageId = package.PackageId,
               Stage = (int)ResearchTacitAssimilationStage.Interpreted },
         () => {
           runtime.AdvancePackageTacitAssimilation(
               state, package.PackageId,
               ResearchTacitAssimilationStage.Interpreted);
           return true;
         });
    Step("assimilation-skip",
         new { Op = "Assimilate", PackageId = package.PackageId,
               Stage = (int)ResearchTacitAssimilationStage.NativePractice },
         () => {
           runtime.AdvancePackageTacitAssimilation(
               state, package.PackageId,
               ResearchTacitAssimilationStage.NativePractice);
           return true;
         });
    Step("assimilation-trained",
         new { Op = "Assimilate", PackageId = package.PackageId,
               Stage = (int)ResearchTacitAssimilationStage.Trained },
         () => {
           runtime.AdvancePackageTacitAssimilation(
               state, package.PackageId,
               ResearchTacitAssimilationStage.Trained);
           return true;
         });
    Step("unusual-understanding-enum",
         new { Op = "Analysis", Reference = "foreign:drive",
               Understanding = 99, Confidence = .95, Year = 2081.0,
               Constraints = Array.Empty<string>() },
         () => runtime.RecordAnalysisResult(
             state, "foreign:drive", (ForeignUnderstandingState)99, .95,
             2081));
    Step("unusual-operability-enum",
         new { Op = "Operability", Reference = "foreign:drive", Value = 99,
               Confidence = .95, Year = 2081.25 },
         () => runtime.RecordOperabilityFact(
             state, "foreign:drive", (ForeignOperabilityState)99, .95,
             2081.25));
    Step("unusual-reproduction-enum",
         new { Op = "Reproduction", Reference = "foreign:drive", Value = 99,
               Confidence = .95, Year = 2081.5 },
         () => runtime.RecordReproductionFact(
             state, "foreign:drive", (ForeignReproductionState)99, .95,
             2081.5));
    Step("unusual-adaptation-enum",
         new { Op = "Adaptation", Reference = "foreign:drive", Value = 99,
               Confidence = .95, Year = 2081.75 },
         () => runtime.RecordAdaptationResult(
             state, "foreign:drive", (ForeignAdaptationState)99, .95,
             2081.75));
    var assessmentPackage = runtime.GetState(state).Packages[package.PackageId];
    var valueContext = new ForeignTechnologyRecipientValueContext(
        90, 80, 70, 60, 50, 40, 30, 20, 10, 5, 2);
    Step("recipient-value",
         new { Op = "Value", PackageId = package.PackageId,
               Context = valueContext, HolderUnusable = true },
         () => runtime.EvaluateRecipientValue(assessmentPackage, valueContext,
                                              true));
    var invalidValue = valueContext with { CapabilityNovelty = double.NaN };
    Step("recipient-value-invalid-first-field",
         new { Op = "Value", PackageId = package.PackageId,
               Context = invalidValue, HolderUnusable = false },
         () => runtime.EvaluateRecipientValue(assessmentPackage, invalidValue,
                                              false));
    var partialState = authority.CreateCivilizationState("fixture:foreign-partial");
    var setProject = typeof(AdaptiveResearchCivilizationState).GetMethod(
        "SetProject", BindingFlags.Instance | BindingFlags.NonPublic)!;
    var poisonProject = new ResearchProjectRuntimeState(
        "missing:partial-project", ResearchMaturity.Experimental, null,
        1, 1, false, null, 0, 0, 0);
    try {
      setProject.Invoke(partialState, new object[] { poisonProject });
    } catch (TargetInvocationException exception) when (exception.InnerException is not null) {
      ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
      throw;
    }
    var partialAcquireInput = package with {
      PackageId = "package:partial-evidence",
      ForeignTechnologyReference = "foreign:partial-evidence",
      SourceLineageReference = "lineage:partial-evidence",
      ComponentIds = Array.Empty<string>(),
      KnowledgeFieldIds = Array.Empty<string>(),
      Evidence = new[] { new ForeignTechnologyEvidenceTransfer(
          "evidence:partial-before-error", evidenceType, "fixture:partial", .8, .9) },
    };
    var partialCoreBefore = Freeze(Core(partialState));
    var partialForeignBefore = Freeze(Foreign(runtime.GetState(partialState)));
    object? partialResult = null;
    Exception? partialError = null;
    try {
      partialResult = runtime.AcquirePackage(partialState, partialAcquireInput);
    } catch (Exception exception) {
      partialError = exception;
    }
    var partialAcquire = new {
      Input = Freeze(new { Op = "Acquire", Package = partialAcquireInput,
                           PoisonProject = poisonProject }),
      CoreBefore = partialCoreBefore,
      ForeignBefore = partialForeignBefore,
      Result = partialResult is null ? (JsonElement?)null : Freeze(partialResult),
      Error = partialError is null ? (JsonElement?)null
          : Freeze(new { Type = partialError.GetType().Name, partialError.Message }),
      CoreAfter = Freeze(Core(partialState)),
      ForeignAfter = Freeze(Foreign(runtime.GetState(partialState))),
    };
    var overflowState = authority.CreateCivilizationState("fixture:foreign-overflow");
    var overflowPackageInput = package with {
      PackageId = "package:overflow-stage",
      ForeignTechnologyReference = "foreign:overflow-stage",
      SourceLineageReference = "lineage:overflow-stage",
      Evidence = Array.Empty<ForeignTechnologyEvidenceTransfer>(),
    };
    runtime.AcquirePackage(overflowState, overflowPackageInput);
    var overflowHeld = runtime.GetState(overflowState).Packages[overflowPackageInput.PackageId];
    var overflowAsset = overflowState.Expertise.TacitAssets[overflowHeld.TacitAssetRefs[0]];
    authority.SetTacitAsset(
        overflowState, overflowAsset.AssetId, overflowAsset.AssetTypeId,
        overflowAsset.ScopeKind, overflowAsset.ScopeRef,
        (ResearchTacitAssimilationStage)int.MaxValue, overflowAsset.Depth,
        overflowAsset.Availability, overflowAsset.TranslationContextQuality,
        overflowAsset.TrainingContinuity, overflowAsset.Provenance,
        overflowAsset.ContextId);
    var overflowCoreBefore = Freeze(Core(overflowState));
    var overflowForeignBefore = Freeze(Foreign(runtime.GetState(overflowState)));
    Exception? overflowError = null;
    try {
      runtime.AdvancePackageTacitAssimilation(
          overflowState, overflowPackageInput.PackageId,
          (ResearchTacitAssimilationStage)int.MaxValue);
    } catch (Exception exception) {
      overflowError = exception;
    }
    var overflowAssimilation = new {
      Input = Freeze(new { Package = overflowPackageInput,
                           Stage = int.MaxValue }),
      CoreBefore = overflowCoreBefore,
      ForeignBefore = overflowForeignBefore,
      Result = (JsonElement?)null,
      Error = overflowError is null ? (JsonElement?)null
          : Freeze(new { Type = overflowError.GetType().Name, overflowError.Message }),
      CoreAfter = Freeze(Core(overflowState)),
      ForeignAfter = Freeze(Foreign(runtime.GetState(overflowState))),
    };
    if (Fingerprint(root) != beforeFingerprint)
      throw new InvalidOperationException(
          "Foreign technology replay changed canonical research data.");
    var output =
        new { Generator = "actual C# AdaptiveResearchForeignTechnology",
              CanonicalFingerprint = beforeFingerprint, Catalog = Catalog(),
              CatalogCases = catalogCases,
              LowLevel = lowLevel,
              PartialAcquire = partialAcquire,
              OverflowAssimilation = overflowAssimilation,
              Sequence = sequence,
              Controls = new { Constraints = constraints,
                               Component = component.Id, Right = right,
                               Field = field, EvidenceType = evidenceType } };
    File.WriteAllText(Path.GetFullPath(args[1]),
                      JsonSerializer.Serialize(output, options) +
                          Environment.NewLine);
    return 0;
} catch (Exception exception) {
  Console.Error.WriteLine(
      $"ResearchForeignTechnologyOracle failed: {exception}\nCWD: {Environment.CurrentDirectory}\nResearch root: {Path.GetFullPath(args[0])}\nFixture: {Path.GetFullPath(args[1])}");
  return 1;
}
