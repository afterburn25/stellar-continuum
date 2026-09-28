using System;
using System.Linq;

namespace Game.Simulation.Diplomacy;

public enum ObserverDiplomacyCommandStatus
{
    Accepted,
    ChannelUnavailable,
    ActionUnavailable,
    InvalidRequest,
}

public sealed record ObserverDiplomacyCommandResult(
    bool Accepted,
    ObserverDiplomacyCommandStatus Status,
    string Message,
    long? ProposalId = null,
    long? AgreementId = null);

/// <summary>
/// Observer-scoped command boundary for player/UI/AI consumers. Callers may act only on
/// counterpart, proposal and agreement identities already visible in their own Diplomacy view.
/// Hidden third-party IDs and nonexistent IDs intentionally produce the same safe rejection.
/// The gateway delegates all authoritative mutation and deeper bilateral validation to
/// Diplomacy-owned services.
/// </summary>
public sealed partial class ObserverDiplomacyCommandService
{
    private const string ChannelUnavailableMessage = "No active diplomatic channel is available to that counterpart.";
    private const string ContactUnavailableMessage = "No usable diplomatic contact is available to that counterpart.";
    private const string ActionUnavailableMessage = "That diplomatic action is not currently available.";
    private const string InvalidRequestMessage = "The diplomatic request is not valid.";

    private readonly DiplomacyState _state;
    private readonly DiplomacySimulation _diplomacy;

    public ObserverDiplomacyCommandService(DiplomacyState state)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));
        _diplomacy = new DiplomacySimulation(_state);
    }

    public DiplomaticStateView BuildView(int observerCivilizationId)
    {
        if (observerCivilizationId < 0)
            throw new ArgumentOutOfRangeException(nameof(observerCivilizationId));
        return _state.BuildViewFor(observerCivilizationId);
    }

    /// <summary>
    /// Attempts to promote a visible, current identified contact into the existing authoritative
    /// mutual communication state. The caller can prove only its own contact eligibility here;
    /// whether the counterpart has reciprocally identified the caller remains hidden behind the
    /// generic ActionUnavailable result unless communication is successfully established.
    /// </summary>
    public ObserverDiplomacyCommandResult EstablishCommunication(
        int observerCivilizationId,
        int targetCivilizationId,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            targetCivilizationId < 0 ||
            targetCivilizationId == observerCivilizationId)
        {
            return InvalidRequest();
        }

        var contact = BuildView(observerCivilizationId).Contacts
            .Where(candidate => candidate.TargetCivilizationId == targetCivilizationId)
            .OrderByDescending(candidate => candidate.LastObservedTick)
            .FirstOrDefault();
        if (contact is null ||
            contact.Awareness < ContactAwareness.Identified ||
            contact.Condition == ContactCondition.StaleOrLost)
        {
            return ContactUnavailable();
        }

        if (contact.CommunicationAvailable)
        {
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Communication channel is already available.");
        }

        try
        {
            new DiplomaticCommunicationService(_state).EstablishMutualCommunication(
                observerCivilizationId,
                targetCivilizationId,
                tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Communication channel established.");
        }
        catch (ArgumentException)
        {
            return InvalidRequest();
        }
        catch (InvalidOperationException)
        {
            // A failed attempt must not reveal whether the counterpart lacks reciprocal contact,
            // has stale contact, or otherwise fails a hidden bilateral prerequisite.
            return ActionUnavailable();
        }
    }

    public ObserverDiplomacyCommandResult SendProposal(
        int observerCivilizationId,
        int targetCivilizationId,
        DiplomaticProposalKind kind,
        long tick,
        string summary,
        DiplomaticAgreementType? agreementType = null,
        string? externalTermsReference = null)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            targetCivilizationId < 0 ||
            targetCivilizationId == observerCivilizationId ||
            string.IsNullOrWhiteSpace(summary) ||
            !Enum.IsDefined(kind))
        {
            return InvalidRequest();
        }

        if (!HasVisibleActiveCommunication(observerCivilizationId, targetCivilizationId))
            return ChannelUnavailable();

        try
        {
            var proposalId = _diplomacy.SendProposal(
                observerCivilizationId,
                targetCivilizationId,
                kind,
                tick,
                summary,
                agreementType,
                externalTermsReference);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Proposal sent.",
                ProposalId: proposalId);
        }
        catch (ArgumentException)
        {
            return InvalidRequest();
        }
        catch (InvalidOperationException)
        {
            // Do not surface whether the opposite side lacks reciprocal communication, whether
            // a hidden state transition happened, or whether a bounded queue rejected the act.
            return ActionUnavailable();
        }
    }

    public ObserverDiplomacyCommandResult RespondToProposal(
        int observerCivilizationId,
        long proposalId,
        bool accept,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) || proposalId <= 0)
            return InvalidRequest();

        var proposal = BuildView(observerCivilizationId).Proposals.FirstOrDefault(candidate =>
            candidate.ProposalId == proposalId &&
            candidate.RecipientCivilizationId == observerCivilizationId &&
            candidate.Status == DiplomaticProposalStatus.Pending);
        if (proposal is null)
            return ActionUnavailable();

        try
        {
            _diplomacy.RespondToProposal(proposalId, observerCivilizationId, accept, tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                accept ? "Proposal accepted." : "Proposal rejected.",
                ProposalId: proposalId);
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

    public ObserverDiplomacyCommandResult WithdrawProposal(
        int observerCivilizationId,
        long proposalId,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) || proposalId <= 0)
            return InvalidRequest();

        var proposal = BuildView(observerCivilizationId).Proposals.FirstOrDefault(candidate =>
            candidate.ProposalId == proposalId &&
            candidate.ProposerCivilizationId == observerCivilizationId &&
            candidate.Status == DiplomaticProposalStatus.Pending);
        if (proposal is null)
            return ActionUnavailable();

        try
        {
            _diplomacy.WithdrawProposal(proposalId, observerCivilizationId, tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Proposal withdrawn.",
                ProposalId: proposalId);
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

    public ObserverDiplomacyCommandResult SetAccessPermission(
        int observerCivilizationId,
        int targetCivilizationId,
        AccessPermission permission,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            targetCivilizationId < 0 ||
            targetCivilizationId == observerCivilizationId ||
            !Enum.IsDefined(permission))
        {
            return InvalidRequest();
        }

        if (!HasVisibleActiveCommunication(observerCivilizationId, targetCivilizationId))
            return ChannelUnavailable();

        try
        {
            _diplomacy.SetAccessPermission(observerCivilizationId, targetCivilizationId, permission, tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Access permission updated.");
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
    /// Formally declares war on a civilization identity already visible to the caller. War does
    /// not require an active communication channel: stale contact may still leave durable identity
    /// knowledge. Whether the target independently knows the declarer remains governed by the
    /// authoritative DeclareWar event audience and is never inferred here.
    /// </summary>
    public ObserverDiplomacyCommandResult DeclareWar(
        int observerCivilizationId,
        int targetCivilizationId,
        long tick)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            targetCivilizationId < 0 ||
            targetCivilizationId == observerCivilizationId)
        {
            return InvalidRequest();
        }

        var visibleTarget = BuildView(observerCivilizationId).Contacts.Any(contact =>
            contact.TargetCivilizationId == targetCivilizationId &&
            contact.Awareness >= ContactAwareness.Identified);
        if (!visibleTarget)
            return ActionUnavailable();

        try
        {
            _diplomacy.DeclareWar(observerCivilizationId, targetCivilizationId, tick);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "War declared.");
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

    public ObserverDiplomacyCommandResult TerminateAgreement(
        int observerCivilizationId,
        long agreementId,
        long tick,
        string reason)
    {
        if (!ValidActorAndTick(observerCivilizationId, tick) ||
            agreementId <= 0 ||
            string.IsNullOrWhiteSpace(reason))
        {
            return InvalidRequest();
        }

        var agreement = BuildView(observerCivilizationId).Agreements
            .FirstOrDefault(candidate => candidate.AgreementId == agreementId);
        if (agreement is null)
            return ActionUnavailable();

        // The authoritative termination service is idempotent. Preserve that useful retry
        // behavior at the observer boundary without requiring a channel that may have gone stale
        // after the original successful termination.
        if (agreement.Status == DiplomaticAgreementStatus.Terminated)
        {
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Agreement was already terminated.",
                AgreementId: agreementId);
        }

        var counterpart = agreement.CivilizationAId == observerCivilizationId
            ? agreement.CivilizationBId
            : agreement.CivilizationAId;
        if (!HasVisibleActiveCommunication(observerCivilizationId, counterpart))
            return ChannelUnavailable();

        try
        {
            var result = new DiplomaticAgreementTerminationService(_state).Terminate(
                agreementId,
                observerCivilizationId,
                tick,
                reason);
            return new ObserverDiplomacyCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                result.Terminated ? "Agreement terminated." : "Agreement was already terminated.",
                AgreementId: agreementId);
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

    private bool HasVisibleActiveCommunication(int observer, int target) =>
        BuildView(observer).Contacts.Any(contact =>
            contact.TargetCivilizationId == target &&
            contact.CommunicationAvailable &&
            contact.Condition != ContactCondition.StaleOrLost);

    private static bool ValidActorAndTick(int observerCivilizationId, long tick) =>
        observerCivilizationId >= 0 && tick >= 0;

    private static ObserverDiplomacyCommandResult ChannelUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ChannelUnavailable,
        ChannelUnavailableMessage);

    private static ObserverDiplomacyCommandResult ContactUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ChannelUnavailable,
        ContactUnavailableMessage);

    private static ObserverDiplomacyCommandResult ActionUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ActionUnavailable,
        ActionUnavailableMessage);

    private static ObserverDiplomacyCommandResult InvalidRequest() => new(
        false,
        ObserverDiplomacyCommandStatus.InvalidRequest,
        InvalidRequestMessage);
}
