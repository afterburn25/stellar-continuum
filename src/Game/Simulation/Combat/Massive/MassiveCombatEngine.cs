using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace Game.Simulation.Combat.Massive;

/// <summary>Fixed-tick authoritative group combat. Work scales with formations and bounded
/// weapon groups, while every cohort ship remains in exact loss and durability accounting.</summary>
public sealed class MassiveCombatEngine
{
    public const double TickSeconds = .1;
    public const float SpatialCellSize = 500f;
    private const float Epsilon = .0001f;
    private readonly IMassiveCombatHostilityView _hostility;

    public MassiveCombatEngine(IMassiveCombatHostilityView? hostility = null) =>
        _hostility = hostility ?? DistinctCivilizationsHostilityView.Instance;

    public bool HasActiveHostilities(MassiveCombatBattleState battle)
    {
        ArgumentNullException.ThrowIfNull(battle);
        var civilizations = battle.Formations.Where(x => x.Active).Select(x => x.CivilizationId).Distinct().OrderBy(x => x).ToArray();
        for (var first = 0; first < civilizations.Length; first++)
            for (var second = first + 1; second < civilizations.Length; second++)
                if (_hostility.AreHostile(civilizations[first], civilizations[second]) ||
                    _hostility.AreHostile(civilizations[second], civilizations[first])) return true;
        return false;
    }

    public MassiveCombatOrderResult IssueOrder(MassiveCombatBattleState battle, int civilizationId, MassiveCombatOrder order)
    {
        ArgumentNullException.ThrowIfNull(battle);
        ArgumentNullException.ThrowIfNull(order);
        if (!Enum.IsDefined(order.Type) || order.Shape is { } requestedShape && !Enum.IsDefined(requestedShape))
            return new(false, "The combat order contains an invalid tactical mode.");
        if (order.Objective is Vector2 requestedObjective && (!float.IsFinite(requestedObjective.X) || !float.IsFinite(requestedObjective.Y)))
            return new(false, "Combat objective must be finite.");
        var formation = battle.Formations.FirstOrDefault(x => x.Id == order.FormationId && x.Active);
        if (formation is null || formation.CivilizationId != civilizationId)
            return new(false, "No active owned formation has that identity.");
        if (order.TargetFormationId is long targetId)
        {
            var target = battle.Formations.FirstOrDefault(x => x.Id == targetId && x.Active);
            var protect = order.Type == MassiveCombatOrderType.ProtectCriticalAsset;
            if (target is null || protect != (target.CivilizationId == civilizationId))
                return new(false, protect ? "The protected asset must be an active friendly formation." : "The requested target is not an active hostile formation.");
            if (!protect && !_hostility.AreHostile(civilizationId, target.CivilizationId))
                return new(false, "The requested target is not an active hostile formation.");
        }
        if (RequiresTarget(order.Type) && order.TargetFormationId is null)
            return new(false, "That combat order requires a target formation.");

        formation.Order = order.Type;
        formation.TargetFormationId = order.TargetFormationId;
        if (order.Objective is Vector2 objective)
            formation.Objective = objective;
        if (order.Shape is { } shape) formation.Shape = shape;
        if (order.Type == MassiveCombatOrderType.ProtectCriticalAsset) formation.ProtectedFormationId = order.TargetFormationId;
        if (order.Type == MassiveCombatOrderType.Surrender)
        {
            var surrenderedShips = formation.SurvivingShipCount;
            formation.Surrendered = true;
            formation.Velocity = new MassivePoint(0, 0);
            Emit(battle, MassiveCombatEventType.Surrendered, formation, null, surrenderedShips, $"{formation.Name} surrendered.");
            return new(true, $"{formation.Name} acknowledged surrender.");
        }
        if (IsWithdrawal(order.Type) && formation.WarpSpoolProgress <= 0) Emit(battle, MassiveCombatEventType.WarpSpooling, formation, null, 0, "Warp preparation started.");
        Emit(battle, MassiveCombatEventType.OrderChanged, formation, null, 0, $"{formation.Name}: {order.Type}.");
        return new(true, $"{formation.Name} acknowledged {order.Type}.");
    }

    public MassiveCombatMetrics Advance(MassiveCombatBattleState battle, double elapsedSeconds)
    {
        ArgumentNullException.ThrowIfNull(battle);
        if (!double.IsFinite(elapsedSeconds) || elapsedSeconds < 0) throw new ArgumentOutOfRangeException(nameof(elapsedSeconds));
        battle.Validate();
        battle.PendingSeconds += elapsedSeconds;
        var ticks = Math.Min(MassiveCombatLimits.MaxCatchUpTicks, (int)Math.Floor((battle.PendingSeconds + 1e-10) / TickSeconds));
        var metrics = new TickMetrics();
        for (var index = 0; index < ticks; index++)
        {
            battle.PendingSeconds -= TickSeconds;
            Step(battle, metrics);
        }
        if (battle.PendingSeconds < 1e-9) battle.PendingSeconds = 0;
        battle.LastMetrics = new(battle.Tick, battle.Formations.Count(x => x.Active), battle.Formations.Where(x => x.Active).Sum(x => x.ActiveShipCount), metrics.Cells, metrics.Candidates, metrics.WeaponGroups, battle.Events.Count);
        return battle.LastMetrics;
    }

    private void Step(MassiveCombatBattleState battle, TickMetrics metrics)
    {
        battle.Tick++;
        battle.SimulatedSeconds = battle.Tick * TickSeconds;
        var active = battle.Formations.Where(x => x.Active).OrderBy(x => x.Id).ToArray();
        var grid = SpatialIndex.Build(active); metrics.Cells = Math.Max(metrics.Cells, grid.CellCount);
        var targets = new Dictionary<long, MassiveFormationState>();
        foreach (var formation in active)
        {
            UpdatePowerAndHeat(formation);
            var target = ResolveTarget(formation, battle, grid, metrics);
            if (target is not null) targets[formation.Id] = target;
            Move(formation, target);
        }

        UpdateInterdiction(active, grid, metrics);
        var attacks = AdvanceSalvos(battle);
        foreach (var source in active)
            if (targets.TryGetValue(source.Id, out var target)) BuildAttacks(battle, source, target, attacks, metrics);
        ResolveAttacks(battle, attacks);
        foreach (var formation in active.Where(x => x.Active && IsWithdrawal(x))) AdvanceWarp(battle, formation);
        TrimEvents(battle);
    }

    private MassiveFormationState? ResolveTarget(MassiveFormationState source, MassiveCombatBattleState battle, SpatialIndex grid, TickMetrics metrics)
    {
        if (source.Order == MassiveCombatOrderType.ProtectCriticalAsset && source.ProtectedFormationId is long protectedId)
        {
            var asset = battle.Formations.FirstOrDefault(x => x.Id == protectedId && x.Active && x.CivilizationId == source.CivilizationId);
            if (asset is not null)
                return grid.Nearby(asset.Position, 2, metrics).Where(x => x.Active && _hostility.AreHostile(source.CivilizationId, x.CivilizationId))
                    .OrderBy(x => Vector2.DistanceSquared(asset.Position, x.Position)).ThenBy(x => x.Id).FirstOrDefault();
        }
        if (source.TargetFormationId is long explicitId)
        {
            var explicitTarget = battle.Formations.FirstOrDefault(x => x.Id == explicitId && x.Active && _hostility.AreHostile(source.CivilizationId, x.CivilizationId));
            if (explicitTarget is not null) return explicitTarget;
        }
        if (source.Order == MassiveCombatOrderType.Breakout || source.WarpBlocked)
        {
            var interdictor = grid.Nearby(source.Position, 4, metrics)
                .Where(x => x.Active && _hostility.AreHostile(source.CivilizationId, x.CivilizationId) && ActiveInterdictor(x) is not null)
                .OrderBy(x => Vector2.DistanceSquared(source.Position, x.Position)).ThenBy(x => x.Id).FirstOrDefault();
            if (interdictor is not null) return interdictor;
        }
        if (source.Order is MassiveCombatOrderType.Hold or MassiveCombatOrderType.Retreat or MassiveCombatOrderType.Disengage or MassiveCombatOrderType.EmergencyRetreat) return null;
        return grid.Nearby(source.Position, 4, metrics)
            .Where(x => x.Active && _hostility.AreHostile(source.CivilizationId, x.CivilizationId))
            .OrderBy(x => Vector2.DistanceSquared(source.Position, x.Position)).ThenBy(x => x.Id).FirstOrDefault();
    }

    private static void Move(MassiveFormationState formation, MassiveFormationState? target)
    {
        var at = formation.Position.Vector;
        var desiredPoint = formation.Objective.Vector;
        if (target is not null)
        {
            var direction = SafeDirection(target.Position.Vector - at, formation.Heading.Vector);
            var side = new Vector2(-direction.Y, direction.X);
            var desiredRange = formation.Order is MassiveCombatOrderType.StandoffAttack or MassiveCombatOrderType.AdvanceCautiously ? .82f * MaximumWeaponRange(formation) : .45f * MaximumWeaponRange(formation);
            desiredPoint = target.Position.Vector - direction * desiredRange;
            if (formation.Order == MassiveCombatOrderType.FlankLeft) desiredPoint += side * 260;
            if (formation.Order == MassiveCombatOrderType.FlankRight) desiredPoint -= side * 260;
            if (formation.Order is MassiveCombatOrderType.Screen or MassiveCombatOrderType.ProtectCriticalAsset) desiredPoint = Vector2.Lerp(at, target.Position.Vector, .35f);
        }
        if (IsWithdrawal(formation))
        {
            var away = target is null ? SafeDirection(at, formation.Heading.Vector) : SafeDirection(at - target.Position.Vector, formation.Heading.Vector);
            desiredPoint = at + away * 2_000;
            formation.Shape = formation.Order == MassiveCombatOrderType.Breakout ? MassiveFormationShape.Breakout : MassiveFormationShape.RetreatColumn;
        }
        var delta = desiredPoint - at;
        var desiredVelocity = delta.LengthSquared() < 4 ? Vector2.Zero : Vector2.Normalize(delta) * SpeedFor(formation);
        var velocity = formation.Velocity.Vector;
        var change = desiredVelocity - velocity;
        var limit = formation.Loadout.Acceleration * MassMobility(formation) * (float)TickSeconds * Math.Clamp(formation.Cohesion, .2f, 1f);
        if (change.Length() > limit) change = Vector2.Normalize(change) * limit;
        velocity += change;
        formation.Position = at + velocity * (float)TickSeconds;
        formation.Velocity = velocity;
        if (velocity.LengthSquared() > .01f) formation.Heading = Vector2.Normalize(velocity);
    }

    private static float SpeedFor(MassiveFormationState formation)
    {
        var factor = formation.Order switch { MassiveCombatOrderType.AdvanceCautiously => .45f, MassiveCombatOrderType.StandoffAttack => .6f, MassiveCombatOrderType.EmergencyRetreat => 1f, MassiveCombatOrderType.Retreat => .85f, _ => .72f };
        return formation.Loadout.MaximumSpeed * MassMobility(formation) * factor * Math.Clamp(formation.Cohesion, .3f, 1f);
    }

    private static void UpdatePowerAndHeat(MassiveFormationState formation)
    {
        var ships = formation.ActiveShipCount;
        var reactorCondition = ModuleCondition(formation, MassiveModuleKind.Reactor, defaultValue: 1f);
        var output = formation.Loadout.ReactorOutputPerShip * ships * reactorCondition;
        var modulePower = formation.Loadout.Modules.Where(x => x.Enabled && x.Condition > 0).Sum(x => x.PowerPerSecondEach * x.InstalledCount);
        var weaponPower = formation.Loadout.Weapons.Sum(x => x.PowerPerSecond * x.MountsPerShip * ships);
        var demand = modulePower + weaponPower;
        formation.PowerReserve = demand <= Epsilon ? 1 : Math.Clamp(output / demand, 0, 1);
        var heatInput = formation.Loadout.Modules.Where(x => x.Enabled).Sum(x => x.HeatPerSecondEach * x.InstalledCount) + formation.Loadout.Weapons.Sum(x => x.HeatPerSecond * x.MountsPerShip * ships) * formation.PowerReserve;
        formation.Heat = Math.Max(0, formation.Heat + (heatInput - formation.Loadout.CoolingPerShip * ships) * (float)TickSeconds);
    }

    private static void BuildAttacks(MassiveCombatBattleState battle, MassiveFormationState source, MassiveFormationState target, List<Attack> attacks, TickMetrics metrics)
    {
        var range = Vector2.Distance(source.Position, target.Position);
        var heatFactor = 1f / (1f + source.Heat / Math.Max(1, source.ActiveShipCount * 250f));
        var controlFactor = ModuleCondition(source, MassiveModuleKind.WeaponControl, 1f);
        var ewAttack = source.Loadout.Weapons.Where(x => x.Kind == MassiveWeaponKind.ElectronicWarfare && range <= x.Range).Sum(x => x.DamagePerShot * x.MountsPerShip) * source.PowerReserve;
        var targetEw = target.Loadout.Weapons.Where(x => x.Kind == MassiveWeaponKind.ElectronicWarfare).Sum(x => x.DamagePerShot * x.MountsPerShip);
        var electronicFactor = Math.Clamp(1f + (ewAttack - targetEw) / 500f, .55f, 1.25f);
        foreach (var weapon in source.Loadout.Weapons.OrderBy(x => x.Id, StringComparer.Ordinal))
        {
            if (weapon.Kind is MassiveWeaponKind.PointDefense or MassiveWeaponKind.ElectronicWarfare || range > weapon.Range) continue;
            metrics.WeaponGroups++;
            var jitter = .92f + DeterministicUnit(battle.Seed, battle.Tick, source.Id, target.Id, weapon.Id) * .16f;
            var shots = source.ActiveShipCount * weapon.MountsPerShip * weapon.ShotsPerSecond * (float)TickSeconds;
            var accuracy = Math.Clamp(weapon.Accuracy * electronicFactor * ShapeAccuracy(source.Shape) * heatFactor * controlFactor, .05f, .98f);
            var damage = shots * weapon.DamagePerShot * accuracy * jitter * source.PowerReserve;
            var shotCount = Math.Max(0, (int)MathF.Round(shots));
            if (weapon.Kind == MassiveWeaponKind.Missile)
                QueueMissileSalvo(battle, source, target, damage, shotCount, range, attacks);
            else
                attacks.Add(new(source, target, weapon.Kind, damage, shotCount));
            Emit(battle, weapon.Kind switch { MassiveWeaponKind.Beam => MassiveCombatEventType.BeamVolley, MassiveWeaponKind.Kinetic => MassiveCombatEventType.KineticVolley, _ => MassiveCombatEventType.MissileSalvo }, source, target, (int)MathF.Round(shots), $"{source.Name} fired an aggregated {weapon.Kind} volley.");
        }
    }

    private static List<Attack> AdvanceSalvos(MassiveCombatBattleState battle)
    {
        var arrived = new List<Attack>();
        for (var index = battle.ActiveSalvos.Count - 1; index >= 0; index--)
        {
            var salvo = battle.ActiveSalvos[index];
            salvo.RemainingSeconds = Math.Max(0, salvo.RemainingSeconds - (float)TickSeconds);
            if (salvo.RemainingSeconds > Epsilon) continue;
            var source = battle.Formations.FirstOrDefault(x => x.Id == salvo.SourceFormationId);
            var target = battle.Formations.FirstOrDefault(x => x.Id == salvo.TargetFormationId && x.Active);
            if (source is not null && target is not null)
                arrived.Add(new(source, target, MassiveWeaponKind.Missile, salvo.Damage, salvo.MissileCount));
            battle.ActiveSalvos.RemoveAt(index);
        }
        arrived.Sort((a, b) => a.Source.Id != b.Source.Id ? a.Source.Id.CompareTo(b.Source.Id) : a.Target.Id.CompareTo(b.Target.Id));
        return arrived;
    }

    private static void QueueMissileSalvo(MassiveCombatBattleState battle, MassiveFormationState source,
        MassiveFormationState target, float damage, int missileCount, float range, List<Attack> immediateOverflow)
    {
        if (damage <= Epsilon || missileCount <= 0) return;
        if (battle.ActiveSalvos.Count >= MassiveCombatLimits.MaxActiveSalvos)
        {
            // Preserve authoritative damage without allowing unbounded in-flight state.
            immediateOverflow.Add(new(source, target, MassiveWeaponKind.Missile, damage, missileCount));
            return;
        }
        var flightSeconds = Math.Max((float)TickSeconds, range / 800f);
        battle.ActiveSalvos.Add(new()
        {
            Id = battle.NextSalvoId++, SourceFormationId = source.Id, TargetFormationId = target.Id,
            Damage = damage, MissileCount = missileCount,
            RemainingSeconds = flightSeconds, LaunchPosition = source.Position, InitialFlightSeconds = flightSeconds,
        });
    }

    private static void ResolveAttacks(MassiveCombatBattleState battle, IReadOnlyList<Attack> attacks)
    {
        foreach (var group in attacks.GroupBy(x => x.Target.Id).OrderBy(x => x.Key))
        {
            var target = group.First().Target;
            var direct = group.Where(x => x.Kind != MassiveWeaponKind.Missile).Sum(x => x.Damage);
            var missileAttacks = group.Where(x => x.Kind == MassiveWeaponKind.Missile).ToArray();
            var missileDamage = missileAttacks.Sum(x => x.Damage);
            var missiles = missileAttacks.Sum(x => x.ShotCount);
            var pd = target.Loadout.Weapons.Where(x => x.Kind == MassiveWeaponKind.PointDefense)
                .Sum(x => x.MountsPerShip * x.ShotsPerSecond * target.ActiveShipCount * (float)TickSeconds * x.Accuracy) * target.PowerReserve;
            var intercepted = Math.Min(missiles, (int)MathF.Floor(pd));
            if (missiles > 0) missileDamage *= 1f - (float)intercepted / missiles;
            if (intercepted > 0) Emit(battle, MassiveCombatEventType.MissileIntercepted, target, null, intercepted, $"{target.Name} point defense intercepted {intercepted:N0} missiles.");
            ApplyDamage(battle, target, direct + missileDamage, group.OrderBy(x => x.Source.Id).First().Source);
        }
    }

    private static void ApplyDamage(MassiveCombatBattleState battle, MassiveFormationState target, float damage, MassiveFormationState source)
    {
        if (damage <= Epsilon || !target.Active) return;
        var shipsBefore = target.ActiveShipCount;
        var remaining = damage;
        var shields = Math.Min(target.ShieldPool, remaining); target.ShieldPool -= shields; remaining -= shields;
        var armor = Math.Min(target.ArmorPool, remaining); target.ArmorPool -= armor; remaining -= armor;
        var hull = Math.Min(target.HullPool, remaining); target.HullPool -= hull;
        target.HullDamageRemainder += hull;
        var systemShock = hull / Math.Max(1f, target.Loadout.HullPerShip * shipsBefore);
        foreach (var module in target.Loadout.Modules)
            module.Condition = Math.Max(0, module.Condition - systemShock * .18f);
        foreach (var vessel in target.ImportantVessels.Where(x => !x.Destroyed && !x.Escaped))
        {
            vessel.HullFraction = Math.Max(0, vessel.HullFraction - systemShock * .12f);
            vessel.EngineFraction = Math.Max(0, vessel.EngineFraction - systemShock * .05f);
            vessel.WarpDriveFraction = Math.Max(0, vessel.WarpDriveFraction - systemShock * .04f);
            vessel.ReactorFraction = Math.Max(0, vessel.ReactorFraction - systemShock * .035f);
            if (vessel.IsInterdictor) vessel.InterdictorFraction = Math.Max(0, vessel.InterdictorFraction - systemShock * .08f);
        }
        var hullLossThreshold = target.HullLossThresholdPerShip > Epsilon
            ? target.HullLossThresholdPerShip
            : target.Loadout.HullPerShip;
        var losses = Math.Min(target.ActiveShipCount, (int)(target.HullDamageRemainder / hullLossThreshold));
        if (losses > 0)
        {
            target.HullDamageRemainder -= losses * hullLossThreshold;
            RemoveShips(target, losses);
            target.DestroyedShips += losses;
            target.Morale = Math.Max(0, target.Morale - losses / (float)Math.Max(1, target.InitialShipCount) * .8f);
        }
        Emit(battle, MassiveCombatEventType.Damage, source, target, losses, $"{target.Name} sustained {damage:0} aggregate damage and lost {losses:N0} ships.");
        if (!target.Active)
            Emit(battle, MassiveCombatEventType.FormationDestroyed, source, target, target.DestroyedShips, $"{target.Name} was destroyed.");
    }

    private static void RemoveShips(MassiveFormationState target, int losses)
    {
        foreach (var cohort in target.Cohorts.OrderByDescending(x => x.ActiveCount).ThenBy(x => x.Id))
        {
            var removed = Math.Min(losses, cohort.ActiveCount); cohort.ActiveCount -= removed; losses -= removed;
            if (losses == 0) return;
        }
        foreach (var vessel in target.ImportantVessels.Where(x => !x.Destroyed && !x.Escaped).OrderBy(x => Importance(x)).ThenByDescending(x => x.Id))
        {
            vessel.Destroyed = true; vessel.HullFraction = 0; losses--;
            if (losses == 0) return;
        }
    }

    private void UpdateInterdiction(IReadOnlyList<MassiveFormationState> active, SpatialIndex grid, TickMetrics metrics)
    {
        foreach (var target in active)
        {
            var fields = grid.Nearby(target.Position, 4, metrics).Where(source => _hostility.AreHostile(source.CivilizationId, target.CivilizationId))
                .Select(source => (Source: source, Module: ActiveInterdictor(source)))
                .Where(x => x.Module is not null && Vector2.Distance(x.Source.Position, target.Position) <= x.Module!.EffectiveRange)
                .OrderByDescending(x => x.Module!.FieldStrength * x.Module.Condition * x.Source.PowerReserve).ThenBy(x => x.Source.Id)
                .Select(x => x.Module!.FieldStrength * x.Module.Condition * x.Source.PowerReserve).ToArray();
            if (fields.Length == 0) { target.WarpBlocked = false; continue; }
            var strength = fields[0];
            for (var index = 1; index < fields.Length; index++) strength += fields[index] * (.35f / index);
            strength = Math.Min(strength, fields[0] * 1.75f);
            target.WarpBlocked = strength > target.Loadout.WarpStabilization * ModuleCondition(target, MassiveModuleKind.WarpDrive, 1f);
        }
    }

    private static void AdvanceWarp(MassiveCombatBattleState battle, MassiveFormationState formation)
    {
        var warpCondition = ModuleCondition(formation, MassiveModuleKind.WarpDrive, 1f);
        var reactorCondition = ModuleCondition(formation, MassiveModuleKind.Reactor, 1f);
        if (warpCondition <= .05f || reactorCondition <= .05f || formation.PowerReserve < .2f) { formation.WarpSpoolProgress = 0; return; }
        if (formation.WarpBlocked)
        {
            formation.WarpSpoolProgress = Math.Min(.95f, formation.WarpSpoolProgress);
            Emit(battle, MassiveCombatEventType.WarpBlocked, formation, null, 0, "Warp completion blocked by a hostile interdiction field.");
            return;
        }
        var multiplier = formation.Order == MassiveCombatOrderType.EmergencyRetreat ? 1.4f : 1f;
        formation.WarpSpoolProgress += (float)TickSeconds * multiplier / formation.Loadout.WarpSpoolSeconds;
        if (formation.WarpSpoolProgress + Epsilon < 1) return;
        var escapedShips = formation.SurvivingShipCount;
        formation.WarpSpoolProgress = 1; formation.Escaped = true;
        foreach (var vessel in formation.ImportantVessels.Where(x => !x.Destroyed)) vessel.Escaped = true;
        Emit(battle, MassiveCombatEventType.Escaped, formation, null, escapedShips, $"{formation.Name} completed warp escape.");
    }

    private static MassiveModuleState? ActiveInterdictor(MassiveFormationState formation) => formation.Loadout.Modules
        .Where(x => x.Kind == MassiveModuleKind.WarpInterdictor && x.Enabled && x.Condition > .05f && formation.PowerReserve >= .2f)
        .OrderByDescending(x => x.FieldStrength * x.Condition).ThenBy(x => x.Id, StringComparer.Ordinal).FirstOrDefault();

    private static float ModuleCondition(MassiveFormationState formation, MassiveModuleKind kind, float defaultValue) =>
        formation.Loadout.Modules.Where(x => x.Kind == kind && x.Enabled).Select(x => x.Condition).DefaultIfEmpty(defaultValue).Max();
    private static float MaximumWeaponRange(MassiveFormationState formation) => formation.Loadout.Weapons.Where(x => x.Kind is not MassiveWeaponKind.PointDefense and not MassiveWeaponKind.ElectronicWarfare).Select(x => x.Range).DefaultIfEmpty(500).Max();
    private static float ShapeAccuracy(MassiveFormationShape shape) => shape switch { MassiveFormationShape.Line => 1.08f, MassiveFormationShape.Wedge => 1.04f, MassiveFormationShape.Dispersed => .88f, MassiveFormationShape.RetreatColumn => .72f, _ => 1f };
    private static float MassMobility(MassiveFormationState formation)
    {
        var moduleMass = formation.Loadout.Modules.Sum(x => x.MassEach * x.InstalledCount);
        return Math.Clamp(formation.Loadout.MassPerShip / Math.Max(1f, formation.Loadout.MassPerShip + moduleMass), .25f, 1f);
    }
    private static Vector2 SafeDirection(Vector2 value, Vector2 fallback) => value.LengthSquared() > Epsilon ? Vector2.Normalize(value) : fallback.LengthSquared() > Epsilon ? Vector2.Normalize(fallback) : Vector2.UnitX;
    private static bool RequiresTarget(MassiveCombatOrderType type) => type is MassiveCombatOrderType.Engage or MassiveCombatOrderType.FocusFire or MassiveCombatOrderType.Intercept or MassiveCombatOrderType.Pursue or MassiveCombatOrderType.ProtectCriticalAsset;
    private static bool IsWithdrawal(MassiveCombatOrderType type) => type is MassiveCombatOrderType.BreakContact or MassiveCombatOrderType.Disengage or MassiveCombatOrderType.Retreat or MassiveCombatOrderType.EmergencyRetreat or MassiveCombatOrderType.Breakout;
    private static bool IsWithdrawal(MassiveFormationState formation) => IsWithdrawal(formation.Order);
    private static int Importance(MassiveVesselState vessel) => vessel.IsStoryShip ? 5 : vessel.IsFlagship ? 4 : vessel.IsInterdictor ? 3 : vessel.IsCarrier ? 2 : 1;

    private static float DeterministicUnit(ulong seed, long tick, long source, long target, string weapon)
    {
        var value = seed ^ (ulong)tick * 0x9E3779B97F4A7C15UL ^ (ulong)source * 0xBF58476D1CE4E5B9UL ^ (ulong)target;
        foreach (var character in weapon) value = (value ^ character) * 0x100000001B3UL;
        value ^= value >> 30; value *= 0xBF58476D1CE4E5B9UL; value ^= value >> 27; value *= 0x94D049BB133111EBUL; value ^= value >> 31;
        return (value >> 40) / 16777216f;
    }

    private static void Emit(MassiveCombatBattleState battle, MassiveCombatEventType type, MassiveFormationState actor, MassiveFormationState? target, int magnitude, string message)
    {
        if (battle.Events.Count(x => x.Tick == battle.Tick) >= MassiveCombatLimits.MaxEventsPerTick) return;
        battle.Events.Add(new(battle.NextEventSequence++, battle.Tick, type, actor.CivilizationId, actor.Id, target?.CivilizationId, target?.Id, Math.Max(0, magnitude), actor.Position, message));
        if (battle.Events.Count > MassiveCombatLimits.MaxRetainedEvents) battle.Events.RemoveAt(0);
    }

    private static void TrimEvents(MassiveCombatBattleState battle)
    {
        var excess = battle.Events.Count - MassiveCombatLimits.MaxRetainedEvents;
        if (excess > 0) battle.Events.RemoveRange(0, excess);
    }

    private sealed record Attack(MassiveFormationState Source, MassiveFormationState Target, MassiveWeaponKind Kind, float Damage, int ShotCount);
    private sealed class TickMetrics { public int Cells; public int Candidates; public int WeaponGroups; }

    private sealed class SpatialIndex
    {
        private readonly Dictionary<(int X, int Y), List<MassiveFormationState>> _cells = new();
        public int CellCount => _cells.Count;
        public static SpatialIndex Build(IEnumerable<MassiveFormationState> formations)
        {
            var result = new SpatialIndex();
            foreach (var formation in formations)
            {
                var key = Cell(formation.Position);
                if (!result._cells.TryGetValue(key, out var list)) result._cells[key] = list = new();
                list.Add(formation);
            }
            foreach (var list in result._cells.Values) list.Sort((a, b) => a.Id.CompareTo(b.Id));
            return result;
        }
        public IReadOnlyList<MassiveFormationState> Nearby(MassivePoint position, int radius, TickMetrics metrics)
        {
            var result = new List<MassiveFormationState>();
            var origin = Cell(position);
            for (var y = origin.Y - radius; y <= origin.Y + radius; y++)
                for (var x = origin.X - radius; x <= origin.X + radius; x++)
                    if (_cells.TryGetValue((x, y), out var list))
                        foreach (var item in list) { metrics.Candidates++; result.Add(item); }
            return result;
        }
        private static (int X, int Y) Cell(MassivePoint point) => ((int)MathF.Floor(point.X / SpatialCellSize), (int)MathF.Floor(point.Y / SpatialCellSize));
    }
}
