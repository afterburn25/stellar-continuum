using System;
using System.Linq;
using Godot;
using Game.Simulation.Economy;
using Game.Simulation.Models;

namespace Game.Presentation;

/// <summary>Compact graphical command shell; all commands delegate to Main.</summary>
public partial class PlayerControls : CanvasLayer
{
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    private PanelContainer _topBar = null!;
    private PanelContainer _statusPanel = null!;
    private Label _identity = null!;
    private Label _date = null!;
    private Label _credits = null!;
    private Label _currencyName = null!;
    private Label _industry = null!;
    private Label _science = null!;
    private Label _statusLabel = null!;
    private PlaybackControl _playback = null!;
    private Button _developerTools = null!;
    private Button _notificationButton = null!;
    private NotificationCenter _notificationCenter = null!;
    private ActionFeedbackEffects _actionEffects = null!;
    private long _lastEffectNotificationSequence;
    private long _lastReadNotificationSequence;
    private ProjectCard _research = null!;
    private ResearchWorkspaceView _researchWorkspace = null!;
    private ProjectCard _construction = null!;
    private ProjectCard _shipyard = null!;
    private Label _economyBalance = null!;
    private Label _economyIncome = null!;
    private Label _economyCosts = null!;
    private Label _economyNet = null!;
    private Label _economyMaterials = null!;
    private Label _economyMaterialRate = null!;
    private Label _economyStatus = null!;
    private Label _industryPriorityStatus = null!;
    private readonly System.Collections.Generic.Dictionary<IndustryPriority, Button> _industryPriorityButtons = new();
    private readonly System.Collections.Generic.Dictionary<string, Label> _economyFlowValues = new(StringComparer.Ordinal);
    private VBoxContainer _fleetList = null!;
    private TextureRect _playerSpeciesPortrait = null!;
    private Label _campaignCivilization = null!;
    private Label _campaignSpecies = null!;
    private readonly System.Collections.Generic.Dictionary<int, Label> _fleetLabels = new();
    private double _refreshTimer;
    private EmpireOverviewPanel _overview = null!;
    private OrbitalConstructionPanel _orbital = null!;

    public override void _Ready()
    {
        _main = GetParent() as Main ?? throw new InvalidOperationException("PlayerControls must be a child of Main.");
        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        Layer = 6;
        BuildTopBar();
        _actionEffects = new ActionFeedbackEffects { Name = "ActionFeedbackEffects", ZIndex = -1 };
        AddChild(_actionEffects);
        BuildNotificationCenter();
        BuildActionDock();
        _overview = new EmpireOverviewPanel(); AddChild(_overview);
        _orbital = new OrbitalConstructionPanel(_main); AddChild(_orbital);
        BuildEconomyPage();
        _research = BuildProject("research", "RESEARCH", VisualIconLibrary.Research);
        _researchWorkspace = new ResearchWorkspaceView { Name = "ResearchWorkspace" };
        _researchWorkspace.Start += _main.UiStartResearch;
        _researchWorkspace.Pause += _main.UiPauseResearch;
        _researchWorkspace.Resume += _main.UiResumeResearch;
        _researchWorkspace.CloseRequested += _sidebar.CloseDrawer;
        AddChild(_researchWorkspace);
        _sidebar.SectionChanged += section =>
        {
            if (section == "research")
            {
                _researchWorkspace.UpdateWorkspace(
                    _main.UiResearchHorizon,
                    _main.UiResearchLockedPreview,
                    _main.UiResearchHorizonEdges);
                _researchWorkspace.Open();
            }
            else _researchWorkspace.Visible = false;
        };
        _construction = BuildProject("industry", "CONSTRUCTION", VisualIconLibrary.Construction);
        _shipyard = BuildProject("ships", "SHIPYARD", VisualIconLibrary.NavShips);
        BuildFleetOverview();
        BuildCampaignMenu();
        GetViewport().SizeChanged += UpdateBounds;
        UpdateBounds();
        RefreshState();
    }

    public override void _ExitTree() => GetViewport().SizeChanged -= UpdateBounds;

    public override void _Process(double delta)
    {
        _refreshTimer += delta;
        if (_refreshTimer < 0.2) return;
        _refreshTimer = 0;
        RefreshState();
    }

    private void BuildTopBar()
    {
        _topBar = new PanelContainer { Name = "ResourceBar", MouseFilter = Control.MouseFilterEnum.Stop };
        VisualUi.ContainPointerInput(_topBar);
        _topBar.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 10));
        var row = new HBoxContainer();
        row.AddThemeConstantOverride("separation", 20);
        _topBar.AddChild(row);
        var identity = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        identity.AddThemeConstantOverride("separation", 0);
        _identity = VisualUi.Text("PLAYER MODE", 15);
        _identity.Name = "CampaignModeBadge";
        _identity.MouseFilter = Control.MouseFilterEnum.Pass;
        _date = VisualUi.Text("", 11, VisualUi.Muted);
        _date.TextOverrunBehavior = TextServer.OverrunBehavior.TrimEllipsis;
        identity.AddChild(_identity);
        identity.AddChild(_date);
        row.AddChild(identity);
        _credits = AddResource(row, "CURRENCY", VisualIconLibrary.Credits, VisualUi.Gold, out _currencyName);
        _credits.CustomMinimumSize = new Vector2(142, 0);
        _credits.AddThemeFontSizeOverride("font_size", 14);
        _industry = AddResource(row, "MATERIALS", VisualIconLibrary.Industry, VisualUi.Accent, out _);
        _industry.CustomMinimumSize = new Vector2(132, 0);
        _science = AddResource(row, "LABS", VisualIconLibrary.Science, new Color("b4a0e4"), out _);
        var time = new HBoxContainer();
        time.AddThemeConstantOverride("separation", 3);
        _notificationButton = VisualUi.Button("0", "Open recent research, construction, mission, colony and combat events.",
            ToggleNotificationCenter, VisualIconLibrary.Info);
        _notificationButton.Name = "NotificationToggle";
        _notificationButton.CustomMinimumSize = new Vector2(50, 36);
        time.AddChild(_notificationButton);
        _playback = new PlaybackControl("SimulationPlayback",
            () => new PlaybackState(_main.UiIsPaused, _main.UiCurrentSpeed, _main.UiResumeSpeed, _main.UiIsDeveloperMode),
            _main.UiCyclePlayback, _main.UiTogglePause);
        time.AddChild(_playback);
        row.AddChild(time);
        AddChild(_topBar);
    }

    private void BuildNotificationCenter()
    {
        _notificationCenter = new NotificationCenter();
        _notificationCenter.Build(() => _notificationCenter.Visible = false);
        AddChild(_notificationCenter);
    }

    private void ToggleNotificationCenter()
    {
        _notificationCenter.Visible = !_notificationCenter.Visible;
        if (_notificationCenter.Visible && _main.UiNotifications.Count > 0)
            _lastReadNotificationSequence = _main.UiNotifications[^1].Sequence;
        RefreshNotifications();
    }

    private static Label AddResource(Container row, string name, Texture2D icon, Color color, out Label nameLabel)
    {
        var group = new HBoxContainer();
        group.AddThemeConstantOverride("separation", 7);
        group.AddChild(VisualUi.Icon(icon, 25));
        var values = new VBoxContainer();
        values.AddThemeConstantOverride("separation", 0);
        nameLabel = VisualUi.Text(name, 9, VisualUi.Muted);
        values.AddChild(nameLabel);
        var amount = VisualUi.Text("0", 17, color);
        amount.CustomMinimumSize = new Vector2(98, 0);
        amount.MouseFilter = Control.MouseFilterEnum.Pass;
        values.AddChild(amount);
        group.AddChild(values);
        row.AddChild(group);
        return amount;
    }

    private void BuildActionDock()
    {
        _statusPanel = new PanelContainer { Name = "CommandFeedback" };
        VisualUi.ContainPointerInput(_statusPanel);
        _statusPanel.AddThemeStyleboxOverride("panel", new StyleBoxEmpty());
        // Route assessments can include a metric distance and its astronomical context.
        // Reserve a few lines so an actionable denial remains readable at small viewports;
        // the tooltip retains the complete message when a route has many legs.
        _statusLabel = VisualUi.Text("", 12, VisualUi.Muted, wrap: true);
        _statusLabel.MouseFilter = Control.MouseFilterEnum.Pass;
        _statusPanel.AddChild(_statusLabel);
        AddChild(_statusPanel);
    }

    private ProjectCard BuildProject(string section, string category, Texture2D icon)
    {
        var panel = new PanelContainer { Name = category + "Card" };
        var card = new ProjectCard();
        panel.AddChild(card);
        card.Build(icon, category);
        if (section != "research")
        {
            var details = VisualUi.Text(section == "ships" ? "Ships require warp capability and an Orbital Shipyard. A colony ship also carries colonists." : "Research and construction can run together. Choose an available project, then start it.", 12, VisualUi.Muted, wrap: true);
            card.AddChild(details);
        }
        _sidebar.RegisterSection(section, panel);
        return card;
    }

    private void BuildEconomyPage()
    {
        var panel = new PanelContainer { Name = "EconomyPage" };
        var body = new VBoxContainer();
        body.AddThemeConstantOverride("separation", 16);
        panel.AddChild(body);

        var heading = new HBoxContainer();
        heading.AddThemeConstantOverride("separation", 14);
        heading.AddChild(VisualUi.Icon(VisualIconLibrary.Credits, 54));
        var headingText = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        headingText.AddChild(VisualUi.Text("SOVEREIGN TREASURY", 22, VisualUi.Gold));
        headingText.AddChild(VisualUi.Text("Live civilian revenue and operating commitments", 12, VisualUi.Muted));
        heading.AddChild(headingText);
        body.AddChild(heading);

        var cards = new GridContainer { Columns = 2 };
        cards.AddThemeConstantOverride("h_separation", 12);
        cards.AddThemeConstantOverride("v_separation", 12);
        _economyBalance = AddEconomyCard(cards, "RESERVES", VisualUi.Gold);
        _economyNet = AddEconomyCard(cards, "NET / DAY", VisualUi.Accent);
        _economyIncome = AddEconomyCard(cards, "INCOME / DAY", new Color("8fe5b1"));
        _economyCosts = AddEconomyCard(cards, "COSTS / DAY", new Color("ee9a91"));
        _economyMaterials = AddEconomyCard(cards, "MATERIALS IN STORAGE", VisualUi.Accent);
        _economyMaterialRate = AddEconomyCard(cards, "MATERIALS / DAY", VisualUi.Accent);
        _economyBalance.Name = "EconomyReserves";
        _economyNet.Name = "EconomyNetFlow";
        _economyIncome.Name = "EconomyGrossIncome";
        _economyCosts.Name = "EconomyOperatingCosts";
        _economyMaterials.Name = "EconomyMaterials";
        _economyMaterialRate.Name = "EconomyMaterialRate";
        body.AddChild(cards);
        _economyStatus = VisualUi.Text("", 13, VisualUi.Accent, wrap: true);
        _economyStatus.Name = "TreasuryHealth";
        body.AddChild(_economyStatus);

        body.AddChild(VisualUi.Text("INDUSTRIAL PRIORITY", 14, VisualUi.Accent));
        _industryPriorityStatus = VisualUi.Text("", 12, VisualUi.Muted, wrap: true);
        _industryPriorityStatus.Name = "IndustryPriorityStatus";
        body.AddChild(_industryPriorityStatus);
        var priorities = new HFlowContainer { Name = "IndustryPriorityControls" };
        foreach (var item in new[]
                 {
                     (IndustryPriority.Balanced, "Balanced", "Split competing infrastructure and shipbuilding demand 1:1."),
                     (IndustryPriority.InfrastructureFirst, "Infrastructure first", "Favor infrastructure 3:1 when both demands compete."),
                     (IndustryPriority.ShipbuildingFirst, "Shipbuilding first", "Favor shipbuilding 3:1 when both demands compete."),
                 })
        {
            var button = VisualUi.Button(item.Item2, item.Item3, () => _main.UiSetIndustryPriority(item.Item1));
            button.Name = "IndustryPriority_" + item.Item1;
            button.ToggleMode = true;
            priorities.AddChild(button); _industryPriorityButtons[item.Item1] = button;
        }
        body.AddChild(priorities);

        body.AddChild(VisualUi.Text("DAILY CASH FLOW", 14, VisualUi.Accent));
        body.AddChild(VisualUi.Text("INCOME", 10, new Color("8fe5b1")));
        var income = BuildEconomyFlowGrid("EconomyIncomeBreakdown");
        AddEconomyFlowRow(income, "colony", "Colony economy", new Color("8fe5b1"));
        AddEconomyFlowRow(income, "trade", "Surface trade", new Color("8fe5b1"));
        body.AddChild(income);
        body.AddChild(VisualUi.Text("OPERATING COSTS", 10, new Color("ee9a91")));
        var costs = BuildEconomyFlowGrid("EconomyCostBreakdown");
        AddEconomyFlowRow(costs, "administration", "Colony administration", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "population", "Population services", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "habitat", "Habitat support", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "fleet", "Fleet operations", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "orbital", "Orbital maintenance", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "surface", "Surface maintenance", new Color("ee9a91"));
        AddEconomyFlowRow(costs, "research", "Research programs", new Color("ee9a91"));
        body.AddChild(costs);
        body.AddChild(VisualUi.Text(
            "Every civilization begins with its own sovereign currency. Trade hubs add revenue while they have enough surface power. Construction and ship orders are one-time capital costs; active research programs have a continuing daily cost.",
            12, VisualUi.Muted, wrap: true));
        _sidebar.RegisterSection("economy", panel);
    }

    private static GridContainer BuildEconomyFlowGrid(string name)
    {
        var grid = new GridContainer { Name = name, Columns = 2, SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        grid.AddThemeConstantOverride("h_separation", 24);
        grid.AddThemeConstantOverride("v_separation", 6);
        return grid;
    }

    private void AddEconomyFlowRow(GridContainer grid, string key, string title, Color color)
    {
        var label = VisualUi.Text(title.ToUpperInvariant(), 12, VisualUi.Muted);
        label.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
        grid.AddChild(label);
        var value = VisualUi.Text("0 / DAY", 13, color);
        value.Name = "EconomyFlow_" + key;
        value.HorizontalAlignment = HorizontalAlignment.Right;
        value.CustomMinimumSize = new Vector2(130, 0);
        grid.AddChild(value);
        _economyFlowValues.Add(key, value);
    }

    private static Label AddEconomyCard(GridContainer grid, string title, Color color)
    {
        var card = new PanelContainer { CustomMinimumSize = new Vector2(320, 92), SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 12));
        var content = new VBoxContainer();
        content.AddChild(VisualUi.Text(title, 11, VisualUi.Muted));
        var value = VisualUi.Text("0", 23, color);
        content.AddChild(value);
        card.AddChild(content);
        grid.AddChild(card);
        return value;
    }

    private void BuildCampaignMenu()
    {
        var panel = new PanelContainer { Name = "CampaignMenu" };
        var body = new VBoxContainer();
        body.AddThemeConstantOverride("separation", 12);
        panel.AddChild(body);
        body.AddChild(VisualUi.Icon(VisualIconLibrary.NavGalaxy, 72));
        body.AddChild(VisualUi.Text("STELLAR CONTINUUM", 21));
        body.AddChild(VisualUi.Text(_main.UiBuildLabel, 12, VisualUi.Muted, wrap: true));
        var identity = new HBoxContainer(); identity.AddThemeConstantOverride("separation", 14);
        _playerSpeciesPortrait = new TextureRect
        {
            Name = "PlayerSpeciesPortrait",
            Texture = VisualIconLibrary.Get(CivilizationArtworkLibrary.PathForSpecies(_main.UiPlayerSpeciesId)),
            CustomMinimumSize = new Vector2(126, 126),
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
            MouseFilter = Control.MouseFilterEnum.Ignore,
        };
        identity.AddChild(_playerSpeciesPortrait);
        var identityText = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        _campaignCivilization = VisualUi.Text("CIVILIZATION INITIALIZING", 19, Colors.White, true);
        _campaignSpecies = VisualUi.Text("SPECIES INITIALIZING", 11, VisualUi.Accent);
        _campaignCivilization.Name = "CampaignCivilizationName";
        _campaignSpecies.Name = "CampaignSpeciesName";
        identityText.AddChild(_campaignCivilization);
        identityText.AddChild(_campaignSpecies);
        identityText.AddChild(VisualUi.Text("Home civilization · Earth, Sol", 12, VisualUi.Muted, true));
        identity.AddChild(identityText); body.AddChild(identity);
        BuildLeadershipCouncil(body);
        body.AddChild(VisualUi.Button("Save", "Save this campaign in its own slot.", _main.UiSave, VisualIconLibrary.Save));
        var campaignMenu = VisualUi.Button("Campaign & modes", "Pause, resume, switch Player/Developer mode, or create a campaign.", _main.UiOpenMenu, VisualIconLibrary.NavMenu);
        campaignMenu.Name = "CampaignMenu";
        body.AddChild(campaignMenu);
        _developerTools = VisualUi.Button("Developer tools", "Explicit testing actions, available only in Developer mode.", _main.UiOpenDeveloperTools, VisualIconLibrary.Construction);
        _developerTools.Name = "DeveloperToolsShortcut";
        body.AddChild(_developerTools);
        body.AddChild(VisualUi.Button("New Player campaign", "Review confirmation before creating a fresh Player campaign.", _main.UiNewCampaign));
        body.AddChild(VisualUi.Button("Support Bundle", "Export game diagnostics and the available campaign save.", _main.UiExportDiagnostics, VisualIconLibrary.Support));
        body.AddChild(VisualUi.Text("Left-drag: pan · wheel: zoom through map scales\nLeft-click: select a star or world\nBackspace: previous view · Space: pause · F6: save", 12, VisualUi.Muted, wrap: true));
        _sidebar.RegisterSection("menu", panel);
    }

    private static void BuildLeadershipCouncil(Container parent)
    {
        parent.AddChild(VisualUi.Text("LEADERSHIP COUNCIL", 12, VisualUi.Accent));
        var grid = new ResponsiveGrid { Name = "LeadershipCouncil", Columns = 2, ReferenceColumns = 3, CompactColumns = 2, SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        grid.AddThemeConstantOverride("h_separation", 9);
        AddLeaderCard(grid, "Civil Administration", "Planetary development and public services", CivilizationArtworkLibrary.PlanetaryGovernor);
        AddLeaderCard(grid, "Science Directorate", "Research institutions and discovery", CivilizationArtworkLibrary.ChiefScientist);
        AddLeaderCard(grid, "Fleet Command", "Exploration, defense and fleet operations", CivilizationArtworkLibrary.FleetCommander);
        parent.AddChild(grid);
    }

    private static void AddLeaderCard(Container parent, string role, string responsibility, string artworkPath)
    {
        var card = new PanelContainer { CustomMinimumSize = new Vector2(210, 238), SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
        card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 7));
        var body = new VBoxContainer(); body.AddThemeConstantOverride("separation", 5); card.AddChild(body);
        body.AddChild(new TextureRect
        {
            Name = "LeaderPortrait_" + role.Replace(" ", string.Empty),
            Texture = VisualIconLibrary.Get(artworkPath),
            CustomMinimumSize = new Vector2(0, 158),
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
            MouseFilter = Control.MouseFilterEnum.Ignore,
        });
        body.AddChild(VisualUi.Text(role.ToUpperInvariant(), 11, Colors.White, true));
        var detail = VisualUi.Text(responsibility, 10, VisualUi.Muted, true); detail.MaxLinesVisible = 2; body.AddChild(detail);
        parent.AddChild(card);
    }

    private void BuildFleetOverview()
    {
        _shipyard.AddChild(VisualUi.Text("ACTIVE FLEETS", 12, VisualUi.Accent));
        _fleetList = new VBoxContainer();
        _fleetList.AddThemeConstantOverride("separation", 7);
        _shipyard.AddChild(_fleetList);
    }

    private void RefreshFleetOverview()
    {
        var fleets = _main.UiOwnedFleets;
        foreach (var staleId in _fleetLabels.Keys.Where(id => !System.Array.Exists(fleets, fleet => fleet.FleetId == id)).ToArray())
        {
            _fleetLabels[staleId].GetParent().QueueFree();
            _fleetLabels.Remove(staleId);
        }
        foreach (var fleet in fleets)
        {
            if (!_fleetLabels.TryGetValue(fleet.FleetId, out var label))
            {
                var row = new HBoxContainer();
                row.AddThemeConstantOverride("separation", 8);
                var portrait = new TextureRect
                {
                    Name = "FleetArtwork_" + fleet.FleetId,
                    Texture = VisualIconLibrary.Get(fleet.ArtworkPath),
                    CustomMinimumSize = new Vector2(82, 64),
                    ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
                    StretchMode = TextureRect.StretchModeEnum.KeepAspectCovered,
                    MouseFilter = Control.MouseFilterEnum.Ignore,
                };
                row.AddChild(portrait);
                row.AddChild(VisualUi.Icon(FleetIcon(fleet.Role), 30));
                label = VisualUi.Text("", 13, Colors.White, wrap: true);
                label.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
                row.AddChild(label);
                if (fleet.IsArmed)
                {
                    var orders = new HFlowContainer();
                    orders.AddThemeConstantOverride("h_separation", 4);
                    orders.AddChild(VisualUi.Button("Deploy", "Deploy this ship to the star currently selected on the strategic map.",
                        () => _main.UiDeployMilitaryFleet(fleet.FleetId), VisualIconLibrary.NavGalaxy));
                    orders.AddChild(VisualUi.Button("Engage", "Attack a detected hostile fleet in this ship's current system. Target selection remains inside the observer-safe combat runtime.",
                        () => _main.UiEngageHostiles(fleet.FleetId), VisualIconLibrary.PatrolCorvette));
                    orders.AddChild(VisualUi.Button("Hold", "Cancel the current tactical order and hold position.",
                        () => _main.UiIssueMilitaryOrder(fleet.FleetId, Game.Simulation.Combat.MilitaryOrderType.Hold)));
                    orders.AddChild(VisualUi.Button("Defend", "Defend the fleet's current star system.",
                        () => _main.UiIssueMilitaryOrder(fleet.FleetId, Game.Simulation.Combat.MilitaryOrderType.Defend)));
                    orders.AddChild(VisualUi.Button("Retreat", "Attempt to disengage from combat.",
                        () => _main.UiIssueMilitaryOrder(fleet.FleetId, Game.Simulation.Combat.MilitaryOrderType.Retreat)));
                    row.AddChild(orders);
                }
                row.AddChild(VisualUi.Button("Locate", "Return to the map and center this fleet's current system.",
                    () => _main.UiFocusOwnedFleet(fleet.FleetId), VisualIconLibrary.NavGalaxy));
                _fleetList.AddChild(row);
                _fleetLabels.Add(fleet.FleetId, label);
            }
            var route = fleet.RemainingRouteLegs > 0
                ? $"  ·  {fleet.RemainingRouteLegs} leg{(fleet.RemainingRouteLegs == 1 ? string.Empty : "s")} / {MetricFormat.InterstellarLength(fleet.RemainingRouteDistanceLightYears)} remaining"
                : string.Empty;
            label.Text = $"{fleet.Name}  ·  {fleet.DesignName}\n{fleet.Activity}  ·  {fleet.Location}{route}\n" +
                $"Speed {MetricFormat.InterstellarSpeed(fleet.StrategicSpeed)}  ·  Leg range {MetricFormat.InterstellarLength(fleet.MaximumLegRangeLightYears)}  ·  {_main.UiFormatMoneyRate(-fleet.OperatingCostPerDay)}" +
                $"\nFuel endurance {MetricFormat.InterstellarLength(fleet.FuelRemainingLightYears)} / {MetricFormat.InterstellarLength(fleet.FuelCapacityLightYears)}" +
                (fleet.CargoMaterialCapacity > 0.0 ? $"\nMaterial cargo {fleet.CargoMaterials:0.#} / {fleet.CargoMaterialCapacity:0.#}  ·  transfer {fleet.CargoTransferRatePerDay:0.#}/day" : string.Empty) +
                $"\nCombat power {fleet.CombatPower:N0}" +
                (fleet.IsArmed ? $"  ·  Integrity {fleet.Integrity:P0}  ·  Order {fleet.MilitaryOrder}" : string.Empty);
        }
    }

    private static Texture2D FleetIcon(Game.Simulation.Models.FleetRole role) => role switch
    {
        Game.Simulation.Models.FleetRole.Scout => VisualIconLibrary.Scout,
        Game.Simulation.Models.FleetRole.Science => VisualIconLibrary.ScienceVessel,
        Game.Simulation.Models.FleetRole.Colony => VisualIconLibrary.ColonyShip,
        Game.Simulation.Models.FleetRole.Military => VisualIconLibrary.PatrolCorvette,
        _ => VisualIconLibrary.NavShips,
    };

    private void RefreshState()
    {
        _overview.Refresh(_main, _sidebar.IsDrawerOpen);
        _orbital.Refresh(_main, _sidebar.IsDrawerOpen);
        var state = _main.UiDashboard;
        _campaignCivilization.Text = state.CivilizationName.ToUpperInvariant();
        _campaignSpecies.Text = _main.UiPlayerSpeciesName.ToUpperInvariant();
        _playerSpeciesPortrait.Texture = VisualIconLibrary.Get(
            CivilizationArtworkLibrary.PathForSpecies(_main.UiPlayerSpeciesId));
        _identity.Text = _main.UiModeLabel.ToUpperInvariant() + (_main.UiIsDeveloperMode && _main.UiDeveloperToolsUsed ? " · TOOLS USED" : "");
        _identity.Modulate = _main.UiIsDeveloperMode ? VisualUi.Gold : VisualUi.Accent;
        _identity.TooltipText = _main.UiIsDeveloperMode
            ? "Developer mode uses its own saves. " + (_main.UiDeveloperToolsUsed ? "Development actions have been used in this campaign." : "No development actions have been used in this campaign.")
            : "Player mode follows ordinary rules and uses a separate save from Developer campaigns.";
        _date.Text = state.Date + "  ·  " + state.CivilizationName;
        _currencyName.Text = _main.UiCurrency.Name.ToUpperInvariant();
        _credits.Text = _main.UiFormatMoney(state.Credits);
        _industry.Text = $"{state.Industry:N0}/{state.IndustryCapacity:N0}";
        _science.Text = $"{state.FreeResearchLabs:N0}/{state.TotalResearchLabs:N0}";
        _credits.TooltipText = $"{_main.UiCurrency.Name} reserves: {_main.UiFormatMoney(state.Credits)}. Net cash flow after colony administration and active-fleet operations: {_main.UiFormatMoneyRate(state.CreditsPerDay)}. Construction, ships, surface buildings, and colony expeditions require available treasury funds.";
        _industry.TooltipText = $"Processed industrial materials: {state.Industry:N1} of {state.IndustryCapacity:N1} in storage. Production: {state.IndustryPerDay:N2}/day before construction and shipbuilding consumption. Mines, fabricators, and freight replenish this finite stock; idle output is curtailed when storage is full.";
        _science.TooltipText = $"Effective Research Labs: {state.FreeResearchLabs:N1} free of {state.TotalResearchLabs:N1} total. Assign labs to active research programs; capacity does not accumulate over time.";
        var flow = _main.UiCreditFlow;
        _economyBalance.Text = _main.UiFormatMoney(state.Credits);
        _economyIncome.Text = _main.UiFormatMoneyRate(flow.GrossIncomePerDay);
        _economyCosts.Text = _main.UiFormatMoneyRate(-flow.OperatingCostsPerDay);
        _economyNet.Text = _main.UiFormatMoneyRate(flow.NetCreditsPerDay);
        _economyMaterials.Text = $"{state.Industry:N0} / {state.IndustryCapacity:N0}";
        _economyMaterialRate.Text = $"{state.IndustryPerDay:+0.00;-0.00;0.00} / DAY";
        _economyMaterials.TooltipText = "Processed industrial materials available to construction and shipyards. Storage is finite.";
        _economyMaterialRate.TooltipText = $"Current funded industrial output. At {_main.UiBaseOperationsFundingFraction:P0} operating funding, unpaid production is not created.";
        _economyNet.Modulate = flow.NetCreditsPerDay < 0 ? new Color("ee9a91") : VisualUi.Accent;
        var treasury = TreasuryHealth.Assess(state.Credits, flow.NetCreditsPerDay, _main.UiOperatingArrears);
        _economyStatus.Text = treasury.State switch
        {
            TreasuryHealthState.Surplus => "SURPLUS · Current income covers operating commitments.",
            TreasuryHealthState.Deficit => $"DEFICIT · Treasury runway {treasury.RunwayDays:0.#} days. Pause research, reduce fleet or surface upkeep, or add staffed revenue before reserves run out.",
            TreasuryHealthState.Depleted => "TREASURY DEPLETED · New authorizations are blocked. Pause research, reduce upkeep, or restore staffed revenue.",
            _ => $"OPERATING ARREARS · {_main.UiFormatMoney(_main.UiOperatingArrears)} unpaid · {_main.UiBaseOperationsFundingFraction:P0} of current base operations funded. New income repays arrears before rebuilding reserves.",
        };
        _economyStatus.Modulate = treasury.State == TreasuryHealthState.Surplus
            ? new Color("8fe5b1") : new Color("ee9a91");
        var priority = _main.UiIndustryPriority;
        foreach (var pair in _industryPriorityButtons)
            pair.Value.ButtonPressed = pair.Key == priority.Priority;
        _industryPriorityStatus.Text = priority.HasLastAllocation
            ? $"Current choice: {priority.DisplayName} ({priority.ConstructionWeight:0}:{priority.ShipbuildingWeight:0}). Most recent allocation: {priority.LastConstructionAllocated:0.0} materials to infrastructure and {priority.LastShipbuildingAllocated:0.0} to shipbuilding."
            : $"Current choice: {priority.DisplayName} ({priority.ConstructionWeight:0}:{priority.ShipbuildingWeight:0}). It applies when demand competes; spare materials go to other work. Infrastructure includes surface sites and empire projects.";
        _industryPriorityStatus.TooltipText = "Priority applies only while both consumers have demand. Spare materials go to other work; it does not reserve materials or promise an ETA.";
        _economyFlowValues["colony"].Text = _main.UiFormatMoneyRate(flow.ColonyRevenuePerDay);
        _economyFlowValues["trade"].Text = _main.UiFormatMoneyRate(flow.TradeRevenuePerDay);
        _economyFlowValues["administration"].Text = _main.UiFormatMoneyRate(-flow.AdministrationPerDay);
        _economyFlowValues["population"].Text = _main.UiFormatMoneyRate(-flow.PopulationServicesPerDay);
        _economyFlowValues["habitat"].Text = _main.UiFormatMoneyRate(-flow.HabitatSupportPerDay);
        _economyFlowValues["fleet"].Text = _main.UiFormatMoneyRate(-flow.FleetOperationsPerDay);
        _economyFlowValues["orbital"].Text = _main.UiFormatMoneyRate(-flow.OrbitalMaintenancePerDay);
        _economyFlowValues["surface"].Text = _main.UiFormatMoneyRate(-flow.SurfaceMaintenancePerDay);
        _economyFlowValues["research"].Text =
            $"{_main.UiFormatMoneyRate(-flow.ResearchOperationsPerDay)}  ·  {_main.UiFormatMoney(_main.UiRemainingResearchMilestoneCredits)} RESERVED";
        _economyFlowValues["research"].TooltipText =
            $"Active research authorization paid: {_main.UiFormatMoney(_main.UiActiveResearchAuthorizationCredits)}. " +
            $"Unspent prototype and validation commitments: {_main.UiFormatMoney(_main.UiRemainingResearchMilestoneCredits)}. " +
            $"Current funded operations: {_main.UiFormatMoneyRate(-flow.ResearchOperationsPerDay)}.";
        _statusLabel.Text = _main.UiStatusMessage;
        _statusLabel.TooltipText = _main.UiStatusMessage;
        RefreshNotifications();
        _developerTools.Disabled = !_main.UiIsDeveloperMode;
        _playback.Refresh();
        _research.UpdateDisplay(state.Research);
        _construction.UpdateDisplay(state.Construction);
        _shipyard.UpdateDisplay(state.Shipyard);
        if (_researchWorkspace.Visible)
            _researchWorkspace.UpdateWorkspace(
                _main.UiResearchHorizon,
                _main.UiResearchLockedPreview,
                _main.UiResearchHorizonEdges);
        _construction.UpdateChoices(_main.UiConstructionChoices, _main.UiQueueConstruction, _main.UiCancelConstruction);
        _shipyard.UpdateChoices(_main.UiShipChoices, _main.UiBuildShip, _main.UiCancelShipOrder);
        RefreshFleetOverview();
    }

    private void UpdateBounds()
    {
        var viewport = GetViewport().GetVisibleRect().Size;
        _topBar.Position = new Vector2(12, 12);
        _topBar.Size = new Vector2(viewport.X - 24, 56);
        ((HBoxContainer)_topBar.GetChild(0)).AddThemeConstantOverride("separation", viewport.X < 1440 ? 12 : 20);
        _statusPanel.Position = new Vector2(126, viewport.Y - 55);
        _statusPanel.Size = new Vector2(Mathf.Max(1, viewport.X - 150), 48);
        _notificationCenter.Position = new Vector2(Mathf.Max(112, viewport.X - 450), 78);
        _notificationCenter.Size = new Vector2(Mathf.Min(430, viewport.X - 128), Mathf.Min(470, viewport.Y - 210));
        _overview?.UpdateBounds();
    }

    private void RefreshNotifications()
    {
        var items = _main.UiNotifications;
        foreach (var item in items.Where(item => item.Sequence > _lastEffectNotificationSequence))
        {
            _lastEffectNotificationSequence = item.Sequence;
            _actionEffects.Trigger(item.Category);
            AudioDirector.PlayEvent(item.Category);
        }
        _notificationCenter.UpdateItems(items);
        var unread = items.Count(item => item.Sequence > _lastReadNotificationSequence);
        _notificationButton.Text = unread > 99 ? "99+" : unread.ToString();
        _notificationButton.Modulate = unread > 0 ? VisualUi.Gold : Colors.White;
        _notificationButton.TooltipText = unread > 0
            ? $"{unread} unread major event{(unread == 1 ? string.Empty : "s")}. Open recent events."
            : "Open recent research, construction, mission, colony and combat events.";
    }
}
