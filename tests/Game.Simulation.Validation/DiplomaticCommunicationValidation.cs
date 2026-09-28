using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomaticCommunicationValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomaticCommunicationChecks()
    {
        ValidateMutualIdentificationRequiredBeforeNegotiation();
        Console.WriteLine("PASS: mutual diplomatic communication and access negotiation");
    }

    private static void ValidateMutualIdentificationRequiredBeforeNegotiation()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var commands = new ObserverDiplomacyCommandService(state);

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 11,
            ContactId: "civilization:22",
            TargetCivilizationId: 22,
            ObservedAtTick: 5,
            ObservedSystemId: 7,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.95));

        var blocked = commands.EstablishCommunication(11, 22, tick: 6);
        Require(!blocked.Accepted && blocked.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "one-way identification did not fail behind the generic observer-safe action boundary");
        Require(!state.BuildViewFor(11).Contacts.Single().CommunicationAvailable,
            "failed mutual-communication attempt partially mutated the informed side");
        Require(state.BuildViewFor(22).Contacts.Count == 0,
            "failed communication attempt revealed the informed civilization to an unaware target");

        var unawareAttempt = commands.EstablishCommunication(22, 11, tick: 6);
        Require(!unawareAttempt.Accepted && unawareAttempt.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "an observer with no visible contact received a non-channel-safe communication result");

        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: 22,
            ContactId: "civilization:11",
            TargetCivilizationId: 11,
            ObservedAtTick: 9,
            ObservedSystemId: 7,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.90));

        var established = commands.EstablishCommunication(11, 22, tick: 10);
        Require(established.Accepted && established.Status == ObserverDiplomacyCommandStatus.Accepted,
            "mutually identified civilizations could not establish communication through the observer gateway");

        var retry = commands.EstablishCommunication(11, 22, tick: 10);
        Require(retry.Accepted,
            "already-established communication was not idempotent at the observer gateway");

        var contact11 = state.BuildViewFor(11).Contacts.Single();
        var contact22 = state.BuildViewFor(22).Contacts.Single();
        Require(contact11.CommunicationAvailable && contact22.CommunicationAvailable,
            "mutually identified civilizations did not receive a two-way communication channel");
        Require(contact11.Awareness == ContactAwareness.CommunicationAvailable &&
                contact22.Awareness == ContactAwareness.CommunicationAvailable,
            "communication establishment did not advance both contact-awareness states");

        var proposal = commands.SendProposal(
            observerCivilizationId: 11,
            targetCivilizationId: 22,
            kind: DiplomaticProposalKind.AccessRequest,
            tick: 11,
            summary: "Request transit access through claimed space.");
        Require(proposal.Accepted && proposal.ProposalId.HasValue,
            "observer command gateway could not send an access request over the new channel");

        var response = commands.RespondToProposal(
            observerCivilizationId: 22,
            proposalId: proposal.ProposalId.Value,
            accept: true,
            tick: 12);
        Require(response.Accepted,
            "observer command gateway could not accept the incoming access request");

        Require(state.GetAccessPermission(grantor: 22, visitor: 11) == AccessPermission.Granted,
            "accepted access request did not create directional transit permission");
        Require(state.GetAccessPermission(grantor: 11, visitor: 22) != AccessPermission.Granted,
            "directional access request incorrectly granted reciprocal transit permission");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
