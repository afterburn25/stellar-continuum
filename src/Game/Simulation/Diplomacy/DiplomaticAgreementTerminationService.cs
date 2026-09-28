using System;
using System.Linq;

namespace Game.Simulation.Diplomacy;

public sealed record DiplomaticAgreementTerminationResult(
    long AgreementId,
    DiplomaticAgreementType AgreementType,
    int RequestedByCivilizationId,
    bool Terminated,
    bool ClearedMutualAccess,
    bool ResumedHostility);

/// <summary>
/// Authoritative participant-initiated agreement termination.
///
/// Diplomacy owns the political agreement lifecycle, but it does not invent economic or research
/// consequences behind external terms references. A participant may end an active agreement only
/// while two-way communication is available. Retrying an already-terminated agreement is
/// idempotent and does not append duplicate history.
/// </summary>
public sealed class DiplomaticAgreementTerminationService
{
    private readonly DiplomacyState _state;

    public DiplomaticAgreementTerminationService(DiplomacyState state)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));
    }

    public DiplomaticAgreementTerminationResult Terminate(
        long agreementId,
        int requesterCivilizationId,
        long tick,
        string reason)
    {
        if (agreementId <= 0)
            throw new ArgumentOutOfRangeException(nameof(agreementId));
        if (requesterCivilizationId < 0)
            throw new ArgumentOutOfRangeException(nameof(requesterCivilizationId));
        if (tick < 0)
            throw new ArgumentOutOfRangeException(nameof(tick));
        if (string.IsNullOrWhiteSpace(reason))
            throw new ArgumentException("Agreement termination requires a reason.", nameof(reason));

        var snapshot = _state.Snapshot().Agreements
            .SingleOrDefault(agreement => agreement.AgreementId == agreementId)
            ?? throw new InvalidOperationException("Unknown diplomatic agreement.");

        var requesterIsParticipant =
            requesterCivilizationId == snapshot.CivilizationAId ||
            requesterCivilizationId == snapshot.CivilizationBId;
        if (!requesterIsParticipant)
            throw new InvalidOperationException("Only an agreement participant may terminate it.");

        if (snapshot.Status == DiplomaticAgreementStatus.Terminated)
        {
            return new DiplomaticAgreementTerminationResult(
                agreementId,
                snapshot.Type,
                requesterCivilizationId,
                Terminated: false,
                ClearedMutualAccess: false,
                ResumedHostility: false);
        }

        if (tick < snapshot.StartedAtTick)
            throw new InvalidOperationException("Agreement termination cannot precede activation.");
        if (!_state.HasMutualCommunication(snapshot.CivilizationAId, snapshot.CivilizationBId))
            throw new InvalidOperationException("Agreement termination requires current two-way communication.");

        var pair = CivilizationPair.Create(snapshot.CivilizationAId, snapshot.CivilizationBId);
        var agreement = _state.ActiveAgreements(pair)
            .SingleOrDefault(candidate => candidate.Id == agreementId)
            ?? throw new InvalidOperationException("Active agreement state changed before termination.");

        agreement.Status = DiplomaticAgreementStatus.Terminated;
        agreement.Ended = tick;

        var otherCivilizationId = requesterCivilizationId == snapshot.CivilizationAId
            ? snapshot.CivilizationBId
            : snapshot.CivilizationAId;
        var clearedMutualAccess = false;
        var resumedHostility = false;

        if (snapshot.Type == DiplomaticAgreementType.Access)
        {
            _state.SetAccess(
                snapshot.CivilizationAId,
                snapshot.CivilizationBId,
                AccessPermission.Unspecified,
                tick);
            _state.SetAccess(
                snapshot.CivilizationBId,
                snapshot.CivilizationAId,
                AccessPermission.Unspecified,
                tick);
            clearedMutualAccess = true;

            _state.Record(
                tick,
                DiplomaticEventKind.AccessChanged,
                requesterCivilizationId,
                otherCivilizationId,
                null,
                $"Mutual access from agreement {agreementId} ended.",
                snapshot.CivilizationAId,
                snapshot.CivilizationBId);
        }

        if (snapshot.Type == DiplomaticAgreementType.Ceasefire)
        {
            var relationship = _state.Relationship(
                snapshot.CivilizationAId,
                snapshot.CivilizationBId);
            if (relationship.PoliticalState == DiplomaticPoliticalState.Ceasefire)
            {
                relationship.PoliticalState = DiplomaticPoliticalState.Hostile;
                resumedHostility = true;
                _state.Record(
                    tick,
                    DiplomaticEventKind.RelationshipChanged,
                    requesterCivilizationId,
                    otherCivilizationId,
                    null,
                    $"Ceasefire agreement {agreementId} ended; relations returned to Hostile.",
                    snapshot.CivilizationAId,
                    snapshot.CivilizationBId);
            }
        }

        _state.Record(
            tick,
            DiplomaticEventKind.AgreementTerminated,
            requesterCivilizationId,
            otherCivilizationId,
            null,
            $"Agreement {agreementId} ({snapshot.Type}) was terminated: {reason.Trim()}",
            snapshot.CivilizationAId,
            snapshot.CivilizationBId);

        return new DiplomaticAgreementTerminationResult(
            agreementId,
            snapshot.Type,
            requesterCivilizationId,
            Terminated: true,
            clearedMutualAccess,
            resumedHostility);
    }
}
