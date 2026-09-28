using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class FriendlyColonyReservationPlannerModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        FriendlyColonyReservationPlannerValidation.ValidateFriendlyReservationBlocksSecondFleetPlanAndOrder();
        FriendlyColonyReservationPlannerValidation.ValidateReservationExcludesRequestingFleetAndForeignIntent();
        FriendlyColonyReservationPlannerValidation.ValidateLocalFriendlyColonyFleetReservesCurrentSystem();
        Console.WriteLine("PASS: shared friendly colony reservation planning and orders");
    }
}
