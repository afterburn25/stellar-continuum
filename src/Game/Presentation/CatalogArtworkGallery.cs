using System;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>Explicit art-library preview, independent of campaign research visibility or unlocks.</summary>
public partial class CatalogArtworkGallery : PanelContainer
{
    private readonly GridContainer _grid = new() { Columns = 4, SizeFlagsHorizontal = SizeFlags.ExpandFill };
    private readonly LineEdit _search = new() { PlaceholderText = "Find a module or research topic", CustomMinimumSize = new(280, 40) };
    private readonly OptionButton _kind = new();
    private readonly Label _count = VisualUi.Text("", 12, VisualUi.Muted);
    private readonly Label _pageLabel = VisualUi.Text("", 12, VisualUi.Muted);
    private readonly Label _details = VisualUi.Text("Select an image to inspect it.", 15, Colors.White, true);
    private readonly TextureRect _portrait = new() { CustomMinimumSize = new(240, 240), ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
        StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered, MouseFilter = MouseFilterEnum.Ignore };
    private readonly Button _previous = new() { Text = "Previous" };
    private readonly Button _next = new() { Text = "Next" };
    private int _page;
    private const int PageSize = 12;
    public bool Standalone { get; set; }
    public event Action? CloseRequested;

    public override void _Ready()
    {
        Name = "CatalogArtworkGallery";
        Standalone = GetTree().CurrentScene == this;
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 18));
        VisualUi.ContainPointerInput(this);
        var root = new VBoxContainer { SizeFlagsVertical = SizeFlags.ExpandFill };
        root.AddThemeConstantOverride("separation", 12); AddChild(root);
        var top = new HBoxContainer(); root.AddChild(top);
        var title = VisualUi.Heading("STELLAR CONTINUUM · ASSET LIBRARY", 24);
        title.SizeFlagsHorizontal = SizeFlags.ExpandFill; top.AddChild(title);
        top.AddChild(VisualUi.Button("Close", "Close the art library", Close, VisualIconLibrary.NavClose));
        root.AddChild(VisualUi.Text("Artwork preview · Includes planned equipment. Viewing an image does not unlock it in your campaign.", 12, VisualUi.Gold, true));
        var filters = new HBoxContainer(); filters.AddThemeConstantOverride("separation", 12); root.AddChild(filters);
        foreach (var label in new[] { "Human station modules", "Pelagic modules", "High-gravity modules", "Cryogenic modules", "Research" }) _kind.AddItem(label);
        filters.AddChild(_kind); filters.AddChild(_search); filters.AddChild(_count);
        _kind.ItemSelected += _ => { _page = 0; Refresh(); };
        _search.TextChanged += _ => { _page = 0; Refresh(); };
        var body = new HBoxContainer { SizeFlagsVertical = SizeFlags.ExpandFill }; body.AddThemeConstantOverride("separation", 18); root.AddChild(body);
        var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled, SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ExpandFill };
        body.AddChild(scroll); scroll.AddChild(_grid); _grid.AddThemeConstantOverride("h_separation", 10); _grid.AddThemeConstantOverride("v_separation", 10);
        var inspector = new VBoxContainer { CustomMinimumSize = new(280, 0) }; body.AddChild(inspector);
        inspector.AddChild(_portrait); inspector.AddChild(_details);
        var footer = new HBoxContainer(); root.AddChild(footer);
        footer.AddChild(_previous); footer.AddChild(_pageLabel); footer.AddChild(_next);
        _previous.Pressed += () => { _page--; Refresh(); };
        _next.Pressed += () => { _page++; Refresh(); };
        Resized += () => _grid.Columns = _kind.Selected == 0 || Size.X < 1100 ? 3 : 4;
        Refresh();
    }

    private void Refresh()
    {
        _grid.Columns = _kind.Selected == 0 || Size.X < 1100 ? 3 : 4;
        foreach (var child in _grid.GetChildren()) { _grid.RemoveChild(child); child.QueueFree(); }
        var race = new[] { "terran_baseline", "pelagic_high_pressure", "compact_high_gravity", "cryogenic_hydrocarbon" };
        var entries = _kind.Selected == 4
            ? CatalogArtwork.Research.Select(x => new Item(x.Name, x.Domain.Replace('_', ' '), x.Art, "Research family illustration\n" + x.Name + "\n" + x.Domain.Replace('_', ' ')))
            : CatalogArtwork.Modules.Where(x => x.Race == race[_kind.Selected]).Select(x => new Item(x.Name, "Level " + x.Level, x.Art,
                x.Name + " · Level " + x.Level + "\n\n" + x.Description + "\n\n" + x.Status));
        var matches = entries.Where(x => (x.Title + " " + x.Subtitle).Contains(_search.Text.Trim(), StringComparison.OrdinalIgnoreCase)).ToArray();
        var pageCount = Math.Max(1, (matches.Length + PageSize - 1) / PageSize);
        _page = Math.Clamp(_page, 0, pageCount - 1);
        _count.Text = matches.Length + " entries"; _pageLabel.Text = $"  {_page + 1} / {pageCount}  ";
        _previous.Disabled = _page == 0; _next.Disabled = _page == pageCount - 1;
        foreach (var item in matches.Skip(_page * PageSize).Take(PageSize))
        {
            var card = new VBoxContainer { CustomMinimumSize = new(140, 0), SizeFlagsHorizontal = SizeFlags.ExpandFill };
            var button = VisualUi.Button("", item.Title + " · " + item.Subtitle, () => Select(item), CatalogArtwork.Texture(item.Art));
            button.CustomMinimumSize = new(140, 138); button.ExpandIcon = true; button.AddThemeConstantOverride("icon_max_width", 128);
            card.AddChild(button); card.AddChild(VisualUi.Text(item.Title, 13, Colors.White, true));
            card.AddChild(VisualUi.Text(item.Subtitle, 11, VisualUi.Accent)); _grid.AddChild(card);
        }
        if (matches.Length > 0) Select(matches[_page * PageSize]);
        else { _portrait.Texture = null; _details.Text = "No matching entries."; }
    }
    private void Select(Item item) { _portrait.Texture = CatalogArtwork.Texture(item.Art, portrait: true); _details.Text = item.Detail; }
    private void Close() { if (Standalone || GetTree().CurrentScene == this) GetTree().Quit(); else { CloseRequested?.Invoke(); QueueFree(); } }
    public override void _UnhandledKeyInput(InputEvent @event)
    {
        if (@event.IsActionPressed("ui_cancel")) { GetViewport().SetInputAsHandled(); Close(); }
    }
    private sealed record Item(string Title, string Subtitle, string Art, string Detail);
}
