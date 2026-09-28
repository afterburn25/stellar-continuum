using System;
using System.Linq;

namespace Game.Simulation.Diplomacy;

public sealed partial class ObserverDiplomacyCommandService
{
    /// <summary>
    /// Communicates one of the observer's already-visible active claims to a counterpart over a
    /// current diplomatic channel. Claim creation is deliberately not exposed here because this
    /// state-only gateway cannot prove that an arbitrary system ID is legitimately known.
    /// </summary>
    public ObserverDiplomacyCommandResult CommunicateTerritorialClaim(
        int observerCivilizationId,
        long claimId,
        int recipientCivilizationId,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            claimId <= 0 ||
            recipientCivilizationId < 0 ||
            recipientCivilizationId == observerCivilizationId)
        {
            return InvalidRequest();
        }

        var claim = BuildView(observerCivilizationId).Claims.FirstOrDefault(candidate =>
            candidate.ClaimId == claimId &&
            candidate.ClaimantCivilizationId == observerCivilizationId &&
            candidate.Active);
        if (claim is null)
            return ActionUnavailable();

        if (!HasVisibleActiveCommunication(observerCivilizationId, recipientCivilizationId))
            return ChannelUnavailable();

        // The claimant legitimately knows whom it has already informed. Avoid duplicate
        // ClaimCommunicated history on harmless UI/AI retries.
        if (claim.KnownToCivilizationIds.Contains(recipientCivilizationId))
        {
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Territorial claim was already communicated.");
        }

        try
        {
            _diplomacy.CommunicateTerritorialClaim(claimId, recipientCivilizationId, tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Territorial claim communicated.");
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

    /// <summary>
    /// Records recognition or dispute of a territorial claim already visible to the observer.
    /// The responder may revise its own prior response while communication remains active; the
    /// observer never receives hidden claimant-side state beyond what its own view already shows.
    /// </summary>
    public ObserverDiplomacyCommandResult RespondToTerritorialClaim(
        int observerCivilizationId,
        long claimId,
        TerritorialClaimResponse response,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            claimId <= 0 ||
            !Enum.IsDefined(response) ||
            response == TerritorialClaimResponse.None)
        {
            return InvalidRequest();
        }

        var view = BuildView(observerCivilizationId);
        var claim = view.Claims.FirstOrDefault(candidate =>
            candidate.ClaimId == claimId && candidate.Active);
        if (claim is null)
            return ActionUnavailable();
        if (claim.ClaimantCivilizationId == observerCivilizationId)
            return InvalidRequest();

        if (!HasVisibleActiveCommunication(observerCivilizationId, claim.ClaimantCivilizationId))
            return ChannelUnavailable();

        var prior = view.ClaimResponses.FirstOrDefault(candidate =>
            candidate.ClaimId == claimId &&
            candidate.RespondingCivilizationId == observerCivilizationId);
        if (prior is not null && prior.Response == response)
        {
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Territorial claim response was already recorded.");
        }

        try
        {
            _diplomacy.RespondToTerritorialClaim(
                claimId,
                observerCivilizationId,
                response,
                tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                response == TerritorialClaimResponse.Recognized
                    ? "Territorial claim recognized."
                    : "Territorial claim disputed.");
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
}
