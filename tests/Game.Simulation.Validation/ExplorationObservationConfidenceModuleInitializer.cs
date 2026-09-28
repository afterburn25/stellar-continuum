using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class ExplorationObservationConfidenceModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ExplorationObservationConfidenceValidation.ValidateConfidenceTracksObserverKnowledgeWithoutLeaks();
        Console.WriteLine("PASS: observer-local exploration observation confidence");
    }
}
