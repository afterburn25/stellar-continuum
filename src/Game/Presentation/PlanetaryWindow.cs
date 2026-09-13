using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation;
using Godot;

namespace Game.Presentation;

/// <summary>Slot-based planetary management. All orders and figures come from the campaign.</summary>
public sealed partial class PlanetaryWindow : Control
{
    private Func<UiSurfaceSnapshot?> _read = null!;
    private Func<int, string, UiSurfaceOrderResult> _build = null!;
    private Func<int, UiSurfaceOrderResult> _remove = null!, _upgrade = null!, _repair = null!;
    private Func<int, bool, UiSurfaceOrderResult> _enabled = null!, _priority = null!;
    private Func<UiSurfaceOrderResult> _command = null!;
    private readonly Dictionary<string, Label> _facts = new();
    private readonly Dictionary<int, Button> _slots = new();
    private readonly List<Action<UiSurfaceSnapshot>> _detailRefresh = new();
    private Label _title = null!, _subtitle = null!, _status = null!, _commandStatus = null!, _slotCount = null!, _alerts = null!;
    private Button _commandButton = null!;
    private GridContainer _grid = null!;
    private VBoxContainer _details = null!;
    private TabContainer _tabs = null!;
    private ConfirmationDialog _demolition = null!;
    private int? _selectedSlot, _pendingRemoval;
    private int _colonyId = -1;
    private string _structure = "", _detailState = "";
    private double _refresh;
    public bool IsOpen { get; private set; }
    public Func<bool>? IsInputBlocked { get; set; }
    public Func<string>? ReadTimeLabel { get; set; }
    public Func<PlaybackState>? ReadPlaybackState { get; set; }
    public event Action? ReturnToOrbit, SaveRequested, PlaybackCycleRequested, PlaybackPauseRequested;
    private static readonly Color Muted = new("a0b8c8"), Teal = new("6ce5d2"), Bad = new("ff9c89"), White = new("ecf4fa");
    private bool Blocked => !IsOpen || IsInputBlocked?.Invoke() == true;

    public void Configure(Func<UiSurfaceSnapshot?> read, Func<int, string, UiSurfaceOrderResult> build,
        Func<int, UiSurfaceOrderResult> remove, Func<int, UiSurfaceOrderResult> upgrade,
        Func<int, UiSurfaceOrderResult> repair, Func<int, bool, UiSurfaceOrderResult> enabled,
        Func<int, bool, UiSurfaceOrderResult> priority, Func<UiSurfaceOrderResult> command)
    { _read = read; _build = build; _remove = remove; _upgrade = upgrade; _repair = repair; _enabled = enabled; _priority = priority; _command = command; }

    public override void _Ready()
    {
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); MouseFilter = MouseFilterEnum.Stop; FocusMode = FocusModeEnum.All;
        var background = new ColorRect { Color = new("07121c"), MouseFilter = MouseFilterEnum.Ignore };
        background.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); AddChild(background);
        var margin = new MarginContainer(); margin.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); AddChild(margin);
        foreach (var side in new[] { "left", "right", "top", "bottom" }) margin.AddThemeConstantOverride("margin_" + side, 16);
        var root = new VBoxContainer(); root.AddThemeConstantOverride("separation", 10); margin.AddChild(root);
        BuildHeader(root);
        var kpis = new HBoxContainer(); kpis.AddThemeConstantOverride("separation", 12); root.AddChild(kpis);
        Kpi(kpis, "population", "POPULATION"); Kpi(kpis, "power", "POWER BALANCE");
        Kpi(kpis, "income", "LOCAL CREDITS / DAY"); Kpi(kpis, "materials", "MATERIALS / DAY"); Kpi(kpis, "research", "RESEARCH LAB CAPACITY");
        var columns = new HBoxContainer { SizeFlagsVertical = SizeFlags.ExpandFill }; columns.AddThemeConstantOverride("separation", 10); root.AddChild(columns);
        var overviewPanel = new PanelContainer { CustomMinimumSize = new(224, 0) }; overviewPanel.AddThemeStyleboxOverride("panel", PanelStyle("102330")); columns.AddChild(overviewPanel);
        var overview = ScrollPanel(overviewPanel, "PlanetOverview", 0);
        overview.AddChild(Text("COMMAND CENTER", 15, Gold));
        overview.AddChild(_commandStatus = Text("", 13));
        overview.AddChild(_commandButton = ActionButton("Build Command Center", "PlanetaryCommand", () => Run(_command)));
        overview.AddChild(Text("Command Center occupies its own site. All other buildings use one slot each.", 12, Muted));
        overview.AddChild(new HSeparator()); overview.AddChild(Text("PLANET & POPULATION", 13, Teal));
        foreach (var (key, label) in new[] { ("system", "System"), ("species", "Population species"), ("stability", "Stability"), ("infrastructure", "Infrastructure"),
            ("gravity", "Surface gravity"), ("temperature", "Temperature"), ("pressure", "Pressure"), ("atmosphere", "Atmosphere"), ("solvent", "Surface solvent"),
            ("radius", "Radius / mass"), ("radiation", "Radiation hazard"), ("discoveries", "Survey features"), ("specialization", "Specialization"), ("wear", "Environmental wear"), ("construction", "Construction cost") }) Fact(overview, key, label);
        var centerPanel = new PanelContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill }; centerPanel.AddThemeStyleboxOverride("panel", PanelStyle()); columns.AddChild(centerPanel);
        var center = new VBoxContainer(); center.AddThemeConstantOverride("separation", 8); centerPanel.AddChild(center);
        center.AddChild(_slotCount = Text("SURFACE BUILDING SLOTS", 17, White));
        center.AddChild(Text("Develop your colony · Select a slot to build or manage", 12, Muted));
        var slotScroll = new ScrollContainer { Name = "PlanetarySlotScroll", SizeFlagsVertical = SizeFlags.ExpandFill, HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled };
        center.AddChild(slotScroll); _grid = new GridContainer { Columns = 3, SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _grid.AddThemeConstantOverride("h_separation", 10); _grid.AddThemeConstantOverride("v_separation", 10); slotScroll.AddChild(_grid);
        slotScroll.Resized += () => _grid.Columns = Math.Clamp((int)(slotScroll.Size.X / 150), 2, 6);
        var alertsScroll = new ScrollContainer { CustomMinimumSize = new(0, 48), HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled }; center.AddChild(alertsScroll);
        _alerts = Text("", 12, Muted); _alerts.SizeFlagsHorizontal = SizeFlags.ExpandFill; alertsScroll.AddChild(_alerts);
        var right = new VBoxContainer { CustomMinimumSize = new(304, 0) }; right.AddThemeConstantOverride("separation", 8); columns.AddChild(right);
        var queuePanel = new PanelContainer { CustomMinimumSize = new(0, 106) }; queuePanel.AddThemeStyleboxOverride("panel", PanelStyle("102830")); right.AddChild(queuePanel);
        var queueBox = new VBoxContainer(); queuePanel.AddChild(queueBox); queueBox.AddChild(_queueTitle = Text("CONSTRUCTION QUEUE", 13, Gold));
        var queueScroll = new ScrollContainer { Name = "PlanetaryBuildQueue", CustomMinimumSize = new(0, 58), SizeFlagsVertical = SizeFlags.ExpandFill, HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled }; queueBox.AddChild(queueScroll);
        _queue = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill }; queueScroll.AddChild(_queue);
        _tabs = new TabContainer { Name = "PlanetaryTabs", SizeFlagsVertical = SizeFlags.ExpandFill }; right.AddChild(_tabs);
        _tabs.AddThemeStyleboxOverride("panel", PanelStyle("102330"));
        _tabs.AddThemeStyleboxOverride("tab_selected", CardStyle("1a3b44", Teal, 2));
        _tabs.AddThemeStyleboxOverride("tab_unselected", PanelStyle("0c1c28"));
        _tabs.AddThemeColorOverride("font_selected_color", Teal);
        var economy = ScrollPanel(_tabs, "Economy", 0); economy.GetParent().Name = "Economy";
        economy.AddChild(Text("PRODUCTION & REQUIREMENTS", 13, Teal));
        economy.AddChild(Text("Daily rates use game days. Food, water and housing are population-support capacities.", 12, Muted));
        foreach (var (key, label) in new[] { ("tax", "Tax revenue / day"), ("trade", "Trade revenue / day"), ("upkeep", "Local operating need / day"), ("upkeepBreakdown", "Operating breakdown / day"),
            ("science", "Operational lab capacity"), ("buildDemand", "Queued construction requirements"), ("generation", "Power supply / demand"), ("battery", "Stored grid energy"), ("batteryFlow", "Charge / discharge per day"),
            ("food", "Food capacity / population"), ("foodBalance", "Food capacity balance"), ("water", "Water capacity / population"), ("waterBalance", "Water capacity balance"),
            ("housing", "Housing capacity / population"), ("housingBalance", "Housing balance"), ("reserves", "Food / water reserves"),
            ("workforce", "Available / required workers"), ("laborBalance", "Workforce balance"), ("employment", "Employment"), ("cargo", "Cargo handling / day"),
            ("deposit", "Resource deposit"), ("extraction", "Extraction / local storage"), ("funding", "Operations funded"), ("empire", "EMPIRE SHARED STORES"), ("empireFlow", "Empire cash flow / material production"), ("arrears", "Empire unpaid operations") }) Fact(economy, key, label);
        economy.AddChild(Text("Local credits show income minus required local costs. Fleet, orbital and research costs belong to the empire. Shared materials fund all construction.", 12, Muted));
        _details = ScrollPanel(_tabs, "Building", 0); _details.GetParent().Name = "Building";
        root.AddChild(_status = Text("Select a building slot to begin.", 12, Teal)); _status.MaxLinesVisible = 2; _status.Name = "PlanetaryStatus";
        _demolition = new ConfirmationDialog { Title = "Remove planetary building", OkButtonText = "Remove building" }; AddChild(_demolition);
        _demolition.Confirmed += () => { if (_pendingRemoval is int id) Run(() => _remove(id)); _pendingRemoval = null; };
        Visible = false; SetProcess(false);
    }

    public void Open()
    {
        IsOpen = true; Visible = true; SetProcess(true); _selectedSlot = null; _structure = ""; _detailState = "";
        Refresh(true); GrabFocus();
    }
    public void Close() { IsOpen = false; Visible = false; SetProcess(false); _demolition.Hide(); _pendingRemoval = null; }
    public override void _Process(double delta) { _refresh += delta; if (_refresh >= .35) { _refresh = 0; Refresh(false); } }
    public override void _UnhandledKeyInput(InputEvent @event)
    {
        if (Blocked || @event is not InputEventKey { Pressed: true, Echo: false } key) return;
        if (key.Keycode == Key.Escape) { ReturnToOrbit?.Invoke(); GetViewport().SetInputAsHandled(); }
        if (key.Keycode == Key.Space) { PlaybackPauseRequested?.Invoke(); GetViewport().SetInputAsHandled(); }
    }

    private void Refresh(bool force)
    {
        var s = _read(); if (s is null) return;
        if (_colonyId != s.ColonyId) { _colonyId = s.ColonyId; _selectedSlot = null; force = true; }
        var p = s.Planet;
        _title.Text = s.PlanetName;
        _subtitle.Text = $"{s.ColonyName}  ·  {(s.IsResourceOutpost ? "Resource outpost" : "Planetary colony")}  ·  {ReadTimeLabel?.Invoke()}";
        Set("population", Population(s.PopulationMillions)); Set("power", Signed(s.PowerSupply + s.StorageDischargePerDay - s.PowerDemand), s.PowerSupply + s.StorageDischargePerDay < s.PowerDemand);
        Set("income", p is null ? "—" : s.Currency.FormatRate(p.CreditFlow.NetCreditsPerDay), p?.CreditFlow.NetCreditsPerDay < 0);
        Set("materials", $"+{p?.IndustryPerDay ?? s.IndustryPerDay:0.00}"); Set("research", $"{s.SciencePerDay:0.##}");
        _commandStatus.Text = s.HubLevel == 0 ? "Not constructed\nBuild the Command Center to unlock the first surface building slots." : $"{s.HubName}\nLevel {s.HubLevel} · {s.BuildingCapacity} slots unlocked";
        if (s.HubUpgradeDaysRemaining > 0) _commandStatus.Text += $"\nConstruction: {s.HubUpgradeDaysRemaining:0.0} days remaining at full funding";
        else if (s.CanUpgradeHub) _commandStatus.Text += $"\n{s.Currency.Format(s.HubUpgradeCreditCost)} + {s.HubUpgradeIndustryCost:0} materials\n{s.HubUpgradeLockReason}";
        else _commandStatus.Text += s.IsResourceOutpost ? "\nConvert to a full colony for further expansion." : "\nMaximum Command Center level";
        _commandButton.Text = s.HubUpgradeDaysRemaining > 0 ? "Construction in progress" : s.HubLevel == 0 ? "Build Command Center" : $"Upgrade to level {s.HubLevel + 1}";
        _commandButton.Disabled = Blocked || !s.CanUpgradeHub || !s.CanAffordHubUpgrade || s.HubUpgradeDaysRemaining > 0;
        _commandButton.TooltipText = s.HubUpgradeLockReason ?? (!s.CanAffordHubUpgrade ? "Requires the listed credits and materials in empire stores." : "Slots unlock after construction completes.");
        _slotCount.Text = $"SURFACE SLOTS    {s.Buildings.Count} / {s.BuildingCapacity}";
        var structural = $"{s.ColonyId}:{s.BuildingCapacity}:" + string.Join(",", s.Buildings.Select(b => $"{b.Id}:{b.SlotIndex}:{b.TypeId}:{b.Complete}:{b.UpgradeDaysRemaining > 0}"));
        if (_structure != structural || force)
        {
            _structure = structural; Clear(_grid); _slots.Clear(); _slotCards.Clear();
            var shown = s.HubLevel == 0 ? (s.IsResourceOutpost ? 8 : 16) : s.BuildingCapacity;
            for (var slot = 0; slot < shown; slot++)
            {
                var button = CreateSlot(slot);
                _grid.AddChild(button); _slots[slot] = button;
            }
        }
        foreach (var (slot, button) in _slots)
        {
            var b = s.Buildings.FirstOrDefault(item => item.SlotIndex == slot);
            button.Disabled = Blocked || slot >= s.BuildingCapacity;
            RefreshSlot(slot, button, b, slot >= s.BuildingCapacity);
            button.TooltipText = b is null ? "This slot can hold one planetary building." : $"{b.Name}\n{b.ConstructionStatus}\nCondition {b.Condition:P0}";
        }
        var messages = new List<string>();
        if (s.HubLevel == 0) messages.Add("COMMAND CENTER REQUIRED — Other buildings unlock when it is complete.");
        if (s.PowerDemand > s.PowerSupply + s.StorageDischargePerDay) messages.Add($"POWER DEFICIT — {s.PowerDemand - s.PowerSupply - s.StorageDischargePerDay:0.##} more power needed. Add generation or disable low-priority loads.");
        if (s.WorkforceDemandMillions > s.WorkforceAvailableMillions) messages.Add("WORKFORCE DEFICIT — Some buildings cannot be fully staffed.");
        if (s.SustenanceSupportRatio < 1) messages.Add(s.SustenanceStatus + " " + s.SustenanceRecoveryAction);
        if (p?.CreditFlow.NetCreditsPerDay < 0) messages.Add("LOCAL CREDIT DEFICIT — This planet draws on the empire treasury.");
        if (s.BaseOperationsFundingFraction < .999) messages.Add("OPERATIONS UNDERFUNDED — Output and construction are slowed.");
        if (s.Buildings.Any(b => !b.Complete) && s.Industry <= 0 && p?.EmpireIndustryPerDay <= 0) messages.Add("CONSTRUCTION AWAITING MATERIALS — Restore material production or shared reserves.");
        if (s.BuildingCapacity > 0 && s.Buildings.Count >= s.BuildingCapacity) messages.Add("ALL SLOTS OCCUPIED — Upgrade the Command Center or remove a building.");
        _alerts.Text = messages.Count == 0 ? "No immediate capacity, power, workforce or funding shortfalls." : string.Join("\n", messages);
        _alerts.AddThemeColorOverride("font_color", messages.Count == 0 ? Teal : Bad);
        Set("system", p?.SystemName ?? "—"); Set("species", p?.SpeciesName ?? "—"); Set("stability", $"{p?.Stability:P0}"); Set("infrastructure", $"{p?.Infrastructure:0.00}");
        Set("gravity", $"{p?.GravityG:0.00} g"); Set("temperature", $"{p?.TemperatureKelvin:0} K / {p?.TemperatureKelvin - 273.15:0} °C");
        Set("pressure", $"{p?.PressureKPa:0.0} kPa"); Set("atmosphere", p?.Atmosphere ?? "—"); Set("solvent", p?.Solvent ?? "—");
        Set("radius", $"{p?.RadiusEarth:0.00} Earth radii / {p?.MassEarth:0.00} Earth masses"); Set("radiation", $"{p?.RadiationHazard:P0}");
        Set("discoveries", $"Rare resources: {(p?.RareResource == true ? "yes" : "no")} · Anomaly: {(p?.Anomaly == true ? "yes" : "no")}");
        Set("specialization", s.SpecializationName + (s.SpecializationActive ? " · active" : " · developing")); Set("wear", $"{s.EnvironmentalWearMultiplier:0.00}×"); Set("construction", $"{s.EnvironmentConstructionCostMultiplier:0.00}×");
        Set("tax", s.Currency.Format(p?.CreditFlow.ColonyRevenuePerDay ?? 0)); Set("trade", s.Currency.Format(p?.CreditFlow.TradeRevenuePerDay ?? 0));
        Set("upkeep", s.Currency.Format(p?.CreditFlow.OperatingCostsPerDay ?? s.UpkeepCreditsPerDay));
        Set("upkeepBreakdown", p is null ? "—" : $"Admin {s.Currency.Format(p.CreditFlow.ColonyAdministrationPerDay)}\nServices {s.Currency.Format(p.CreditFlow.PopulationServicesPerDay)}\nHabitats {s.Currency.Format(p.CreditFlow.HabitatSupportPerDay)}\nBuildings {s.Currency.Format(p.CreditFlow.SurfaceMaintenancePerDay)}");
        Set("science", $"{s.SciencePerDay:0.##}"); Set("generation", $"{s.PowerSupply:0.##} / {s.PowerDemand:0.##}");
        Set("buildDemand", $"{s.Buildings.Where(b => !b.Complete).Sum(b => b.RemainingConstructionMaterials):0.0} materials remaining\nUp to {s.Buildings.Where(b => !b.Complete).Sum(b => Math.Min(30, b.RemainingConstructionMaterials)):0.0}/day from shared stores");
        Set("battery", $"{s.StoredPowerDays:0.##} / {s.PowerStorageCapacityDays:0.##} power-days"); Set("batteryFlow", $"+{s.StorageChargePerDay:0.##} / −{s.StorageDischargePerDay:0.##}");
        Set("food", $"{Population(s.FoodCapacityMillions)} / {Population(s.PopulationMillions)}"); Set("foodBalance", PopulationBalance(s.FoodCapacityMillions - s.PopulationMillions), s.FoodCapacityMillions < s.PopulationMillions);
        Set("water", $"{Population(s.WaterCapacityMillions)} / {Population(s.PopulationMillions)}"); Set("waterBalance", PopulationBalance(s.WaterCapacityMillions - s.PopulationMillions), s.WaterCapacityMillions < s.PopulationMillions);
        Set("housing", $"{Population(s.HousingCapacityMillions)} / {Population(s.PopulationMillions)}"); Set("housingBalance", PopulationBalance(s.HousingCapacityMillions - s.PopulationMillions), s.HousingCapacityMillions < s.PopulationMillions);
        Set("reserves", $"{s.FoodReserveDays:0.0} / {s.WaterReserveDays:0.0} days"); Set("workforce", $"{Population(s.WorkforceAvailableMillions)} / {Population(s.WorkforceDemandMillions)}");
        Set("laborBalance", PopulationBalance(s.WorkforceAvailableMillions - s.WorkforceDemandMillions), s.WorkforceAvailableMillions < s.WorkforceDemandMillions);
        Set("employment", $"{s.EmploymentRate:P0} · {Population(s.EmployedPopulationMillions)} employed"); Set("cargo", $"{s.CargoTransferCapacityPerDay:0.##} materials");
        Set("deposit", s.IsResourceOutpost ? $"{s.DepositMaterialName} · {s.DepositGrade}\n{s.RemainingDepositMaterials:0} / {s.InitialDepositMaterials:0} remaining" : "No active extraction outpost");
        Set("extraction", s.IsResourceOutpost ? $"+{s.ExtractionPerDay:0.00}/day · {s.StoredExtractedMaterials:0} / {s.ExtractedMaterialCapacity:0} stored" : "—");
        Set("funding", $"{s.BaseOperationsFundingFraction:P0}", s.BaseOperationsFundingFraction < .999);
        Set("empire", $"{s.Currency.Format(s.Credits)}\n{s.Industry:0.0} construction materials"); Set("empireFlow", $"{s.Currency.FormatRate(p?.EmpireCreditsPerDay ?? 0)}\n{Signed(p?.EmpireIndustryPerDay ?? 0)} materials/day");
        Set("arrears", s.Currency.Format(p?.OperatingArrears ?? 0), p?.OperatingArrears > 0);
        RefreshVisuals(s);
        var detailState = structural + ":" + _selectedSlot;
        if (force || detailState != _detailState) { _detailState = detailState; Details(s); }
        foreach (var update in _detailRefresh) update(s);
        foreach (var button in _details.GetChildren().OfType<Button>())
            if (button.HasMeta("type")) button.Disabled = Blocked || s.BuildOptions.First(item => item.Id == button.GetMeta("type").AsString()).CanAfford == false;
    }

    private void Details(UiSurfaceSnapshot s)
    {
        Clear(_details); _detailRefresh.Clear();
        if (_details.GetParent() is ScrollContainer detailScroll) detailScroll.ScrollVertical = 0;
        if (_selectedSlot is not int slot) { _details.AddChild(Text("BUILDING MANAGEMENT", 15, Teal)); _details.AddChild(Text("Select a surface slot to choose a building or manage an existing one.", 14)); return; }
        _details.AddChild(Text($"SURFACE SLOT {slot + 1:00}", 14, Teal));
        var b = s.Buildings.FirstOrDefault(item => item.SlotIndex == slot);
        if (b is null)
        {
            _details.AddChild(Text("Construct a building", 20));
            _details.AddChild(Text("Credits authorize the site now. Materials are consumed during construction from the shared empire pool. The slot is reserved immediately.", 12, Muted));
            foreach (var option in s.BuildOptions) AddBuildOption(s, slot, option);
            return;
        }
        var buildingPicture = Picture(BuildingArt(b.TypeId)); buildingPicture.CustomMinimumSize = new(0, 110); buildingPicture.StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered; _details.AddChild(buildingPicture);
        _details.AddChild(Text(b.Name, 21, BuildingColor(b.TypeId)));
        var stateLabel = Text("", 14, Teal); _details.AddChild(stateLabel);
        var conditionLabel = Text("", 13); _details.AddChild(conditionLabel);
        var progressLabel = Text("", 13, Muted); _details.AddChild(progressLabel);
        _detailRefresh.Add(snapshot =>
        {
            var current = snapshot.Buildings.First(item => item.Id == b.Id);
            stateLabel.Text = BuildingState(current);
            conditionLabel.Text = $"Condition {current.Condition:P0} · Efficiency {current.Efficiency:P0}\n{(current.Powered ? "Powered" : "Power unavailable")} · {(current.Staffed ? "Staffed" : "Workers needed")}";
            progressLabel.Text = !current.Complete ? $"{current.ConstructionStatus}\n{current.RemainingConstructionMaterials:0.0} materials remaining\n{current.ConstructionRecoveryAction}" : current.UpgradeDaysRemaining > 0 ? $"Upgrade in progress · {current.UpgradeDaysRemaining:0.0} days at full funding" : "";
        });
        if (b.CanUpgrade)
        {
            _details.AddChild(Text($"Upgrade: {b.UpgradeName}\n{s.Currency.Format(b.UpgradeCreditCost)} + {b.UpgradeIndustryCost:0} materials\n{b.UpgradeLockReason}", 13));
            var upgrade = ActionButton("Upgrade building", "PlanetaryUpgrade", () => Run(() => _upgrade(b.Id))); upgrade.Disabled = !b.CanAffordUpgrade || b.UpgradeLockReason is not null; _details.AddChild(upgrade);
            _detailRefresh.Add(snapshot => { var current = snapshot.Buildings.First(item => item.Id == b.Id); upgrade.Disabled = Blocked || !current.CanAffordUpgrade || current.UpgradeLockReason is not null; });
        }
        if (b.Complete)
        {
            _details.AddChild(ActionButton(b.Enabled ? "Disable building" : "Enable building", "PlanetaryEnable", () => Run(() => _enabled(b.Id, !b.Enabled))));
            _details.AddChild(ActionButton(b.Prioritized ? "Use normal priority" : "Prioritize operations", "PlanetaryPriority", () => Run(() => _priority(b.Id, !b.Prioritized))));
            var repair = ActionButton($"Repair · {b.RepairIndustryCost:0.0} materials", "PlanetaryRepair", () => Run(() => _repair(b.Id)));
            repair.Disabled = b.Condition >= .999999 || !b.CanAffordRepair; _details.AddChild(repair);
            _detailRefresh.Add(snapshot => { var current = snapshot.Buildings.First(item => item.Id == b.Id); repair.Text = $"Repair · {current.RepairIndustryCost:0.0} materials"; repair.Disabled = Blocked || current.Condition >= .999999 || !current.CanAffordRepair; });
        }
        _details.AddChild(ActionButton(b.Complete ? "Demolish building…" : "Cancel construction…", "PlanetaryRemove", () =>
        {
            _pendingRemoval = b.Id;
            _demolition.DialogText = b.Complete ? $"Demolish {b.Name}? Production stops and the slot becomes available. Completed buildings do not refund their cost." :
                $"Cancel {b.Name}? The slot becomes available. Half the authorization credit cost is returned; spent materials are not refunded.";
            _demolition.PopupCentered(new Vector2I(480, 190));
        }));
    }

    private void Run(Func<UiSurfaceOrderResult> action)
    {
        if (Blocked) return;
        var result = action(); _status.Text = result.Message; _status.AddThemeColorOverride("font_color", result.Accepted ? Teal : Bad);
        Refresh(true);
    }
    private Button ActionButton(string text, string name, Action action)
    {
        var button = new Button { Text = text, Name = name, CustomMinimumSize = new(0, 38) };
        button.AddThemeFontSizeOverride("font_size", 13); button.AddThemeStyleboxOverride("normal", PanelStyle());
        button.AddThemeStyleboxOverride("hover", PanelStyle("223b49")); button.AddThemeStyleboxOverride("focus", CardStyle("183642", Teal));
        button.AddThemeStyleboxOverride("pressed", PanelStyle("235364")); button.AddThemeStyleboxOverride("disabled", PanelStyle("0c1a25"));
        button.AddThemeColorOverride("font_color", White); button.AddThemeColorOverride("font_disabled_color", Muted);
        button.Pressed += () => { if (!Blocked) action(); }; return button;
    }
    private static StyleBoxFlat PanelStyle(string color = "112532") => new()
    {
        BgColor = new(color), BorderColor = new("29404d"), BorderWidthBottom = 1, BorderWidthLeft = 1, BorderWidthRight = 1, BorderWidthTop = 1,
        CornerRadiusBottomLeft = 5, CornerRadiusBottomRight = 5, CornerRadiusTopLeft = 5, CornerRadiusTopRight = 5,
        ContentMarginLeft = 10, ContentMarginRight = 10, ContentMarginTop = 8, ContentMarginBottom = 8
    };
    private static VBoxContainer ScrollPanel(Node parent, string name, int width)
    {
        var scroll = new ScrollContainer { Name = name, CustomMinimumSize = new(width, 0), HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled };
        parent.AddChild(scroll); var content = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill };
        content.AddThemeConstantOverride("separation", 10); scroll.AddChild(content); return content;
    }
    private static Label Text(string text, int size, Color? color = null)
    {
        var label = new Label { Text = text, AutowrapMode = TextServer.AutowrapMode.WordSmart, MouseFilter = MouseFilterEnum.Ignore };
        label.AddThemeFontSizeOverride("font_size", size); label.AddThemeColorOverride("font_color", color ?? White); return label;
    }
    private void Fact(VBoxContainer parent, string key, string title)
    {
        var block = new VBoxContainer(); block.AddThemeConstantOverride("separation", 2); parent.AddChild(block);
        block.AddChild(Text(title, 11, Muted)); var value = Text("—", 13); value.Name = "PlanetaryFact_" + key; block.AddChild(value); _facts[key] = value;
    }
    private static Color ResourceColor(string key) => key switch { "power" or "income" => Gold, "materials" => Orange, "research" => Purple, _ => Blue };
    private void Kpi(HBoxContainer parent, string key, string title)
    {
        var accent = ResourceColor(key);
        var card = new PanelContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, CustomMinimumSize = new(0, 67) }; card.AddThemeStyleboxOverride("panel", CardStyle("142934", accent.Darkened(.3f), 3)); parent.AddChild(card);
        var box = new VBoxContainer(); box.AddThemeConstantOverride("separation", 2); card.AddChild(box); box.AddChild(Text(title, 10, accent));
        var label = Text("—", key == "income" ? 19 : 24, accent); label.Name = "PlanetaryMetric_" + key; box.AddChild(label); _facts[key] = label;
    }
    private void Set(string key, string value, bool? warning = false) { _facts[key].Text = value; _facts[key].AddThemeColorOverride("font_color", warning == true ? Bad : key is "population" or "power" or "income" or "materials" or "research" ? ResourceColor(key) : White); }
    private static string Signed(double value) => value.ToString("+0.##;-0.##;0");
    private static string Population(double millions) => Math.Abs(millions) >= 1000 ? $"{millions / 1000:0.00} B" : Math.Abs(millions) >= 1 ? $"{millions:0.00} M" : $"{millions * 1000000:0} people";
    private static string PopulationBalance(double value) => $"{(value < 0 ? "Deficit" : "Surplus")} {Population(Math.Abs(value))}";
    private static string BuildingState(UiSurfaceBuilding b) => !b.Complete ? $"Building {b.Progress:P0}" : b.UpgradeDaysRemaining > 0 ? "Upgrading" : !b.Enabled ? "Disabled" : b.Condition <= .15 ? "Repairs required" : !b.Staffed ? "Workers needed" : !b.Powered ? "Power needed" : "Operational";
    private static string Wrap(string text) { var words = text.Split(' '); var line = ""; var result = ""; foreach (var word in words) { if ((line + " " + word).Length > 21) { result += line + "\n"; line = word; } else line += (line.Length > 0 ? " " : "") + word; } return result + line; }
    private static void Clear(Node node) { foreach (var child in node.GetChildren()) { node.RemoveChild(child); child.QueueFree(); } }
}
