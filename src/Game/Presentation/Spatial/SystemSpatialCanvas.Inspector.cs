using System;
using Godot;

namespace Game.Presentation.Spatial;

public partial class SystemSpatialCanvas
{
    private PlanetInspectorPanel? _inspector;
    public Func<int, bool>? CanOpenSurface { get; set; }
    public event Action<int>? OpenSurfaceRequested;

    private void UpdatePlanetInspector()
    {
        if (_snapshot is null) return;
        if (_inspector is null)
        {
            _inspector = new PlanetInspectorPanel { ZIndex = 20 };
            _inspector.SelectRequested += id =>
            {
                if (IsNavigationBlocked?.Invoke() == true) return;
                if (IsPlanetFocused) ExitPlanetFocus();
                _selectedBodyId = id; UpdatePlanetInspector(); QueueRedraw();
            };
            _inspector.FocusRequested += FocusSelectedBody;
            _inspector.SurfaceRequested += id =>
            {
                if (IsNavigationBlocked?.Invoke() == true || CanOpenSurface?.Invoke(id) != true) return;
                FocusSelectedBody(); OpenSurfaceRequested?.Invoke(id);
            };
            AddChild(_inspector);
        }
        _inspector.Visible = IsObjectInspectorOpen?.Invoke() != true;
        var selected = _focusedBodyId ?? _selectedBodyId;
        _inspector.Position = new(Size.X - 282, 84);
        _inspector.Size = new(270, Math.Max(120, Size.Y - 132));
        _inspector.Present(_snapshot, selected, selected is int id && CanOpenSurface?.Invoke(id) == true);
    }
}
