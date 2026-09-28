using System;
using Godot;
using Game.Simulation.Diplomacy;

namespace Game.Presentation;

public partial class DiplomacyWorkspaceView : Control
{
    public const string ContactListNode = "ContactList";
    public const string SelectedContactNode = "SelectedContact";
    public const string RelationshipPanelNode = "RelationshipPanel";
    public const string AgreementsPanelNode = "AgreementsPanel";
    public const string ProposalsPanelNode = "ProposalsPanel";
    public Action<int>? ContactSelected { get; set; }
    public Action<long>? ProposalSelected { get; set; }
    public Action<string, int?>? ActionRequested { get; set; }
    public DiplomacyWorkspaceModel? Model { get; private set; }
    public DiplomacyWorkspaceModel BuildModel(DiplomaticStateView view, int contactIndex, int proposalIndex, Func<int, string> identifiedCivilizationName) => Model = DiplomacyWorkspacePresenter.Build(view, contactIndex, proposalIndex, identifiedCivilizationName);
    private static System.Collections.Generic.IReadOnlyList<DiplomacyWorkspaceContact> FilterContacts(DiplomacyWorkspaceModel model, DiplomacyContactFilter filter) => DiplomacyWorkspacePresenter.FilterContacts(model, filter);
    public override void _Ready() => BuildPremiumLayout();
    partial void BuildPremiumLayout();
}
