using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class ObserverDiplomacyCommandValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunObserverDiplomacyCommandChecks()
    {
        ValidateVisibleProposalLifecycleAndAccess();
        ValidateHiddenProposalIdsDoNotBecomeAnOracle();
        ValidateUnavailableCounterpartsAreIndistinguishable();
        ValidateObserverScopedWithdrawal();
        Console.WriteLine("PASS: observer-safe Diplomacy command gateway");
    }

    private static void ValidateVisibleProposalLifecycleAndAccess()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);

        var first = new ObserverDiplomacyCommandService(state);
        var second = new ObserverDiplomacyCommandService(state);

        var sent = first.SendProposal(
            observerCivilizationId: 1,
            targetCivilizationId: 2,
            DiplomaticProposalKind.AccessRequest,
            tick: 10,
            summary: "Request transit access.");
        Require(sent.Accepted && sent.ProposalId is > 0, "visible communicated counterpart could not receive a proposal");

        var outgoingResponse = first.RespondToProposal(1, sent.ProposalId!.Value, accept: true, tick: 11);
        Require(!outgoingResponse.Accepted && outgoingResponse.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "proposal sender was allowed to respond to its own outgoing proposal through observer gateway");

        var accepted = second.RespondToProposal(2, sent.ProposalId.Value, accept: true, tick: 11);
        Require(accepted.Accepted, $"visible proposal recipient could not accept proposal: {accepted.Message}");
        Require(state.GetAccessPermission(2, 1) == AccessPermission.Granted,
            "accepted access request did not grant recipient-to-proposer transit access");

        var access = first.SetAccessPermission(1, 2, AccessPermission.Denied, tick: 12);
        Require(access.Accepted, "communicating observer could not set its own access permission");
        Require(state.GetAccessPermission(1, 2) == AccessPermission.Denied,
            "observer access command did not mutate authoritative Diplomacy state");
    }

    private static void ValidateHiddenProposalIdsDoNotBecomeAnOracle()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);
        EstablishCommunication(state, 3, 4, tick: 0, systemId: 20);

        var thirdParty = new ObserverDiplomacyCommandService(state).SendProposal(
            3,
            4,
            DiplomaticProposalKind.AccessRequest,
            tick: 30,
            summary: "Private third-party access request.");
        Require(thirdParty.Accepted && thirdParty.ProposalId is > 0,
            "validation setup could not create hidden third-party proposal");

        var observer = new ObserverDiplomacyCommandService(state);
        var hidden = observer.RespondToProposal(1, thirdParty.ProposalId!.Value, accept: false, tick: 31);
        var nonexistent = observer.RespondToProposal(1, 999_999, accept: false, tick: 31);

        Require(!hidden.Accepted && !nonexistent.Accepted,
            "hidden/nonexistent proposal probes were unexpectedly accepted");
        Require(hidden.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistent.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "hidden/nonexistent proposal probes returned distinguishable status classes");
        Require(hidden.Message == nonexistent.Message,
            "hidden third-party proposal ID could be distinguished from nonexistent ID by message text");

        var visible = observer.BuildView(1);
        Require(!visible.Proposals.Any(proposal => proposal.ProposalId == thirdParty.ProposalId.Value),
            "third-party proposal leaked into unrelated observer view");
    }

    private static void ValidateUnavailableCounterpartsAreIndistinguishable()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 1,
            ContactId: "identified-no-channel",
            TargetCivilizationId: 5,
            ObservedAtTick: 40,
            ObservedSystemId: 30,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.9));

        var observer = new ObserverDiplomacyCommandService(state);
        var identifiedButSilent = observer.SendProposal(
            1,
            5,
            DiplomaticProposalKind.Demand,
            tick: 41,
            summary: "Cannot be sent yet.");
        var completelyUnknown = observer.SendProposal(
            1,
            6,
            DiplomaticProposalKind.Demand,
            tick: 41,
            summary: "Cannot be sent yet either.");

        Require(!identifiedButSilent.Accepted && !completelyUnknown.Accepted,
            "unavailable counterpart proposal command was accepted");
        Require(identifiedButSilent.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable &&
                completelyUnknown.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "identified silent and unknown counterparts returned distinguishable status classes");
        Require(identifiedButSilent.Message == completelyUnknown.Message,
            "identified silent and unknown counterparts returned distinguishable error text");

        var accessUnknown = observer.SetAccessPermission(1, 6, AccessPermission.Denied, tick: 42);
        Require(!accessUnknown.Accepted && accessUnknown.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "access command exposed or mutated an unknown counterpart");
    }

    private static void ValidateObserverScopedWithdrawal()
    {
        var state = new DiplomacyState();
        EstablishCommunication(state, 1, 2, tick: 0, systemId: 10);

        var first = new ObserverDiplomacyCommandService(state);
        var second = new ObserverDiplomacyCommandService(state);
        var sent = first.SendProposal(
            1,
            2,
            DiplomaticProposalKind.Demand,
            tick: 50,
            summary: "Withdrawable demand.");
        Require(sent.Accepted && sent.ProposalId is > 0, "withdrawal setup proposal was not created");

        var recipientWithdrawal = second.WithdrawProposal(2, sent.ProposalId!.Value, tick: 51);
        Require(!recipientWithdrawal.Accepted && recipientWithdrawal.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "proposal recipient was allowed to withdraw the proposer's proposal");

        var proposerWithdrawal = first.WithdrawProposal(1, sent.ProposalId.Value, tick: 51);
        Require(proposerWithdrawal.Accepted, "proposal proposer could not withdraw its visible pending proposal");
        Require(
            state.Snapshot().Proposals.Single(proposal => proposal.ProposalId == sent.ProposalId.Value).Status == DiplomaticProposalStatus.Withdrawn,
            "withdraw command did not persist authoritative Withdrawn state");

        var secondWithdrawal = first.WithdrawProposal(1, sent.ProposalId.Value, tick: 52);
        Require(!secondWithdrawal.Accepted && secondWithdrawal.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "already-resolved proposal could be withdrawn again");
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
