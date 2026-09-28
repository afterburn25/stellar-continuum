using System;
using System.Linq;
using System.Text;
using Game.Simulation.Diplomacy;

namespace Game.Presentation;

public sealed record RelationsPresentationState(
    int ContactIndex,
    int ContactCount,
    int ProposalIndex,
    int ProposalCount,
    string Details,
    int? TargetCivilizationId,
    bool HasVisibleCommunication,
    long? PendingProposalId,
    bool CanAcceptProposal,
    bool CanRejectProposal,
    bool CanWithdrawProposal,
    bool CanOfferNonAggression,
    bool CanRequestAccess,
    bool CanOfferPeace,
    bool CanOfferCeasefire,
    bool CanSetAccess,
    bool CanDeclareWar = false)
{
    public string ContactName { get; init; } = "NO FOREIGN CONTACTS";
    public string ContactStatus { get; init; } = "Awaiting first contact";
    public string CommunicationStatus { get; init; } = "Unavailable";
    public string PoliticalStatus { get; init; } = "No formal relationship";
    public double? Trust { get; init; }
    public double? Hostility { get; init; }
    public double? Fear { get; init; }
    public double? Respect { get; init; }
    public double? Cooperation { get; init; }
    public string AccessSummary { get; init; } = "No transit permissions";
    public string AgreementsSummary { get; init; } = "No active agreements";
    public string ProposalSummary { get; init; } = "No pending proposals";
    public string[] RecentEvents { get; init; } = Array.Empty<string>();
    public string? SpeciesId { get; init; }
}

/// <summary>
/// Plain-C# presentation shaping over an already observer-filtered Diplomacy view.
/// It never receives authoritative DiplomacyState and therefore cannot reveal contacts,
/// relationships, proposals, agreements, claims, or history hidden from the observer.
/// </summary>
public sealed class DiplomacyRelationsPresenter
{
    public RelationsPresentationState Build(
        DiplomaticStateView view,
        int requestedContactIndex,
        int requestedProposalIndex,
        Func<int, string> identifiedCivilizationName)
    {
        ArgumentNullException.ThrowIfNull(view);
        ArgumentNullException.ThrowIfNull(identifiedCivilizationName);

        if (view.Contacts.Count == 0)
        {
            return new RelationsPresentationState(
                0,
                0,
                0,
                0,
                "No foreign contacts are currently known. Sensor detection does not automatically identify a civilization or establish diplomacy.",
                null,
                false,
                null,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
                false)
            {
                ContactStatus = "Explore and identify another civilization to open diplomatic channels.",
            };
        }

        var contactIndex = Math.Clamp(requestedContactIndex, 0, view.Contacts.Count - 1);
        var contact = view.Contacts[contactIndex];
        var targetId = contact.TargetCivilizationId;

        var relationship = targetId is int identifiedTarget
            ? view.Relationships.FirstOrDefault(candidate => candidate.OtherCivilizationId == identifiedTarget)
            : null;

        var agreements = targetId is int agreementTarget
            ? view.Agreements
                .Where(agreement => PairMatches(agreement.CivilizationAId, agreement.CivilizationBId, view.ObserverCivilizationId, agreementTarget))
                .OrderBy(agreement => agreement.AgreementId)
                .ToArray()
            : Array.Empty<DiplomaticAgreementSnapshot>();

        var pendingProposals = targetId is int proposalTarget
            ? view.Proposals
                .Where(proposal => proposal.Status == DiplomaticProposalStatus.Pending)
                .Where(proposal => PairMatches(proposal.ProposerCivilizationId, proposal.RecipientCivilizationId, view.ObserverCivilizationId, proposalTarget))
                .OrderBy(proposal => proposal.ProposalId)
                .ToArray()
            : Array.Empty<DiplomaticProposalSnapshot>();

        var proposalIndex = pendingProposals.Length == 0
            ? 0
            : Math.Clamp(requestedProposalIndex, 0, pendingProposals.Length - 1);
        var selectedProposal = pendingProposals.Length == 0 ? null : pendingProposals[proposalIndex];

        var outboundAccess = targetId is int outboundTarget
            ? LatestAccess(view, view.ObserverCivilizationId, outboundTarget)
            : AccessPermission.Unspecified;
        var inboundAccess = targetId is int inboundTarget
            ? LatestAccess(view, inboundTarget, view.ObserverCivilizationId)
            : AccessPermission.Unspecified;

        var activeCommunication = contact.CommunicationAvailable && contact.Condition != ContactCondition.StaleOrLost;
        var politicalState = relationship?.PoliticalState ?? DiplomaticPoliticalState.Unknown;
        var hasActiveNonAggression = agreements.Any(agreement =>
            agreement.Type == DiplomaticAgreementType.NonAggression &&
            agreement.Status == DiplomaticAgreementStatus.Active);
        var canDeclareWar = targetId is int warTarget &&
            ObserverDiplomacyActionAvailabilityBuilder.Build(view)
                .Any(availability => availability.CounterpartCivilizationId == warTarget && availability.CanDeclareWar);
        var identifiedTargetName = targetId is int nameTarget
            ? identifiedCivilizationName(nameTarget)
            : null;

        var details = BuildDetails(
            view,
            contact,
            targetId,
            relationship,
            agreements,
            pendingProposals,
            selectedProposal,
            outboundAccess,
            inboundAccess,
            identifiedTargetName);

        var targetName = targetId is int
            ? identifiedTargetName!.ToUpperInvariant()
            : $"UNIDENTIFIED CONTACT {contact.ContactId}";
        var activeAgreements = agreements.Where(agreement =>
            agreement.Status == DiplomaticAgreementStatus.Active).ToArray();
        var recentEvents = targetId is int recentTarget
            ? view.RecentEvents.Where(history => PairMatches(history.PrimaryCivilizationId,
                    history.SecondaryCivilizationId, view.ObserverCivilizationId, recentTarget))
                .TakeLast(3).Select(history => history.Summary).ToArray()
            : Array.Empty<string>();
        var proposalSummary = selectedProposal is null
            ? "No pending proposals"
            : $"{(selectedProposal.RecipientCivilizationId == view.ObserverCivilizationId ? "Incoming" : "Outgoing")} · " +
              $"{selectedProposal.Kind}{(selectedProposal.AgreementType is { } type ? " · " + type : string.Empty)} · {selectedProposal.Summary}";

        return new RelationsPresentationState(
            contactIndex,
            view.Contacts.Count,
            proposalIndex,
            pendingProposals.Length,
            details,
            targetId,
            activeCommunication,
            selectedProposal?.ProposalId,
            selectedProposal?.RecipientCivilizationId == view.ObserverCivilizationId,
            selectedProposal?.RecipientCivilizationId == view.ObserverCivilizationId,
            selectedProposal?.ProposerCivilizationId == view.ObserverCivilizationId,
            activeCommunication && politicalState != DiplomaticPoliticalState.AtWar && !hasActiveNonAggression,
            activeCommunication && inboundAccess != AccessPermission.Granted,
            activeCommunication && politicalState is DiplomaticPoliticalState.Hostile or DiplomaticPoliticalState.AtWar or DiplomaticPoliticalState.Ceasefire,
            activeCommunication && politicalState is DiplomaticPoliticalState.Hostile or DiplomaticPoliticalState.AtWar,
            activeCommunication,
            canDeclareWar)
        {
            ContactName = targetName,
            ContactStatus = $"{contact.Awareness} · {contact.Condition} · {Math.Round(contact.Confidence * 100.0)}% confidence",
            CommunicationStatus = activeCommunication ? "Channel available" : "Channel unavailable",
            PoliticalStatus = relationship?.PoliticalState.ToString() ?? (targetId is null ? "Identity unknown" : "No formal relationship"),
            Trust = relationship?.Trust,
            Hostility = relationship?.Hostility,
            Fear = relationship?.Fear,
            Respect = relationship?.Respect,
            Cooperation = relationship?.Cooperation,
            AccessSummary = targetId is null ? "Transit rights unavailable until identification" :
                $"Your access: {inboundAccess} · Their access: {outboundAccess}",
            AgreementsSummary = activeAgreements.Length == 0 ? "No active agreements" :
                string.Join(" · ", activeAgreements.Select(agreement => agreement.Type.ToString())),
            ProposalSummary = proposalSummary,
            RecentEvents = recentEvents,
        };
    }

    private static string BuildDetails(
        DiplomaticStateView view,
        DiplomaticContactView contact,
        int? targetId,
        DiplomaticRelationshipView? relationship,
        DiplomaticAgreementSnapshot[] agreements,
        DiplomaticProposalSnapshot[] pendingProposals,
        DiplomaticProposalSnapshot? selectedProposal,
        AccessPermission outboundAccess,
        AccessPermission inboundAccess,
        string? identifiedTargetName)
    {
        var builder = new StringBuilder();
        var unidentifiedCount = view.Contacts.Count(candidate => candidate.TargetCivilizationId is null);
        builder.Append("Contacts: ").Append(view.Contacts.Count)
            .Append("  •  Unidentified: ").AppendLine(unidentifiedCount.ToString());

        if (targetId is not int identifiedTarget)
        {
            builder.Append("Selected: Unidentified contact ").AppendLine(contact.ContactId)
                .Append("Awareness: ").Append(contact.Awareness)
                .Append("  •  Condition: ").AppendLine(contact.Condition.ToString())
                .Append("Confidence: ").Append(Math.Round(contact.Confidence * 100.0)).AppendLine("%")
                .Append("Last observed system: ").AppendLine(contact.LastObservedSystemId?.ToString() ?? "Unknown")
                .Append("Communication: ").Append(contact.CommunicationAvailable ? "Available" : "Unavailable");
            return builder.ToString().TrimEnd();
        }

        var targetName = identifiedTargetName
            ?? throw new InvalidOperationException($"Identified civilization {identifiedTarget} has no display name.");
        builder.Append("Selected: ").Append(targetName).Append("  [ID ").Append(identifiedTarget).AppendLine("]")
            .Append("Awareness: ").Append(contact.Awareness)
            .Append("  •  Condition: ").AppendLine(contact.Condition.ToString())
            .Append("Communication: ").AppendLine(contact.CommunicationAvailable && contact.Condition != ContactCondition.StaleOrLost ? "Available" : "Unavailable")
            .Append("Political state: ").AppendLine(relationship?.PoliticalState.ToString() ?? "No formal relationship");

        if (relationship is not null)
        {
            builder.Append("Trust ").Append(Percent(relationship.Trust))
                .Append("  Hostility ").Append(Percent(relationship.Hostility))
                .Append("  Fear ").AppendLine(Percent(relationship.Fear))
                .Append("Respect ").Append(Percent(relationship.Respect))
                .Append("  Cooperation ").AppendLine(Percent(relationship.Cooperation));
        }

        builder.Append("Your access to them: ").Append(inboundAccess)
            .Append("  •  Their access to you: ").AppendLine(outboundAccess.ToString());

        var activeAgreements = agreements.Where(agreement => agreement.Status == DiplomaticAgreementStatus.Active).ToArray();
        builder.Append("Active agreements: ")
            .AppendLine(activeAgreements.Length == 0
                ? "None"
                : string.Join(", ", activeAgreements.Select(agreement => agreement.Type.ToString())));

        builder.Append("Pending proposals: ").AppendLine(pendingProposals.Length.ToString());
        if (selectedProposal is not null)
        {
            var direction = selectedProposal.RecipientCivilizationId == view.ObserverCivilizationId ? "Incoming" : "Outgoing";
            builder.Append(direction).Append(" #").Append(selectedProposal.ProposalId)
                .Append(" — ").Append(selectedProposal.Kind);
            if (selectedProposal.AgreementType is DiplomaticAgreementType agreementType)
                builder.Append(" / ").Append(agreementType);
            builder.AppendLine().Append("  ").AppendLine(selectedProposal.Summary);
        }

        var recent = view.RecentEvents
            .Where(history =>
                PairMatches(history.PrimaryCivilizationId, history.SecondaryCivilizationId, view.ObserverCivilizationId, identifiedTarget))
            .TakeLast(3)
            .ToArray();
        if (recent.Length > 0)
        {
            builder.AppendLine("Recent diplomacy:");
            foreach (var history in recent)
                builder.Append("• ").AppendLine(history.Summary);
        }

        return builder.ToString().TrimEnd();
    }

    private static AccessPermission LatestAccess(DiplomaticStateView view, int grantor, int visitor) =>
        view.AccessPermissions
            .Where(access => access.GrantorCivilizationId == grantor && access.VisitorCivilizationId == visitor)
            .OrderByDescending(access => access.UpdatedAtTick)
            .Select(access => access.Permission)
            .FirstOrDefault();

    private static bool PairMatches(int first, int second, int observer, int target) =>
        (first == observer && second == target) || (first == target && second == observer);

    private static bool PairMatches(int primary, int? secondary, int observer, int target) =>
        secondary is int other && PairMatches(primary, other, observer, target);

    private static string Percent(double value) => $"{Math.Round(Math.Clamp(value, 0.0, 1.0) * 100.0)}%";
}
