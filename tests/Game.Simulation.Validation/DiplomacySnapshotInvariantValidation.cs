using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Validation;

internal static class DiplomacySnapshotInvariantValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunDiplomacySnapshotInvariantChecks()
    {
        ValidateRepresentativeCurrentSnapshot();
        ValidateImpossibleCurrentSnapshotsAreRejected();
        ValidateTolerantRestoreCanNormalizeRecoverableLegacyNumbers();
        Console.WriteLine("PASS: strict Diplomacy snapshot persistence invariants");
    }

    private static void ValidateRepresentativeCurrentSnapshot()
    {
        var state = CreateRepresentativeState();
        var snapshot = state.Snapshot();
        var result = DiplomacySnapshotInvariantValidator.Validate(snapshot);

        Require(result.ContactCount == 2, "representative snapshot lost directional contacts");
        Require(result.RelationshipCount == 1, "representative snapshot lost its bilateral relationship");
        Require(result.AccessPermissionCount == 1, "representative snapshot lost directional access");
        Require(result.ClaimCount == 1 && result.ClaimResponseCount == 1,
            "representative snapshot lost claim/response state");
        Require(result.AgreementCount == 1, "representative snapshot lost its trade agreement");
        Require(result.ProposalCount == 2, "representative snapshot lost proposal state");
        Require(result.HistoryEventCount > 0, "representative snapshot has no meaningful history");

        var json = JsonSerializer.Serialize(snapshot);
        var roundTrip = JsonSerializer.Deserialize<DiplomacyStateSnapshot>(json)
            ?? throw new InvalidOperationException("Diplomacy snapshot JSON round trip returned null.");
        var roundTripResult = DiplomacySnapshotInvariantValidator.Validate(roundTrip);
        Require(roundTripResult == result, "strict validation counts changed across snapshot JSON round trip");
    }

    private static void ValidateImpossibleCurrentSnapshotsAreRejected()
    {
        var snapshot = CreateRepresentativeState().Snapshot();

        var invalidConfidence = snapshot with
        {
            Contacts = snapshot.Contacts
                .Select((contact, index) => index == 0 ? contact with { Confidence = 1.5 } : contact)
                .ToArray(),
        };
        ExpectInvalid(invalidConfidence, "out-of-range contact confidence was accepted");

        var pending = snapshot.Proposals.Single(proposal => proposal.Status == DiplomaticProposalStatus.Pending);
        var pendingWithResolution = snapshot with
        {
            Proposals = snapshot.Proposals
                .Select(proposal => proposal.ProposalId == pending.ProposalId
                    ? proposal with { ResolvedAtTick = proposal.CreatedAtTick + 1 }
                    : proposal)
                .ToArray(),
        };
        ExpectInvalid(pendingWithResolution, "pending proposal with a resolution tick was accepted");

        var activeAgreement = snapshot.Agreements.Single(agreement => agreement.Status == DiplomaticAgreementStatus.Active);
        var activeAgreementWithEnd = snapshot with
        {
            Agreements = snapshot.Agreements
                .Select(agreement => agreement.AgreementId == activeAgreement.AgreementId
                    ? agreement with { EndedAtTick = agreement.StartedAtTick + 1 }
                    : agreement)
                .ToArray(),
        };
        ExpectInvalid(activeAgreementWithEnd, "active agreement with an end tick was accepted");

        var responseToMissingClaim = snapshot with
        {
            ClaimResponses = snapshot.ClaimResponses
                .Append(new TerritorialClaimResponseSnapshot(
                    ClaimId: 999_999,
                    RespondingCivilizationId: 102,
                    Response: TerritorialClaimResponse.Disputed,
                    RespondedAtTick: 20))
                .ToArray(),
        };
        ExpectInvalid(responseToMissingClaim, "claim response to an unknown claim was accepted");

        var maxProposal = snapshot.Proposals.Max(proposal => proposal.ProposalId);
        ExpectInvalid(snapshot with { NextProposalId = maxProposal },
            "next proposal ID was allowed to collide with current state");

        var firstHistory = snapshot.RecentHistory[0];
        var missingPrimaryAudience = snapshot with
        {
            RecentHistory = snapshot.RecentHistory
                .Select(history => history.EventId == firstHistory.EventId
                    ? history with
                    {
                        KnownToCivilizationIds = history.KnownToCivilizationIds
                            .Where(id => id != history.PrimaryCivilizationId)
                            .ToArray(),
                    }
                    : history)
                .ToArray(),
        };
        ExpectInvalid(missingPrimaryAudience, "history event hidden from its own primary civilization was accepted");

        var orphanRelationship = snapshot with { Contacts = Array.Empty<DiplomaticContactSnapshot>() };
        ExpectInvalid(orphanRelationship, "relationship without any identified-contact basis was accepted");
    }

    private static void ValidateTolerantRestoreCanNormalizeRecoverableLegacyNumbers()
    {
        var snapshot = CreateRepresentativeState().Snapshot();
        var relationship = snapshot.Relationships.Single();
        var damaged = snapshot with
        {
            Contacts = snapshot.Contacts
                .Select((contact, index) => index == 0 ? contact with { Confidence = 4.0 } : contact)
                .ToArray(),
            Relationships = snapshot.Relationships
                .Select(candidate => candidate.CivilizationAId == relationship.CivilizationAId &&
                                     candidate.CivilizationBId == relationship.CivilizationBId
                    ? candidate with
                    {
                        Trust = -2.0,
                        Hostility = 4.0,
                        Grievances = candidate.Grievances
                            .Select((grievance, index) => index == 0 ? grievance with { Severity = 5.0 } : grievance)
                            .ToArray(),
                    }
                    : candidate)
                .ToArray(),
        };

        ExpectInvalid(damaged, "strict current-snapshot validation silently normalized damaged numeric state");

        var restored = DiplomacyState.Restore(damaged);
        var normalized = restored.Snapshot();
        DiplomacySnapshotInvariantValidator.Validate(normalized);

        Require(Math.Abs(normalized.Contacts[0].Confidence - 1.0) < 0.000001,
            "tolerant restore did not clamp damaged contact confidence");
        var normalizedRelationship = normalized.Relationships.Single();
        Require(Math.Abs(normalizedRelationship.Trust) < 0.000001 &&
                Math.Abs(normalizedRelationship.Hostility - 1.0) < 0.000001,
            "tolerant restore did not clamp damaged relationship dimensions");
        Require(normalizedRelationship.Grievances.Any(grievance => Math.Abs(grievance.Severity - 1.0) < 0.000001),
            "tolerant restore did not clamp damaged grievance severity");
    }

    private static DiplomacyState CreateRepresentativeState()
    {
        var state = new DiplomacyState();
        var diplomacy = new DiplomacySimulation(state);

        diplomacy.ProcessContactOpportunity(Contact(101, 102, tick: 1));
        diplomacy.ProcessContactOpportunity(Contact(102, 101, tick: 1));
        new DiplomaticCommunicationService(state).EstablishMutualCommunication(101, 102, tick: 2);

        diplomacy.ApplyRelationshipImpact(
            101,
            102,
            new RelationshipImpact(
                TrustDelta: 0.15,
                HostilityDelta: 0.2,
                FearDelta: 0.05,
                RespectDelta: 0.1,
                CooperationDelta: 0.05,
                GrievanceSeverity: 0.4,
                Reason: "Attributed border confrontation."),
            tick: 3);

        diplomacy.SetAccessPermission(101, 102, AccessPermission.Granted, tick: 4);

        var claimId = diplomacy.AssertTerritorialClaim(101, systemId: 7, tick: 5);
        diplomacy.CommunicateTerritorialClaim(claimId, recipient: 102, tick: 6);
        diplomacy.RespondToTerritorialClaim(claimId, responder: 102, TerritorialClaimResponse.Disputed, tick: 7);

        var trade = diplomacy.SendProposal(
            101,
            102,
            DiplomaticProposalKind.TradeOffer,
            tick: 8,
            summary: "Exchange refined materials for fuel reserves.",
            externalTermsReference: "validation:trade:101-102");
        diplomacy.RespondToProposal(trade, responder: 102, accept: true, tick: 9);

        diplomacy.SendProposal(
            101,
            102,
            DiplomaticProposalKind.Demand,
            tick: 10,
            summary: "Withdraw armed patrols from the disputed system.");

        return state;
    }

    private static FirstContactOpportunity Contact(int observer, int target, long tick) => new(
        ObserverCivilizationId: observer,
        ContactId: $"snapshot-contact-{target}",
        TargetCivilizationId: target,
        ObservedAtTick: tick,
        ObservedSystemId: 7,
        Awareness: ContactAwareness.ContactEstablished,
        Condition: ContactCondition.Active,
        CommunicationAvailable: false,
        Confidence: 0.9);

    private static void ExpectInvalid(DiplomacyStateSnapshot snapshot, string failureMessage)
    {
        var rejected = false;
        try
        {
            DiplomacySnapshotInvariantValidator.Validate(snapshot);
        }
        catch (DiplomacySnapshotValidationException)
        {
            rejected = true;
        }

        Require(rejected, failureMessage);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
