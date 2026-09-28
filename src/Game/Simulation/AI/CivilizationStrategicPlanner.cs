using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.AI;

public enum StrategicPriorityType
{
    StabilizeSupply,
    ExpandIndustry,
    ExpandResearch,
    Explore,
    Colonize,
    BuildFleet,
    Defend,
    ImproveRelations,
}

/// <summary>
/// Legitimate self-knowledge supplied to the strategic planner by the owning civilization's
/// authoritative systems. This record must never contain hidden enemy state.
/// </summary>
public sealed record CivilizationOwnState(
    double MilitaryStrength,
    double SupplyCoverageRatio,
    double IndustryReserve,
    double ResearchCapacity,
    bool HasAvailableResearch,
    bool HasUnexploredReachableSystems,
    bool HasKnownColonizationOpportunity,
    bool CanBuildInterstellarShips,
    bool HasFleetCapacityShortfall
);

public sealed record StrategicPriority(
    StrategicPriorityType Type,
    double Score,
    string Reason
);

public sealed record CivilizationStrategicPlan(
    int CivilizationId,
    long GeneratedAtTick,
    long ReviewAfterTick,
    IReadOnlyList<StrategicPriority> Priorities
)
{
    public StrategicPriority? PrimaryPriority => Priorities.Count == 0 ? null : Priorities[0];
}

/// <summary>
/// Scheduled strategic planner for early-release Civilization AI. It consumes only own state
/// plus KnowledgeSnapshot observations, caches plans until their review tick, and never asks
/// the authoritative galaxy for hidden rival information.
/// </summary>
public sealed class CivilizationStrategicPlanner
{
    private readonly StrategicDecisionEvaluator _decisionEvaluator;
    private readonly Dictionary<int, CivilizationStrategicPlan> _cachedPlans = new();
    private readonly long _reviewIntervalTicks;

    public CivilizationStrategicPlanner(
        StrategicDecisionEvaluator? decisionEvaluator = null,
        long reviewIntervalTicks = 30)
    {
        _decisionEvaluator = decisionEvaluator ?? new StrategicDecisionEvaluator();
        _reviewIntervalTicks = Math.Max(1, reviewIntervalTicks);
    }

    public CivilizationStrategicPlan GetPlan(
        int civilizationId,
        CivilizationTraits traits,
        CivilizationOwnState ownState,
        KnowledgeSnapshot knowledge,
        long nowTick,
        bool forceReview = false)
    {
        ArgumentNullException.ThrowIfNull(traits);
        ArgumentNullException.ThrowIfNull(ownState);
        ArgumentNullException.ThrowIfNull(knowledge);

        if (!forceReview
            && _cachedPlans.TryGetValue(civilizationId, out var cached)
            && nowTick < cached.ReviewAfterTick)
        {
            return cached;
        }

        var priorities = BuildPriorities(traits, ownState, knowledge, nowTick);
        var plan = new CivilizationStrategicPlan(
            civilizationId,
            nowTick,
            checked(nowTick + _reviewIntervalTicks),
            priorities);

        _cachedPlans[civilizationId] = plan;
        return plan;
    }

    public void Invalidate(int civilizationId) => _cachedPlans.Remove(civilizationId);

    public void RemoveCivilization(int civilizationId) => _cachedPlans.Remove(civilizationId);

    public void Clear() => _cachedPlans.Clear();

    private IReadOnlyList<StrategicPriority> BuildPriorities(
        CivilizationTraits traits,
        CivilizationOwnState ownState,
        KnowledgeSnapshot knowledge,
        long nowTick)
    {
        var priorities = new List<StrategicPriority>(8);
        var supplyCoverage = Math.Clamp(ownState.SupplyCoverageRatio, 0.0, 2.0);

        if (supplyCoverage < 1.0)
        {
            var severity = Math.Clamp(1.0 - supplyCoverage, 0.0, 1.0);
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.StabilizeSupply,
                0.60 + severity * 1.10 + traits.SurvivalPriority * 0.25,
                $"Own supply coverage is {supplyCoverage:P0}; survival and expansion depend on restoring logistics."));
        }

        var industryScarcity = 1.0 / (1.0 + Math.Max(0.0, ownState.IndustryReserve) / 250.0);
        priorities.Add(new StrategicPriority(
            StrategicPriorityType.ExpandIndustry,
            0.30 + industryScarcity * 0.55 + traits.Greed * 0.15,
            "Own industrial reserve constrains construction, shipbuilding and recovery capacity."));

        if (ownState.HasAvailableResearch)
        {
            var researchNeed = 1.0 / (1.0 + Math.Max(0.0, ownState.ResearchCapacity) / 10.0);
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.ExpandResearch,
                0.35 + researchNeed * 0.35 + traits.ScientificCuriosity * 0.50,
                "Known research opportunities exist and can be pursued with the civilization's own scientific capacity."));
        }

        if (ownState.HasUnexploredReachableSystems)
        {
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.Explore,
                0.30 + traits.ScientificCuriosity * 0.40 + traits.RiskTolerance * 0.15,
                "Legitimately known reachable space still contains unexplored targets."));
        }

        if (ownState.HasKnownColonizationOpportunity)
        {
            var logisticsRestraint = supplyCoverage < 0.95 ? (0.95 - supplyCoverage) * 0.90 : 0.0;
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.Colonize,
                0.45 + traits.Greed * 0.25 + traits.RiskTolerance * 0.10 - logisticsRestraint,
                "A known colonization opportunity exists; logistics health moderates expansion appetite."));
        }

        StrategicPriority? defensePriority = null;
        var strongestKnownThreat = EvaluateStrongestKnownThreat(traits, ownState, knowledge, nowTick);
        if (strongestKnownThreat is { } threat)
        {
            var defenseScore = 0.40
                               + Math.Clamp(1.10 - threat.Assessment.PerceivedStrengthRatio, 0.0, 1.0) * 0.85
                               + (1.0 - threat.Assessment.IntelligenceConfidence) * 0.20
                               + traits.SurvivalPriority * 0.25;

            defensePriority = new StrategicPriority(
                StrategicPriorityType.Defend,
                defenseScore,
                $"Known civilization {threat.CivilizationId} is a credible threat under current observed/estimated intelligence.");
        }

        // A persisted observer-visible war is itself sufficient reason to defend even when no
        // lawful military-strength estimate exists. Do not manufacture a strength range merely
        // to quantify that threat; use the known political fact and own survival priorities.
        var unknownStrengthWar = knowledge.Civilizations.Values
            .Where(known => known.KnownToBeAtWar && !known.HasMilitaryEstimate)
            .OrderBy(known => known.CivilizationId)
            .FirstOrDefault();
        if (unknownStrengthWar is not null)
        {
            var warDefenseScore = 0.95 + traits.SurvivalPriority * 0.30;
            if (defensePriority is null || warDefenseScore > defensePriority.Score)
            {
                defensePriority = new StrategicPriority(
                    StrategicPriorityType.Defend,
                    warDefenseScore,
                    $"Civilization {unknownStrengthWar.CivilizationId} is known to be at war with us; enemy strength remains legitimately unknown.");
            }
        }

        if (defensePriority is not null)
            priorities.Add(defensePriority);

        if (ownState.CanBuildInterstellarShips && ownState.HasFleetCapacityShortfall)
        {
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.BuildFleet,
                0.48 + traits.Aggression * 0.25 + traits.SurvivalPriority * 0.20,
                "Own fleet capacity is below current strategic needs and ship construction is known to be available."));
        }

        var relationshipOpportunity = knowledge.Civilizations.Values
            .Where(known => known.Trust > -0.25 && !known.KnownToBeAtWar)
            .OrderByDescending(known => known.KnownTradeDependence + known.Trust)
            .FirstOrDefault();

        if (relationshipOpportunity is not null)
        {
            priorities.Add(new StrategicPriority(
                StrategicPriorityType.ImproveRelations,
                0.22 + Math.Clamp(relationshipOpportunity.KnownTradeDependence, 0.0, 1.0) * 0.20
                     + Math.Clamp(relationshipOpportunity.Trust, -1.0, 1.0) * 0.10,
                $"Known relations with civilization {relationshipOpportunity.CivilizationId} provide a plausible diplomatic opportunity."));
        }

        priorities.Sort(static (left, right) =>
        {
            var scoreOrder = right.Score.CompareTo(left.Score);
            return scoreOrder != 0 ? scoreOrder : left.Type.CompareTo(right.Type);
        });

        return priorities;
    }

    private KnownThreat? EvaluateStrongestKnownThreat(
        CivilizationTraits traits,
        CivilizationOwnState ownState,
        KnowledgeSnapshot knowledge,
        long nowTick)
    {
        KnownThreat? strongest = null;
        foreach (var known in knowledge.Civilizations.Values)
        {
            if (!known.HasMilitaryEstimate)
                continue;

            var assessment = _decisionEvaluator.EvaluateWar(
                traits,
                ownState.MilitaryStrength,
                known,
                nowTick);

            // We use the inverse perceived strength ratio as a defensive threat signal.
            // Nothing here resolves uncertainty by reading authoritative rival state.
            var threatScore = 1.0 / Math.Max(0.05, assessment.PerceivedStrengthRatio);
            threatScore *= 0.75 + (1.0 - Math.Clamp(known.Trust, -1.0, 1.0)) * 0.25;
            if (known.KnownToBeAtWar)
                threatScore *= 0.90;

            if (strongest is null || threatScore > strongest.ThreatScore)
                strongest = new KnownThreat(known.CivilizationId, threatScore, assessment);
        }

        return strongest;
    }

    private sealed record KnownThreat(int CivilizationId, double ThreatScore, WarAssessment Assessment);
}
