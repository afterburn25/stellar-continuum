using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignDiplomacyTerritorialClaimCommandValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCampaignDiplomacyTerritorialClaimCommandChecks()
    {
        ValidateKnowledgeAwareClaimAssertion();
        Console.WriteLine("PASS: campaign-aware territorial claim assertion");
    }

    private static void ValidateKnowledgeAwareClaimAssertion()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434C_4149_4D4B_4E4FL,
            new GalaxyGenerationSettings
            {
                SystemCount = 48,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 620.0f,
            });

        var civilizations = galaxy.Civilizations
            .OrderBy(civilization => civilization.Id)
            .Take(3)
            .ToArray();
        Require(civilizations.Length == 3,
            "territorial claim validation requires three civilizations");

        var first = civilizations[0];
        var second = civilizations[1];
        var third = civilizations[2];
        var state = new DiplomacyState();
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        var claimCommands = runtime.CreateTerritorialClaimCommands(galaxy);

        var catalogOnlySystem = galaxy.Systems.FirstOrDefault(system =>
            !galaxy.Knowledge.IsSystemKnown(first.Id, system.Id));
        Require(catalogOnlySystem is not null,
            "validation galaxy did not provide a catalog-only system outside first-civilization knowledge");

        var unknownExisting = claimCommands.AssertTerritorialClaim(
            first.Id,
            catalogOnlySystem.Id,
            tick: 1);
        var nonexistent = claimCommands.AssertTerritorialClaim(
            first.Id,
            int.MaxValue,
            tick: 1);
        Require(!unknownExisting.Accepted && !nonexistent.Accepted &&
                unknownExisting.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistent.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                unknownExisting.Message == nonexistent.Message,
            "catalog-only and nonexistent system IDs produced distinguishable claim assertion results");
        Require(runtime.BuildView(first.Id).Claims.Count == 0,
            "rejected unknown-system claim assertion created political state");

        var targetSystem = galaxy.Systems.FirstOrDefault(system =>
            !galaxy.Knowledge.IsSystemKnown(second.Id, system.Id) &&
            !galaxy.Knowledge.IsSystemKnown(third.Id, system.Id));
        Require(targetSystem is not null,
            "validation galaxy did not provide a system hidden from second and third civilizations");

        galaxy.Knowledge.RevealSystem(first.Id, targetSystem.Id);
        Require(galaxy.Knowledge.IsSystemKnown(first.Id, targetSystem.Id),
            "test setup failed to reveal claim target to first civilization");

        var asserted = claimCommands.AssertTerritorialClaim(first.Id, targetSystem.Id, tick: 2);
        Require(asserted.Accepted && asserted.ClaimId.HasValue,
            "legitimately known system could not be claimed through campaign-aware boundary");
        var claimId = asserted.ClaimId.Value;
        var ownClaim = runtime.BuildView(first.Id).Claims.Single(claim => claim.ClaimId == claimId);
        Require(ownClaim.SystemId == targetSystem.Id && ownClaim.ClaimantCivilizationId == first.Id,
            "accepted campaign claim did not create the expected authoritative claim");
        Require(runtime.BuildView(second.Id).Claims.All(claim => claim.ClaimId != claimId) &&
                runtime.BuildView(third.Id).Claims.All(claim => claim.ClaimId != claimId),
            "new territorial claim leaked to civilizations that were never informed");

        var claimEventsBeforeRetry = runtime.BuildView(first.Id).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimAsserted && evt.SystemId == targetSystem.Id);
        var retry = claimCommands.AssertTerritorialClaim(first.Id, targetSystem.Id, tick: 3);
        var claimEventsAfterRetry = runtime.BuildView(first.Id).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.ClaimAsserted && evt.SystemId == targetSystem.Id);
        Require(retry.Accepted && retry.ClaimId == claimId &&
                claimEventsAfterRetry == claimEventsBeforeRetry,
            "idempotent campaign claim retry changed identity or duplicated ClaimAsserted history");

        // Independent astronomical/system knowledge is not political claim knowledge.
        galaxy.Knowledge.RevealSystem(second.Id, targetSystem.Id);
        Require(galaxy.Knowledge.IsSystemKnown(second.Id, targetSystem.Id),
            "test setup failed to reveal the same system independently to second civilization");
        Require(runtime.BuildView(second.Id).Claims.All(claim => claim.ClaimId != claimId),
            "independent system discovery leaked another civilization's territorial claim");

        var thirdUnknown = runtime.CreateTerritorialClaimCommands(galaxy)
            .AssertTerritorialClaim(third.Id, targetSystem.Id, tick: 4);
        Require(!thirdUnknown.Accepted &&
                thirdUnknown.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "civilization asserted a claim on a system absent from its own knowledge");

        Observe(new DiplomacySimulation(state), first.Id, second.Id, tick: 5);
        Observe(new DiplomacySimulation(state), second.Id, first.Id, tick: 6);
        Require(runtime.Commands.EstablishCommunication(first.Id, second.Id, tick: 7).Accepted,
            "claim assertion validation could not establish communication for claim publication");
        Require(runtime.Commands.CommunicateTerritorialClaim(first.Id, claimId, second.Id, tick: 8).Accepted,
            "campaign-created claim could not flow into observer-safe claim communication");
        Require(runtime.BuildView(second.Id).Claims.Any(claim => claim.ClaimId == claimId),
            "communicated campaign-created claim did not become visible to recipient");
        Require(runtime.Commands.RespondToTerritorialClaim(
                second.Id,
                claimId,
                TerritorialClaimResponse.Disputed,
                tick: 9).Accepted,
            "recipient could not dispute campaign-created claim through observer gateway");
        Require(runtime.BuildView(first.Id).ClaimResponses.Any(response =>
                response.ClaimId == claimId &&
                response.RespondingCivilizationId == second.Id &&
                response.Response == TerritorialClaimResponse.Disputed),
            "claimant did not receive visible dispute response after campaign-aware assertion");

        var invalidObserver = claimCommands.AssertTerritorialClaim(-1, targetSystem.Id, tick: 10);
        var invalidSystem = claimCommands.AssertTerritorialClaim(first.Id, -1, tick: 10);
        var invalidTick = claimCommands.AssertTerritorialClaim(first.Id, targetSystem.Id, tick: -1);
        Require(invalidObserver.Status == ObserverDiplomacyCommandStatus.InvalidRequest &&
                invalidSystem.Status == ObserverDiplomacyCommandStatus.InvalidRequest &&
                invalidTick.Status == ObserverDiplomacyCommandStatus.InvalidRequest,
            "invalid campaign territorial claim request did not fail through InvalidRequest");
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
