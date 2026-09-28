using System;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>Lit inspector preview of the same staged geometry used in the system scene.</summary>
public partial class OrbitalStructureView : TextureRect
{
    private SubViewport _viewport = null!;
    private Node3D _model = null!;
    private string _key = "";

    public override void _Ready()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        ExpandMode = ExpandModeEnum.IgnoreSize;
        StretchMode = StretchModeEnum.KeepAspectCentered;
        _viewport = new SubViewport { Size = new(256, 256), TransparentBg = true, OwnWorld3D = true,
            RenderTargetUpdateMode = SubViewport.UpdateMode.WhenVisible, Msaa3D = Viewport.Msaa.Msaa4X };
        AddChild(_viewport); Texture = _viewport.GetTexture();
        var camera = new Camera3D { Projection = Camera3D.ProjectionType.Orthogonal, Size = 18, Position = new(16, 18, 24) };
        _viewport.AddChild(camera); camera.LookAt(new(0, 1.2f, 0));
        _viewport.AddChild(new DirectionalLight3D { RotationDegrees = new(-40, -28, 0), LightColor = new("fff0d8"), LightEnergy = 2.2f });
        _viewport.AddChild(new WorldEnvironment { Environment = new Godot.Environment {
            BackgroundMode = Godot.Environment.BGMode.Color, BackgroundColor = Colors.Transparent,
            AmbientLightSource = Godot.Environment.AmbientSource.Color, AmbientLightColor = new("7594b3"), AmbientLightEnergy = .65f } });
        Resized += () => {
            if (Size.X < 2 || Size.Y < 2) return;
            var pixels = Size * GetViewport().GetFinalTransform().Scale;
            _viewport.Size = new((int)Mathf.Clamp(pixels.X, 64, 2048), (int)Mathf.Clamp(pixels.Y, 64, 2048));
        };
    }

    public void Present(string project, double progress)
    {
        var phase = Math.Clamp((int)Math.Ceiling(progress * 4), 1, 4);
        var key = $"{project}:{phase}";
        if (_key == key) return;
        _key = key;
        if (_model is not null) { _viewport.RemoveChild(_model); _model.QueueFree(); }
        _model = OrbitalStructureGeometry.Create(new SystemSpatialInfrastructureMarker(project, project,
            SystemSpatialInfrastructureState.Active, progress));
        _viewport.AddChild(_model);
    }
}
