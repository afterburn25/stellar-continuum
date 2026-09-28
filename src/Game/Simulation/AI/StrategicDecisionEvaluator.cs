using System;

namespace Game.Simulation.AI;

public sealed class StrategicDecisionEvaluator
{
    public WarAssessment EvaluateWar(
        CivilizationTraits traits,
        double ownKnownMilitaryStrength,
        KnownCivilization target,
        long nowTick,
        bool honorCompelsBattle = false)
    {
        if (!target.HasMilitaryEstimate)
        {
            // Knowing a civilization diplomatically is not military intelligence. Fail closed:
            // no offensive recommendation may be produced from an absent estimate.
            return new WarAssessment(
                Score: -1.0,
                PerceivedStrengthRatio: 1.0,
                IntelligenceConfidence: 0.0,
                SurvivalGateTriggered: true,
                RecommendWar: false);
        }

        var freshness = target.Freshness(nowTick, staleAfterTicks: 365);
        var confidence = Math.Clamp(target.EstimateConfidence * freshness, 0.05, 1.0);

        // Uncertain AI becomes more conservative: it cannot peek behind fog of war to resolve doubt.
        var threatEstimate = target.EstimatedMilitaryMidpoint * (1.0 + (1.0 - confidence) * 0.35);
        var ratio = ownKnownMilitaryStrength / Math.Max(1.0, threatEstimate);

        var survivalFloor = 0.70 + traits.SurvivalPriority * 0.20 - traits.RiskTolerance * 0.15;
        var survivalRejectsWar = ratio < survivalFloor && !(traits.HonorBound && honorCompelsBattle);

        var borderConcern = target.HasSharedBorder ? traits.Territoriality * 0.35 : 0.0;
        var aggressionPressure = traits.Aggression * 0.35;
        var opportunity = Math.Clamp((ratio - 0.75) / 1.5, -0.5, 0.6);
        var trustRestraint = Math.Clamp(target.Trust, -1.0, 1.0) * 0.30;
        var tradeRestraint = Math.Clamp(target.KnownTradeDependence, 0.0, 1.0) * 0.25;
        var exhaustionRestraint = Math.Clamp(target.KnownWarExhaustion, 0.0, 1.0) * 0.35;
        var treatyRestraint = target.HasDefenseTreatyWithObserver ? 1.0 : 0.0;

        var score = aggressionPressure + borderConcern + opportunity
                    - trustRestraint - tradeRestraint - exhaustionRestraint - treatyRestraint;

        if (survivalRejectsWar)
            score = Math.Min(score, -0.65);

        if (traits.HonorBound && honorCompelsBattle)
            score = Math.Max(score, 0.55);

        return new WarAssessment(
            Score: score,
            PerceivedStrengthRatio: ratio,
            IntelligenceConfidence: confidence,
            SurvivalGateTriggered: survivalRejectsWar,
            RecommendWar: score >= 0.50);
    }
}

public sealed record WarAssessment(
    double Score,
    double PerceivedStrengthRatio,
    double IntelligenceConfidence,
    bool SurvivalGateTriggered,
    bool RecommendWar
);
