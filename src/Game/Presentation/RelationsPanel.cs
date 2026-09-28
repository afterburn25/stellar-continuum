using System;
using System.Linq;
using System.Text.Json;
using Godot;

namespace Game.Presentation;

/// <summary>Full-page host for observer-safe diplomacy. Commands are revalidated by Main.</summary>
public partial class RelationsPanel : CanvasLayer
{
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    public DiplomacyWorkspaceView Workspace { get; private set; } = null!;
    private int _contactIndex;
    private string? _contactId;
    private double _refresh;
    private string _signature = "";
    public bool IsOpen => Workspace?.Visible == true;

    public override void _Ready()
    {
        Layer = 6;
        _main = (Main)GetParent();
        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        Workspace = new DiplomacyWorkspaceView { Name = "DiplomacyWorkspace", Visible = false };
        Workspace.CloseRequested = _sidebar.CloseDrawer;
        Workspace.ContactSelected = index => { _contactIndex = index; _contactId = null; Refresh(true); };
        Workspace.ActionRequested = (action, target) =>
        {
            if (target is not int id) return;
            Workspace.SetResult(_main.IssueUiDiplomacyWorkspaceAction(id, action));
            Refresh(true);
        };
        Workspace.ProposalActionRequested = (id, action) =>
        {
            Workspace.SetResult(action == "withdraw" ? _main.IssueUiDiplomacyProposalWithdrawal(id) :
                _main.IssueUiDiplomacyProposalResponse(id, action == "accept"));
            Refresh(true);
        };
        Workspace.FocusSystemRequested = id => { _sidebar.CloseDrawer(); _main.UiFocusDiplomaticObservation(id); };
        AddChild(Workspace);
        _sidebar.SectionChanged += SectionChanged;
        GetViewport().SizeChanged += UpdateBounds;
        UpdateBounds();
    }

    public override void _ExitTree()
    {
        _sidebar.SectionChanged -= SectionChanged;
        GetViewport().SizeChanged -= UpdateBounds;
    }

    private void UpdateBounds()
    {
        var size = GetViewport().GetVisibleRect().Size;
        Workspace.Position = new Vector2(CampaignSidebar.RailWidth + 4, 74);
        Workspace.Size = new Vector2(Math.Max(100, size.X - CampaignSidebar.RailWidth - 10), Math.Max(100, size.Y - 80));
        Workspace.AdaptBounds();
    }

    private void SectionChanged(string? section)
    {
        Workspace.Visible = section == "relations";
        if (Workspace.Visible) { Refresh(true); AudioDirector.PlayConfirm(); }
        else Workspace.CloseModal();
    }

    public void SelectCivilization(int civilizationId)
    {
        var view = _main.GetUiDiplomacyView();
        var index = view.Contacts.ToList().FindIndex(c => c.TargetCivilizationId == civilizationId);
        if (index < 0) return;
        _contactIndex = index; _contactId = null;
        if (_sidebar.ActiveSection != "relations") _sidebar.ShowSection("relations");
        Refresh(true);
    }

    public override void _Input(InputEvent input)
    {
        if (!IsOpen || _main.UiIsMenuOpen) return;
        if (input is InputEventKey { Pressed: true, Echo: false, Keycode: Key.Escape })
        {
            if (Workspace.HasModal) Workspace.CloseModal(); else _sidebar.CloseDrawer();
            GetViewport().SetInputAsHandled();
        }
    }

    public override void _Process(double delta)
    {
        if (!IsOpen) return;
        var voice = _main.UiVoice;
        Workspace.SetTransmissionSubtitle(voice?.ActiveSpeakerName ?? "", voice?.ActiveSubtitle ?? "", voice?.HasActiveSubtitle == true);
        _refresh += delta;
        if (_refresh < .5) return;
        _refresh = 0;
        Refresh(false);
    }

    public void Refresh(bool force)
    {
        var view = _main.GetUiDiplomacyView();
        if (_contactId is not null)
        {
            var index = view.Contacts.ToList().FindIndex(c => c.ContactId == _contactId);
            if (index >= 0) _contactIndex = index;
        }
        _contactIndex = Math.Clamp(_contactIndex, 0, Math.Max(0, view.Contacts.Count - 1));
        _contactId = view.Contacts.Count == 0 ? null : view.Contacts[_contactIndex].ContactId;
        var state = _main.GetUiRelationsState(_contactIndex, 0);
        var signature = JsonSerializer.Serialize(view) + _contactIndex + state.SpeciesId;
        Workspace.SetDate(_main.UiDiplomacyDate);
        if (!force && signature == _signature) return;
        _signature = signature;
        var model = Workspace.BuildModel(view, _contactIndex, 0, _main.UiKnownDiplomaticName);
        model = model with { Contacts = model.Contacts.Select(c => c with
        {
            SpeciesName = c.CivilizationId is int id ? _main.UiKnownDiplomaticSpeciesName(id) : null,
        }).ToArray() };
        Workspace.Present(model, view, state.SpeciesId, _main.UiKnownDiplomaticSystemName);
    }
}
