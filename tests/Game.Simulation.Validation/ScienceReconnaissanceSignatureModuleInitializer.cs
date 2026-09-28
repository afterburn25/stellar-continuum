using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class ScienceReconnaissanceSignatureModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ScienceReconnaissanceSignatureValidation.ValidateScienceTransitionEmitsPositiveSignaturesOnce();
        ScienceReconnaissanceSignatureValidation.ValidateDirectCompletionSkipsTransientSignatures();
        Console.WriteLine("PASS: science reconnaissance signature event consistency");
    }
}
