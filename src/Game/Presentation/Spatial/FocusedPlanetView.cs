using System;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>A camera-positioned GPU planet disc with correctly ordered Saturn rings.</summary>
public partial class FocusedPlanetView : Control
{
    private TextureRect? _planet;
    private PlanetRingArc? _backRings;
    private PlanetRingArc? _frontRings;
    private int? _bodyId;
    private ShaderMaterial? _material;

    public FocusedPlanetView()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        ClipContents = false;
    }

    public void SetBody(SystemSpatialBodyMarker body)
    {
        ArgumentNullException.ThrowIfNull(body);
        EnsureChildren();
        if (_bodyId is int previous && previous != body.BodyId)
            CelestialBodyMaterials.ReleasePlanetMaterial(previous);
        _bodyId = body.BodyId;
        _material = CelestialBodyMaterials.GetPlanetMaterial(body);
        _planet!.Material = _material;
        var rings = body.HasDetailedEnvironment
            && body.VisualClass is not (SystemSpatialBodyVisualClass.UnknownPlanet or SystemSpatialBodyVisualClass.UnknownMoon)
            && body.SurfaceKey == "saturn";
        _backRings!.Visible = rings;
        _frontRings!.Visible = rings;
        SetLightDirection(new Vector2(-body.OffsetX, -body.OffsetY));
    }

    /// <summary>The rectangle bounds the solid sphere; Saturn rings extend beyond it.</summary>
    public void SetDiscRect(Rect2 rectangle)
    {
        EnsureChildren();
        var sizeChanged = Size != rectangle.Size;
        Position = rectangle.Position;
        Size = rectangle.Size;
        _planet!.Position = rectangle.Size * -.04f;
        _planet.Size = rectangle.Size * 1.08f;
        _backRings!.Size = rectangle.Size;
        _frontRings!.Size = rectangle.Size;
        if (sizeChanged)
        {
            _backRings.QueueRedraw();
            _frontRings.QueueRedraw();
        }
    }

    public void SetLightDirection(Vector2 towardStar)
    {
        if (_material is not null)
            CelestialBodyMaterials.UpdateLighting(_material, towardStar);
    }

    public override void _ExitTree()
    {
        if (_bodyId is int id) CelestialBodyMaterials.ReleasePlanetMaterial(id);
    }

    private void EnsureChildren()
    {
        if (_planet is not null) return;
        _backRings = new PlanetRingArc { Name = "BackRings", Front = false, Visible = false };
        _planet = new TextureRect
        {
            Name = "PlanetDisc",
            Texture = CelestialBodyMaterials.WhiteTexture,
            ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.Scale,
            MouseFilter = MouseFilterEnum.Ignore,
        };
        _frontRings = new PlanetRingArc { Name = "FrontRings", Front = true, Visible = false };
        AddChild(_backRings);
        AddChild(_planet);
        AddChild(_frontRings);
    }
}

/// <summary>Illustrative Saturn ring plane, rendered behind and in front of the sphere.</summary>
public partial class PlanetRingArc : Control
{
    public bool Front { get; set; }
    public PlanetRingArc() => MouseFilter = MouseFilterEnum.Ignore;

    public override void _Draw()
    {
        var center = Size / 2.0f;
        var radius = Mathf.Min(Size.X, Size.Y) / 2.0f;
        if (radius <= 0.0f) return;
        const float tilt = -0.36f;
        for (var band = 0; band < 23; band++)
        {
            if (band is 14 or 15) continue; // Schematic Cassini division.
            var distance = radius * (1.21f + band * 0.043f);
            var points = new Vector2[129];
            for (var point = 0; point < points.Length; point++)
            {
                var angle = (Front ? 0.0f : MathF.PI) + point / 128.0f * MathF.PI;
                points[point] = center + new Vector2(MathF.Cos(angle) * distance, MathF.Sin(angle) * distance * 0.31f).Rotated(tilt);
            }
            var highlight = band < 7 ? 0.70f : band < 14 ? 0.90f : 0.78f;
            var color = new Color(0.79f * highlight, 0.73f * highlight, 0.61f * highlight, Front ? 0.79f : 0.55f);
            DrawPolyline(points, color, Math.Max(0.65f, radius * 0.036f), true);
        }
    }
}
