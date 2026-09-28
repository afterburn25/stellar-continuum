using System.Runtime.CompilerServices;
using Game.Simulation.Colonization;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Industry;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Validation;

internal static class CombatOutcomeStepSyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCombatOutcomeStepCheck()
    {
        var combatEvents = new CombatEvent[]
        {
            new(
                CombatEventType.EngagementStarted,
                17,
                1,
                101,
                2,
                201,
                0.0,
                0.0,
                0.0,
                "step engagement"),
            new(
                CombatEventType.DamageApplied,
                17,
                1,
                101,
                2,
                201,
                4.0,
                5.0,
                6.0,
                "step damage"),
            new(
                CombatEventType.FleetDestroyed,
                17,
                1,
                101,
                2,
                201,
                0.0,
                0.0,
                0.0,
                "step destruction",
                250.0),
        };

        var result = new SimulationStepResult(
            0.5,
            Array.Empty<CivilizationIndustryAllocation>(),
            Array.Empty<ConstructionEvent>(),
            Array.Empty<ShipbuildingEvent>(),
            Array.Empty<ResearchEvent>(),
            Array.Empty<ExplorationEvent>(),
            combatEvents,
            Array.Empty<ColonizationEvent>());

        var outcome = result.CombatOutcome;
        Require(ReferenceEquals(result.CombatEvents, combatEvents),
            "CombatOutcome exposure replaced or copied the authoritative raw Combat event contract");
        Require(outcome.EventCount == 3 && outcome.EngagementsStarted == 1 && outcome.DamageEvents == 1 && outcome.VesselsDestroyed == 1,
            "SimulationStepResult CombatOutcome did not reflect its Combat event stream");
        RequireNear(outcome.TotalDamageApplied, 15.0,
            "SimulationStepResult CombatOutcome changed damage aggregation");
        RequireNear(outcome.EmbarkedPopulationCasualtiesMillions, 250.0,
            "SimulationStepResult CombatOutcome changed population-casualty aggregation");
        Require(outcome.Civilizations.Count == 2 && outcome.Systems.Count == 1 && outcome.Systems[0].SystemId == 17,
            "SimulationStepResult CombatOutcome lost compact participant/system aggregation");

        var direct = CombatOutcomeSummaryBuilder.Build(combatEvents);
        Require(outcome == direct || Equivalent(outcome, direct),
            "SimulationStepResult CombatOutcome diverged from the canonical summary builder");

        var empty = SimulationStepResult.Empty.CombatOutcome;
        Require(empty.EventCount == 0 && empty.Civilizations.Count == 0 && empty.Systems.Count == 0,
            "SimulationStepResult.Empty produced a non-empty Combat outcome");

        Console.WriteLine("PASS: SimulationStepResult compact Combat outcome exposure");
    }

    private static bool Equivalent(CombatOutcomeSummary first, CombatOutcomeSummary second) =>
        first.EventCount == second.EventCount &&
        first.EngagementsStarted == second.EngagementsStarted &&
        first.EngagementsEnded == second.EngagementsEnded &&
        first.DamageEvents == second.DamageEvents &&
        first.VesselsDestroyed == second.VesselsDestroyed &&
        first.RetreatsInitiated == second.RetreatsInitiated &&
        first.SuccessfulEscapes == second.SuccessfulEscapes &&
        Math.Abs(first.TotalDamageApplied - second.TotalDamageApplied) < 0.000001 &&
        Math.Abs(first.EmbarkedPopulationCasualtiesMillions - second.EmbarkedPopulationCasualtiesMillions) < 0.000001 &&
        first.Civilizations.SequenceEqual(second.Civilizations) &&
        first.Systems.Count == second.Systems.Count;

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
