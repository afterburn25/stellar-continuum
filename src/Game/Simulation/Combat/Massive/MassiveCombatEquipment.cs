using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Combat;

namespace Game.Simulation.Combat.Massive;

public static class MassiveEquipmentIds
{
    public const string Reactor = "reactor";
    public const string WarpDrive = "warp_drive";
    public const string BeamBattery = "beam_battery";
    public const string KineticBattery = "kinetic_battery";
    public const string MissileBattery = "missile_battery";
    public const string PointDefense = "point_defense";
    public const string ElectronicWarfare = "electronic_warfare";
    public const string WarpInterdictionArray = "warp_interdiction_array";
}

public static class MassiveCombatLoadouts
{
    public static MassiveCombatLoadout FromLegacy(CombatProfileDefinition profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        var loadout = new MassiveCombatLoadout
        {
            ShieldPerShip = (float)profile.MaxShields,
            ArmorPerShip = (float)profile.MaxArmor,
            HullPerShip = (float)profile.MaxHull,
            WarpSpoolSeconds = Math.Max(4f, (float)profile.RetreatDelayDays * 8f),
        };
        loadout.Modules.Add(new() { Id = MassiveEquipmentIds.Reactor, Kind = MassiveModuleKind.Reactor, MassEach = 18, FieldStrength = 100 });
        loadout.Modules.Add(new() { Id = MassiveEquipmentIds.WarpDrive, Kind = MassiveModuleKind.WarpDrive, MassEach = 22, PowerPerSecondEach = 12 });
        if (profile.HasWeapon)
            loadout.Weapons.Add(new() { Id = MassiveEquipmentIds.BeamBattery, Kind = MassiveWeaponKind.Beam, DamagePerShot = (float)profile.WeaponDamage, ShotsPerSecond = 1f / Math.Max(.1f, (float)profile.WeaponIntervalDays * 8f), Range = 700 });
        return loadout;
    }

    public static MassiveModuleState WarpInterdictor(float range = 900f, float strength = 72f) => new()
    {
        Id = MassiveEquipmentIds.WarpInterdictionArray,
        Kind = MassiveModuleKind.WarpInterdictor,
        InstalledCount = 1,
        MassEach = 85,
        PowerPerSecondEach = 42,
        HeatPerSecondEach = 18,
        EffectiveRange = range,
        FieldStrength = strength,
        DetectionSignature = 85,
    };
}

public sealed class MassiveCombatLoadout
{
    public float MassPerShip { get; set; } = 100f;
    public float Acceleration { get; set; } = 18f;
    public float MaximumSpeed { get; set; } = 120f;
    public float ShieldPerShip { get; set; } = 35f;
    public float ArmorPerShip { get; set; } = 45f;
    public float HullPerShip { get; set; } = 95f;
    public float ReactorOutputPerShip { get; set; } = 100f;
    public float CoolingPerShip { get; set; } = 28f;
    public float WarpStabilization { get; set; } = 50f;
    public float WarpSpoolSeconds { get; set; } = 12f;
    public int ModuleSlotCapacity { get; set; } = 12;
    public float MaximumModuleMass { get; set; } = 420f;
    public List<MassiveWeaponGroup> Weapons { get; set; } = new();
    public List<MassiveModuleState> Modules { get; set; } = new();

    public void Validate()
    {
        foreach (var value in new[] { MassPerShip, Acceleration, MaximumSpeed, ShieldPerShip, ArmorPerShip, HullPerShip, ReactorOutputPerShip, CoolingPerShip, WarpStabilization, WarpSpoolSeconds })
            if (!float.IsFinite(value) || value < 0) throw new InvalidOperationException("Combat loadout contains an invalid scalar.");
        if (HullPerShip <= 0 || WarpSpoolSeconds <= 0 || ModuleSlotCapacity < 0 || MaximumModuleMass < 0 || Weapons.Count > 32 || Modules.Count > 32) throw new InvalidOperationException("Combat loadout bounds are invalid.");
        foreach (var weapon in Weapons) weapon.Validate();
        foreach (var module in Modules) module.Validate();
        if (Modules.Sum(x => x.Slots * x.InstalledCount) > ModuleSlotCapacity || Modules.Sum(x => x.MassEach * x.InstalledCount) > MaximumModuleMass)
            throw new InvalidOperationException("Installed combat modules exceed the design's slot or mass budget.");
    }
}

public sealed class MassiveWeaponGroup
{
    public string Id { get; set; } = string.Empty;
    public MassiveWeaponKind Kind { get; set; }
    public int MountsPerShip { get; set; } = 1;
    public float DamagePerShot { get; set; } = 8f;
    public float ShotsPerSecond { get; set; } = 1f;
    public float Range { get; set; } = 650f;
    public float Accuracy { get; set; } = .65f;
    public float PowerPerSecond { get; set; } = 3f;
    public float HeatPerSecond { get; set; } = 2f;
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(Id) || MountsPerShip < 0 || !Enum.IsDefined(Kind)) throw new InvalidOperationException("Weapon group identity is invalid.");
        foreach (var value in new[] { DamagePerShot, ShotsPerSecond, Range, Accuracy, PowerPerSecond, HeatPerSecond })
            if (!float.IsFinite(value) || value < 0) throw new InvalidOperationException("Weapon group scalar is invalid.");
    }
}

public sealed class MassiveModuleState
{
    public string Id { get; set; } = string.Empty;
    public MassiveModuleKind Kind { get; set; }
    public int InstalledCount { get; set; } = 1;
    public float MassEach { get; set; }
    public float PowerPerSecondEach { get; set; }
    public float HeatPerSecondEach { get; set; }
    public float Condition { get; set; } = 1f;
    public bool Enabled { get; set; } = true;
    public float EffectiveRange { get; set; }
    public float FieldStrength { get; set; }
    public float DetectionSignature { get; set; }
    public int Slots { get; set; } = 1;
    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(Id) || InstalledCount < 0 || Slots < 0 || !Enum.IsDefined(Kind) || !float.IsFinite(Condition) || Condition < 0 || Condition > 1) throw new InvalidOperationException("Module identity or condition is invalid.");
        foreach (var value in new[] { MassEach, PowerPerSecondEach, HeatPerSecondEach, EffectiveRange, FieldStrength, DetectionSignature })
            if (!float.IsFinite(value) || value < 0) throw new InvalidOperationException("Module scalar is invalid.");
        if (EffectiveRange > MassiveCombatEngine.SpatialCellSize * 4f) throw new InvalidOperationException("Module range exceeds the bounded combat spatial search.");
    }
}
