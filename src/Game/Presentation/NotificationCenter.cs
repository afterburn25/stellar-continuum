using System;
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>Graphical, dismissible view over the bounded player notification feed.</summary>
public partial class NotificationCenter : PanelContainer
{
    private VBoxContainer _list = null!;
    private Label _empty = null!;
    private string _signature = string.Empty;

    public void Build(Action close)
    {
        Name = "NotificationCenter";
        Visible = false;
        MouseFilter = MouseFilterEnum.Stop;
        VisualUi.ContainPointerInput(this);
        AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 12));

        var body = new VBoxContainer();
        body.AddThemeConstantOverride("separation", 10);
        AddChild(body);
        var header = new HBoxContainer();
        header.AddChild(VisualUi.Icon(VisualIconLibrary.Info, 26));
        var title = VisualUi.Text("RECENT EVENTS", 18, VisualUi.Accent);
        title.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        header.AddChild(title);
        var closeButton = VisualUi.Button("", "Close recent events.", close, VisualIconLibrary.NavClose);
        closeButton.Name = "NotificationClose";
        header.AddChild(closeButton);
        body.AddChild(header);
        var description = VisualUi.Text("Important outcomes remain here until the campaign or mode changes.",
            11, VisualUi.Muted, wrap: true);
        description.CustomMinimumSize = new Vector2(370, 0);
        body.AddChild(description);

        var scroll = new ScrollContainer
        {
            Name = "NotificationScroll", HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, SizeFlagsVertical = SizeFlags.ExpandFill,
        };
        _list = new VBoxContainer { Name = "NotificationList", SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _list.AddThemeConstantOverride("separation", 7);
        scroll.AddChild(_list);
        body.AddChild(scroll);
        _empty = VisualUi.Text("No major events yet. Research, construction, missions, colonies and combat will appear here.",
            13, VisualUi.Muted, wrap: true);
        _list.AddChild(_empty);
    }

    public void UpdateItems(IReadOnlyList<UiPlayerNotification> items)
    {
        var signature = string.Join('|', items.Select(item => item.Sequence));
        if (signature == _signature) return;
        _signature = signature;
        foreach (var child in _list.GetChildren()) child.QueueFree();
        if (items.Count == 0)
        {
            _empty = VisualUi.Text("No major events yet. Research, construction, missions, colonies and combat will appear here.",
                13, VisualUi.Muted, wrap: true);
            _list.AddChild(_empty);
            return;
        }

        foreach (var item in items.Reverse().Take(16))
        {
            var card = new PanelContainer();
            card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 9));
            var content = new VBoxContainer();
            content.AddThemeConstantOverride("separation", 3);
            card.AddChild(content);
            var heading = new HBoxContainer();
            var category = VisualUi.Text(item.Category.ToUpperInvariant(), 10, CategoryColor(item.Category));
            category.SizeFlagsHorizontal = SizeFlags.ExpandFill;
            heading.AddChild(category);
            heading.AddChild(VisualUi.Text(item.Date, 10, VisualUi.Muted));
            content.AddChild(heading);
            var message = VisualUi.Text(item.Message, 12, Colors.White, wrap: true);
            // Establish the wrapping width before this free-floating panel's first layout pass.
            // Without it, Godot computes a very tall zero-width minimum and expands the panel
            // beyond the viewport even though the rendered text later fits on one or two lines.
            message.CustomMinimumSize = new Vector2(350, 0);
            content.AddChild(message);
            if (item.DiplomaticContactId is int contactId)
            {
                var open = VisualUi.Button("OPEN RELATIONS", "Review this diplomatic contact.", () =>
                {
                    var main = GetTree().Root.FindChild("Main", true, false) as Main;
                    main?.UiOpenDiplomaticContact(contactId);
                    Hide();
                }, VisualIconLibrary.Relations);
                open.Name = "OpenDiplomaticContact_" + contactId;
                content.AddChild(open);
            }
            _list.AddChild(card);
        }
    }

    private static Color CategoryColor(string category) => category switch
    {
        "Research" => new Color("b4a0e4"),
        "Industry" or "Construction" or "Economy" => VisualUi.Gold,
        "Ships" => VisualUi.Accent,
        "Exploration" => new Color("8fd7b0"),
        "Colony" => new Color("8fe5b1"),
        "Combat" => new Color("ee9a91"),
        _ => VisualUi.Accent,
    };
}
