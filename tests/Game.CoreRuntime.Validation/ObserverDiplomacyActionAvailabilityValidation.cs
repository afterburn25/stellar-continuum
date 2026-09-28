using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class ObserverDiplomacyActionAvailabilityValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunObserverDiplomacyActionAvailabilityChecks()
    {
        ValidateVisibleSideActionAvailability();
        Console.WriteLine("PASS: observer-safe Diplomacy action availability");
    }

    private static void ValidateVisibleSideActionAvailability()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);

        Observe(diplomacy, observer: 1, target: 2, tick: 1, ContactCondition.Active);
        Observe(diplomacy, observer: 1, target: 3, tick: 2, ContactCondition.StaleOrLost);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 1,
            ContactId: "unknown:signal",
            TargetCivilizationId: null,
            ObservedAtTick: 3,
            ObservedSystemId: 44,
            Awareness: ContactAwareness.DetectedUnidentified,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.40));

        var initial = runtime.BuildActionAvailability(1);
        Require(initial.Count == 2,
            "action availability exposed an unidentified contact as a targetable civilization");

        var activeIdentified = initial.Single(item => item.CounterpartCivilizationId == 2);
        Require(activeIdentified.CanAttemptCommunication &&
                !activeIdentified.CommunicationAvailable &&
                !activeIdentified.CanSendProposal &&
                !activeIdentified.CanSetAccessPermission &&
                activeIdentified.CanDeclareWar,
            "active identified contact produced incorrect visible-side action guidance");

        var staleIdentified = initial.Single(item => item.CounterpartCivilizationId == 3);
        Require(!staleIdentified.CanAttemptCommunication &&
                !staleIdentified.CommunicationAvailable &&
                !staleIdentified.CanSendProposal &&
                staleIdentified.CanDeclareWar,
            "stale identified contact did not preserve identity-only war availability");

        Observe(diplomacy, observer: 2, target: 1, tick: 4, ContactCondition.Active);
        var communication = runtime.Commands.EstablishCommunication(1, 2, tick: 5);
        Require(communication.Accepted,
            "action-availability validation could not establish mutual communication");

        var cooperation = runtime.Commands.SendProposal(
            1,
            2,
            DiplomaticProposalKind.Agreement,
            tick: 6,
            summary: "Cooperation agreement for action availability.",
            agreementType: DiplomaticAgreementType.Cooperation);
        Require(cooperation.Accepted && cooperation.ProposalId.HasValue,
            "action-availability validation could not send cooperation proposal");
        Require(runtime.Commands.RespondToProposal(2, cooperation.ProposalId.Value, accept: true, tick: 7).Accepted,
            "action-availability validation could not activate cooperation agreement");

        var outgoing = runtime.Commands.SendProposal(
            1,
            2,
            DiplomaticProposalKind.Demand,
            tick: 8,
            summary: "Pending outgoing demand.");
        var incoming = runtime.Commands.SendProposal(
            2,
            1,
            DiplomaticProposalKind.AccessRequest,
            tick: 9,
            summary: "Pending incoming access request.");
        Require(outgoing.Accepted && incoming.Accepted,
            "action-availability validation could not create pending proposal directions");

        var communicating = runtime.BuildActionAvailability(1)
            .Single(item => item.CounterpartCivilizationId == 2);
        Require(communicating.CommunicationAvailable &&
                !communicating.CanAttemptCommunication &&
                communicating.CanSendProposal &&
                communicating.CanSetAccessPermission &&
                communicating.CanDeclareWar,
            "active communication produced incorrect general action availability");
        Require(communicating.PendingIncomingProposalCount == 1 &&
                communicating.PendingOutgoingProposalCount == 1 &&
                communicating.CanRespondToPendingProposal &&
                communicating.CanWithdrawPendingProposal,
            "pending proposal direction/count availability was incorrect");
        Require(communicating.ActiveAgreementCount == 1 &&
                communicating.CanTerminateActiveAgreement,
            "active agreement termination availability was not derived from the observer view");

        // Create a hidden third-party agreement. The observer-safe availability for civilization 1
        // must not count or surface it merely because counterpart 2 participates in both pairs.
        Observe(diplomacy, observer: 2, target: 4, tick: 10, ContactCondition.Active);
        Observe(diplomacy, observer: 4, target: 2, tick: 11, ContactCondition.Active);
        Require(runtime.Commands.EstablishCommunication(2, 4, tick: 12).Accepted,
            "third-party communication setup failed");
        var hiddenAgreement = runtime.Commands.SendProposal(
            2,
            4,
            DiplomaticProposalKind.Agreement,
            tick: 13,
            summary: "Hidden third-party cooperation.",
            agreementType: DiplomaticAgreementType.Cooperation);
        Require(hiddenAgreement.Accepted && hiddenAgreement.ProposalId.HasValue &&
                runtime.Commands.RespondToProposal(4, hiddenAgreement.ProposalId.Value, accept: true, tick: 14).Accepted,
            "hidden third-party agreement setup failed");

        var afterHiddenAgreement = runtime.BuildActionAvailability(1);
        Require(afterHiddenAgreement.All(item => item.CounterpartCivilizationId != 4),
            "action availability leaked a hidden third-party counterpart");
        Require(afterHiddenAgreement.Single(item => item.CounterpartCivilizationId == 2).ActiveAgreementCount == 1,
            "hidden third-party agreement inflated the observer's visible agreement count");

        Require(runtime.Commands.DeclareWar(1, 2, tick: 15).Accepted,
            "formal war command failed during availability transition validation");
        var atWar = runtime.BuildActionAvailability(1)
            .Single(item => item.CounterpartCivilizationId == 2);
        Require(atWar.PoliticalState == DiplomaticPoliticalState.AtWar &&
                !atWar.CanDeclareWar,
            "availability did not suppress redundant formal war declaration while AtWar");
        Require(atWar.ActiveAgreementCount == 0 &&
                !atWar.CanTerminateActiveAgreement,
            "war-terminated agreement remained actionably active in observer availability");
        Require(atWar.CommunicationAvailable && atWar.CanSendProposal,
            "action availability incorrectly treated war as automatic communication loss");
    }

    private static void Observe(
        DiplomacySimulation diplomacy,
        int observer,
        int target,
        long tick,
        ContactCondition condition)
    {
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: observer,
            ContactId: $"civilization:{target}",
            TargetCivilizationId: target,
            ObservedAtTick: tick,
            ObservedSystemId: target + 100,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: condition,
            CommunicationAvailable: false,
            Confidence: condition == ContactCondition.StaleOrLost ? 0.55 : 0.95));
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
