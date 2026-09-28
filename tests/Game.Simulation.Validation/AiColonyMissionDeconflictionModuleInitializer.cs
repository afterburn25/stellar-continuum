using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class AiColonyMissionDeconflictionModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        AiColonyMissionDeconflictionValidation.ValidateFriendlyColonyShipsSplitAcrossViableSystems();
        AiColonyMissionDeconflictionValidation.ValidateOnlyRemainingFriendlyTargetIsNotDuplicated();
        AiColonyMissionDeconflictionValidation.ValidateForeignMissionDoesNotReserveHiddenIntent();
        AiColonyMissionDeconflictionValidation.ValidateInterruptedSettlementRetarget();
        Console.WriteLine("PASS: same-civilization AI colony mission deconfliction");
    }
}
