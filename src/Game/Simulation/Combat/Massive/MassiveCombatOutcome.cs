using System;
using System.Linq;

namespace Game.Simulation.Combat.Massive;

public static class MassiveCombatPowerCalculator
{
    public static float PerShipPower(MassiveCombatLoadout loadout)
    {
        ArgumentNullException.ThrowIfNull(loadout);
        loadout.Validate();
        var durability = loadout.ShieldPerShip + loadout.ArmorPerShip + loadout.HullPerShip;
        var offense = loadout.Weapons.Where(x => x.Kind is not MassiveWeaponKind.PointDefense and not MassiveWeaponKind.ElectronicWarfare)
            .Sum(x => x.DamagePerShot * x.ShotsPerSecond * x.MountsPerShip * Math.Clamp(x.Accuracy, 0, 1));
        var defense = loadout.Weapons.Where(x => x.Kind == MassiveWeaponKind.PointDefense).Sum(x => x.ShotsPerSecond * x.MountsPerShip * 4f);
        var capability = loadout.Modules.Where(x => x.Enabled).Sum(x => x.FieldStrength * x.Condition * .2f);
        return Math.Max(0, durability + offense * 8f + defense + capability);
    }

    public static float FormationPower(MassiveFormationState formation)
    {
        ArgumentNullException.ThrowIfNull(formation);
        var maximumDurability = (formation.Loadout.ShieldPerShip + formation.Loadout.ArmorPerShip + formation.Loadout.HullPerShip) * Math.Max(1, formation.SurvivingShipCount);
        var condition = Math.Clamp((formation.ShieldPool + formation.ArmorPool + formation.HullPool) / Math.Max(1, maximumDurability), 0, 1);
        return PerShipPower(formation.Loadout) * formation.SurvivingShipCount * condition * Math.Clamp(formation.Cohesion, .2f, 1f);
    }

    public static float EffectivePerShipPower(MassiveFormationState formation) => formation.SurvivingShipCount <= 0
        ? 0
        : FormationPower(formation) / formation.SurvivingShipCount;

    public static float ImportantVesselPower(MassiveCombatLoadout loadout, MassiveVesselState vessel) =>
        PerShipPower(loadout) * Math.Clamp((vessel.HullFraction + vessel.EngineFraction + vessel.SensorFraction +
            vessel.WarpDriveFraction + vessel.ReactorFraction) / 5f, 0, 1);
}

public static class MassiveCombatOutcomeBuilder
{
    public static MassiveCombatAftermath Build(MassiveCombatBattleState battle)
    {
        ArgumentNullException.ThrowIfNull(battle);
        var fleets = battle.Formations.GroupBy(x => (x.FleetId, x.CivilizationId)).OrderBy(x => x.Key.FleetId).Select(group =>
        {
            var surviving = group.Sum(x => x.SurvivingShipCount);
            var initial = group.Sum(x => x.InitialShipCount);
            float Fraction(Func<MassiveFormationState, float> pool, Func<MassiveCombatLoadout, float> maximum) =>
                Math.Clamp(group.Sum(pool) / Math.Max(1, group.Sum(x => maximum(x.Loadout) * x.SurvivingShipCount)), 0, 1);
            return new MassiveFleetCombatOutcome(group.Key.FleetId, group.Key.CivilizationId, surviving,
                initial - surviving, group.All(x => x.Escaped), group.Any(x => x.Surrendered),
                Fraction(x => x.ShieldPool, x => x.ShieldPerShip), Fraction(x => x.ArmorPool, x => x.ArmorPerShip),
                Fraction(x => x.HullPool, x => x.HullPerShip), group.SelectMany(x => x.ImportantVessels).OrderBy(x => x.Id).ToArray());
        }).ToArray();
        return new(battle.BattleId, battle.Tick, battle.IsComplete, fleets);
    }
}
