using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;

namespace Game.CoreRuntime.Validation;

internal static class ObserverDiplomacyClaimCommandValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunObserverDiplomacyClaimCommandChecks()
    {
        ValidateObserverSafeClaimCommunicationAndResponse();
        Console.WriteLine("PASS: observer-safe territorial claim communication and response");
    }

    private static void ValidateObserverSafeClaimCommunicationAndResponse()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);

        Require(typeof(ObserverDiplomacyCommandService)
                .GetMethods()
                .All(method => method.Name != nameof(DiplomacySimulation.AssertTerritorialClaim)),
            "state-only observer command gateway exposed territorial claim assertion without campaign knowledge validation");

        var ownClaimId = diplomacy.AssertTerritorialClaim(claimant: 1, systemId: 77, tick: 1);
        var hiddenThirdPartyClaimId = diplomacy.AssertTerritorialClaim(claimant: 3, systemId: 88, tick: 2);

        Require(runtime.BuildView(1).Claims.Any(claim => claim.ClaimId == ownClaimId),
            "claimant could not see its own territorial claim");
        Require(runtime.BuildView(2).Claims.All(claim => claim.ClaimId != ownClaimId),
            "uncommunicated claim leaked to another civilization");

        var hiddenClaimAttempt = runtime.Commands.CommunicateTerritorialClaim(
            observerCivilizationId: 1,
            claimId: hiddenThirdPartyClaimId,
            recipientCivilizationId: 2,
            tick: 3);
        var nonexistentClaimAttempt = runtime.Commands.CommunicateTerritorialClaim(
            observerCivilizationId: 1,
            claimId: 999_999,
            recipientCivilizationId: 2,
            tick: 3);
        Require(!hiddenClaimAttempt.Accepted && !nonexistentClaimAttempt.Accepted &&
                hiddenClaimAttempt.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistentClaimAttempt.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                hiddenClaimAttempt.Message == nonexistentClaimAttempt.Message,
            "hidden and nonexistent claim IDs produced distinguishable observer results");

        var noChannel = runtime.Commands.CommunicateTerritorialClaim(1, ownClaimId, 2, tick: 4);
        Require(!noChannel.Accepted && noChannel.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "claim communication succeeded without a visible active diplomatic channel");

        Observe(diplomacy, observer: 1, target: 2, tick: 5);
        Observe(diplomacy, observer: 2, target: 1, tick: 6);
        Require(runtime.Commands.EstablishCommunication(1, 2, tick: 7).Accepted,
            "claim validation could not establish mutual communication");

        var communicated = runtime.Commands.CommunicateTerritorialClaim(1, ownClaimId, 2, tick: 8);
        Require(communicated.Accepted,
            "claimant could not communicate its visible claim through the observer gateway");

        var recipientClaim = runtime.BuildView(2).Claims.SingleOrDefault(claim => claim.ClaimId == ownClaimId);
        Require(recipientClaim is not null && recipientClaim.ClaimantCivilizationId == 1 && recipientClaim.SystemId == 77,
            "claim recipient did not receive the communicated claim");
        Require(runtime.BuildView(3).Claims.All(claim => claim.ClaimId != ownClaimId),
            "communicated claim leaked to an uninvolved third party");

        var claimCommunicationEventsBeforeRetry = runtime.BuildView(1).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimCommunicated && evt.PrimaryCivilizationId == 1 && evt.SecondaryCivilizationId == 2);
        var communicateRetry = runtime.Commands.CommunicateTerritorialClaim(1, ownClaimId, 2, tick: 9);
        var claimCommunicationEventsAfterRetry = runtime.BuildView(1).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimCommunicated && evt.PrimaryCivilizationId == 1 && evt.SecondaryCivilizationId == 2);
        Require(communicateRetry.Accepted && claimCommunicationEventsAfterRetry == claimCommunicationEventsBeforeRetry,
            "idempotent claim communication retry created duplicate diplomatic history");

        var nonClaimantCommunication = runtime.Commands.CommunicateTerritorialClaim(2, ownClaimId, 1, tick: 10);
        Require(!nonClaimantCommunication.Accepted &&
                nonClaimantCommunication.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "non-claimant was allowed to retransmit another civilization's claim as its own");

        var recognized = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 2,
            claimId: ownClaimId,
            response: TerritorialClaimResponse.Recognized,
            tick: 11);
        Require(recognized.Accepted,
            "claim recipient could not recognize a visible claim");
        AssertVisibleResponse(runtime, ownClaimId, responder: 2, TerritorialClaimResponse.Recognized);
        Require(runtime.BuildView(3).ClaimResponses.All(response => response.ClaimId != ownClaimId),
            "claim response leaked to an uninvolved third party");

        var responseEventsBeforeRetry = runtime.BuildView(2).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimResponded && evt.PrimaryCivilizationId == 2 && evt.SecondaryCivilizationId == 1);
        var recognizeRetry = runtime.Commands.RespondToTerritorialClaim(2, ownClaimId, TerritorialClaimResponse.Recognized, tick: 12);
        var responseEventsAfterRetry = runtime.BuildView(2).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimResponded && evt.PrimaryCivilizationId == 2 && evt.SecondaryCivilizationId == 1);
        Require(recognizeRetry.Accepted && responseEventsAfterRetry == responseEventsBeforeRetry,
            "idempotent claim response retry created duplicate diplomatic history");

        var disputed = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 2,
            claimId: ownClaimId,
            response: TerritorialClaimResponse.Disputed,
            tick: 13);
        Require(disputed.Accepted,
            "claim recipient could not revise recognition into a dispute");
        AssertVisibleResponse(runtime, ownClaimId, responder: 2, TerritorialClaimResponse.Disputed);

        var claimantRespondingToOwnClaim = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 1,
            claimId: ownClaimId,
            response: TerritorialClaimResponse.Recognized,
            tick: 14);
        Require(!claimantRespondingToOwnClaim.Accepted &&
                claimantRespondingToOwnClaim.Status == ObserverDiplomacyCommandStatus.InvalidRequest,
            "claimant was allowed to respond to its own claim");

        diplomacy.MarkContactLost(observer: 2, contactId: "civilization:1", tick: 15);
        var staleChannelResponse = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 2,
            claimId: ownClaimId,
            response: TerritorialClaimResponse.Recognized,
            tick: 16);
        Require(!staleChannelResponse.Accepted &&
                staleChannelResponse.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "claim response succeeded through a stale/lost communication channel");
        AssertVisibleResponse(runtime, ownClaimId, responder: 2, TerritorialClaimResponse.Disputed);

        var hiddenResponse = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 4,
            claimId: ownClaimId,
            response: TerritorialClaimResponse.Disputed,
            tick: 17);
        var nonexistentResponse = runtime.Commands.RespondToTerritorialClaim(
            observerCivilizationId: 4,
            claimId: 999_999,
            response: TerritorialClaimResponse.Disputed,
            tick: 17);
        Require(!hiddenResponse.Accepted && !nonexistentResponse.Accepted &&
                hiddenResponse.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistentResponse.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                hiddenResponse.Message == nonexistentResponse.Message,
            "hidden and nonexistent claim response IDs produced distinguishable observer results");
    }

    private static void AssertVisibleResponse(
        DiplomacyCampaignRuntimeCoordinator runtime,
        long claimId,
        int responder,
        TerritorialClaimResponse expected)
    {
        var claimantResponse = runtime.BuildView(1).ClaimResponses.Single(response =>
            response.ClaimId == claimId && response.RespondingCivilizationId == responder);
        var responderResponse = runtime.BuildView(responder).ClaimResponses.Single(response =>
            response.ClaimId == claimId && response.RespondingCivilizationId == responder);
        Require(claimantResponse.Response == expected && responderResponse.Response == expected,
            "claim response was not consistently visible to claimant and responder");
    }

    private static void Observe(DiplomacySimulation diplomacy, int observer, int target, long tick)
    {
        diplomacy.ProcessContactOpportunity(new FirstContactOpportunity(
            ObserverCivilizationId: observer,
            ContactId: $"civilization:{target}",
            TargetCivilizationId: target,
            ObservedAtTick: tick,
            ObservedSystemId: target + 100,
            Awareness: ContactAwareness.ContactEstablished,
            Condition: ContactCondition.Active,
            CommunicationAvailable: false,
            Confidence: 0.95));
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
