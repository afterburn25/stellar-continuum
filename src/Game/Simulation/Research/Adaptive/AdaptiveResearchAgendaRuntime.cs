using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;

namespace Game.Simulation.Research.Adaptive;

public sealed record ResearchPerceivedAdequacyAssessment(
    string DomainId,
    double Adequacy,
    double CredibleChallenge,
    double Confidence);

public sealed record ResearchAgendaReviewRecommendation(
    string DomainId,
    string RecommendedPriorityId,
    double RelevantPressure,
    double ComplacencyIndex,
    double ChallengeIndex,
    string Explanation);

public sealed record ResearchProjectUtilityComponent(string Id, double Score, string Explanation);

public sealed record ResearchVisibleProjectCandidate(
    string NodeId,
    double UtilityScore,
    bool CanStart,
    double RequestedEffectiveLabs,
    double EstimatedYearsToMature,
    IReadOnlyList<ResearchProjectUtilityComponent> Components,
    IReadOnlyList<ResearchBlocker> Blockers,
    string Explanation);

/// <summary>
/// High-level agenda, culture and fair-information project planning. It reads only visible
/// Investigable node state and never queries Unknown graph nodes for candidate generation.
/// </summary>
public sealed class AdaptiveResearchAgendaRuntime
{
    private sealed record CandidateCache(
        long CoreRevision,
        long ExpertiseRevision,
        long AgendaRevision,
        IReadOnlyList<ResearchVisibleProjectCandidate> Candidates);

    private readonly AdaptiveResearchAuthority _authority;
    private readonly AdaptiveResearchAgendaCatalog _catalog;
    private readonly ConditionalWeakTable<AdaptiveResearchCivilizationState, AdaptiveResearchAgendaState> _states = new();
    private readonly ConditionalWeakTable<AdaptiveResearchCivilizationState, CacheHolder> _caches = new();

    private sealed class CacheHolder
    {
        public CandidateCache? Value { get; set; }
    }

    public AdaptiveResearchAgendaRuntime(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchAgendaCatalog catalog)
    {
        _authority = authority ?? throw new ArgumentNullException(nameof(authority));
        _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
    }

    public AdaptiveResearchAgendaState GetState(AdaptiveResearchCivilizationState state) =>
        _states.GetValue(state ?? throw new ArgumentNullException(nameof(state)), _ => new AdaptiveResearchAgendaState(_catalog));

    public void SetDomainPriority(AdaptiveResearchCivilizationState state, string domainId, string priorityId)
    {
        if (!_authority.Catalog.Nodes.Values.Any(node => string.Equals(node.DomainId, domainId, StringComparison.Ordinal)))
            throw new ArgumentException($"Unknown research domain '{domainId}'.", nameof(domainId));
        SetPriority(GetState(state), AgendaTarget.Domain, domainId, priorityId);
    }

    public void SetFieldPriority(AdaptiveResearchCivilizationState state, string fieldId, string priorityId)
    {
        if (!_authority.ExpertiseCatalog.Fields.ContainsKey(fieldId))
            throw new ArgumentException($"Unknown research field '{fieldId}'.", nameof(fieldId));
        SetPriority(GetState(state), AgendaTarget.Field, fieldId, priorityId);
    }

    public void SetProblemPriority(AdaptiveResearchCivilizationState state, string pressureId, string priorityId)
    {
        if (!_authority.Catalog.PressureIds.Contains(pressureId))
            throw new ArgumentException($"Unknown Research Pressure '{pressureId}'.", nameof(pressureId));
        SetPriority(GetState(state), AgendaTarget.Problem, pressureId, priorityId);
    }

    public void SetCapabilityPriority(AdaptiveResearchCivilizationState state, string capabilityId, string priorityId)
    {
        if (!_authority.Catalog.Capabilities.ContainsKey(capabilityId))
            throw new ArgumentException($"Unknown cross-lineage capability '{capabilityId}'.", nameof(capabilityId));
        SetPriority(GetState(state), AgendaTarget.Capability, capabilityId, priorityId);
    }

    public void SetScientificCultureAxis(AdaptiveResearchCivilizationState state, string axisId, double value)
    {
        if (!_catalog.CultureAxes.ContainsKey(axisId))
            throw new ArgumentException($"Unknown scientific-culture axis '{axisId}'.", nameof(axisId));
        GetState(state).SetCultureAxis(axisId, value);
        Invalidate(state);
    }

    public void SetOrientations(
        AdaptiveResearchCivilizationState state,
        ResearchAgendaOrientationState orientations)
    {
        GetState(state).SetOrientations(orientations);
        Invalidate(state);
    }

    public ResearchAgendaReviewRecommendation EvaluatePerceivedAdequacy(
        AdaptiveResearchCivilizationState state,
        ResearchPerceivedAdequacyAssessment assessment)
    {
        Validate100(assessment.Adequacy, nameof(assessment.Adequacy));
        Validate100(assessment.CredibleChallenge, nameof(assessment.CredibleChallenge));
        if (assessment.Confidence < 0 || assessment.Confidence > 1 || double.IsNaN(assessment.Confidence))
            throw new ArgumentOutOfRangeException(nameof(assessment.Confidence));
        if (!_authority.Catalog.Nodes.Values.Any(node => string.Equals(node.DomainId, assessment.DomainId, StringComparison.Ordinal)))
            throw new ArgumentException($"Unknown research domain '{assessment.DomainId}'.", nameof(assessment));

        var agenda = GetState(state);
        var policy = _catalog.RuntimePolicy;
        var relevantPressure = VisibleDomainPressure(state, assessment.DomainId);
        var threatSensitivity = agenda.GetCultureAxis("threat_sensitivity");
        var complacency = agenda.GetCultureAxis("complacency_tendency");
        var conservatism = agenda.GetCultureAxis("institutional_conservatism");
        var complacencyCulture = (complacency + conservatism + (100.0 - threatSensitivity)) / 3.0;
        var complacencyIndex = assessment.Adequacy * assessment.Confidence * complacencyCulture / 100.0;
        var challengeIndex = assessment.CredibleChallenge * assessment.Confidence * threatSensitivity / 100.0;

        var currentPriority = agenda.GetDomainPriority(assessment.DomainId, policy.DefaultPriorityId);
        var recommended = currentPriority;
        string explanation;

        if (assessment.Confidence < policy.MinimumAdequacyConfidence)
        {
            explanation = "No agenda change recommended because the adequacy/threat assessment confidence is too low.";
        }
        else if (challengeIndex >= policy.CriticalChallengeThreshold)
        {
            recommended = "critical";
            explanation = "Critical attention recommended because a legitimately observed challenge is severe and this civilization is highly threat-sensitive.";
        }
        else if (challengeIndex >= policy.StrategicChallengeThreshold)
        {
            recommended = "strategic";
            explanation = "Strategic attention recommended because credible observed competition/threat is strong.";
        }
        else if (challengeIndex >= policy.ImportantChallengeThreshold)
        {
            recommended = "important";
            explanation = "Important attention recommended because credible observed competition/threat is rising.";
        }
        else if (assessment.Adequacy >= policy.HighAdequacyMinimum &&
                 relevantPressure <= policy.LowRelevantPressureMaximum &&
                 complacencyIndex >= policy.ComplacencyDeprioritizeThreshold)
        {
            recommended = "deprioritized";
            explanation = "Deprioritization is plausible because current capability is perceived as adequate, recognized need is low, and complacency/conservatism outweigh threat sensitivity.";
        }
        else
        {
            explanation = "Current agenda priority remains reasonable under the legitimate adequacy, pressure, and challenge information available.";
        }

        return new ResearchAgendaReviewRecommendation(
            assessment.DomainId,
            recommended,
            relevantPressure,
            complacencyIndex,
            challengeIndex,
            explanation);
    }

    public void ApplyRecommendation(
        AdaptiveResearchCivilizationState state,
        ResearchAgendaReviewRecommendation recommendation,
        double currentYear,
        string provenance)
    {
        SetDomainPriority(state, recommendation.DomainId, recommendation.RecommendedPriorityId);
        GetState(state).MarkReviewed(currentYear, provenance);
        Invalidate(state);
    }

    public IReadOnlyList<ResearchVisibleProjectCandidate> BuildVisibleShortlist(AdaptiveResearchCivilizationState state)
    {
        var agenda = GetState(state);
        var cache = _caches.GetValue(state, _ => new CacheHolder());
        if (cache.Value is { } existing &&
            existing.CoreRevision == state.MaterializedViewRevision &&
            existing.ExpertiseRevision == state.Expertise.Revision &&
            existing.AgendaRevision == agenda.Revision)
            return existing.Candidates;

        var candidates = state.NodeStates.Values
            .Where(nodeState => nodeState.Maturity == ResearchMaturity.Investigable)
            .Where(nodeState => !state.ActiveProjects.ContainsKey(nodeState.NodeId))
            .Select(nodeState => ScoreVisibleCandidate(state, agenda, nodeState.NodeId))
            .OrderByDescending(candidate => candidate.UtilityScore)
            .ThenBy(candidate => candidate.NodeId, StringComparer.Ordinal)
            .Take(_catalog.ShortlistBound)
            .ToArray();

        cache.Value = new CandidateCache(
            state.MaterializedViewRevision,
            state.Expertise.Revision,
            agenda.Revision,
            candidates);
        return candidates;
    }

    private ResearchVisibleProjectCandidate ScoreVisibleCandidate(
        AdaptiveResearchCivilizationState state,
        AdaptiveResearchAgendaState agenda,
        string nodeId)
    {
        var node = _authority.Catalog.GetNode(nodeId);
        var requestedLabs = ChoosePlanningLabAllocation(state, node);
        var readiness = _authority.GetProjectReadiness(
            state,
            nodeId,
            ResearchMaturity.Experimental,
            requestedLabs);
        var eligibility = _authority.Kernel.Eligibility.EvaluateProjectStart(state, nodeId, requestedLabs);
        var components = new List<ResearchProjectUtilityComponent>();

        var pressureIds = node.PressureAffinities
            .Concat(node.ProjectRequirements.RequiredPressure.Keys)
            .Concat(node.ProjectRequirements.RequiredPressureAny.Keys)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        if (pressureIds.Length > 0)
        {
            var need = pressureIds.Max(pressureId =>
                (state.GetPressure(pressureId) + PriorityScore(agenda.GetProblemPriority(pressureId, _catalog.RuntimePolicy.DefaultPriorityId))) / 2.0);
            components.Add(new ResearchProjectUtilityComponent("recognized_need", need, $"Recognized need {need:0.#}/100 from known pressure and problem priority."));
        }

        var alignmentCandidates = new List<double>
        {
            PriorityScore(agenda.GetDomainPriority(node.DomainId, _catalog.RuntimePolicy.DefaultPriorityId)),
        };
        alignmentCandidates.AddRange(node.KnowledgeFields.Select(fieldId =>
            PriorityScore(agenda.GetFieldPriority(fieldId, _catalog.RuntimePolicy.DefaultPriorityId))));
        foreach (var capabilityId in node.DeclaredCapabilities.Where(_authority.Catalog.Capabilities.ContainsKey))
            alignmentCandidates.Add(PriorityScore(agenda.GetCapabilityPriority(capabilityId, _catalog.RuntimePolicy.DefaultPriorityId)));
        var alignment = alignmentCandidates.Max();
        components.Add(new ResearchProjectUtilityComponent("strategic_alignment", alignment, $"Strategic agenda alignment {alignment:0.#}/100."));

        components.Add(new ResearchProjectUtilityComponent("readiness", readiness.OverallReadinessScore, $"Current Project Readiness {readiness.OverallReadinessScore:0.#}/100."));

        var capabilityOutputs = node.DeclaredCapabilities
            .Where(_authority.Catalog.Capabilities.ContainsKey)
            .Select(id => _authority.Catalog.Capabilities[id])
            .Where(definition => definition.Scope == ResearchCapabilityScope.Civilization)
            .ToArray();
        if (capabilityOutputs.Length > 0)
        {
            var missing = capabilityOutputs.Count(definition => !state.HasCapability(definition.Id));
            var gapValue = 100.0 * missing / capabilityOutputs.Length;
            components.Add(new ResearchProjectUtilityComponent("capability_gap_value", gapValue, $"Known civilization capability-gap value {gapValue:0.#}/100."));
        }

        var scaledLabs = _authority.Catalog.LabScaling.ScaleAssignedLabs(requestedLabs, node.ProjectRequirements.RecommendedLabs);
        var rpPerYear = scaledLabs * _authority.Catalog.Metadata.BaseRpPerEffectiveLabPerYear * readiness.RpEfficiency;
        var estimatedYears = rpPerYear <= 0 ? double.PositiveInfinity : node.ProjectRequirements.BaseResearchPoints / rpPerYear;
        var halfYears = _catalog.RuntimePolicy.TimeToEffectHalfValueYears;
        var timeScore = double.IsInfinity(estimatedYears) ? 0.0 : 100.0 / (1.0 + (estimatedYears / halfYears));
        components.Add(new ResearchProjectUtilityComponent("time_to_effect", timeScore, $"Estimated research horizon {FormatYears(estimatedYears)}; time value {timeScore:0.#}/100."));

        var capacity = state.TotalEffectiveResearchLabs;
        var halfFraction = _catalog.RuntimePolicy.LabCostHalfValueFraction;
        var labScore = capacity <= 0 ? 0.0 : 100.0 / (1.0 + (requestedLabs / Math.Max(0.000001, capacity * halfFraction)));
        components.Add(new ResearchProjectUtilityComponent("lab_opportunity_cost", labScore, $"Lab opportunity-cost desirability {labScore:0.#}/100."));

        var matureSameFamily = state.NodeStates.Values.Any(existing =>
            existing.CountsAsEstablishedKnowledge &&
            !string.Equals(existing.NodeId, node.Id, StringComparison.Ordinal) &&
            string.Equals(_authority.Catalog.GetNode(existing.NodeId).SolutionFamily, node.SolutionFamily, StringComparison.Ordinal));
        var alternativeValue = matureSameFamily
            ? _catalog.RuntimePolicy.AlreadyCoveredSolutionValue
            : _catalog.RuntimePolicy.NovelSolutionValue;
        components.Add(new ResearchProjectUtilityComponent("alternative_coverage", alternativeValue,
            matureSameFamily ? "A mature solution already covers this solution family." : "This would preserve/open a distinct solution family."));

        var diversityPreference = (agenda.Orientations.PortfolioDiversityPolicy + agenda.GetCultureAxis("portfolio_diversity")) / 2.0;
        if (diversityPreference > 50.0)
        {
            var diversityScore = matureSameFamily ? 100.0 - diversityPreference : diversityPreference;
            components.Add(new ResearchProjectUtilityComponent("portfolio_diversity", diversityScore, $"Portfolio-diversity value {diversityScore:0.#}/100."));
        }

        if (node.IsHypothesis)
        {
            var riskFit = agenda.GetCultureAxis("risk_tolerance");
            components.Add(new ResearchProjectUtilityComponent("uncertainty_risk", riskFit, $"Hypothesis risk fit {riskFit:0.#}/100."));
        }

        var immediateNeed = pressureIds.Length == 0 ? 0.0 : pressureIds.Max(state.GetPressure);
        if (immediateNeed < 50.0)
        {
            var longValue = (agenda.GetCultureAxis("curiosity") + agenda.GetCultureAxis("long_term_orientation") + (100.0 - agenda.Orientations.BasicVsAppliedOrientation)) / 3.0;
            components.Add(new ResearchProjectUtilityComponent("long_horizon_value", longValue, $"Long-horizon/basic-science value {longValue:0.#}/100."));
        }

        if (node.KnowledgeFields.Count > 0)
        {
            var spillover = node.KnowledgeFields
                .Select(fieldId => 100.0 - state.Expertise.GetField(fieldId).Current.Theoretical)
                .Average();
            components.Add(new ResearchProjectUtilityComponent("knowledge_spillover", spillover, $"Potential active-competence learning value {spillover:0.#}/100."));
        }

        var utility = components.Count == 0 ? 0.0 : components.Average(component => component.Score);
        if (!eligibility.Allowed)
            utility *= _catalog.RuntimePolicy.BlockedProjectScoreMultiplier;
        utility = Math.Clamp(utility, 0.0, 100.0);

        var strongest = components.OrderByDescending(component => component.Score).Take(3).Select(component => component.Id).ToArray();
        var explanation = eligibility.Allowed
            ? $"Visible candidate score {utility:0.#}/100; strongest factors: {string.Join(", ", strongest)}."
            : $"Visible strategic candidate score {utility:0.#}/100 after blocker reduction; requires real capacity/requirements before start.";

        return new ResearchVisibleProjectCandidate(
            nodeId,
            utility,
            eligibility.Allowed,
            requestedLabs,
            estimatedYears,
            components,
            eligibility.Blockers,
            explanation);
    }

    private double VisibleDomainPressure(AdaptiveResearchCivilizationState state, string domainId)
    {
        var pressureIds = state.NodeStates.Values
            .Where(nodeState => string.Equals(_authority.Catalog.GetNode(nodeState.NodeId).DomainId, domainId, StringComparison.Ordinal))
            .SelectMany(nodeState =>
            {
                var node = _authority.Catalog.GetNode(nodeState.NodeId);
                return node.PressureAffinities
                    .Concat(node.ProjectRequirements.RequiredPressure.Keys)
                    .Concat(node.ProjectRequirements.RequiredPressureAny.Keys);
            })
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        return pressureIds.Length == 0 ? 0.0 : pressureIds.Max(state.GetPressure);
    }

    private double ChoosePlanningLabAllocation(AdaptiveResearchCivilizationState state, AdaptiveResearchNodeDefinition node)
    {
        var minimum = node.ProjectRequirements.MinimumLabs;
        var recommended = node.ProjectRequirements.RecommendedLabs;
        if (state.FreeEffectiveLabs <= 0)
            return minimum;
        return Math.Max(minimum, Math.Min(recommended, state.FreeEffectiveLabs));
    }

    private double PriorityScore(string priorityId) => _catalog.GetPriority(priorityId).Score;

    private void SetPriority(AdaptiveResearchAgendaState state, AgendaTarget target, string key, string priorityId)
    {
        _catalog.GetPriority(priorityId);
        var defaultId = _catalog.RuntimePolicy.DefaultPriorityId;
        _ = target switch
        {
            AgendaTarget.Domain => state.SetDomainPriority(key, priorityId, defaultId),
            AgendaTarget.Field => state.SetFieldPriority(key, priorityId, defaultId),
            AgendaTarget.Problem => state.SetProblemPriority(key, priorityId, defaultId),
            AgendaTarget.Capability => state.SetCapabilityPriority(key, priorityId, defaultId),
            _ => false,
        };
        InvalidateForAgendaState(state);
    }

    private void InvalidateForAgendaState(AdaptiveResearchAgendaState agendaState)
    {
        foreach (var pair in _states)
        {
            if (ReferenceEquals(pair.Value, agendaState))
            {
                Invalidate(pair.Key);
                break;
            }
        }
    }

    private void Invalidate(AdaptiveResearchCivilizationState state)
    {
        if (_caches.TryGetValue(state, out var holder))
            holder.Value = null;
    }

    private static string FormatYears(double years) => double.IsInfinity(years) ? "unbounded under current conditions" : $"{years:0.#}y";

    private static void Validate100(double value, string name)
    {
        if (value < 0 || value > 100 || double.IsNaN(value) || double.IsInfinity(value))
            throw new ArgumentOutOfRangeException(name, value, "Value must be in 0..100.");
    }

    private enum AgendaTarget
    {
        Domain,
        Field,
        Problem,
        Capability,
    }
}
