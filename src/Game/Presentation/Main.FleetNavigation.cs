using System;
using System.Linq;
using Godot;
using Game.Simulation.Models;
using Game.Simulation.Exploration;
using Game.Simulation.Economy;
using Game.Simulation.Colonization;

namespace Game.Presentation;

public sealed record UiFleetRouteAssessment(bool ReachSupported, double DistanceLy, string Reason);

/// <summary>Selection and mouse orders use exact owned vessels and authoritative route commands.</summary>
public partial class Main
{
    private int? _selectedFleetId;
    private GalaxyState? _fleetSelectionContext;
    private int? _hoverDestinationId;
    private FleetState? SelectedFleet => ReferenceEquals(_fleetSelectionContext, _galaxy)
        ? _galaxy.Fleets.FirstOrDefault(f => f.Id == _selectedFleetId && f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId)
        : null;
    public int? UiSelectedFleetId => SelectedFleet?.Id;
    /// <summary>Read-only canonical reach assessment used by the destination preview and map clients.</summary>
    public UiFleetRouteAssessment UiGetFleetRouteAssessment(int fleetId, int targetSystemId)
    {
        if (_galaxy is null) return new(false, 0, "Campaign is initializing.");
        var fleet = _galaxy.Fleets.FirstOrDefault(item => item.Id == fleetId && item.IsActive &&
            item.CivilizationId == _galaxy.PlayerCivilizationId);
        if (fleet is null) return new(false, 0, "That owned ship is no longer available.");
        var reach = fleet.Role == FleetRole.Colony
            ? _colonization.AssessOperationalReach(_galaxy, fleet.Id, targetSystemId)
            : _exploration.AssessOperationalReach(_galaxy, fleet.Id, targetSystemId);
        return new(reach.IsSupported, reach.RouteDistanceLightYears, reach.Reason);
    }
    public void UiClearFleetSelection() { _returnConfirmation = null; _selectedFleetId = null; QueueRedraw(); }

    public void UiSelectOwnedFleet(int fleetId, bool center = false)
    {
        var fleet = _galaxy.Fleets.FirstOrDefault(f => f.Id == fleetId && f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId);
        if (fleet is null) return;
        _returnConfirmation = null;
        UiCloseOrbitalInspector();
        _fleetSelectionContext = _galaxy;
        _selectedFleetId = fleetId;
        GetNode<CampaignSidebar>("CampaignSidebar").CloseDrawer();
        if (center)
        {
            ReturnToStellarView(announce: false);
            _zoom = Spatial.SpatialNavigationLayout.StellarRegionScale;
            _pan = -new Vector2(fleet.Position.X, fleet.Position.Y) * (_zoom * UiCatalogVisualCoordinateScale);
            SynchronizeRegionalCamera();
            var midpoint = GetViewportRect().Size * .5f;
            _regionalCamera.Snap(_zoom,
                midpoint.X - (double)fleet.Position.X * UiCatalogVisualCoordinateScale * _zoom,
                midpoint.Y - (double)fleet.Position.Y * UiCatalogVisualCoordinateScale * _zoom);
        }
        SetStatus($"{fleet.Name} selected. Right-click a destination to set its course.", 4);
        AudioDirector.PlayConfirm();
        QueueRedraw();
    }

    private Vector2 FleetMarkerScreenPosition(FleetState fleet, Vector2 center)
    {
        var offset = fleet.Role switch
        {
            FleetRole.Scout => new Vector2(-17, 23), FleetRole.Science => new Vector2(17, 23),
            FleetRole.Colony => new Vector2(-17, 52), FleetRole.Military => new Vector2(17, 52),
            _ => new Vector2(51, 52),
        };
        return ToScreen(fleet.Position, center) + offset;
    }

    public Vector2? UiGetFleetScreenPosition(int fleetId)
    {
        var fleet = _galaxy.Fleets.FirstOrDefault(f => f.Id == fleetId && f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId);
        return fleet is null || UiIsSystemSpatialView ? null : FleetMarkerScreenPosition(fleet, GetViewportRect().Size * .5f + _pan);
    }

    private bool TrySelectFleetAt(Vector2 pointer)
    {
        var center = GetViewportRect().Size * .5f + _pan;
        var hits = _galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId &&
            FleetMarkerScreenPosition(f, center).DistanceTo(pointer) <= 15).OrderBy(f => f.Id).ToArray();
        if (hits.Length == 0) return false;
        // Repeated clicks cycle the ships in a co-located role group; the outliner selects exact IDs.
        var index = Array.FindIndex(hits, f => f.Id == UiSelectedFleetId);
        UiSelectOwnedFleet(hits[(index + 1) % hits.Length].Id);
        return true;
    }

    private void IssueSelectedFleetOrderAt(Vector2 pointer)
    {
        var target = FindNearestCatalogSystem(pointer, 18);
        if (target is null) return;
        var fleet = SelectedFleet;
        if (fleet is null) { SetStatus("Select a ship icon first, then right-click its destination.", 5); return; }
        UiPointerCommandRevision++;
        if (fleet.Role is FleetRole.Scout or FleetRole.Science)
            IssueExplorationOrder(fleet, target.Id, fleet.Role == FleetRole.Scout ? "scout" : "science vessel");
        else if (fleet.Role == FleetRole.Military)
        {
            var result = _coreSimulation.IssueMilitaryDeploymentOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id, target.Id);
            SetStatus(result.Message, 6);
        }
        else if (fleet.Role == FleetRole.Colony)
        {
            var result = _colonization.IssueTransitOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id, target.Id);
            SetStatus(result.Message, 7);
        }
        else
        {
            var result = _coreSimulation.IssueFreightTransitOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id, target.Id);
            SetStatus(result.Message, 6);
        }
        QueueRedraw();
    }

    private void IssueSelectedFleetBodyOrder(int bodyId)
    {
        if (SelectedFleet is not { } fleet) { SetStatus("Select a ship before choosing its destination.", 5); return; }
        UiPointerCommandRevision++;
        if (fleet.Role == FleetRole.Colony)
            SetStatus(IssueUiColonyOrder(fleet.Id, _selectedSystemId, bodyId), 7);
        else if (fleet.Role is FleetRole.Scout or FleetRole.Science)
            IssueExplorationOrder(fleet, _selectedSystemId, fleet.Role == FleetRole.Scout ? "scout" : "science vessel");
        else SetStatus("Select a star on the galaxy map to order interstellar travel.", 5);
    }

    public string UiSelectedFleetEta
    {
        get
        {
            if (SelectedFleet is not { } fleet) return "";
            var status = _missionStatusEvaluator.Build(_galaxy, fleet);
            if (status.Phase == ExplorationMissionPhase.Traveling && status.EstimatedTransitDaysRemaining is double transit)
                return $"Arrival in {transit:0.0} game days";
            return status.EstimatedMissionDaysRemaining is > 0 ? $"Work: {status.EstimatedMissionDaysRemaining:0.0} game days remaining" : status.Summary;
        }
    }

    public void UiToggleSelectedCivilianFleetHold()
    {
        if (SelectedFleet is not { } fleet)
        {
            SetStatus("Select a civilian mission ship first.", 4);
            return;
        }
        var result = fleet.HoldRequested
            ? _coreSimulation.IssueCivilianResumeOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id)
            : _coreSimulation.IssueCivilianHoldOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id);
        SetStatus(result.Message, 5);
        QueueRedraw();
    }

    private ReturnConfirmation? _returnConfirmation;
    private sealed record ReturnConfirmation(GalaxyState Campaign, FleetState Fleet, int MissionOrderRevision);

    public void UiRequestSelectedCivilianReturnToBase()
    {
        if (SelectedFleet is not { } fleet)
        {
            SetStatus("Select a civilian mission ship first.", 4);
            return;
        }
        var confirm = _returnConfirmation is { } pending && ReferenceEquals(pending.Campaign, _galaxy) &&
            ReferenceEquals(pending.Fleet, fleet) && pending.MissionOrderRevision == fleet.MissionOrderRevision;
        var result = _coreSimulation.IssueCivilianReturnToBaseOrder(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id, confirm);
        _returnConfirmation = result.RequiresConfirmation
            ? new ReturnConfirmation(_galaxy, fleet, fleet.MissionOrderRevision)
            : null;
        SetStatus(result.Message, 7);
        QueueRedraw();
    }

    public string UiSelectedCivilianReturnPreview => SelectedFleet is not { } fleet
        ? "Select a civilian mission ship to review recovery options."
        : _coreSimulation.PreviewCivilianReturnToBase(_galaxy, _galaxy.PlayerCivilizationId, fleet.Id).Message;

    public bool UiSelectedCivilianReturnNeedsConfirmation => SelectedFleet is { } fleet && _returnConfirmation is { } pending &&
        ReferenceEquals(pending.Campaign, _galaxy) && ReferenceEquals(pending.Fleet, fleet) &&
        pending.MissionOrderRevision == fleet.MissionOrderRevision;

    public string UiFleetDestinationPreview
    {
        get
        {
            if (SelectedFleet is not { } fleet) return "Select a ship to preview its orders.";
            if (UiIsSystemSpatialView)
            {
                if (fleet.Role == FleetRole.Colony && (_systemSpatialCanvas?.HoveredBodyId ?? UiSelectedBodyId) is int bodyId)
                {
                    var outpost = ResourceOutpostOpportunityPlanner.IsOutpostFleet(fleet);
                    var assessment = outpost
                        ? new ResourceOutpostOpportunityPlanner().AssessOrder(_galaxy, fleet.Id, _selectedSystemId, bodyId)
                        : null;
                    var colony = outpost ? null : _colonization.AssessColonyOrder(_galaxy, fleet.Id, _selectedSystemId, bodyId);
                    if (!(assessment?.Accepted ?? colony!.Accepted)) return assessment?.Message ?? colony!.Message;
                    var authorized = !fleet.PreventAutomaticSettlement && (fleet.DestinationPlanetaryBodyId is not null || fleet.SettlementBodyId is not null);
                    var cost = authorized ? 0 : outpost ? ColonizationSimulation.ResourceOutpostExpeditionCreditCost : ColonizationSimulation.ColonyExpeditionCreditCost;
                    return $"{UiFormatMoney(cost)} • At least {ColonizationSimulation.EstablishmentDays(fleet):0} game days\n" +
                        (PlayerEconomy.Credits < cost ? "Additional funding required." : "Right-click this world to begin settlement.");
                }
                return fleet.Role == FleetRole.Colony ? "Point at a surveyed world to review settlement costs and requirements." :
                    "Scouts and science vessels work after arrival. Select a star in the galaxy view to set a new course.";
            }
            if (_hoverDestinationId is not int targetId) return "Hover a star to preview its route. Right-click to travel.";
            var reach = UiGetFleetRouteAssessment(fleet.Id, targetId);
            if (!reach.ReachSupported) return reach.Reason;
            var funding = CivilizationOperatingCapacity.GetFundingFraction(_galaxy, fleet.CivilizationId);
            var speed = fleet.StrategicSpeed * funding;
            var time = speed > 0 ? $"{reach.DistanceLy / speed:0.0} game days" : "Awaiting operations funding";
            return $"{MetricFormat.InterstellarDistance(reach.DistanceLy)}\n{time} • Fuel {MetricFormat.InterstellarLength(reach.DistanceLy)}\nRight-click to set course.";
        }
    }
}
