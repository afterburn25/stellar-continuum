using System;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>Stable local sky for one system, shared by its orbital and planet cameras.</summary>
public partial class SystemSkyBackdrop : Control
{
    private readonly record struct SkyStar(Vector2 Position, float Radius, Color Color, bool Bright);
    private SkyStar[] _stars = Array.Empty<SkyStar>();
    private TextureRect? _gas;
    private ShaderMaterial? _gasMaterial;
    private static Shader? _shader;
    private int? _systemId;
    private Game.Presentation.PlanetIdentity.SystemSkyProfile? _profile;
    public int? SystemId => _systemId;
    public int StarCount => _stars.Length;
    public void SetProfile(Game.Presentation.PlanetIdentity.SystemSkyProfile profile)
    {
        if(_profile==profile)return;
        _systemId=null;SetSystem(profile.SystemId);_profile=profile;
        _gasMaterial!.SetShaderParameter("scenery_seed",(float)(profile.Identity%1000003)/113f);
        _gasMaterial.SetShaderParameter("gas_color",new Color(profile.NebulaColor));
        _gasMaterial.SetShaderParameter("secondary_color",new Color(profile.DustColor));
        _gasMaterial.SetShaderParameter("gas_density",profile.NebulaVisibility);
        var random=new Random(unchecked((int)profile.Identity));
        _stars=new SkyStar[profile.StarDensity];
        for(int i=0;i<_stars.Length;i++)_stars[i]=new(new((float)random.NextDouble(),(float)random.NextDouble()),i%47==0?1.3f:.5f,new Color("b9c6d7",.25f+(float)random.NextDouble()*.45f),i%47==0);
        QueueRedraw();
    }

    public SystemSkyBackdrop()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        Resized += () => { UpdateAspect(); QueueRedraw(); };
    }

    public void SetSystem(int systemId)
    {
        if (_systemId == systemId) return;
        _systemId = systemId;
        _gasMaterial ??= new ShaderMaterial { Shader = _shader ??= GD.Load<Shader>("res://assets/visual/shaders/system_sky.gdshader") };
        if (_gas is null)
        {
            _gas = new TextureRect { Texture = CelestialBodyMaterials.WhiteTexture, Material = _gasMaterial,
                ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize, MouseFilter = MouseFilterEnum.Ignore, ShowBehindParent = true };
            _gas.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect); AddChild(_gas);
        }
        var seed = unchecked((uint)systemId * 2654435761u + 0x534b59u);
        var random = new Random(unchecked((int)seed));
        // Variation is cosmetic and depends only on the public system identifier.
        // It never reveals unsurveyed star classes, planets, resources or anomalies.
        var palettes = new[] {
            ("745326", "253b52"), ("3d6480", "665478"), ("563244", "4c5b82"),
            ("31706b", "333b67"), ("795340", "47455e"), ("353d57", "655377") };
        var colors = palettes[(int)((uint)systemId % (uint)palettes.Length)];
        _gasMaterial.SetShaderParameter("scenery_seed", (seed % 4096) / 31f);
        _gasMaterial.SetShaderParameter("gas_color", new Color(colors.Item1));
        _gasMaterial.SetShaderParameter("secondary_color", new Color(colors.Item2));
        _gasMaterial.SetShaderParameter("gas_density", systemId % 5 == 4 ? .16f : .55f + (float)random.NextDouble()*.45f);
        _stars = new SkyStar[760 + random.Next(420)];
        for (var i=0; i<_stars.Length; i++)
        {
            var bright = i % 47 == 0;
            var tint = i % 7 == 0 ? new Color("eac897") : i % 5 == 0 ? new Color("abc7ed") : new Color("d2dbe5");
            tint.A = .15f + (float)random.NextDouble() * .57f;
            _stars[i] = new(new((float)random.NextDouble(), (float)random.NextDouble()),
                bright ? 1.2f + (float)random.NextDouble()*.65f : .35f+(float)random.NextDouble()*.42f, tint, bright);
        }
        UpdateAspect(); QueueRedraw();
    }

    private void UpdateAspect() => _gasMaterial?.SetShaderParameter("aspect", Size.X / Math.Max(1, Size.Y));

    public override void _Draw()
    {
        foreach (var star in _stars)
        {
            var point = star.Position * Size;
            if (star.Bright) CinematicArt.DrawStarlight(this, point, star.Radius, star.Color, star.Color.A*.8f);
            else DrawCircle(point, star.Radius, star.Color, true, -1, true);
        }
    }
}
