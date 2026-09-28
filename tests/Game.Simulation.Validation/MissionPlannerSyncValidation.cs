using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

/// <summary>
/// Runs the save-neutral exploration mission-planning regressions imported after the Species
/// v8 synchronization point without replacing the Species-owned v8 Program.cs registry.
/// </summary>
internal static class MissionPlannerSyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunMissionPlannerSyncChecks()
    {
        ExplorationMissionPlanningValidation.ValidateBoundedObserverSafeMissionPlan();
        ExplorationMissionPlanningValidation.ValidateSharedReachRejectionAndLocalOrders();
        ExplorationMissionPlanningValidation.ValidateAiUsesSharedMissionPlan();
        Console.WriteLine("PASS: synchronized exploration mission-planning regressions");
    }
}
