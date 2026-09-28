using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Research.Adaptive;

public sealed record ForeignTechnologyAssessmentSnapshot(
    string ForeignTechnologyReference,
    string SourceLineageReference,
    ForeignUnderstandingState Understanding,
    ForeignOperabilityState Operability,
    ForeignReproductionState Reproduction,
    ForeignAdaptationState Adaptation,
    IReadOnlyList<string> KnownConstraintIds,
    IReadOnlyList<string> EvidenceRefs,
    IReadOnlyList<string> TacitAssetRefs,
    double LastAssessmentYear,
    double Confidence);

public sealed record ForeignTechnologyPackageSnapshot(
    string PackageId,
    string ForeignTechnologyReference,
    string SourceLineageReference,
    IReadOnlyList<string> ComponentIds,
    IReadOnlyList<string> RightIds,
    IReadOnlyList<string> EvidenceRefs,
    IReadOnlyList<string> TacitAssetRefs,
    IReadOnlyList<string> KnowledgeFieldIds,
    string Provenance,
    double Integrity);

public sealed record AdaptiveResearchStateSnapshotV4(
    int SchemaVersion,
    string CatalogId,
    AdaptiveResearchStateSnapshotV3 Research,
    IReadOnlyList<ForeignTechnologyAssessmentSnapshot> ForeignAssessments,
    IReadOnlyList<ForeignTechnologyPackageSnapshot> ForeignPackages);

/// <summary>
/// Preferred standalone Adaptive Research snapshot after foreign-technology runtime integration.
/// V4 restores package/assessment metadata without replaying acquisition and duplicating evidence/tacit assets.
/// </summary>
public sealed class AdaptiveResearchForeignTechnologySnapshotCodec
{
    public const int CurrentSchemaVersion = 4;

    private readonly AdaptiveResearchStrategicRuntime _runtime;
    private readonly AdaptiveResearchStrategicSnapshotCodec _v3;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    public AdaptiveResearchForeignTechnologySnapshotCodec(AdaptiveResearchStrategicRuntime runtime)
    {
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _v3 = new AdaptiveResearchStrategicSnapshotCodec(runtime);
    }

    public AdaptiveResearchStateSnapshotV4 Capture(AdaptiveResearchCivilizationState state)
    {
        var foreign = _runtime.ForeignTechnology.GetState(state);
        return new AdaptiveResearchStateSnapshotV4(
            CurrentSchemaVersion,
            _runtime.Authority.Catalog.Metadata.CatalogId,
            _v3.Capture(state),
            foreign.Assessments.Values
                .OrderBy(value => value.ForeignTechnologyReference, StringComparer.Ordinal)
                .Select(value => new ForeignTechnologyAssessmentSnapshot(
                    value.ForeignTechnologyReference,
                    value.SourceLineageReference,
                    value.Understanding,
                    value.Operability,
                    value.Reproduction,
                    value.Adaptation,
                    value.KnownConstraintIds,
                    value.EvidenceRefs,
                    value.TacitAssetRefs,
                    value.LastAssessmentYear,
                    value.Confidence))
                .ToArray(),
            foreign.Packages.Values
                .OrderBy(value => value.PackageId, StringComparer.Ordinal)
                .Select(value => new ForeignTechnologyPackageSnapshot(
                    value.PackageId,
                    value.ForeignTechnologyReference,
                    value.SourceLineageReference,
                    value.ComponentIds,
                    value.RightIds,
                    value.EvidenceRefs,
                    value.TacitAssetRefs,
                    value.KnowledgeFieldIds,
                    value.Provenance,
                    value.Integrity))
                .ToArray());
    }

    public string Serialize(AdaptiveResearchCivilizationState state) =>
        JsonSerializer.Serialize(Capture(state), _jsonOptions);

    public AdaptiveResearchCivilizationState Deserialize(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("schemaVersion", out var schemaElement))
            throw new InvalidDataException("Adaptive Research snapshot is missing schemaVersion.");
        var schemaVersion = schemaElement.GetInt32();
        if (schemaVersion is >= 1 and <= 3)
            return _v3.Deserialize(json);
        if (schemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research foreign-technology snapshot schema {schemaVersion}.");

        var snapshot = JsonSerializer.Deserialize<AdaptiveResearchStateSnapshotV4>(json, _jsonOptions)
            ?? throw new InvalidDataException("Adaptive Research v4 snapshot deserialized to null.");
        return Restore(snapshot);
    }

    public AdaptiveResearchCivilizationState Restore(AdaptiveResearchStateSnapshotV4 snapshot)
    {
        if (snapshot.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research foreign-technology snapshot schema {snapshot.SchemaVersion}.");
        if (!string.Equals(snapshot.CatalogId, _runtime.Authority.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research snapshot catalog '{snapshot.CatalogId}' does not match runtime catalog '{_runtime.Authority.Catalog.Metadata.CatalogId}'.");

        var state = _v3.Restore(snapshot.Research);
        var foreign = _runtime.ForeignTechnology.GetState(state);

        foreach (var package in snapshot.ForeignPackages)
        {
            ValidatePackageReferences(state, package);
            foreign.AddPackage(new ForeignTechnologyPackageRuntimeState(
                package.PackageId,
                package.ForeignTechnologyReference,
                package.SourceLineageReference,
                package.ComponentIds,
                package.RightIds,
                package.EvidenceRefs,
                package.TacitAssetRefs,
                package.KnowledgeFieldIds,
                package.Provenance,
                package.Integrity,
                foreign.Revision + 1));
        }

        foreach (var assessment in snapshot.ForeignAssessments)
        {
            foreach (var constraintId in assessment.KnownConstraintIds)
                if (!_runtime.ForeignTechnologyCatalog.ConstraintIds.Contains(constraintId))
                    throw new InvalidDataException($"Foreign assessment '{assessment.ForeignTechnologyReference}' references unknown constraint '{constraintId}'.");
            foreach (var evidenceId in assessment.EvidenceRefs)
                if (!state.EvidenceInstances.ContainsKey(evidenceId))
                    throw new InvalidDataException($"Foreign assessment '{assessment.ForeignTechnologyReference}' references missing evidence '{evidenceId}'.");
            foreach (var assetId in assessment.TacitAssetRefs)
                if (!state.Expertise.TacitAssets.ContainsKey(assetId))
                    throw new InvalidDataException($"Foreign assessment '{assessment.ForeignTechnologyReference}' references missing tacit asset '{assetId}'.");
            foreign.SetAssessment(new ForeignTechnologyAssessmentRuntimeState(
                assessment.ForeignTechnologyReference,
                assessment.SourceLineageReference,
                assessment.Understanding,
                assessment.Operability,
                assessment.Reproduction,
                assessment.Adaptation,
                assessment.KnownConstraintIds,
                assessment.EvidenceRefs,
                assessment.TacitAssetRefs,
                assessment.LastAssessmentYear,
                assessment.Confidence,
                foreign.Revision + 1));
        }

        return state;
    }

    private void ValidatePackageReferences(AdaptiveResearchCivilizationState state, ForeignTechnologyPackageSnapshot package)
    {
        if (string.IsNullOrWhiteSpace(package.PackageId) ||
            string.IsNullOrWhiteSpace(package.ForeignTechnologyReference) ||
            string.IsNullOrWhiteSpace(package.SourceLineageReference))
            throw new InvalidDataException("Foreign package snapshot contains an empty stable reference.");
        if (package.Integrity < 0 || package.Integrity > 1 || double.IsNaN(package.Integrity) || double.IsInfinity(package.Integrity))
            throw new InvalidDataException($"Foreign package '{package.PackageId}' has invalid integrity {package.Integrity}.");
        foreach (var componentId in package.ComponentIds)
            if (!_runtime.ForeignTechnologyCatalog.Components.ContainsKey(componentId))
                throw new InvalidDataException($"Foreign package '{package.PackageId}' references unknown component '{componentId}'.");
        foreach (var rightId in package.RightIds)
            if (!_runtime.ForeignTechnologyCatalog.Rights.Contains(rightId))
                throw new InvalidDataException($"Foreign package '{package.PackageId}' references unknown right '{rightId}'.");
        foreach (var fieldId in package.KnowledgeFieldIds)
            if (!_runtime.Authority.ExpertiseCatalog.Fields.ContainsKey(fieldId))
                throw new InvalidDataException($"Foreign package '{package.PackageId}' references unknown field '{fieldId}'.");
        foreach (var evidenceId in package.EvidenceRefs)
            if (!state.EvidenceInstances.ContainsKey(evidenceId))
                throw new InvalidDataException($"Foreign package '{package.PackageId}' references missing evidence '{evidenceId}'.");
        foreach (var assetId in package.TacitAssetRefs)
            if (!state.Expertise.TacitAssets.ContainsKey(assetId))
                throw new InvalidDataException($"Foreign package '{package.PackageId}' references missing tacit asset '{assetId}'.");
    }
}
