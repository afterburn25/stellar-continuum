using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Diplomacy;

/// <summary>
/// Campaign-aware command boundary for political border warnings. It validates system knowledge
/// and an issuer-side territorial basis before delegating to Diplomacy's existing warning event.
/// This does not block movement or create a physical border.
/// </summary>
public sealed class CampaignDiplomacyBorderWarningCommandService
{
    private const string ChannelUnavailableMessage = "No active diplomatic channel is available to that counterpart.";
    private const string ActionUnavailableMessage = "That border warning action is not currently available.";
    private const string InvalidRequestMessage = "The border warning request is not valid.";

    private readonly GalaxyState _galaxy;
    private readonly DiplomacyCampaignRuntimeCoordinator _runtime;
    private readonly DiplomacySimulation _diplomacy;

    public CampaignDiplomacyBorderWarningCommandService(
        GalaxyState galaxy,
        DiplomacyCampaignRuntimeCoordinator runtime)
    {
        _galaxy = galaxy ?? throw new ArgumentNullException(nameof(galaxy));
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _diplomacy = new DiplomacySimulation(_runtime.State);
    }

    public ObserverDiplomacyCommandResult IssueBorderWarning(
        int issuerCivilizationId,
        int recipientCivilizationId,
        int systemId,
        long tick)
    {
        if (issuerCivilizationId < 0 || recipientCivilizationId < 0 || systemId < 0 || tick < 0 ||
            issuerCivilizationId == recipientCivilizationId ||
            !_galaxy.Civilizations.Any(civilization => civilization.Id == issuerCivilizationId))
        {
            return InvalidRequest();
        }

        var issuerView = _runtime.BuildView(issuerCivilizationId);
        var activeCommunication = issuerView.Contacts.Any(contact =>
            contact.TargetCivilizationId == recipientCivilizationId &&
            contact.CommunicationAvailable &&
            contact.Condition != ContactCondition.StaleOrLost);
        if (!activeCommunication)
            return ChannelUnavailable();

        // Do not turn the command into a system-ID oracle. A real system that the issuer has not
        // legitimately revealed and an arbitrary nonexistent ID share one unavailable result.
        if (!_galaxy.Systems.Any(system => system.Id == systemId) ||
            !_galaxy.Knowledge.IsSystemKnown(issuerCivilizationId, systemId))
        {
            return ActionUnavailable();
        }

        var hasActiveClaim = issuerView.Claims.Any(claim =>
            claim.Active &&
            claim.ClaimantCivilizationId == issuerCivilizationId &&
            claim.SystemId == systemId);
        var hasColony = _galaxy.Colonies.Any(colony =>
            colony.CivilizationId == issuerCivilizationId &&
            colony.SystemId == systemId);
        if (!hasActiveClaim && !hasColony)
            return ActionUnavailable();

        // In the current early-release model, a generic border warning means presence is not
        // authorized under ordinary access. Explicit granted access therefore suppresses it.
        if (_runtime.State.GetAccessPermission(issuerCivilizationId, recipientCivilizationId) == AccessPermission.Granted)
            return ActionUnavailable();

        // Harmless same-tick UI retries should not duplicate persistent diplomatic history.
        var alreadyRecordedThisTick = issuerView.RecentEvents.Any(evt =>
            evt.Kind == DiplomaticEventKind.BorderWarningIssued &&
            evt.PrimaryCivilizationId == issuerCivilizationId &&
            evt.SecondaryCivilizationId == recipientCivilizationId &&
            evt.SystemId == systemId &&
            evt.Tick == tick);
        if (alreadyRecordedThisTick)
        {
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Border warning was already issued.");
        }

        try
        {
            _diplomacy.IssueBorderWarning(
                issuerCivilizationId,
                recipientCivilizationId,
                systemId,
                tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Border warning issued.");
        }
        catch (ArgumentException)
        {
            return InvalidRequest();
        }
        catch (InvalidOperationException)
        {
            return ActionUnavailable();
        }
    }

    private static ObserverDiplomacyCommandResult ChannelUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ChannelUnavailable,
        ChannelUnavailableMessage);

    private static ObserverDiplomacyCommandResult ActionUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ActionUnavailable,
        ActionUnavailableMessage);

    private static ObserverDiplomacyCommandResult InvalidRequest() => new(
        false,
        ObserverDiplomacyCommandStatus.InvalidRequest,
        InvalidRequestMessage);
}

public static class CampaignDiplomacyBorderWarningCommandExtensions
{
    public static CampaignDiplomacyBorderWarningCommandService CreateBorderWarningCommands(
        this DiplomacyCampaignRuntimeCoordinator runtime,
        GalaxyState galaxy)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        ArgumentNullException.ThrowIfNull(galaxy);
        return new CampaignDiplomacyBorderWarningCommandService(galaxy, runtime);
    }
}
