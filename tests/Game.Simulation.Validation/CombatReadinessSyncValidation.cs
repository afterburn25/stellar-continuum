using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class CombatReadinessSyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunCombatReadinessCheck()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5245_4144_494E_4553L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var civilization = galaxy.Civilizations[0];
        var foreign = galaxy.Civilizations[1];
        var system = galaxy.Systems[0];

        var legacy = CreateFleet(9000, civilization.Id, "Readiness Legacy Patrol", FleetRole.Military, system.Id, system.Position, null);
        galaxy.Fleets.Add(legacy);

        var damaged = CreateFleet(
            9001,
            civilization.Id,
            "Readiness Damaged Patrol",
            FleetRole.Military,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military));
        damaged.Combat!.Shields = 5.0;
        damaged.Combat.Armor = 20.0;
        damaged.Combat.Hull = 40.0;
        galaxy.Fleets.Add(damaged);

        var retreating = CreateFleet(
            9002,
            civilization.Id,
            "Readiness Retreating Patrol",
            FleetRole.Military,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military));
        retreating.Combat!.Order = MilitaryOrderType.Retreat;
        galaxy.Fleets.Add(retreating);

        var disengaged = CreateFleet(
            9003,
            civilization.Id,
            "Readiness Disengaged Patrol",
            FleetRole.Military,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military));
        disengaged.Combat!.IsDisengaged = true;
        disengaged.Combat.DisengagedSystemId = system.Id;
        galaxy.Fleets.Add(disengaged);

        var damagedColony = CreateFleet(
            9004,
            civilization.Id,
            "Readiness Damaged Colony",
            FleetRole.Colony,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.CivilianHeavy, FleetRole.Colony));
        damagedColony.Combat!.Armor = 12.0;
        damagedColony.Combat.Hull = 80.0;
        galaxy.Fleets.Add(damagedColony);

        var invalidProfile = CreateFleet(
            9005,
            civilization.Id,
            "Readiness Invalid Profile",
            FleetRole.Military,
            system.Id,
            system.Position,
            new FleetCombatState
            {
                ProfileId = "readiness-invalid-profile",
                Shields = 0.0,
                Armor = 0.0,
                Hull = 1.0,
                Order = MilitaryOrderType.Retreat,
            });
        galaxy.Fleets.Add(invalidProfile);

        var inactive = CreateFleet(
            9006,
            civilization.Id,
            "Readiness Inactive Patrol",
            FleetRole.Military,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military));
        inactive.IsActive = false;
        galaxy.Fleets.Add(inactive);

        var foreignDamaged = CreateFleet(
            9100,
            foreign.Id,
            "Readiness Foreign Patrol",
            FleetRole.Military,
            system.Id,
            system.Position,
            CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military));
        foreignDamaged.Combat!.Hull = 10.0;
        galaxy.Fleets.Add(foreignDamaged);

        var invalidBefore = (
            invalidProfile.Combat!.ProfileId,
            invalidProfile.Combat.Shields,
            invalidProfile.Combat.Armor,
            invalidProfile.Combat.Hull,
            invalidProfile.Combat.Order);
        var damagedBefore = (damaged.Combat!.Shields, damaged.Combat.Armor, damaged.Combat.Hull);

        var summary = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        var coreSummary = new GalaxySimulationStepCoordinator().GetOwnCombatReadinessSummary(galaxy, civilization.Id);

        Require(summary == coreSummary,
            "Core readiness wrapper diverged from the canonical Combat readiness calculator");
        Require(summary.ActiveVessels == 6, "active-vessel readiness count changed");
        Require(summary.ActiveArmedVessels == 5, "armed-vessel readiness count changed");
        Require(summary.CombatEffectiveArmedVessels == 3, "combat-effective armed-vessel count changed");
        Require(summary.DamagedVessels == 2 && summary.HullDamagedVessels == 2,
            "damaged/hull-damaged readiness counts changed");
        Require(summary.RetreatingVessels == 1 && summary.DisengagedVessels == 1,
            "retreat/disengagement readiness counts changed");

        RequireNear(summary.CurrentDurability, 857.0, "current own-force durability changed");
        RequireNear(summary.MaximumDurability, 1004.0, "maximum own-force durability changed");
        RequireNear(summary.TotalRepairDeficit, 147.0, "own-force repair deficit changed");
        RequireNear(summary.DurabilityRatio, 857.0 / 1004.0, "own-force durability ratio changed");

        var damagedStrength = 65.0 + 112.0 * (40.0 / 95.0);
        var expectedCurrentArmedStrength = 4.0 * 287.0 + damagedStrength;
        var expectedEffectiveArmedStrength = 2.0 * 287.0 + damagedStrength;
        RequireNear(summary.CurrentArmedStrength, expectedCurrentArmedStrength,
            "current armed strength changed");
        RequireNear(summary.MaximumArmedStrength, 5.0 * 287.0,
            "maximum armed strength changed");
        RequireNear(summary.CombatEffectiveArmedStrength, expectedEffectiveArmedStrength,
            "combat-effective armed strength changed");
        RequireNear(summary.ArmedStrengthRatio, expectedCurrentArmedStrength / (5.0 * 287.0),
            "armed strength ratio changed");

        Require(legacy.Combat is null,
            "readiness aggregation invented Combat state for a pristine legacy fleet");
        Require(invalidBefore == (
                invalidProfile.Combat!.ProfileId,
                invalidProfile.Combat.Shields,
                invalidProfile.Combat.Armor,
                invalidProfile.Combat.Hull,
                invalidProfile.Combat.Order),
            "readiness aggregation rewrote invalid Combat state instead of reading the pristine fallback");
        Require(damagedBefore == (damaged.Combat!.Shields, damaged.Combat.Armor, damaged.Combat.Hull),
            "readiness aggregation mutated authoritative damage state");

        galaxy.Fleets.Where(fleet => fleet.CivilizationId == civilization.Id).ToList().ForEach(fleet => fleet.IsActive = false);
        var empty = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        Require(empty.ActiveVessels == 0 && empty.ActiveArmedVessels == 0 && empty.CombatEffectiveArmedVessels == 0,
            "empty own force produced nonzero vessel readiness counts");
        RequireNear(empty.CurrentDurability, 0.0, "empty own force produced current durability");
        RequireNear(empty.MaximumDurability, 0.0, "empty own force produced maximum durability");
        RequireNear(empty.DurabilityRatio, 1.0, "empty own force did not use neutral durability ratio");
        RequireNear(empty.ArmedStrengthRatio, 1.0, "empty own force did not use neutral armed strength ratio");

        RequireThrows(
            () => CombatReadinessCalculator.Build(galaxy, int.MaxValue),
            "unknown civilization readiness request was not rejected");

        Console.WriteLine("PASS: exact own-force Combat readiness summary");
    }

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        string name,
        FleetRole role,
        int systemId,
        Vector2 position,
        FleetCombatState? combat) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = role,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = role == FleetRole.Military ? 21.0 : 13.5,
        SensorRange = role == FleetRole.Military ? 125.0f : 75.0f,
        IsActive = true,
        Combat = combat,
    };

    private static void RequireThrows(Action action, string message)
    {
        try
        {
            action();
        }
        catch (InvalidOperationException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
