using System;
using System.Linq;
using Godot;
using Game.Diagnostics;
using Game.Presentation.Spatial;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Presentation;

public enum SpatialPresentationScale
{
    StellarRegion,
    StarSystem,
    GalaxyOverview,
    PlanetFocus,
}

/// <summary>
/// Integrated scale/navigation adapter for the map. It consumes the observer-safe exploration
/// read model, preserves stellar-map selection/pan/zoom, and never mutates simulation state.
/// </summary>
public partial class Main
{
    private readonly ExplorationReadModel _spatialExplorationReadModel = new();
    private readonly InterstellarLaneNetwork _spatialLaneNetwork = new();
    private readonly SystemSpatialProjection _systemSpatialProjection = new();
    private readonly SystemSpatialViewState _systemSpatialState = new();
    private SystemSpatialCanvas? _systemSpatialCanvas;

    public SpatialPresentationScale UiSpatialScale =>
        _systemSpatialState.IsOpen
            ? (_systemSpatialCanvas?.IsPlanetFocused == true ? SpatialPresentationScale.PlanetFocus : SpatialPresentationScale.StarSystem)
            : (UiOverviewBlend > 0.5f ? SpatialPresentationScale.GalaxyOverview : SpatialPresentationScale.StellarRegion);

    public bool UiIsSystemSpatialView => _systemSpatialState.IsOpen;

    public string UiSpatialScaleLabel => UiSpatialScale switch
    {
        SpatialPresentationScale.StarSystem => "Star system",
        SpatialPresentationScale.PlanetFocus => "Planet focus",
        SpatialPresentationScale.GalaxyOverview => UiOverviewName,
        _ => "Stellar region",
    };

    protected void InitializeSpatialPresentation()
    {
        if (_systemSpatialCanvas is not null)
            return;

        _systemSpatialCanvas = new SystemSpatialCanvas
        {
            Name = "SystemSpatialCanvas",
            ZIndex = 100,
            IsNavigationBlocked = () => (UiIsMenuOpen || UiIsDeveloperToolsOpen) || UiIsSurfaceOpen,
            CanOpenSurface = id => PlanetSurfaceAvailable?.Invoke(id) == true,
            GetVisualStyle = () => UiVisualStyle,
        };
        _systemSpatialCanvas.ReturnRequested += BeginReturnToRegion;
        _systemSpatialCanvas.BodyOrderRequested += IssueSelectedFleetBodyOrder;
        _systemSpatialCanvas.GetLocalFleets = () => _galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == _galaxy.PlayerCivilizationId &&
                f.CurrentSystemId == _selectedSystemId && f.TransitPhase != FleetTransitPhase.InterstellarWarp)
            .OrderBy(f => f.Id).Select(f => new LocalFleetMarker(f.Id, f.Name, f.Role,
                ShipDesignRegistry.TryGet(f.DesignId, out var design) ? design!.Id : ShipDesignRegistry.GetCurrentDesignForRole(f.Role).Id,
                new Vector2(f.LocalTransitPosition.X, f.LocalTransitPosition.Y),
                new Vector2(f.LocalTransitTarget.X, f.LocalTransitTarget.Y),
                f.TransitPhase is FleetTransitPhase.LocalDeparture or FleetTransitPhase.LocalArrival,
                f.HoldRequested)).ToArray();
        _systemSpatialCanvas.GetLocalLanes = () =>
        {
            var current = _galaxy.Systems.FirstOrDefault(system => system.Id == _selectedSystemId);
            if (current is null) return Array.Empty<LocalLaneMarker>();
            return SystemLanePresentation.Build(
                _galaxy.Systems,
                _spatialLaneNetwork.Build(_galaxy.Systems),
                current.Id,
                systemId => _galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, systemId) >=
                    SystemSurveyLevel.PartiallySurveyed);
        };
        _systemSpatialCanvas.GetShipyardActivity = () =>
        {
            var yard = PlayerShipyard;
            if (yard.ActiveDesignId is not { } designId || !ShipDesignRegistry.TryGet(designId, out var design))
                return new ShipyardBuildActivity(null, 0, false, !UiIsPaused);
            var progress = design!.IndustryCost <= 0 ? 1 : Math.Clamp(yard.ActiveBuildProgress / design.IndustryCost, 0, 1);
            return new ShipyardBuildActivity(designId, progress, true, !UiIsPaused);
        };
        _systemSpatialCanvas.FleetSelected += id => UiSelectOwnedFleet(id);
        _systemSpatialCanvas.LaneSelected += UiInspectLaneDestination;
        _systemSpatialCanvas.IsObjectInspectorOpen = () => UiSelectedFleetId.HasValue || UiSelectedOrbitalConstruction is not null;
        _systemSpatialCanvas.OpenSurfaceRequested += id => PlanetSurfaceRequested?.Invoke(id);
        _systemSpatialCanvas.DescentRequested += UiBeginPlanetDescent;
        _systemSpatialCanvas.InfrastructureRequested += InspectOrbitalStructure;
        AddChild(_systemSpatialCanvas);
        _systemSpatialCanvas.SetSnapshot(null);
        InitializeSpatialNavigation();
    }

    private void UiInspectLaneDestination(int systemId)
    {
        var current = _galaxy.Systems.FirstOrDefault(system => system.Id == _selectedSystemId);
        var destination = _galaxy.Systems.FirstOrDefault(system => system.Id == systemId);
        if (current is null || destination is null || !SystemLanePresentation.HasCanonicalConnection(
                _spatialLaneNetwork.Build(_galaxy.Systems), current.Id, destination.Id)) return;
        if (_galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, destination.Id) < SystemSurveyLevel.PartiallySurveyed)
        {
            SetStatus("Long-range telemetry is incomplete. Dispatch a scout vessel to chart this system before approach.", 8.0);
            PublishReconnaissanceRequiredCue();
            return;
        }
        // Navigation only: no survey, order, or travel state changes.
        UiSelectSystem(destination.Id, "Connected system selected. Reconnaissance remains unchanged.");
        EnterSelectedSystemView();
    }

    protected void RefreshSpatialPresentation(double delta)
    {
        if (_systemSpatialCanvas is null)
            InitializeSpatialPresentation();
        RefreshSpatialNavigation(delta);
        if (!_systemSpatialState.IsOpen)
            return;

        if (_galaxy is null ||
            !_systemSpatialState.MatchesContext(_galaxy, _galaxy.PlayerCivilizationId, _selectedSystemId))
        {
            ReturnToStellarView(announce: false);
            return;
        }

        var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, _systemSpatialState.SystemId);
        if (surveyLevel < SystemSurveyLevel.PartiallySurveyed)
        {
            ReturnToStellarView(announce: false);
            SetStatus("System view closed because reconnaissance-grade orbital knowledge is no longer available.", 6.0);
            return;
        }

        if (!_systemSpatialState.NeedsRefresh(surveyLevel, delta))
            return;

        if (!_galaxy.Systems.Any(system => system.Id == _systemSpatialState.SystemId))
        {
            ReturnToStellarView(announce: false);
            return;
        }

        RebuildSystemSpatialSnapshot();
    }

    protected bool HandleSpatialPresentationInput(InputEvent @event)
    {
        if (@event is InputEventKey { Pressed: true, Echo: false, Keycode: Key.Backspace })
        {
            UiNavigateBack();
            return true;
        }
        if (!_systemSpatialState.IsOpen && UiOverviewBlend > 0.5f &&
            @event is InputEventMouseButton { Pressed: true, ButtonIndex: MouseButton.Right })
        {
            return true;
        }
        if (_systemSpatialState.IsOpen ||
            @event is not InputEventMouseButton mouse ||
            !mouse.Pressed ||
            mouse.ButtonIndex != MouseButton.Left ||
            !mouse.DoubleClick)
        {
            return false;
        }

        if (_galaxy is null || _selectedSystemId < 0)
            return false;

        if (FindNearestCatalogSystem(mouse.Position, 14.0f)?.Id != _selectedSystemId)
            return false;

        var surveyLevel = _galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, _selectedSystemId);
        if (surveyLevel < SystemSurveyLevel.PartiallySurveyed)
        {
            SetStatus("A detected star needs scout reconnaissance before its orbital system can be opened.", 6.0);
            return true;
        }

        EnterSelectedSystemView();
        return true;
    }

    private void EnterSelectedSystemView(bool starFocusedEntry = false)
    {
        if (_systemSpatialCanvas?.IsDetailedFocus == true)
        {
            _systemSpatialCanvas.ExitDetailedFocus();
            return;
        }
        if (_systemSpatialState.IsOpen) return;
        if (_systemSpatialCanvas is null)
            InitializeSpatialPresentation();
        if (_galaxy is null || _selectedSystemId < 0)
        {
            SetStatus("Select a star on the regional map first.", 6.0);
            return;
        }

        // Every entry point (button or double-click) shares the same knowledge gate.
        if (_galaxy.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, _selectedSystemId) < SystemSurveyLevel.PartiallySurveyed)
        {
            SetStatus("Send a scout to reconnoitre this star before opening its orbital system.", 7.0);
            return;
        }

        var previousStarScreen = UiGetCatalogScreenPosition(_selectedSystemId) ?? GetViewportRect().Size * 0.5f;
        SynchronizeRegionalCamera();
        _systemSpatialState.Open(_galaxy, _galaxy.PlayerCivilizationId, _selectedSystemId);
        _systemViewBlend = 0;
        _leavingSystem = false;
        _panning = false;
        RebuildSystemSpatialSnapshot();
        _systemSpatialCanvas?.BeginEntry(previousStarScreen);
        if (starFocusedEntry)
            _systemSpatialCanvas?.FocusStar();

        if (!_systemSpatialState.IsOpen)
            return;

        foreach (var marker in _scienceFleetMarkers.Values)
            marker.Visible = false;

        var selected = _galaxy.Systems.First(system => system.Id == _systemSpatialState.SystemId);
        SetStatus($"{selected.Name} · Drag to pan · Wheel to zoom · Select a planet to approach", 6.0);
        SupportLogger.Log("spatial-view", $"entered system={selected.Id} survey={_systemSpatialState.SurveyLevel} progress={_systemSpatialState.SurveyProgress:0.000}");
    }

    private void RebuildSystemSpatialSnapshot()
    {
        if (_systemSpatialCanvas is null || _galaxy is null || !_systemSpatialState.IsOpen)
            return;

        var exploration = _spatialExplorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId);
        var system = exploration.KnownSystems.FirstOrDefault(candidate => candidate.SystemId == _systemSpatialState.SystemId);
        if (system is null || !system.HasReconnaissanceCatalog)
        {
            ReturnToStellarView(announce: false);
            return;
        }

        var snapshot = _systemSpatialProjection.Build(system);
        // Our inhabited worlds are public to their owner. Never derive night lights from
        // hidden foreign colonies or turn a generic activity signature into a city map.
        var inhabited = _galaxy.Colonies.Where(c => c.CivilizationId == _galaxy.PlayerCivilizationId &&
            c.SystemId == snapshot.SystemId && c.PopulationMillions > 0).Select(c => c.PlanetaryBodyId).ToHashSet();
        snapshot = snapshot with { Bodies = snapshot.Bodies.Select(body => body with
            { HasCityLights = body.HasDetailedEnvironment && inhabited.Contains(body.BodyId) }).ToArray() };
        if (snapshot.SystemId == PlayerCivilization.HomeSystemId)
        {
            var construction = PlayerConstruction;
            snapshot = snapshot with
            {
                Infrastructure = ConstructionRegistry.All
                    .Where(project => project.Category == ConstructionCategory.Orbital)
                    .Select(project =>
                    {
                        var complete = construction.CompletedProjectIds.Contains(project.Id);
                        var active = construction.ActiveProjectId == project.Id;
                        var available = _construction.GetLockReason(
                            _galaxy, _galaxy.PlayerCivilizationId, project) is null;
                        var state = complete ? SystemSpatialInfrastructureState.Complete :
                            active ? SystemSpatialInfrastructureState.Active :
                            available ? SystemSpatialInfrastructureState.Available : SystemSpatialInfrastructureState.Locked;
                        var progress = complete ? 1 : active && project.IndustryCost > 0
                            ? Math.Clamp(construction.ActiveProjectProgress / project.IndustryCost, 0, 1) : 0;
                        return new SystemSpatialInfrastructureMarker(project.Id, project.Name, state, progress,
                            project.Id == "asteroid_resource_network" ? null :
                                _galaxy.Colonies.Where(c => c.CivilizationId == _galaxy.PlayerCivilizationId && c.SystemId == snapshot.SystemId)
                                    .OrderByDescending(c => c.PopulationMillions).FirstOrDefault()?.PlanetaryBodyId);
                    }).ToArray(),
            };
        }
        _systemSpatialCanvas.SetSnapshot(snapshot);
        _systemSpatialState.Refreshed(system.SurveyLevel, system.SurveyProgress);
    }

    private void ReturnToStellarView(bool announce)
    {
        if (!_systemSpatialState.IsOpen)
            return;

        var previousSystemId = _systemSpatialState.SystemId;
        _systemSpatialCanvas?.SetSnapshot(null);
        UiCloseOrbitalInspector();
        _systemSpatialState.Close();
        _systemViewBlend = 0;
        _leavingSystem = false;
        _panning = false;
        foreach (var marker in _scienceFleetMarkers.Values)
            marker.Visible = true;
        QueueRedraw();

        if (announce)
            SetStatus("Returned to the stellar map with the previous system selection preserved.", 5.0);
        SupportLogger.Log("spatial-view", $"returned-to-stellar-map system={previousSystemId}");
    }
}
