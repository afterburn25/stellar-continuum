using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Game.Simulation.Research.Adaptive;

public enum ForeignUnderstandingState
{
    Unknown,
    Observed,
    Characterized,
    PrincipleUnderstood,
    EngineeringUnderstood,
}

public enum ForeignOperabilityState
{
    Unknown,
    Unusable,
    OriginOnly,
    SupportedOperation,
    AdaptedOperation,
    NativeOperation,
}

public enum ForeignReproductionState
{
    None,
    ComponentReplication,
    SubsystemReplication,
    ForeignProcessReplication,
    NativeProcessReplication,
}

public enum ForeignAdaptationState
{
    None,
    ConceptualInspiration,
    InterfaceAdaptation,
    NativeDerivative,
    HybridLineage,
}

public sealed record ForeignTechnologyPackageComponentDefinition(
    string Id,
    IReadOnlyList<string> TacitAssetTypeIds,
    bool CreatesOrReferencesEvidence);

public sealed record ForeignTechnologyRuntimePolicy(
    IReadOnlyDictionary<string, ForeignUnderstandingState> MinimumUnderstandingByComponent,
    bool ConceptualInspirationIfCharacterized,
    double MinimumTrainingContinuityForTrained,
    double MinimumTranslationQualityBeyondAccess,
    double NewObservationConfidence,
    double NewCharacterizedConfidence,
    double ControlledAnalysisConfidenceMinimum,
    double OperationalFactConfidenceMinimum,
    double EngineeringUnderstoodConfidenceMinimum);

/// <summary>
/// Immutable foreign-technology axes, constraints, package components, rights and runtime seed policy.
/// </summary>
public sealed class AdaptiveResearchForeignTechnologyCatalog
{
    private AdaptiveResearchForeignTechnologyCatalog(
        IReadOnlySet<string> constraintIds,
        IReadOnlyDictionary<string, ForeignTechnologyPackageComponentDefinition> components,
        IReadOnlySet<string> rights,
        ForeignTechnologyRuntimePolicy runtimePolicy)
    {
        ConstraintIds = constraintIds;
        Components = components;
        Rights = rights;
        RuntimePolicy = runtimePolicy;
    }

    public IReadOnlySet<string> ConstraintIds { get; }
    public IReadOnlyDictionary<string, ForeignTechnologyPackageComponentDefinition> Components { get; }
    public IReadOnlySet<string> Rights { get; }
    public ForeignTechnologyRuntimePolicy RuntimePolicy { get; }

    public static AdaptiveResearchForeignTechnologyCatalog LoadFromDirectory(
        string rootPath,
        AdaptiveResearchCatalog catalog,
        AdaptiveResearchExpertiseCatalog expertiseCatalog)
    {
        var root = Path.GetFullPath(rootPath);
        using var foreignDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "foreign_technology_model.json")));
        ValidateCatalogId(foreignDoc.RootElement, catalog.Metadata.CatalogId, "foreign_technology_model.json");

        ValidateAxis(foreignDoc.RootElement, "understanding_axis", new[] { "unknown", "observed", "characterized", "principle_understood", "engineering_understood" });
        ValidateAxis(foreignDoc.RootElement, "operability_axis", new[] { "unknown", "unusable", "origin_only", "supported_operation", "adapted_operation", "native_operation" });
        ValidateAxis(foreignDoc.RootElement, "reproduction_axis", new[] { "none", "component_replication", "subsystem_replication", "foreign_process_replication", "native_process_replication" });
        ValidateAxis(foreignDoc.RootElement, "adaptation_axis", new[] { "none", "conceptual_inspiration", "interface_adaptation", "native_derivative", "hybrid_lineage" });

        var constraints = foreignDoc.RootElement.GetProperty("compatibility_constraints")
            .EnumerateArray().Select(element => RequiredString(element, "id", "foreign_technology_model.json")).ToHashSet(StringComparer.Ordinal);
        if (constraints.Count != 12)
            throw new InvalidDataException($"Expected 12 foreign-technology compatibility constraints, found {constraints.Count}.");

        using var exchangeDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "technology_exchange_model.json")));
        ValidateCatalogId(exchangeDoc.RootElement, catalog.Metadata.CatalogId, "technology_exchange_model.json");
        var components = new Dictionary<string, ForeignTechnologyPackageComponentDefinition>(StringComparer.Ordinal);
        foreach (var element in exchangeDoc.RootElement.GetProperty("transfer_package_components").EnumerateArray())
        {
            var id = RequiredString(element, "id", "technology_exchange_model.json");
            var tacitIds = StringArray(element, "creates_or_transfers_tacit_asset_types");
            foreach (var tacitId in tacitIds)
                if (!expertiseCatalog.TacitAssetTypes.ContainsKey(tacitId))
                    throw new InvalidDataException($"Transfer component '{id}' references unknown tacit asset type '{tacitId}'.");
            components.Add(id, new ForeignTechnologyPackageComponentDefinition(
                id,
                tacitIds,
                element.GetProperty("creates_or_references_evidence").GetBoolean()));
        }
        if (components.Count != 10)
            throw new InvalidDataException($"Expected 10 transfer package components, found {components.Count}.");

        var rights = exchangeDoc.RootElement.GetProperty("rights")
            .EnumerateArray().Select(element => RequiredString(element, "id", "technology_exchange_model.json")).ToHashSet(StringComparer.Ordinal);
        if (rights.Count != 11)
            throw new InvalidDataException($"Expected 11 technology-transfer rights, found {rights.Count}.");

        using var policyDoc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "foreign_technology_runtime_policy.json")));
        ValidateCatalogId(policyDoc.RootElement, catalog.Metadata.CatalogId, "foreign_technology_runtime_policy.json");
        var intake = policyDoc.RootElement.GetProperty("package_intake");
        var minimumUnderstanding = new Dictionary<string, ForeignUnderstandingState>(StringComparer.Ordinal);
        foreach (var property in intake.GetProperty("minimum_understanding_by_component").EnumerateObject())
        {
            if (!components.ContainsKey(property.Name))
                throw new InvalidDataException($"Foreign technology runtime policy references unknown package component '{property.Name}'.");
            minimumUnderstanding.Add(property.Name, ParseUnderstanding(property.Value.GetString() ?? string.Empty));
        }
        if (minimumUnderstanding.Count != components.Count)
            throw new InvalidDataException("Foreign technology runtime policy must define intake understanding for every transfer component.");

        var assimilation = policyDoc.RootElement.GetProperty("assimilation");
        var confidence = policyDoc.RootElement.GetProperty("confidence");
        var runtimePolicy = new ForeignTechnologyRuntimePolicy(
            new ReadOnlyDictionary<string, ForeignUnderstandingState>(minimumUnderstanding),
            intake.GetProperty("conceptual_inspiration_if_characterized").GetBoolean(),
            assimilation.GetProperty("minimum_training_continuity_to_advance_to_trained").GetDouble(),
            assimilation.GetProperty("minimum_translation_quality_to_advance_beyond_access").GetDouble(),
            confidence.GetProperty("new_assessment_from_observation").GetDouble(),
            confidence.GetProperty("new_assessment_from_characterized_package").GetDouble(),
            confidence.GetProperty("successful_controlled_analysis_minimum").GetDouble(),
            confidence.GetProperty("successful_operational_or_reproduction_test_minimum").GetDouble(),
            confidence.GetProperty("engineering_understood_minimum").GetDouble());

        foreach (var value in new[]
                 {
                     runtimePolicy.MinimumTrainingContinuityForTrained,
                     runtimePolicy.MinimumTranslationQualityBeyondAccess,
                     runtimePolicy.NewObservationConfidence,
                     runtimePolicy.NewCharacterizedConfidence,
                     runtimePolicy.ControlledAnalysisConfidenceMinimum,
                     runtimePolicy.OperationalFactConfidenceMinimum,
                     runtimePolicy.EngineeringUnderstoodConfidenceMinimum,
                 })
            if (value < 0 || value > 1 || double.IsNaN(value) || double.IsInfinity(value))
                throw new InvalidDataException("Foreign technology runtime policy contains a value outside 0..1.");

        return new AdaptiveResearchForeignTechnologyCatalog(
            constraints,
            new ReadOnlyDictionary<string, ForeignTechnologyPackageComponentDefinition>(components),
            rights,
            runtimePolicy);
    }

    public static ForeignUnderstandingState ParseUnderstanding(string id) => id switch
    {
        "unknown" => ForeignUnderstandingState.Unknown,
        "observed" => ForeignUnderstandingState.Observed,
        "characterized" => ForeignUnderstandingState.Characterized,
        "principle_understood" => ForeignUnderstandingState.PrincipleUnderstood,
        "engineering_understood" => ForeignUnderstandingState.EngineeringUnderstood,
        _ => throw new InvalidDataException($"Unknown foreign understanding state '{id}'."),
    };

    private static void ValidateAxis(JsonElement root, string propertyName, IReadOnlyList<string> expected)
    {
        var actual = root.GetProperty(propertyName).EnumerateArray()
            .Select(element => RequiredString(element, "id", $"foreign_technology_model.json:{propertyName}"))
            .ToArray();
        if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
            throw new InvalidDataException($"Foreign technology axis '{propertyName}' does not match the executable runtime order.");
    }

    private static IReadOnlyList<string> StringArray(JsonElement element, string name) =>
        element.TryGetProperty(name, out var property)
            ? property.EnumerateArray().Select(value => value.GetString() ?? throw new InvalidDataException($"{name} contains null.")).ToArray()
            : Array.Empty<string>();

    private static string RequiredString(JsonElement element, string name, string source) =>
        element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String && !string.IsNullOrWhiteSpace(value.GetString())
            ? value.GetString()!
            : throw new InvalidDataException($"{source} is missing non-empty string '{name}'.");

    private static void ValidateCatalogId(JsonElement root, string expected, string source)
    {
        var actual = RequiredString(root, "catalog_id", source);
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"{source} catalog_id '{actual}' does not match '{expected}'.");
    }
}
