using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Presentation;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>
/// Native perspective system renderer. It consumes only the observer-safe spatial snapshot:
/// meshes, inclinations, local sky and stations are cosmetic and never amend orbital facts.
/// The owning canvas remains responsible for selection state and user-command policy.
/// </summary>
public partial class SystemScene3D : Control
{
    private const float PrimaryStarRadius = SystemCelestialScale.PrimaryStarRadius;
    private readonly Dictionary<int, BodyVisual> _bodies = new();
    private readonly Dictionary<string, Node3D> _infrastructure = new(StringComparer.Ordinal);
    private TextureRect _presenter = null!;
    private SubViewport _viewport = null!;
    private Node3D _world = null!;
    private Camera3D _camera = null!;
    private OmniLight3D _vesselFill = null!;
    private DirectionalLight3D _vesselKey = null!;
    private Node3D _cameraRig = null!;
    private Node3D? _systemStar;
    private OmniLight3D? _systemLight;
    private Node3D? _localStars;
    private SystemSpatialSnapshot? _snapshot;
    private Vector3 _target, _targetTarget;
    private float _distance = 220, _targetDistance = 220;
    private float _yaw = -.72f, _targetYaw = -.72f;
    private float _pitch = .40f, _targetPitch = .40f;
    private Vector2 _entry;
    private int? _focusedBodyId;
    private bool _focusedStar;
    private OrbitalPose? _savedPose;
    private Shader? _atmosphereShader;
    private CivilizationVisualStyle _visualStyle = CivilizationVisualStyles.Terran;
    private readonly record struct OrbitalPose(Vector3 Target, float Distance, float Yaw, float Pitch);

    private sealed record BodyVisual(SystemSpatialBodyMarker Marker, Node3D Root, MeshInstance3D Globe, float Radius, MeshInstance3D? Atmosphere);

    public int? FocusedBodyId => _focusedBodyId;
    public Vector3 CameraPosition => _camera?.GlobalPosition ?? Vector3.Zero;
    public Vector3 CameraTarget => _target;
    public Vector3 TargetCameraTarget => _targetTarget;
    public float Distance => _distance;
    public float TargetDistance => _targetDistance;
    public bool IsMoving => !CameraHasConverged();
    public float Yaw => _yaw;
    public float Pitch => _pitch;
    public Basis CameraBasis => _camera?.GlobalTransform.Basis ?? Basis.Identity;
    public int BodyCount => _bodies.Count;
    public int InfrastructureCount => _infrastructure.Count;
    public int StarRootCount => _world is null ? 0 : _world.GetChildren()
        .Count(node => node.Name.ToString().StartsWith("SystemStar", StringComparison.Ordinal));
    public int PrimaryStarCount => _systemStar is null || !GodotObject.IsInstanceValid(_systemStar) ? 0 :
        _systemStar.GetChildren().Count(node => node.Name.ToString() == "StellarA");
    public int PrimaryCoronaCount => _systemStar is null || !GodotObject.IsInstanceValid(_systemStar) ? 0 :
        _systemStar.GetChildren().Where(node => node.Name.ToString() == "StellarA")
            .SelectMany(node => node.GetChildren()).Count(node => node.Name.ToString() == "StellarCorona");
    public void SetVisualStyle(CivilizationVisualStyle style)
    {
        if (_visualStyle == style) return;
        _visualStyle = style;
        if (_snapshot is null) return;
        ClearInfrastructure();
        foreach (var marker in _snapshot.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>()) BuildInfrastructure(marker);
    }
    public float FitDistance { get; private set; } = 220;
    public float StarFocusExitDistance => MathF.Max(
        PrimaryStarRadius * 5.6f,
        MathF.Min(FitDistance * .78f, PrimaryStarRadius * 6f));
    public float PlanetFocusExitDistance => _focusedBodyId is int id && _bodies.TryGetValue(id, out var body)
        ? body.Radius * 18f : FitDistance;
    public float FocusAltitudeRatio => _focusedBodyId is int id && _bodies.TryGetValue(id, out var body)
        ? MathF.Max(0, CameraPosition.DistanceTo(body.Root.GlobalPosition) / body.Radius - 1) : float.PositiveInfinity;

    public override void _Ready()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        _presenter = new TextureRect { Name = "SystemViewportPresenter", ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
            StretchMode = TextureRect.StretchModeEnum.Scale, MouseFilter = MouseFilterEnum.Ignore };
        AddChild(_presenter);
        _presenter.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        _viewport = new SubViewport { Name = "SystemViewport", TransparentBg = false, OwnWorld3D = true,
            RenderTargetUpdateMode = SubViewport.UpdateMode.WhenVisible, Msaa3D = Viewport.Msaa.Msaa4X };
        _presenter.AddChild(_viewport); _presenter.Texture = _viewport.GetTexture();
        _world = new Node3D { Name = "SystemWorld" };
        _viewport.AddChild(_world);
        _cameraRig = new Node3D { Name = "CameraRig" }; _world.AddChild(_cameraRig);
        _camera = new Camera3D { Name = "SystemCamera", Current = true, Fov = 48, Near = .08f, Far = 10000 };
        _cameraRig.AddChild(_camera);
        _vesselFill = new OmniLight3D { Name = "VesselFill", LightColor = new Color("c9dcff"),
            LightEnergy = 2.4f, OmniRange = 190, OmniAttenuation = .72f, ShadowEnabled = false,
            LightCullMask = 2, Visible = false };
        _vesselKey = new DirectionalLight3D { Name = "VesselKey", LightColor = new Color("fff0d8"),
            LightEnergy = 2.8f, ShadowEnabled = false, LightCullMask = 2, Visible = false };
        _camera.AddChild(_vesselFill); _camera.AddChild(_vesselKey);
        _world.AddChild(new WorldEnvironment { Environment = new Godot.Environment {
            BackgroundMode = Godot.Environment.BGMode.Sky,
            Sky = new Sky { SkyMaterial = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/local_space_sky.gdshader") } },
            AmbientLightSource = Godot.Environment.AmbientSource.Color, AmbientLightColor = new Color("b7c2ca"),
            AmbientLightSkyContribution = 0, AmbientLightEnergy = .25f,
            TonemapMode = Godot.Environment.ToneMapper.Aces, GlowEnabled = true, GlowIntensity = .65f } });
        Resized += ResizeViewport;
        ResizeViewport();
        UpdateCamera();
    }

    public void Clear()
    {
        _snapshot = null;
        _focusedBodyId = null;
        _focusedStar = false;
        _savedPose = null;
        ClearWorld();
    }

    public void Present(SystemSpatialSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        ResizeViewport(); // Presentation can arrive before the control's first layout notification.
        var changedSystem = _snapshot?.SystemId != snapshot.SystemId;
        var bodiesChanged = changedSystem || _snapshot is null || !_snapshot.Bodies.SequenceEqual(snapshot.Bodies) ||
            _snapshot.StarArchetype != snapshot.StarArchetype || _snapshot.StellarClass != snapshot.StellarClass ||
            _snapshot.SecondaryStellarClass != snapshot.SecondaryStellarClass || _snapshot.TertiaryStellarClass != snapshot.TertiaryStellarClass ||
            MathF.Abs(_snapshot.DesignRadius - snapshot.DesignRadius) > .001f;
        var infrastructureChanged = changedSystem || _snapshot is null ||
            !(_snapshot.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>()).SequenceEqual(snapshot.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>());
        if (!bodiesChanged && !infrastructureChanged) return;
        _snapshot = snapshot;
        if (changedSystem)
        {
            ClearWorld();
            BuildWorld(snapshot);
            ResetCamera();
            ApplyEntry();
        }
        else if (bodiesChanged)
        {
            // Marker refreshes may reveal safe classes or infrastructure, but never reposition
            // already projected coordinates from any unobserved simulation state.
            ClearWorld();
            BuildWorld(snapshot);
            if (_focusedBodyId is int id && !_bodies.ContainsKey(id)) ExitFocus();
        }
        else if (infrastructureChanged)
        {
            ClearInfrastructure();
            foreach (var marker in snapshot.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>()) BuildInfrastructure(marker);
        }
    }

    public void Advance(double delta)
    {
        VideoSettingsService.ApplyToViewport(_viewport);
        _target = SystemSceneCameraInterpolation.Advance(_target, _targetTarget, delta, 7.5);
        _distance = SystemSceneCameraInterpolation.Advance(_distance, _targetDistance, delta, 7.5);
        _yaw = SystemSceneCameraInterpolation.AdvanceAngle(_yaw, _targetYaw, delta, 7.5);
        _pitch = SystemSceneCameraInterpolation.Advance(_pitch, _targetPitch, delta, 7.5);
        if (CameraHasConverged())
        {
            _target = _targetTarget;
            _distance = _targetDistance;
            _yaw = _targetYaw;
            _pitch = _targetPitch;
        }
        UpdateCamera();
        if (_combatActive) AdvanceCombatPresentation(delta);
        else AdvanceLocalFleetModels(delta);
    }

    public void Pan(Vector2 screenDelta)
    {
        if (_camera is null) return;
        // Perspective frustum height at the target plane, per logical screen pixel.
        var scale = MathF.Max(.0001f, 2f * _distance * MathF.Tan(Mathf.DegToRad(_camera.Fov) * .5f) / MathF.Max(1f, Size.Y));
        var right = _camera.GlobalTransform.Basis.X.Normalized();
        var up = _camera.GlobalTransform.Basis.Y.Normalized();
        var offset = (-right * screenDelta.X + up * screenDelta.Y) * scale;
        _target += offset; _targetTarget += offset;
    }

    public void Rotate(Vector2 delta)
    {
        _targetYaw += delta.X * .008f;
        _targetPitch = Math.Clamp(_targetPitch + delta.Y * .006f, -.82f, 1.18f);
    }

    public void Zoom(float factor, Vector2 anchor)
    {
        _ = anchor;
        if (!float.IsFinite(factor) || factor <= 0) return;
        // After panning, approach the selected object rather than an empty offset in space.
        if (factor > 1 && !_combatActive)
        {
            if (_focusedBodyId is int targetId && _bodies.TryGetValue(targetId, out var targetBody))
                _targetTarget = targetBody.Root.Position;
            else if (_focusedStar) _targetTarget = Vector3.Zero;
        }
        var minimum = _combatActive ? 12f : _focusedBodyId is int id && _bodies.TryGetValue(id, out var focused)
            ? focused.Radius + Math.Max(.025f, focused.Radius * .005f)
            : _focusedStar ? PrimaryStarRadius * 1.005f
            : MathF.Max(8, FitDistance * .16f);
        _targetDistance = Math.Clamp(_targetDistance / factor, minimum, FitDistance * 2f);
    }

    public void FocusBody(int bodyId)
    {
        if (!_bodies.TryGetValue(bodyId, out var body)) return;
        DemoteFocusedFleet();
        SetVesselLighting(false);
        _focusedLocalFleetId = null;
        _focusedStar = false;
        if (_focusedBodyId is null) _savedPose = new(_targetTarget, _targetDistance, _targetYaw, _targetPitch);
        _focusedBodyId = bodyId;
        _targetTarget = body.Root.Position;
        // Approach from the day-side quarter; further mouse orbiting remains unrestricted.
        var towardStar = -body.Root.Position.Normalized();
        _targetYaw = MathF.Atan2(towardStar.X, towardStar.Z) + .72f;
        _targetPitch = body.Marker.SurfaceKey == "saturn" ? .80f : .34f;
        var framingRadius = body.Marker.SurfaceKey == "saturn" && body.Marker.HasDetailedEnvironment ? 8f : 3.8f;
        _targetDistance = Math.Clamp(body.Radius * framingRadius, body.Radius + .025f, FitDistance * .72f);
    }

    public void ExitFocus()
    {
        DemoteFocusedFleet();
        SetVesselLighting(false);
        _focusedBodyId = null;
        _focusedLocalFleetId = null;
        _focusedStar = false;
        if (_savedPose is { } pose)
        {
            _targetTarget = pose.Target; _targetDistance = pose.Distance; _targetYaw = pose.Yaw; _targetPitch = pose.Pitch;
            _savedPose = null;
        }
        else ResetCamera();
    }

    public void ResetCamera()
    {
        DemoteFocusedFleet();
        SetVesselLighting(false);
        _focusedBodyId = null;
        _focusedLocalFleetId = null;
        _focusedStar = false;
        _targetTarget = Vector3.Zero;
        _targetDistance = FitDistance;
        _targetYaw = -.72f;
        _targetPitch = .40f;
    }

    public void SetEntry(Vector2 previousScreen) => _entry = previousScreen;

    public Vector3? GetBodyWorldPosition(int bodyId) => _bodies.TryGetValue(bodyId, out var body) ? body.Root.GlobalPosition : null;
    public float? GetBodyRadius(int bodyId) => _bodies.TryGetValue(bodyId, out var body) ? body.Radius : null;
    public Basis? GetBodyBasis(int bodyId) => _bodies.TryGetValue(bodyId, out var body) ? body.Root.GlobalTransform.Basis : null;

    public Vector2? ProjectBody(int bodyId) => _bodies.TryGetValue(bodyId, out var body)
        ? ProjectPoint(body.Root.GlobalPosition) : null;

    public int? HitBody(Vector2 screen)
    {
        var nearest = float.MaxValue;
        int? hit = null;
        foreach (var pair in _bodies)
        {
            if (!RayHitsSphere(screen, pair.Value.Root.GlobalPosition, pair.Value.Radius, out var distance)) continue;
            if (distance < nearest) { nearest = distance; hit = pair.Key; }
        }
        return hit;
    }

    public Vector2? ProjectInfrastructure(string projectId) => _infrastructure.TryGetValue(projectId, out var node)
        ? ProjectPoint(node.GlobalPosition) : null;

    public string? HitInfrastructure(Vector2 screen)
    {
        string? nearest = null;
        var nearestPixels = 22f;
        foreach (var pair in _infrastructure)
            if (ProjectInfrastructure(pair.Key) is { } point && point.DistanceTo(screen) < nearestPixels)
            {
                nearestPixels = point.DistanceTo(screen);
                nearest = pair.Key;
            }
        return nearest;
    }

    public Vector2? ProjectPoint(Vector3 point)
    {
        if (_camera is null || _camera.IsPositionBehind(point)) return null;
        var projected = _camera.UnprojectPosition(point);
        projected /= NativeScale;
        return new Rect2(Vector2.Zero, Size).HasPoint(projected) ? projected : null;
    }

    public bool HasAtmosphere(int bodyId) => _bodies.TryGetValue(bodyId, out var body) && body.Atmosphere is not null;

    private void ResizeViewport()
    {
        if (_viewport is null || Size.X < 2 || Size.Y < 2) return;
        var native = GetWindow().Size;
        _viewport.Size = new Vector2I(Math.Max(2, native.X), Math.Max(2, native.Y));
    }

    private void ClearWorld()
    {
        ClearCombatPresentation();
        foreach (var body in _bodies.Values) ReleaseWorldNode(body.Root);
        foreach (var structure in _infrastructure.Values) ReleaseWorldNode(structure);
        _bodies.Clear(); _infrastructure.Clear();
        ClearLocalFleetModels();
        ReleaseWorldNode(_systemStar); _systemStar = null;
        ReleaseWorldNode(_systemLight); _systemLight = null;
        ReleaseWorldNode(_localStars); _localStars = null;
    }

    // Detaching immediately releases the exact scene-tree name before its queued disposal.
    // Successive system presentations can therefore never leave renamed stellar roots behind.
    private static void ReleaseWorldNode(Node? node)
    {
        if (node is null || !GodotObject.IsInstanceValid(node)) return;
        node.GetParent()?.RemoveChild(node);
        node.QueueFree();
    }

    private void ClearInfrastructure()
    {
        foreach (var structure in _infrastructure.Values) ReleaseWorldNode(structure);
        _infrastructure.Clear();
    }

    private void BuildWorld(SystemSpatialSnapshot snapshot)
    {
        FitDistance = MathF.Max(105, snapshot.DesignRadius * 2.15f);
        _camera.Far = Math.Max(10000f, FitDistance * 8f);
        _camera.Near = .005f;
        var sky = (ShaderMaterial)_world.GetChildren().OfType<WorldEnvironment>().Single().Environment.Sky.SkyMaterial;
        sky.SetShaderParameter("seed", (float)snapshot.SystemId);
        BuildLocalSky(snapshot.SystemId);
        BuildStar(snapshot);
        foreach (var marker in snapshot.Bodies.OrderBy(marker => marker.Kind).ThenBy(marker => marker.BodyId)) BuildBody(marker);
        foreach (var marker in snapshot.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>()) BuildInfrastructure(marker);
    }

    private void BuildStar(SystemSpatialSnapshot snapshot)
    {
        var star = new Node3D { Name = "SystemStar" }; _world.AddChild(star); _systemStar = star;
        // StellarClass enters this observer-safe snapshot only with the completed survey.
        // Keep unsurveyed systems neutral rather than exposing the generation data here.
        var known = snapshot.StellarClass.HasValue;
        var color = StellarColor(snapshot.StellarClass);
        AddStellarComponent(star, "A", known ? color : new Color("56616b"), PrimaryStarRadius, Vector3.Zero,
            snapshot.SystemId * 1.071f + 11f, known ? SolarTreatment(snapshot.StellarClass) : 0f);
        if (known && snapshot.SecondaryStellarClass is StellarPrimaryClass secondary)
            AddStellarComponent(star, "B", StellarColor(secondary), 196, new Vector3(850, 80, -390),
                snapshot.SystemId * 1.071f + 29f, SolarTreatment(secondary));
        if (known && snapshot.TertiaryStellarClass is StellarPrimaryClass tertiary)
            AddStellarComponent(star, "C", StellarColor(tertiary), 168, new Vector3(-820, -60, 480),
                snapshot.SystemId * 1.071f + 47f, SolarTreatment(tertiary));
        var light = new OmniLight3D { Name = "SystemLight", LightColor = known ? color.Lerp(Colors.White, .58f) : new Color("aeb9c0"),
            LightEnergy = known ? 1.55f : .7f, OmniAttenuation = .45f, OmniRange = FitDistance * 2.7f, ShadowEnabled = false };
        _world.AddChild(light); _systemLight = light;
    }

    public void FocusStar()
    {
        DemoteFocusedFleet();
        SetVesselLighting(false);
        _focusedBodyId = null;
        _focusedLocalFleetId = null;
        _focusedStar = true;
        _savedPose = null;
        _target = _targetTarget = Vector3.Zero;
        // The first close view keeps the full limb and active corona visible; wheel zoom can
        // continue down to a radius-relative safety limit without entering the photosphere.
        _distance = _targetDistance = PrimaryStarRadius * 4.45f;
        _yaw = _targetYaw = -.34f;
        _pitch = _targetPitch = .12f;
        UpdateCamera();
    }

    private static Color StellarColor(StellarPrimaryClass? stellarClass) => stellarClass switch
    {
        StellarPrimaryClass.MRedDwarf => new Color("e66d54"), StellarPrimaryClass.KOrangeDwarf => new Color("ff9d54"),
        StellarPrimaryClass.GYellowDwarf => new Color("ffd278"), StellarPrimaryClass.FYellowWhiteDwarf => new Color("fff1c7"),
        StellarPrimaryClass.AWhiteStar => new Color("e4efff"), StellarPrimaryClass.HotBlueStar => new Color("8dbdff"),
        StellarPrimaryClass.Giant => new Color("ff765c"), StellarPrimaryClass.WhiteDwarf => new Color("d9edff"),
        StellarPrimaryClass.NeutronStar => new Color("79cfff"), StellarPrimaryClass.Pulsar => new Color("67dcff"),
        StellarPrimaryClass.BlackHole => new Color("9b87d9"),
        StellarPrimaryClass.Protostar => new Color("ffb065"), _ => new Color("d5d9d6"),
    };

    private static float SolarTreatment(StellarPrimaryClass? stellarClass) => stellarClass switch
    {
        StellarPrimaryClass.GYellowDwarf => 1f,
        StellarPrimaryClass.KOrangeDwarf => .68f,
        StellarPrimaryClass.FYellowWhiteDwarf => .32f,
        _ => 0f,
    };

    private static void AddStellarComponent(Node3D parent, string label, Color color, float radius, Vector3 position,
        float seed, float solarTreatment)
    {
        var component = new Node3D { Name = "Stellar" + label, Position = position };
        var material = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/stellar_photosphere.gdshader") };
        material.SetShaderParameter("star_color", color);
        material.SetShaderParameter("seed", seed);
        material.SetShaderParameter("solar_treatment", solarTreatment);
        component.AddChild(new MeshInstance3D { Mesh = new SphereMesh { Radius = radius, Height = radius * 2, RadialSegments = 96, Rings = 48 }, MaterialOverride = material });
        var corona = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/stellar_corona.gdshader") };
        corona.SetShaderParameter("star_color", color);
        corona.SetShaderParameter("seed", seed);
        corona.SetShaderParameter("solar_treatment", solarTreatment);
        component.AddChild(new MeshInstance3D { Name = "StellarCorona", Mesh = new QuadMesh { Size = Vector2.One * radius * 4.7f },
            MaterialOverride = corona, CastShadow = GeometryInstance3D.ShadowCastingSetting.Off });
        parent.AddChild(component);
    }

    private void BuildBody(SystemSpatialBodyMarker marker)
    {
        var position = PositionFor(marker);
        if (marker.ParentBodyId is int parentId && _bodies.TryGetValue(parentId, out var parent))
        {
            position.Y += parent.Root.Position.Y;
        }
        var root = new Node3D { Name = $"Body_{marker.BodyId}", Position = position };
        if (marker.HasDetailedEnvironment && marker.SurfaceKey == "saturn") root.RotationDegrees = new(0, 0, 26.7f);
        _world.AddChild(root);
        var radius = marker.DisplayRadius;
        var globe = new MeshInstance3D { Name = "Globe", Mesh = new SphereMesh { Radius = radius, Height = radius * 2, RadialSegments = 128, Rings = 64 }, MaterialOverride = PlanetMaterial3D.Create(marker) };
        root.AddChild(globe);
        ((ShaderMaterial)globe.MaterialOverride).SetShaderParameter("sun_direction", root.Basis.Inverse() * -position.Normalized());
        if (marker.HasDetailedEnvironment && marker.SurfaceKey == "saturn") globe.Scale = new(1, .91f, 1);
        if (marker.HasIllustratedOcean && marker.SurfaceKey != "earth")
        {
            var clouds = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/planet_clouds.gdshader") };
            clouds.SetShaderParameter("seed", (float)marker.BodyId);
            if (marker.SurfaceKey == "earth" && ResourceLoader.Exists("res://assets/visual/sol/earth-clouds.jpg"))
            {
                clouds.SetShaderParameter("mapped", true);
                clouds.SetShaderParameter("cloud_map", GD.Load<Texture2D>("res://assets/visual/sol/earth-clouds.jpg"));
            }
            root.AddChild(new MeshInstance3D { Name = "WeatherLayer", CastShadow = GeometryInstance3D.ShadowCastingSetting.Off, Mesh = new SphereMesh {
                Radius = radius * 1.006f, Height = radius * 2.012f, RadialSegments = 128, Rings = 64 }, MaterialOverride = clouds });
        }
        MeshInstance3D? atmosphere = null;
        if (PlanetMaterial3D.HasAtmosphere(marker))
        {
            _atmosphereShader ??= GD.Load<Shader>("res://assets/visual/shaders/atmosphere_shell.gdshader");
            var haze = new ShaderMaterial { Shader = _atmosphereShader };
            haze.SetShaderParameter("atmosphere_color", PlanetMaterial3D.AtmosphereColor(marker));
            haze.SetShaderParameter("density", marker.VisualClass is SystemSpatialBodyVisualClass.GasGiant or SystemSpatialBodyVisualClass.IceGiant ? .22f : .14f);
            atmosphere = new MeshInstance3D { Name = "Atmosphere", CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
                Mesh = new SphereMesh { Radius = radius * 1.025f, Height = radius * 2.05f, RadialSegments = 128, Rings = 64 }, MaterialOverride = haze };
            root.AddChild(atmosphere);
        }
        if (marker.HasDetailedEnvironment && marker.SurfaceKey == "saturn") AddRings(root, radius);
        _bodies[marker.BodyId] = new BodyVisual(marker, root, globe, radius, atmosphere);
    }

    private void BuildInfrastructure(SystemSpatialInfrastructureMarker marker)
    {
        var node = OrbitalStructureGeometry.Create(marker, _visualStyle);
        node.Name = $"Infrastructure_{marker.ProjectId}";
        var host = marker.HostBodyId is int hostId && _bodies.TryGetValue(hostId, out var hostBody)
            ? hostBody.Root.Position : InfrastructureFallback(marker);
        // Separate sites around their host instead of stacking all facilities at one
        // coordinate. Stable ordering keeps the same slot after completion/save/load.
        var sites = (_snapshot!.Infrastructure ?? Array.Empty<SystemSpatialInfrastructureMarker>())
            .OrderBy(site => site.ProjectId, StringComparer.Ordinal).Select(site => site.ProjectId).ToArray();
        var slot = Math.Max(0, Array.IndexOf(sites, marker.ProjectId));
        var angle = -.65f + MathF.Tau * slot / Math.Max(3, sites.Length);
        var hostRadius = marker.HostBodyId is int offsetHostId && _bodies.TryGetValue(offsetHostId, out var hostVisual)
            ? hostVisual.Radius : 14;
        var orbit = hostRadius + 26 + slot * 3;
        var offset = new Vector3(MathF.Cos(angle) * orbit, 5 + slot * 2, MathF.Sin(angle) * orbit);
        node.Position = host + offset;
        node.RotationDegrees = new(0, (marker.ProjectId.GetHashCode(StringComparison.Ordinal) & 255) - 128, 0);
        node.Scale = Vector3.One * .48f;
        _world.AddChild(node); _infrastructure[marker.ProjectId] = node;
    }

    private Vector3 InfrastructureFallback(SystemSpatialInfrastructureMarker marker)
    {
        if (marker.ProjectId == "asteroid_resource_network") return new Vector3(_snapshot!.DesignRadius * .52f, 3, -_snapshot.DesignRadius * .22f);
        return _bodies.Values.Where(body => body.Marker.Kind != PlanetaryBodyKind.Moon).OrderBy(body => body.Marker.OrbitIndex)
            .Select(body => body.Root.Position).FirstOrDefault(new Vector3(18, 0, 0));
    }

    private static Vector3 PositionFor(SystemSpatialBodyMarker marker)
    {
        if (marker.OrbitalInclinationDegrees != 0)
            return new Vector3(marker.OffsetX, marker.OffsetHeight, marker.OffsetY);
        var inclination = ((marker.BodyId * 1103515245 + 12345) & 1023) / 1023f * .18f - .09f;
        return new Vector3(marker.OffsetX, marker.OrbitRadius * inclination, marker.OffsetY);
    }

    private static void AddRings(Node3D parent, float radius)
    {
        const int segments = 192;
        const float innerScale = 1.16f;
        const float outerScale = 2.72f;
        var surface = new SurfaceTool();
        surface.Begin(Mesh.PrimitiveType.Triangles);
        void Vertex(float scale, float angle, float radial)
        {
            surface.SetNormal(Vector3.Up);
            surface.SetUV(new Vector2(radial, angle / MathF.Tau));
            surface.AddVertex(new Vector3(MathF.Cos(angle) * radius * scale, 0, MathF.Sin(angle) * radius * scale));
        }
        for (var segment = 0; segment < segments; segment++)
        {
            var first = MathF.Tau * segment / segments;
            var second = MathF.Tau * (segment + 1) / segments;
            Vertex(innerScale, first, 0); Vertex(outerScale, first, 1); Vertex(outerScale, second, 1);
            Vertex(innerScale, first, 0); Vertex(outerScale, second, 1); Vertex(innerScale, second, 0);
        }
        var material = new ShaderMaterial { Shader = GD.Load<Shader>("res://assets/visual/shaders/saturn_rings.gdshader") };
        var ring = new MeshInstance3D { Name = "SaturnRings", Mesh = surface.Commit(), MaterialOverride = material,
            CastShadow = GeometryInstance3D.ShadowCastingSetting.On };
        // The parent globe carries Saturn's axial tilt, so the rings stay in its equator.
        parent.AddChild(ring);
    }

    private void BuildLocalSky(int seed)
    {
        var sky = new Node3D { Name = "LocalStars" }; _world.AddChild(sky); _localStars = sky;
        var random = new Random(seed ^ 0x53544152);
        var starMaterial = new StandardMaterial3D { AlbedoColor = new Color("c8d5df"), EmissionEnabled = true, Emission = new Color("a9c9df"), EmissionEnergyMultiplier = 1.6f, ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded };
        var multi = new MultiMesh { TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = new SphereMesh { Radius = .16f, Height = .32f, RadialSegments = 8, Rings = 4 }, InstanceCount = 560 };
        for (var index = 0; index < multi.InstanceCount; index++)
        {
            var direction = new Vector3((float)random.NextDouble() * 2 - 1, (float)random.NextDouble() * 2 - 1, (float)random.NextDouble() * 2 - 1).Normalized();
            multi.SetInstanceTransform(index, new Transform3D(Basis.Identity, direction * (FitDistance * 3.6f)));
        }
        sky.AddChild(new MultiMeshInstance3D { Multimesh = multi, MaterialOverride = starMaterial });
        var hazeMaterial = new StandardMaterial3D { Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded, AlbedoColor = new Color("617486", .018f) };
        for (var index = 0; index < 4; index++)
        {
            var direction = new Vector3((float)random.NextDouble() * 2 - 1, (float)random.NextDouble() * .38f - .19f, (float)random.NextDouble() * 2 - 1).Normalized();
            sky.AddChild(new MeshInstance3D { Mesh = new SphereMesh { Radius = FitDistance * (.32f + index * .05f), Height = FitDistance * (.64f + index * .10f), RadialSegments = 24, Rings = 12 }, MaterialOverride = hazeMaterial, Position = direction * (FitDistance * 2.8f) });
        }
    }

    private void ApplyEntry()
    {
        if (_entry == Vector2.Zero || Size.X < 2 || Size.Y < 2) return;
        var horizontal = Math.Clamp((_entry.X / Size.X - .5f) * .48f, -.24f, .24f);
        _yaw = _targetYaw - horizontal; _distance = FitDistance * 1.22f;
        _entry = Vector2.Zero;
    }

    private void UpdateCamera()
    {
        if (_camera is null) return;
        var horizontal = MathF.Cos(_pitch) * _distance;
        _camera.Position = new Vector3(MathF.Sin(_yaw) * horizontal, MathF.Sin(_pitch) * _distance, MathF.Cos(_yaw) * horizontal);
        _cameraRig.Position = _target;
        _camera.LookAt(_target, Vector3.Up);
    }

    private bool CameraHasConverged()
    {
        // At interplanetary fit distances a float cannot reliably close an absolute 0.0001
        // gap. Scale the tolerance above that floor, then assign the exact target in Advance.
        var linearTolerance = MathF.Max(.0001f, Math.Max(_targetDistance, _targetTarget.Length()) * .000002f);
        return _target.DistanceTo(_targetTarget) <= linearTolerance &&
            MathF.Abs(_distance - _targetDistance) <= linearTolerance &&
            MathF.Abs(_yaw - _targetYaw) <= .00001f && MathF.Abs(_pitch - _targetPitch) <= .00001f;
    }

    private Vector2 NativeScale => Size.X > 1 && Size.Y > 1
        ? new Vector2(_viewport.Size.X / Size.X, _viewport.Size.Y / Size.Y) : Vector2.One;

    private bool RayHitsSphere(Vector2 logicalScreen, Vector3 center, float radius, out float distance)
    {
        var native = logicalScreen * NativeScale;
        var origin = _camera.ProjectRayOrigin(native);
        var direction = _camera.ProjectRayNormal(native);
        var offset = origin - center;
        var halfB = offset.Dot(direction);
        var discriminant = halfB * halfB - (offset.LengthSquared() - radius * radius);
        if (discriminant < 0) { distance = 0; return false; }
        distance = -halfB - MathF.Sqrt(discriminant);
        if (distance < 0) distance = -halfB + MathF.Sqrt(discriminant);
        return distance >= 0;
    }
}
