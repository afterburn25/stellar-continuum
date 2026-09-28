using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Diplomacy;

namespace Game.Presentation;

public sealed record DiplomacyWorkspaceContact(string ContactId, string DisplayName, string Status, string Communication, double Confidence, bool Identified, int? CivilizationId, int SourceIndex, int? LastObservedSystemId, bool CommunicationAvailable, double? Cooperation, int PendingProposalCount)
{
    public string? SpeciesName { get; init; }
}
public sealed record DiplomacyWorkspaceProposal(long ProposalId, string Direction, string Kind, string Summary, bool CanAccept, bool CanReject, bool CanWithdraw);
public sealed record DiplomacyWorkspaceAgreement(long AgreementId, string Type, string Status);
public enum DiplomacyContactFilter { All, Identified, Unidentified, Cooperative, Neutral, Hostile, AtWar, PendingProposal, CommunicationAvailable }
public sealed record DiplomacyWorkspaceModel(IReadOnlyList<DiplomacyWorkspaceContact> Contacts, RelationsPresentationState Selected, IReadOnlyList<DiplomacyWorkspaceProposal> Proposals, IReadOnlyList<DiplomacyWorkspaceAgreement> Agreements);

public static class DiplomacyWorkspacePresenter
{
    public static DiplomacyWorkspaceModel Build(DiplomaticStateView view, int contactIndex, int proposalIndex, Func<int, string> identifiedCivilizationName)
    {
        ArgumentNullException.ThrowIfNull(view); ArgumentNullException.ThrowIfNull(identifiedCivilizationName);
        var presenter = new DiplomacyRelationsPresenter(); var selected = presenter.Build(view, contactIndex, proposalIndex, identifiedCivilizationName);
        var contacts = view.Contacts.Select((contact, sourceIndex) =>
        {
            var targetId = contact.TargetCivilizationId; var identified = targetId is not null;
            var name = identified ? identifiedCivilizationName(targetId!.Value) : "UNKNOWN CONTACT";
            var relation = identified ? view.Relationships.FirstOrDefault(r => r.OtherCivilizationId == targetId!.Value) : null;
            var pending = identified ? view.Proposals.Count(p => p.Status == DiplomaticProposalStatus.Pending && PairMatches(p.ProposerCivilizationId, p.RecipientCivilizationId, view.ObserverCivilizationId, targetId!.Value)) : 0;
            var communicationAvailable = contact.CommunicationAvailable && contact.Condition != ContactCondition.StaleOrLost;
            return new DiplomacyWorkspaceContact(contact.ContactId, name, identified ? relation?.PoliticalState.ToString() ?? "NO FORMAL RELATIONSHIP" : "IDENTITY UNKNOWN", communicationAvailable ? "CHANNEL AVAILABLE" : "CHANNEL UNAVAILABLE", Math.Clamp(contact.Confidence, 0, 1), identified, targetId, sourceIndex, contact.LastObservedSystemId, communicationAvailable, relation?.Cooperation, pending);
        }).ToArray();
        var target = selected.TargetCivilizationId;
        var proposals = target is null ? Array.Empty<DiplomacyWorkspaceProposal>() : view.Proposals.Where(p => p.Status == DiplomaticProposalStatus.Pending && PairMatches(p.ProposerCivilizationId, p.RecipientCivilizationId, view.ObserverCivilizationId, target.Value)).OrderBy(p => p.ProposalId).Select((p, index) => { var state = presenter.Build(view, contactIndex, index, identifiedCivilizationName); return new DiplomacyWorkspaceProposal(p.ProposalId, p.RecipientCivilizationId == view.ObserverCivilizationId ? "INCOMING" : "OUTGOING", p.Kind.ToString(), p.Summary, state.CanAcceptProposal, state.CanRejectProposal, state.CanWithdrawProposal); }).ToArray();
        var agreements = target is null ? Array.Empty<DiplomacyWorkspaceAgreement>() : view.Agreements.Where(a => PairMatches(a.CivilizationAId, a.CivilizationBId, view.ObserverCivilizationId, target.Value)).OrderBy(a => a.AgreementId).Select(a => new DiplomacyWorkspaceAgreement(a.AgreementId, a.Type.ToString(), a.Status.ToString())).ToArray();
        return new DiplomacyWorkspaceModel(contacts, selected, proposals, agreements);
    }
    public static IReadOnlyList<DiplomacyWorkspaceContact> FilterContacts(
        DiplomacyWorkspaceModel model,
        DiplomacyContactFilter filter) => model.Contacts.Where(contact => filter switch
        {
            DiplomacyContactFilter.Identified => contact.Identified,
            DiplomacyContactFilter.Unidentified => !contact.Identified,
            DiplomacyContactFilter.Cooperative => contact.Cooperation is >= 0.5,
            DiplomacyContactFilter.Neutral => contact.Status is "NO FORMAL RELATIONSHIP" or "Unknown" or "Peace",
            DiplomacyContactFilter.Hostile => contact.Status == "Hostile",
            DiplomacyContactFilter.AtWar => contact.Status == "AtWar",
            DiplomacyContactFilter.PendingProposal => contact.PendingProposalCount > 0,
            DiplomacyContactFilter.CommunicationAvailable => contact.CommunicationAvailable,
            _ => true,
        }).ToArray();
    private static bool PairMatches(int a, int b, int observer, int target) => (a == observer && b == target) || (a == target && b == observer);
}
