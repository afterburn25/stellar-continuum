using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomaticProposalLifecycleValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomaticProposalLifecycleChecks()
    {
        ValidateCallerDefinedProposalLifetimes();
        ValidateResolvedProposalsAreNeverReexpired();
        ValidateExpirationDoesNotRequireCurrentCommunication();
        Console.WriteLine("PASS: scheduled diplomatic proposal lifecycle and expiration");
    }

    private static void ValidateCallerDefinedProposalLifetimes()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        EstablishMutualCommunication(state, diplomacy, 51, 52, tick: 1);

        var access = diplomacy.SendProposal(
            51,
            52,
            DiplomaticProposalKind.AccessRequest,
            tick: 10,
            summary: "Request reciprocal survey access.");
        var demand = diplomacy.SendProposal(
            51,
            52,
            DiplomaticProposalKind.Demand,
            tick: 10,
            summary: "Withdraw the patrol from the disputed system.");
        var trade = diplomacy.SendProposal(
            51,
            52,
            DiplomaticProposalKind.TradeOffer,
            tick: 10,
            summary: "Exchange refined materials for fuel.",
            externalTermsReference: "validation:trade:51-52");

        var lifetimeByKind = new Dictionary<DiplomaticProposalKind, long>
        {
            [DiplomaticProposalKind.Demand] = 5,
            [DiplomaticProposalKind.TradeOffer] = 10,
        };
        var lifecycle = new DiplomaticProposalLifecycleService(state);

        var before = lifecycle.Review(nowTick: 14, defaultLifetimeTicks: 20, lifetimeByKind);
        Require(before.PendingProposalsReviewed == 3 && before.NewlyExpiredProposals == 0,
            "proposal expired before its caller-defined lifetime");

        var demandExpiry = lifecycle.Review(nowTick: 15, defaultLifetimeTicks: 20, lifetimeByKind);
        Require(demandExpiry.PendingProposalsReviewed == 3 && demandExpiry.NewlyExpiredProposals == 1,
            "demand did not expire exactly at its override lifetime");
        Require(Proposal(state, 51, demand).Status == DiplomaticProposalStatus.Expired &&
                Proposal(state, 51, demand).ResolvedAtTick == 15,
            "expired demand did not retain the authoritative expiration tick");

        var tradeExpiry = lifecycle.Review(nowTick: 20, defaultLifetimeTicks: 20, lifetimeByKind);
        Require(tradeExpiry.PendingProposalsReviewed == 2 && tradeExpiry.NewlyExpiredProposals == 1,
            "trade offer did not expire at its override lifetime");
        Require(Proposal(state, 51, trade).Status == DiplomaticProposalStatus.Expired,
            "trade offer remained pending after its expiration threshold");
        Require(Proposal(state, 51, access).Status == DiplomaticProposalStatus.Pending,
            "default-lifetime access request expired too early");

        var defaultExpiry = lifecycle.Review(nowTick: 30, defaultLifetimeTicks: 20, lifetimeByKind);
        Require(defaultExpiry.PendingProposalsReviewed == 1 && defaultExpiry.NewlyExpiredProposals == 1,
            "default-lifetime access request did not expire at its threshold");
        Require(Proposal(state, 51, access).Status == DiplomaticProposalStatus.Expired,
            "default-lifetime proposal remained pending after expiration");

        var repeated = lifecycle.Review(nowTick: 31, defaultLifetimeTicks: 20, lifetimeByKind);
        Require(repeated.PendingProposalsReviewed == 0 && repeated.NewlyExpiredProposals == 0,
            "resolved proposals were repeatedly processed by lifecycle review");
        Require(state.BuildViewFor(51).RecentEvents.Count(evt => evt.Kind == DiplomaticEventKind.ProposalExpired) == 3,
            "proposal expiration history was duplicated or omitted");
        Require(state.BuildViewFor(52).RecentEvents.Count(evt => evt.Kind == DiplomaticEventKind.ProposalExpired) == 3,
            "recipient did not receive the same proposal-expiration history");

        var rejectedLateResponse = false;
        try
        {
            diplomacy.RespondToProposal(demand, 52, accept: true, tick: 32);
        }
        catch (InvalidOperationException)
        {
            rejectedLateResponse = true;
        }
        Require(rejectedLateResponse, "recipient was allowed to accept an expired proposal");
    }

    private static void ValidateResolvedProposalsAreNeverReexpired()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        EstablishMutualCommunication(state, diplomacy, 61, 62, tick: 1);

        var accepted = diplomacy.SendProposal(
            61,
            62,
            DiplomaticProposalKind.Agreement,
            tick: 5,
            summary: "Mutual non-aggression pact.",
            agreementType: DiplomaticAgreementType.NonAggression);
        diplomacy.RespondToProposal(accepted, 62, accept: true, tick: 6);

        var rejected = diplomacy.SendProposal(
            61,
            62,
            DiplomaticProposalKind.AccessRequest,
            tick: 7,
            summary: "Request military transit access.");
        diplomacy.RespondToProposal(rejected, 62, accept: false, tick: 8);

        var withdrawn = diplomacy.SendProposal(
            61,
            62,
            DiplomaticProposalKind.Demand,
            tick: 9,
            summary: "Temporary political demand.");
        diplomacy.WithdrawProposal(withdrawn, 61, tick: 10);

        var lifecycle = new DiplomaticProposalLifecycleService(state);
        var result = lifecycle.Review(nowTick: 100, defaultLifetimeTicks: 1);
        Require(result.PendingProposalsReviewed == 0 && result.NewlyExpiredProposals == 0,
            "accepted/rejected/withdrawn proposals were reconsidered for expiration");
        Require(Proposal(state, 61, accepted).Status == DiplomaticProposalStatus.Accepted,
            "accepted proposal changed status during lifecycle review");
        Require(Proposal(state, 61, rejected).Status == DiplomaticProposalStatus.Rejected,
            "rejected proposal changed status during lifecycle review");
        Require(Proposal(state, 61, withdrawn).Status == DiplomaticProposalStatus.Withdrawn,
            "withdrawn proposal changed status during lifecycle review");
    }

    private static void ValidateExpirationDoesNotRequireCurrentCommunication()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        EstablishMutualCommunication(state, diplomacy, 71, 72, tick: 1);

        var proposal = diplomacy.SendProposal(
            71,
            72,
            DiplomaticProposalKind.Demand,
            tick: 10,
            summary: "Time-limited withdrawal demand.");

        diplomacy.MarkContactLost(71, ContactId(72), tick: 11);
        diplomacy.MarkContactLost(72, ContactId(71), tick: 11);

        var lifecycle = new DiplomaticProposalLifecycleService(state);
        var result = lifecycle.Review(nowTick: 15, defaultLifetimeTicks: 5);
        Require(result.NewlyExpiredProposals == 1,
            "pending proposal required a live communication channel just to expire");
        Require(Proposal(state, 71, proposal).Status == DiplomaticProposalStatus.Expired,
            "proposal remained actionable after its channel disappeared and lifetime elapsed");
    }

    private static DiplomaticProposalSnapshot Proposal(DiplomacyState state, int observer, long proposalId) =>
        state.BuildViewFor(observer).Proposals.Single(proposal => proposal.ProposalId == proposalId);

    private static void EstablishMutualCommunication(
        DiplomacyState state,
        DiplomacySimulation diplomacy,
        int first,
        int second,
        long tick)
    {
        diplomacy.ProcessContactOpportunity(Contact(first, second, tick));
        diplomacy.ProcessContactOpportunity(Contact(second, first, tick));
        new DiplomaticCommunicationService(state).EstablishMutualCommunication(first, second, tick + 1);
    }

    private static FirstContactOpportunity Contact(int observer, int target, long tick) => new(
        observer,
        ContactId(target),
        target,
        tick,
        ObservedSystemId: 12,
        Awareness: ContactAwareness.ContactEstablished,
        Condition: ContactCondition.Active,
        CommunicationAvailable: false,
        Confidence: 1.0);

    private static string ContactId(int target) => $"proposal-contact-{target}";

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
