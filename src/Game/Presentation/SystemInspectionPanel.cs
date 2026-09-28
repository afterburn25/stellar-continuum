using System;
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
    private string _factSignature = "not-rendered";
    private int _lastSystemId = int.MinValue;
    private double _refreshTimer;

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
            var card = new PanelContainer { CustomMinimumSize = new Vector2(260, 58),
                SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
            var factSurface = VisualUi.Surface(highlighted: fact.Positive, margin: 8);
            factSurface.BgColor = VisualPalette.SurfaceSecondary;
            card.AddThemeStyleboxOverride("panel", factSurface);
            var body = new VBoxContainer();
            body.AddThemeConstantOverride("separation", 1);
            body.AddChild(VisualUi.Text(fact.Label, 9, VisualUi.Muted));
            body.AddChild(VisualUi.Text(fact.Value.ToUpperInvariant(), 14,
                fact.Positive ? VisualUi.Accent : Colors.White));
            card.AddChild(body);
            _facts.AddChild(card);
        }
    }
}
