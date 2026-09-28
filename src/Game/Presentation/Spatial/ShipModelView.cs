using Godot;
using Game.Presentation;

namespace Game.Presentation.Spatial;

/// <summary>Compact live 3D ship preview for inspectors; it shares exact silhouettes with the system renderer.</summary>
public partial class ShipModelView : TextureRect
{
    private SubViewport _viewport = null!;
    private Node3D _model = null!;
    private string _designId = "";
    private CivilizationVisualStyle? _style;

    public override void _Ready()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        ExpandMode = ExpandModeEnum.IgnoreSize;
        StretchMode = StretchModeEnum.KeepAspectCentered;
        CustomMinimumSize = new(0, 104);
        _viewport = new SubViewport { Size = new(360, 180), TransparentBg = true, OwnWorld3D = true, RenderTargetUpdateMode = SubViewport.UpdateMode.WhenVisible, Msaa3D = Viewport.Msaa.Msaa4X };
        AddChild(_viewport); Texture = _viewport.GetTexture();
        var camera = new Camera3D { Projection = Camera3D.ProjectionType.Orthogonal, Size = 4.8f, Position = new(8, 6, 10) };
        _viewport.AddChild(camera); camera.LookAt(new(0, 0, .25f));
        _viewport.AddChild(new DirectionalLight3D { RotationDegrees = new(-42, -32, 0), LightColor = new("fff0d8"), LightEnergy = 2.15f });
        _viewport.AddChild(new WorldEnvironment { Environment = new Environment { BackgroundMode = Environment.BGMode.Color, BackgroundColor = Colors.Transparent, AmbientLightSource = Environment.AmbientSource.Color, AmbientLightColor = new("7694af"), AmbientLightEnergy = .72f } });
        if (!string.IsNullOrWhiteSpace(_designId)) RefreshModel();
        Resized += ResizePreview;
        ResizePreview();
    }

    public void Present(string designId, CivilizationVisualStyle? style = null)
    {
        if (_designId == designId && _style == style && _model is not null) return;
        _designId = designId; _style = style;
        if (_viewport is not null) RefreshModel();
    }

    private void RefreshModel()
    {
        if (_model is not null) { _viewport.RemoveChild(_model); _model.QueueFree(); }
        _model = ShipGeometry.Create(_designId, _style, highDetail: true);
        _model.RotationDegrees = new(-8, -25, 0);
        _viewport.AddChild(_model);
    }

    private void ResizePreview()
    {
        if (_viewport is null || Size.X < 2 || Size.Y < 2) return;
        var pixels = Size * GetViewport().GetFinalTransform().Scale;
        _viewport.Size = new((int)Mathf.Clamp(pixels.X, 64, 2048), (int)Mathf.Clamp(pixels.Y, 64, 2048));
    }
}
