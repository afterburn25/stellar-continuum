using System;
using System.Collections.Generic;
using Game.Simulation;
using Godot;

namespace Game.Presentation;

/// <summary>Explicit, marked development actions. Main validates every command and persists provenance.</summary>
public partial class DeveloperToolsLayer : CanvasLayer
{
    private Main _main = null!;
    private Control _overlay = null!;
    private VBoxContainer _commands = null!;
    private Label _mode = null!, _result = null!;
    private Button _close = null!;
    private readonly Dictionary<string, Button> _buttons = new();
    private SimulationClock.SpeedLevel _resumeSpeed;
    private double _refresh;
    public bool IsOpen => _overlay?.IsVisibleInTree() == true;

    public override void _Ready()
    {
        Name = "DeveloperToolsLayer";
        Layer = 90;
        _main = GetParent() as Main ?? throw new InvalidOperationException("DeveloperToolsLayer must be a child of Main.");
        _overlay = new ColorRect { Name = "DeveloperOverlay", Color = new Color(.008f, .016f, .026f, .9f), Visible = false };
        VisualUi.ContainPointerInput(_overlay);
        _overlay.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect);
        var center = new CenterContainer();
        center.SetAnchorsAndOffsetsPreset(Control.LayoutPreset.FullRect); _overlay.AddChild(center);
        var panel = new PanelContainer { Name = "DeveloperPanel", CustomMinimumSize = new(650, 600) };
        panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(true, 18)); center.AddChild(panel);
        var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 10); panel.AddChild(body);
        var heading = new HBoxContainer(); body.AddChild(heading);
        heading.AddChild(VisualUi.Icon(VisualIconLibrary.Construction, 32));
        var title = VisualUi.Text("DEVELOPER TOOLS", 25, VisualUi.Gold);
        title.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill; heading.AddChild(title);
        _close = VisualUi.Button("Close", "Close tools and restore the previous simulation speed (Esc).", Close, VisualIconLibrary.NavClose);
        _close.Name = "DeveloperToolsClose"; heading.AddChild(_close);
        _mode = VisualUi.Text("", 13, VisualUi.Gold); _mode.Name = "DeveloperProvenance"; body.AddChild(_mode);
        body.AddChild(VisualUi.Text("Simulation paused. Choose an action explicitly; changes stay in this Developer campaign.", 13, VisualUi.Muted, true));
        var scroll = new ScrollContainer
        {
            Name = "DeveloperCommandScroll", CustomMinimumSize = new(0, 315),
            SizeFlagsVertical = Control.SizeFlags.ExpandFill,
            HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true,
        };
        body.AddChild(scroll);
        _commands = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        _commands.AddThemeConstantOverride("separation", 8); scroll.AddChild(_commands);
        _result = VisualUi.Text("No action has been run in this session.", 13, VisualUi.Muted, true);
        _result.Name = "DeveloperCommandResult";
        _result.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        // Full exception/save messages remain readable without growing the modal off screen.
        var resultScroll = new ScrollContainer
        {
            Name = "DeveloperResultScroll", CustomMinimumSize = new(0, 64),
            HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true,
        };
        resultScroll.AddChild(_result); body.AddChild(resultScroll);
        var save = VisualUi.Button("Save Developer campaign", "Save the active Developer campaign and its tools-used marker.", () =>
        {
            if (!_main.UiIsDeveloperMode || _main.UiIsMenuOpen) return;
            _main.UiSave();
            _result.Text = _main.UiStatusMessage;
            _result.Modulate = VisualUi.Muted;
        }, VisualIconLibrary.Save);
        save.Name = "DeveloperSave"; body.AddChild(save);
        AddChild(_overlay);
        GetViewport().GuiFocusChanged += KeepToolsFocus;
    }

    public override void _ExitTree() => GetViewport().GuiFocusChanged -= KeepToolsFocus;

    private void KeepToolsFocus(Control focus)
    {
        if (focus is not null && IsOpen && !_main.UiIsMenuOpen && !_overlay.IsAncestorOf(focus))
            _close.GrabFocus();
    }

    public void Open()
    {
        if (IsOpen || !_main.UiIsDeveloperMode || _main.UiIsMenuOpen) return;
        _resumeSpeed = _main.UiCurrentSpeed;
        _main.UiResumeAtSpeed(SimulationClock.SpeedLevel.Paused);
        _result.Text = "Choose an action. No tools run automatically.";
        _result.Modulate = VisualUi.Muted;
        Refresh(); _overlay.Show(); _close.GrabFocus();
    }

    public void Close()
    {
        if (!IsOpen) return;
        _overlay.Hide();
        if (!_main.UiIsMenuOpen) _main.UiResumeAtSpeed(_main.UiIsDeveloperMode ? _resumeSpeed : SimulationClock.SpeedLevel.Normal);
    }

    public override void _Process(double delta)
    {
        if (!IsOpen) return;
        if (!_main.UiIsDeveloperMode) { Close(); return; }
        _refresh += delta;
        if (_refresh < .2) return;
        _refresh = 0; Refresh();
    }

    public override void _Input(InputEvent input)
    {
        if (!IsOpen || _main.UiIsMenuOpen) return;
        if (input.IsActionPressed("ui_cancel")) { Close(); GetViewport().SetInputAsHandled(); }
    }

    private void Refresh()
    {
        _mode.Text = _main.UiModeLabel.ToUpperInvariant() + "  ·  " +
            (_main.UiDeveloperToolsUsed ? "TOOLS USED · DEVELOPER CAMPAIGN" : "TOOLS UNUSED");
        foreach (var command in _main.UiDeveloperCommands)
        {
            if (_buttons.ContainsKey(command.Id)) continue;
            var card = new PanelContainer();
            card.AddThemeStyleboxOverride("panel", VisualUi.Surface(false, 8)); _commands.AddChild(card);
            var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 3); card.AddChild(body);
            var id = command.Id;
            var button = VisualUi.Button(command.Title, command.Description, () => Run(id), CommandIcon(id));
            button.Name = "DeveloperCommand_" + id; button.Alignment = HorizontalAlignment.Left;
            body.AddChild(button);
            body.AddChild(VisualUi.Text(command.Description, 12, VisualUi.Muted, true));
            _buttons.Add(id, button);
        }
        foreach (var button in _buttons.Values) button.Disabled = !_main.UiIsDeveloperMode || _main.UiIsMenuOpen;
    }

    private void Run(string id)
    {
        if (!IsOpen || _main.UiIsMenuOpen) return;
        var outcome = _main.UiRunDeveloperCommand(id);
        _result.Text = outcome.Message;
        _result.Modulate = outcome.Accepted ? VisualUi.Accent : new Color("efac92");
        Refresh();
    }

    private static Texture2D CommandIcon(string id) => id switch
    {
        "grant_resources" => VisualIconLibrary.Credits,
        "finish_orders" => VisualIconLibrary.Construction,
        "reveal_galaxy" => VisualIconLibrary.NavGalaxy,
        "unlock_technology" => VisualIconLibrary.Research,
        "advance_30_days" => VisualIconLibrary.Speed,
        _ => VisualIconLibrary.Info,
    };
}
