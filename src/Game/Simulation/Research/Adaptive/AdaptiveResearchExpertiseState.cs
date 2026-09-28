using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchFieldCompetenceRuntimeState(
    string FieldId,
    ResearchCompetenceVector Current,
    ResearchCompetenceVector HistoricalPeak,
    double LastTheoreticalActivityYear,
    double LastExperimentalActivityYear,
    double LastEngineeringActivityYear,
    long Revision);

public sealed record ResearchInstitutionRuntimeState(
    string InstitutionInstanceId,
    string InstitutionArchetypeId,
    string? ContextId,
    int TotalCount,
    int ActiveCount,
    long Revision)
{
    public bool IsActive => ActiveCount > 0;
}

public sealed record ResearchTacitAssetRuntimeState(
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
    string? ContextId,
    long Revision);

/// <summary>
/// Sparse sidecar state for active competence, research institutions and tacit expertise.
/// Kept separate from the milestone-13 core state so each runtime layer stays bounded and independently serializable.
/// </summary>
public sealed class AdaptiveResearchExpertiseState
{
    private readonly Dictionary<string, ResearchFieldCompetenceRuntimeState> _fieldCompetence = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ResearchInstitutionRuntimeState> _institutions = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ResearchTacitAssetRuntimeState> _tacitAssets = new(StringComparer.Ordinal);

    public long Revision { get; private set; }

    public IReadOnlyDictionary<string, ResearchFieldCompetenceRuntimeState> FieldCompetence =>
        new ReadOnlyDictionary<string, ResearchFieldCompetenceRuntimeState>(_fieldCompetence);

    public IReadOnlyDictionary<string, ResearchInstitutionRuntimeState> Institutions =>
        new ReadOnlyDictionary<string, ResearchInstitutionRuntimeState>(_institutions);

    public IReadOnlyDictionary<string, ResearchTacitAssetRuntimeState> TacitAssets =>
        new ReadOnlyDictionary<string, ResearchTacitAssetRuntimeState>(_tacitAssets);

    public ResearchFieldCompetenceRuntimeState GetField(string fieldId) =>
        _fieldCompetence.TryGetValue(fieldId, out var state)
            ? state
            : new ResearchFieldCompetenceRuntimeState(
                fieldId,
                new ResearchCompetenceVector(0, 0, 0),
                new ResearchCompetenceVector(0, 0, 0),
                double.NegativeInfinity,
                double.NegativeInfinity,
                double.NegativeInfinity,
                Revision);

    public double TotalActiveEffectiveLabUnits(AdaptiveResearchExpertiseCatalog catalog) =>
        _institutions.Values.Sum(institution =>
        {
            var definition = catalog.Institutions[institution.InstitutionArchetypeId];
            return definition.EffectiveLabUnits * institution.ActiveCount;
        });

    internal void SetField(ResearchFieldCompetenceRuntimeState state)
    {
        ValidateVector(state.Current, state.FieldId);
        ValidateVector(state.HistoricalPeak, state.FieldId);
        _fieldCompetence[state.FieldId] = state with { Revision = Revision + 1 };
        Touch();
    }

    internal void RemoveFieldIfZero(string fieldId)
    {
        if (!_fieldCompetence.TryGetValue(fieldId, out var state))
            return;
        if (state.Current.Theoretical > 0 || state.Current.Experimental > 0 || state.Current.Engineering > 0 ||
            state.HistoricalPeak.Theoretical > 0 || state.HistoricalPeak.Experimental > 0 || state.HistoricalPeak.Engineering > 0)
            return;
        _fieldCompetence.Remove(fieldId);
        Touch();
    }

    internal void SetInstitution(ResearchInstitutionRuntimeState state)
    {
        if (state.TotalCount < 0 || state.ActiveCount < 0 || state.ActiveCount > state.TotalCount)
            throw new ArgumentOutOfRangeException(nameof(state), $"Invalid institution counts for '{state.InstitutionInstanceId}'.");
        if (state.TotalCount == 0)
        {
            if (_institutions.Remove(state.InstitutionInstanceId))
                Touch();
            return;
        }
        _institutions[state.InstitutionInstanceId] = state with { Revision = Revision + 1 };
        Touch();
    }

    internal bool RemoveInstitution(string institutionInstanceId)
    {
        if (!_institutions.Remove(institutionInstanceId))
            return false;
        Touch();
        return true;
    }

    internal void SetTacitAsset(ResearchTacitAssetRuntimeState state)
    {
        ValidateUnit(state.Depth, nameof(state.Depth), 100.0);
        ValidateUnit(state.Availability, nameof(state.Availability), 1.0);
        ValidateUnit(state.TranslationContextQuality, nameof(state.TranslationContextQuality), 1.0);
        ValidateUnit(state.TrainingContinuity, nameof(state.TrainingContinuity), 1.0);
        _tacitAssets[state.AssetId] = state with { Revision = Revision + 1 };
        Touch();
    }

    internal bool RemoveTacitAsset(string assetId)
    {
        if (!_tacitAssets.Remove(assetId))
            return false;
        Touch();
        return true;
    }

    private void Touch() => Revision++;

    private static void ValidateVector(ResearchCompetenceVector value, string fieldId)
    {
        ValidateUnit(value.Theoretical, $"{fieldId}.theoretical", 100.0);
        ValidateUnit(value.Experimental, $"{fieldId}.experimental", 100.0);
        ValidateUnit(value.Engineering, $"{fieldId}.engineering", 100.0);
    }

    private static void ValidateUnit(double value, string name, double maximum)
    {
        if (value < 0 || value > maximum || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(name, value, $"Value must be in 0..{maximum}.");
    }
}
