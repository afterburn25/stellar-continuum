using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class OwnCombatFleetStatusValidation
{
    private static readonly JsonSerializerOptions SnapshotOptions = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };

    [Game.Validation.RegressionCheck]
    internal static void RunOwnCombatFleetStatusChecks()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4F57_4E43_4F4D_4241L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var local = galaxy.Systems[0];
        var remote = galaxy.Systems[1];
        var owner = galaxy.Civilizations[0].Id;
        var rival = galaxy.Civilizations[1].Id;

        var target = CreateFleet(
            7900,
            rival,
            "Hidden Rival Dreadnought Stand-in",
            FleetRole.Military,
            local.Id,
            local.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        target.Combat!.Shields = 35.0;
        target.Combat.Armor = 45.0;
        target.Combat.Hull = 95.0;

        var damaged = CreateFleet(
            7101,
            owner,
            "Own Damaged Patrol",
            FleetRole.Military,
            local.Id,
            local.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        damaged.Combat!.Shields = 17.5;
        damaged.Combat.Armor = 21.0;
        damaged.Combat.Hull = 70.0;
        damaged.Combat.WeaponCooldownRemainingDays = 0.25;
        damaged.Combat.Order = MilitaryOrderType.Attack;
        damaged.Combat.TargetFleetId = target.Id;

        var retreating = CreateFleet(
            7102,
            owner,
            "Own Retreating Patrol",
            FleetRole.Military,
            local.Id,
            local.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        retreating.Combat!.Order = MilitaryOrderType.Retreat;
        retreating.Combat.RetreatStarted = true;
        retreating.Combat.RetreatProgressDays = 99.0;
        retreating.Combat.WeaponCooldownRemainingDays = double.NaN;

        var malformed = CreateFleet(
            7103,
            owner,
            "Own Legacy Patrol",
            FleetRole.Military,
            remote.Id,
            remote.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        malformed.Combat = new FleetCombatState
        {
            ProfileId = "unknown_legacy_profile",
            Shields = double.NaN,
            Armor = -500.0,
            Hull = double.PositiveInfinity,
            WeaponCooldownRemainingDays = double.NegativeInfinity,
            Order = (MilitaryOrderType)999,
            TargetFleetId = target.Id,
            DefendSystemId = remote.Id,
            RetreatProgressDays = double.PositiveInfinity,
            IsDisengaged = true,
            DisengagedSystemId = local.Id,
        };

        var unarmed = CreateFleet(
            7104,
            owner,
            "Own Science Vessel",
            FleetRole.Science,
            local.Id,
            local.Position,
            CombatProfileIds.CivilianScience);
        unarmed.Combat = null; // read model must use pristine role default without materializing it.

        var inactiveOwn = CreateFleet(
            7105,
            owner,
            "Inactive Own Vessel",
            FleetRole.Military,
            local.Id,
            local.Position,
            CombatProfileIds.PatrolCorvetteMk1);
        inactiveOwn.IsActive = false;

        galaxy.Fleets.Add(damaged);
        galaxy.Fleets.Add(retreating);
        galaxy.Fleets.Add(malformed);
        galaxy.Fleets.Add(unarmed);
        galaxy.Fleets.Add(inactiveOwn);
        galaxy.Fleets.Add(target);

        var before = Snapshot(galaxy);
        var view = OwnCombatFleetStatusBuilder.Build(galaxy, owner);

        Require(before == Snapshot(galaxy),
            "own Combat fleet status view mutated authoritative fleet/Combat state");
        Require(view.CivilizationId == owner,
            "own Combat fleet status view returned the wrong civilization identity");
        Require(view.Fleets.Select(fleet => fleet.FleetId).SequenceEqual(new[] { 7101, 7102, 7103, 7104 }),
            "own Combat fleet status view is not stable, own-only, active-only, ascending fleet-ID order");
        Require(view.Fleets.All(fleet => fleet.FleetId != target.Id && fleet.FleetId != inactiveOwn.Id),
            "own Combat fleet status view leaked foreign or inactive vessel state");

        var damagedStatus = view.Fleets.Single(fleet => fleet.FleetId == damaged.Id);
        Require(Math.Abs(damagedStatus.Shields - 17.5) < 0.000001 &&
                Math.Abs(damagedStatus.Armor - 21.0) < 0.000001 &&
                Math.Abs(damagedStatus.Hull - 70.0) < 0.000001,
            "own damaged vessel durability did not remain exact");
        Require(damagedStatus.IsArmed && damagedStatus.IsCombatEffective && damagedStatus.IsDamaged &&
                damagedStatus.HasHullDamage && !damagedStatus.CanFireNow,
            "own damaged vessel readiness flags are incorrect");
        Require(damagedStatus.CurrentOrder == MilitaryOrderType.Attack && damagedStatus.HasAssignedAttackTarget,
            "own Attack order state did not expose assigned-target presence");
        Require(typeof(OwnCombatFleetStatus).GetProperty("TargetFleetId") is null,
            "own status contract exposes exact foreign Attack target identity despite the unresolved observer-target boundary");

        var retreatStatus = view.Fleets.Single(fleet => fleet.FleetId == retreating.Id);
        Require(retreatStatus.IsRetreating && !retreatStatus.IsCombatEffective && !retreatStatus.CanFireNow,
            "retreating own vessel was incorrectly classified as combat-effective/fire-ready");
        Require(Math.Abs(retreatStatus.RetreatProgressDays - retreatStatus.RetreatDelayDays) < 0.000001 &&
                Math.Abs(retreatStatus.RetreatProgressRatio - 1.0) < 0.000001,
            "retreat progress was not bounded to the profile's represented disengagement delay");
        Require(retreatStatus.WeaponCooldownRemainingDays == 0.0,
            "non-finite own weapon cooldown was not safely normalized in the read model");

        var malformedStatus = view.Fleets.Single(fleet => fleet.FleetId == malformed.Id);
        var patrol = CombatProfileRegistry.Get(CombatProfileIds.PatrolCorvetteMk1);
        Require(malformedStatus.CombatProfileId == CombatProfileIds.PatrolCorvetteMk1 &&
                Math.Abs(malformedStatus.Shields - patrol.MaxShields) < 0.000001 &&
                Math.Abs(malformedStatus.Armor - patrol.MaxArmor) < 0.000001 &&
                Math.Abs(malformedStatus.Hull - patrol.MaxHull) < 0.000001,
            "malformed own Combat state was not evaluated as the pristine role-safe default");
        Require(malformedStatus.CurrentOrder == MilitaryOrderType.Hold &&
                !malformedStatus.HasAssignedAttackTarget &&
                malformedStatus.DefendSystemId is null &&
                !malformedStatus.IsDisengaged,
            "malformed own command/disengagement state leaked through instead of using safe defaults");

        var scienceStatus = view.Fleets.Single(fleet => fleet.FleetId == unarmed.Id);
        Require(!scienceStatus.IsArmed && !scienceStatus.IsCombatEffective && !scienceStatus.CanFireNow &&
                scienceStatus.CombatProfileId == CombatProfileIds.CivilianScience,
            "missing science-vessel Combat state did not use the non-mutating role-safe default");
        Require(unarmed.Combat is null,
            "own status view materialized missing Combat state on the source science vessel");

        Require(view.Summary.ActiveVessels == view.Fleets.Count &&
                view.Summary.ActiveArmedVessels == view.Fleets.Count(fleet => fleet.IsArmed) &&
                view.Summary.CombatEffectiveArmedVessels == view.Fleets.Count(fleet => fleet.IsCombatEffective) &&
                view.Summary.DamagedVessels == view.Fleets.Count(fleet => fleet.IsDamaged),
            "per-vessel status counts diverged from canonical CombatReadinessSummary");
        Require(Math.Abs(view.Summary.CurrentDurability - view.Fleets.Sum(fleet => fleet.CurrentDurability)) < 0.000001 &&
                Math.Abs(view.Summary.MaximumDurability - view.Fleets.Sum(fleet => fleet.MaximumDurability)) < 0.000001 &&
                Math.Abs(view.Summary.CurrentArmedStrength - view.Fleets.Where(fleet => fleet.IsArmed).Sum(fleet => fleet.CurrentStrength)) < 0.000001 &&
                Math.Abs(view.Summary.MaximumArmedStrength - view.Fleets.Where(fleet => fleet.IsArmed).Sum(fleet => fleet.MaximumStrength)) < 0.000001 &&
                Math.Abs(view.Summary.TotalRepairDeficit - view.Fleets.Sum(fleet => fleet.RepairDeficit)) < 0.000001,
            "per-vessel status scalars diverged from canonical CombatReadinessSummary");
        Require(view.Fleets.All(IsFinite),
            "own Combat fleet status emitted non-finite values");

        RequireThrows<InvalidOperationException>(
            () => OwnCombatFleetStatusBuilder.Build(galaxy, 999999),
            "own Combat fleet status accepted an unknown civilization");

        Console.WriteLine("PASS: exact-own non-mutating per-vessel Combat status");
    }

    private static bool IsFinite(OwnCombatFleetStatus status) =>
        double.IsFinite(status.Shields) &&
        double.IsFinite(status.MaximumShields) &&
        double.IsFinite(status.Armor) &&
        double.IsFinite(status.MaximumArmor) &&
        double.IsFinite(status.Hull) &&
        double.IsFinite(status.MaximumHull) &&
        double.IsFinite(status.WeaponCooldownRemainingDays) &&
        double.IsFinite(status.RetreatProgressDays) &&
        double.IsFinite(status.RetreatDelayDays) &&
        double.IsFinite(status.CurrentStrength) &&
        double.IsFinite(status.MaximumStrength) &&
        double.IsFinite(status.RepairDeficit) &&
        double.IsFinite(status.DurabilityRatio) &&
        double.IsFinite(status.HullIntegrityRatio) &&
        double.IsFinite(status.RetreatProgressRatio);

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        string name,
        FleetRole role,
        int systemId,
        Vector2 position,
        string combatProfileId) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = role,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 21.0,
        SensorRange = 125.0f,
        IsActive = true,
        Combat = CombatProfileRegistry.CreateInitialState(combatProfileId, role),
    };

    private static string Snapshot(GalaxyState galaxy) =>
        JsonSerializer.Serialize(
            galaxy.Fleets
                .OrderBy(fleet => fleet.Id)
                .Select(fleet => new
                {
                    fleet.Id,
                    fleet.CivilizationId,
                    fleet.IsActive,
                    fleet.CurrentSystemId,
                    fleet.DestinationSystemId,
                    Combat = fleet.Combat is null ? null : new
                    {
                        fleet.Combat.ProfileId,
                        fleet.Combat.Shields,
                        fleet.Combat.Armor,
                        fleet.Combat.Hull,
                        fleet.Combat.WeaponCooldownRemainingDays,
                        fleet.Combat.Order,
                        fleet.Combat.TargetFleetId,
                        fleet.Combat.DefendSystemId,
                        fleet.Combat.RetreatProgressDays,
                        fleet.Combat.RetreatStarted,
                        fleet.Combat.IsDisengaged,
                        fleet.Combat.DisengagedSystemId,
                    },
                }),
            SnapshotOptions);

    private static void RequireThrows<TException>(Action action, string message)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
