using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;

namespace Game.Simulation.Research.Adaptive;

public sealed record ForeignTechnologyEvidenceTransfer(
    string EvidenceInstanceId,
    string EvidenceTypeId,
    string Provenance,
    double Quality,
    double Confidence,
    string? ContextId = null);

public sealed record ForeignTechnologyPackageInput(
    string PackageId,
    string ForeignTechnologyReference,
    string SourceLineageReference,
    IReadOnlyList<string> ComponentIds,
    IReadOnlyList<string> RightIds,
    IReadOnlyList<string> KnowledgeFieldIds,
    IReadOnlyList<ForeignTechnologyEvidenceTransfer> Evidence,
    IReadOnlyList<string> KnownConstraintIds,
    string Provenance,
    double Integrity,
    double TranslationContextQuality,
    double TrainingContinuity,
    double AcquiredYear,
    string? TargetApplicabilityContextId = null);

public sealed record ForeignTechnologyRecipientValueContext(
    double CapabilityNovelty,
    double StrategicNeed,
    double ExpectedNativeWorkSaved,
    double RecipientReadiness,
    double RecipientOperabilityFit,
    double DependencySafety,
    double KnownThirdPartyDemand,
    double PackageTransferability,
    double ScarcityOrExclusivity,
    double DependencyRisk,
    double HazardRisk);

public sealed record ForeignTechnologyRecipientValueAssessment(
    double ResearchUtility,
    double OperationalUtility,
    double ResaleOrBrokerageUtility,
    double DependencyRisk,
    double HazardRisk,
    bool HolderIncompatibilityCanStillBroker,
    string Explanation);

/// <summary>
/// Executable foreign-technology package/assessment runtime. Diplomacy, Intelligence, conquest and
/// physical systems report factual possession/tests; this layer owns research-side evidence,
/// tacit assets, assessment axes and recipient-specific value.
/// </summary>
public sealed class AdaptiveResearchForeignTechnologyRuntime
{
    private readonly AdaptiveResearchAuthority _authority;
    private readonly AdaptiveResearchForeignTechnologyCatalog _catalog;
    private readonly ConditionalWeakTable<AdaptiveResearchCivilizationState, AdaptiveResearchForeignTechnologyState> _states = new();

    public AdaptiveResearchForeignTechnologyRuntime(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchForeignTechnologyCatalog catalog)
    {
        _authority = authority ?? throw new ArgumentNullException(nameof(authority));
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
    }

    public AdaptiveResearchForeignTechnologyState GetState(AdaptiveResearchCivilizationState state) =>
        _states.GetValue(state ?? throw new ArgumentNullException(nameof(state)), _ => new AdaptiveResearchForeignTechnologyState());

    public ForeignTechnologyAssessmentRuntimeState Observe(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        string sourceLineageReference,
        double confidence,
        double year,
        IEnumerable<string>? knownConstraintIds = null)
    {
        ValidateReference(foreignTechnologyReference, nameof(foreignTechnologyReference));
        ValidateReference(sourceLineageReference, nameof(sourceLineageReference));
        ValidateConfidence(confidence);
        var foreign = GetState(state);
        var current = foreign.GetOrUnknown(foreignTechnologyReference, sourceLineageReference);
        var constraints = MergeConstraints(current.KnownConstraintIds, knownConstraintIds ?? Array.Empty<string>());
        var next = current with
        {
            Understanding = Max(current.Understanding, ForeignUnderstandingState.Observed),
            KnownConstraintIds = constraints,
            LastAssessmentYear = year,
            Confidence = Math.Max(current.Confidence, Math.Max(confidence, _catalog.RuntimePolicy.NewObservationConfidence)),
        };
        foreign.SetAssessment(next);
        return foreign.Assessments[foreignTechnologyReference];
    }

    public ForeignTechnologyAssessmentRuntimeState AcquirePackage(
        AdaptiveResearchCivilizationState state,
        ForeignTechnologyPackageInput input)
    {
        ValidatePackageInput(input);
        var foreign = GetState(state);
        if (foreign.Packages.ContainsKey(input.PackageId))
            throw new InvalidOperationException($"Foreign technology package '{input.PackageId}' is already held.");

        var evidenceRefs = new List<string>();
        foreach (var evidence in input.Evidence)
        {
            var quality = Math.Clamp(evidence.Quality * input.Integrity, 0.0, 1.0);
            _authority.AddEvidence(
                state,
                evidence.EvidenceInstanceId,
                evidence.EvidenceTypeId,
                evidence.Provenance,
                quality,
                evidence.Confidence,
                evidence.ContextId ?? input.TargetApplicabilityContextId);
            evidenceRefs.Add(evidence.EvidenceInstanceId);
        }

        var tacitRefs = new List<string>();
        foreach (var componentId in input.ComponentIds.Distinct(StringComparer.Ordinal))
        {
            var component = _catalog.Components[componentId];
            foreach (var assetTypeId in component.TacitAssetTypeIds)
            {
                foreach (var fieldId in input.KnowledgeFieldIds.Distinct(StringComparer.Ordinal))
                {
                    var assetId = $"foreign:{input.PackageId}:{componentId}:{assetTypeId}:{fieldId}";
                    _authority.SetTacitAsset(
                        state,
                        assetId,
                        assetTypeId,
                        ResearchTacitScopeKind.KnowledgeField,
                        fieldId,
                        ResearchTacitAssimilationStage.Access,
                        100.0 * input.Integrity,
                        1.0,
                        input.TranslationContextQuality,
                        input.TrainingContinuity,
                        input.Provenance,
                        input.TargetApplicabilityContextId);
                    tacitRefs.Add(assetId);
                }
            }
        }

        foreign.AddPackage(new ForeignTechnologyPackageRuntimeState(
            input.PackageId,
            input.ForeignTechnologyReference,
            input.SourceLineageReference,
            input.ComponentIds,
            input.RightIds,
            evidenceRefs,
            tacitRefs,
            input.KnowledgeFieldIds,
            input.Provenance,
            input.Integrity,
            foreign.Revision + 1));

        var current = foreign.GetOrUnknown(input.ForeignTechnologyReference, input.SourceLineageReference);
        var intakeUnderstanding = input.ComponentIds.Count == 0
            ? ForeignUnderstandingState.Observed
            : input.ComponentIds.Max(componentId => _catalog.RuntimePolicy.MinimumUnderstandingByComponent[componentId]);
        var nextUnderstanding = Max(current.Understanding, intakeUnderstanding);
        var adaptation = current.Adaptation;
        if (_catalog.RuntimePolicy.ConceptualInspirationIfCharacterized && nextUnderstanding >= ForeignUnderstandingState.Characterized)
            adaptation = Max(adaptation, ForeignAdaptationState.ConceptualInspiration);
        var confidenceFloor = nextUnderstanding >= ForeignUnderstandingState.Characterized
            ? _catalog.RuntimePolicy.NewCharacterizedConfidence
            : _catalog.RuntimePolicy.NewObservationConfidence;
        var nextAssessment = current with
        {
            Understanding = nextUnderstanding,
            Adaptation = adaptation,
            KnownConstraintIds = MergeConstraints(current.KnownConstraintIds, input.KnownConstraintIds),
            EvidenceRefs = current.EvidenceRefs.Concat(evidenceRefs).Distinct(StringComparer.Ordinal).ToArray(),
            TacitAssetRefs = current.TacitAssetRefs.Concat(tacitRefs).Distinct(StringComparer.Ordinal).ToArray(),
            LastAssessmentYear = input.AcquiredYear,
            Confidence = Math.Max(current.Confidence, confidenceFloor * input.Integrity),
        };
        foreign.SetAssessment(nextAssessment);
        return foreign.Assessments[input.ForeignTechnologyReference];
    }

    public ForeignTechnologyAssessmentRuntimeState RecordAnalysisResult(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignUnderstandingState targetUnderstanding,
        double confidence,
        double year,
        IEnumerable<string>? newlyKnownConstraintIds = null)
    {
        ValidateConfidence(confidence);
        var foreign = GetState(state);
        if (!foreign.Assessments.TryGetValue(foreignTechnologyReference, out var current))
            throw new InvalidOperationException($"Cannot analyze unobserved foreign technology '{foreignTechnologyReference}'.");
        if (targetUnderstanding < current.Understanding)
            throw new InvalidOperationException("Controlled analysis cannot erase established foreign-technology understanding.");
        if (targetUnderstanding >= ForeignUnderstandingState.PrincipleUnderstood && confidence < _catalog.RuntimePolicy.ControlledAnalysisConfidenceMinimum)
            throw new InvalidOperationException("Principle-understood assessment requires stronger controlled-analysis confidence.");
        if (targetUnderstanding == ForeignUnderstandingState.EngineeringUnderstood && confidence < _catalog.RuntimePolicy.EngineeringUnderstoodConfidenceMinimum)
            throw new InvalidOperationException("Engineering-understood assessment requires the configured high confidence.");

        var adaptation = current.Adaptation;
        if (_catalog.RuntimePolicy.ConceptualInspirationIfCharacterized && targetUnderstanding >= ForeignUnderstandingState.Characterized)
            adaptation = Max(adaptation, ForeignAdaptationState.ConceptualInspiration);
        foreign.SetAssessment(current with
        {
            Understanding = targetUnderstanding,
            Adaptation = adaptation,
            KnownConstraintIds = MergeConstraints(current.KnownConstraintIds, newlyKnownConstraintIds ?? Array.Empty<string>()),
            LastAssessmentYear = year,
            Confidence = Math.Max(current.Confidence, confidence),
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    /// <summary>
    /// Current operability can improve or regress as real dependencies appear/disappear.
    /// </summary>
    public ForeignTechnologyAssessmentRuntimeState RecordOperabilityFact(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignOperabilityState operability,
        double confidence,
        double year)
    {
        RequireOperationalConfidence(confidence);
        var foreign = GetState(state);
        var current = RequireAssessment(foreign, foreignTechnologyReference);
        foreign.SetAssessment(current with
        {
            Operability = operability,
            LastAssessmentYear = year,
            Confidence = Math.Max(current.Confidence, confidence),
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    /// <summary>
    /// Current reproduction ability can regress if foreign tooling/process/institutions are lost.
    /// </summary>
    public ForeignTechnologyAssessmentRuntimeState RecordReproductionFact(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignReproductionState reproduction,
        double confidence,
        double year)
    {
        RequireOperationalConfidence(confidence);
        var foreign = GetState(state);
        var current = RequireAssessment(foreign, foreignTechnologyReference);
        foreign.SetAssessment(current with
        {
            Reproduction = reproduction,
            LastAssessmentYear = year,
            Confidence = Math.Max(current.Confidence, confidence),
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    public ForeignTechnologyAssessmentRuntimeState RecordAdaptationResult(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        ForeignAdaptationState adaptation,
        double confidence,
        double year)
    {
        ValidateConfidence(confidence);
        var foreign = GetState(state);
        var current = RequireAssessment(foreign, foreignTechnologyReference);
        if (adaptation < current.Adaptation)
            throw new InvalidOperationException("A completed foreign-derived native adaptation lineage cannot be unlearned by reassessment.");
        foreign.SetAssessment(current with
        {
            Adaptation = adaptation,
            LastAssessmentYear = year,
            Confidence = Math.Max(current.Confidence, confidence),
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    public ForeignTechnologyAssessmentRuntimeState ConfirmConstraint(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        string constraintId,
        double year)
    {
        ValidateConstraint(constraintId);
        var foreign = GetState(state);
        var current = RequireAssessment(foreign, foreignTechnologyReference);
        foreign.SetAssessment(current with
        {
            KnownConstraintIds = MergeConstraints(current.KnownConstraintIds, new[] { constraintId }),
            LastAssessmentYear = year,
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    public ForeignTechnologyAssessmentRuntimeState ResolveConstraint(
        AdaptiveResearchCivilizationState state,
        string foreignTechnologyReference,
        string constraintId,
        double year)
    {
        ValidateConstraint(constraintId);
        var foreign = GetState(state);
        var current = RequireAssessment(foreign, foreignTechnologyReference);
        foreign.SetAssessment(current with
        {
            KnownConstraintIds = current.KnownConstraintIds.Where(id => !string.Equals(id, constraintId, StringComparison.Ordinal)).ToArray(),
            LastAssessmentYear = year,
        });
        return foreign.Assessments[foreignTechnologyReference];
    }

    public void AdvancePackageTacitAssimilation(
        AdaptiveResearchCivilizationState state,
        string packageId,
        ResearchTacitAssimilationStage targetStage)
    {
        var foreign = GetState(state);
        if (!foreign.Packages.TryGetValue(packageId, out var package))
            throw new KeyNotFoundException($"Unknown held foreign technology package '{packageId}'.");
        foreach (var assetId in package.TacitAssetRefs)
        {
            if (!state.Expertise.TacitAssets.TryGetValue(assetId, out var asset))
                continue;
            if (targetStage < asset.AssimilationStage)
                throw new InvalidOperationException("Tacit assimilation cannot move backward through this command.");
            if ((int)targetStage > (int)asset.AssimilationStage + 1)
                throw new InvalidOperationException("Tacit assimilation must advance through adjacent stages so real translation/training steps are represented.");
            if (targetStage > ResearchTacitAssimilationStage.Access &&
                asset.TranslationContextQuality < _catalog.RuntimePolicy.MinimumTranslationQualityBeyondAccess)
                throw new InvalidOperationException("Translation/context quality is insufficient to advance foreign tacit knowledge beyond Access.");
            if (targetStage >= ResearchTacitAssimilationStage.Trained &&
                asset.TrainingContinuity < _catalog.RuntimePolicy.MinimumTrainingContinuityForTrained)
                throw new InvalidOperationException("Training continuity is insufficient to establish Trained foreign practice.");

            _authority.SetTacitAsset(
                state,
                asset.AssetId,
                asset.AssetTypeId,
                asset.ScopeKind,
                asset.ScopeRef,
                targetStage,
                asset.Depth,
                asset.Availability,
                asset.TranslationContextQuality,
                asset.TrainingContinuity,
                asset.Provenance,
                asset.ContextId);
        }
    }

    public ForeignTechnologyRecipientValueAssessment EvaluateRecipientValue(
        ForeignTechnologyPackageRuntimeState package,
        ForeignTechnologyRecipientValueContext context,
        bool holderCurrentlyUnusable)
    {
        ValidateValueContext(context);
        var completeness = package.Integrity * 100.0;
        var research = Mean(context.CapabilityNovelty, context.StrategicNeed, context.ExpectedNativeWorkSaved, context.RecipientReadiness, completeness);
        var operational = Mean(context.RecipientOperabilityFit, completeness, context.DependencySafety);
        var resale = Mean(context.KnownThirdPartyDemand, context.PackageTransferability, context.ScarcityOrExclusivity);
        return new ForeignTechnologyRecipientValueAssessment(
            research,
            operational,
            resale,
            context.DependencyRisk,
            context.HazardRisk,
            holderCurrentlyUnusable && resale > 0,
            holderCurrentlyUnusable && resale >= Math.Max(research, operational)
                ? "The current holder may be unable to use the technology, but legitimately known third-party demand makes brokerage valuable."
                : "Value is recipient-specific across research, operation, dependencies, hazards, scarcity and resale demand; no universal currency price is assigned by Research.");
    }

    private void ValidatePackageInput(ForeignTechnologyPackageInput input)
    {
        ValidateReference(input.PackageId, nameof(input.PackageId));
        ValidateReference(input.ForeignTechnologyReference, nameof(input.ForeignTechnologyReference));
        ValidateReference(input.SourceLineageReference, nameof(input.SourceLineageReference));
        if (input.Integrity < 0 || input.Integrity > 1 || double.IsNaN(input.Integrity) || double.IsInfinity(input.Integrity))
            throw new ArgumentOutOfRangeException(nameof(input.Integrity));
        ValidateUnit(input.TranslationContextQuality, nameof(input.TranslationContextQuality));
        ValidateUnit(input.TrainingContinuity, nameof(input.TrainingContinuity));
        foreach (var componentId in input.ComponentIds)
            if (!_catalog.Components.ContainsKey(componentId))
                throw new ArgumentException($"Unknown technology-transfer component '{componentId}'.", nameof(input.ComponentIds));
        foreach (var rightId in input.RightIds)
            if (!_catalog.Rights.Contains(rightId))
                throw new ArgumentException($"Unknown technology-transfer right '{rightId}'.", nameof(input.RightIds));
        foreach (var fieldId in input.KnowledgeFieldIds)
            if (!_authority.ExpertiseCatalog.Fields.ContainsKey(fieldId))
                throw new ArgumentException($"Unknown research knowledge field '{fieldId}'.", nameof(input.KnowledgeFieldIds));
        foreach (var constraintId in input.KnownConstraintIds)
            ValidateConstraint(constraintId);
        foreach (var evidence in input.Evidence)
        {
            if (!_authority.Catalog.EvidenceTypeIds.Contains(evidence.EvidenceTypeId))
                throw new ArgumentException($"Unknown foreign package evidence type '{evidence.EvidenceTypeId}'.", nameof(input.Evidence));
            ValidateUnit(evidence.Quality, nameof(evidence.Quality));
            ValidateUnit(evidence.Confidence, nameof(evidence.Confidence));
        }
    }

    private ForeignTechnologyAssessmentRuntimeState RequireAssessment(
        AdaptiveResearchForeignTechnologyState state,
        string foreignTechnologyReference) =>
        state.Assessments.TryGetValue(foreignTechnologyReference, out var value)
            ? value
            : throw new KeyNotFoundException($"Foreign technology '{foreignTechnologyReference}' has not been legitimately observed/acquired.");

    private IReadOnlyList<string> MergeConstraints(IEnumerable<string> existing, IEnumerable<string> added)
    {
        var result = existing.Concat(added).Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray();
        foreach (var id in result)
            ValidateConstraint(id);
        return result;
    }

    private void ValidateConstraint(string constraintId)
    {
        if (!_catalog.ConstraintIds.Contains(constraintId))
            throw new ArgumentException($"Unknown foreign-technology compatibility constraint '{constraintId}'.", nameof(constraintId));
    }

    private void RequireOperationalConfidence(double confidence)
    {
        ValidateConfidence(confidence);
        if (confidence < _catalog.RuntimePolicy.OperationalFactConfidenceMinimum)
            throw new InvalidOperationException("Operational/reproduction facts require the configured controlled-test confidence.");
    }

    private static T Max<T>(T left, T right) where T : struct, Enum =>
        Convert.ToInt32(left) >= Convert.ToInt32(right) ? left : right;

    private static double Mean(params double[] values) => values.Length == 0 ? 0.0 : values.Average();

    private static void ValidateReference(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new ArgumentException("Reference cannot be empty.", name);
    }

    private static void ValidateConfidence(double value) => ValidateUnit(value, nameof(value));

    private static void ValidateUnit(double value, string name)
    {
        if (value < 0 || value > 1 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(name, value, "Value must be in 0..1.");
    }

    private static void Validate100(double value, string name)
    {
        if (value < 0 || value > 100 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(name, value, "Value must be in 0..100.");
    }

    private static void ValidateValueContext(ForeignTechnologyRecipientValueContext context)
    {
        Validate100(context.CapabilityNovelty, nameof(context.CapabilityNovelty));
        Validate100(context.StrategicNeed, nameof(context.StrategicNeed));
        Validate100(context.ExpectedNativeWorkSaved, nameof(context.ExpectedNativeWorkSaved));
        Validate100(context.RecipientReadiness, nameof(context.RecipientReadiness));
        Validate100(context.RecipientOperabilityFit, nameof(context.RecipientOperabilityFit));
        Validate100(context.DependencySafety, nameof(context.DependencySafety));
        Validate100(context.KnownThirdPartyDemand, nameof(context.KnownThirdPartyDemand));
        Validate100(context.PackageTransferability, nameof(context.PackageTransferability));
        Validate100(context.ScarcityOrExclusivity, nameof(context.ScarcityOrExclusivity));
        Validate100(context.DependencyRisk, nameof(context.DependencyRisk));
        Validate100(context.HazardRisk, nameof(context.HazardRisk));
    }
}
