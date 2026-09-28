using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class BodylessColonySettlementResolverModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        BodylessColonySettlementResolverValidation.ValidateReadStatusAndFoundingUseSameSpeciesRelativeBody();
        Console.WriteLine("PASS: shared bodyless colony settlement resolver");
    }
}
