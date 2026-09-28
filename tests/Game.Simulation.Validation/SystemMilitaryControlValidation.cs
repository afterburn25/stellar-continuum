using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class SystemMilitaryControlValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunSystemMilitaryControlCheck()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4D49_4C43_5452_4F4CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var first = galaxy.Civilizations[0];
        var second = galaxy.Civilizations[1];
        var neutral = galaxy.Civilizations[2];
        var scoutOwner = galaxy.Civilizations[3];
        var system = galaxy.Systems[0];
        var remoteSystem = galaxy.Systems[1];

        var firstPatrol = CreateFleet(8100, first.Id, "Control First Patrol", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        var firstDamaged = CreateFleet(8101, first.Id, "Control Damaged Patrol", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        firstDamaged.Combat!.Shields = 5.0;
        firstDamaged.Combat.Armor = 20.0;
        firstDamaged.Combat.Hull = 50.0;

        var hostilePatrol = CreateFleet(8200, second.Id, "Control Hostile Patrol", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        var hostileRetreating = CreateFleet(8201, second.Id, "Control Hostile Retreat", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        hostileRetreating.Combat!.Order = MilitaryOrderType.Retreat;

        var neutralPatrol = CreateFleet(8300, neutral.Id, "Control Neutral Patrol", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        var unarmedScout = CreateFleet(8400, scoutOwner.Id, "Control Unarmed Scout", FleetRole.Scout, system.Id, system.Position, CombatProfileIds.CivilianLight);
        var remotePatrol = CreateFleet(8202, second.Id, "Control Remote Patrol", FleetRole.Military, remoteSystem.Id, remoteSystem.Position, CombatProfileIds.PatrolCorvetteMk1);
        var inactivePatrol = CreateFleet(8102, first.Id, "Control Inactive Patrol", FleetRole.Military, system.Id, system.Position, CombatProfileIds.PatrolCorvetteMk1);
        inactivePatrol.IsActive = false;

        galaxy.Fleets.Add(firstPatrol);
        galaxy.Fleets.Add(firstDamaged);
        galaxy.Fleets.Add(hostilePatrol);
        galaxy.Fleets.Add(hostileRetreating);
        galaxy.Fleets.Add(neutralPatrol);
        galaxy.Fleets.Add(unarmedScout);
        galaxy.Fleets.Add(remotePatrol);
        galaxy.Fleets.Add(inactivePatrol);

        var hostilityCalls = 0;
        var hostility = new DelegateCombatHostilityView((a, b) =>
        {
            hostilityCalls++;
            return (a == first.Id && b == second.Id) ||
                   (a == second.Id && b == first.Id);
        });
        var view = new AuthoritativeSystemMilitaryControlView(hostility);

        var retreatBefore = hostileRetreating.Combat!.Order;
        var control = view.Build(galaxy, system.Id);

        Require(control.State == SystemMilitaryControlState.Contested,
            "hostile combat-effective co-presence did not mark the system contested");
        Require(control.IsContested && control.HostileEffectivePairCount == 1,
            "system contest did not contain exactly the known hostile effective pair");
        Require(control.SoleControllerCivilizationId is null,
            "contested system invented a sole military controller");
        Require(control.Presences.Select(presence => presence.CivilizationId)
                .SequenceEqual(new[] { first.Id, second.Id, neutral.Id }.OrderBy(id => id)),
            "military presence included an unarmed/inactive/remote force or lost deterministic ordering");

        var firstPresence = control.Presences.Single(presence => presence.CivilizationId == first.Id);
        var secondPresence = control.Presences.Single(presence => presence.CivilizationId == second.Id);
        var neutralPresence = control.Presences.Single(presence => presence.CivilizationId == neutral.Id);
        Require(firstPresence.ActiveArmedVessels == 2 && firstPresence.CombatEffectiveArmedVessels == 2,
            "first civilization military presence count changed");
        Require(secondPresence.ActiveArmedVessels == 2 && secondPresence.CombatEffectiveArmedVessels == 1 &&
                secondPresence.RetreatingArmedVessels == 1,
            "retreating hostile vessel incorrectly projected combat-effective control");
        Require(neutralPresence.ActiveArmedVessels == 1 && neutralPresence.CombatEffectiveArmedVessels == 1,
            "neutral armed presence changed");
        Require(control.TotalCombatEffectiveArmedVessels == 4,
            "system effective armed-vessel total changed");
        Require(hostileRetreating.Combat!.Order == retreatBefore,
            "system-control read mutated authoritative retreat state");

        var firstExposure = view.AssessInterdiction(galaxy, system.Id, first.Id);
        Require(firstExposure.IsThreatened && firstExposure.HostileCivilizationCount == 1 &&
                firstExposure.HostileCombatEffectiveArmedVessels == 1,
            "first civilization interdiction exposure did not isolate the effective hostile force");
        Require(firstExposure.OwnCombatEffectiveArmedVessels == 2,
            "interdiction assessment lost own effective force");
        Require(firstExposure.HostileCombatEffectiveArmedStrength > 0.0 &&
                firstExposure.OwnCombatEffectiveArmedStrength > 0.0,
            "interdiction assessment lost effective strength");

        var neutralExposure = view.AssessInterdiction(galaxy, system.Id, neutral.Id);
        Require(!neutralExposure.IsThreatened && neutralExposure.HostileCivilizationCount == 0 &&
                neutralExposure.HostileCombatEffectiveArmedVessels == 0,
            "neutral co-presence was incorrectly treated as hostile interdiction");

        // A political opponent that is physically retreating/disengaged no longer projects
        // effective system control, even though its armed hull still physically exists there.
        hostilePatrol.Combat!.Order = MilitaryOrderType.Retreat;
        neutralPatrol.Combat!.IsDisengaged = true;
        neutralPatrol.Combat.DisengagedSystemId = system.Id;

        var unopposed = view.Build(galaxy, system.Id);
        Require(unopposed.State == SystemMilitaryControlState.UnopposedEffectiveControl &&
                unopposed.SoleControllerCivilizationId == first.Id,
            "sole combat-effective force did not receive unopposed system control");
        Require(!unopposed.IsContested && unopposed.HostileEffectivePairCount == 0,
            "retreating hostile force kept the system falsely contested");
        Require(unopposed.Presences.Single(presence => presence.CivilizationId == second.Id).CombatEffectiveArmedVessels == 0,
            "retreating hostile force still projected effective military presence");
        Require(unopposed.Presences.Single(presence => presence.CivilizationId == neutral.Id).DisengagedArmedVessels == 1,
            "disengaged neutral force was not represented as physically present but ineffective");

        var noThreat = view.AssessInterdiction(galaxy, system.Id, first.Id);
        Require(!noThreat.IsThreatened && noThreat.HostileCombatEffectiveArmedVessels == 0,
            "retreating enemy still created active interdiction exposure");

        firstPatrol.Combat!.Order = MilitaryOrderType.Retreat;
        firstDamaged.Combat!.Order = MilitaryOrderType.Retreat;
        var noEffective = view.Build(galaxy, system.Id);
        Require(noEffective.State == SystemMilitaryControlState.NoEffectiveArmedPresence &&
                noEffective.SoleControllerCivilizationId is null &&
                noEffective.TotalCombatEffectiveArmedVessels == 0,
            "system with only retreating/disengaged armed hulls retained effective military control");

        var remote = view.Build(galaxy, remoteSystem.Id);
        Require(remote.State == SystemMilitaryControlState.UnopposedEffectiveControl &&
                remote.SoleControllerCivilizationId == second.Id,
            "remote physical military presence was not isolated to its actual system");

        RequireThrows(() => view.Build(galaxy, int.MaxValue),
            "unknown system military-control request was not rejected");
        RequireThrows(() => view.AssessInterdiction(galaxy, system.Id, int.MaxValue),
            "unknown civilization interdiction request was not rejected");

        Require(hostilityCalls > 0,
            "system-control validation did not exercise the political hostility boundary");
        Console.WriteLine("PASS: authoritative system military control and interdiction exposure");
    }

    private static FleetState CreateFleet(
        int id,
        int civilizationId,
        string name,
        FleetRole role,
        int systemId,
        Vector2 position,
        string profileId) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = role,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = role == FleetRole.Military ? 21.0 : 18.0,
        SensorRange = role == FleetRole.Military ? 125.0f : 80.0f,
        IsActive = true,
        Combat = CombatProfileRegistry.CreateInitialState(profileId, role),
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

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
