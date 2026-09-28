using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ForeignTechnologyAssessmentRuntimeState(
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
    double Confidence,
    long Revision);

public sealed record ForeignTechnologyPackageRuntimeState(
    string PackageId,
    string ForeignTechnologyReference,
    string SourceLineageReference,
    IReadOnlyList<string> ComponentIds,
    IReadOnlyList<string> RightIds,
    IReadOnlyList<string> EvidenceRefs,
    IReadOnlyList<string> TacitAssetRefs,
    IReadOnlyList<string> KnowledgeFieldIds,
    string Provenance,
    double Integrity,
    long Revision);

/// <summary>
/// Sparse acquired/observed foreign technology state. No record exists for unseen lineages/assets.
/// </summary>
public sealed class AdaptiveResearchForeignTechnologyState
{
    private readonly Dictionary<string, ForeignTechnologyAssessmentRuntimeState> _assessments = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ForeignTechnologyPackageRuntimeState> _packages = new(StringComparer.Ordinal);

    public long Revision { get; private set; }

    public IReadOnlyDictionary<string, ForeignTechnologyAssessmentRuntimeState> Assessments =>
        new ReadOnlyDictionary<string, ForeignTechnologyAssessmentRuntimeState>(_assessments);

    public IReadOnlyDictionary<string, ForeignTechnologyPackageRuntimeState> Packages =>
        new ReadOnlyDictionary<string, ForeignTechnologyPackageRuntimeState>(_packages);

    public ForeignTechnologyAssessmentRuntimeState GetOrUnknown(string foreignTechnologyReference, string sourceLineageReference) =>
        _assessments.TryGetValue(foreignTechnologyReference, out var value)
            ? value
            : new ForeignTechnologyAssessmentRuntimeState(
                foreignTechnologyReference,
                sourceLineageReference,
                ForeignUnderstandingState.Unknown,
                ForeignOperabilityState.Unknown,
                ForeignReproductionState.None,
                ForeignAdaptationState.None,
                Array.Empty<string>(),
                Array.Empty<string>(),
                Array.Empty<string>(),
                0,
                0,
                Revision);

    internal void SetAssessment(ForeignTechnologyAssessmentRuntimeState value)
    {
        if (value.Confidence < 0 || value.Confidence > 1 || double.IsNaN(value.Confidence) || double.IsInfinity(value.Confidence))
            throw new ArgumentOutOfRangeException(nameof(value), "Foreign technology confidence must be in 0..1.");
        if (_assessments.TryGetValue(value.ForeignTechnologyReference, out var existing) &&
            !string.Equals(existing.SourceLineageReference, value.SourceLineageReference, StringComparison.Ordinal))
            throw new InvalidOperationException($"Foreign technology '{value.ForeignTechnologyReference}' cannot change source lineage from '{existing.SourceLineageReference}' to '{value.SourceLineageReference}'.");
        _assessments[value.ForeignTechnologyReference] = value with
        {
            KnownConstraintIds = value.KnownConstraintIds.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            EvidenceRefs = value.EvidenceRefs.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            TacitAssetRefs = value.TacitAssetRefs.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            Revision = Revision + 1,
        };
        Revision++;
    }

    internal void AddPackage(ForeignTechnologyPackageRuntimeState value)
    {
        if (value.Integrity < 0 || value.Integrity > 1 || double.IsNaN(value.Integrity) || double.IsInfinity(value.Integrity))
            throw new ArgumentOutOfRangeException(nameof(value), "Foreign technology package integrity must be in 0..1.");
        if (_packages.ContainsKey(value.PackageId))
            throw new InvalidOperationException($"Foreign technology package '{value.PackageId}' already exists for this holder.");
        _packages.Add(value.PackageId, value with
        {
            ComponentIds = value.ComponentIds.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            RightIds = value.RightIds.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            EvidenceRefs = value.EvidenceRefs.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            TacitAssetRefs = value.TacitAssetRefs.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            KnowledgeFieldIds = value.KnowledgeFieldIds.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray(),
            Revision = Revision + 1,
        });
        Revision++;
    }
}
