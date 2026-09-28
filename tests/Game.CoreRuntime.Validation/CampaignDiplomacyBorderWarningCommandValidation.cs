using System;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignDiplomacyBorderWarningCommandValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCampaignDiplomacyBorderWarningCommandChecks()
    {
        ValidateCampaignAwareBorderWarnings();
        Console.WriteLine("PASS: campaign-aware political border warnings");
    }

    private static void ValidateCampaignAwareBorderWarnings()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x424F_5244_4552_574EL,
            new GalaxyGenerationSettings
            {
                SystemCount = 52,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 680.0f,
            });

        var civilizations = galaxy.Civilizations
            .OrderBy(civilization => civilization.Id)
            .Take(3)
            .ToArray();
        Require(civilizations.Length == 3,
            "border warning validation requires three civilizations");

        var issuer = civilizations[0];
        var recipient = civilizations[1];
        var third = civilizations[2];
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);
        var runtime = new DiplomacyCampaignRuntimeCoordinator(state);
        var warnings = runtime.CreateBorderWarningCommands(galaxy);

        Observe(diplomacy, issuer.Id, recipient.Id, tick: 1);
        Observe(diplomacy, recipient.Id, issuer.Id, tick: 2);
        Require(runtime.Commands.EstablishCommunication(issuer.Id, recipient.Id, tick: 3).Accepted,
            "border warning validation could not establish mutual communication");

        var catalogOnlySystem = galaxy.Systems.FirstOrDefault(system =>
            !galaxy.Knowledge.IsSystemKnown(issuer.Id, system.Id));
        Require(catalogOnlySystem is not null,
            "validation galaxy did not provide a catalog-only system outside issuer knowledge");

        var unknownExisting = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 4);
        var nonexistent = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            int.MaxValue,
            tick: 4);
        Require(!unknownExisting.Accepted && !nonexistent.Accepted &&
                unknownExisting.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                nonexistent.Status == ObserverDiplomacyCommandStatus.ActionUnavailable &&
                unknownExisting.Message == nonexistent.Message,
            "unknown-existing and nonexistent border-warning system IDs were distinguishable");

        galaxy.Knowledge.RevealSystem(issuer.Id, catalogOnlySystem.Id);
        var knownWithoutTerritory = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 5);
        Require(!knownWithoutTerritory.Accepted &&
                knownWithoutTerritory.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "issuer warned over a known system without a territorial claim or colony");

        var asserted = runtime.CreateTerritorialClaimCommands(galaxy)
            .AssertTerritorialClaim(issuer.Id, catalogOnlySystem.Id, tick: 6);
        Require(asserted.Accepted && asserted.ClaimId.HasValue,
            "border warning validation could not establish territorial claim basis");

        var warning = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 7);
        Require(warning.Accepted,
            "issuer could not warn over a legitimately known claimed system");
        AssertWarningVisibility(runtime, issuer.Id, recipient.Id, third.Id, catalogOnlySystem.Id, tick: 7);

        var warningCountBeforeRetry = runtime.BuildView(issuer.Id).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.BorderWarningIssued &&
            evt.SecondaryCivilizationId == recipient.Id &&
            evt.SystemId == catalogOnlySystem.Id &&
            evt.Tick == 7);
        var retry = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 7);
        var warningCountAfterRetry = runtime.BuildView(issuer.Id).RecentEvents.Count(evt =>
            evt.Kind == DiplomaticEventKind.BorderWarningIssued &&
            evt.SecondaryCivilizationId == recipient.Id &&
            evt.SystemId == catalogOnlySystem.Id &&
            evt.Tick == 7);
        Require(retry.Accepted && warningCountAfterRetry == warningCountBeforeRetry,
            "same-tick border-warning retry duplicated persistent diplomatic history");

        Require(runtime.Commands.SetAccessPermission(
                issuer.Id,
                recipient.Id,
                AccessPermission.Granted,
                tick: 8).Accepted,
            "border warning validation could not grant transit access");
        var grantedAccessWarning = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 9);
        Require(!grantedAccessWarning.Accepted &&
                grantedAccessWarning.Status == ObserverDiplomacyCommandStatus.ActionUnavailable,
            "generic unauthorized-presence warning remained available while access was granted");
        Require(state.GetAccessPermission(issuer.Id, recipient.Id) == AccessPermission.Granted,
            "rejected border warning mutated the existing access grant");

        Require(runtime.Commands.SetAccessPermission(
                issuer.Id,
                recipient.Id,
                AccessPermission.Denied,
                tick: 10).Accepted,
            "border warning validation could not revoke transit access");
        var deniedAccessWarning = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            catalogOnlySystem.Id,
            tick: 11);
        Require(deniedAccessWarning.Accepted,
            "border warning was not available after access became denied");

        // An actual colony is also a territorial basis even without a separate claim record.
        var colonySystemId = issuer.HomeSystemId;
        Require(galaxy.Colonies.Any(colony =>
                colony.CivilizationId == issuer.Id && colony.SystemId == colonySystemId),
            "validation campaign did not seed an issuer home-system colony");
        Require(galaxy.Knowledge.IsSystemKnown(issuer.Id, colonySystemId),
            "issuer did not know its own colony system");
        Require(runtime.BuildView(issuer.Id).Claims.All(claim => claim.SystemId != colonySystemId),
            "colony-basis check unexpectedly already had a separate claim");
        var colonyBasisWarning = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            colonySystemId,
            tick: 12);
        Require(colonyBasisWarning.Accepted,
            "issuer could not warn over its own colony system without a redundant claim");

        diplomacy.MarkContactLost(issuer.Id, $"civilization:{recipient.Id}", tick: 13);
        var staleChannelWarning = warnings.IssueBorderWarning(
            issuer.Id,
            recipient.Id,
            colonySystemId,
            tick: 14);
        Require(!staleChannelWarning.Accepted &&
                staleChannelWarning.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "border warning succeeded through a stale/lost diplomatic channel");

        var noChannelThirdParty = warnings.IssueBorderWarning(
            issuer.Id,
            third.Id,
            colonySystemId,
            tick: 15);
        Require(!noChannelThirdParty.Accepted &&
                noChannelThirdParty.Status == ObserverDiplomacyCommandStatus.ChannelUnavailable,
            "border warning succeeded toward an unidentified/no-channel third party");
        Require(runtime.BuildView(third.Id).RecentEvents.All(evt =>
                evt.Kind != DiplomaticEventKind.BorderWarningIssued || evt.PrimaryCivilizationId != issuer.Id),
            "rejected border warning leaked issuer activity to third party");

        var invalidSelf = warnings.IssueBorderWarning(issuer.Id, issuer.Id, colonySystemId, tick: 16);
        var invalidSystem = warnings.IssueBorderWarning(issuer.Id, recipient.Id, -1, tick: 16);
        var invalidTick = warnings.IssueBorderWarning(issuer.Id, recipient.Id, colonySystemId, tick: -1);
        Require(invalidSelf.Status == ObserverDiplomacyCommandStatus.InvalidRequest &&
                invalidSystem.Status == ObserverDiplomacyCommandStatus.InvalidRequest &&
                invalidTick.Status == ObserverDiplomacyCommandStatus.InvalidRequest,
            "invalid border warning request did not fail through InvalidRequest");
    }

    private static void AssertWarningVisibility(
        DiplomacyCampaignRuntimeCoordinator runtime,
        int issuer,
        int recipient,
        int third,
        int systemId,
        long tick)
    {
        Require(runtime.BuildView(issuer).RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.BorderWarningIssued &&
                evt.SecondaryCivilizationId == recipient &&
                evt.SystemId == systemId &&
                evt.Tick == tick),
            "issuer did not retain its border-warning history");
        Require(runtime.BuildView(recipient).RecentEvents.Any(evt =>
                evt.Kind == DiplomaticEventKind.BorderWarningIssued &&
                evt.PrimaryCivilizationId == issuer &&
                evt.SystemId == systemId &&
                evt.Tick == tick),
            "recipient did not receive the border warning");
        Require(runtime.BuildView(third).RecentEvents.All(evt =>
                evt.Kind != DiplomaticEventKind.BorderWarningIssued ||
                evt.PrimaryCivilizationId != issuer ||
                evt.SystemId != systemId),
            "border warning leaked to uninvolved third party");
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
