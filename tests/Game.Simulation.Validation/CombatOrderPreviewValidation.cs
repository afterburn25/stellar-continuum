using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class CombatOrderPreviewValidation
{
    private static readonly JsonSerializerOptions SnapshotOptions = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };

    [Game.Validation.RegressionCheck]
    internal static void ValidateOrderPreviewParityAndNonMutation()
    {
        ValidateCase("hostile attack", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, true);
        });
        ValidateCase("peace denies attack", fixture =>
        {
            fixture.Hostile = false;
            var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, false);
        });
        ValidateCase("unarmed combat profile attack", fixture =>
        {
            fixture.Attacker.Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.CivilianLight, fixture.Attacker.Role);
            var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, false);
        });
        ValidateCase("off-system attack", fixture =>
        {
            var remote = fixture.Galaxy.Systems[1];
            fixture.Target.CurrentSystemId = remote.Id;
            fixture.Target.Position = remote.Position;
            var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, false);
        });
        ValidateCase("disengaged target attack", fixture =>
        {
            fixture.Target.Combat!.IsDisengaged = true;
            fixture.Target.Combat.DisengagedSystemId = fixture.Target.CurrentSystemId;
            var order = new MilitaryOrder(MilitaryOrderType.Attack, fixture.Target.Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, false);
        });
        ValidateCase("valid defend", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Defend, DefendSystemId: fixture.Attacker.CurrentSystemId);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, true);
        });
        ValidateCase("invalid remote defend", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Defend, DefendSystemId: fixture.Galaxy.Systems[1].Id);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, false);
        });
        ValidateCase("hold", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Hold);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, true);
        });
        ValidateCase("retreat", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Retreat);
            return (fixture.Attacker.CivilizationId, fixture.Attacker.Id, order, true);
        });
        ValidateCase("unknown owned fleet", fixture =>
        {
            var order = new MilitaryOrder(MilitaryOrderType.Hold);
            return (fixture.Attacker.CivilizationId, 999999, order, false);
        });

        // Legacy/malformed state is the most important non-mutation case: preview may reason from
        // the role-safe default but must not normalize the persisted payload merely to answer UI.
        var legacy = CreateFixture();
        legacy.Attacker.Combat = new FleetCombatState
        {
            ProfileId = "unknown_legacy_profile",
            Shields = double.NaN,
            Armor = -50.0,
            Hull = double.PositiveInfinity,
            Order = MilitaryOrderType.Attack,
            TargetFleetId = legacy.Target.Id,
        };
        var beforeLegacy = Snapshot(legacy.Galaxy);
        var legacyPreview = CreatePreview(legacy).Preview(
            legacy.Galaxy,
            legacy.Attacker.CivilizationId,
            legacy.Attacker.Id,
            new MilitaryOrder(MilitaryOrderType.Hold));
        Require(legacyPreview.Accepted, "legacy role-safe Hold preview was unexpectedly rejected");
        Require(beforeLegacy == Snapshot(legacy.Galaxy),
            "preview normalized or otherwise mutated malformed legacy Combat state");

        Console.WriteLine("PASS: Combat order preview parity and non-mutation");
    }

    private static void ValidateCase(
        string name,
        Func<Fixture, (int CivilizationId, int FleetId, MilitaryOrder Order, bool Expected)> configure)
    {
        var fixture = CreateFixture();
        var scenario = configure(fixture);
        var before = Snapshot(fixture.Galaxy);
        var preview = CreatePreview(fixture).Preview(
            fixture.Galaxy,
            scenario.CivilizationId,
            scenario.FleetId,
            scenario.Order);

        Require(preview.Accepted == scenario.Expected,
            $"{name}: preview acceptance {preview.Accepted} did not match expected {scenario.Expected}");
        Require(before == Snapshot(fixture.Galaxy),
            $"{name}: preview mutated authoritative galaxy/fleet state");

        var issuance = CreateCombat(fixture).IssueOrder(
            fixture.Galaxy,
            scenario.CivilizationId,
            scenario.FleetId,
            scenario.Order);
        Require(issuance.Accepted == preview.Accepted,
            $"{name}: preview acceptance diverged from authoritative IssueOrder acceptance");
    }

    private static CombatOrderPreviewService CreatePreview(Fixture fixture) =>
        new(new DelegateCombatHostilityView((first, second) =>
            fixture.Hostile &&
            first == fixture.Attacker.CivilizationId &&
            second == fixture.Target.CivilizationId));

    private static CombatSimulation CreateCombat(Fixture fixture) =>
        new(new DelegateCombatHostilityView((first, second) =>
            fixture.Hostile &&
            first == fixture.Attacker.CivilizationId &&
            second == fixture.Target.CivilizationId));

    private static Fixture CreateFixture()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x5052_4556_4945_574CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var system = galaxy.Systems[0];
        var attackerCivilization = galaxy.Civilizations[0];
        var targetCivilization = galaxy.Civilizations[1];
        var attacker = CreatePatrol(9100, attackerCivilization.Id, "Preview Sentinel", system.Id, system.Position);
        var target = CreatePatrol(9200, targetCivilization.Id, "Preview Rival", system.Id, system.Position);
        galaxy.Fleets.Add(attacker);
        galaxy.Fleets.Add(target);
        return new Fixture(galaxy, attacker, target);
    }

    private static FleetState CreatePatrol(
        int id,
        int civilizationId,
        string name,
        int systemId,
        Vector2 position) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = FleetRole.Military,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 21.0,
        SensorRange = 125.0f,
        IsActive = true,
        Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military),
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

    private sealed class Fixture(
        GalaxyState galaxy,
        FleetState attacker,
        FleetState target)
    {
        public GalaxyState Galaxy { get; } = galaxy;
        public FleetState Attacker { get; } = attacker;
        public FleetState Target { get; } = target;
        public bool Hostile { get; set; } = true;
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
