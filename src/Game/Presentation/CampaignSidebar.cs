using System;
using System.Collections.Generic;
using Godot;

namespace Game.Presentation;

/// <summary>Map-first navigation with a dedicated operational page for each game department.</summary>
public partial class CampaignSidebar : CanvasLayer
{
    public const float RailWidth = 58;
    // The old narrow drawer made research, industry, fleets, and colonies feel like menus.
    // These are now proper operational pages that retain the map behind them.
    public const float DrawerWidth = 760;
    private PanelContainer _rail = null!;
    private PanelContainer _drawer = null!;
    private ScrollContainer _scroll = null!;
    private VBoxContainer _panels = null!;
    private Label _title = null!;
    private readonly Dictionary<string, PanelContainer> _sections = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Button> _navigation = new(StringComparer.Ordinal);
    private float _captionSafeArea;
    private bool _tutorialActive;
    public string? ActiveSection { get; private set; }
    public bool IsDrawerOpen => ActiveSection is not null;
    public Rect2 UiDrawerBounds => _drawer?.GetGlobalRect() ?? new Rect2();
    public event Action<string?>? SectionChanged;
    public void SetTutorialActive(bool active)
    {
        if (_tutorialActive == active) return;
        _tutorialActive = active;
        UpdateBounds();
    }

    /// <summary>Bottom space reserved for the persistent voice caption while a drawer is open.</summary>
    public void SetCaptionSafeArea(float height)
    {
        var next = Mathf.Max(0, height);
        var effective = IsDrawerOpen ? Mathf.Max(_captionSafeArea, next) : next;
        if (Mathf.Abs(effective - _captionSafeArea) < 1f) return;
        _captionSafeArea = effective;
        UpdateBounds();
    }

    public override void _Ready()
    {
        Layer = 5;
        _rail = new PanelContainer { Name = "NavigationRail", MouseFilter = Control.MouseFilterEnum.Stop };
        VisualUi.ContainPointerInput(_rail);
        _rail.AddThemeStyleboxOverride("panel", VisualUi.OperationSurface(margin: 3));
        var railScroll = new ScrollContainer
        {
            Name = "NavigationScroll",
            HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true,
        };
        _rail.AddChild(railScroll);
        var railItems = new VBoxContainer { Name = "Items", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        railItems.AddThemeConstantOverride("separation", 2);
        railScroll.AddChild(railItems);
        AddChild(_rail);
        AddNavigation(railItems, "map", "Map", VisualIconLibrary.NavGalaxySemantic, "Show the map and close the detail drawer.", CloseDrawer);
        var main = (Main)GetParent();
        AddNavigation(railItems, "home", "Home", VisualIconLibrary.NavHomeSemantic, "Center the home system.", main.UiSelectHomeSystem);
        AddNavigation(railItems, "inspection", "Inspect", VisualIconLibrary.NavInspection, "Inspect the selected system's known information.");
        AddNavigation(railItems, "zoom-in", "Zoom in", VisualIconLibrary.NavZoomIn, "Zoom toward the selected star or world.", main.UiZoomIn).Name = "MapZoomIn";
        AddNavigation(railItems, "zoom-out", "Zoom out", VisualIconLibrary.NavZoomOut, "Zoom out to the next map scale.", main.UiZoomOut).Name = "MapZoomOut";
        AddNavigation(railItems, "economy", "Economy", VisualIconLibrary.NavEconomy, "Review revenue, operating costs, and purchasing power.");
        AddNavigation(railItems, "research", "Research", VisualIconLibrary.NavResearch, "Choose research and follow progress.");
        AddNavigation(railItems, "industry", "Construction", VisualIconLibrary.NavConstruction, "Construct planetary and orbital infrastructure from stored materials.");
        AddNavigation(railItems, "ships", "Ships", VisualIconLibrary.NavShipyard, "Choose a ship design and build your fleet.");
        AddNavigation(railItems, "explore", "Explore", VisualIconLibrary.NavExploration, "Follow scout and science missions.");
        AddNavigation(railItems, "colonies", "Colonies", VisualIconLibrary.NavColonization, "Choose a surveyed world and settle with a colony ship.");
        AddNavigation(railItems, "logistics", "Logistics", VisualIconLibrary.NavLogistics, "Inspect supply and infrastructure connections.");
        AddNavigation(railItems, "relations", "Relations", VisualIconLibrary.NavRelations, "Review known diplomatic contacts.");
        AddNavigation(railItems, "menu", "Menu", VisualIconLibrary.NavSettings, "Save, switch Player or Developer mode, or manage your campaign.");

        _drawer = new PanelContainer { Name = "DetailDrawer", Visible = false, MouseFilter = Control.MouseFilterEnum.Stop };
        VisualUi.ContainPointerInput(_drawer);
        _drawer.AddThemeStyleboxOverride("panel", CinematicArt.Frame());
        var body = new VBoxContainer { Name = "Body" };
        body.AddThemeConstantOverride("separation", 10);
        _drawer.AddChild(body);
        var heading = new HBoxContainer { Name = "Header" };
        _title = VisualUi.Heading("OPERATIONS", 22);
        _title.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        heading.AddChild(_title);
        var close = VisualUi.Button("", "Close this operations page and return to the map.", CloseDrawer, VisualIconLibrary.NavClose);
        close.Name = "DrawerClose";
        heading.AddChild(close);
        body.AddChild(heading);
        var headerKeyline = new ColorRect
        {
            Name = "HeaderKeyline", Color = new Color(VisualUi.Accent, .55f),
            CustomMinimumSize = new Vector2(0, 2), MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        body.AddChild(headerKeyline);
        _scroll = new ScrollContainer
        {
            Name = "DetailScroll", HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true,
            SizeFlagsVertical = Control.SizeFlags.ExpandFill,
        };
        body.AddChild(_scroll);
        _panels = new VBoxContainer { Name = "Panels", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        _scroll.AddChild(_panels);
        AddChild(_drawer);
        GetViewport().SizeChanged += UpdateBounds;
        UpdateBounds();
        UpdateNavigation();
    }

    public override void _ExitTree() => GetViewport().SizeChanged -= UpdateBounds;

    public void AddPanel(PanelContainer panel)
    {
        var section = panel.Name.ToString() switch
        {
            "DemoProgress" => "demo", "ExplorationPanel" => "explore",
            "SystemInspection" => "inspection", "LogisticsNetwork" => "logistics",
            "RelationsOverlay" => "relations", _ => panel.Name.ToString().ToLowerInvariant(),
        };
        RegisterSection(section, panel);
    }

    public void RegisterSection(string section, PanelContainer panel)
    {
        panel.Name = section switch { "economy" => "Economy", "research" => "Research", "industry" => "Construction", "ships" => "Ships", "explore" => "Exploration", "inspection" => "Inspection", "logistics" => "Logistics", "relations" => "Relations", "menu" => "Menu", "demo" => "Demo", _ => section };
        panel.MouseFilter = Control.MouseFilterEnum.Stop;
        // Section content belongs to DetailScroll: it must forward wheel input up to that
        // scroller. DetailDrawer, including its header and margins, is the final boundary.
        panel.MouseForcePassScrollEvents = true;
        panel.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.TopLeft);
        panel.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        panel.AddThemeStyleboxOverride("panel", new StyleBoxEmpty());
        _sections.Add(section, panel);
        _panels.AddChild(panel);
        panel.Visible = ActiveSection == section || section == "explore" && ActiveSection == "colonies";
    }

    public void ShowSection(string section)
    {
        if (ActiveSection == section) { CloseDrawer(); return; }
        ActiveSection = section;
        _title.Text = section switch
        {
            "economy" => "ECONOMY", "industry" => "CONSTRUCTION", "ships" => "SHIPYARD", "inspection" => "SYSTEM INTELLIGENCE",
            "explore" => "EXPEDITION CONTROL", "colonies" => "COLONY SITES", "demo" => "YOUR FIRST COLONY",
            "menu" => "CAMPAIGN", _ => section.ToUpperInvariant(),
        };
        foreach (var entry in _sections)
            entry.Value.Visible = entry.Key == section || entry.Key == "explore" && section == "colonies";
        _drawer.Visible = section != "relations";
        _scroll.ScrollVertical = 0;
        UpdateNavigation();
        SectionChanged?.Invoke(section);
    }

    public void CloseDrawer()
    {
        ActiveSection = null;
        _drawer.Visible = false;
        _captionSafeArea = 0;
        foreach (var panel in _sections.Values) panel.Visible = false;
        UpdateNavigation();
        SectionChanged?.Invoke(null);
    }

    private Button AddNavigation(Container parent, string key, string title, Texture2D icon, string tooltip, Action? action = null)
    {
        icon = RailIcon(key, icon);
        var button = VisualUi.Button(title, tooltip, action ?? (() => ShowSection(key)), icon);
        button.Name = "Nav" + title;
        button.ToggleMode = true;
        button.CustomMinimumSize = new Vector2(0, 36);
        button.Text = "";
        button.TooltipText = title + " — " + tooltip;
        var quiet = VisualUi.OperationSurface(margin: 3);
        quiet.BgColor = new Color(0.01f, 0.03f, 0.05f, .7f);
        quiet.BorderWidthLeft = quiet.BorderWidthRight = quiet.BorderWidthTop = quiet.BorderWidthBottom = 0;
        quiet.ShadowSize = 0;
        button.AddThemeStyleboxOverride("normal", quiet);
        var hover = (StyleBoxFlat)quiet.Duplicate();
        hover.BgColor = VisualPalette.SurfaceRaised;
        hover.BorderColor = VisualPalette.Focus;
        hover.BorderWidthLeft = 2;
        var active = (StyleBoxFlat)hover.Duplicate();
        active.BgColor = new Color("0d2636");
        active.BorderColor = VisualUi.Accent;
        // Theme padding adds to this minimum. Keep all ten destinations fully visible
        // without scrolling after a larger-window round trip at the supported 720px height.
        foreach (var state in new[] { "normal", "hover", "pressed", "hover_pressed", "disabled", "focus" })
        {
            var source = state is "pressed" or "hover_pressed" ? active : state is "hover" or "focus" ? hover : quiet;
            var style = (StyleBox)source.Duplicate();
            style.ContentMarginTop = 3;
            style.ContentMarginBottom = 3;
            button.AddThemeStyleboxOverride(state, style);
        }
        button.AddThemeConstantOverride("icon_max_width", 28);
        button.IconAlignment = HorizontalAlignment.Center;
        button.VerticalIconAlignment = VerticalAlignment.Center;
        button.AddThemeColorOverride("icon_normal_color", Colors.White);
        button.AddThemeColorOverride("icon_hover_color", Colors.White);
        button.AddThemeColorOverride("icon_pressed_color", Colors.White);
        button.AddThemeColorOverride("icon_focus_color", Colors.White);
        button.AddThemeColorOverride("icon_disabled_color", VisualPalette.Disabled);
        button.AddThemeFontSizeOverride("font_size", 11);
        parent.AddChild(button);
        _navigation.Add(key, button);
        return button;
    }

    private void UpdateNavigation()
    {
        foreach (var pair in _navigation)
        {
            var selected = ActiveSection == pair.Key || ActiveSection is null && pair.Key == "map";
            pair.Value.SetPressedNoSignal(selected);
            pair.Value.AddThemeColorOverride("icon_normal_color", Colors.White);
            pair.Value.AddThemeColorOverride("icon_pressed_color", Colors.White);
            pair.Value.AddThemeColorOverride("icon_hover_color", Colors.White);
            pair.Value.Modulate = Colors.White; // Preserve semantic icon colors; the button frame marks selection.
        }
    }

    private static Texture2D RailIcon(string key, Texture2D fallback) => key switch
    {
        "zoom-in" => VisualIconLibrary.Get("res://assets/visual/icons/navigation/nav_zoom_in.svg"),
        "zoom-out" => VisualIconLibrary.Get("res://assets/visual/icons/navigation/nav_zoom_out.svg"),
        _ => fallback,
    };

    private static Color NavigationAccent(string key) => key switch
    {
        "map" => VisualPalette.Exploration,
        "home" => VisualPalette.Success,
        "inspection" => VisualPalette.Science,
        "zoom-in" or "zoom-out" => VisualPalette.Selected,
        "economy" => VisualPalette.Economy,
        "research" => VisualPalette.Science,
        "industry" => VisualPalette.Construction,
        "ships" => VisualPalette.Military,
        "explore" => VisualPalette.Exploration,
        "colonies" => VisualPalette.Diplomacy,
        "logistics" => VisualPalette.Selected,
        "relations" => VisualPalette.Unknown,
        _ => VisualPalette.TextSecondary,
    };

    private void UpdateBounds()
    {
        var viewport = GetViewport().GetVisibleRect().Size;
        _rail.Position = new Vector2(4, 74);
        _rail.Size = new Vector2(RailWidth - 8, Mathf.Min(540, Mathf.Max(120, viewport.Y - 78)));
        var availableWidth = Mathf.Max(240, viewport.X - RailWidth - 48);
        var pageWidth = Mathf.Min(DrawerWidth, availableWidth);
        _drawer.Position = new Vector2(RailWidth + 24 + (_tutorialActive ? 0 : Mathf.Max(0, (availableWidth - pageWidth) * 0.5f)), 80);
        var reservedCaption = IsDrawerOpen ? _captionSafeArea : 0;
        _drawer.Size = new Vector2(pageWidth, Mathf.Max(120, viewport.Y - 112 - reservedCaption));
    }
}
