using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class CombatRepairApplicationSyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCombatRepairApplicationCheck()
    {
        CombatRepairApplicationValidation.ValidateExternallyBudgetedCombatRepairApplication();
        Console.WriteLine("PASS: synchronized Combat repair-application regression");
    }
}
