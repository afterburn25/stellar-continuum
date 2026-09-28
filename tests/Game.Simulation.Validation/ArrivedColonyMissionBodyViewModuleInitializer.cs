using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class ArrivedColonyMissionBodyViewModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ArrivedColonyMissionBodyViewValidation.ValidateArrivedAndIdleColonyBodyTargetVisibility();
        Console.WriteLine("PASS: arrived colony mission body target read model");
    }
}
