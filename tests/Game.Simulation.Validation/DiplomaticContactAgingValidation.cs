using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomaticContactAgingValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomaticContactAgingChecks()
    {
        ValidateObservationOnlyContactAgesAndReacquires();
        ValidateActiveCommunicationPreventsAutomaticStaleness();
        Console.WriteLine("PASS: scheduled diplomatic contact aging and reacquisition");
    }

    private static void ValidateObservationOnlyContactAgesAndReacquires()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var aging = new DiplomaticContactAgingService(state);

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 31,
            ContactId: "civilization:32",
            TargetCivilizationId: 32,
            ObservedAtTick: 10,
            ObservedSystemId: 4,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.84));

        var beforeThreshold = aging.Review(nowTick: 19, staleAfterTicks: 10);
        Require(beforeThreshold.NewlyStaleContacts == 0,
            "contact became stale before the caller-supplied threshold");

        var atThreshold = aging.Review(nowTick: 20, staleAfterTicks: 10);
        Require(atThreshold.NewlyStaleContacts == 1,
            "observation-only contact did not become stale at the review threshold");

        var stale = state.BuildViewFor(31).Contacts.Single();
        Require(stale.Condition == ContactCondition.StaleOrLost,
            "aged contact did not enter stale/lost condition");
        Require(!stale.CommunicationAvailable,
            "stale contact retained current communication capability");
        Require(stale.LastObservedTick == 10,
            "contact aging overwrote the true last-observed timestamp with the review time");

        var repeated = aging.Review(nowTick: 40, staleAfterTicks: 10);
        Require(repeated.NewlyStaleContacts == 0,
            "repeated aging emitted duplicate stale transitions");
        Require(state.BuildViewFor(31).RecentEvents.Count(evt => evt.Kind == DiplomaticEventKind.ContactLost) == 1,
            "repeated stale reviews duplicated ContactLost history");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 31,
            ContactId: "civilization:32",
            TargetCivilizationId: 32,
            ObservedAtTick: 41,
            ObservedSystemId: 5,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.93));

        var reacquired = state.BuildViewFor(31).Contacts.Single();
        Require(reacquired.Condition == ContactCondition.Active && reacquired.LastObservedTick == 41,
            "fresh legitimate observation did not reacquire stale contact");
        Require(!reacquired.CommunicationAvailable,
            "reacquisition incorrectly restored a previously lost communication channel");
    }

    private static void ValidateActiveCommunicationPreventsAutomaticStaleness()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(Contact(41, 42, "civilization:42", tick: 2));
        diplomacy.ProcessContactOpportunity(Contact(42, 41, "civilization:41", tick: 3));

        var communication = new DiplomaticCommunicationService(state);
        communication.EstablishMutualCommunication(41, 42, tick: 4);

        var aging = new DiplomaticContactAgingService(state);
        var result = aging.Review(nowTick: 1000, staleAfterTicks: 5);
        Require(result.NewlyStaleContacts == 0,
            "active two-way communication was aged as if no contact existed");
        Require(state.BuildViewFor(41).Contacts.Single().CommunicationAvailable &&
                state.BuildViewFor(42).Contacts.Single().CommunicationAvailable,
            "scheduled review removed a still-active communication channel");
    }

    private static FirstContactOpportunity Contact(int observer, int target, string id, long tick) => new(
        observer,
        id,
        target,
        tick,
        ObservedSystemId: 8,
        Awareness: ContactAwareness.ContactEstablished,
        Condition: ContactCondition.Active,
        CommunicationAvailable: false,
        Confidence: 1.0);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
