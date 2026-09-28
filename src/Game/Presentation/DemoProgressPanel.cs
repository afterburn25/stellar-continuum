using Godot;

namespace Game.Presentation;

/// <summary>Compact visual milestones; full instructions are available on demand.</summary>
public partial class DemoProgressPanel : CanvasLayer
{
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    private PanelContainer _strip = null!;
    private Label _objective = null!;
    private Label _research = null!;
    private Label _construction = null!;
    private Button _developerSpeed = null!;
    private Button _expeditionSpeed = null!;
    private readonly Button[] _steps = new Button[3];
    private double _refresh;

    public override void _Ready()
    {
        _main = (Main)GetParent();
        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        // Keep the map guide above the map but below the operations drawer. The guide's
        // buttons otherwise retain pointer ownership for a few frames while the drawer
        // opens and can pass wheel input through to the regional camera.
        Layer = 4;
        _strip = new PanelContainer { Name = "DemoMilestones", MouseFilter = Control.MouseFilterEnum.Ignore };
        _strip.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 6));
        var row = new HFlowContainer { MouseFilter = Control.MouseFilterEnum.Ignore };
        row.AddThemeConstantOverride("h_separation", 6);
        _strip.AddChild(row);
        var intro = VisualUi.Text("FIRST LIGHT", 10, VisualUi.Muted);
        intro.CustomMinimumSize = new Vector2(78, 38);
        row.AddChild(intro);
        _steps[0] = VisualUi.Button("1 · Warp", "Develop warp flight. Research and construction can run together.", () => _sidebar.ShowSection("research"), VisualIconLibrary.Research);
        _steps[1] = VisualUi.Button("2 · Fleet", "Build a scout, science vessel, and colony ship.", () => _sidebar.ShowSection("ships"), VisualIconLibrary.NavShips);
        _steps[2] = VisualUi.Button("3 · Settle", "Scout, survey, and settle a suitable world.", () => _sidebar.ShowSection("colonies"), VisualIconLibrary.Colony);
        foreach (var step in _steps) row.AddChild(step);
        row.AddChild(VisualUi.Button("Guide", "Open the current objective and suggested research and construction.", () => _sidebar.ShowSection("demo"), VisualIconLibrary.Info));
        AddChild(_strip);
        _sidebar.SectionChanged += OnSectionChanged;

        var panel = new PanelContainer { Name = "DemoProgress" };
        var content = new VBoxContainer();
        content.AddThemeConstantOverride("separation", 14);
        panel.AddChild(content);
        content.AddChild(VisualUi.Icon(VisualIconLibrary.Colony, 74));
        content.AddChild(VisualUi.Text("THE FIRST LIGHT EXPEDITION", 23, wrap: true));
        var tutorial = VisualUi.Button("Start / resume voice tutorial", "Your officers explain the controls and opening steps. Pauses the game on entry.",
            () => _main.UiStartVoiceTutorial(), VisualIconLibrary.Info);
        tutorial.Name = "VoiceTutorialStart"; content.AddChild(tutorial);
        var restartTutorial = VisualUi.Button("Restart tutorial", "Return to the first spoken lesson without resetting your campaign.",
            () => _main.UiStartVoiceTutorial(restart: true));
        restartTutorial.Name = "VoiceTutorialRestart"; content.AddChild(restartTutorial);
        content.AddChild(VisualUi.Text("Fast-forward at 8× during long waits, then pause to review funding, materials, and orders. The current route takes about 12–13 active minutes at 8× before your decisions; travel and planning can extend it.", 13, VisualUi.Gold, wrap: true));
        _objective = VisualUi.Text("", 16, wrap: true);
        _research = VisualUi.Text("", 14, VisualUi.Muted, wrap: true);
        _construction = VisualUi.Text("", 14, VisualUi.Muted, wrap: true);
        content.AddChild(_objective);
        content.AddChild(_research);
        content.AddChild(_construction);
        var actions = VisualUi.Actions(content);
        actions.AddChild(VisualUi.Button("Research", "Open research projects.", () => _sidebar.ShowSection("research"), VisualIconLibrary.Research));
        actions.AddChild(VisualUi.Button("Construction", "Open construction projects.", () => _sidebar.ShowSection("industry"), VisualIconLibrary.Construction));
        _expeditionSpeed = VisualUi.Button("Fast-forward at 8×", "Continue ordinary simulation at 8× during long waits. Pause any time to review the next decision.", () =>
            _main.UiSetSpeed((int)Game.Simulation.SimulationClock.SpeedLevel.Maximum), VisualIconLibrary.Speed);
        _expeditionSpeed.Name = "ExpeditionSpeed";
        content.AddChild(_expeditionSpeed);
        _developerSpeed = VisualUi.Button("Resume Developer at 24×", "Accelerate ordinary simulation rules in Developer mode.", _main.UiResumeDemoSpeed, VisualIconLibrary.Speed);
        _developerSpeed.Name = "DeveloperResumeSpeed";
        content.AddChild(_developerSpeed);
        content.AddChild(VisualUi.Text("The opening guide follows ordinary research and construction rules. Developer tools run only when you choose them.", 12, VisualUi.Muted, wrap: true));
        _sidebar.AddPanel(panel);
        Refresh();
    }

    public override void _ExitTree() => _sidebar.SectionChanged -= OnSectionChanged;

    public override void _Process(double delta)
    {
        _refresh += delta;
        if (_refresh < 0.25) return;
        _refresh = 0;
        Refresh();
    }

    private void Refresh()
    {
        RefreshVisibility();
        _developerSpeed.Visible = _main.UiIsDeveloperMode;
        _expeditionSpeed.Visible = !_main.UiIsDeveloperMode && _main.UiCurrentSpeed != Game.Simulation.SimulationClock.SpeedLevel.Maximum;
        _expeditionSpeed.Text = _main.UiIsPaused ? "Continue at 8×" : "Fast-forward at 8×";
        var viewport = GetViewport().GetVisibleRect().Size;
        // This strip is hidden behind the drawer. Do not narrow it while hidden:
        // HFlow's wrapped minimum height otherwise survives reopening and blocks the map.
        var available = viewport.X - 136;
        _strip.Position = new Vector2(120, 112);
        _strip.Size = new Vector2(Mathf.Max(1, Mathf.Min(570, available)), 50);
        var state = _main.UiDemoObjective;
        if (state is null) return;
        _objective.Text = _main.UiDashboard.DemoStep >= 3
            ? "First-colony milestone complete. Save or continue building your civilization."
            : state.Objective;
        _research.Text = state.Research;
        _construction.Text = state.Construction;
        var currentStep = _main.UiDashboard.DemoStep;
        for (var i = 0; i < _steps.Length; i++)
        {
            _steps[i].Modulate = i < currentStep ? VisualUi.Accent : i == currentStep ? VisualUi.Gold : VisualUi.Muted;
            _steps[i].TooltipText = i == currentStep ? state.Objective : _steps[i].Text;
        }
    }

    private void OnSectionChanged(string? _) => RefreshVisibility();

    private void RefreshVisibility() =>
        // The opening objective is the player's persistent compass. Keeping it available
        // at every map scale also prevents a smooth camera transition from taking the
        // Guide button away while the player is trying to open it.
        _strip.Visible = !_sidebar.IsDrawerOpen && !_main.UiIsSystemSpatialView;
}
