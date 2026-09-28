using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class ExplorationMissionStatusSyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunExplorationMissionStatusChecks()
    {
        ExplorationMissionStatusValidation.ValidateTransitEtaAndSurveyInformationBoundary();
        ExplorationMissionStatusValidation.ValidateLocalScoutAndSciencePhases();
        ExplorationMissionStatusValidation.ValidateColonySettlementReadiness();
        Console.WriteLine("PASS: synchronized Exploration mission status/ETA regressions");
    }
}
