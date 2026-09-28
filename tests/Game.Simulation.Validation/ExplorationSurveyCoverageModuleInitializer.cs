using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class ExplorationSurveyCoverageModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ExplorationSurveyCoverageValidation.ValidateObserverSafeCoverageTransitions();
        ExplorationSurveyCoverageValidation.ValidateBoundedWorkOrderingAndOwnedFleetCounts();
        Console.WriteLine("PASS: observer-safe exploration survey coverage and bounded workload");
    }
}
