using System;
using System.Collections.Generic;
using Godot;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

public partial class SystemSpatialCanvas
{
    private readonly SmoothSpatialCamera _camera = new();
    private bool _cameraReady;
    private bool _systemPanning;
    private bool _leftPanCandidate;
    private bool _leftPanMoved;
    private Vector2 _leftPanStart;
    private bool _rotating;
    private bool _descentRequested;
    private int? _focusedBodyId;
    private int? _focusedFleetId;
    private bool _starFocused;

    public Func<bool>? IsNavigationBlocked { get; set; }
    public bool IsPlanetFocused => _focusedBodyId.HasValue;
    public bool IsFleetFocused => _focusedFleetId.HasValue;
    public bool IsStarFocused => _starFocused;
    public bool IsDetailedFocus => IsPlanetFocused || IsFleetFocused || IsStarFocused;
    public int? FocusedBodyId => _focusedBodyId;
    public string SystemName => _snapshot?.CatalogName ?? "System";
    public IReadOnlyList<SystemSpatialBodyMarker> VisibleBodies => _snapshot?.Bodies ?? Array.Empty<SystemSpatialBodyMarker>();
    public int CachedSurfaceCount => _surfaces.Count + (_scene?.BodyCount ?? 0);
    internal SmoothSpatialCamera Camera => _camera;
    private SystemSpatialViewport CurrentViewport => _cameraReady
        ? new(_camera.OriginX, _camera.OriginY, _camera.Scale)
        : _snapshot is null ? new(Size.X / 2, Size.Y / 2, 1) : SystemSpatialViewport.Fit(_snapshot, Size.X, Size.Y);
    private float OrbitalContextOpacity => IsPlanetFocused ? 0 : 1;

    public void BeginEntry(Vector2 previousStarScreen)
    {
        if (_snapshot is null) return;
        var fit = SystemSpatialViewport.Fit(_snapshot, Size.X, Size.Y);
        _camera.Snap(fit.Scale * 0.35f, previousStarScreen.X, previousStarScreen.Y);
        _camera.SetTarget(fit.Scale, fit.CenterX, fit.CenterY);
        _cameraReady = true;
        _scene.SetEntry(previousStarScreen);
    }

    public void ZoomAt(float factor, Vector2 anchor)
    {
        if (_snapshot is null || IsNavigationBlocked?.Invoke() == true) return;
        if (IsPlanetFocused)
        {
            if (factor < 1 && _scene.TargetDistance / factor > _scene.PlanetFocusExitDistance)
                ExitPlanetFocus();
            else
            {
                _descentRequested = false;
                _scene.Zoom(factor, anchor);
            }
            return;
        }
        if (IsFleetFocused)
        {
            if (factor < 1) ExitFleetFocus(); else _scene.Zoom(factor, anchor);
            return;
        }
        if (IsStarFocused)
        {
            if (factor < 1 && _scene.TargetDistance / factor > _scene.StarFocusExitDistance)
                ExitDetailedFocus();
            else
                _scene.Zoom(factor, anchor);
            return;
        }
        ExitFleetFocus();
        EnsureOrbitalCamera();
        var fit = SystemSpatialViewport.Fit(_snapshot, Size.X, Size.Y);
        if (factor < 1 && _camera.TargetScale * factor < fit.Scale * 0.58f)
        {
            ReturnRequested?.Invoke();
            return;
        }
        // Wheel navigation never changes selection or enters a focused view. An absolute
        // upper bound also lets small bodies fill the screen in very large catalogues.
        _camera.ZoomAt(factor, anchor.X, anchor.Y, fit.Scale * 0.58f,
            Math.Max(fit.Scale * 512f, 48f));
    }

    private void FocusFleet(int fleetId)
    {
        if (IsNavigationBlocked?.Invoke() == true || IsPlanetFocused || !_scene.FocusFleet(fleetId)) return;
        _focusedFleetId = fleetId;
        _scene.Visible = true;
        QueueRedraw();
    }

    public void FocusStar()
    {
        if (_snapshot is null || IsNavigationBlocked?.Invoke() == true) return;
        _focusedBodyId = null;
        _focusedFleetId = null;
        _starFocused = true;
        _scene.FocusStar();
        _scene.Visible = true;
        QueueRedraw();
    }

    public void ExitDetailedFocus()
    {
        if (!IsDetailedFocus) return;
        _focusedBodyId = null;
        _focusedFleetId = null;
        _starFocused = false;
        _scene.ExitFocus();
        _scene.Visible = false;
        _systemPanning = _leftPanCandidate = _leftPanMoved = _rotating = false;
        QueueRedraw();
    }

    private void ExitFleetFocus()
    {
        if (!IsFleetFocused) return;
        _focusedFleetId = null;
        _starFocused = false;
        _scene.ExitFocus();
        _scene.Visible = false;
        QueueRedraw();
    }

    public void FocusSelectedBody()
    {
        if (IsNavigationBlocked?.Invoke() == true || IsPlanetFocused ||
            _selectedBodyId is not int id || !_bodiesById.TryGetValue(id, out var body)) return;
        EnsureOrbitalCamera();
        _focusedBodyId = id;
        _focusedFleetId = null;
        _starFocused = false;
        _scene.Visible = true;
        _descentRequested = false;
        _systemPanning = false;
        _leftPanCandidate = false;
        _leftPanMoved = false;
        _scene.FocusBody(id);
        QueueRedraw();
    }

    public bool FocusBody(int bodyId)
    {
        if (!_bodiesById.ContainsKey(bodyId)) return false;
        if (IsPlanetFocused) ExitPlanetFocus();
        _selectedBodyId = bodyId;
        FocusSelectedBody();
        return _focusedBodyId == bodyId;
    }

    public void ExitPlanetFocus()
    {
        if (!IsPlanetFocused) return;
        _focusedBodyId = null;
        _focusedFleetId = null;
        _starFocused = false;
        _scene.ExitFocus();
        _scene.Visible = false;
        _systemPanning = false;
        _leftPanCandidate = false;
        _leftPanMoved = false;
        QueueRedraw();
    }

    private void EnsureOrbitalCamera()
    {
        if (_cameraReady || _snapshot is null) return;
        var fit = SystemSpatialViewport.Fit(_snapshot, Size.X, Size.Y);
        _camera.Snap(fit.Scale, fit.CenterX, fit.CenterY);
        _cameraReady = true;
    }

    private void ResetSpatialCamera()
    {
        _cameraReady = false;
        _focusedBodyId = null;
        _focusedFleetId = null;
        _starFocused = false;
        _systemPanning = false;
        _leftPanCandidate = false;
        _leftPanMoved = false;
        _descentRequested = false;
        if (_scene is not null)
        {
            _scene.ExitFocus();
            _scene.Visible = false;
        }
    }

    public void PrepareSurfaceReturn()
    {
        _descentRequested = true;
        if (_focusedBodyId is int id) _scene.FocusBody(id);
    }
}
