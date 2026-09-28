using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Diplomacy;

/// <summary>
/// Presentation/AI guidance derived exclusively from one observer's already-filtered Diplomacy
/// view. A true value means the observer has enough visible-side state to attempt the action; it
/// never promises that hidden reciprocal prerequisites will succeed.
/// </summary>
public sealed record ObserverDiplomacyActionAvailability(
    int CounterpartCivilizationId,
    ContactAwareness ContactAwareness,
    ContactCondition ContactCondition,
    double ContactConfidence,
    long LastObservedTick,
    DiplomaticPoliticalState PoliticalState,
    bool CommunicationAvailable,
    bool CanAttemptCommunication,
    bool CanSendProposal,
    bool CanSetAccessPermission,
    bool CanDeclareWar,
    bool CanRespondToPendingProposal,
    bool CanWithdrawPendingProposal,
    bool CanTerminateActiveAgreement,
    int PendingIncomingProposalCount,
    int PendingOutgoingProposalCount,
    int ActiveAgreementCount);

public static class ObserverDiplomacyActionAvailabilityBuilder
{
    public static IReadOnlyList<ObserverDiplomacyActionAvailability> Build(DiplomaticStateView view)
    {
        ArgumentNullException.ThrowIfNull(view);

        var latestIdentifiedContacts = view.Contacts
            .Where(contact =>
                contact.TargetCivilizationId.HasValue &&
                contact.Awareness >= ContactAwareness.Identified)
            .GroupBy(contact => contact.TargetCivilizationId!.Value)
            .Select(group => group
                .OrderByDescending(contact => contact.LastObservedTick)
                .ThenBy(contact => contact.ContactId, StringComparer.Ordinal)
                .First())
            .OrderBy(contact => contact.TargetCivilizationId!.Value)
            .ToArray();

        var result = new List<ObserverDiplomacyActionAvailability>(latestIdentifiedContacts.Length);
        foreach (var contact in latestIdentifiedContacts)
        {
            var counterpart = contact.TargetCivilizationId!.Value;
            var relationship = view.Relationships.FirstOrDefault(candidate =>
                candidate.OtherCivilizationId == counterpart);
            var politicalState = relationship?.PoliticalState ?? DiplomaticPoliticalState.Unknown;
            var activeCommunication = contact.CommunicationAvailable &&
                                      contact.Condition != ContactCondition.StaleOrLost;

            var pendingIncoming = view.Proposals.Count(proposal =>
                proposal.ProposerCivilizationId == counterpart &&
                proposal.RecipientCivilizationId == view.ObserverCivilizationId &&
                proposal.Status == DiplomaticProposalStatus.Pending);
            var pendingOutgoing = view.Proposals.Count(proposal =>
                proposal.ProposerCivilizationId == view.ObserverCivilizationId &&
                proposal.RecipientCivilizationId == counterpart &&
                proposal.Status == DiplomaticProposalStatus.Pending);
            var activeAgreements = view.Agreements.Count(agreement =>
                agreement.Status == DiplomaticAgreementStatus.Active &&
                (agreement.CivilizationAId == counterpart || agreement.CivilizationBId == counterpart));

            result.Add(new ObserverDiplomacyActionAvailability(
                counterpart,
                contact.Awareness,
                contact.Condition,
                contact.Confidence,
                contact.LastObservedTick,
                politicalState,
                activeCommunication,
                CanAttemptCommunication: !activeCommunication &&
                    contact.Condition != ContactCondition.StaleOrLost,
                CanSendProposal: activeCommunication,
                CanSetAccessPermission: activeCommunication,
                CanDeclareWar: politicalState != DiplomaticPoliticalState.AtWar,
                CanRespondToPendingProposal: pendingIncoming > 0,
                CanWithdrawPendingProposal: pendingOutgoing > 0,
                CanTerminateActiveAgreement: activeCommunication && activeAgreements > 0,
                PendingIncomingProposalCount: pendingIncoming,
                PendingOutgoingProposalCount: pendingOutgoing,
                ActiveAgreementCount: activeAgreements));
        }

        return result;
    }
}

public static class DiplomacyCampaignRuntimeActionAvailabilityExtensions
{
    public static IReadOnlyList<ObserverDiplomacyActionAvailability> BuildActionAvailability(
        this DiplomacyCampaignRuntimeCoordinator runtime,
        int observerCivilizationId)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        return ObserverDiplomacyActionAvailabilityBuilder.Build(runtime.BuildView(observerCivilizationId));
    }
}
