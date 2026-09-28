using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Presentation;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

public sealed record LocalFleetMarker(
    int Id, string Name, FleetRole Role, string DesignId, Vector2 ChartPosition,
    Vector2 ChartTarget, bool IsMoving, bool IsHeld);

public sealed record LocalLaneMarker(int DestinationSystemId, string Label, Vector2 Direction, bool IsKnown, double LengthLightYears);

public partial class SystemSpatialCanvas
{
    public Func<IReadOnlyList<LocalFleetMarker>>? GetLocalFleets { get; set; }
    public Func<IReadOnlyList<LocalLaneMarker>>? GetLocalLanes { get; set; }
    public Func<CivilizationVisualStyle>? GetVisualStyle { get; set; }
    public Func<ShipyardBuildActivity>? GetShipyardActivity { get; set; }
    public event Action<int>? FleetSelected;
    public event Action<int>? LaneSelected;
    private readonly Dictionary<int, Button> _fleetIcons = new();

    private void UpdateLocalFleets()
    {
        IReadOnlyList<LocalFleetMarker> fleets = (GetLocalFleets?.Invoke() ?? Array.Empty<LocalFleetMarker>()).Take(64).ToArray();
        var style = GetVisualStyle?.Invoke() ?? CivilizationVisualStyles.Terran;
        _scene.SetVisualStyle(style);
        _scene.PresentLocalFleets(fleets, style);
        if (_focusedFleetId is int focusedFleetId && !_scene.HasLocalFleet(focusedFleetId))
            ExitFleetFocus();
        if (GetShipyardActivity?.Invoke() is { } activity)
            _scene.PresentShipyardActivity(activity, style);
        foreach (var stale in _fleetIcons.Keys.Where(id => !fleets.Any(f => f.Id == id)).ToArray())
        { _fleetIcons[stale].QueueFree(); _fleetIcons.Remove(stale); }
        for (var i = 0; i < fleets.Count; i++)
        {
            var fleet = fleets[i];
            if (!_fleetIcons.TryGetValue(fleet.Id, out var button))
            {
                var icon = fleet.Role switch { FleetRole.Scout => VisualIconLibrary.Scout, FleetRole.Science => VisualIconLibrary.ScienceVessel,
                    FleetRole.Colony => VisualIconLibrary.ColonyShip, FleetRole.Military => VisualIconLibrary.PatrolCorvette, _ => VisualIconLibrary.NavShips };
                button = VisualUi.Button("", fleet.Name + " · click to select · double-click for vessel focus",
                    () => FleetSelected?.Invoke(fleet.Id), icon);
                button.GuiInput += @event =>
                {
                    if (@event is not InputEventMouseButton { ButtonIndex: MouseButton.Left, Pressed: true, DoubleClick: true }) return;
                    FocusFleet(fleet.Id);
                    button.AcceptEvent();
                };
                button.Name = "SystemFleet" + fleet.Id; button.ZIndex = 18; button.CustomMinimumSize = new(30, 30);
                // BaseButton consumes its normal left-click selection, while unhandled right-clicks
                // continue to the orbital canvas so a marker cannot make the world beneath it unorderable.
                button.MouseFilter = MouseFilterEnum.Pass;
                button.Modulate = new Color(.40f, 1f, .62f, 1f);
                button.Size = new(30, 30); AddChild(button); _fleetIcons.Add(fleet.Id, button);
            }
            Vector2? anchor = null;
            if (_snapshot is not null && !IsDetailedFocus)
            {
                var layout = CurrentViewport;
                // The chart coordinate is simulation-owned local transit geometry. Idle ships
                // remain at the system centre; a local crossing follows its persisted path.
                anchor = new Vector2(layout.CenterX, layout.CenterY) + fleet.ChartPosition * (_snapshot.DesignRadius * ChartRenderRadiusFactor * layout.Scale);
            }
            button.Visible = anchor.HasValue;
            if (anchor.HasValue) button.Position = anchor.Value - new Vector2(15, 15);
        }
    }
}
