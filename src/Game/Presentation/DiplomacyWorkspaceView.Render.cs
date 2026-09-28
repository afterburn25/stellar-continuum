using System;
using System.Linq;
using System.Text.RegularExpressions;
using Godot;
using Game.Simulation.Diplomacy;
using Game.Simulation.Time;

namespace Game.Presentation;

public partial class DiplomacyWorkspaceView
{
    public Action? CloseRequested { get; set; }
    public Action<long, string>? ProposalActionRequested { get; set; }
    public Action<int>? FocusSystemRequested { get; set; }
    private VBoxContainer _layout = null!, _contactRows = null!, _meters = null!, _details = null!, _actions = null!;
    private HBoxContainer _mainColumns = null!;
    private PanelContainer _contactPanel = null!, _portraitPanel = null!, _relationshipPanel = null!;
    private ScrollContainer _detailsScroll = null!;
    private Label _date = null!, _name = null!, _political = null!, _channel = null!, _result = null!, _subtitle = null!;
    private TextureRect _portrait = null!;
    private Control _signal = null!, _modal = null!;
    private LineEdit _search = null!;
    private OptionButton _filter = null!;
    private HBoxContainer _tabs = null!;
    private HBoxContainer _compactActions = null!;
    private bool _compact;
    private string _tab = "Agreements";
    private DiplomaticStateView? _observerView;
    private Func<int, string>? _systemName;
    private double _phase;
    public bool HasModal => _modal?.Visible == true;

    partial void BuildPremiumLayout()
    {
        VisualUi.ContainPointerInput(this);
        var background = new PanelContainer { Name = "WorkspaceBackground" };
        background.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        var surface = VisualUi.Surface(margin: 18);
        surface.BgColor = new Color("09131e");
        surface.BorderColor = new Color("3a5965");
        background.AddThemeStyleboxOverride("panel", surface);
        AddChild(background);
        _layout = new VBoxContainer { Name = "WorkspaceRoot" };
        _layout.AddThemeConstantOverride("separation", 12);
        background.AddChild(_layout);
        var header = new HBoxContainer();
        header.AddChild(VisualUi.Icon(VisualIconLibrary.Relations, 34));
        header.AddChild(VisualUi.Text("RELATIONS", 26, Colors.White));
        _date = VisualUi.Text("", 14, VisualUi.Muted);
        _date.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        _date.HorizontalAlignment = HorizontalAlignment.Right;
        header.AddChild(_date);
        var close = VisualUi.Button("RETURN TO MAP", "Close Relations", () => CloseRequested?.Invoke(), VisualIconLibrary.NavClose);
        close.Name = "DiplomacyClose"; header.AddChild(close); _layout.AddChild(header);

        _mainColumns = new HBoxContainer { Name = "WorkspaceColumns", SizeFlagsVertical = SizeFlags.ExpandFill };
        _mainColumns.AddThemeConstantOverride("separation", 14);
        _layout.AddChild(_mainColumns);
        var contacts = new VBoxContainer();
        contacts.AddChild(VisualUi.Text("CONTACT DIRECTORY", 13, VisualUi.Accent));
        _search = new LineEdit { Name = "ContactSearch", PlaceholderText = "Search name or species", ClearButtonEnabled = true };
        _search.AddThemeFontSizeOverride("font_size", 14);
        _search.TextChanged += _ => RenderContacts();
        contacts.AddChild(_search);
        _filter = new OptionButton { Name = "ContactFilter" };
        foreach (var value in Enum.GetValues<DiplomacyContactFilter>()) _filter.AddItem(Words(value.ToString()));
        _filter.ItemSelected += _ => RenderContacts();
        contacts.AddChild(_filter);
        _contactRows = new VBoxContainer { Name = ContactListNode, SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _contactRows.AddThemeConstantOverride("separation", 8);
        contacts.AddChild(Scroll(_contactRows));
        _contactPanel = Panel(contacts);
        _mainColumns.AddChild(_contactPanel);

        _portraitPanel = new PanelContainer { Name = SelectedContactNode, SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsStretchRatio = 3 };
        var portraitSurface = VisualUi.Surface(margin: 0); portraitSurface.BgColor = new Color("0b1b2b");
        portraitSurface.BorderColor = new Color("567481");
        _portraitPanel.AddThemeStyleboxOverride("panel", portraitSurface);
        _mainColumns.AddChild(_portraitPanel);
        var transmission = new VBoxContainer();
        transmission.AddThemeConstantOverride("separation", 0);
        _portraitPanel.AddChild(transmission);
        var channelMargin = new MarginContainer();
        foreach (var edge in new[] { "left", "right", "top", "bottom" }) channelMargin.AddThemeConstantOverride("margin_" + edge, 8);
        _channel = VisualUi.Text("SIGNAL SEARCH", 13, VisualUi.Accent);
        channelMargin.AddChild(_channel);
        transmission.AddChild(channelMargin);
        var stage = new Control { Name = "TransmissionStage", ClipContents = true, SizeFlagsVertical = SizeFlags.ExpandFill,
            CustomMinimumSize = new Vector2(0, 120) };
        transmission.AddChild(stage);
        _portrait = new TextureRect { Name = "CivilizationPortrait", ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
            TextureFilter = TextureFilterEnum.LinearWithMipmaps, MouseFilter = MouseFilterEnum.Ignore };
        _portrait.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        stage.AddChild(_portrait);
        _signal = new DiplomacySignalField { Name = "UnknownSignal", MouseFilter = MouseFilterEnum.Ignore };
        _signal.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); stage.AddChild(_signal);
        var lower = new VBoxContainer();
        _political = VisualUi.Text("", 14, VisualUi.Gold, true);
        _name = VisualUi.Text("THE UNDISCOVERED", 28, Colors.White, true);
        _subtitle = VisualUi.Text("Explore beyond your borders to make first contact.", 15, VisualUi.Muted, true);
        _subtitle.MaxLinesVisible = 2;
        _subtitle.TextOverrunBehavior = TextServer.OverrunBehavior.TrimEllipsis;
        lower.AddChild(_political); lower.AddChild(_name); lower.AddChild(_subtitle);
        var captionPanel = Panel(lower);
        captionPanel.Name = "TransmissionCaption";
        var captionStyle = VisualUi.Surface(margin: 14);
        captionStyle.BgColor = new Color(.018f, .035f, .055f, .94f);
        captionPanel.AddThemeStyleboxOverride("panel", captionStyle);
        transmission.AddChild(captionPanel);

        var metrics = new VBoxContainer();
        metrics.AddChild(VisualUi.Text("RELATIONSHIP", 13, VisualUi.Accent));
        _meters = new VBoxContainer { Name = RelationshipPanelNode };
        _meters.AddThemeConstantOverride("separation", 13);
        metrics.AddChild(_meters);
        _actions = new VBoxContainer { Name = "DiplomaticActions" };
        _actions.AddThemeConstantOverride("separation", 6);
        metrics.AddChild(_actions);
        _relationshipPanel = Panel(Scroll(metrics));
        _mainColumns.AddChild(_relationshipPanel);

        _compactActions = new HBoxContainer { Name = "CompactDiplomaticActions", Visible = false };
        _compactActions.AddThemeConstantOverride("separation", 8);
        _layout.AddChild(_compactActions);
        _tabs = new HBoxContainer { Name = "DiplomacyTabs" };
        _tabs.AddThemeConstantOverride("separation", 8);
        foreach (var tab in new[] { "Agreements", "Proposals", "History", "Intelligence", "Overview" })
        {
            var chosen = tab;
            var button = VisualUi.Button(tab.ToUpperInvariant(), "Show " + tab.ToLowerInvariant(), () => { _tab = chosen; RenderDetails(); });
            button.Name = "Tab" + tab; button.SizeFlagsHorizontal = SizeFlags.ExpandFill;
            _tabs.AddChild(button);
        }
        _layout.AddChild(_tabs);
        _details = new VBoxContainer { Name = "DiplomacyDetails", SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _details.AddThemeConstantOverride("separation", 8);
        _detailsScroll = Scroll(_details);
        _detailsScroll.Name = "DiplomacyDetailsScroll";
        _detailsScroll.SizeFlagsVertical = SizeFlags.Fill;
        _layout.AddChild(_detailsScroll);
        _result = VisualUi.Text("Select a contact to review your relationship.", 14, VisualUi.Accent, true);
        _result.Name = "DiplomacyActionResult"; _layout.AddChild(_result);

        _modal = new Control { Name = "DiplomacyModal", Visible = false, MouseFilter = MouseFilterEnum.Stop };
        _modal.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        VisualUi.ContainPointerInput(_modal);
        AddChild(_modal);
        AdaptBounds();
    }

    public void AdaptBounds()
    {
        if (_layout is null) return;
        var compact = Size.X < 1450 || Size.Y < 760;
        _compact = compact;
        _layout.AddThemeConstantOverride("separation", compact ? 8 : 12);
        var workspacePanel = (PanelContainer)_layout.GetParent();
        var workspaceFrame = (StyleBoxFlat)workspacePanel.GetThemeStylebox("panel").Duplicate();
        workspaceFrame.ContentMarginLeft = workspaceFrame.ContentMarginRight = compact ? 12 : 18;
        workspaceFrame.ContentMarginTop = workspaceFrame.ContentMarginBottom = compact ? 12 : 18;
        workspacePanel.AddThemeStyleboxOverride("panel", workspaceFrame);
        _compactActions.Visible = compact;
        _actions.Visible = !compact;
        _meters.AddThemeConstantOverride("separation", compact ? 7 : 13);
        _contactPanel.CustomMinimumSize = new Vector2(compact ? 216 : 264, 0);
        _relationshipPanel.CustomMinimumSize = new Vector2(compact ? 244 : 296, 0);
        _detailsScroll.CustomMinimumSize = new Vector2(0, compact ? 64 : 228);
        _detailsScroll.Size = new Vector2(_detailsScroll.Size.X, compact ? 64 : 228);
        _name.AddThemeFontSizeOverride("font_size", compact ? 23 : 32);
        _political.Visible = !compact;
        _subtitle.Visible = !compact;
        if (Model is not null) RenderActions();
    }

    public void SetDate(string date) { if (_date is not null) _date.Text = date; }
    public void SetResult(string message)
    {
        _result.Text = message;
        _subtitle.Text = message; // Text feedback is independent of the speech/audio backend.
    }

    public void SetTransmissionSubtitle(string speaker, string text, bool active)
    {
        if (Model is null) return;
        _subtitle.Visible = !_compact || active;
        _subtitle.Text = active ? speaker + " · " + text : Words(Model.Selected.ContactStatus);
        _subtitle.TooltipText = _subtitle.Text;
    }

    public void Present(DiplomacyWorkspaceModel model, DiplomaticStateView view, string? knownSpeciesId, Func<int, string> knownSystemName)
    {
        Model = model; _observerView = view; _systemName = knownSystemName;
        var s = model.Selected;
        var treatment = DiplomacyTransmissionStyle.ForKnownSpecies(s.TargetCivilizationId is null ? null : knownSpeciesId);
        _channel.AddThemeColorOverride("font_color", treatment.Accent);
        _portraitPanel.AddThemeStyleboxOverride("panel", treatment.Frame());
        _name.Text = model.Contacts.Count == 0 ? "THE UNDISCOVERED" : s.TargetCivilizationId is null ? "UNKNOWN CONTACT" : s.ContactName;
        _political.Text = model.Contacts.Count == 0 ? "NO KNOWN CIVILIZATIONS" : (s.PoliticalStatus == "AtWar" ? "AT WAR" : Words(s.PoliticalStatus).ToUpperInvariant());
        _political.Modulate = s.PoliticalStatus is "AtWar" or "Hostile" ? new Color("f39982") : Colors.White;
        _channel.Text = s.HasVisibleCommunication ? "●  COMMUNICATION CHANNEL AVAILABLE" : "○  COMMUNICATION UNAVAILABLE";
        _subtitle.Text = Words(s.ContactStatus);
        _portrait.Texture = s.TargetCivilizationId is not null && knownSpeciesId is not null
            ? VisualIconLibrary.Get(CivilizationArtworkLibrary.TransmissionPathForSpecies(knownSpeciesId)) : null;
        _portrait.Visible = _portrait.Texture is not null;
        _signal.Visible = !_portrait.Visible;
        RenderContacts(); RenderMeters(); RenderActions(); RenderDetails();
    }

    private void RenderContacts()
    {
        if (Model is null || _contactRows is null) return;
        Clear(_contactRows);
        var filtered = FilterContacts(Model, (DiplomacyContactFilter)_filter.Selected)
            .Where(c => c.DisplayName.Contains(_search.Text, StringComparison.OrdinalIgnoreCase) ||
                c.SpeciesName?.Contains(_search.Text, StringComparison.OrdinalIgnoreCase) == true).ToArray();
        if (filtered.Length == 0)
        {
            _contactRows.AddChild(VisualUi.Text(Model.Contacts.Count == 0 ? "No contacts yet.\n\nSend scout ships into unexplored systems to discover other civilizations." : "No contacts match this filter.", 15, VisualUi.Muted, true));
            return;
        }
        foreach (var contact in filtered)
        {
            var b = VisualUi.Button("", "Select " + contact.DisplayName, () => ContactSelected?.Invoke(contact.SourceIndex));
            b.Name = "Contact_" + contact.SourceIndex;
            b.CustomMinimumSize = new Vector2(0, 88);
            b.ClipContents = true;
            var box = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore };
            box.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
            box.OffsetLeft = 12; box.OffsetRight = -12; box.OffsetTop = 9; box.OffsetBottom = -9;
            box.AddChild(VisualUi.Text(contact.DisplayName.ToUpperInvariant(), 15, Colors.White, true));
            box.AddChild(VisualUi.Text(Words(contact.Status), 13, VisualUi.Gold, true));
            box.AddChild(VisualUi.Text(contact.Identified ? contact.Communication.Replace("CHANNEL ", "") : $"Identity confidence {contact.Confidence:P0}", 12, VisualUi.Muted, true));
            b.AddChild(box);
            b.ToggleMode = true; b.SetPressedNoSignal(contact.SourceIndex == Model.Selected.ContactIndex);
            _contactRows.AddChild(b);
        }
    }

    private void RenderMeters()
    {
        Clear(_meters);
        var s = Model!.Selected;
        foreach (var (title, value, color) in new (string, double?, string)[] {
            ("TRUST", s.Trust, "78c5a5"), ("RESPECT", s.Respect, "77b9d3"), ("FEAR", s.Fear, "d9b677"),
            ("HOSTILITY", s.Hostility, "d67c72"), ("COOPERATION", s.Cooperation, "a796ce") })
        {
            var row = new HBoxContainer();
            var label = VisualUi.Text(title, 13, VisualUi.Muted); label.SizeFlagsHorizontal = SizeFlags.ExpandFill; row.AddChild(label);
            row.AddChild(VisualUi.Text(value is null ? "UNKNOWN" : $"{value:P0}", 14, value is null ? VisualUi.Muted : new Color(color)));
            _meters.AddChild(row);
            var bar = new ProgressBar { Name = "Meter" + title, Value = value is null ? 0 : value.Value * 100, ShowPercentage = false,
                CustomMinimumSize = new Vector2(0, 8), TooltipText = value is null ? "No legitimate relationship reading is available." : title + ": observed relationship value" };
            bar.AddThemeStyleboxOverride("background", new StyleBoxFlat { BgColor = new Color("182733") });
            bar.AddThemeStyleboxOverride("fill", new StyleBoxFlat { BgColor = new Color(color) });
            _meters.AddChild(bar);
        }
    }

    private void RenderActions()
    {
        Clear(_actions);
        Clear(_compactActions);
        Container actions = _compact ? _compactActions : _actions;
        var s = Model!.Selected;
        var available = _observerView is null ? null : ObserverDiplomacyActionAvailabilityBuilder.Build(_observerView)
            .FirstOrDefault(a => a.CounterpartCivilizationId == s.TargetCivilizationId);
        if (s.HasVisibleCommunication)
            AddAction(actions, "Open transmission", "communication", true, () => SetResult("Channel open. Select a proposal to begin negotiations."));
        else if (available?.CanAttemptCommunication == true)
            AddAction(actions, "Establish communication", "communication", true);
        AddAction(actions, "Negotiate", "negotiate", s.CanOfferNonAggression || s.CanRequestAccess || s.CanOfferPeace || s.CanOfferCeasefire || s.CanSetAccess, OpenNegotiation);
        AddAction(actions, "Declare war", "war", s.CanDeclareWar, () => Confirm("DECLARE WAR ON " + s.ContactName,
            "Your civilizations will enter a state of war. Active agreements may be affected.",
            () => DispatchTo("war", s.TargetCivilizationId), true));
        if (available is null || (!s.HasVisibleCommunication && available.CanAttemptCommunication == false))
        {
            var explanation = VisualUi.Text(s.ContactCount == 0 ? "Discovery opens diplomatic options." : "Identify this contact and recover communication to negotiate.", 13, VisualUi.Muted, true);
            explanation.SizeFlagsHorizontal = SizeFlags.ExpandFill;
            actions.AddChild(explanation);
        }
    }

    private void AddAction(Container parent, string title, string id, bool legal, Action? action = null)
    {
        if (!legal) return;
        var b = VisualUi.Button(title, title, action ?? (() => Dispatch(id)));
        b.Name = "Action_" + id;
        b.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        parent.AddChild(b);
    }

    private void Dispatch(string action) { CloseModal(); ActionRequested?.Invoke(action, Model?.Selected.TargetCivilizationId); }
    private void DispatchTo(string action, int? target) { CloseModal(); ActionRequested?.Invoke(action, target); }

    private bool IsPair(int a, int b) => _observerView is { } v && Model?.Selected.TargetCivilizationId is int t &&
        ((a == v.ObserverCivilizationId && b == t) || (b == v.ObserverCivilizationId && a == t));

    private void RenderDetails()
    {
        if (Model is null || _details is null) return;
        Clear(_details);
        _detailsScroll.ScrollVertical = 0;
        foreach (Button b in _tabs.GetChildren()) b.Modulate = b.Name == "Tab" + _tab ? VisualUi.Accent : Colors.White;
        switch (_tab)
        {
            case "Agreements":
                RenderAccess(_details);
                var agreements = _observerView!.Agreements.Where(a => IsPair(a.CivilizationAId, a.CivilizationBId)).ToArray();
                if (agreements.Length == 0) Empty(_details, "NO AGREEMENTS", "Your active agreements will appear here once accepted.");
                foreach (var a in agreements)
                    DetailCard(_details, Words(a.Type.ToString()).ToUpperInvariant(), a.Status + " · Since " + Date(a.StartedAtTick) +
                        (a.EndedAtTick is long ended ? " · Ended " + Date(ended) : ""), VisualIconLibrary.DiplomacyAgreement);
                break;
            case "Proposals":
                if (Model.Proposals.Count == 0) Empty(_details, "NO PENDING PROPOSALS", "Use Negotiate to propose a supported agreement.");
                foreach (var p in Model.Proposals)
                {
                    var card = DetailCard(_details, p.Direction + " · " + Words(p.Kind), p.Summary, VisualIconLibrary.DiplomacyAgreement);
                    var buttons = new HBoxContainer();
                    if (p.CanAccept) buttons.AddChild(CommandButton("Accept", "Accept_" + p.ProposalId, () => ProposalActionRequested?.Invoke(p.ProposalId, "accept")));
                    if (p.CanReject) buttons.AddChild(CommandButton("Reject", "Reject_" + p.ProposalId, () => ProposalActionRequested?.Invoke(p.ProposalId, "reject")));
                    if (p.CanWithdraw) buttons.AddChild(CommandButton("Withdraw", "Withdraw_" + p.ProposalId, () => ProposalActionRequested?.Invoke(p.ProposalId, "withdraw")));
                    card.AddChild(buttons);
                }
                break;
            case "History":
                var history = _observerView!.RecentEvents.Where(e => e.SecondaryCivilizationId is int other && IsPair(e.PrimaryCivilizationId, other)).Reverse().ToArray();
                if (history.Length == 0) Empty(_details, "A HISTORY YET TO BE WRITTEN", "Observer-visible contact, agreements and conflicts are recorded here.");
                foreach (var e in history) DetailCard(_details, Date(e.Tick) + " · " + Words(e.Kind.ToString()), e.Summary, VisualIconLibrary.Info);
                break;
            case "Intelligence":
                if (Model.Contacts.Count == 0) { Empty(_details, "NO INTELLIGENCE", "Explore to acquire legitimate observations."); break; }
                var c = Model.Contacts[Model.Selected.ContactIndex];
                var raw = _observerView!.Contacts[c.SourceIndex];
                DetailCard(_details, "CONTACT EVIDENCE", Words(raw.Awareness.ToString()) + " · " + Words(raw.Condition.ToString()) +
                    $" · {raw.Confidence:P0} identity confidence · Last observed {Date(raw.LastObservedTick)}", VisualIconLibrary.DiplomacyContact);
                if (c.LastObservedSystemId is int system)
                    _details.AddChild(CommandButton("Last observation · " + _systemName!(system), "FocusContactSystem", () => FocusSystemRequested?.Invoke(system)));
                DetailCard(_details, "UNRESOLVED INFORMATION", "Representative: Unknown    •    Government: Unknown    •    Military strength: Unknown\nTechnology and intentions: Unknown", VisualIconLibrary.Info);
                break;
            case "Overview":
                foreach (var contact in FilterContacts(Model, (DiplomacyContactFilter)_filter.Selected))
                    _details.AddChild(CommandButton(contact.DisplayName + "   ·   " + Words(contact.Status) + "   ·   " + contact.Communication.Replace("CHANNEL ", ""),
                        "Overview_" + contact.SourceIndex, () => ContactSelected?.Invoke(contact.SourceIndex)));
                if (Model.Contacts.Count == 0) Empty(_details, "THE GALACTIC COMMUNITY", "No foreign civilization has been observed.");
                break;
        }
    }

    private void RenderAccess(Container parent)
    {
        if (Model?.Selected.TargetCivilizationId is not int target || _observerView is null) return;
        var row = new HBoxContainer();
        string Access(int grantor, int visitor) => _observerView.AccessPermissions.Where(a => a.GrantorCivilizationId == grantor && a.VisitorCivilizationId == visitor)
            .OrderByDescending(a => a.UpdatedAtTick).FirstOrDefault()?.Permission.ToString().ToUpperInvariant() ?? "UNSPECIFIED";
        var ours = DetailCard(row, "OUR FLEETS → THEIR SPACE", Access(target, _observerView.ObserverCivilizationId), VisualIconLibrary.DiplomacyAccessGranted);
        var theirs = DetailCard(row, "THEIR FLEETS → OUR SPACE", Access(_observerView.ObserverCivilizationId, target), VisualIconLibrary.DiplomacyAccessGranted);
        parent.AddChild(row);
    }

    private void OpenNegotiation()
    {
        var body = ModalBody("NEGOTIATION", "Choose the agreement you want to propose.");
        var s = Model!.Selected;
        var target = s.TargetCivilizationId;
        foreach (var (name, id, legal, description) in new[] {
            ("Non-aggression", "nonaggression", s.CanOfferNonAggression, "Propose mutual non-aggression. The other civilization must accept."),
            ("Request transit access", "access", s.CanRequestAccess, "Ask permission for OUR fleets to enter THEIR territory."),
            ("Ceasefire", "ceasefire", s.CanOfferCeasefire, "Propose a ceasefire to halt active hostilities."),
            ("Peace", "peace", s.CanOfferPeace, "Propose peace to end the hostile political relationship."),
            ("Grant transit access", "grant", s.CanSetAccess, "Permit THEIR fleets to enter OUR territory."),
            ("Deny transit access", "deny", s.CanSetAccess, "Deny THEIR fleets permission to enter OUR territory.") })
        {
            if (!legal) continue;
            var action = id;
            var button = CommandButton(name, "Term_" + id, () =>
                Confirm(name.ToUpperInvariant(), description + "\n\nCounterpart: " + s.ContactName,
                    () => DispatchTo(action, target), false, action is "grant" or "deny" ? "APPLY ACCESS" : "SEND PROPOSAL"));
            body.AddChild(button);
        }
    }

    private void Confirm(string title, string description, Action commit, bool danger = false, string buttonTitle = "DECLARE WAR")
    {
        var body = ModalBody(title, description);
        var button = CommandButton(buttonTitle, "ConfirmDiplomaticAction", commit);
        VisualUi.ApplyInteractiveStates(button, danger ? new Color("dd876f") : VisualUi.Accent);
        body.AddChild(button);
    }

    private VBoxContainer ModalBody(string title, string description)
    {
        Clear(_modal); _modal.Visible = true;
        var dim = new ColorRect { Color = new Color(0, 0, 0, .78f), MouseFilter = MouseFilterEnum.Stop };
        dim.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); _modal.AddChild(dim);
        var center = new CenterContainer(); center.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); _modal.AddChild(center);
        var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 14);
        body.AddChild(VisualUi.Text(title, 24, VisualUi.Gold, true));
        body.AddChild(VisualUi.Text(description, 16, Colors.White, true));
        body.AddChild(CommandButton("Cancel", "CancelDiplomaticAction", CloseModal));
        var panel = Panel(body); panel.CustomMinimumSize = new Vector2(Math.Min(650, Math.Max(200, Size.X - 70)), 0);
        center.AddChild(panel);
        return body;
    }

    public void CloseModal() { if (_modal is not null) _modal.Visible = false; }
    public override void _Process(double delta)
    {
        if (!IsVisibleInTree()) return;
        _phase += Math.Min(delta, .05);
        _channel.Modulate = new Color(1, 1, 1, .90f + .10f * (float)Math.Sin(_phase * 1.7));
    }

    private static Button CommandButton(string text, string name, Action action)
    {
        var b = VisualUi.Button(text, text, action); b.Name = name; b.AddThemeFontSizeOverride("font_size", 14); return b;
    }
    private static void Clear(Node node) { foreach (var c in node.GetChildren()) { node.RemoveChild(c); c.QueueFree(); } }
    private static PanelContainer Panel(Control child)
    {
        var panel = new PanelContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 12)); panel.AddChild(child); return panel;
    }
    private static ScrollContainer Scroll(Control child)
    {
        var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled, FollowFocus = true, SizeFlagsVertical = SizeFlags.ExpandFill };
        child.SizeFlagsHorizontal = SizeFlags.ExpandFill; scroll.AddChild(child); return scroll;
    }
    private static VBoxContainer DetailCard(Container parent, string title, string text, Texture2D icon)
    {
        var box = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill };
        var row = new HBoxContainer(); row.AddChild(VisualUi.Icon(icon, 24));
        var heading = VisualUi.Text(title, 14, VisualUi.Accent, true);
        heading.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        row.AddChild(heading); box.AddChild(row);
        box.AddChild(VisualUi.Text(text, 14, Colors.White, true));
        parent.AddChild(Panel(box)); return box;
    }
    private static void Empty(Container parent, string title, string explanation) => DetailCard(parent, title, explanation, VisualIconLibrary.DiplomacyContact);
    private static string Words(string value) => Regex.Replace(value, "([a-z])([A-Z])", "$1 $2").Replace('_', ' ');
    private static string Date(long tick) => CampaignCalendar.FormatDate(tick / (double)DiplomacyCampaignClock.TicksPerSimulationDay);
}

/// <summary>Generic reception field. Its geometry contains no contact ID or hidden species data.</summary>
public partial class DiplomacySignalField : Control
{
    public override void _Draw()
    {
        var center = Size * .5f;
        DrawRect(new Rect2(Vector2.Zero, Size), new Color("0b1c2d"));
        for (var i = 1; i <= 5; i++)
            DrawArc(center, Math.Min(Size.X, Size.Y) * (.06f + i * .066f), 0, Mathf.Tau, 100, new Color(.3f, .6f, .7f, .12f), 1, true);
        var points = new Vector2[129];
        for (var i = 0; i < points.Length; i++)
        {
            var x = i / 128f;
            points[i] = new Vector2(x * Size.X, center.Y + MathF.Sin(x * 66) * MathF.Exp(-MathF.Pow((x - .5f) * 7, 2)) * Size.Y * .12f);
        }
        DrawPolyline(points, new Color("538b9a"), 2, true);
    }
    public override void _Notification(int what) { if (what == NotificationResized) QueueRedraw(); }
}
