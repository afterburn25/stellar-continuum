using System;
using System.Runtime.CompilerServices;

namespace Game.Quality.Validation;

internal static class SpatialPresentationModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void RunSpatialPresentationChecks()
    {
        SpatialPresentationValidation.Run();
        Console.WriteLine("PASS: spatial projection, observer cache, refresh bounds and celestial navigation geometry");
    }
}
