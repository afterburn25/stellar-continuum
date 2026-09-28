using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class DiplomacyCampaignMaintenanceValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyCampaignMaintenanceChecks()
    {
        ValidateLowFrequencyAgingAndProposalExpiry();
        ValidateLoadedCampaignReviewsOverdueStateImmediately();
        Console.WriteLine("PASS: low-frequency live Diplomacy maintenance scheduling");
    }

    private static void ValidateLowFrequencyAgingAndProposalExpiry()
    {
        var state = CreateStateWithCommunicationAndTrackingContact();
        var diplomacy = new DiplomacySimulation(state);
        var proposalId = diplomacy.SendProposal(
            proposer: 1,
            recipient: 2,
            DiplomaticProposalKind.AccessRequest,
            tick: 0,
            summary: "Request transit access.");

        var policy = new DiplomacyCampaignMaintenancePolicy(
            ReviewIntervalTicks: 100,
            ContactStaleAfterTicks: 200,
            ProposalLifetimeTicks: 300);
        var scheduler = new DiplomacyCampaignMaintenanceScheduler(state, policy);
        scheduler.Reset(0, reviewImmediately: true);

        var initial = scheduler.ReviewIfDue(0);
        Require(initial.Ran, "initial maintenance review did not run");
        Require(initial.ContactAging.NewlyStaleContacts == 0, "fresh contact became stale immediately");
        Require(initial.ProposalLifecycle.NewlyExpiredProposals == 0, "fresh proposal expired immediately");

        var early = scheduler.ReviewIfDue(99);
        Require(!early.Ran, "maintenance scanned before its review cadence was due");

        var aging = scheduler.ReviewIfDue(200);
        Require(aging.Ran, "maintenance did not run after crossing the cadence boundary");
        Require(aging.ContactAging.NewlyStaleContacts == 1, "exactly one observation-only tracking contact should have gone stale");
        Require(aging.ProposalLifecycle.NewlyExpiredProposals == 0, "proposal expired before its configured lifetime");
        Require(
            state.GetContact(1, "tracking")?.Condition == ContactCondition.StaleOrLost,
            "observation-only tracking contact did not become stale");
        Require(
            state.GetContact(1, "communication-a-b")?.CommunicationAvailable == true &&
            state.GetContact(1, "communication-a-b")?.Condition == ContactCondition.Active,
            "active communication contact incorrectly aged out");

        var beforeExpiry = scheduler.ReviewIfDue(299);
        Require(!beforeExpiry.Ran, "maintenance scanned again before the next cadence boundary");

        var expiry = scheduler.ReviewIfDue(300);
        Require(expiry.Ran, "maintenance did not run at the next cadence boundary");
        Require(expiry.ProposalLifecycle.NewlyExpiredProposals == 1, "pending proposal did not expire at its configured lifetime");
        Require(
            state.Snapshot().Proposals.Single(proposal => proposal.ProposalId == proposalId).Status == DiplomaticProposalStatus.Expired,
            "proposal state did not transition to Expired");

        var historyCount = state.Snapshot().RecentHistory.Length;
        var duplicate = scheduler.ReviewIfDue(300);
        Require(!duplicate.Ran, "same-tick maintenance review ran twice");
        Require(state.Snapshot().RecentHistory.Length == historyCount, "same-tick maintenance duplicated history events");
    }

    private static void ValidateLoadedCampaignReviewsOverdueStateImmediately()
    {
        var state = CreateStateWithCommunicationAndTrackingContact();
        var diplomacy = new DiplomacySimulation(state);
        var proposalId = diplomacy.SendProposal(
            proposer: 1,
            recipient: 2,
            DiplomaticProposalKind.AccessRequest,
            tick: 400,
            summary: "Overdue after reload.");

        var restored = DiplomacyState.Restore(state.Snapshot());
        var policy = new DiplomacyCampaignMaintenancePolicy(
            ReviewIntervalTicks: 100,
            ContactStaleAfterTicks: 10_000,
            ProposalLifetimeTicks: 300);
        var scheduler = new DiplomacyCampaignMaintenanceScheduler(restored, policy);
        scheduler.Reset(700, reviewImmediately: true);

        var review = scheduler.ReviewIfDue(700);
        Require(review.Ran, "loaded campaign did not perform its immediate maintenance review");
        Require(review.ProposalLifecycle.NewlyExpiredProposals == 1, "overdue loaded proposal was not expired immediately");
        Require(
            restored.Snapshot().Proposals.Single(proposal => proposal.ProposalId == proposalId).Status == DiplomaticProposalStatus.Expired,
            "loaded overdue proposal did not persist its expiration transition");
    }

    private static DiplomacyState CreateStateWithCommunicationAndTrackingContact()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 1,
            ContactId: "communication-a-b",
            TargetCivilizationId: 2,
            ObservedAtTick: 0,
            ObservedSystemId: 10,
            Awareness: ContactAwareness.CommunicationAvailable,
            Condition: ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 2,
            ContactId: "communication-b-a",
            TargetCivilizationId: 1,
            ObservedAtTick: 0,
            ObservedSystemId: 10,
            Awareness: ContactAwareness.CommunicationAvailable,
            Condition: ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 1,
            ContactId: "tracking",
            TargetCivilizationId: 2,
            ObservedAtTick: 0,
            ObservedSystemId: 10,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.8));

        return state;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
