using System;
using System.Collections.Generic;
using System.Linq;
using Game.Presentation.PlanetIdentity;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Presentation;

public partial class PlanetSurfaceView
{
    private PlanetPresentation? _planetIdentity;
    private bool _visualPreview;
    private readonly List<DirectionalLight3D> _companionSuns = new();
    public PlanetPresentation? VisualIdentity => _planetIdentity;
    public bool HasSurfaceAtmosphere => _planetIdentity?.HasAtmosphere ?? SurfaceVisualClass != "airless";

    private void ApplyPlanetIdentity(PlanetPresentation? identity)
    {
        if (identity is null || identity == _planetIdentity) return;
        _planetIdentity = identity;
        SurfaceVisualClass = identity.HasAtmosphere ? identity.SurfaceFamily : "airless";
        _terrainMaterial.Shader = GD.Load<Shader>(PlanetVisualCatalog.SurfaceShader);
        PlanetIdentityMaterials.Configure(_terrainMaterial, identity);
        _terrainMaterial.SetShaderParameter("preview", _visualPreview);
        var replacement = CreateTerrain();
        _world.GetNode<MeshInstance3D>("Terrain").Mesh = replacement.Mesh;
        replacement.Free();
        _sun.RotationDegrees = new Vector3(-18, -155, 0);
        PlanetIdentityMaterials.Sky(_surfaceSky, identity.Sky, identity);
        _surfaceSky.SetShaderParameter("sun_direction", _sun.GlobalBasis.Z.Normalized());
        _environment.Sky.SkyMaterial = _surfaceSky;
        ApplyIdentityAtmosphere();
        _sun.LightColor = new Color(identity.Sky.Primary.Color);
        _sun.LightEnergy = identity.Sky.Primary.Intensity * 1.3f;
        _sun.LightAngularDistance = identity.HasAtmosphere ? identity.Sky.Primary.ApparentRadius : .03f;
        _skyMaterial.SkyTopColor = new Color(identity.Variant.Atmosphere) * .37f;
        _skyMaterial.SkyHorizonColor = new Color(identity.Variant.Atmosphere);
        foreach (var light in _companionSuns) light.QueueFree();
        _companionSuns.Clear();
        foreach (var star in new[] { identity.Sky.Secondary, identity.Sky.Tertiary }.Where(s => s is not null))
        {
            var light = new DirectionalLight3D
            {
                Name = "CompanionSun",
                LightColor = new Color(star!.Color),
                LightEnergy = star.Intensity * .22f,
                ShadowEnabled = false,
                RotationDegrees = new Vector3(_companionSuns.Count == 0 ? -27 : -18, _companionSuns.Count == 0 ? -169 : 162, 0)
            };
            _world.AddChild(light); _companionSuns.Add(light);
        }
    }
    private float VisualTerrainHeight(float x, float z)
    {
        var physical = Game.Simulation.Construction.SurfaceConstruction.TerrainHeight(x, z);
        if (_planetIdentity is not { } p) return physical;
        // Every buildable coordinate retains its exact simulation height. Only distant
        // scenery (or the explicit non-interactive specimen) receives class morphology.
        var blend = _visualPreview ? 1 : Mathf.SmoothStep(544, 1100, Math.Max(Math.Abs(x), Math.Abs(z)));
        if (blend <= 0) return physical;
        var phase = p.ShaderSeed * .11f;
        var broad = MathF.Sin(x * .0024f + phase) * MathF.Cos(z * .0028f - phase);
        var ridge = MathF.Abs(MathF.Sin(x * .009f + MathF.Sin(z * .004f) * 2 + phase));
        float visual = p.Variant.Pattern switch
        {
            1 => 9 + 13 * MathF.Sin(x * .017f + z * .003f + MathF.Sin(z * .008f) * 1.2f + phase) + broad * 8,
            2 => 8 + MathF.Pow(ridge, 6) * 34 + broad * 10,
            0 => 8 + broad * 17 - MathF.Pow(MathF.Max(0, MathF.Sin(x * .018f + phase) * MathF.Cos(z * .017f)), 5) * 34,
            6 => 12 + MathF.Pow(ridge, 3) * 48 + broad * 14,
            7 => 10 + MathF.Pow(ridge, 4) * 26 + broad * 14,
            8 => 12 + MathF.Pow(ridge, 3) * 85 + broad * 35,
            9 => 6 + Mathf.SmoothStep(.35f, .55f, ridge) * 20 + broad * 4,
            _ => physical + broad * 12
        };
        if (p.Classification.Class is PlanetClass.Ocean or PlanetClass.HighPressureOcean) visual = 5;
        return Mathf.Lerp(physical, visual, blend);
    }
    private void ApplyIdentityAtmosphere()
    {
        if (_planetIdentity is not { } p) return;
        _environment.FogEnabled = p.HasAtmosphere;
        _environment.FogSkyAffect = p.HasAtmosphere ? .16f * p.AtmosphereDensity : 0;
        _environment.FogDensity = p.HasAtmosphere ? .00008f + .00065f * p.AtmosphereDensity * p.Variant.Haze : 0;
        _environment.FogLightColor = new Color(p.Variant.Atmosphere).Lerp(new Color(p.Sky.Primary.Color), .10f);
        _environment.AmbientLightEnergy = p.HasAtmosphere ? .16f + p.AtmosphereDensity * .22f : .075f;
        _environment.AmbientLightColor = p.HasAtmosphere ? new Color(p.Variant.Atmosphere) : new Color("959ba4");
    }
    /// <summary>Developer-only synthetic fixture. Uses the actual colony renderer, without a
    /// campaign, placement callbacks, settlement assets or hidden world information.</summary>
    public void OpenVisualPreview(PlanetPresentation identity, IReadOnlyList<SystemSpatialBodyMarker>? companions = null)
    {
        if (!identity.CanShowSolidSurface) throw new ArgumentException("Gas envelopes have no solid ground view.");
        _visualPreview = true; Open(); ApplyPlanetIdentity(identity);
        foreach (var panel in _overlayPanels) panel.Hide();
        foreach (var node in _world.GetChildren().OfType<Node3D>())
            if (node != _sun && node.Name.ToString() != "Terrain" &&
                node is MeshInstance3D mesh && mesh.MaterialOverride != _terrainMaterial) node.Hide();
        ReadSkyCompanions = () => companions ?? Array.Empty<SystemSpatialBodyMarker>();
        _distance = 240; _yaw = 0; _pitch = -.08f; _target = new Vector3(0, 62, -280);
        UpdateCamera(); RefreshSkyCompanions();
    }
}
