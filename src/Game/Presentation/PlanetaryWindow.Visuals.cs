using System;
using System.Collections.Generic;
using System.Linq;
using Game.Presentation.Spatial;
using Game.Simulation;
using Game.Simulation.Construction;
using Godot;

namespace Game.Presentation;

public sealed partial class PlanetaryWindow
{
    public Func<SystemSpatialBodyMarker?>? ReadPlanetAppearance { get; set; }
    private static readonly Color Gold = new("f5cf73"), Blue = new("7dbdff"), Orange = new("f6ac70"), Purple = new("c5a0ff"), Green = new("9cde80");
    private TextureRect _portrait = null!, _panorama = null!;
    private Label _worldClass = null!, _worldStats = null!, _designation = null!, _artCaption = null!, _queueTitle = null!;
    private VBoxContainer _queue = null!;
    private readonly Dictionary<int, SlotCard> _slotCards = new();
    private readonly Dictionary<string, Texture2D> _buildingArt = new();
    private readonly Dictionary<string, Label> _queueItems = new();
    private string _portraitKey = "", _queueKey = "";
    private sealed record SlotCard(TextureRect Art, Label Number, Label Title, Label State, Label Empty, ProgressBar Progress);

    private void BuildHeader(VBoxContainer root)
    {
        var navigation = new HBoxContainer(); navigation.AddThemeConstantOverride("separation", 8); root.AddChild(navigation);
        var wordmark = Text("STELLAR CONTINUUM   /   PLANETARY OPERATIONS", 14, Teal);
        wordmark.SizeFlagsHorizontal = SizeFlags.ExpandFill; navigation.AddChild(wordmark);
        navigation.AddChild(new PlaybackControl("PlanetaryPlayback", () => ReadPlaybackState?.Invoke() ?? new(true, SimulationClock.SpeedLevel.Normal, SimulationClock.SpeedLevel.Normal, false),
            () => { if (!Blocked) PlaybackCycleRequested?.Invoke(); }, () => { if (!Blocked) PlaybackPauseRequested?.Invoke(); }));
        navigation.AddChild(ActionButton("Save", "PlanetarySave", () => SaveRequested?.Invoke()));
        navigation.AddChild(ActionButton("Return to orbit", "PlanetaryBack", () => ReturnToOrbit?.Invoke()));

        var hero = new HBoxContainer { CustomMinimumSize = new(0, 166) }; hero.AddThemeConstantOverride("separation", 10); root.AddChild(hero);
        var landscape = new PanelContainer { ClipContents = true, SizeFlagsHorizontal = SizeFlags.ExpandFill };
        landscape.AddThemeStyleboxOverride("panel", PanelStyle("102832")); hero.AddChild(landscape);
        _panorama = Picture(GD.Load<Texture2D>("res://assets/visual/planetary/colony-panorama-v1.png"));
        _panorama.Name = "PlanetaryPanorama"; _panorama.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); landscape.AddChild(_panorama);
        var shade = Picture(new GradientTexture2D { Width = 512, Height = 2, FillFrom = Vector2.Zero, FillTo = new(1, 0),
            Gradient = new Gradient { Offsets = new[] { 0f, .42f, 1f }, Colors = new[] { new Color("07131df2"), new Color("07131da0"), new Color("07131d14") } } });
        shade.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); landscape.AddChild(shade);
        var labels = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore }; labels.AddThemeConstantOverride("separation", 1); landscape.AddChild(labels);
        labels.AddChild(_designation = Text("PLANETARY COLONY", 12, Gold));
        labels.AddChild(_title = Text("Planet", 42));
        labels.AddChild(_subtitle = Text("", 12, new Color("d1e3ee")));
        var spacer = new Control { SizeFlagsVertical = SizeFlags.ExpandFill, MouseFilter = MouseFilterEnum.Ignore }; labels.AddChild(spacer);
        labels.AddChild(_artCaption = Text("COLONY VISTA · ILLUSTRATION", 10, new Color("d1e3ee")));
        var summary = new PanelContainer { CustomMinimumSize = new(318, 0) }; summary.AddThemeStyleboxOverride("panel", PanelStyle("10222c")); hero.AddChild(summary);
        var summaryRow = new HBoxContainer(); summary.AddChild(summaryRow);
        _portrait = Picture(CelestialBodyMaterials.WhiteTexture); _portrait.Name = "PlanetaryPortrait";
        _portrait.CustomMinimumSize = new(132, 132); _portrait.SizeFlagsVertical = SizeFlags.ShrinkCenter;
        _portrait.StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered; summaryRow.AddChild(_portrait);
        var summaryText = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ShrinkCenter }; summaryText.AddThemeConstantOverride("separation", 10); summaryRow.AddChild(summaryText);
        summaryText.AddChild(Text("WORLD SUMMARY", 11, Teal)); summaryText.AddChild(_worldClass = Text("", 19)); summaryText.AddChild(_worldStats = Text("", 12, Muted));
    }

    private void RefreshVisuals(UiSurfaceSnapshot s)
    {
        _designation.Text = s.IsCapitalHub ? "EMPIRE CAPITAL" : s.IsResourceOutpost ? "RESOURCE OUTPOST" : s.HubLevel == 0 ? "COLONY FOUNDATION" : "PLANETARY COLONY";
        _worldClass.Text = s.SurfaceVisualClass switch { "temperate" => "Temperate world", "oceanic" => "Ocean world", "frozen" => "Frozen world", "hot" => "Hot rocky world", "airless" => "Airless world", "reducing" => "Reducing atmosphere", _ => "Rocky world" };
        _worldStats.Text = $"{s.Planet?.SystemName} system\n{s.Planet?.GravityG:0.00} g  ·  {s.Planet?.TemperatureKelvin - 273.15:0} °C\nStability  {s.Planet?.Stability:P0}";
        // A temperate city is decorative art, not a live settlement reconstruction.
        // Other environments and unbuilt foundations show their orbital backdrop.
        var city = s.HubLevel > 0 && s.SurfaceVisualClass == "temperate";
        var artPath = city ? "res://assets/visual/planetary/colony-panorama-v1.png" : "res://assets/visual/space/deep-field-v2.png";
        if (_panorama.Texture.ResourcePath != artPath) _panorama.Texture = GD.Load<Texture2D>(artPath);
        _artCaption.Text = city ? "COLONY VISTA · ILLUSTRATION" : "ORBITAL SURVEY  /  " + (s.HubLevel == 0 ? "COMMAND CENTER REQUIRED" : _worldClass.Text.ToUpperInvariant());
        var marker = ReadPlanetAppearance?.Invoke();
        var key = $"{s.BodyId}:{marker?.SurfaceKey}:{marker?.VisualClass}:{marker?.Atmosphere}:{marker?.HasDetailedEnvironment}";
        if (key != _portraitKey)
        {
            _portraitKey = key;
            // Own the portrait's material so its lighting cannot alter the orbital scene.
            _portrait.Material = marker is null ? null : (ShaderMaterial)CelestialBodyMaterials.GetPlanetMaterial(marker).Duplicate();
            _portrait.Visible = marker is not null;
            _portrait.TooltipText = $"{s.PlanetName} · {(marker?.SurfaceKey is null ? "Illustrated planetary appearance" : "Planetary imagery")}";
        }
        var queued = s.Buildings.Where(b => !b.Complete || b.UpgradeDaysRemaining > 0).ToArray();
        var keyQueue = (s.HubUpgradeDaysRemaining > 0 ? "command," : "") + string.Join(",", queued.Select(b => b.Id));
        _queueTitle.Text = $"CONSTRUCTION QUEUE   {queued.Length + (s.HubUpgradeDaysRemaining > 0 ? 1 : 0)}";
        if (keyQueue != _queueKey || _queue.GetChildCount() == 0)
        {
            _queueKey = keyQueue; Clear(_queue); _queueItems.Clear();
            if (s.HubUpgradeDaysRemaining > 0) { var label = Text("", 12, Gold); _queue.AddChild(label); _queueItems["command"] = label; }
            foreach (var b in queued)
            {
                var label = Text("", 12, BuildingColor(b.TypeId)); _queue.AddChild(label); _queueItems[b.Id.ToString()] = label;
            }
            if (_queueItems.Count == 0) _queue.AddChild(Text("No construction scheduled.\nChoose an empty surface slot to begin.", 12, Muted));
        }
        if (_queueItems.TryGetValue("command", out var command)) command.Text = $"Command Center  ·  {s.HubUpgradeDaysRemaining:0.0} days*";
        foreach (var b in queued) _queueItems[b.Id.ToString()].Text = $"{b.Name}  ·  " + (!b.Complete ? $"{b.Progress:P0}" : $"{b.UpgradeDaysRemaining:0.0} days*");
        _queue.TooltipText = "Construction shares empire materials. * Days remaining assume full funding. Select a building slot for progress and recovery actions.";
    }

    private Button CreateSlot(int slot)
    {
        var button = ActionButton("", "PlanetarySlot_" + slot, () => { _selectedSlot = slot; _tabs.CurrentTab = 1; Refresh(true); });
        button.CustomMinimumSize = new(140, 146); button.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        var box = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore }; box.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        box.OffsetLeft = 9; box.OffsetRight = -9; box.OffsetTop = 7; box.OffsetBottom = -7; box.AddThemeConstantOverride("separation", 2); button.AddChild(box);
        var number = Text($"SLOT {slot + 1:00}", 10, Muted); box.AddChild(number);
        var art = Picture(null); art.Name = "PlanetaryBuildingArt"; art.CustomMinimumSize = new(0, 65); art.SizeFlagsVertical = SizeFlags.ExpandFill;
        art.StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered; box.AddChild(art);
        var empty = Text("+", 34, Teal); empty.HorizontalAlignment = HorizontalAlignment.Center; empty.SizeFlagsVertical = SizeFlags.ExpandFill; box.AddChild(empty);
        var title = Text("", 12); title.HorizontalAlignment = HorizontalAlignment.Center; title.CustomMinimumSize = new(0, 30); box.AddChild(title);
        var state = Text("", 11, Teal); state.HorizontalAlignment = HorizontalAlignment.Center; box.AddChild(state);
        var progress = new ProgressBar { ShowPercentage = false, CustomMinimumSize = new(0, 3), MouseFilter = MouseFilterEnum.Ignore, MaxValue = 1 };
        progress.AddThemeStyleboxOverride("background", new StyleBoxFlat { BgColor = new("263a47") });
        progress.AddThemeStyleboxOverride("fill", new StyleBoxFlat { BgColor = Gold }); box.AddChild(progress);
        _slotCards[slot] = new(art, number, title, state, empty, progress); return button;
    }

    private void RefreshSlot(int slot, Button button, UiSurfaceBuilding? b, bool locked)
    {
        var card = _slotCards[slot]; var accent = b is null ? Teal : BuildingColor(b.TypeId);
        var selected = slot == _selectedSlot;
        button.AddThemeStyleboxOverride("normal", CardStyle(selected ? "183945" : b is null ? "0b1c28" : "122331", selected ? Teal : b is null ? new Color("294655") : accent.Darkened(.5f), b is null ? 1 : 3));
        card.Number.AddThemeColorOverride("font_color", locked ? Muted : accent);
        card.Art.Visible = b is not null; card.Empty.Visible = b is null;
        if (b is not null) card.Art.Texture = BuildingArt(b.TypeId);
        card.Empty.Text = locked ? "◇" : "+"; card.Empty.AddThemeColorOverride("font_color", locked ? Muted : Teal);
        card.Title.Text = b?.Name ?? (locked ? "Locked slot" : "Available slot");
        card.State.Text = b is null ? (locked ? "Command Center required" : "Construct building") : BuildingState(b);
        card.State.AddThemeColorOverride("font_color", b is not null && b.Complete && (!b.Enabled || !b.Powered || !b.Staffed || b.Condition <= .15) ? Bad : accent);
        card.Progress.Visible = b is not null && !b.Complete; card.Progress.Value = b?.Progress ?? 0;
        button.AccessibilityName = $"Slot {slot + 1}: {card.Title.Text}. {card.State.Text}";
    }

    private Texture2D BuildingArt(string type)
    {
        var family = SurfaceBuildingCatalog.FunctionalFamily(type);
        if (_buildingArt.TryGetValue(family, out var cached)) return cached;
        var index = family switch { "power_generator" => 0, "science_lab" => 1, "fabricator" => 2, "trade_hub" => 3, "habitat_complex" => 4, "controlled_agriculture" => 5, "water_reclamation" => 6, "grid_battery" => 7, _ => 8 };
        var atlas = GD.Load<Texture2D>("res://assets/visual/planetary/building-portraits-v1.png");
        var size = atlas.GetSize() / 3;
        return _buildingArt[family] = new AtlasTexture { Atlas = atlas, Region = new Rect2(new Vector2(index % 3, index / 3) * size, size), FilterClip = true };
    }
    private static Color BuildingColor(string type) => SurfaceBuildingCatalog.FunctionalFamily(type) switch
    { "power_generator" or "grid_battery" => Gold, "science_lab" => Purple, "fabricator" => Orange, "controlled_agriculture" => Green, "water_reclamation" or "habitat_complex" => Blue, "trade_hub" => Gold, _ => Teal };

    private void AddBuildOption(UiSurfaceSnapshot s, int slot, UiSurfaceBuildOption option)
    {
        var button = ActionButton("", "PlanetaryBuild_" + option.Id, () => Run(() => _build(slot, option.Id)));
        button.CustomMinimumSize = new(0, 84); button.TooltipText = option.Description;
        button.Disabled = !option.CanAfford || Blocked; button.SetMeta("type", option.Id);
        button.AccessibilityName = $"{option.Name}. {s.Currency.Format(option.CreditCost)}. {option.IndustryCost:0} materials.";
        button.AddThemeStyleboxOverride("normal", CardStyle("142733", BuildingColor(option.Id).Darkened(.45f), 3)); _details.AddChild(button);
        var row = new HBoxContainer { MouseFilter = MouseFilterEnum.Ignore }; row.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        row.OffsetLeft = 7; row.OffsetRight = -7; row.OffsetTop = 6; row.OffsetBottom = -6; button.AddChild(row);
        var picture = Picture(BuildingArt(option.Id)); picture.CustomMinimumSize = new(65, 65); picture.StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered; row.AddChild(picture);
        var labels = new VBoxContainer { MouseFilter = MouseFilterEnum.Ignore, SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ShrinkCenter }; row.AddChild(labels);
        labels.AddChild(Text(option.Name, 13, BuildingColor(option.Id))); labels.AddChild(Text($"{s.Currency.Format(option.CreditCost)}\n{option.IndustryCost:0} materials", 11));
        _details.AddChild(Text(option.Description, 11, Muted));
    }

    private static TextureRect Picture(Texture2D? texture) => new()
    { Texture = texture, ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize, StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered, MouseFilter = MouseFilterEnum.Ignore };
    private static StyleBoxFlat CardStyle(string color, Color border, int top = 1)
    { var style = PanelStyle(color); style.BorderColor = border; style.BorderWidthTop = top; return style; }
}
