using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacyPostRestoreLifecycleValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyPostRestoreLifecycleChecks()
    {
        ValidateRestoredStateContinuesLifecycleAndIdentityAllocation();
        Console.WriteLine("PASS: Diplomacy post-restore lifecycle and ID continuity");
    }

    private static void ValidateRestoredStateContinuesLifecycleAndIdentityAllocation()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.ProcessContactOpportunity(CommunicatingContact(201, 202, tick: 1));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(202, 201, tick: 1));
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 201,
            ContactId: "aging-contact-203",
            TargetCivilizationId: 203,
            ObservedAtTick: 5,
            ObservedSystemId: 55,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.7));

        var pendingDemandId = diplomacy.SendProposal(
            201,
            202,
            DiplomaticProposalKind.Demand,
            tick: 10,
            summary: "Time-limited pre-save withdrawal demand.");

        var preSave = state.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(preSave);

        var expectedNextClaimId = preSave.NextClaimId;
        var expectedNextAgreementId = preSave.NextAgreementId;
        var expectedNextProposalId = preSave.NextProposalId;
        var expectedNextEventId = preSave.NextEventId;

        var json = JsonSerializer.Serialize(preSave);
        var serialized = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(json)
            ?? throw new InvalidOperationException("Post-restore lifecycle snapshot JSON round trip returned null.");
        DiplomacySnapshotInvariantValidator.Validate(serialized);

        var restored = DiplomacyState.Restore(serialized);
        DiplomacySnapshotInvariantValidator.Validate(restored.Snapshot());
        var restoredDiplomacy = new DiplomacySimulation(restored);

        var expiry = new DiplomaticProposalLifecycleService(restored)
            .Review(nowTick: 20, defaultLifetimeTicks: 10);
        Require(expiry.PendingProposalsReviewed == 1 && expiry.NewlyExpiredProposals == 1,
            "restored pending proposal did not continue through scheduled expiration");
        Require(restored.BuildViewFor(201).Proposals.Single(proposal => proposal.ProposalId == pendingDemandId).Status ==
                DiplomaticProposalStatus.Expired,
            "restored pending demand did not become authoritatively expired");

        var aging = new DiplomaticContactAgingService(restored)
            .Review(nowTick: 20, staleAfterTicks: 10);
        Require(aging.NewlyStaleContacts == 1,
            "restored observation-only contact did not continue through scheduled aging");
        var agedContact = restored.GetContact(201, "aging-contact-203")
            ?? throw new InvalidOperationException("restored aging contact disappeared");
        Require(agedContact.Condition == ContactCondition.StaleOrLost && agedContact.LastObservedTick == 5,
            "post-restore contact aging did not preserve the original observation timestamp");
        Require(restored.GetContact(201, 202)?.CommunicationAvailable == true &&
                restored.GetContact(202, 201)?.CommunicationAvailable == true,
            "post-restore contact aging incorrectly aged active communication channels");

        var newClaimId = restoredDiplomacy.AssertTerritorialClaim(201, systemId: 77, tick: 21);
        Require(newClaimId == expectedNextClaimId,
            "restored claim allocator skipped or reused the persisted next claim ID");
        restoredDiplomacy.CommunicateTerritorialClaim(newClaimId, recipient: 202, tick: 22);

        var newDemandId = restoredDiplomacy.SendProposal(
            201,
            202,
            DiplomaticProposalKind.Demand,
            tick: 23,
            summary: "Post-restore follow-up demand.");
        Require(newDemandId == expectedNextProposalId,
            "restored proposal allocator skipped or reused the persisted next proposal ID");

        var agreementProposalId = restoredDiplomacy.SendProposal(
            201,
            202,
            DiplomaticProposalKind.Agreement,
            tick: 24,
            summary: "Post-restore cooperation framework.",
            agreementType: DiplomaticAgreementType.Cooperation);
        Require(agreementProposalId == expectedNextProposalId + 1,
            "restored proposal allocator was not monotonic after its first post-restore allocation");
        restoredDiplomacy.RespondToProposal(agreementProposalId, responder: 202, accept: true, tick: 25);

        var final = restored.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(final);

        Require(final.Agreements.Any(agreement =>
                agreement.AgreementId == expectedNextAgreementId &&
                agreement.Type == DiplomaticAgreementType.Cooperation &&
                agreement.Status == DiplomaticAgreementStatus.Active),
            "restored agreement allocator did not continue from the persisted next agreement ID");
        Require(final.NextClaimId == expectedNextClaimId + 1,
            "post-restore claim allocation did not advance next claim ID exactly once");
        Require(final.NextProposalId == expectedNextProposalId + 2,
            "post-restore proposal allocation did not advance next proposal ID twice");
        Require(final.NextAgreementId == expectedNextAgreementId + 1,
            "post-restore agreement allocation did not advance next agreement ID exactly once");

        var newEvents = final.RecentHistory
            .Where(history => history.EventId >= expectedNextEventId)
            .OrderBy(history => history.EventId)
            .ToArray();
        Require(newEvents.Length >= 7,
            "post-restore lifecycle/actions did not append the expected meaningful history");
        Require(newEvents[0].EventId == expectedNextEventId &&
                newEvents.Select(history => history.EventId)
                    .SequenceEqual(Enumerable.Range(0, newEvents.Length)
                        .Select(offset => expectedNextEventId + offset)),
            "post-restore diplomatic event IDs were not contiguous and monotonic");
        Require(newEvents.Any(history => history.Kind == DiplomaticEventKind.ProposalExpired) &&
                newEvents.Any(history => history.Kind == DiplomaticEventKind.ContactLost) &&
                newEvents.Any(history => history.Kind == DiplomaticEventKind.ClaimAsserted) &&
                newEvents.Any(history => history.Kind == DiplomaticEventKind.ProposalAccepted) &&
                newEvents.Any(history => history.Kind == DiplomaticEventKind.AgreementActivated),
            "post-restore state did not continue all tested lifecycle/event transitions");
        Require(final.NextEventId == expectedNextEventId + newEvents.Length,
            "post-restore event allocator does not match the appended event sequence");

        var secondRoundTripJson = JsonSerializer.Serialize(final);
        var secondRoundTrip = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(secondRoundTripJson)
            ?? throw new InvalidOperationException("Second post-restore Diplomacy round trip returned null.");
        DiplomacySnapshotInvariantValidator.Validate(secondRoundTrip);
        var restoredAgain = DiplomacyState.Restore(secondRoundTrip);
        DiplomacySnapshotInvariantValidator.Validate(restoredAgain.Snapshot());

        Require(restoredAgain.Snapshot().NextClaimId == final.NextClaimId &&
                restoredAgain.Snapshot().NextAgreementId == final.NextAgreementId &&
                restoredAgain.Snapshot().NextProposalId == final.NextProposalId &&
                restoredAgain.Snapshot().NextEventId == final.NextEventId,
            "a second persistence cycle changed the continued Diplomacy identity allocators");
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"continuity-contact-{observer}-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: 42,
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
