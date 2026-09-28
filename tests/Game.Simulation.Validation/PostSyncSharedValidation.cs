using System.Runtime.CompilerServices;

namespace Game.Simulation.Validation;

internal static class PostSyncSharedValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        CombatCasualtyValidation.ValidateEmbarkedPopulationCasualties();
        Console.WriteLine("PASS: embarked population casualties on fleet destruction");

        CombatBatchOrderValidation.ValidateMixedSelectionBatchOrders();
        Console.WriteLine("PASS: deterministic batch military command behavior");

        CombatSystemPresenceValidation.ValidateAuthoritativeSystemMilitaryPresence();
        Console.WriteLine("PASS: authoritative system military presence and interdiction");

        SurveyOperationsValidation.ValidateDeterministicBoundedSurveyEffortAndTickInvariance();
        Console.WriteLine("PASS: deterministic bounded survey effort and tick invariance");

        SurveyOperationsValidation.ValidateReconnaissanceSignalsRemainPositiveOnly();
        Console.WriteLine("PASS: reconnaissance signals remain positive only");

        SurveyOperationsValidation.ValidatePositiveSignaturesAndConfirmedBodyDiscoveries();
        Console.WriteLine("PASS: positive signatures and confirmed body discoveries");
    }
}
