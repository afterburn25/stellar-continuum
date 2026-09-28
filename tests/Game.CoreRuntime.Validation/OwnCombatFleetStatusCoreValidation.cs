using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Game.Simulation;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class OwnCombatFleetStatusCoreValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunOwnCombatFleetStatusCoreChecks()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_5245_4F57_4E53L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var system = galaxy.Systems[0];
        var owner = galaxy.Civilizations[0].Id;
        var rival = galaxy.Civilizations[1].Id;

        var own = CreatePatrol(8101, owner, "Core Status Sentinel", system.Id, system.Position);
        var foreign = CreatePatrol(8900, rival, "Hidden Core Rival", system.Id, system.Position);
        own.Combat!.Shields = 11.0;
        own.Combat.Armor = 28.0;
        own.Combat.Hull = 73.0;
        own.Combat.Order = MilitaryOrderType.Attack;
        own.Combat.TargetFleetId = foreign.Id;
        galaxy.Fleets.Add(own);
        galaxy.Fleets.Add(foreign);

        var before = Snapshot(galaxy);
        var coordinator = new GalaxySimulationStepCoordinator();
        var throughCore = coordinator.GetOwnCombatFleetStatus(galaxy, owner);
        var direct = OwnCombatFleetStatusBuilder.Build(galaxy, owner);

        Require(before == Snapshot(galaxy),
            "Core-owned Combat status access mutated authoritative fleet state");
        Require(throughCore.CivilizationId == owner && throughCore.Fleets.Count == 1,
            "Core-owned Combat status did not remain active-own scoped");
        Require(throughCore.Fleets[0].FleetId == own.Id &&
                throughCore.Fleets.All(status => status.FleetId != foreign.Id),
            "Core-owned Combat status leaked a foreign vessel");
        Require(throughCore.Fleets[0].HasAssignedAttackTarget &&
                typeof(OwnCombatFleetStatus).GetProperty("TargetFleetId") is null,
            "Core-owned Combat status exposed exact foreign Attack target identity");
        Require(Math.Abs(throughCore.Fleets[0].Shields - 11.0) < 0.000001 &&
                Math.Abs(throughCore.Fleets[0].Armor - 28.0) < 0.000001 &&
                Math.Abs(throughCore.Fleets[0].Hull - 73.0) < 0.000001,
            "Core-owned Combat status lost exact own durability");

        Require(throughCore.Summary == direct.Summary,
            "Core-owned Combat status summary diverged from the accepted builder");
        Require(throughCore.Fleets.SequenceEqual(direct.Fleets),
            "Core-owned Combat status detail diverged from the accepted builder");

        RequireThrows<InvalidOperationException>(
            () => coordinator.GetOwnCombatFleetStatus(galaxy, 999999),
            "Core-owned Combat status accepted an unknown civilization");

        Console.WriteLine("PASS: Core exposes exact-own non-mutating Combat fleet status");
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
        Combat = CombatProfileRegistry.CreateInitialState(
            CombatProfileIds.PatrolCorvetteMk1,
            FleetRole.Military),
    };

    private static string Snapshot(GalaxyState galaxy) =>
        JsonSerializer.Serialize(galaxy.Fleets
            .OrderBy(fleet => fleet.Id)
            .Select(fleet => new
            {
                fleet.Id,
                fleet.CivilizationId,
                fleet.IsActive,
                fleet.CurrentSystemId,
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
            }));

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
