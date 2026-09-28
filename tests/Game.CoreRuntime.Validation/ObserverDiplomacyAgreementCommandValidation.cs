using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class ObserverDiplomacyAgreementCommandValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunObserverDiplomacyAgreementCommandChecks()
    {
        ValidateVisibleAgreementTerminationAndIdempotence();
        ValidateActiveAgreementRequiresVisibleChannel();
        ValidateHiddenAgreementIdsDoNotBecomeAnOracle();
        Console.WriteLine("PASS: observer-safe Diplomacy agreement termination gateway");
    }

    private static void ValidateVisibleAgreementTerminationAndIdempotence()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);
        var first = new ObserverDiplomacyCommandService(state);
        var second = new ObserverDiplomacyCommandService(state);

        var sent = first.SendProposal(
            1,
            2,
            DiplomaticProposalKind.Agreement,
            tick: 10,
            summary: "Mutual access agreement.",
            agreementType: DiplomaticAgreementType.Access);
        Require(sent.Accepted && sent.ProposalId is > 0, "access agreement proposal was not created");
        var accepted = second.RespondToProposal(2, sent.ProposalId!.Value, accept: true, tick: 11);
        Require(accepted.Accepted, "access agreement proposal was not accepted");

        var agreement = first.BuildView(1).Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.Access &&
            candidate.Status == DiplomaticAgreementStatus.Active);
        Require(state.GetAccessPermission(1, 2) == AccessPermission.Granted &&
                state.GetAccessPermission(2, 1) == AccessPermission.Granted,
            "accepted Access agreement did not establish mutual access");

        var terminated = first.TerminateAgreement(
            1,
            agreement.AgreementId,
            tick: 12,
            reason: "Strategic access policy changed.");
        Require(terminated.Accepted && terminated.AgreementId == agreement.AgreementId,
            "visible active agreement could not be terminated by a participant");
        Require(state.GetAccessPermission(1, 2) == AccessPermission.Unspecified &&
                state.GetAccessPermission(2, 1) == AccessPermission.Unspecified,
            "terminating Access agreement did not clear mutual access");
        Require(first.BuildView(1).Agreements.Single(candidate => candidate.AgreementId == agreement.AgreementId).Status ==
                DiplomaticAgreementStatus.Terminated,
            "observer view did not expose the authoritative terminated agreement state");

        // Lose the channel after the authoritative termination. A retry must still be a safe,
        // idempotent success rather than depending on now-stale communication.
        new DiplomacySimulation(state).MarkContactLost(1, "1-2", tick: 13);
        var historyCount = state.Snapshot().RecentHistory.Length;
        var retry = first.TerminateAgreement(
            1,
            agreement.AgreementId,
            tick: 14,
            reason: "Retry after response loss.");
        Require(retry.Accepted && retry.Status == ObserverDiplomacyCommandStatus.Accepted,
            "already-terminated visible agreement retry was not idempotently accepted");
        Require(state.Snapshot().RecentHistory.Length == historyCount,
            "idempotent agreement-termination retry appended duplicate history");
    }

    private static void ValidateActiveAgreementRequiresVisibleChannel()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);
        var first = new ObserverDiplomacyCommandService(state);
        var second = new ObserverDiplomacyCommandService(state);

        var sent = first.SendProposal(
            1,
            2,
            DiplomaticProposalKind.Agreement,
            tick: 20,
            summary: "Non-aggression agreement.",
            agreementType: DiplomaticAgreementType.NonAggression);
        Require(sent.Accepted && sent.ProposalId is > 0, "non-aggression setup proposal was not created");
        Require(second.RespondToProposal(2, sent.ProposalId!.Value, accept: true, tick: 21).Accepted,
            "non-aggression setup proposal was not accepted");

        var agreement = first.BuildView(1).Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.NonAggression &&
            candidate.Status == DiplomaticAgreementStatus.Active);
        new DiplomacySimulation(state).MarkContactLost(1, "1-2", tick: 22);

        var result = first.TerminateAgreement(
            1,
            agreement.AgreementId,
            tick: 23,
            reason: "Cannot transmit while channel is stale.");
        Require(!result.Accepted && result.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "active agreement termination ignored the observer's stale communication channel");
        Require(first.BuildView(1).Agreements.Single(candidate => candidate.AgreementId == agreement.AgreementId).Status ==
                DiplomaticAgreementStatus.Active,
            "failed stale-channel termination mutated the agreement");
    }

    private static void ValidateHiddenAgreementIdsDoNotBecomeAnOracle()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);
        EstablishCommunication(state, 3, 4, tick: 0, systemId: 20);
        var third = new ObserverDiplomacyCommandService(state);
        var fourth = new ObserverDiplomacyCommandService(state);

        var sent = third.SendProposal(
            3,
            4,
            DiplomaticProposalKind.Agreement,
            tick: 30,
            summary: "Private third-party non-aggression agreement.",
            agreementType: DiplomaticAgreementType.NonAggression);
        Require(sent.Accepted && sent.ProposalId is > 0, "third-party agreement setup proposal was not created");
        Require(fourth.RespondToProposal(4, sent.ProposalId!.Value, accept: true, tick: 31).Accepted,
            "third-party agreement setup proposal was not accepted");
        var hiddenAgreementId = third.BuildView(3).Agreements.Single(candidate =>
            candidate.Type == DiplomaticAgreementType.NonAggression &&
            candidate.Status == DiplomaticAgreementStatus.Active).AgreementId;

        var observer = new ObserverDiplomacyCommandService(state);
        Require(!observer.BuildView(1).Agreements.Any(candidate => candidate.AgreementId == hiddenAgreementId),
            "third-party agreement leaked into unrelated observer view");

        var hidden = observer.TerminateAgreement(
            1,
            hiddenAgreementId,
            tick: 32,
            reason: "Probe hidden agreement.");
        var nonexistent = observer.TerminateAgreement(
            1,
            999_999,
            tick: 32,
            reason: "Probe nonexistent agreement.");

        Require(!hidden.Accepted && !nonexistent.Accepted,
            "hidden/nonexistent agreement probe was accepted");
        Require(hidden.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistent.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "hidden/nonexistent agreement IDs returned distinguishable status classes");
        Require(hidden.Message == nonexistent.Message,
            "hidden third-party agreement ID could be distinguished from nonexistent ID by message text");
    }

    private static void EstablishCommunication(
        DiplomacyState state,
        int civilizationA,
        int civilizationB,
        long tick,
        int systemId)
    {
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            civilizationA,
            $"{civilizationA}-{civilizationB}",
            civilizationB,
            tick,
            systemId,
            ContactAwareness.CommunicationAvailable,
            ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            civilizationB,
            $"{civilizationB}-{civilizationA}",
            civilizationA,
            tick,
            systemId,
            ContactAwareness.CommunicationAvailable,
            ContactCondition.Active,
            CommunicationAvailable: true,
            Confidence: 1.0));
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
