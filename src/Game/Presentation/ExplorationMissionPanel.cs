using System;
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Game.Presentation;

/// <summary>
/// Compact observer-safe exploration/colonization panel. It renders Main's already-filtered
/// presentation state. Colony selection is presentation-only; authoritative order validation and
/// mutation occur through Core when Settle Here is pressed.
/// </summary>
public partial class ExplorationMissionPanel : CanvasLayer
{
    private Main _main = null!;
    private CampaignSidebar _sidebar = null!;
    private Label _content = null!;
    private VBoxContainer _missionList = null!;
    private string _missionSignature = "not-rendered";
    private VBoxContainer _ownedColonies = null!;
    private sealed record OwnedColonyCard(
        PanelContainer Panel, Label Title, Label Population, Label Support,
        Label Infrastructure, Label Specialization, Button Land, Button Freight);

    private readonly Dictionary<int, OwnedColonyCard> _ownedColonyCards = new();
    private HFlowContainer _colonyControls = null!;
    private Label _actionStatus = null!;
    private Button _previousFleetButton = null!;
    private Button _nextFleetButton = null!;
    private Button _previousSiteButton = null!;
    private Button _nextSiteButton = null!;
    private Button _settleButton = null!;
    private double _refreshTimer;
    private bool _showColonySites;
    private int _selectedFleetIndex;
    private int _selectedSiteIndex;

    public override void _Ready()
    {
        _main = GetParent() as Main
            ?? throw new InvalidOperationException("ExplorationMissionPanel must be a child of Main.");

        _sidebar = _main.GetNode<CampaignSidebar>("CampaignSidebar");
        var panel = new PanelContainer { Name = "ExplorationPanel" };

        var root = new VBoxContainer();
        root.AddThemeConstantOverride("separation", 6);
        panel.AddChild(root);

        var header = new HFlowContainer();
        header.AddThemeConstantOverride("h_separation", 8);
        header.AddThemeConstantOverride("v_separation", 6);
        root.AddChild(header);

        header.AddChild(new Label
        {
            Text = "MISSIONS & SETTLEMENT",
            TooltipText = "Mission phases and colony opportunities come from observer-safe simulation read models. The panel does not calculate survey, biological suitability, or operational reach itself.",
        });

        var missionsButton = new Button
        {
            Text = "Missions",
            Icon = VisualIconLibrary.Exploration,
            TooltipText = "Show active scout, science, and colony mission phases and ETAs.",
            CustomMinimumSize = new Vector2(104, 28),
        };
        AudioDirector.Bind(missionsButton);
        missionsButton.Pressed += () =>
        {
            _showColonySites = false;
            _actionStatus.Text = string.Empty;
            RefreshContent();
        };
        header.AddChild(missionsButton);

        var colonyButton = new Button
        {
            Text = "Colony Sites",
            Icon = VisualIconLibrary.Colony,
            TooltipText = "Browse fully surveyed settlement opportunities for populated player colony ships. Suitability and reach come from shared simulation contracts.",
            CustomMinimumSize = new Vector2(128, 28),
        };
        AudioDirector.Bind(colonyButton);
        colonyButton.Pressed += () =>
        {
            _showColonySites = true;
            _actionStatus.Text = string.Empty;
            RefreshContent();
        };
        header.AddChild(colonyButton);

        _ownedColonies = new VBoxContainer { Visible = false };
        _ownedColonies.AddThemeConstantOverride("separation", 7);
        root.AddChild(_ownedColonies);

        _missionList = new VBoxContainer { Name = "MissionCards" };
        _missionList.AddThemeConstantOverride("separation", 8);
        root.AddChild(_missionList);

        _content = new Label
        {
            Text = "Exploration missions are initializing…",
            AutowrapMode = TextServer.AutowrapMode.WordSmart,
            CustomMinimumSize = new Vector2(0, 156),
            VerticalAlignment = VerticalAlignment.Top,
        };
        root.AddChild(_content);

        _colonyControls = new HFlowContainer
        {
            Visible = false,
        };
        _colonyControls.AddThemeConstantOverride("h_separation", 6);
        _colonyControls.AddThemeConstantOverride("v_separation", 6);
        root.AddChild(_colonyControls);

        _previousFleetButton = AddControlButton(_colonyControls, "← Ship", "Previous populated colony ship.", () =>
        {
            _selectedFleetIndex--;
            _selectedSiteIndex = 0;
            ClearActionAndRefresh();
        });
        _nextFleetButton = AddControlButton(_colonyControls, "Ship →", "Next populated colony ship.", () =>
        {
            _selectedFleetIndex++;
            _selectedSiteIndex = 0;
            ClearActionAndRefresh();
        });
        _previousSiteButton = AddControlButton(_colonyControls, "← Site", "Previous bounded colony-site candidate.", () =>
        {
            _selectedSiteIndex--;
            ClearActionAndRefresh();
        });
        _nextSiteButton = AddControlButton(_colonyControls, "Site →", "Next bounded colony-site candidate.", () =>
        {
            _selectedSiteIndex++;
            ClearActionAndRefresh();
        });
        _settleButton = AddControlButton(_colonyControls, "Select ship on map", "Navigate this ship directly on the map.", IssueSelectedColonyOrder, 150.0f, VisualIconLibrary.Colony);

        _actionStatus = new Label
        {
            Visible = false,
            AutowrapMode = TextServer.AutowrapMode.WordSmart,
            CustomMinimumSize = new Vector2(0, 38),
        };
        root.AddChild(_actionStatus);

        _sidebar.AddPanel(panel);
        _sidebar.SectionChanged += OnSectionChanged;
        RefreshContent();
    }

    public override void _ExitTree() => _sidebar.SectionChanged -= OnSectionChanged;

    private void OnSectionChanged(string? section)
    {
        if (section is not ("explore" or "colonies")) return;
        _showColonySites = section == "colonies";
        _actionStatus.Text = string.Empty;
        RefreshContent();
    }

    public override void _Process(double delta)
    {
        _refreshTimer += delta;
        if (_main is null || _content is null || _refreshTimer < 0.5)
            return;

        _refreshTimer = 0.0;
        RefreshContent();
    }

    private static Button AddControlButton(
        Container parent,
        string text,
        string tooltip,
        Action action,
        float width = 82.0f,
        Texture2D? icon = null)
    {
        var button = new Button
        {
            Text = text,
            Icon = icon,
            TooltipText = tooltip,
            CustomMinimumSize = new Vector2(width, 28),
        };
        AudioDirector.Bind(button);
        button.Pressed += action;
        parent.AddChild(button);
        return button;
    }

    private void ClearActionAndRefresh()
    {
        _actionStatus.Text = string.Empty;
        RefreshContent();
    }

    private void IssueSelectedColonyOrder()
    {
        var selection = _main.GetUiColonyOpportunityState(_selectedFleetIndex, _selectedSiteIndex);
        if (selection.FleetId is int fleetId) _main.UiFocusOwnedFleet(fleetId);
    }

    private void RefreshContent()
    {
        if (_main is null || _content is null)
            return;

        if (!_showColonySites)
        {
            RefreshMissionCards();
            _missionList.Visible = true;
            _content.Visible = false;
            _ownedColonies.Visible = false;
            _colonyControls.Visible = false;
            _actionStatus.Visible = false;
            return;
        }

        var selection = _main.GetUiColonyOpportunityState(_selectedFleetIndex, _selectedSiteIndex);
        RefreshOwnedColonies();
        _missionList.Visible = false;
        _ownedColonies.Visible = true;
        _content.Visible = true;
        _selectedFleetIndex = selection.FleetIndex;
        _selectedSiteIndex = selection.SiteIndex;
        _content.Text = selection.Details;
        _colonyControls.Visible = true;

        _previousFleetButton.Disabled = selection.FleetCount <= 1 || selection.FleetIndex <= 0;
        _nextFleetButton.Disabled = selection.FleetCount <= 1 || selection.FleetIndex >= selection.FleetCount - 1;
        _previousSiteButton.Disabled = selection.SiteCount <= 1 || selection.SiteIndex <= 0;
        _nextSiteButton.Disabled = selection.SiteCount <= 1 || selection.SiteIndex >= selection.SiteCount - 1;
        _settleButton.Disabled = selection.FleetId is null;
        _settleButton.Text = "Select ship on map";
        _settleButton.TooltipText = "Choose this ship, then right-click a star to travel. At the destination, right-click the chosen world to begin settlement.";

        _actionStatus.Visible = !string.IsNullOrWhiteSpace(_actionStatus.Text);
    }

    private void RefreshMissionCards()
    {
        var missions = _main.UiExplorationMissions;
        var signature = string.Join('|', missions.Select(mission =>
            $"{mission.FleetId}:{mission.Phase}:{mission.Destination}:{mission.Eta}:{mission.Summary}"));
        if (signature == _missionSignature) return;
        _missionSignature = signature;
        foreach (var child in _missionList.GetChildren()) child.QueueFree();

        if (missions.Length == 0)
        {
            var empty = new PanelContainer { Name = "NoActiveMissions" };
            empty.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 18));
            var row = new HBoxContainer();
            row.AddThemeConstantOverride("separation", 16);
            empty.AddChild(row);
            row.AddChild(VisualUi.Icon(VisualIconLibrary.Exploration, 70));
            var text = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
            text.AddThemeConstantOverride("separation", 5);
            text.AddChild(VisualUi.Text("DEEP SPACE AWAITS", 19, VisualUi.Accent));
            text.AddChild(VisualUi.Text(
                "No active expedition. Select a scout or science ship, then right-click a star to send it.",
                13, VisualUi.Muted, wrap: true));
            row.AddChild(text);
            _missionList.AddChild(empty);
            return;
        }

        foreach (var mission in missions)
        {
            var card = new PanelContainer { Name = "Mission_" + mission.FleetId };
            card.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 12));
            var row = new HBoxContainer();
            row.AddThemeConstantOverride("separation", 13);
            card.AddChild(row);
            row.AddChild(VisualUi.Icon(MissionIcon(mission.Role), 48));
            var details = new VBoxContainer { SizeFlagsHorizontal = Control.SizeFlags.ExpandFill };
            details.AddThemeConstantOverride("separation", 4);
            row.AddChild(details);
            var heading = new HBoxContainer();
            var name = VisualUi.Text(mission.FleetName.ToUpperInvariant(), 16, Colors.White);
            name.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
            heading.AddChild(name);
            heading.AddChild(VisualUi.Text(mission.Phase.ToUpperInvariant(), 10, MissionColor(mission.Phase)));
            details.AddChild(heading);
            details.AddChild(VisualUi.Text($"{mission.Role.ToString().ToUpperInvariant()}  ·  {mission.Destination}  ·  {mission.Eta}",
                11, VisualUi.Gold, wrap: true));
            details.AddChild(VisualUi.Text(mission.Summary, 12, VisualUi.Muted, wrap: true));
            _missionList.AddChild(card);
        }
    }

    private static Texture2D MissionIcon(Game.Simulation.Models.FleetRole role) => role switch
    {
        Game.Simulation.Models.FleetRole.Scout => VisualIconLibrary.Scout,
        Game.Simulation.Models.FleetRole.Science => VisualIconLibrary.ScienceVessel,
        Game.Simulation.Models.FleetRole.Colony => VisualIconLibrary.ColonyShip,
        _ => VisualIconLibrary.Exploration,
    };

    private static Color MissionColor(string phase) => phase switch
    {
        "Traveling" => VisualUi.Accent,
        "Science survey" => new Color("b4a0e4"),
        "Reconnaissance ready" or "Settlement ready" => new Color("8fe5b1"),
        _ => VisualUi.Gold,
    };

    private void RefreshOwnedColonies()
    {
        var colonies = _main.UiOwnedColonies;
        foreach (var staleId in new List<int>(_ownedColonyCards.Keys))
        {
            if (Array.Exists(colonies, colony => colony.ColonyId == staleId)) continue;
            _ownedColonyCards[staleId].Panel.QueueFree();
            _ownedColonyCards.Remove(staleId);
        }
        if (_ownedColonyCards.Count == 0)
            _ownedColonies.AddChild(VisualUi.Text("OWNED WORLDS", 12, VisualUi.Accent));
        foreach (var colony in colonies)
        {
            if (!_ownedColonyCards.TryGetValue(colony.ColonyId, out var card))
            {
                var panel = new PanelContainer { Name = "OwnedColony_" + colony.ColonyId };
                panel.AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 11));
                var body = new VBoxContainer();
                body.AddThemeConstantOverride("separation", 5);
                panel.AddChild(body);
                var header = new HBoxContainer();
                header.AddThemeConstantOverride("separation", 7);
                var title = VisualUi.Text("", 17, Colors.White);
                title.SizeFlagsHorizontal = Control.SizeFlags.ExpandFill;
                header.AddChild(title);
                header.AddChild(VisualUi.Button("View", "Open this colony's orbital system and focus its world.",
                    () => _main.UiOpenOwnedColony(colony.ColonyId, false), VisualIconLibrary.NavSystem));
                var land = VisualUi.Button("Land", "Open the freely navigable colony surface and construction palette.",
                    () => _main.UiOpenOwnedColony(colony.ColonyId, true), VisualIconLibrary.Colony);
                header.AddChild(land);
                var freight = VisualUi.Button("Collect", "Dispatch an idle bulk freighter from a developed colony.",
                    () =>
                    {
                        _actionStatus.Text = _main.UiRequestOutpostFreight(colony.ColonyId);
                        RefreshContent();
                    }, VisualIconLibrary.Logistics);
                header.AddChild(freight);
                body.AddChild(header);
                var populationLabel = VisualUi.Text("", 12, VisualUi.Gold);
                var supportLabel = VisualUi.Text("", 12, VisualUi.Muted, wrap: true);
                var infrastructureLabel = VisualUi.Text("", 12, VisualUi.Accent);
                var specializationLabel = VisualUi.Text("", 11, VisualUi.Muted, wrap: true);
                body.AddChild(populationLabel);
                body.AddChild(supportLabel);
                body.AddChild(infrastructureLabel);
                body.AddChild(specializationLabel);
                _ownedColonies.AddChild(panel);
                card = new OwnedColonyCard(panel, title, populationLabel, supportLabel,
                    infrastructureLabel, specializationLabel, land, freight);
                _ownedColonyCards.Add(colony.ColonyId, card);
            }
            var population = colony.PopulationMillions >= 1
                ? $"{colony.PopulationMillions:N0}M"
                : $"{colony.PopulationMillions * 1000:N0}K";
            var habitatCost = colony.HabitatSupportReduction > 0
                ? $"{_main.UiFormatMoneyRate(-colony.HabitatSupportCreditsPerDay)} life support after {colony.HabitatSupportReduction:P0} local reduction (gross {_main.UiFormatMoneyRate(-colony.GrossHabitatSupportCreditsPerDay)})"
                : $"{_main.UiFormatMoneyRate(-colony.HabitatSupportCreditsPerDay)} life support";
            var availablePower = colony.SurfacePowerSupply + colony.StorageDischargePerDay;
            var powerState = colony.SurfacePowerDemand > availablePower ? "POWER SHORTAGE" : "power available";
            card.Title.Text = $"{colony.ColonyName.ToUpperInvariant()}   /   {colony.PlanetName}, {colony.SystemName}";
            card.Population.Text = $"{colony.SettlementScale.ToUpperInvariant()}   ·   {population} POPULATION   ·   {_main.UiFormatMoneyRate(-colony.AdministrationCreditsPerDay)} ADMIN";
            card.Support.Text = $"{colony.HabitatNeeds}   ·   {habitatCost}";
            card.Support.Text += $"\nFOOD {colony.FoodCapacityMillions:N0}M   ·   WATER {colony.WaterCapacityMillions:N0}M   ·   HOUSING {colony.HousingCapacityMillions:N0}M   ·   SUSTAINABLE POPULATION {colony.SupportedPopulationMillions:N0}M";
            card.Support.Text += $"\nRESERVES: FOOD {colony.FoodReserveDays:0.0} DAYS   ·   WATER {colony.WaterReserveDays:0.0} DAYS";
            if (colony.SustenanceSupportRatio < 1.0)
                card.Support.Text += $"\nSHORTAGE: {colony.LimitingSustenanceSupply.ToUpperInvariant()} SUPPORT AT {colony.SustenanceSupportRatio:P0}";
            card.Infrastructure.Text = $"{colony.BuildingCount} SURFACE BUILDINGS   ·   POWER {colony.SurfacePowerDemand:0.#} / {availablePower:0.#} GW   ·   {powerState}";
            if (colony.PowerStorageCapacityDays > 0.0)
                card.Infrastructure.Text += $"   ·   BATTERY {colony.StoredPowerDays * 24:0.#}/{colony.PowerStorageCapacityDays * 24:0.#} GWh" +
                    (colony.StorageDischargePerDay > 0.0 ? $" DISCHARGING {colony.StorageDischargePerDay:0.#} GW" :
                        colony.StorageChargePerDay > 0.0 ? $" CHARGING {colony.StorageChargePerDay:0.#} GW" : string.Empty);
            card.Infrastructure.Text += $"   ·   CARGO {colony.CargoTransferCapacityPerDay:0.#}/DAY";
            card.Infrastructure.Text += $"\nWORKFORCE {Math.Min(colony.WorkforceAvailableMillions, colony.WorkforceDemandMillions):N3}M / {colony.WorkforceDemandMillions:N3}M";
            card.Infrastructure.Text += $"   ·   EMPLOYED {colony.EmployedPopulationMillions:N0}M / {colony.WorkingAgePopulationMillions:N0}M ({colony.EmploymentRate:P0})";
            if (colony.DamagedBuildingCount > 0)
                card.Infrastructure.Text += $"\nCONDITION {colony.AverageBuildingCondition:P0}   ·   {colony.DamagedBuildingCount} NEED REPAIR" +
                    (colony.FailedBuildingCount > 0 ? $"   ·   {colony.FailedBuildingCount} OFFLINE" : string.Empty);
            if (colony.EnvironmentalWearMultiplier > 1.0001)
                card.Infrastructure.Text += $"   ·   MAINTENANCE EXPOSURE {colony.EnvironmentalWearMultiplier:0.00}×";
            if (colony.WorkforceDemandMillions > colony.WorkforceAvailableMillions + .0000001)
                card.Infrastructure.Text += "   ·   STAFF SHORTAGE";
            if (colony.SettlementScale == "Staffed resource outpost")
            {
                card.Infrastructure.Text += $"\n{colony.DepositGrade.ToUpperInvariant()} {colony.DepositMaterialName.ToUpperInvariant()}   ·   YIELD {colony.ExtractionYieldMultiplier:0.00}×   ·   ACCESS {colony.DepositAccessibility:P0}";
                card.Infrastructure.Text += $"\nEXTRACTION {colony.ExtractionPerDay:0.##}/DAY   ·   STORAGE {colony.StoredExtractedMaterials:0.#}/{colony.ExtractedMaterialCapacity:0.#}   ·   DEPOSIT {colony.RemainingDepositMaterials:0}/{colony.InitialDepositMaterials:0}\n{colony.OutpostOperationsStatus}";
            }
            card.Infrastructure.Modulate = colony.FailedBuildingCount > 0 || colony.SurfacePowerDemand > availablePower
                ? new Color("ee9a91") : VisualUi.Accent;
            card.Specialization.Text = $"{colony.SpecializationName.ToUpperInvariant()}   ·   {colony.SpecializationDescription}";
            card.Land.Disabled = !colony.CanLand;
            card.Freight.Visible = colony.SettlementScale == "Staffed resource outpost";
            card.Freight.Disabled = !colony.CanRequestFreight;
            card.Freight.TooltipText = colony.FreightActionReason;
        }
    }
}
