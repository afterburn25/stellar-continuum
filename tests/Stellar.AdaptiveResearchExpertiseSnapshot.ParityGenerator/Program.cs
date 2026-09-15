using System.Globalization;
using System.Collections;
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
try {
    if (args.Length != 2)
        throw new ArgumentException("Expected canonical research directory and output fixture path.");
    var root = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var jsonOptions = new JsonSerializerOptions { WriteIndented = false };

    string Fingerprint() {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var path in Directory.GetFiles(root, "*.json")
                     .OrderBy(path => Path.GetFileName(path), StringComparer.Ordinal)) {
            hash.AppendData(Encoding.UTF8.GetBytes(Path.GetFileName(path)));
            hash.AppendData(File.ReadAllBytes(path));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    object Projection(AdaptiveResearchCivilizationState state) => new {
        state.CivilizationId, state.Revision, state.MaterializedViewRevision,
        state.DirectedProgramStageId, state.TotalEffectiveResearchLabs,
        state.AssignedEffectiveLabs, state.FreeEffectiveLabs,
        NodeStates = state.NodeStates.Values.ToArray(),
        Pressures = state.Pressures.Select(pair => new { Id = pair.Key, pair.Value }).ToArray(),
        EvidenceInstances = state.EvidenceInstances.Values.ToArray(),
        CivilizationTraits = state.CivilizationTraits.ToArray(),
        ApplicabilityContexts = state.ApplicabilityContexts.Select(pair =>
            new { Id = pair.Key, Traits = pair.Value.ToArray() }).ToArray(),
        Capabilities = state.Capabilities.ToArray(),
        FacilityCapabilities = state.FacilityCapabilities.ToArray(),
        EnabledDeploymentEventIds = state.EnabledDeploymentEventIds.ToArray(),
        ActiveProjects = state.ActiveProjects.Values.ToArray(),
        Expertise = new {
            state.Expertise.Revision,
            FieldCompetence = state.Expertise.FieldCompetence.Values.ToArray(),
            Institutions = state.Expertise.Institutions.Values.ToArray(),
            TacitAssets = state.Expertise.TacitAssets.Values.ToArray(),
        },
    };

    var loadBefore = Fingerprint();
    var authority = AdaptiveResearchAuthority.LoadFromDirectory(root);
    var codec = new AdaptiveResearchSnapshotV2Codec(authority);
    var v1 = new AdaptiveResearchSnapshotCodec(authority.Kernel);
    var loadAfter = Fingerprint();
    if (loadBefore != loadAfter) throw new InvalidOperationException("Loading changed canonical inputs.");

    var rows = new List<object>();
    var profiles = authority.StartingProfiles.ReferenceProfileIds.ToArray();
    for (var index = 0; index < profiles.Length; ++index) {
        var profile = profiles[index];
        var composition = authority.ComposeReferenceProfile(
            $"fixture:v2:{index}", profile, $"fixture:context:{index}", 2050 + index);
        var state = composition.State;
        if (index < 2) {
            var candidate = authority.BuildView(state).VisibleNodes.First(value =>
                value.State == ResearchMaturity.Investigable && value.Blockers.Count == 0 && value.MinimumLabs is not null);
            var started = authority.StartDirectedResearch(state, candidate.NodeId, candidate.MinimumLabs!.Value,
                                                           $"fixture:context:{index}");
            if (!started.Accepted) throw new InvalidOperationException(started.Message);
            if (index == 0) authority.PauseDirectedResearch(state, candidate.NodeId);
        }
        if (index == 0) {
            var assetType = authority.ExpertiseCatalog.TacitAssetTypes.Values.First();
            var field = authority.ExpertiseCatalog.Fields.Values.First();
            authority.SetTacitAsset(state, "fixture:tacit", assetType.Id,
                ResearchTacitScopeKind.KnowledgeField, field.Id,
                ResearchTacitAssimilationStage.Interpreted, .7, .8, .9, .6,
                "fixture provenance", $"fixture:context:{index}");
            authority.Kernel.AddFacilityCapability(state, "large_scale_prototyping");
        }
        var before = Fingerprint();
        Func<string> serializeCall = () => codec.Serialize(state);
        string? serialized = null;
        Exception? serializeError = null;
        try { serialized = serializeCall(); } catch (Exception error) { serializeError = error; }
        AdaptiveResearchCivilizationState? restored = null;
        Exception? restoreError = null;
        if (serialized is not null) {
            var ownedJson = serialized;
            Func<AdaptiveResearchCivilizationState> deserializeCall = () => codec.Deserialize(ownedJson);
            try { restored = deserializeCall(); } catch (Exception error) { restoreError = error; }
        }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Snapshot production calls changed inputs.");
        rows.Add(new {
            Name = "profile-" + profile,
            ProfileId = profile,
            BeforeFingerprint = before,
            AfterFingerprint = after,
            Serialized = serialized,
            SerializeError = serializeError is null ? null : new { Type = serializeError.GetType().Name, serializeError.Message },
            RestoreError = restoreError is null ? null : new { Type = restoreError.GetType().Name, restoreError.Message },
            OriginalProjection = Projection(state),
            RestoredProjection = restored is null ? null : Projection(restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
        });
    }

    var fallbackState = authority.ComposeReferenceProfile(
        "fixture:v1", profiles[0], "fixture:v1:context").State;
    var v1Json = v1.Serialize(fallbackState);
    var v1Before = Fingerprint();
    Func<AdaptiveResearchCivilizationState> v1Call = () => codec.Deserialize(v1Json);
    AdaptiveResearchCivilizationState? v1Restored = null;
    Exception? v1Error = null;
    try { v1Restored = v1Call(); } catch (Exception error) { v1Error = error; }
    var v1After = Fingerprint();
    rows.Add(new {
        Name = "schema1-direct-fallback",
        BeforeFingerprint = v1Before,
        AfterFingerprint = v1After,
        Serialized = v1Json,
        Error = v1Error is null ? null : new { Type = v1Error.GetType().Name, v1Error.Message },
        RestoredProjection = v1Restored is null ? null : Projection(v1Restored),
        RestoredV1 = v1Restored is null ? null : v1.Serialize(v1Restored),
    });

    var ordinalState = authority.ComposeReferenceProfile(
        "fixture:ordinal", profiles[0], "fixture:ordinal:context", 2099).State;
    var ordinalAssetType = authority.ExpertiseCatalog.TacitAssetTypes.Values.First();
    var ordinalField = authority.ExpertiseCatalog.Fields.Values.First();
    authority.SetTacitAsset(ordinalState, "\uE000", ordinalAssetType.Id,
        ResearchTacitScopeKind.KnowledgeField, ordinalField.Id,
        ResearchTacitAssimilationStage.Access, .5, .5, .5, .5, "bmp", null);
    authority.SetTacitAsset(ordinalState, "\U00010000", ordinalAssetType.Id,
        ResearchTacitScopeKind.KnowledgeField, ordinalField.Id,
        ResearchTacitAssimilationStage.Access, .5, .5, .5, .5, "supplementary", null);
    var ordinalBefore = Fingerprint();
    Func<string> ordinalCall = () => codec.Serialize(ordinalState);
    string? ordinalSerialized = null;
    Exception? ordinalError = null;
    try { ordinalSerialized = ordinalCall(); } catch (Exception error) { ordinalError = error; }
    var ordinalAfter = Fingerprint();
    rows.Add(new {
        Name = "serialize-utf16-ordinal-asset-order", Kind = "utf16-order",
        BeforeFingerprint = ordinalBefore, AfterFingerprint = ordinalAfter,
        Serialized = ordinalSerialized,
        Error = ordinalError is null ? null : new { Type = ordinalError.GetType().Name, ordinalError.Message },
        Projection = Projection(ordinalState),
    });

    var boundaryState = authority.ComposeReferenceProfile(
        "fixture:boundary", profiles[0], "fixture:boundary:context", 2100).State;
    var boundarySnapshot = codec.Capture(boundaryState);

    void AddRestore(string name, AdaptiveResearchStateSnapshotV2 snapshot) {
        var before = Fingerprint();
        Func<AdaptiveResearchCivilizationState> call = () => codec.Restore(snapshot);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Restore changed canonical inputs.");
        rows.Add(new {
            Name = name, Kind = "typed-restore", BeforeFingerprint = before,
            AfterFingerprint = after,
            Error = error is null ? null : new { Type = error.GetType().Name, error.Message },
            RestoredProjection = restored is null ? null : Projection(restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
        });
    }

    void AddDeserialize(string name, string json, string kind = "json-deserialize") {
        var before = Fingerprint();
        var ownedJson = json;
        Func<AdaptiveResearchCivilizationState> call = () => codec.Deserialize(ownedJson);
        AdaptiveResearchCivilizationState? restored = null;
        Exception? error = null;
        try { restored = call(); } catch (Exception caught) { error = caught; }
        var after = Fingerprint();
        if (before != after) throw new InvalidOperationException("Deserialize changed canonical inputs.");
        rows.Add(new {
            Name = name, Kind = kind, BeforeFingerprint = before,
            AfterFingerprint = after, Input = ownedJson,
            Error = error is null ? null : new { Type = error.GetType().Name, error.Message },
            RestoredProjection = restored is null ? null : Projection(restored),
            RestoredSerialized = restored is null ? null : codec.Serialize(restored),
        });
    }

    AddRestore("restore-schema-unsupported", boundarySnapshot with { SchemaVersion = 3 });
    AddRestore("restore-catalog-mismatch", boundarySnapshot with { CatalogId = "wrong" });
    var fieldId = authority.ExpertiseCatalog.Fields.Keys.First();
    var zero = new ResearchCompetenceVector(0, 0, 0);
    var one = new ResearchCompetenceVector(1, 1, 1);
    ResearchFieldCompetenceSnapshot Field(string id, ResearchCompetenceVector current,
                                          ResearchCompetenceVector peak) =>
        new(id, current, peak, 0, 0, 0);
    AdaptiveResearchStateSnapshotV2 WithFields(params ResearchFieldCompetenceSnapshot[] fields) =>
        boundarySnapshot with { Expertise = boundarySnapshot.Expertise with { Fields = fields } };
    AddRestore("restore-field-unknown", WithFields(Field("unknown", zero, zero)));
    AddRestore("restore-current-negative", WithFields(Field(fieldId, new(-.5, 0, 0), zero)));
    AddRestore("restore-current-nan", WithFields(Field(fieldId, new(double.NaN, 0, 0), zero)));
    AddRestore("restore-current-positive-infinity", WithFields(Field(fieldId, new(double.PositiveInfinity, 0, 0), zero)));
    AddRestore("restore-current-huge", WithFields(Field(fieldId, new(1e300, 0, 0), zero)));
    AddRestore("restore-current-small-negative", WithFields(Field(fieldId, new(-1e-8, 0, 0), zero)));
    AddRestore("restore-current-one-million", WithFields(Field(fieldId, new(1e6, 0, 0), zero)));
    AddRestore("restore-current-one-quadrillion", WithFields(Field(fieldId, new(1e15, 0, 0), zero)));
    AddRestore("restore-current-ten-quadrillion", WithFields(Field(fieldId, new(1e16, 0, 0), zero)));
    AddRestore("restore-current-negative-fourth", WithFields(Field(fieldId, new(-1e-4, 0, 0), zero)));
    AddRestore("restore-current-negative-fifth", WithFields(Field(fieldId, new(-1e-5, 0, 0), zero)));
    AddRestore("restore-peak-over-100", WithFields(Field(fieldId, zero, new(100.5, 0, 0))));
    AddRestore("restore-current-exceeds-peak", WithFields(Field(fieldId, one, zero)));
    AddRestore("restore-field-order-unknown-before-invalid", WithFields(
        Field("unknown", zero, zero), Field(fieldId, new(-1, 0, 0), zero)));
    AddRestore("restore-field-order-invalid-before-unknown", WithFields(
        Field(fieldId, new(-1, 0, 0), zero), Field("unknown", zero, zero)));
    AddRestore("restore-core-before-expertise", boundarySnapshot with {
        Core = boundarySnapshot.Core with { SchemaVersion = 2 },
        Expertise = boundarySnapshot.Expertise with { Fields = new[] { Field("unknown", zero, zero) } },
    });

    var institution = authority.ExpertiseCatalog.Institutions.Values.First();
    AdaptiveResearchStateSnapshotV2 WithInstitutions(params ResearchInstitutionSnapshot[] values) =>
        boundarySnapshot with { Expertise = boundarySnapshot.Expertise with { Institutions = values } };
    AddRestore("restore-institution-unknown", WithInstitutions(new ResearchInstitutionSnapshot("instance", "unknown", null, 1, 1)));
    AddRestore("restore-institution-negative-total", WithInstitutions(new ResearchInstitutionSnapshot("instance", institution.InstitutionArchetypeId, null, -1, 0)));
    AddRestore("restore-institution-active-exceeds-total", WithInstitutions(new ResearchInstitutionSnapshot("instance", institution.InstitutionArchetypeId, null, 1, 2)));
    AddRestore("restore-institution-lab-mismatch", WithInstitutions(new ResearchInstitutionSnapshot("instance", institution.InstitutionArchetypeId, null, 1, 1)));
    AddRestore("restore-fields-before-institutions", boundarySnapshot with {
        Expertise = boundarySnapshot.Expertise with {
            Fields = new[] { Field("unknown", zero, zero) },
            Institutions = new[] { new ResearchInstitutionSnapshot("instance", "unknown", null, 1, 1) },
        },
    });
    AddRestore("restore-institution-preserves-unsorted-physical-facilities", boundarySnapshot with {
        Core = boundarySnapshot.Core with {
            TotalEffectiveResearchLabs = institution.EffectiveLabUnits,
            FacilityCapabilities = new[] { "large_scale_prototyping", "advanced_computation", "large_scale_prototyping" },
        },
        Expertise = boundarySnapshot.Expertise with {
            Institutions = new[] { new ResearchInstitutionSnapshot("instance", institution.InstitutionArchetypeId, null, 1, 1) },
        },
    });
    AddRestore("restore-empty-institutions-skip-lab-mismatch", boundarySnapshot with {
        Core = boundarySnapshot.Core with { TotalEffectiveResearchLabs = boundarySnapshot.Core.TotalEffectiveResearchLabs + 123 },
        Expertise = boundarySnapshot.Expertise with { Institutions = Array.Empty<ResearchInstitutionSnapshot>() },
    });

    var boundaryAssetType = authority.ExpertiseCatalog.TacitAssetTypes.Values.First();
    ResearchTacitAssetSnapshot Asset(string id, string type, ResearchTacitScopeKind scope,
                                     string scopeRef, ResearchTacitAssimilationStage stage,
                                     double depth = .5, double availability = .5,
                                     string provenance = "fixture") =>
        new(id, type, scope, scopeRef, stage, depth, availability, .5, .5, provenance, null);
    AdaptiveResearchStateSnapshotV2 WithAssets(params ResearchTacitAssetSnapshot[] values) =>
        boundarySnapshot with { Expertise = boundarySnapshot.Expertise with { TacitAssets = values } };
    AddRestore("restore-tacit-type-unknown", WithAssets(Asset("asset", "unknown", ResearchTacitScopeKind.KnowledgeField, fieldId, ResearchTacitAssimilationStage.Access)));
    AddRestore("restore-tacit-depth-invalid", WithAssets(Asset("asset", boundaryAssetType.Id, ResearchTacitScopeKind.KnowledgeField, fieldId, ResearchTacitAssimilationStage.Access, -1)));
    AddRestore("restore-tacit-availability-invalid", WithAssets(Asset("asset", boundaryAssetType.Id, ResearchTacitScopeKind.KnowledgeField, fieldId, ResearchTacitAssimilationStage.Access, .5, 2)));
    AddRestore("restore-tacit-empty-provenance", WithAssets(Asset("asset", boundaryAssetType.Id, ResearchTacitScopeKind.KnowledgeField, fieldId, ResearchTacitAssimilationStage.Access, .5, .5, "")));
    AddRestore("restore-tacit-unknown-scope-enum", WithAssets(Asset("asset", boundaryAssetType.Id, (ResearchTacitScopeKind)99, "unknown-ref", ResearchTacitAssimilationStage.Access)));
    AddRestore("restore-tacit-unknown-assimilation-enum", WithAssets(Asset("asset", boundaryAssetType.Id, ResearchTacitScopeKind.KnowledgeField, fieldId, (ResearchTacitAssimilationStage)99)));

    var validJson = codec.Serialize(boundaryState);
    void MutateJson(string name, Action<JsonObject> mutate,
                    string kind = "json-deserialize") {
        var node = JsonNode.Parse(validJson)!.AsObject();
        mutate(node);
        AddDeserialize(name, node.ToJsonString(), kind);
    }
    AddDeserialize("json-null-root", "null");
    AddDeserialize("json-array-root", "[]");
    MutateJson("json-missing-schema", value => value.Remove("schemaVersion"));
    MutateJson("json-schema-string", value => value["schemaVersion"] = "2");
    MutateJson("json-schema-overflow", value => value["schemaVersion"] = 2147483648L);
    MutateJson("json-missing-catalog", value => value.Remove("catalogId"));
    MutateJson("json-null-catalog", value => value["catalogId"] = null);
    MutateJson("json-missing-core", value => value.Remove("core"));
    MutateJson("json-null-core", value => value["core"] = null);
    MutateJson("json-missing-expertise", value => value.Remove("expertise"));
    MutateJson("json-null-expertise", value => value["expertise"] = null);
    MutateJson("json-missing-fields", value => value["expertise"]!.AsObject().Remove("fields"));
    MutateJson("json-null-fields", value => value["expertise"]!["fields"] = null);
    MutateJson("json-fields-object", value => value["expertise"]!["fields"] = new JsonObject());
    MutateJson("json-missing-institutions", value => value["expertise"]!.AsObject().Remove("institutions"));
    MutateJson("json-null-institutions", value => value["expertise"]!["institutions"] = null);
    MutateJson("json-missing-tacit-assets", value => value["expertise"]!.AsObject().Remove("tacitAssets"));
    MutateJson("json-null-tacit-assets", value => value["expertise"]!["tacitAssets"] = null);
    MutateJson("json-field-missing-current", value => {
        var array = value["expertise"]!["fields"]!.AsArray();
        array.Add(JsonNode.Parse("{\"fieldId\":\"" + fieldId + "\",\"historicalPeak\":{\"theoretical\":0,\"experimental\":0,\"engineering\":0}}"));
    });
    MutateJson("json-field-null-current", value => {
        var array = value["expertise"]!["fields"]!.AsArray();
        array.Add(new JsonObject { ["fieldId"] = fieldId, ["current"] = null,
            ["historicalPeak"] = JsonNode.Parse("{\"theoretical\":0,\"experimental\":0,\"engineering\":0}") });
    });
    MutateJson("json-field-missing-vector-components", value => {
        value["expertise"]!["fields"] = new JsonArray(JsonNode.Parse("{\"fieldId\":\"" + fieldId + "\",\"current\":{},\"historicalPeak\":{}}"));
    });
    MutateJson("json-field-null-id-source-only", value => {
        value["expertise"]!["fields"] = new JsonArray(JsonNode.Parse("{\"fieldId\":null,\"current\":{},\"historicalPeak\":{}}"));
    }, "source-only-null-boundary");
    MutateJson("json-institution-null-archetype-source-only", value => {
        value["expertise"]!["institutions"] = new JsonArray(JsonNode.Parse("{\"institutionInstanceId\":\"i\",\"institutionArchetypeId\":null,\"totalCount\":1,\"activeCount\":1}"));
    }, "source-only-null-boundary");
    MutateJson("json-tacit-null-scope-ref-source-only", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":0,\"scopeRef\":null,\"assimilationStage\":0,\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\"}"));
    }, "source-only-null-boundary");
    MutateJson("json-tacit-null-asset-id-source-only", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":null,\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":0,\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":0,\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\"}"));
    }, "source-only-null-boundary");
    MutateJson("json-enum-integer", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":0,\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":0,\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-numeric-string", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\"0\",\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":\"0\",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-plus-numeric-string", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\"+1\",\"scopeRef\":\"fusion_power\",\"assimilationStage\":\"+1\",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-comma-separated-name", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\"knowledgeField, technologyNode\",\"scopeRef\":\"fusion_power\",\"assimilationStage\":\"access, interpreted\",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-whitespace-name", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\" \\tKnowledgeField\\r\\n\",\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":\" nativePractice \",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-unicode-whitespace-name", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\"\\u00a0KnowledgeField\\u3000\",\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":\"\\u2000nativePractice\\u2000\",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-unknown-name", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":\"unknown\",\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":\"access\",\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\",\"contextId\":null}"));
    });
    MutateJson("json-enum-int32-overflow", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":2147483648,\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":0,\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":\"p\"}"));
    });
    MutateJson("json-institution-missing-counts", value => {
        value["expertise"]!["institutions"] = new JsonArray(JsonNode.Parse("{\"institutionInstanceId\":\"i\",\"institutionArchetypeId\":\"" + institution.InstitutionArchetypeId + "\"}"));
    });
    MutateJson("json-institution-count-overflow", value => {
        value["expertise"]!["institutions"] = new JsonArray(JsonNode.Parse("{\"institutionInstanceId\":\"i\",\"institutionArchetypeId\":\"" + institution.InstitutionArchetypeId + "\",\"totalCount\":2147483648,\"activeCount\":0}"));
    });
    MutateJson("json-tacit-missing-scalars", value => {
        value["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":0,\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":0,\"provenance\":\"p\"}"));
    });
    MutateJson("json-order-bad-catalog-and-fields-shape", value => {
        value["catalogId"] = "wrong";
        value["expertise"]!["fields"] = new JsonObject();
    }, "known-parser-order-boundary");
    MutateJson("json-order-bad-core-and-fields-shape", value => {
        value["core"]!["schemaVersion"] = 3;
        value["expertise"]!["fields"] = new JsonObject();
    }, "known-parser-order-boundary");
    {
        var node = JsonNode.Parse(validJson)!.AsObject();
        node["expertise"]!["tacitAssets"] = new JsonArray(JsonNode.Parse("{\"assetId\":\"a\",\"assetTypeId\":\"" + boundaryAssetType.Id + "\",\"scopeKind\":0,\"scopeRef\":\"" + fieldId + "\",\"assimilationStage\":0,\"depth\":0.5,\"availability\":0.5,\"translationContextQuality\":0.5,\"trainingContinuity\":0.5,\"provenance\":null}"));
        AddDeserialize("json-tacit-null-provenance-source-only", node.ToJsonString(),
                       "source-only-null-boundary");
    }

    var nonfiniteState = authority.ComposeReferenceProfile(
        "fixture:nonfinite", profiles[0], "fixture:nonfinite:context", 2101).State;
    var expertiseDictionaryField = typeof(AdaptiveResearchExpertiseState).GetField(
        "_fieldCompetence", BindingFlags.Instance | BindingFlags.NonPublic)
        ?? throw new MissingFieldException("AdaptiveResearchExpertiseState._fieldCompetence");
    var expertiseDictionary = (IDictionary)(expertiseDictionaryField.GetValue(nonfiniteState.Expertise)
        ?? throw new InvalidOperationException("Expertise dictionary was null."));
    expertiseDictionary[fieldId] = new ResearchFieldCompetenceRuntimeState(
        fieldId, new ResearchCompetenceVector(double.NaN, 0, 0), zero, 0, 0, 0, 1);
    var nonfiniteBefore = Fingerprint();
    Func<string> nonfiniteCall = () => codec.Serialize(nonfiniteState);
    string? nonfiniteSerialized = null;
    Exception? nonfiniteError = null;
    try { nonfiniteSerialized = nonfiniteCall(); } catch (Exception error) { nonfiniteError = error; }
    var nonfiniteAfter = Fingerprint();
    if (nonfiniteBefore != nonfiniteAfter)
        throw new InvalidOperationException("Non-finite serialization changed canonical inputs.");
    rows.Add(new {
        Name = "serialize-nonfinite-competence", Kind = "nonfinite-serialize",
        BeforeFingerprint = nonfiniteBefore, AfterFingerprint = nonfiniteAfter,
        Serialized = nonfiniteSerialized,
        Error = nonfiniteError is null ? null : new { Type = nonfiniteError.GetType().Name, nonfiniteError.Message },
    });

    var fixture = new {
        Generator = "actual C# AdaptiveResearchSnapshotV2Codec",
        CanonicalFingerprint = loadBefore,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    var fixtureJson = JsonSerializer.Serialize(fixture, jsonOptions)
        .Replace("},{\"Name\":", "},\n{\"Name\":", StringComparison.Ordinal);
    File.WriteAllText(output, fixtureJson + "\n");
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
