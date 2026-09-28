using System;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>
/// Read-only graphical panel for the reconstructible home-system logistics network.
/// It renders Main's filtered presentation model and never mutates or owns logistics state.
/// </summary>
public partial class LogisticsNetworkPanel : CanvasLayer
{
    private Main _main = null!;
    private Label _system = null!;
    private Label _supply = null!;
    private Label _demand = null!;
    private Label _delivered = null!;
    private Label _shortfall = null!;
    private Label _guidance = null!;
    private VBoxContainer _nodes = null!;
    private string _signature = "not-rendered";
    private double _refreshTimer;

    public override void _Ready()
    {
        _main = GetParent() as Main
            ?? throw new InvalidOperationException("LogisticsNetworkPanel must be a child of Main.");

        var panel = new PanelContainer { Name = "LogisticsNetwork" };

        var root = new VBoxContainer();
        root.AddThemeConstantOverride("separation", 8);
        panel.AddChild(root);

        var header = new HBoxContainer();
        header.AddThemeConstantOverride("separation", 12);
        root.AddChild(header);
        header.AddChild(VisualUi.Icon(VisualIconLibrary.Logistics, 52));
        var heading = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        heading.AddChild(VisualUi.Text("SUPPLY NETWORK", 21, VisualUi.Accent));
        _system = VisualUi.Text("INITIALIZING", 12, VisualUi.Muted);
        heading.AddChild(_system);
        header.AddChild(heading);

        var metrics = new GridContainer { Name = "LogisticsMetrics", Columns = 2 };
        metrics.AddThemeConstantOverride("h_separation", 10);
        metrics.AddThemeConstantOverride("v_separation", 10);
        _supply = AddMetric(metrics, "SUPPLY / DAY", new Color("8fe5b1"));
        _demand = AddMetric(metrics, "DEMAND / DAY", VisualUi.Gold);
        _delivered = AddMetric(metrics, "DELIVERED / DAY", VisualUi.Accent);
        _shortfall = AddMetric(metrics, "SHORTFALL / DAY", new Color("ee9a91"));
        _supply.Name = "LogisticsSupply";
        _demand.Name = "LogisticsDemand";
        _delivered.Name = "LogisticsDelivered";
        _shortfall.Name = "LogisticsShortfall";
        root.AddChild(metrics);
        _guidance = VisualUi.Text("", 12, VisualUi.Muted, wrap: true);
        _guidance.Name = "LogisticsGuidance";
        root.AddChild(_guidance);
        root.AddChild(VisualUi.Text("NETWORK NODES", 12, VisualUi.Accent));
        _nodes = new VBoxContainer { Name = "LogisticsNodes" };
        _nodes.AddThemeConstantOverride("separation", 7);
        root.AddChild(_nodes);

        _main.GetNode<CampaignSidebar>("CampaignSidebar").AddPanel(panel);
    }

    public override void _Process(double delta)
    {
        _refreshTimer += delta;
        if (_main is null || _nodes is null || _refreshTimer < 0.5)
            return;

        _refreshTimer = 0.0;
        RefreshNetwork();
    }

    private static Label AddMetric(GridContainer parent, string title, Color color)
    {
        var panel = new PanelContainer { CustomMinimumSize = new Vector2(280, 74), SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 10));
        var body = new VBoxContainer();
        body.AddChild(VisualUi.Text(title, 10, VisualUi.Muted));
        var value = VisualUi.Text("0.00", 20, color);
        body.AddChild(value);
        panel.AddChild(body);
        parent.AddChild(panel);
        return value;
    }

    private void RefreshNetwork()
    {
        var network = _main.UiHomeSystemLogistics;
        _system.Text = $"{network.SystemName.ToUpperInvariant()}  ·  {network.Nodes.Length} NODES  ·  {network.CorridorCount} CORRIDORS";
        _supply.Text = network.SupplyPerDay.ToString("0.00");
        _demand.Text = network.DemandPerDay.ToString("0.00");
        _delivered.Text = network.DeliveredPerDay.ToString("0.00");
        _shortfall.Text = network.ShortfallPerDay.ToString("0.00");
        _shortfall.Modulate = network.ShortfallPerDay > 0.001 ? Colors.White : new Color("8fe5b1");
        _guidance.Text = network.ShortfallPerDay > 0.001
            ? "NEXT DECISION · Restore staffing, power or funding at the shortfall node, then add freight capacity if delivery still cannot meet demand."
            : "NETWORK READY · Supply currently meets represented demand. Expansion will add new corridors and operating requirements.";
        _guidance.Modulate = network.ShortfallPerDay > 0.001 ? new Color("ee9a91") : new Color("8fe5b1");

        var signature = string.Join('|', network.Nodes.Select(node =>
            $"{node.NodeId}:{node.SupplyPerDay:0.000}:{node.DemandPerDay:0.000}:{node.DeliveredPerDay:0.000}:{node.Status}"));
        if (signature == _signature) return;
        _signature = signature;
        foreach (var child in _nodes.GetChildren()) child.QueueFree();
        if (network.Nodes.Length == 0)
        {
            _nodes.AddChild(VisualUi.Text("No represented supply nodes are available yet.", 13, VisualUi.Muted));
            return;
        }

        foreach (var node in network.Nodes)
        {
            var card = new PanelContainer { Name = "LogisticsNode_" + node.NodeId };
            card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 10));
            var row = new HBoxContainer();
            row.AddThemeConstantOverride("separation", 12);
            card.AddChild(row);
            row.AddChild(VisualUi.Icon(NodeIcon(node.Kind), 38));
            var details = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
            var title = new HBoxContainer();
            var name = VisualUi.Text(node.Name.ToUpperInvariant(), 14, Colors.White);
            name.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
            title.AddChild(name);
            title.AddChild(VisualUi.Text(node.Status.ToUpperInvariant(), 10,
                node.Status == "Shortfall" ? new Color("ee9a91") : new Color("8fe5b1")));
            details.AddChild(title);
            details.AddChild(VisualUi.Text(
                $"{node.Kind.ToUpperInvariant()}  ·  SUPPLY {node.SupplyPerDay:0.00}  ·  DEMAND {node.DemandPerDay:0.00}  ·  DELIVERED {node.DeliveredPerDay:0.00}",
                11, VisualUi.Muted, wrap: true));
            row.AddChild(details);
            _nodes.AddChild(card);
        }
    }

    private static Texture2D NodeIcon(string kind) => kind switch
    {
        "Resource site" => VisualIconLibrary.Industry,
        "Shipyard" or "Orbital hub" => VisualIconLibrary.Construction,
        "Depot" => VisualIconLibrary.Logistics,
        _ => VisualIconLibrary.Colony,
    };
}
