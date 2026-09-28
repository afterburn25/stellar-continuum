using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class LocalColonyReservationViabilityModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        LocalColonyReservationViabilityValidation.ValidateBodylessStrandedFleetDoesNotDeadlockViableFriendlySpecies();
        LocalColonyReservationViabilityValidation.ValidateInvalidRetainedBodyDoesNotReserveAnotherViableBody();
        Console.WriteLine("PASS: local colony reservation requires actual settlement viability");
    }
}
