using System.Linq;
using Godot;
using Game.Simulation.Models;
using Game.Presentation.Spatial;

namespace Game.Presentation;

/// <summary>Mouse-driven shortcuts built exclusively from the player's own read models.</summary>
public partial class EmpireOverviewPanel : PanelContainer
{
    private VBoxContainer _body;
    private readonly System.Collections.Generic.Dictionary<string, Label> _shipValues = new();
    private string _key = "";
    private bool _showingShip;
    private float _overviewContentHeight = 90;
    public EmpireOverviewPanel()
    {
        Name = "EmpireOverview";
        VisualUi.ContainPointerInput(this);
        AddThemeStyleboxOverride("panel", CinematicArt.Frame(margin: 10));
        var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true };
        AddChild(scroll);
        _body = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _body.AddThemeConstantOverride("separation", 5); scroll.AddChild(_body);
    }

    public void Refresh(Main main, bool drawerOpen)
    {
        Visible = !main.UiIsMenuOpen && !main.UiIsDeveloperToolsOpen && !main.UiIsSurfaceOpen && main.UiTutorial?.IsActive != true &&
            (!main.UiIsSystemSpatialView || main.UiSelectedFleetId.HasValue) && !drawerOpen;
        if (!Visible) return;
        var colonies = main.UiOwnedColonies;
        var fleets = main.UiOwnedFleets;
        var selected = fleets.FirstOrDefault(f => f.FleetId == main.UiSelectedFleetId);
        if (selected is not null)
        {
            if (!_showingShip)
            {
                AddThemeStyleboxOverride("panel", VisualUi.Surface(margin: 10));
                _showingShip = true;
            }
            UpdateBounds();
            PresentShip(main, selected);
            return;
        }
        if (_showingShip)
        {
            AddThemeStyleboxOverride("panel", CinematicArt.Frame(margin: 10));
            _showingShip = false;
        }
        var reference = main.UiSelectedSystemHomeReference;
        var key = $"{main.UiSelectedSystemId}:{reference.Name}:{reference.Distance}|" + string.Join("|", colonies.Select(c => $"{c.ColonyId}:{c.PlanetName}:{c.PopulationMillions:0}:{c.BuildingCount}")) +
            string.Join("|", fleets.Select(f => $"{f.FleetId}:{f.Name}:{f.Location}:{f.CombatPower:0}"));
        _overviewContentHeight = 164 + colonies.Length * 86 + fleets.Length * 62;
        UpdateBounds();
        if (_key == key) return;
        _key = key;
        foreach (var child in _body.GetChildren()) { _body.RemoveChild(child); child.QueueFree(); }
        _body.AddChild(VisualUi.Text("SELECTED SYSTEM", 10, VisualUi.Accent));
        var selectedName = VisualUi.Text(reference.Name, 15, wrap: true);
        selectedName.Name = "OverviewSelectedSystem";
        _body.AddChild(selectedName);
        _body.AddChild(VisualUi.Text("DISTANCE FROM HOMEWORLD", 9, VisualUi.Muted));
        var distance = VisualUi.Text(reference.Distance, 12, VisualUi.Accent, wrap: true);
        distance.Name = "OverviewHomeDistance";
        _body.AddChild(distance);
        _body.AddChild(new HSeparator());
        _body.AddChild(VisualUi.Text("EMPIRE OVERVIEW", 14));
        _body.AddChild(VisualUi.Text($"COLONIES   {colonies.Length}", 10, VisualUi.Accent));
        foreach (var colony in colonies)
        {
            var button = VisualUi.Button(colony.PlanetName, "Inspect " + colony.PlanetName + " in " + colony.SystemName,
                () => main.UiOpenOwnedColony(colony.ColonyId, false), VisualIconLibrary.Colony);
            button.Name = "OverviewColony" + colony.ColonyId; button.Alignment = HorizontalAlignment.Left;
            _body.AddChild(button);
            var row = new HBoxContainer(); _body.AddChild(row);
            var system = VisualUi.Text(colony.SystemName, 11, VisualUi.Muted);
            system.SizeFlagsHorizontal = SizeFlags.ExpandFill; row.AddChild(system);
            row.AddChild(VisualUi.Text(colony.PopulationMillions >= 1000 ? $"{colony.PopulationMillions/1000:0.00}B people" :
                $"{colony.PopulationMillions:0.0}M people", 11));
        }
        _body.AddChild(new HSeparator());
        _body.AddChild(VisualUi.Text($"FLEETS   {fleets.Length}", 10, VisualUi.Accent));
        _body.AddChild(VisualUi.Text($"Combined power  {fleets.Sum(f => f.CombatPower):N0}", 12, VisualUi.Accent));
        foreach (var fleet in fleets)
        {
            var button = VisualUi.Button(fleet.Name, fleet.DesignName + " · " + fleet.Activity,
                () => main.UiFocusOwnedFleet(fleet.FleetId), VisualIconLibrary.NavShips);
            button.Name = "OverviewFleet" + fleet.FleetId; button.Alignment = HorizontalAlignment.Left;
            button.TextOverrunBehavior = TextServer.OverrunBehavior.TrimEllipsis;
            button.AddThemeFontSizeOverride("font_size", 12); _body.AddChild(button);
            _body.AddChild(VisualUi.Text($"{fleet.Location}  ·  Power {fleet.CombatPower:N0}", 10, VisualUi.Muted));
        }
        if (fleets.Length == 0) _body.AddChild(VisualUi.Text("No commissioned fleets", 11, VisualUi.Muted));
    }

    /// <summary>
    /// Keep the fixed-position overview inside the viewport independently from its throttled
    /// campaign-data refresh. Window resizing can occur between two data refreshes.
    /// </summary>
    public void UpdateBounds()
    {
        var viewport = GetViewportRect().Size;
        Position = new(viewport.X - 282, 84);
        Size = new(270, _showingShip
            ? viewport.Y - 132
            : Mathf.Min(viewport.Y - 132, _overviewContentHeight));
    }

    private void PresentShip(Main main, UiOwnedFleetSnapshot ship)
    {
        var key = $"ship:{ship.FleetId}:{ship.Name}";
        if (_key != key)
        {
            _key = key; _shipValues.Clear();
            foreach (var child in _body.GetChildren()) { _body.RemoveChild(child); child.QueueFree(); }
            var header = new HBoxContainer(); _body.AddChild(header);
            var title = VisualUi.Text(ship.Name, 16, wrap: true); title.Name = "SelectedShipName";
            title.SizeFlagsHorizontal = SizeFlags.ExpandFill; header.AddChild(title);
            var close = VisualUi.Button("", "Return to world and fleet overview.", main.UiClearFleetSelection, VisualIconLibrary.NavClose);
            close.Name = "CloseShipInspector"; close.CustomMinimumSize = new(30, 30); header.AddChild(close);
            var model = new ShipModelView { Name = "SelectedShipModel" };
            _body.AddChild(model); model.Present(ship.DesignId, main.UiVisualStyle);
            _body.AddChild(VisualUi.Text(ship.DesignName, 12, VisualUi.Accent, true));
            AddShipSection("NAVIGATION");
            foreach (var field in new[] { "Location", "Activity", "Destination", "Next stop", "Arrival", "Course", "Recovery" }) AddShipValue(field, true);
            var holdButton = VisualUi.Button("Hold", "Hold at the current system, or after the current lane finishes.", main.UiToggleSelectedCivilianFleetHold);
            holdButton.Name = "CivilianHoldResume"; _body.AddChild(holdButton);
            var baseButton = VisualUi.Button("Return to base", "Route to the nearest reachable owned refuelling settlement.", main.UiRequestSelectedCivilianReturnToBase);
            baseButton.Name = "CivilianReturnToBase"; _body.AddChild(baseButton);
            AddShipSection("VESSEL");
            foreach (var field in new[] { "Combat power", "Speed", "Jump range", "Fuel", "Integrity", "Cargo", "Upkeep" })
                AddShipValue(field, field is "Speed" or "Jump range" or "Fuel");
            AddShipSection("DESTINATION PREVIEW"); AddShipValue("Preview", true);
        }
        SetShipValue("Location", ship.Location);
        SetShipValue("Activity", ship.Activity);
        SetShipValue("Destination", ship.Destination);
        SetShipValue("Next stop", ship.NextStop);
        SetShipValue("Arrival", main.UiSelectedFleetEta);
        SetShipValue("Course", ship.RemainingRouteLegs > 0 ? $"{MetricFormat.InterstellarLength(ship.RemainingRouteDistanceLightYears)} · {ship.RemainingRouteLegs} legs" : "No active route");
        SetShipValue("Recovery", ship.ReturnToBaseFailureReason ?? main.UiSelectedCivilianReturnPreview);
        SetShipValue("Speed", MetricFormat.InterstellarSpeed(ship.StrategicSpeed));
        SetShipValue("Jump range", MetricFormat.InterstellarLength(ship.MaximumLegRangeLightYears));
        SetShipValue("Fuel", $"Remaining: {MetricFormat.InterstellarLength(ship.FuelRemainingLightYears)}\nCapacity: {MetricFormat.InterstellarLength(ship.FuelCapacityLightYears)}");
        SetShipValue("Integrity", $"{ship.Integrity:P0}");
        SetShipValue("Combat power", $"{ship.CombatPower:N0}");
        SetShipValue("Cargo", $"{ship.CargoMaterials:0.#} / {ship.CargoMaterialCapacity:0.#}");
        SetShipValue("Upkeep", main.UiFormatMoney(ship.OperatingCostPerDay) + " / day");
        SetShipValue("Preview", main.UiFleetDestinationPreview);
        if (_body.GetNodeOrNull<Button>("CivilianHoldResume") is { } hold)
        {
            var civilian = ship.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony;
            hold.Visible = civilian;
            hold.Disabled = !civilian;
            hold.Text = ship.HoldRequested ? "Resume" : "Hold";
            hold.TooltipText = ship.HoldRequested
                ? "Resume this ship's existing mission under current operating conditions."
                : "Hold at the current system, or after the current lane finishes.";
        }
        if (_body.GetNodeOrNull<Button>("CivilianReturnToBase") is { } returnButton)
        {
            var civilian = ship.Role is FleetRole.Scout or FleetRole.Science or FleetRole.Colony;
            returnButton.Visible = civilian;
            returnButton.Disabled = !civilian || ship.ReturnToBaseRequested;
            returnButton.Text = main.UiSelectedCivilianReturnNeedsConfirmation ? "Confirm return (no refund)" : "Return to base";
            returnButton.TooltipText = main.UiSelectedCivilianReturnNeedsConfirmation
                ? main.UiSelectedCivilianReturnPreview
                : "Route to the nearest reachable owned refuelling settlement using current fuel.";
        }
    }

    private void AddShipSection(string name) { _body.AddChild(new HSeparator()); _body.AddChild(VisualUi.Text(name, 10, VisualUi.Accent)); }
    private void AddShipValue(string name, bool stacked)
    {
        var row = stacked ? (BoxContainer)new VBoxContainer() : new HBoxContainer();
        row.AddThemeConstantOverride("separation", 2); _body.AddChild(row);
        var label = VisualUi.Text(name, 11, VisualUi.Muted); label.SizeFlagsHorizontal = SizeFlags.ExpandFill; row.AddChild(label);
        var value = VisualUi.Text("", 12, wrap: stacked); value.Name = "Ship" + name.Replace(" ", "");
        value.SizeFlagsHorizontal = SizeFlags.ExpandFill;
        if (!stacked) value.HorizontalAlignment = HorizontalAlignment.Right;
        row.AddChild(value); _shipValues.Add(name, value);
    }
    private void SetShipValue(string name, string text) => _shipValues[name].Text = text;
}
