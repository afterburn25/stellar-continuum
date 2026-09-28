using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Simulation.Combat.Massive;

namespace Game.Presentation.Spatial;

/// <summary>
/// Bounded 3D tactical presentation inside the observer-safe system scene. Battle-space
/// coordinates remain authoritative kilometres; the constant below is presentation-only.
/// </summary>
public partial class SystemScene3D
{
    private const float CombatKilometresToScene = .07f;
    private const float CombatPlaneAltitude = PrimaryStarRadius + 38f;
    private const int MaximumCombatRepresentatives = 4096;
    private const int MaximumDetailedCombatVessels = 32;
    private const int MaximumCombatEffects = 192;
    private readonly Dictionary<string, Node3D> _combatDetailedVessels = new(StringComparer.Ordinal);
    private readonly Dictionary<long, Vector2> _combatDisplayedPositions = new();
    private readonly List<CombatVisualEvent3D> _combatEvents = new();
    private readonly HashSet<long> _combatSelectedFormationIds = new();
    private MultiMeshInstance3D? _combatRepresentatives;
    private MultiMeshInstance3D? _combatExplosions;
    private MeshInstance3D? _combatLines;
    private ImmediateMesh? _combatLineMesh;
    private StandardMaterial3D? _combatLineMaterial;
    private StandardMaterial3D? _combatFriendlyOverlay, _combatHostileOverlay, _combatUnknownOverlay;
    private MassiveCombatSnapshot? _combatSnapshot;
    private int _combatObserverId = -1;
    private bool _combatActive;
    private bool _combatAnimationRunning = true;
    private float _combatRepresentativeAccumulator;
    private Vector3 _combatAnchor = new(64, CombatPlaneAltitude, -42);
    private long _combatLatestEventSequence;

    public int CombatDetailedVesselCount => _combatDetailedVessels.Count;
    public int CombatRepresentativeCount => _combatRepresentatives?.Multimesh?.VisibleInstanceCount ?? 0;
    public int CombatEffectCount => _combatEvents.Count;
    public int CombatDetailedMeshCount => _combatDetailedVessels.Values.Sum(node =>
        node.FindChildren("*", "MeshInstance3D", true, false).Count);
    public int CombatWeaponMountCount => _combatDetailedVessels.Values.Sum(node =>
        node.FindChildren("Weapon*", "MeshInstance3D", true, false).Count);
    public bool HasVisibleDetailedCombatVessel(Rect2 logicalBounds) => _combatDetailedVessels.Values
        .Where(node => node.Visible).Select(node => ProjectPoint(node.GlobalPosition))
        .Any(point => point.HasValue && logicalBounds.HasPoint(point.Value));
    public void SetCombatAnimationRunning(bool running) => _combatAnimationRunning = running;

    public void PresentCombat(MassiveCombatSnapshot? snapshot, int observerCivilizationId,
        IReadOnlyCollection<long> selectedFormationIds)
    {
        _combatSnapshot = snapshot;
        _combatObserverId = observerCivilizationId;
        _combatSelectedFormationIds.Clear();
        _combatSelectedFormationIds.UnionWith(selectedFormationIds);
        _combatActive = snapshot is not null;
        if (snapshot is null)
        {
            ClearCombatPresentation();
            return;
        }

        if (_snapshot is not null)
            _combatAnchor = new Vector3(_snapshot.DesignRadius * .36f, CombatPlaneAltitude, -_snapshot.DesignRadius * .23f);

        EnsureCombatResources();
        var incoming = snapshot.Formations.ToDictionary(x => x.FormationId, x => x.Position);
        foreach (var formation in snapshot.Formations)
        {
            if (!_combatDisplayedPositions.TryGetValue(formation.FormationId, out var displayed))
                displayed = GodotPoint(formation.Position);
            // Pausing freezes the rendered battle as well as authoritative ticks. Camera motion
            // remains independent so the player can inspect that frozen state.
            _combatDisplayedPositions[formation.FormationId] = _combatAnimationRunning
                ? displayed.Lerp(GodotPoint(formation.Position), .58f) : displayed;
        }
        foreach (var stale in _combatDisplayedPositions.Keys.Where(id => !incoming.ContainsKey(id)).ToArray())
            _combatDisplayedPositions.Remove(stale);

        IngestCombatEvents(snapshot.Events);
        PopulateCombatRepresentatives(snapshot.Formations, selectedFormationIds);
        PopulateDetailedCombatVessels(snapshot.Formations, selectedFormationIds);
        UpdateCombatEffects();
        SetVesselLighting(true);
    }

    public void FitCombat()
    {
        if (_combatSnapshot?.Formations.Count is not > 0) return;
        var positions = _combatSnapshot.Formations.Select(x => CombatWorld(GodotPoint(x.Position))).ToArray();
        var minX = positions.Min(x => x.X); var maxX = positions.Max(x => x.X);
        var minZ = positions.Min(x => x.Z); var maxZ = positions.Max(x => x.Z);
        // Aim between the elevated traffic plane and orbital plane so small encounters
        // retain a real planet/star backdrop instead of becoming an empty black inspector.
        _targetTarget = new Vector3((minX + maxX) * .5f, CombatPlaneAltitude - 20f, (minZ + maxZ) * .5f);
        var extent = MathF.Max(42, MathF.Max(maxX - minX, maxZ - minZ));
        _targetDistance = Math.Clamp(extent * 1.42f, 100, MathF.Max(180, FitDistance * .82f));
        _targetYaw = -.70f;
        _targetPitch = .72f;
    }

    public void FocusCombatPosition(Vector2 kilometres)
    {
        if (!_combatActive) return;
        _targetTarget = CombatWorld(kilometres);
        _targetDistance = 24;
    }

    public Vector2? ProjectCombatPosition(Vector2 kilometres) => ProjectPoint(CombatWorld(kilometres));

    public float? CombatScalePixels(float kilometres)
    {
        if (_combatDisplayedPositions.Count == 0 || kilometres <= 0) return null;
        var center = _combatDisplayedPositions.Values.Aggregate(Vector2.Zero, (sum, value) => sum + value) /
            _combatDisplayedPositions.Count;
        var first = ProjectPoint(CombatWorld(center));
        var second = ProjectPoint(CombatWorld(center + Vector2.Right * kilometres));
        return first.HasValue && second.HasValue ? first.Value.DistanceTo(second.Value) : null;
    }

    public Vector2? UnprojectCombatPosition(Vector2 logicalScreen)
    {
        if (_camera is null) return null;
        var native = logicalScreen * NativeScale;
        var origin = _camera.ProjectRayOrigin(native);
        var direction = _camera.ProjectRayNormal(native);
        if (MathF.Abs(direction.Y) < .0001f) return null;
        var distance = (CombatPlaneAltitude - origin.Y) / direction.Y;
        if (distance <= 0) return null;
        var point = origin + direction * distance;
        return new Vector2((point.X - _combatAnchor.X) / CombatKilometresToScene,
            (point.Z - _combatAnchor.Z) / CombatKilometresToScene);
    }

    private void AdvanceCombatPresentation(double delta)
    {
        if (!_combatActive || _combatSnapshot is null) return;
        var seconds = _combatAnimationRunning ? (float)Math.Clamp(delta, 0, .12) : 0f;
        foreach (var formation in _combatSnapshot.Formations)
        {
            if (!_combatDisplayedPositions.TryGetValue(formation.FormationId, out var displayed)) continue;
            // Extrapolation is capped to one presentation refresh, then corrected by snapshots.
            var predicted = GodotPoint(formation.Position) + GodotPoint(formation.Velocity) * Math.Min(seconds, .1f);
            _combatDisplayedPositions[formation.FormationId] = displayed.Lerp(predicted, 1f - MathF.Exp(-10f * seconds));
        }
        for (var i = _combatEvents.Count - 1; i >= 0; i--)
        {
            _combatEvents[i] = _combatEvents[i] with { Age = _combatEvents[i].Age + seconds };
            if (_combatEvents[i].Age >= _combatEvents[i].Lifetime) _combatEvents.RemoveAt(i);
        }
        _combatRepresentativeAccumulator += seconds;
        if (_combatRepresentativeAccumulator >= 1f / 30f)
        {
            _combatRepresentativeAccumulator = 0;
            PopulateCombatRepresentatives(_combatSnapshot.Formations, _combatSelectedFormationIds);
        }
        UpdateDetailedTransforms(_combatSnapshot.Formations);
        UpdateCombatEffects();
    }

    private void EnsureCombatResources()
    {
        if (_combatRepresentatives is not null) return;
        var hull = BuildCombatRepresentativeMesh();
        var material = new StandardMaterial3D { AlbedoColor = Colors.White, Metallic = .72f, Roughness = .28f,
            VertexColorUseAsAlbedo = true, ShadingMode = BaseMaterial3D.ShadingModeEnum.PerPixel };
        hull.SurfaceSetMaterial(0, material);
        var instances = new MultiMesh { TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            UseColors = true, Mesh = hull, InstanceCount = MaximumCombatRepresentatives,
            VisibleInstanceCount = 0 };
        _combatRepresentatives = new MultiMeshInstance3D { Name = "CombatRepresentativePool", Multimesh = instances,
            Layers = 2, CastShadow = GeometryInstance3D.ShadowCastingSetting.Off };
        _world.AddChild(_combatRepresentatives);

        var explosionMesh = new SphereMesh { Radius = .7f, Height = 1.4f, RadialSegments = 8, Rings = 4,
            Material = new StandardMaterial3D { AlbedoColor = Colors.White, EmissionEnabled = true,
                Emission = new Color("ff6b19"), EmissionEnergyMultiplier = 4, VertexColorUseAsAlbedo = true,
                Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
                ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded } };
        _combatExplosions = new MultiMeshInstance3D { Name = "CombatExplosionPool", Layers = 2,
            CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
            Multimesh = new MultiMesh { TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
                UseColors = true, Mesh = explosionMesh, InstanceCount = MaximumCombatEffects, VisibleInstanceCount = 0 } };
        _world.AddChild(_combatExplosions);
        _combatLineMesh = new ImmediateMesh();
        _combatLineMaterial = new StandardMaterial3D { VertexColorUseAsAlbedo = true,
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded };
        _combatLines = new MeshInstance3D { Name = "CombatVolleyLines", Layers = 2, Mesh = _combatLineMesh,
            CastShadow = GeometryInstance3D.ShadowCastingSetting.Off };
        _world.AddChild(_combatLines);
    }

    private void PopulateCombatRepresentatives(IReadOnlyList<MassiveObservedFormation> formations,
        IReadOnlyCollection<long> selected)
    {
        if (_combatRepresentatives?.Multimesh is not { } pool) return;
        var lodCap = _targetDistance > 110 ? 1 : _targetDistance > 75 ? 8 : 20;
        var desired = formations.Select(f => Math.Max(1, Math.Min(lodCap,
            (int)MathF.Ceiling(MathF.Sqrt(Math.Max(1, (f.ShipCountLow + f.ShipCountHigh) * .5f)) / 7f)))).ToArray();
        var total = desired.Sum();
        var divisor = total <= MaximumCombatRepresentatives ? 1f : total / (float)MaximumCombatRepresentatives;
        var instance = 0;
        for (var f = 0; f < formations.Count && instance < MaximumCombatRepresentatives; f++)
        {
            var formation = formations[f];
            var count = Math.Max(1, Math.Min(desired[f], (int)MathF.Floor(desired[f] / divisor)));
            var center = _combatDisplayedPositions.GetValueOrDefault(formation.FormationId, GodotPoint(formation.Position));
            var velocity = GodotPoint(formation.Velocity);
            var heading = new Vector2(MathF.Cos(formation.HeadingRadians), MathF.Sin(formation.HeadingRadians));
            var yaw = MathF.Atan2(-heading.X, -heading.Y);
            var color = CombatFormationColor(formation, selected.Contains(formation.FormationId));
            var selectedDetail = selected.Contains(formation.FormationId);
            if (selectedDetail && _targetDistance < 80) continue;
            for (var token = 0; token < count && instance < MaximumCombatRepresentatives; token++, instance++)
            {
                var hasDetailedCenter = selectedDetail || formation.ImportantVessels.Count > 0;
                var visualToken = hasDetailedCenter ? token + 1 : token;
                var offset = CombatFormationOffset(formation.FormationId, visualToken, count + (hasDetailedCenter ? 1 : 0), formation.Shape).Rotated(-yaw);
                var at = CombatWorld(center + offset);
                var scale = token == 0 ? .29f : .22f;
                pool.SetInstanceTransform(instance, new Transform3D(new Basis(Vector3.Up, yaw).Scaled(Vector3.One * scale), at));
                pool.SetInstanceColor(instance, color);
            }
        }
        pool.VisibleInstanceCount = instance;
    }

    private void PopulateDetailedCombatVessels(IReadOnlyList<MassiveObservedFormation> formations,
        IReadOnlyCollection<long> selected)
    {
        var wanted = new List<(string Key, string Design, MassiveObservedFormation Formation, int Slot, bool Critical, float Scale)>();
        foreach (var formation in formations.OrderByDescending(x => selected.Contains(x.FormationId)).ThenBy(x => x.FormationId))
        {
            var formationSlot = 0;
            foreach (var vessel in formation.ImportantVessels)
            {
                if (wanted.Count >= MaximumDetailedCombatVessels) break;
                var majorScale = vessel.IsCarrier ? 1.38f : vessel.IsFlagship ? 1.20f : vessel.IsInterdictor ? 1.10f : 1f;
                wanted.Add(($"v:{vessel.VesselId}", vessel.DesignId, formation, formationSlot++, vessel.IsCriticallyDamaged, majorScale));
            }
            if (selected.Contains(formation.FormationId) && wanted.Count < MaximumDetailedCombatVessels &&
                formation.Cohorts.FirstOrDefault(x => x.Identified) is { } cohort)
            {
                var representatives = Math.Min(6, Math.Max(1, (cohort.CountLow + cohort.CountHigh) / 2));
                for (var representative = 0; representative < representatives && wanted.Count < MaximumDetailedCombatVessels; representative++)
                    wanted.Add(($"f:{formation.FormationId}:{representative}", cohort.DisplayClass, formation, formationSlot++, false, 1f));
            }
            if (wanted.Count >= MaximumDetailedCombatVessels) break;
        }
        var wantedKeys = wanted.Select(x => x.Key).ToHashSet(StringComparer.Ordinal);
        foreach (var stale in _combatDetailedVessels.Keys.Where(x => !wantedKeys.Contains(x)).ToArray())
        {
            ReleaseWorldNode(_combatDetailedVessels[stale]);
            _combatDetailedVessels.Remove(stale);
        }
        foreach (var item in wanted)
        {
            if (!_combatDetailedVessels.TryGetValue(item.Key, out var node))
            {
                node = ShipGeometry.Create(item.Design, null, highDetail: true);
                node.Name = "CombatDetailed_" + item.Key.Replace(':', '_');
                SetCombatLayers(node);
                _world.AddChild(node);
                _combatDetailedVessels.Add(item.Key, node);
            }
            var allegiance = item.Formation.CivilizationId == _combatObserverId ? "friendly" :
                item.Formation.IsExact ? "hostile" : "unknown";
            if (node.GetMeta("CombatAllegiance", "").AsString() != allegiance)
            {
                ApplyCombatAllegiance(node, CombatFormationColor(item.Formation, false));
                node.SetMeta("CombatAllegiance", allegiance);
            }
            node.SetMeta("FormationId", item.Formation.FormationId);
            node.SetMeta("CombatSlot", item.Slot);
            node.SetMeta("Critical", item.Critical);
            node.SetMeta("CombatScale", item.Scale);
        }
        UpdateDetailedTransforms(formations);
    }

    private void UpdateDetailedTransforms(IReadOnlyList<MassiveObservedFormation> formations)
    {
        var map = formations.ToDictionary(x => x.FormationId);
        foreach (var node in _combatDetailedVessels.Values)
        {
            var formationId = (long)node.GetMeta("FormationId").AsInt64();
            if (!map.TryGetValue(formationId, out var formation)) continue;
            var slot = node.GetMeta("CombatSlot").AsInt32();
            var center = _combatDisplayedPositions.GetValueOrDefault(formationId, GodotPoint(formation.Position));
            var offset = CombatFormationOffset(formationId, slot + 1, Math.Max(2, formation.ImportantVessels.Count + 1), formation.Shape) * 4.8f;
            var heading = new Vector2(MathF.Cos(formation.HeadingRadians), MathF.Sin(formation.HeadingRadians));
            var yaw = MathF.Atan2(-heading.X, -heading.Y);
            node.Position = CombatWorld(center + offset.Rotated(-yaw)) + Vector3.Up * (1.3f + slot % 3 * .55f);
            node.Rotation = new Vector3(0, yaw, 0);
            node.Scale = Vector3.One * (node.GetMeta("Critical").AsBool() ? .38f : .44f) *
                node.GetMeta("CombatScale", 1f).AsSingle();
            node.Visible = _combatSelectedFormationIds.Contains(formationId) ||
                node.GetMeta("Critical").AsBool() && _targetDistance < 110;
        }
    }

    private void IngestCombatEvents(IReadOnlyList<MassiveObservedCombatEvent> events)
    {
        foreach (var value in events.Where(x => x.Sequence > _combatLatestEventSequence).OrderBy(x => x.Sequence))
        {
            _combatLatestEventSequence = Math.Max(_combatLatestEventSequence, value.Sequence);
            if (value.Type == MassiveCombatEventType.MissileSalvo) continue;
            var impactOnly = value.ImpactPosition is { } && value.Type is MassiveCombatEventType.Damage or
                MassiveCombatEventType.MissileIntercepted or MassiveCombatEventType.FormationDestroyed;
            if (!value.DetailsKnown && !impactOnly) continue;
            if (value.Position is null && value.ImpactPosition is null) continue;
            var reported = value.Position ?? value.ImpactPosition!.Value;
            var target = value.ImpactPosition is { } impact ? GodotPoint(impact.Vector) :
                value.TargetFormationId is long targetId && _combatDisplayedPositions.TryGetValue(targetId, out var targetAt)
                    ? targetAt : GodotPoint(reported.Vector);
            var source = value.DetailsKnown && value.ActorFormationId is long sourceId &&
                _combatDisplayedPositions.TryGetValue(sourceId, out var sourceAt) ? sourceAt : target;
            var lifetime = value.Type switch { MassiveCombatEventType.BeamVolley => .42f,
                MassiveCombatEventType.KineticVolley => .72f, MassiveCombatEventType.MissileSalvo => 2.2f,
                MassiveCombatEventType.MissileIntercepted => .85f, MassiveCombatEventType.Damage => .72f,
                MassiveCombatEventType.FormationDestroyed => 1.8f, _ => .9f };
            _combatEvents.Add(new(value.Type, value.ActorFormationId, source, target, 0, lifetime, value.Magnitude ?? 1));
        }
        if (_combatEvents.Count > MaximumCombatEffects)
            _combatEvents.RemoveRange(0, _combatEvents.Count - MaximumCombatEffects);
    }

    private void UpdateCombatEffects()
    {
        if (_combatLines is null || _combatExplosions?.Multimesh is not { } explosions) return;
        if (_combatLineMesh is null || _combatLineMaterial is null) return;
        var lines = _combatLineMesh;
        lines.ClearSurfaces();
        var hasLines = false;
        void AddLine(Vector3 first, Vector3 second, Color color)
        {
            if (!hasLines)
            {
                lines.SurfaceBegin(Mesh.PrimitiveType.Lines, _combatLineMaterial);
                hasLines = true;
            }
            lines.SurfaceSetColor(color); lines.SurfaceAddVertex(first);
            lines.SurfaceSetColor(color); lines.SurfaceAddVertex(second);
        }
        var explosionCount = 0;
        void AddExplosion(Vector3 at, Vector3 scale, Color color)
        {
            if (explosionCount >= MaximumCombatEffects) return;
            explosions.SetInstanceTransform(explosionCount, new Transform3D(Basis.Identity.Scaled(scale), at));
            explosions.SetInstanceColor(explosionCount, color);
            explosionCount++;
        }
        foreach (var effect in _combatEvents.TakeLast(48))
        {
            var progress = Math.Clamp(effect.Age / effect.Lifetime, 0, 1);
            var alpha = 1f - progress;
            var start = DetailedWeaponOrigin(effect.ActorFormationId) ?? CombatWorld(effect.Start) + Vector3.Up * 1.4f;
            var end = CombatWorld(effect.End) + Vector3.Up * 1.4f;
            if (effect.Type is MassiveCombatEventType.BeamVolley or MassiveCombatEventType.KineticVolley or MassiveCombatEventType.MissileSalvo)
            {
                var color = effect.Type switch { MassiveCombatEventType.BeamVolley => new Color(.28f, .9f, 1, alpha),
                    MassiveCombatEventType.KineticVolley => new Color(1, .72f, .24f, alpha),
                    _ => new Color(1, .3f, .08f, alpha) };
                var tip = effect.Type == MassiveCombatEventType.MissileSalvo ? start.Lerp(end, Mathf.SmoothStep(0, 1, progress)) : end;
                AddLine(start, tip, color);
            }
            if (effect.Type is MassiveCombatEventType.Damage or MassiveCombatEventType.MissileIntercepted)
            {
                var hotCore = .34f + progress * .48f;
                AddExplosion(end, Vector3.One * hotCore, new Color(1, .88f, .42f, alpha * .92f));
                AddExplosion(end, Vector3.One * (hotCore * 2.1f), new Color(1, .22f, .025f, alpha * .24f));
                for (var fragment = 0; fragment < 5; fragment++)
                {
                    var angle = fragment * 2.094f + effect.Magnitude * .17f;
                    var direction = new Vector3(MathF.Cos(angle), -.16f + fragment * .09f, MathF.Sin(angle)).Normalized();
                    var tip = end + direction * (.65f + progress * (2.4f + fragment * .22f));
                    AddLine(end.Lerp(tip, .42f), tip, new Color(1, .28f, .035f, alpha * .62f));
                    AddExplosion(tip, new Vector3(.09f, .09f, .30f), new Color(1, .45f, .08f, alpha * .72f));
                }
            }
            else if (effect.Type == MassiveCombatEventType.FormationDestroyed)
            {
                AddExplosion(end, Vector3.One * (1.2f + progress * 5.8f), new Color(1, .8f * alpha, .22f, alpha));
                AddExplosion(end, Vector3.One * (.7f + progress * 3.3f), new Color(1, .16f, .025f, alpha * .82f));
                for (var fragment = 0; fragment < 9; fragment++)
                {
                    var angle = fragment * 2.399963f + effect.Magnitude * .071f;
                    var direction = new Vector3(MathF.Cos(angle), -.45f + (fragment % 4) * .3f, MathF.Sin(angle)).Normalized();
                    var tip = end + direction * (2f + progress * (8f + fragment % 3 * 2f));
                    AddLine(end.Lerp(tip, .55f), tip, new Color(1, .32f, .06f, alpha * .8f));
                    AddExplosion(tip, new Vector3(.25f, .25f, 1.1f), new Color(1, .48f, .08f, alpha));
                }
            }
        }
        if (_combatSnapshot is not null)
        {
            foreach (var salvo in _combatSnapshot.ActiveMissileSalvos)
            {
                if (salvo.CurrentPosition is not { } current)
                {
                    // A detected incoming salvo may disclose its threatened location without
                    // disclosing a hidden launcher or inventing an in-flight trajectory.
                    if (!salvo.IncomingToOwn || salvo.TargetPosition is not { } threatened) continue;
                    var impact = CombatWorld(GodotPoint(threatened.Vector)) + Vector3.Up * 1.7f;
                    var warning = new Color(1, .23f, .06f, .85f);
                    AddLine(impact + Vector3.Left * 1.5f, impact + Vector3.Right * 1.5f, warning);
                    AddLine(impact + Vector3.Forward * 1.5f, impact + Vector3.Back * 1.5f, warning);
                    continue;
                }
                var currentWorld = CombatWorld(GodotPoint(current.Vector)) + Vector3.Up * 1.6f;
                Vector3 trailStart;
                if (salvo.SourcePosition is { } source)
                    trailStart = CombatWorld(GodotPoint(source.Vector)) + Vector3.Up * 1.6f;
                else if (salvo.TargetPosition is { } target)
                {
                    var targetWorld = CombatWorld(GodotPoint(target.Vector)) + Vector3.Up * 1.6f;
                    trailStart = currentWorld + (currentWorld - targetWorld).Normalized() * 4f;
                }
                else continue;
                var color = salvo.IncomingToOwn ? new Color(1, .2f, .06f, .95f) : new Color(1, .62f, .16f, .9f);
                AddLine(trailStart.Lerp(currentWorld, .72f), currentWorld, color);
            }
        }
        if (hasLines) lines.SurfaceEnd();
        _combatLines.Mesh = lines;
        explosions.VisibleInstanceCount = explosionCount;
    }

    private void ClearCombatPresentation()
    {
        _combatActive = false; _combatSnapshot = null; _combatObserverId = -1; _combatLatestEventSequence = 0;
        _combatEvents.Clear(); _combatDisplayedPositions.Clear(); _combatSelectedFormationIds.Clear();
        _combatRepresentativeAccumulator = 0;
        foreach (var node in _combatDetailedVessels.Values) ReleaseWorldNode(node);
        _combatDetailedVessels.Clear();
        ReleaseWorldNode(_combatRepresentatives); _combatRepresentatives = null;
        ReleaseWorldNode(_combatExplosions); _combatExplosions = null;
        ReleaseWorldNode(_combatLines); _combatLines = null;
        _combatLineMesh = null; _combatLineMaterial = null;
        _combatFriendlyOverlay = null; _combatHostileOverlay = null; _combatUnknownOverlay = null;
        SetVesselLighting(false);
    }

    private Vector3? DetailedWeaponOrigin(long? formationId)
    {
        if (formationId is null) return null;
        var vessel = _combatDetailedVessels.Values.FirstOrDefault(node =>
            (long)node.GetMeta("FormationId", -1).AsInt64() == formationId.Value && node.Visible);
        if (vessel is null) return null;
        MeshInstance3D? barrel = null;
        var cachedPath = vessel.GetMeta("CombatWeaponBarrel", new NodePath("")).AsNodePath();
        if (!cachedPath.IsEmpty) barrel = vessel.GetNodeOrNull<MeshInstance3D>(cachedPath);
        if (barrel is null)
        {
            barrel = vessel.FindChildren("WeaponBarrel", "MeshInstance3D", true, false)
                .OfType<MeshInstance3D>().FirstOrDefault();
            if (barrel is not null) vessel.SetMeta("CombatWeaponBarrel", vessel.GetPathTo(barrel));
        }
        return barrel?.GlobalPosition ?? vessel.GlobalPosition;
    }

    private Vector3 CombatWorld(Vector2 kilometres) => _combatAnchor +
        new Vector3(kilometres.X * CombatKilometresToScene, 0, kilometres.Y * CombatKilometresToScene);

    private static Vector2 GodotPoint(System.Numerics.Vector2 value) => new(value.X, value.Y);

    private Color CombatFormationColor(MassiveObservedFormation formation, bool selected) => selected
        ? new Color("8fffd0") : formation.CivilizationId == _combatObserverId
            ? new Color("23966f") : formation.IsExact ? new Color("d85b55") : new Color("756583");

    private static Vector2 CombatFormationOffset(long id, int token, int count, MassiveFormationShape shape)
    {
        if (token == 0) return Vector2.Zero;
        var row = 1 + (token - 1) / 7; var column = (token - 1) % 7 - 3;
        var jitter = CombatHash01(id, token) - .5f;
        return shape switch
        {
            MassiveFormationShape.Wedge => new Vector2(-row * 10, column * (6 + row * 1.2f) + jitter * 3),
            MassiveFormationShape.Screen => new Vector2(column * 10 + jitter * 3, row * 5),
            MassiveFormationShape.Standoff => new Vector2(column * 11, row * 9 + jitter * 3),
            MassiveFormationShape.Dispersed => new Vector2(column * 13 + jitter * 8, row * 11 + CombatHash01(id, token + 91) * 8),
            MassiveFormationShape.Escort => new Vector2(MathF.Cos(token * 2.399f) * row * 10, MathF.Sin(token * 2.399f) * row * 7),
            MassiveFormationShape.RetreatColumn or MassiveFormationShape.Breakout => new Vector2(-row * 13, column * 5 + jitter * 3),
            _ => new Vector2(column * 9, row * 7 + jitter * 2),
        };
    }

    private static float CombatHash01(long id, int token)
    {
        unchecked
        {
            var value = (ulong)id ^ ((ulong)(token + 1) * 0x9E3779B97F4A7C15UL);
            value ^= value >> 30; value *= 0xBF58476D1CE4E5B9UL; value ^= value >> 27;
            value *= 0x94D049BB133111EBUL; value ^= value >> 31;
            return (value & 0xffff) / 65535f;
        }
    }

    private static void SetCombatLayers(Node node)
    {
        foreach (var child in node.GetChildren()) SetCombatLayers(child);
        if (node is GeometryInstance3D geometry)
        {
            geometry.Layers = 2;
            geometry.CastShadow = GeometryInstance3D.ShadowCastingSetting.Off;
        }
    }

    private void ApplyCombatAllegiance(Node node, Color color)
    {
        var overlay = color.B > color.R && color.B > color.G
            ? _combatUnknownOverlay ??= CombatOverlay(new Color("75668f"))
            : color.R > color.G
                ? _combatHostileOverlay ??= CombatOverlay(new Color("ef4f43"))
                : _combatFriendlyOverlay ??= CombatOverlay(new Color("38d98b"));
        foreach (var child in node.FindChildren("*", "MeshInstance3D", true, false).OfType<MeshInstance3D>())
            child.MaterialOverlay = overlay;
    }

    private static StandardMaterial3D CombatOverlay(Color color) => new()
    {
        AlbedoColor = new Color(color, .13f),
        Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
        ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
    };

    private static ArrayMesh BuildCombatRepresentativeMesh()
    {
        var surface = new SurfaceTool();
        surface.Begin(Mesh.PrimitiveType.Triangles);
        static void Triangle(SurfaceTool target, Vector3 a, Vector3 b, Vector3 c)
        {
            target.AddVertex(a); target.AddVertex(b); target.AddVertex(c);
        }
        static void Box(SurfaceTool target, Vector3 min, Vector3 max)
        {
            var p = new[]
            {
                new Vector3(min.X, min.Y, min.Z), new Vector3(max.X, min.Y, min.Z),
                new Vector3(max.X, max.Y, min.Z), new Vector3(min.X, max.Y, min.Z),
                new Vector3(min.X, min.Y, max.Z), new Vector3(max.X, min.Y, max.Z),
                new Vector3(max.X, max.Y, max.Z), new Vector3(min.X, max.Y, max.Z),
            };
            var faces = new[] { 0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,1,5, 0,5,4,
                3,7,6, 3,6,2, 0,4,7, 0,7,3, 1,2,6, 1,6,5 };
            for (var i = 0; i < faces.Length; i += 3) Triangle(target, p[faces[i]], p[faces[i + 1]], p[faces[i + 2]]);
        }

        // A compact recognizable spacecraft silhouette for thousands of pooled instances:
        // tapered central hull plus two engine nacelles, all in a single cached mesh.
        var noseTop = new Vector3(0, .2f, -1.65f); var noseBottom = new Vector3(0, -.2f, -1.65f);
        var lt = new Vector3(-.46f, .2f, .82f); var rt = new Vector3(.46f, .2f, .82f);
        var lb = new Vector3(-.46f, -.2f, .82f); var rb = new Vector3(.46f, -.2f, .82f);
        Triangle(surface, noseTop, rt, lt); Triangle(surface, noseBottom, lb, rb);
        Triangle(surface, noseTop, noseBottom, rb); Triangle(surface, noseTop, rb, rt);
        Triangle(surface, noseTop, lt, lb); Triangle(surface, noseTop, lb, noseBottom);
        Triangle(surface, lt, rt, rb); Triangle(surface, lt, rb, lb);
        Box(surface, new Vector3(-.76f, -.16f, -.15f), new Vector3(-.5f, .16f, 1.08f));
        Box(surface, new Vector3(.5f, -.16f, -.15f), new Vector3(.76f, .16f, 1.08f));
        surface.GenerateNormals();
        return surface.Commit();
    }

    private sealed record CombatVisualEvent3D(MassiveCombatEventType Type, long? ActorFormationId, Vector2 Start, Vector2 End,
        float Age, float Lifetime, int Magnitude);
}
