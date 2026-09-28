using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacyAgreementTerminationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacyAgreementTerminationChecks()
    {
        ValidateAccessAgreementTerminationAndIdempotence();
        ValidateCeasefireTerminationResumesHostility();
        ValidateOrdinaryAgreementTerminationDoesNotInventHostility();
        ValidateParticipantAndCommunicationGates();
        Console.WriteLine("PASS: participant-initiated Diplomacy agreement termination");
    }

    private static void ValidateAccessAgreementTerminationAndIdempotence()
    {
        var state = CreateMutualCommunication(11, 12, tick: 1);
        var diplomacy = new DiplomacySimulation(state);
        var proposalId = diplomacy.SendProposal(
            11,
            12,
            DiplomaticProposalKind.Agreement,
            tick: 2,
            summary: "Mutual transit access.",
            agreementType: DiplomaticAgreementType.Access);
        diplomacy.RespondToProposal(proposalId, responder: 12, accept: true, tick: 3);

        var agreement = state.Snapshot().Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.Access &&
            candidate.Status == DiplomaticAgreementStatus.Active);
        Require(state.GetAccessPermission(11, 12) == AccessPermission.Granted &&
                state.GetAccessPermission(12, 11) == AccessPermission.Granted,
            "accepted Access agreement did not grant mutual access before termination");

        var service = new DiplomaticAgreementTerminationService(state);
        var beforeHistory = state.Snapshot().RecentHistory.Length;
        var result = service.Terminate(
            agreement.AgreementId,
            requesterCivilizationId: 11,
            tick: 4,
            reason: "Transit compact no longer serves current policy.");

        Require(result.Terminated && result.ClearedMutualAccess && !result.ResumedHostility,
            "Access agreement termination result did not report its actual side effects");
        var terminated = state.Snapshot().Agreements.Single(candidate => candidate.AgreementId == agreement.AgreementId);
        Require(terminated.Status == DiplomaticAgreementStatus.Terminated && terminated.EndedAtTick == 4,
            "Access agreement did not persist terminated status/end tick");
        Require(state.GetAccessPermission(11, 12) == AccessPermission.Unspecified &&
                state.GetAccessPermission(12, 11) == AccessPermission.Unspecified,
            "terminating mutual Access agreement did not clear both legal access directions");
        Require(state.GetRelationship(11, 12)?.PoliticalState == DiplomaticPoliticalState.Peace,
            "ending Access agreement invented political hostility");
        Require(state.BuildViewFor(11).RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.AgreementTerminated &&
                evt.SecondaryCivilizationId == 12),
            "agreement participant did not receive termination event");
        Require(state.BuildViewFor(12).RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.AgreementTerminated &&
                evt.SecondaryCivilizationId == 12),
            "counterparty did not receive termination event");

        var afterFirstTerminationHistory = state.Snapshot().RecentHistory.Length;
        Require(afterFirstTerminationHistory > beforeHistory,
            "agreement termination did not append meaningful history");
        var retry = service.Terminate(
            agreement.AgreementId,
            requesterCivilizationId: 12,
            tick: 5,
            reason: "Idempotent retry.");
        Require(!retry.Terminated,
            "retrying an already-terminated agreement was not idempotent");
        Require(state.Snapshot().RecentHistory.Length == afterFirstTerminationHistory,
            "idempotent agreement termination retry duplicated diplomatic history");

        DiplomacySnapshotInvariantValidator.Validate(state.Snapshot());
    }

    private static void ValidateCeasefireTerminationResumesHostility()
    {
        var state = CreateMutualCommunication(21, 22, tick: 10);
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.DeclareWar(21, 22, tick: 11);

        var ceasefireProposal = diplomacy.SendProposal(
            21,
            22,
            DiplomaticProposalKind.CeasefireOffer,
            tick: 12,
            summary: "Suspend fighting while negotiations continue.");
        diplomacy.RespondToProposal(ceasefireProposal, responder: 22, accept: true, tick: 13);
        Require(state.GetRelationship(21, 22)?.PoliticalState == DiplomaticPoliticalState.Ceasefire,
            "accepted ceasefire did not enter Ceasefire political state");
        Require(!new DiplomacyCombatHostilityView(state).AreHostile(21, 22),
            "Combat still treated active ceasefire participants as hostile");

        var agreement = state.Snapshot().Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.Ceasefire &&
            candidate.Status == DiplomaticAgreementStatus.Active);
        var result = new DiplomaticAgreementTerminationService(state).Terminate(
            agreement.AgreementId,
            requesterCivilizationId: 22,
            tick: 14,
            reason: "Ceasefire talks collapsed.");

        Require(result.Terminated && result.ResumedHostility,
            "ceasefire termination did not report resumed hostility");
        Require(state.GetRelationship(21, 22)?.PoliticalState == DiplomaticPoliticalState.Hostile,
            "ending a ceasefire did not return the bilateral state to Hostile");
        Require(new DiplomacyCombatHostilityView(state).AreHostile(21, 22),
            "Combat hostility bridge did not reflect the resumed Hostile state");
        Require(state.GetRelationship(21, 22)?.PoliticalState != DiplomaticPoliticalState.AtWar,
            "ending a ceasefire silently declared a new war");

        DiplomacySnapshotInvariantValidator.Validate(state.Snapshot());
    }

    private static void ValidateOrdinaryAgreementTerminationDoesNotInventHostility()
    {
        var state = CreateMutualCommunication(31, 32, tick: 20);
        var diplomacy = new DiplomacySimulation(state);
        var proposal = diplomacy.SendProposal(
            31,
            32,
            DiplomaticProposalKind.Agreement,
            tick: 21,
            summary: "Cooperative scientific exchange.",
            agreementType: DiplomaticAgreementType.Cooperation);
        diplomacy.RespondToProposal(proposal, responder: 32, accept: true, tick: 22);
        var agreement = state.Snapshot().Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.Cooperation &&
            candidate.Status == DiplomaticAgreementStatus.Active);

        var result = new DiplomaticAgreementTerminationService(state).Terminate(
            agreement.AgreementId,
            requesterCivilizationId: 31,
            tick: 23,
            reason: "Cooperation framework completed its purpose.");

        Require(result.Terminated && !result.ClearedMutualAccess && !result.ResumedHostility,
            "ordinary agreement termination reported unrelated side effects");
        Require(state.GetRelationship(31, 32)?.PoliticalState == DiplomaticPoliticalState.Peace,
            "ending Cooperation agreement invented hostility or war");
        Require(!new DiplomacyCombatHostilityView(state).AreHostile(31, 32),
            "Combat became hostile after an ordinary peaceful agreement ended");

        DiplomacySnapshotInvariantValidator.Validate(state.Snapshot());
    }

    private static void ValidateParticipantAndCommunicationGates()
    {
        var state = CreateMutualCommunication(41, 42, tick: 30);
        var diplomacy = new DiplomacySimulation(state);
        var proposal = diplomacy.SendProposal(
            41,
            42,
            DiplomaticProposalKind.TradeOffer,
            tick: 31,
            summary: "Exchange strategic materials.",
            externalTermsReference: "agreement-termination:trade:41-42");
        diplomacy.RespondToProposal(proposal, responder: 42, accept: true, tick: 32);
        var agreement = state.Snapshot().Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.Trade &&
            candidate.Status == DiplomaticAgreementStatus.Active);
        var service = new DiplomaticAgreementTerminationService(state);

        ExpectInvalidOperation(
            () => service.Terminate(agreement.AgreementId, 99, tick: 33, reason: "Unauthorized."),
            "non-participant was allowed to terminate an agreement");
        Require(state.Snapshot().Agreements.Single(candidate => candidate.AgreementId == agreement.AgreementId)
                .Status == DiplomaticAgreementStatus.Active,
            "unauthorized termination attempt mutated agreement state");

        diplomacy.MarkContactLost(41, "agreement-contact-42", tick: 34);
        ExpectInvalidOperation(
            () => service.Terminate(
                agreement.AgreementId,
                requesterCivilizationId: 41,
                tick: 35,
                reason: "Attempt without current communication."),
            "agreement termination succeeded without two-way communication");
        Require(state.Snapshot().Agreements.Single(candidate => candidate.AgreementId == agreement.AgreementId)
                .Status == DiplomaticAgreementStatus.Active,
            "failed communication-gated termination mutated agreement state");

        diplomacy.ProcessContactOpportunity(CommunicatingContact(41, 42, tick: 36));
        var successful = service.Terminate(
            agreement.AgreementId,
            requesterCivilizationId: 42,
            tick: 37,
            reason: "Trade framework formally ended after communications resumed.");
        Require(successful.Terminated,
            "agreement could not be terminated after two-way communication was restored");

        DiplomacySnapshotInvariantValidator.Validate(state.Snapshot());
    }

    private static DiplomacyState CreateMutualCommunication(int first, int second, long tick)
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(CommunicatingContact(first, second, tick));
        diplomacy.ProcessContactOpportunity(CommunicatingContact(second, first, tick));
        return state;
    }

    private static FirstContactOpportunity CommunicatingContact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"agreement-contact-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: null,
        Awareness: ContactAwareness.CommunicationAvailable,
        Condition: ContactCondition.Active,
        CommunicationAvailable: true,
        Confidence: 1.0);

    private static void ExpectInvalidOperation(Action action, string message)
    {
        var rejected = false;
        try
        {
            action();
        }
        catch (InvalidOperationException)
        {
            rejected = true;
        }

        Require(rejected, message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
