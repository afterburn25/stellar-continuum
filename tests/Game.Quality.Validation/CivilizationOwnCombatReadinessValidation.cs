using System;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Quality.Validation;

internal static class CivilizationOwnCombatReadinessValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateExactOwnCombatStrength();
        Console.WriteLine("PASS: Civilization AI uses exact own Combat readiness strength");
    }

    private static void ValidateExactOwnCombatStrength()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4F57_4E43_4F4D_4241L,
            new GalaxyGenerationSettings
            {
                SystemCount = 28,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 360.0f,
            });

        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer);
        var foreign = galaxy.Civilizations.First(candidate => candidate.Id != civilization.Id);
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);

        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.CivilizationId == civilization.Id))
            fleet.IsActive = false;

        var nextId = galaxy.Fleets.Count == 0 ? 70000 : galaxy.Fleets.Max(fleet => fleet.Id) + 70000;
        var ownPatrol = CreatePatrol(nextId++, civilization.Id, home.Id, home.Position, "Own Readiness Patrol");
        var foreignPatrol = CreatePatrol(nextId++, foreign.Id, home.Id, home.Position, "Foreign Readiness Patrol");
        galaxy.Fleets.Add(ownPatrol);
        galaxy.Fleets.Add(foreignPatrol);

        var builder = new CivilizationStrategicInputBuilder();

        var healthyReadiness = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        var healthyOwnState = builder.Build(galaxy, civilization.Id);
        RequireNear(
            healthyOwnState.MilitaryStrength,
            Math.Max(1.0, healthyReadiness.CombatEffectiveArmedStrength),
            "healthy exact-own Combat strength did not reach Civilization AI");
        Require(healthyReadiness.CombatEffectiveArmedVessels == 1,
            "expected exactly one combat-effective owned patrol in test fixture");

        // Foreign-force changes must never alter exact own strategic strength.
        foreignPatrol.Combat!.Shields = 0.0;
        foreignPatrol.Combat.Armor = 0.0;
        foreignPatrol.Combat.Hull = 1.0;
        foreignPatrol.Combat.Order = MilitaryOrderType.Retreat;
        var afterForeignMutation = builder.Build(galaxy, civilization.Id);
        RequireNear(
            afterForeignMutation.MilitaryStrength,
            healthyOwnState.MilitaryStrength,
            "foreign fleet state leaked into Civilization own military strength");

        // Owned damage should reduce strength by exactly the amount Combat's own readiness model reports.
        ownPatrol.Combat!.Shields = Math.Max(0.0, ownPatrol.Combat.Shields - 18.0);
        ownPatrol.Combat.Armor = Math.Max(0.0, ownPatrol.Combat.Armor - 16.0);
        ownPatrol.Combat.Hull = Math.Max(1.0, ownPatrol.Combat.Hull - 22.0);
        var damagedReadiness = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        var damagedOwnState = builder.Build(galaxy, civilization.Id);
        RequireNear(
            damagedOwnState.MilitaryStrength,
            Math.Max(1.0, damagedReadiness.CombatEffectiveArmedStrength),
            "damaged exact-own Combat strength diverged from Civilization AI");
        Require(damagedOwnState.MilitaryStrength < healthyOwnState.MilitaryStrength,
            "owned combat damage did not reduce Civilization AI military strength");

        // A retreating vessel is still physically present, but it is not immediately combat-effective.
        ownPatrol.Combat.Order = MilitaryOrderType.Retreat;
        var retreatingReadiness = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        var retreatingOwnState = builder.Build(galaxy, civilization.Id);
        RequireNear(retreatingReadiness.CombatEffectiveArmedStrength, 0.0,
            "retreating patrol remained combat-effective in Combat readiness");
        RequireNear(retreatingOwnState.MilitaryStrength, 1.0,
            "Civilization AI did not fail down to its nonzero floor for a retreating-only force");

        // Disengagement in the current system has the same immediate-availability consequence.
        ownPatrol.Combat.Order = MilitaryOrderType.Hold;
        ownPatrol.Combat.IsDisengaged = true;
        ownPatrol.Combat.DisengagedSystemId = home.Id;
        var disengagedReadiness = CombatReadinessCalculator.Build(galaxy, civilization.Id);
        var disengagedOwnState = builder.Build(galaxy, civilization.Id);
        RequireNear(disengagedReadiness.CombatEffectiveArmedStrength, 0.0,
            "disengaged patrol remained combat-effective in Combat readiness");
        RequireNear(disengagedOwnState.MilitaryStrength, 1.0,
            "Civilization AI counted a disengaged-only force as immediately available strength");
    }

    private static FleetState CreatePatrol(
        int id,
        int civilizationId,
        int systemId,
        Vector2 position,
        string name) => new()
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
