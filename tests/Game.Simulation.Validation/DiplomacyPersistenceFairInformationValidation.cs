using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacyPersistenceFairInformationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyPersistenceFairInformationChecks()
    {
        ValidateThirdPartySecretsSurviveRoundTripWithoutLeaking();
        ValidateDirectionalContactRemainsDirectionalAfterRestore();
        Console.WriteLine("PASS: Diplomacy persistence preserves fair-information boundaries");
    }

    private static void ValidateThirdPartySecretsSurviveRoundTripWithoutLeaking()
    {
        var state = new DiplomacyState();
        EstablishMutualCommunication(state, 1, 2, tick: 10);
        EstablishMutualCommunication(state, 2, 3, tick: 20);
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.ApplyRelationshipImpact(
            1,
            2,
            new RelationshipImpact(
                TrustDelta: 0.1,
                HostilityDelta: 0.0,
                FearDelta: 0.0,
                RespectDelta: 0.05,
                CooperationDelta: 0.1,
                GrievanceSeverity: 0.0,
                Reason: "Visible bilateral cooperation."),
            tick: 25);

        var secretAgreementProposal = diplomacy.SendProposal(
            2,
            3,
            DiplomaticProposalKind.Agreement,
            tick: 30,
            summary: "Private two-party cooperation framework.",
            agreementType: DiplomaticAgreementType.Cooperation);
        diplomacy.RespondToProposal(secretAgreementProposal, 3, accept: true, tick: 31);

        var secretTradeProposal = diplomacy.SendProposal(
            3,
            2,
            DiplomaticProposalKind.TradeOffer,
            tick: 32,
            summary: "Private resource exchange.",
            externalTermsReference: "validation:secret-trade:3-2");
        diplomacy.RespondToProposal(secretTradeProposal, 2, accept: true, tick: 33);

        var secretClaimId = diplomacy.AssertTerritorialClaim(2, systemId: 88, tick: 34);
        diplomacy.CommunicateTerritorialClaim(secretClaimId, recipient: 3, tick: 35);
        diplomacy.RespondToTerritorialClaim(
            secretClaimId,
            responder: 3,
            TerritorialClaimResponse.Disputed,
            tick: 36);
        diplomacy.RecordTrespass(territorialCivilizationId: 2, intruder: 3, systemId: 88, tick: 37);
        diplomacy.IssueBorderWarning(issuer: 2, recipient: 3, systemId: 88, tick: 38);

        var beforeObserverOne = state.BuildViewFor(1);
        RequireObserverOneDoesNotKnowCivilizationThree(beforeObserverOne, secretClaimId);

        var snapshot = state.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(snapshot);
        var json = JsonSerializer.Serialize(snapshot);
        var deserialized = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(json)
            ?? throw new InvalidOperationException("Diplomacy snapshot JSON round trip returned null.");
        DiplomacySnapshotInvariantValidator.Validate(deserialized);

        var restored = DiplomacyState.Restore(deserialized);
        var restoredSnapshot = restored.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(restoredSnapshot);

        var afterObserverOne = restored.BuildViewFor(1);
        RequireObserverOneDoesNotKnowCivilizationThree(afterObserverOne, secretClaimId);

        Require(afterObserverOne.Contacts.Count == beforeObserverOne.Contacts.Count,
            "snapshot restore changed observer one's directional contact count");
        Require(afterObserverOne.Relationships.Count == beforeObserverOne.Relationships.Count,
            "snapshot restore changed observer one's visible relationship count");
        Require(afterObserverOne.Agreements.Count == beforeObserverOne.Agreements.Count,
            "snapshot restore changed observer one's visible agreement count");
        Require(afterObserverOne.Proposals.Count == beforeObserverOne.Proposals.Count,
            "snapshot restore changed observer one's visible proposal count");
        Require(afterObserverOne.Claims.Count == beforeObserverOne.Claims.Count,
            "snapshot restore changed observer one's visible claim count");
        Require(afterObserverOne.ClaimResponses.Count == beforeObserverOne.ClaimResponses.Count,
            "snapshot restore changed observer one's visible claim-response count");
        Require(afterObserverOne.RecentEvents.Select(evt => evt.EventId)
                .SequenceEqual(beforeObserverOne.RecentEvents.Select(evt => evt.EventId)),
            "snapshot restore changed observer one's explicit diplomatic event audience");

        var observerTwo = restored.BuildViewFor(2);
        var observerThree = restored.BuildViewFor(3);
        Require(observerTwo.Relationships.Any(relationship => relationship.OtherCivilizationId == 3),
            "restore removed civilization 2's legitimately known relationship with civilization 3");
        Require(observerThree.Relationships.Any(relationship => relationship.OtherCivilizationId == 2),
            "restore removed civilization 3's legitimately known relationship with civilization 2");
        Require(observerTwo.Agreements.Any(agreement =>
                agreement.CivilizationAId == 2 && agreement.CivilizationBId == 3 &&
                agreement.Type == DiplomaticAgreementType.Cooperation &&
                agreement.Status == DiplomaticAgreementStatus.Active),
            "restore removed the private 2/3 cooperation agreement from a participant");
        Require(observerThree.Agreements.Any(agreement =>
                agreement.CivilizationAId == 2 && agreement.CivilizationBId == 3 &&
                agreement.Type == DiplomaticAgreementType.Trade &&
                agreement.ExternalTermsReference == "validation:secret-trade:3-2"),
            "restore removed private trade terms from a legitimate participant");
        Require(observerTwo.Claims.Any(claim => claim.ClaimId == secretClaimId) &&
                observerThree.Claims.Any(claim => claim.ClaimId == secretClaimId),
            "restore removed a communicated claim from its legitimate audience");
        Require(observerTwo.ClaimResponses.Any(response => response.ClaimId == secretClaimId) &&
                observerThree.ClaimResponses.Any(response => response.ClaimId == secretClaimId),
            "restore removed a claim response from its legitimate parties");
    }

    private static void ValidateDirectionalContactRemainsDirectionalAfterRestore()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 4,
            ContactId: "directional-4-sees-5",
            TargetCivilizationId: 5,
            ObservedAtTick: 100,
            ObservedSystemId: 44,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.8));

        Require(state.BuildViewFor(4).Contacts.Any(contact => contact.TargetCivilizationId == 5),
            "observer 4 did not retain its legitimate one-way contact before persistence");
        Require(state.BuildViewFor(5).Contacts.Count == 0 && state.BuildViewFor(5).Relationships.Count == 0,
            "one-way contact became reciprocal before persistence");

        var snapshot = state.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(snapshot);
        var json = JsonSerializer.Serialize(snapshot);
        var deserialized = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(json)
            ?? throw new InvalidOperationException("Directional Diplomacy snapshot round trip returned null.");
        var restored = DiplomacyState.Restore(deserialized);
        DiplomacySnapshotInvariantValidator.Validate(restored.Snapshot());

        var observerFour = restored.BuildViewFor(4);
        var observerFive = restored.BuildViewFor(5);
        Require(observerFour.Contacts.Count == 1 && observerFour.Contacts[0].TargetCivilizationId == 5,
            "restore removed legitimate one-way contact from its observer");
        Require(observerFour.Relationships.Count == 1 && observerFour.Relationships[0].OtherCivilizationId == 5,
            "restore removed observer 4's visible established relationship");
        Require(observerFive.Contacts.Count == 0,
            "restore manufactured reciprocal contact for the previously unaware civilization");
        Require(observerFive.Relationships.Count == 0,
            "restore exposed bilateral authoritative relationship state to an unaware civilization");
        Require(observerFive.RecentEvents.Count == 0,
            "restore leaked observer 4's contact event into civilization 5's event audience");
    }

    private static void RequireObserverOneDoesNotKnowCivilizationThree(
        DiplomaticStateView observerOne,
        long secretClaimId)
    {
        Require(observerOne.Contacts.All(contact => contact.TargetCivilizationId != 3),
            "observer 1 learned civilization 3 through a third-party contact chain");
        Require(observerOne.Relationships.All(relationship => relationship.OtherCivilizationId != 3),
            "observer 1 learned the hidden 2/3 relationship");
        Require(observerOne.Agreements.All(agreement =>
                agreement.CivilizationAId != 3 && agreement.CivilizationBId != 3),
            "observer 1 learned a hidden agreement involving civilization 3");
        Require(observerOne.Proposals.All(proposal =>
                proposal.ProposerCivilizationId != 3 && proposal.RecipientCivilizationId != 3),
            "observer 1 learned a hidden proposal involving civilization 3");
        Require(observerOne.Claims.All(claim => claim.ClaimId != secretClaimId),
            "observer 1 learned a territorial claim that was never communicated to it");
        Require(observerOne.ClaimResponses.All(response => response.ClaimId != secretClaimId),
            "observer 1 learned a response to a claim it never knew");
        Require(observerOne.RecentEvents.All(evt =>
                evt.PrimaryCivilizationId != 3 && evt.SecondaryCivilizationId != 3),
            "observer 1 received a third-party event outside its explicit information audience");
    }

    private static void EstablishMutualCommunication(
        DiplomacyState state,
        int first,
        int second,
        long tick)
    {
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(CommunicatingContact(first, second, tick));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(second, first, tick));
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"persistence-contact-{observer}-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: 12,
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
