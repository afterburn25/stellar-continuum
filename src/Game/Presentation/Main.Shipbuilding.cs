using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Diagnostics;
using Game.Simulation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Presentation;

public partial class Main
{
    private ShipbuildingSimulation _shipbuilding = new();
    private readonly Dictionary<int, Label> _scienceFleetMarkers = new();
    private int _shipDesignCandidateIndex;
    private CanvasLayer? _shipbuildingUiLayer;
    public string UiShipbuildingSummary { get; private set; } = "Shipyard initializing…";

    private ShipyardState PlayerShipyard => _galaxy.ShipyardStates.First(state => state.CivilizationId == _galaxy.PlayerCivilizationId);
    private FleetState? PlayerScienceVessel => _galaxy.Fleets.FirstOrDefault(fleet => fleet.IsActive && fleet.CivilizationId == _galaxy.PlayerCivilizationId && fleet.Role == FleetRole.Science);

    public override void _PhysicsProcess(double delta)
    {
        _ = delta;
        if (_galaxy is null)
            return;

        EnsureScienceFleetMarkerLayer();
        UpdateShipbuildingSummary();
        UpdateScienceFleetMarkers();
    }

    public override void _Input(InputEvent @event)
    {
        if (ShouldBlockGameplayInput())
            return;

        if (_galaxy is null)
            return;

        if (@event is InputEventKey key && key.Pressed && !key.Echo)
        {
            if (key.Keycode == Key.V)
            {
                CycleShipDesignCandidate();
                GetViewport().SetInputAsHandled();
                return;
            }

            if (key.Keycode == Key.Y)
            {
                StartSelectedShipBuild();
                GetViewport().SetInputAsHandled();
                return;
            }
        }

    }

    private void CycleShipDesignCandidate()
    {
        var available = _shipbuilding.GetAvailableDesigns(_galaxy, _galaxy.PlayerCivilizationId);
        if (available.Count == 0)
        {
            SetStatus("No interstellar ship designs are available yet. Develop compatible spacecraft-construction and interstellar-transit capability, and complete an Orbital Shipyard.", 7.0);
            return;
        }

        _shipDesignCandidateIndex = (_shipDesignCandidateIndex + 1) % available.Count;
        SetStatus($"Shipyard candidate: {available[_shipDesignCandidateIndex].Name}");
    }

    private void StartSelectedShipBuild()
    {
        var candidate = GetShipDesignCandidate();
        if (candidate is null)
        {
            SetStatus("No ship design is currently available. Develop compatible spacecraft-construction and interstellar-transit capability, and complete an Orbital Shipyard.", 7.0);
            return;
        }

        var result = _shipbuilding.StartBuild(_galaxy, _galaxy.PlayerCivilizationId, candidate.Id);
        SetStatus(result.Message, result.Accepted ? 6.0 : 7.0);
        SupportLogger.Log("shipbuilding-order", $"design={candidate.Id} accepted={result.Accepted} message={result.Message}");
        if (result.Accepted)
            PublishPlayerNotification("Ships", result.Message);
    }

    private ShipDesignDefinition? GetShipDesignCandidate()
    {
        var available = _shipbuilding.GetAvailableDesigns(_galaxy, _galaxy.PlayerCivilizationId);
        if (available.Count == 0)
            return null;

        _shipDesignCandidateIndex = Math.Clamp(_shipDesignCandidateIndex, 0, available.Count - 1);
        return available[_shipDesignCandidateIndex];
    }

    private void HandleShipbuildingEvents(IReadOnlyList<ShipbuildingEvent> events)
    {
        foreach (var e in events)
        {
            SupportLogger.Log("shipbuilding", $"civilization={e.CivilizationId} fleet={e.FleetId} design={e.DesignId} message={e.Message}");
            if (e.CivilizationId != _galaxy.PlayerCivilizationId)
                continue;

            _shipDesignCandidateIndex = 0;
            SetStatus(e.Message, 7.0);
            PublishPlayerNotification("Ships", e.Message);
            RouteShipCompletedVoice(e);
        }
    }

    private void IssueExplorationOrder(FleetState? fleet, int targetSystemId, string vesselType)
    {
        if (fleet is null)
        {
            SetStatus($"No active {vesselType} is available. Build one in the Orbital Shipyard.", 7.0);
            return;
        }

        if (!_galaxy.Systems.Any(system => system.Id == targetSystemId))
        {
            SetStatus("Select a destination star on the regional map first.", 7.0);
            return;
        }

        // This is the same authoritative assessment used by IssueMoveOrder, including
        // local surveys and range rejection. Keep its useful rejection message visible.
        var result = _exploration.IssueTravelOrder(_galaxy, fleet.Id, targetSystemId);
        var known = _galaxy.Knowledge.IsSystemKnown(_galaxy.PlayerCivilizationId, targetSystemId);
        var message = result.Accepted && !known
            ? $"{fleet.Name}: course set for {PublicCatalogSystemName(targetSystemId)}. {result.Candidate?.Reach.Reason}"
            : result.Message;
        SetStatus(message, result.Accepted ? 6.0 : 8.0);
        SupportLogger.Log("exploration-order", $"fleet={fleet.Id} target={targetSystemId} accepted={result.Accepted} message={result.Message}");
        QueueRedraw();
    }

    private void EnsureScienceFleetMarkerLayer()
    {
        if (_shipbuildingUiLayer is not null)
            return;

        _shipbuildingUiLayer = new CanvasLayer
        {
            Name = "ShipbuildingUiLayer",
            Layer = 20,
        };
        AddChild(_shipbuildingUiLayer);
    }

    private void UpdateShipbuildingSummary()
    {
        var state = PlayerShipyard;
        var candidate = GetShipDesignCandidate();
        if (state.ActiveDesignId is { } activeDesignId)
        {
            var design = ShipDesignRegistry.Get(activeDesignId);
            var percent = design.IndustryCost <= 0.0 ? 100.0 : state.ActiveBuildProgress / design.IndustryCost * 100.0;
            var population = state.ReservedPopulationMillions > 0.0 ? $" | Colonists reserved {state.ReservedPopulationMillions:0}M" : string.Empty;
            UiShipbuildingSummary = $"Shipyard: {design.Name} — {state.ActiveBuildProgress:0}/{design.IndustryCost:0} ({percent:0.0}%){population} | Queue {state.PendingBuildCount}/{ShipyardState.MaxPendingBuilds} | Open Ships to add a named design";
            return;
        }

        UiShipbuildingSummary = candidate is null
            ? $"Shipyard locked: {ShipDesignRegistry.All[0].Name} {_shipbuilding.GetLockReason(_galaxy, _galaxy.PlayerCivilizationId, ShipDesignRegistry.All[0])}"
            : $"Shipyard: {_shipbuilding.GetAvailableDesigns(_galaxy, _galaxy.PlayerCivilizationId).Count} designs available | Queue {state.PendingBuildCount}/{ShipyardState.MaxPendingBuilds} — open Ships and choose a named design | Select a ship icon, then right-click its destination";
    }

    private void UpdateScienceFleetMarkers()
    {
        if (_shipbuildingUiLayer is null)
            return;

        var fleets = _galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == _galaxy.PlayerCivilizationId && fleet.Role == FleetRole.Science)
            .ToArray();
        var activeIds = fleets.Select(fleet => fleet.Id).ToHashSet();

        foreach (var fleetId in _scienceFleetMarkers.Keys.Where(id => !activeIds.Contains(id)).ToArray())
        {
            _scienceFleetMarkers[fleetId].QueueFree();
            _scienceFleetMarkers.Remove(fleetId);
        }

        var center = GetViewportRect().Size * 0.5f + _pan;
        foreach (var fleet in fleets)
        {
            if (!_scienceFleetMarkers.TryGetValue(fleet.Id, out var marker))
            {
                marker = new Label
                {
                    Text = "◆",
                    Size = new Vector2(20, 20),
                    MouseFilter = Control.MouseFilterEnum.Ignore,
                    TooltipText = fleet.Name,
                };
                marker.AddThemeFontSizeOverride("font_size", 16);
                marker.AddThemeColorOverride("font_color", new Color(0.40f, 0.86f, 1.0f));
                _shipbuildingUiLayer.AddChild(marker);
                _scienceFleetMarkers[fleet.Id] = marker;
            }

            marker.Position = ToScreen(fleet.Position, center) - new Vector2(7, 11);
            marker.Visible = !UiIsSystemSpatialView;
        }
    }
}
