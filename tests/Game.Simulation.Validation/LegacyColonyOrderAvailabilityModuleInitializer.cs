using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class LegacyColonyOrderAvailabilityModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        LegacyColonyOrderAvailabilityValidation.ValidateLegacyOrderSkipsCommittedFleet();
        LegacyColonyOrderAvailabilityValidation.ValidateLegacyOrderRejectsWhenOnlyCommittedFleetExists();
        LegacyColonyOrderAvailabilityValidation.ValidateSettlementReadyAndLegacyArrivalsAreNotStolen();
        LegacyColonyOrderAvailabilityValidation.ValidateExplicitFleetOrderRemainsRetargetable();
        Console.WriteLine("PASS: legacy colony order uses only uncommitted fleet availability");
    }
}
