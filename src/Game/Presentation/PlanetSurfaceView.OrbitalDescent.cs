using System;
using Game.Presentation.Spatial;
using Game.Simulation.Construction;
using Godot;

namespace Game.Presentation;

public partial class PlanetSurfaceView
{
    private Node3D? _orbitalWorld;
    private MeshInstance3D? _regionalTerrain;
    private ShaderMaterial? _descentSky;
    private float _planetRadius;
    private float _orbitalDistance;
    private float _requestedOrbitalDistance;
    private float _orbitReturnDistance;
    private bool _descentAvailable;
    private bool _orbitalFlight;
    private bool _hasAtmosphere;
    public bool IsOrbitalFlight => _orbitalFlight;
    public float AltitudeMeters => _orbitalFlight ? _orbitalDistance : _camera.Position.Y;
    public Vector2I RenderResolution => _viewport.Size;

    private void ResizeSurfaceViewport()
    {
        if (_viewport is null || Size.X < 2 || Size.Y < 2) return;
        var scale = GetViewport().GetFinalTransform().Scale;
        var pixels = new Vector2I(Math.Max(2, Mathf.RoundToInt(Size.X * scale.X)),
            Math.Max(2, Mathf.RoundToInt(Size.Y * scale.Y)));
        if (_viewport.Size != pixels) _viewport.Size = pixels;
    }

    private void ResetOrbitalDescent()
    {
        _orbitalFlight = _descentAvailable = false;
        _orbitalWorld?.QueueFree(); _orbitalWorld = null;
        _regionalTerrain?.QueueFree(); _regionalTerrain = null;
        _descentSky = null;
        _distance = 205; _target = Vector3.Zero; _yaw = .65f; _pitch = .69f;
        if (!_built) return;
        _environment.Sky.SkyMaterial = _surfaceSky;
        _environment.FogEnabled = SurfaceVisualClass != "airless";
        // Match the ordinary surface environment after an orbital round trip.
        _environment.FogDensity = .00032f;
        _environment.AmbientLightEnergy = .42f;
        _camera.Near = .5f; _camera.Far = 3200;
    }

    /// <summary>Rebase the focused globe into a local tangent frame. The planet's angular
    /// size and camera orientation survive the unit change; the camera then traverses real
    /// altitude above the same spherical body into the existing simulation-owned colony.</summary>
    public void OpenFromOrbit(SystemSpatialBodyMarker body, Basis cameraBasis, Basis globeBasis, float altitudeRatio)
    {
        if (!IsOpen || !_built) return;
        _planetRadius = (float)Math.Clamp(body.RadiusEarth * 6_371_000.0, 500_000, 80_000_000);
        _orbitalDistance = Math.Max(ColonyOverviewDistance, altitudeRatio * _planetRadius);
        _requestedOrbitalDistance = _orbitalDistance;
        _orbitReturnDistance = _orbitalDistance * 1.35f;
        _descentAvailable = _orbitalFlight = true;
        _hasAtmosphere = PlanetMaterial3D.HasAtmosphere(body);
        _target = Vector3.Zero; _yaw = 0; _pitch = MathF.PI / 2 - .0001f;
        _orbitalWorld = new Node3D { Name = "OrbitalDescentWorld" };
        _world.AddChild(_orbitalWorld);
        _regionalTerrain = CreateRegionalTerrain();
        _world.AddChild(_regionalTerrain);
        var radial = cameraBasis.Z.Normalized();
        var east = cameraBasis.X.Normalized();
        var localFrame = new Basis(east, radial, east.Cross(radial).Normalized());
        var globe = new MeshInstance3D
        {
            Name = "ContinuousPlanetGlobe", Position = new(0, -_planetRadius, 0),
            Mesh = new SphereMesh { Radius = _planetRadius, Height = _planetRadius * 2,
                RadialSegments = 256, Rings = 128 },
            MaterialOverride = PlanetMaterial3D.Create(body), Basis = localFrame.Inverse() * globeBasis,
            CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
        };
        _orbitalWorld.AddChild(globe);
        ((ShaderMaterial)globe.MaterialOverride).SetShaderParameter("sun_direction",
            globe.Basis.Inverse() * _sun.GlobalBasis.Z.Normalized());
        if (_hasAtmosphere)
        {
            var atmosphere = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/atmosphere_shell.gdshader") };
            atmosphere.SetShaderParameter("atmosphere_color", PlanetMaterial3D.AtmosphereColor(body));
            atmosphere.SetShaderParameter("density", .10f);
            _orbitalWorld.AddChild(new MeshInstance3D
            {
                Name = "AtmosphereVolume", Position = globe.Position,
                Mesh = new SphereMesh { Radius = _planetRadius * 1.015f, Height = _planetRadius * 2.03f,
                    RadialSegments = 192, Rings = 96 }, MaterialOverride = atmosphere,
                CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
            });
        }
        _descentSky = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/descent_sky.gdshader") };
        _descentSky.SetShaderParameter("seed", (float)(body.BodyId % 1024));
        _environment.Sky.SkyMaterial = _descentSky;
        UpdateOrbitalEnvironment();
        UpdateCamera();
    }

    private void ZoomSurface(bool inward)
    {
        if (_orbitalFlight)
        {
            var factor = _requestedOrbitalDistance > 15000 ? .60f : .78f;
            _requestedOrbitalDistance = inward ? Math.Max(900, _requestedOrbitalDistance * factor)
                : _requestedOrbitalDistance / factor;
            if (_requestedOrbitalDistance > _orbitReturnDistance) ReturnToOrbit?.Invoke();
            return;
        }
        if (!inward && _descentAvailable && _distance >= ColonyOverviewDistance - 1)
        {
            _orbitalFlight = true;
            _orbitalDistance = _distance;
            _requestedOrbitalDistance = _distance / .78f;
            return;
        }
        _distance = Math.Clamp(inward ? _distance * .88f : _distance / .88f, StreetViewDistance, ColonyOverviewDistance);
    }

    private void AdvanceOrbitalDescent(double delta)
    {
        if (!_orbitalFlight) return;
        _orbitalDistance = Mathf.Lerp(_orbitalDistance, _requestedOrbitalDistance,
            (float)(1 - Math.Exp(-6 * Math.Clamp(delta, 0, .12))));
        if (Math.Abs(_orbitalDistance - _requestedOrbitalDistance) < Math.Max(.05f, _orbitalDistance * .00005f))
            _orbitalDistance = _requestedOrbitalDistance;
        if (_orbitalDistance <= ColonyOverviewDistance && _requestedOrbitalDistance <= 900)
        {
            _distance = _orbitalDistance;
            _orbitalFlight = false;
            // Hand off from the radial descent camera to the ordinary colony view.
            // Keeping the polar orbital pitch left ground controls looking straight down.
            _yaw = .65f;
            _pitch = .69f;
            _camera.Near = .5f; _camera.Far = 3200;
        }
        UpdateOrbitalEnvironment();
    }

    private void UpdateOrbitalEnvironment()
    {
        if (_descentSky is null) return;
        var altitude = _orbitalFlight ? _orbitalDistance : _distance;
        var density = _hasAtmosphere ? MathF.Exp(-altitude / 18000f) : 0;
        _descentSky.SetShaderParameter("air_density", density);
        _descentSky.SetShaderParameter("sky_color", _skyMaterial.SkyTopColor);
        _descentSky.SetShaderParameter("horizon_color", _skyMaterial.SkyHorizonColor);
        _environment.FogEnabled = _hasAtmosphere && altitude < 30000;
        // Meet the grounded scene at the same final values, while the high
        // atmosphere remains thin and dim.
        _environment.FogDensity = .00032f * density;
        _environment.AmbientLightEnergy = .12f + density * .30f;
        if (_orbitalWorld is not null) _orbitalWorld.Visible = altitude > 15000;
        if (_regionalTerrain is not null) _regionalTerrain.Visible = altitude < 100000;
    }

    private bool UpdateOrbitalCamera()
    {
        if (!_orbitalFlight) return false;
        // Keep the depth ratio bounded throughout descent. A planet-scale far plane
        // at street-level near distances makes single-precision frustum inversion fail.
        _camera.Near = Math.Max(.5f, _orbitalDistance * .002f);
        _camera.Far = Math.Max(3200, _orbitalDistance * 4 + Math.Min(_planetRadius * 2, _orbitalDistance * 10));
        var offset = new Vector3(MathF.Sin(_yaw) * MathF.Cos(_pitch), MathF.Sin(_pitch),
            MathF.Cos(_yaw) * MathF.Cos(_pitch)) * _orbitalDistance;
        _camera.Position = _target + offset;
        _camera.LookAt(_target, MathF.Abs(_pitch - MathF.PI / 2) < .001f ? Vector3.Forward : Vector3.Up);
        return true;
    }

    private MeshInstance3D CreateRegionalTerrain()
    {
        // Scenic LOD outside the colony, curved to the same planetary radius. Construction
        // continues to intersect only the authoritative local heightfield.
        const int segments = 256;
        float[] rings = { 1800, 2200, 3000, 4500, 7000, 11000, 17000, 26000, 40000, 65000, 100000, 160000, 250000 };
        var vertices = new Vector3[rings.Length * (segments + 1)];
        var normals = new Vector3[vertices.Length];
        var tangents = new float[vertices.Length * 4];
        var indices = new int[(rings.Length - 1) * segments * 6];
        for (var ring = 0; ring < rings.Length; ring++)
        for (var segment = 0; segment <= segments; segment++)
        {
            var angle = segment * MathF.Tau / segments;
            var x = MathF.Cos(angle) * rings[ring]; var z = MathF.Sin(angle) * rings[ring];
            var curvature = -rings[ring] * rings[ring] / (2 * _planetRadius);
            var relief = SurfaceConstruction.TerrainHeight(x, z) + MathF.Sin(x / 6400) * MathF.Cos(z / 8700) * 230;
            var height = Mathf.Lerp(SurfaceConstruction.TerrainHeight(x, z) - 1, curvature + relief,
                Math.Clamp((rings[ring] - 2200) / 12000, 0, 1));
            var i = ring * (segments + 1) + segment;
            vertices[i] = new(x, height, z);
            normals[i] = new Vector3(x / _planetRadius, 1, z / _planetRadius).Normalized();
            var tangent = Vector3.Right.Slide(normals[i]).Normalized();
            tangents[i * 4] = tangent.X; tangents[i * 4 + 1] = tangent.Y;
            tangents[i * 4 + 2] = tangent.Z; tangents[i * 4 + 3] = -1;
        }
        var write = 0;
        for (var ring = 0; ring < rings.Length - 1; ring++)
        for (var segment = 0; segment < segments; segment++)
        {
            var a = ring * (segments + 1) + segment; var b = a + segments + 1;
            indices[write++] = a; indices[write++] = b; indices[write++] = a + 1;
            indices[write++] = a + 1; indices[write++] = b; indices[write++] = b + 1;
        }
        var arrays = new Godot.Collections.Array(); arrays.Resize((int)Godot.Mesh.ArrayType.Max);
        arrays[(int)Godot.Mesh.ArrayType.Vertex] = vertices; arrays[(int)Godot.Mesh.ArrayType.Normal] = normals;
        arrays[(int)Godot.Mesh.ArrayType.Tangent] = tangents; arrays[(int)Godot.Mesh.ArrayType.Index] = indices;
        var mesh = new ArrayMesh(); mesh.AddSurfaceFromArrays(Godot.Mesh.PrimitiveType.Triangles, arrays);
        return new MeshInstance3D { Name = "RegionalTerrainLOD", Mesh = mesh, MaterialOverride = _terrainMaterial,
            CastShadow = GeometryInstance3D.ShadowCastingSetting.Off };
    }
}
