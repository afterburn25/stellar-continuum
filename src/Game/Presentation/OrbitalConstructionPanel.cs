using Godot;
using Game.Presentation.Spatial;

namespace Game.Presentation;

public partial class OrbitalConstructionPanel : PanelContainer
{
    private readonly Label _title, _description, _state, _time, _cost, _upkeep, _output, _reason;
    private readonly ProgressBar _progress;
    private readonly Button _build, _cancel;
    private readonly OrbitalStructureView _model;
    private string _projectId = "";
    public OrbitalConstructionPanel(Main main)
    {
        Name = "OrbitalInspector"; Visible = false;
        VisualUi.ContainPointerInput(this); AddThemeStyleboxOverride("panel", CinematicArt.Frame(margin: 12));
        var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled, VerticalScrollMode = ScrollContainer.ScrollMode.Auto };
        AddChild(scroll);
        var body = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill }; body.AddThemeConstantOverride("separation", 8); scroll.AddChild(body);
        var header = new HBoxContainer(); body.AddChild(header);
        _title = VisualUi.Text("ORBITAL CONSTRUCTION", 16, wrap: true); _title.SizeFlagsHorizontal = SizeFlags.ExpandFill; header.AddChild(_title);
        var close = VisualUi.Button("", "Return to planetary information", main.UiCloseOrbitalInspector, VisualIconLibrary.NavClose);
        close.Name = "CloseOrbitalInspector"; header.AddChild(close);
        _model = new OrbitalStructureView { CustomMinimumSize = new(0, 164) }; body.AddChild(_model);
        _state = VisualUi.Text("", 12, VisualUi.Accent); body.AddChild(_state);
        _progress = new ProgressBar { ShowPercentage = false, CustomMinimumSize = new(0, 7), MaxValue = 1 }; body.AddChild(_progress);
        _time = VisualUi.Text("", 12, wrap: true); body.AddChild(_time);
        _description = VisualUi.Text("", 12, VisualUi.Muted, true); body.AddChild(_description);
        Label Stat(string name) { body.AddChild(VisualUi.Text(name, 10, VisualUi.Accent)); var value = VisualUi.Text("", 12, wrap: true); body.AddChild(value); return value; }
        _cost = Stat("CONSTRUCTION COST"); _upkeep = Stat("DAILY UPKEEP"); _output = Stat("MATERIAL OUTPUT");
        _reason = VisualUi.Text("", 12, VisualUi.Gold, true); body.AddChild(_reason);
        _build = VisualUi.Button("Authorize construction", "Authorize the displayed cost and start or queue this orbital construction.", () => main.UiQueueConstruction(_projectId), VisualIconLibrary.Construction);
        _build.Name = "BuildOrbitalStructure"; body.AddChild(_build);
        _cancel = VisualUi.Button("Cancel construction", "Refund the displayed authorization amount. Consumed materials are never refunded.", () => main.UiCancelConstruction(_projectId), VisualIconLibrary.NavClose);
        _cancel.Name = "CancelOrbitalConstruction"; body.AddChild(_cancel);
    }
    public void Refresh(Main main, bool drawerOpen)
    {
        var view = main.UiSelectedOrbitalConstruction;
        Visible = view is not null && !drawerOpen && !main.UiIsMenuOpen && !main.UiIsDeveloperToolsOpen && !main.UiIsSurfaceOpen;
        if (!Visible || view is null) return;
        Position = new(GetViewportRect().Size.X - 282, 84); Size = new(270, GetViewportRect().Size.Y - 132);
        _projectId = view.Id; _title.Text = view.Name; _description.Text = view.Description; _state.Text = view.State;
        _model.Present(view.Id, view.State == "Planned orbital site" ? 1 : view.Progress);
        _progress.Value = view.Progress; _progress.Visible = view.State == "Under construction";
        _time.Text = view.State == "Queued" ? $"Queued at position {view.QueuePosition}; waiting for the active project and its requirements."
            : view.DaysRemaining > 0 ? $"At least {view.DaysRemaining:0.0} game days. Material shortages extend construction." : "Construction complete";
        _cost.Text = view.Cost; _upkeep.Text = view.Upkeep; _output.Text = $"{view.MaterialOutput:0.00} / day";
        _reason.Text = view.LockReason ?? ""; _reason.Visible = view.LockReason is not null;
        _build.Visible = view.State == "Planned orbital site"; _build.Disabled = !view.CanBuild;
        _cancel.Visible = view.State is "Under construction" or "Queued";
        _cancel.Text = $"Cancel · refund {main.UiFormatMoney(view.CancellationRefundPreview)}";
    }
}
