using System;
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>
/// Dedicated early-release system/colony inspection panel. It renders only the already-filtered
/// player-facing inspection view exposed by Main and never queries authoritative galaxy state.
/// </summary>
public partial class SystemInspectionPanel : CanvasLayer
{
    private Main _main = null!;
    private Label _name = null!;
    private Label _survey = null!;
    private ProgressBar _progress = null!;
    private Label _guidance = null!;
    private GridContainer _facts = null!;
    private Label _colonyName = null!;
    private Label _colonyDetails = null!;
    private VBoxContainer _territory = null!;
    private string _territorySignature = "not-rendered";
    private readonly Dictionary<int, TerritorialSiteControls> _territorialSiteControls = new();
    private OptionButton? _territorialSelector;
    private Label? _territorialPreview;
    private Button? _territorialStart;
    private int? _confirmDecommissionId;
    private int _territoryKindIndex;
    private string _factSignature = "not-rendered";
    private int _lastSystemId = int.MinValue;
    private double _refreshTimer;

    private sealed record TerritorialSiteControls(Label Name, ProgressBar Progress, Label Detail, Button Action);

    public override void _Ready()
    {
        _main = GetParent() as Main
            ?? throw new InvalidOperationException("SystemInspectionPanel must be a child of Main.");

        var panel = new PanelContainer { Name = "SystemInspection" };

        var root = new VBoxContainer();
        root.AddThemeConstantOverride("separation", 10);
        panel.AddChild(root);

        var header = new HBoxContainer();
        header.AddThemeConstantOverride("separation", 8);
        root.AddChild(header);
        header.AddChild(VisualUi.Icon(VisualIconLibrary.Info, 50));
        var identity = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        _name = VisualUi.Text("SELECT A STAR", 20, VisualUi.PrimaryText);
        _survey = VisualUi.Text("NO TARGET", 11, VisualUi.Accent);
        identity.AddChild(_name);
        identity.AddChild(_survey);
        header.AddChild(identity);

        _progress = new ProgressBar { Name = "InspectionSurveyProgress", MaxValue = 100,
            ShowPercentage = false, CustomMinimumSize = new Vector2(0, 10) };
        root.AddChild(_progress);
        _guidance = VisualUi.Text("Select a star on the map to open its intelligence record.",
            12, VisualUi.Muted, wrap: true);
        root.AddChild(_guidance);
        var signalsHeading = new HBoxContainer { Name = "SignalsHeading" };
        signalsHeading.AddChild(VisualUi.Text("INTELLIGENCE SIGNALS", 11, VisualUi.Accent));
        var rule = new ColorRect { Color = VisualPalette.Keyline, CustomMinimumSize = new Vector2(0, 1),
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, SizeFlagsVertical = Control.SizeFlags.ShrinkCenter };
        signalsHeading.AddChild(rule);
        root.AddChild(signalsHeading);
        _facts = new GridContainer { Name = "InspectionFacts", Columns = 2 };
        _facts.AddThemeConstantOverride("h_separation", 8);
        _facts.AddThemeConstantOverride("v_separation", 8);
        root.AddChild(_facts);

        var colony = new PanelContainer { Name = "InspectionColonyCard" };
        colony.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 10));
        var colonyRow = new HBoxContainer();
        colonyRow.AddThemeConstantOverride("separation", 12);
        colony.AddChild(colonyRow);
        colonyRow.AddChild(VisualUi.Icon(VisualIconLibrary.Colony, 42));
        var colonyText = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        colonyText.AddChild(VisualUi.Text("SETTLEMENT INTELLIGENCE", 9, VisualUi.Muted));
        _colonyName = VisualUi.Text("NO COLONY DATA", 15, Colors.White);
        _colonyDetails = VisualUi.Text("Survey a system to identify settlements.", 11, VisualUi.Muted, wrap: true);
        colonyText.AddChild(_colonyName);
        colonyText.AddChild(_colonyDetails);
        colonyRow.AddChild(colonyText);
        root.AddChild(colony);

        var territoryHeading = new HBoxContainer { Name = "RegionalInfrastructureHeading" };
        territoryHeading.AddChild(VisualUi.Text("REGIONAL INFRASTRUCTURE", 11, VisualUi.Accent));
        var territoryRule = new ColorRect { Color = VisualPalette.Keyline, CustomMinimumSize = new Vector2(0, 1),
            SizeFlagsHorizontal = Control.SizeFlags.ExpandFill, SizeFlagsVertical = Control.SizeFlags.ShrinkCenter };
        territoryHeading.AddChild(territoryRule);
        root.AddChild(territoryHeading);
        _territory = new VBoxContainer { Name = "RegionalInfrastructure" };
        _territory.AddThemeConstantOverride("separation", 6);
        root.AddChild(_territory);

        _main.GetNode<CampaignSidebar>("CampaignSidebar").AddPanel(panel);
    }

    public override void _Process(double delta)
    {
        _refreshTimer += delta;
        if (_main is null || _name is null)
            return;

        var selectionChanged = _main.UiSelectedSystemId != _lastSystemId;
        if (!selectionChanged && _refreshTimer < 0.5)
            return;

        _lastSystemId = _main.UiSelectedSystemId;
        if (selectionChanged) _confirmDecommissionId = null;
        _refreshTimer = 0.0;
        RefreshIntelligence();
    }

    private void RefreshIntelligence()
    {
        var intelligence = _main.UiSelectedSystemIntelligence;
        _name.Text = intelligence.Name;
        _survey.Text = $"SURVEY {intelligence.SurveyStatus.ToUpperInvariant()}  ·  {intelligence.SurveyProgress:P0}";
        _progress.Value = intelligence.SurveyProgress * 100.0;
        _guidance.Text = intelligence.Guidance;
        _colonyName.Text = intelligence.ColonyName;
        _colonyDetails.Text = intelligence.ColonyDetails;

        RefreshTerritorialInfrastructure();

        var signature = string.Join('|', intelligence.Facts.Select(fact => $"{fact.Label}:{fact.Value}:{fact.Positive}"));
        if (signature == _factSignature) return;
        _factSignature = signature;
        foreach (var child in _facts.GetChildren()) child.QueueFree();
        if (intelligence.Facts.Length == 0)
        {
            var unavailable = VisualUi.Text("Detailed signals remain hidden until survey data supports them.",
                12, VisualUi.Muted, wrap: true);
            unavailable.Name = "InspectionFactsUnavailable";
            _facts.AddChild(unavailable);
            return;
        }
        foreach (var fact in intelligence.Facts)
        {
            var card = new PanelContainer { CustomMinimumSize = new Vector2(210, 58),
                SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
            var factSurface = VisualUi.Surface(highlighted: fact.Positive, margin: 8);
            factSurface.BgColor = VisualPalette.SurfaceSecondary;
            card.AddThemeStyleboxOverride("panel", factSurface);
            var body = new VBoxContainer();
            body.AddThemeConstantOverride("separation", 1);
            body.AddChild(VisualUi.Text(fact.Label, 9, VisualUi.Muted));
            body.AddChild(VisualUi.Text(fact.Value.ToUpperInvariant(), 14,
                fact.Positive ? VisualUi.Accent : Colors.White, wrap: true));
            card.AddChild(body);
            _facts.AddChild(card);
        }
    }

    private void RefreshTerritorialInfrastructure()
    {
        var sites = _main.UiSelectedTerritorialSites;
        var options = _main.UiTerritorialInstallationOptions;
        var signature = string.Join('|', options.Select(option => option.Kind)) +
            $"/{string.Join(',', sites.Select(site => site.Id))}";
        if (signature != _territorySignature)
        {
            _territorySignature = signature;
            RebuildTerritorialInfrastructure(sites, options);
        }
        UpdateTerritorialInfrastructure(sites, options);
    }

    private void RebuildTerritorialInfrastructure(UiTerritorialSite[] sites, UiTerritorialInstallationOption[] options)
    {
        foreach (var child in _territory.GetChildren()) child.QueueFree();
        _territorialSiteControls.Clear();
        _territorialSelector = null;
        _territorialPreview = null;
        _territorialStart = null;

        foreach (var site in sites)
        {
            var card = new PanelContainer { Name = "RegionalSiteProgress" };
            card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 8));
            var body = new VBoxContainer();
            var name = VisualUi.Text(string.Empty, 13, Colors.White);
            body.AddChild(name);
            var progress = new ProgressBar { MaxValue = 100, ShowPercentage = false,
                CustomMinimumSize = new Vector2(0, 8) };
            body.AddChild(progress);
            var detail = VisualUi.Text(string.Empty, 11, VisualUi.Muted, wrap: true);
            body.AddChild(detail);
            var remove = VisualUi.Button(string.Empty, string.Empty,
                () =>
                {
                    var current = _main.UiSelectedTerritorialSites.FirstOrDefault(item => item.Id == site.Id);
                    if (current is null) return;
                    var confirming = _confirmDecommissionId == site.Id;
                    if (current.Complete && !confirming) _confirmDecommissionId = site.Id;
                    else { _main.UiRemoveTerritorialInstallation(site.Id, confirming); _confirmDecommissionId = null; }
                    RefreshTerritorialInfrastructure();
                });
            body.AddChild(remove);
            card.AddChild(body);
            _territory.AddChild(card);
            _territorialSiteControls[site.Id] = new TerritorialSiteControls(name, progress, detail, remove);
        }

        if (options.Length == 0)
        {
            _territory.AddChild(VisualUi.Text("Select a surveyed system to plan regional infrastructure.", 11, VisualUi.Muted, wrap: true));
            return;
        }
        _territoryKindIndex = Math.Clamp(_territoryKindIndex, 0, options.Length - 1);
        var selected = options[_territoryKindIndex];
        var planning = new PanelContainer { Name = "TerritorialPlanning", SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        planning.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 7));
        var planningBody = new VBoxContainer();
        planningBody.AddChild(VisualUi.Text("PLAN A REGIONAL SITE", 12, VisualUi.Accent));
        var selector = new OptionButton { Name = "TerritorialInstallationSelector" };
        foreach (var option in options) selector.AddItem(option.Name);
        selector.Selected = _territoryKindIndex;
        selector.ItemSelected += index =>
        {
            _territoryKindIndex = (int)index;
            UpdateTerritorialInfrastructure(_main.UiSelectedTerritorialSites, _main.UiTerritorialInstallationOptions);
        };
        planningBody.AddChild(selector);
        var preview = VisualUi.Text(string.Empty, 10, VisualUi.Muted, wrap: true);
        planningBody.AddChild(preview);
        var start = VisualUi.Button(string.Empty, string.Empty, () =>
        {
            var current = _main.UiTerritorialInstallationOptions;
            if (current.Length == 0) return;
            _territoryKindIndex = Math.Clamp(_territoryKindIndex, 0, current.Length - 1);
            _main.UiStartTerritorialInstallation(current[_territoryKindIndex].Kind);
            RefreshTerritorialInfrastructure();
        });
        planningBody.AddChild(start);
        planning.AddChild(planningBody);
        _territory.AddChild(planning);
        _territorialSelector = selector;
        _territorialPreview = preview;
        _territorialStart = start;
    }

    private void UpdateTerritorialInfrastructure(UiTerritorialSite[] sites, UiTerritorialInstallationOption[] options)
    {
        foreach (var site in sites)
        {
            if (!_territorialSiteControls.TryGetValue(site.Id, out var controls)) continue;
            controls.Name.Text = site.Name.ToUpperInvariant();
            controls.Name.Modulate = site.Complete ? VisualUi.Accent : Colors.White;
            controls.Progress.Value = site.Progress * 100;
            controls.Detail.Text = site.Detail;
            controls.Detail.Modulate = site.Paused ? new Color("e4aa55") : VisualUi.Muted;
            var confirming = _confirmDecommissionId == site.Id;
            controls.Action.Text = site.Complete && !confirming ? "Decommission" :
                site.Complete ? "Confirm decommission" : "Cancel construction";
            controls.Action.TooltipText = site.Complete ? "Completed sites have no refund." :
                "Cancel returns 80% of remaining paid capital and materials.";
        }

        if (options.Length == 0 || _territorialSelector is null || _territorialPreview is null || _territorialStart is null)
            return;
        _territoryKindIndex = Math.Clamp(_territoryKindIndex, 0, options.Length - 1);
        _territorialSelector.Selected = _territoryKindIndex;
        var selected = options[_territoryKindIndex];
        _territorialPreview.Text = selected.Preview;
        _territorialPreview.Modulate = selected.Available ? VisualUi.Muted : new Color("e4aa55");
        _territorialStart.Text = selected.Available ? "Start project" : "Requirements";
        _territorialStart.TooltipText = selected.Preview;
        _territorialStart.Disabled = !selected.Available;
    }
}
