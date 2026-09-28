using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacyLongCampaignBoundsValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyLongCampaignBoundsChecks()
    {
        ValidateContactStorageEvictsOnlyEligibleStaleUnknowns();
        ValidateProposalStorageAndPendingPairBounds();
        ValidateGrievanceHistoryRemainsBoundedAndRecent();
        Console.WriteLine("PASS: Diplomacy long-campaign bounded state churn");
    }

    private static void ValidateContactStorageEvictsOnlyEligibleStaleUnknowns()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        for (var i = 0; i < DiplomacyState.MaxContactRecords; i++)
        {
            diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
                ObserverCivilizationId: 1,
                ContactId: $"stale-unknown-{i:D4}",
                TargetCivilizationId: null,
                ObservedAtTick: i,
                ObservedSystemId: i % 64,
                Awareness: ContactAwareness.DetectedUnidentified,
                Condition: ContactCondition.StaleOrLost,
                CommunicationAvailable: false,
                Confidence: 0.1));
        }

        Require(state.Snapshot().Contacts.Length == DiplomacyState.MaxContactRecords,
            "contact storage did not reach its configured bound deterministically");
        Require(state.Snapshot().RecentHistory.Length == DiplomacyState.MaxRecentHistoryEvents,
            "contact churn allowed recent diplomatic history to exceed its bound");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 1,
            ContactId: "new-active-unknown",
            TargetCivilizationId: null,
            ObservedAtTick: DiplomacyState.MaxContactRecords,
            ObservedSystemId: 99,
            Awareness: ContactAwareness.DetectedUnidentified,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.4));

        var afterEviction = state.Snapshot();
        Require(afterEviction.Contacts.Length == DiplomacyState.MaxContactRecords,
            "contact storage grew beyond its configured bound after churn");
        Require(state.GetContact(1, "stale-unknown-0000") is null,
            "contact churn did not evict the oldest eligible stale unidentified record");
        Require(state.GetContact(1, "new-active-unknown") is not null,
            "new contact was lost while recycling eligible stale contact storage");
        DiplomacySnapshotInvariantValidator.Validate(afterEviction);

        // A full store containing no eligible stale/unidentified record must fail closed instead
        // of discarding established political knowledge just to make room.
        var protectedState = new DiplomacyState();
        var protectedDiplomacy = new DiplomacySimulation(protectedState);
        for (var i = 0; i < DiplomacyState.MaxContactRecords; i++)
        {
            protectedDiplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
                ObserverCivilizationId: 2,
                ContactId: $"active-unknown-{i:D4}",
                TargetCivilizationId: null,
                ObservedAtTick: i,
                ObservedSystemId: i % 64,
                Awareness: ContactAwareness.DetectedUnidentified,
                Condition: ContactCondition.Active,
                CommunicationAvailable: false,
                Confidence: 0.2));
        }

        var rejected = false;
        try
        {
            protectedDiplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
                ObserverCivilizationId: 2,
                ContactId: "overflow-active-contact",
                TargetCivilizationId: null,
                ObservedAtTick: DiplomacyState.MaxContactRecords,
                ObservedSystemId: 100,
                Awareness: ContactAwareness.DetectedUnidentified,
                Condition: ContactCondition.Active,
                CommunicationAvailable: false,
                Confidence: 0.3));
        }
        catch (InvalidOperationException)
        {
            rejected = true;
        }

        Require(rejected,
            "full contact storage silently discarded non-stale diplomatic knowledge");
        Require(protectedState.Snapshot().Contacts.Length == DiplomacyState.MaxContactRecords,
            "failed contact overflow changed the bounded contact count");
    }

    private static void ValidateProposalStorageAndPendingPairBounds()
    {
        var state = CreateMutualCommunication(10, 11);
        var diplomacy = new DiplomacySimulation(state);

        for (var i = 0; i < DiplomacyState.MaxStoredProposals; i++)
        {
            var created = 100L + i * 2L;
            var proposal = diplomacy.SendProposal(
                10,
                11,
                DiplomaticProposalKind.Demand,
                created,
                $"Resolved bounded demand {i}");
            diplomacy.RespondToProposal(proposal, 11, accept: false, tick: created + 1);
        }

        var full = state.Snapshot();
        Require(full.Proposals.Length == DiplomacyState.MaxStoredProposals,
            "proposal storage did not reach its configured bound");
        var oldestId = full.Proposals.Min(proposal => proposal.ProposalId);

        var newestId = diplomacy.SendProposal(
            10,
            11,
            DiplomaticProposalKind.Demand,
            tick: 1000,
            summary: "Newest unresolved bounded demand");

        var compacted = state.Snapshot();
        Require(compacted.Proposals.Length == DiplomacyState.MaxStoredProposals,
            "proposal storage grew beyond its configured bound");
        Require(compacted.Proposals.All(proposal => proposal.ProposalId != oldestId),
            "proposal compaction did not discard the oldest resolved proposal first");
        Require(compacted.Proposals.Any(proposal => proposal.ProposalId == newestId &&
                                                   proposal.Status == DiplomaticProposalStatus.Pending),
            "proposal compaction lost the newly created pending proposal");
        DiplomacySnapshotInvariantValidator.Validate(compacted);

        var pairBoundState = CreateMutualCommunication(20, 21);
        var pairBoundDiplomacy = new DiplomacySimulation(pairBoundState);
        for (var i = 0; i < DiplomacyState.MaxPendingProposalsPerPair; i++)
        {
            pairBoundDiplomacy.SendProposal(
                20,
                21,
                DiplomaticProposalKind.Demand,
                tick: 200 + i,
                summary: $"Pending bounded demand {i}");
        }

        var ninthRejected = false;
        try
        {
            pairBoundDiplomacy.SendProposal(
                20,
                21,
                DiplomaticProposalKind.Demand,
                tick: 300,
                summary: "Overflow pending demand");
        }
        catch (InvalidOperationException)
        {
            ninthRejected = true;
        }

        Require(ninthRejected,
            "per-pair pending proposal bound allowed an unbounded negotiation queue");
        Require(pairBoundState.Snapshot().Proposals.Count(proposal =>
                    proposal.Status == DiplomaticProposalStatus.Pending) ==
                DiplomacyState.MaxPendingProposalsPerPair,
            "rejected pending overflow mutated the proposal queue");
        DiplomacySnapshotInvariantValidator.Validate(pairBoundState.Snapshot());
    }

    private static void ValidateGrievanceHistoryRemainsBoundedAndRecent()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 30,
            ContactId: "grievance-contact-31",
            TargetCivilizationId: 31,
            ObservedAtTick: 1,
            ObservedSystemId: 7,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 1.0));

        for (var i = 0; i < 24; i++)
        {
            diplomacy.ApplyRelationshipImpact(
                30,
                31,
                new RelationshipImpact(
                    TrustDelta: 0.0,
                    HostilityDelta: 0.01,
                    FearDelta: 0.0,
                    RespectDelta: 0.0,
                    CooperationDelta: 0.0,
                    GrievanceSeverity: 0.25,
                    Reason: $"Bounded grievance {i:D2}"),
                tick: 10 + i);
        }

        var relationship = state.GetRelationship(30, 31)
            ?? throw new InvalidOperationException("grievance churn lost its relationship");
        Require(relationship.Grievances.Length == 16,
            "relationship grievance storage did not remain at its bounded recent-history size");
        Require(relationship.Grievances[0].Reason == "Bounded grievance 08" &&
                relationship.Grievances[^1].Reason == "Bounded grievance 23",
            "grievance compaction did not retain the most recent bounded history deterministically");
        Require(relationship.Grievances.All(grievance => grievance.SourceCivilizationId == 31),
            "grievance compaction changed the attributed source civilization");
        DiplomacySnapshotInvariantValidator.Validate(state.Snapshot());
    }

    private static DiplomacyState CreateMutualCommunication(int first, int second)
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(CommunicatingContact(first, second, tick: 1));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(second, first, tick: 1));
        return state;
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"bounded-contact-{observer}-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: 5,
        Awareness: ContactAwareness.CommunicationAvailable,
        Condition: ContactCondition.Active,
        CommunicationAvailable: true,
        Confidence: 1.0);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
