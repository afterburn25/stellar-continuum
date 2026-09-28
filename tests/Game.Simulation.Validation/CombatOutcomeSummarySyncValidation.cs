using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class CombatOutcomeSummarySyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCombatOutcomeSummaryCheck()
    {
        CombatOutcomeSummaryValidation.ValidateCompactDeterministicCombatOutcomeSummary();
        Console.WriteLine("PASS: compact deterministic Combat outcome summary");
    }
}
