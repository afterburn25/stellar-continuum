using System;
using System.Runtime.CompilerServices;

namespace Game.Quality.Validation;

internal static class SurveyPersistenceModuleInitializer
{
    [Game.Validation.RegressionCheck]
    internal static void RunSurveyPersistenceChecks()
    {
        SurveyPersistenceValidation.Run();
        Console.WriteLine("PASS: survey/discovery persistence round trip");
    }
}
