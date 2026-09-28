using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchFieldCompetenceSnapshot(
    string FieldId,
    ResearchCompetenceVector Current,
    ResearchCompetenceVector HistoricalPeak,
    double LastTheoreticalActivityYear,
    double LastExperimentalActivityYear,
    double LastEngineeringActivityYear);

public sealed record ResearchInstitutionSnapshot(
    string InstitutionInstanceId,
    string InstitutionArchetypeId,
    string? ContextId,
    int TotalCount,
    int ActiveCount);

public sealed record ResearchTacitAssetSnapshot(
    string AssetId,
    string AssetTypeId,
    ResearchTacitScopeKind ScopeKind,
    string ScopeRef,
    ResearchTacitAssimilationStage AssimilationStage,
    double Depth,
    double Availability,
    double TranslationContextQuality,
    double TrainingContinuity,
    string Provenance,
    string? ContextId);

public sealed record AdaptiveResearchExpertiseSnapshot(
    IReadOnlyList<ResearchFieldCompetenceSnapshot> Fields,
    IReadOnlyList<ResearchInstitutionSnapshot> Institutions,
    IReadOnlyList<ResearchTacitAssetSnapshot> TacitAssets);

public sealed record AdaptiveResearchStateSnapshotV2(
    int SchemaVersion,
    string CatalogId,
    AdaptiveResearchStateSnapshot Core,
    AdaptiveResearchExpertiseSnapshot Expertise);

/// <summary>
/// Preferred standalone Adaptive Research snapshot envelope. Schema v2 adds sparse expertise state
/// while retaining the validated v1 core payload. It is still not the global campaign save format.
/// </summary>
public sealed class AdaptiveResearchSnapshotV2Codec
{
    public const int CurrentSchemaVersion = 2;

    private readonly AdaptiveResearchAuthority _authority;
    private readonly AdaptiveResearchSnapshotCodec _v1;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    public AdaptiveResearchSnapshotV2Codec(AdaptiveResearchAuthority authority)
    {
        _authority = authority ?? throw new ArgumentNullException(nameof(authority));
        _v1 = new AdaptiveResearchSnapshotCodec(authority.Kernel);
    }

    public AdaptiveResearchStateSnapshotV2 Capture(AdaptiveResearchCivilizationState state)
    {
        var expertise = state.Expertise;
        return new AdaptiveResearchStateSnapshotV2(
            CurrentSchemaVersion,
            _authority.Catalog.Metadata.CatalogId,
            _v1.Capture(state),
            new AdaptiveResearchExpertiseSnapshot(
                expertise.FieldCompetence.Values
                    .OrderBy(value => value.FieldId, StringComparer.Ordinal)
                    .Select(value => new ResearchFieldCompetenceSnapshot(
                        value.FieldId,
                        value.Current,
                        value.HistoricalPeak,
                        value.LastTheoreticalActivityYear,
                        value.LastExperimentalActivityYear,
                        value.LastEngineeringActivityYear))
                    .ToArray(),
                expertise.Institutions.Values
                    .OrderBy(value => value.InstitutionInstanceId, StringComparer.Ordinal)
                    .Select(value => new ResearchInstitutionSnapshot(
                        value.InstitutionInstanceId,
                        value.InstitutionArchetypeId,
                        value.ContextId,
                        value.TotalCount,
                        value.ActiveCount))
                    .ToArray(),
                expertise.TacitAssets.Values
                    .OrderBy(value => value.AssetId, StringComparer.Ordinal)
                    .Select(value => new ResearchTacitAssetSnapshot(
                        value.AssetId,
                        value.AssetTypeId,
                        value.ScopeKind,
                        value.ScopeRef,
                        value.AssimilationStage,
                        value.Depth,
                        value.Availability,
                        value.TranslationContextQuality,
                        value.TrainingContinuity,
                        value.Provenance,
                        value.ContextId))
                    .ToArray()));
    }

    public string Serialize(AdaptiveResearchCivilizationState state) =>
        JsonSerializer.Serialize(Capture(state), _jsonOptions);

    public AdaptiveResearchCivilizationState Deserialize(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (!document.RootElement.TryGetProperty("schemaVersion", out var schemaElement))
            throw new InvalidDataException("Adaptive Research snapshot is missing schemaVersion.");
        var schemaVersion = schemaElement.GetInt32();
        if (schemaVersion == AdaptiveResearchSnapshotCodec.CurrentSchemaVersion)
            return _v1.Deserialize(json);
        if (schemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research snapshot schema {schemaVersion}.");

        var snapshot = JsonSerializer.Deserialize<AdaptiveResearchStateSnapshotV2>(json, _jsonOptions)
            ?? throw new InvalidDataException("Adaptive Research v2 snapshot deserialized to null.");
        return Restore(snapshot);
    }

    public AdaptiveResearchCivilizationState Restore(AdaptiveResearchStateSnapshotV2 snapshot)
    {
        if (snapshot.SchemaVersion != CurrentSchemaVersion)
            throw new InvalidDataException($"Unsupported Adaptive Research snapshot schema {snapshot.SchemaVersion}.");
        if (!string.Equals(snapshot.CatalogId, _authority.Catalog.Metadata.CatalogId, StringComparison.Ordinal))
            throw new InvalidDataException($"Adaptive Research snapshot catalog '{snapshot.CatalogId}' does not match runtime catalog '{_authority.Catalog.Metadata.CatalogId}'.");

        var state = _v1.Restore(snapshot.Core);
        foreach (var field in snapshot.Expertise.Fields)
        {
            if (!_authority.ExpertiseCatalog.Fields.ContainsKey(field.FieldId))
                throw new InvalidDataException($"Snapshot references unknown competence field '{field.FieldId}'.");
            ValidateCompetenceVector(field.Current, field.FieldId, "current");
            ValidateCompetenceVector(field.HistoricalPeak, field.FieldId, "historicalPeak");
            if (field.Current.Theoretical > field.HistoricalPeak.Theoretical + 0.000001 ||
                field.Current.Experimental > field.HistoricalPeak.Experimental + 0.000001 ||
                field.Current.Engineering > field.HistoricalPeak.Engineering + 0.000001)
                throw new InvalidDataException($"Current competence exceeds historical peak for field '{field.FieldId}'.");
            state.Expertise.SetField(new ResearchFieldCompetenceRuntimeState(
                field.FieldId,
                field.Current,
                field.HistoricalPeak,
                field.LastTheoreticalActivityYear,
                field.LastExperimentalActivityYear,
                field.LastEngineeringActivityYear,
                state.Expertise.Revision + 1));
        }

        foreach (var institution in snapshot.Expertise.Institutions)
        {
            if (!_authority.ExpertiseCatalog.Institutions.ContainsKey(institution.InstitutionArchetypeId))
                throw new InvalidDataException($"Snapshot references unknown research institution '{institution.InstitutionArchetypeId}'.");
            _authority.Expertise.SetInstitution(
                state,
                institution.InstitutionInstanceId,
                institution.InstitutionArchetypeId,
                institution.TotalCount,
                institution.ActiveCount,
                institution.ContextId);
        }

        foreach (var asset in snapshot.Expertise.TacitAssets)
        {
            _authority.Expertise.SetTacitAsset(
                state,
                asset.AssetId,
                asset.AssetTypeId,
                asset.ScopeKind,
                asset.ScopeRef,
                asset.AssimilationStage,
                asset.Depth,
                asset.Availability,
                asset.TranslationContextQuality,
                asset.TrainingContinuity,
                asset.Provenance,
                asset.ContextId);
        }

        // Restoring institutions recalculates their derived facility capabilities. The saved
        // core list can also contain capabilities supplied by physical campaign facilities
        // (for example the Warp Test Facility), so reinstate the complete authoritative set
        // after institution reconstruction instead of silently dropping those capabilities.
        state.SetFacilityCapabilities(snapshot.Core.FacilityCapabilities);

        if (snapshot.Expertise.Institutions.Count > 0 &&
            Math.Abs(state.TotalEffectiveResearchLabs - snapshot.Core.TotalEffectiveResearchLabs) > 0.000001)
            throw new InvalidDataException("Expertise institution capacity does not reproduce the saved core Effective Research Lab total.");

        RecalculateActiveReadiness(state);
        state.MarkViewDirty();
        return state;
    }

    private void RecalculateActiveReadiness(AdaptiveResearchCivilizationState state)
    {
        foreach (var project in state.ActiveProjects.Values.ToArray())
        {
            var breakdown = _authority.GetProjectReadiness(
                state,
                project.NodeId,
                project.Stage,
                project.AssignedEffectiveLabs,
                project.TargetApplicabilityContextId);
            state.SetProject(project with { ReadinessEfficiency = breakdown.RpEfficiency });
        }
    }

    private static void ValidateCompetenceVector(ResearchCompetenceVector value, string fieldId, string label)
    {
        ValidateCompetence(value.Theoretical, fieldId, label, "theoretical");
        ValidateCompetence(value.Experimental, fieldId, label, "experimental");
        ValidateCompetence(value.Engineering, fieldId, label, "engineering");
    }

    private static void ValidateCompetence(double value, string fieldId, string label, string component)
    {
        if (value < 0.0 || value > 100.0 || double.IsNaN(value) || double.IsInfinity(value))
            throw new InvalidDataException($"Invalid {label} {component} competence {value} for field '{fieldId}'.");
    }
}
