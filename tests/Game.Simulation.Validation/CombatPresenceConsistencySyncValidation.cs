using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class CombatPresenceConsistencySyncValidation
{
    [Game.Validation.RegressionCheck]
    internal static void RunPresenceConsistencyCheck()
    {
        const double epsilon = 0.000001;

        var galaxy = new GalaxyGenerator().Generate(
            0x5052_4553_434F_4E53L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var owner = galaxy.Civilizations[0];
        var hostile = galaxy.Civilizations[1];
        var neutral = galaxy.Civilizations[2];
        var system = galaxy.Systems[0];

        var damaged = CreatePatrol(9100, owner.Id, "Consistency Damaged", system.Id, system.Position);
        damaged.Combat!.Shields = double.NaN;
        damaged.Combat.Armor = 20.0;
        damaged.Combat.Hull = 50.0;

        var invalid = CreatePatrol(9101, owner.Id, "Consistency Invalid Profile", system.Id, system.Position);
        invalid.Combat!.ProfileId = "invalid_profile_must_remain_stored";
        invalid.Combat.Shields = 0.0;
        invalid.Combat.Armor = 0.0;
        invalid.Combat.Hull = 0.0;

        var hostilePatrol = CreatePatrol(9200, hostile.Id, "Consistency Hostile", system.Id, system.Position);
        var hostileRetreat = CreatePatrol(9201, hostile.Id, "Consistency Retreat", system.Id, system.Position);
        hostileRetreat.Combat!.Order = MilitaryOrderType.Retreat;

        var neutralPatrol = CreatePatrol(9300, neutral.Id, "Consistency Neutral", system.Id, system.Position);
        galaxy.Fleets.Add(damaged);
        galaxy.Fleets.Add(invalid);
        galaxy.Fleets.Add(hostilePatrol);
        galaxy.Fleets.Add(hostileRetreat);
        galaxy.Fleets.Add(neutralPatrol);

        var hostility = new DelegateCombatHostilityView((first, second) =>
            (first == owner.Id && second == hostile.Id) ||
            (first == hostile.Id && second == owner.Id));

        var compatibility = CombatSystemPresenceCalculator.Assess(
            galaxy,
            hostility,
            owner.Id,
            system.Id);
        var controlView = new AuthoritativeSystemMilitaryControlView(hostility);
        var control = controlView.Build(galaxy, system.Id);
        var exposure = controlView.AssessInterdiction(galaxy, system.Id, owner.Id);

        var ownerPresence = control.Presences.Single(presence => presence.CivilizationId == owner.Id);
        var hostilePresence = control.Presences.Single(presence => presence.CivilizationId == hostile.Id);
        var neutralPresence = control.Presences.Single(presence => presence.CivilizationId == neutral.Id);

        Require(compatibility.OwnArmedVessels == ownerPresence.CombatEffectiveArmedVessels,
            "compatibility presence and system control disagree on own combat-effective vessel count");
        Require(compatibility.HostileArmedVessels == exposure.HostileCombatEffectiveArmedVessels,
            "compatibility presence and interdiction disagree on hostile combat-effective vessel count");
        Require(compatibility.NonHostileForeignArmedVessels == neutralPresence.CombatEffectiveArmedVessels,
            "compatibility presence and system control disagree on neutral effective presence");
        Require(Math.Abs(compatibility.OwnCurrentStrength - exposure.OwnCombatEffectiveArmedStrength) <= epsilon,
            "compatibility presence and interdiction disagree on own effective strength");
        Require(Math.Abs(compatibility.HostileCurrentStrength - exposure.HostileCombatEffectiveArmedStrength) <= epsilon,
            "compatibility presence and interdiction disagree on hostile effective strength");
        Require(compatibility.Posture == SystemMilitaryPosture.Contested && exposure.IsThreatened && control.IsContested,
            "symmetric hostile co-presence did not agree across Combat presence contracts");

        Require(hostilePresence.ActiveArmedVessels == 2 && hostilePresence.CombatEffectiveArmedVessels == 1 &&
                hostilePresence.RetreatingArmedVessels == 1,
            "system control lost physical/effective distinction for the retreating hostile hull");
        Require(double.IsFinite(compatibility.OwnCurrentStrength) && compatibility.OwnCurrentStrength > 0.0,
            "non-finite persisted damage escaped the canonical non-mutating readiness evaluator");

        // Unknown Combat profiles follow the same effective pristine role-default fallback used
        // by readiness without rewriting the stored invalid payload.
        Require(ownerPresence.CombatEffectiveArmedVessels == 2,
            "invalid-profile own military vessel did not receive consistent effective fallback semantics");
        Require(invalid.Combat!.ProfileId == "invalid_profile_must_remain_stored" &&
                invalid.Combat.Shields == 0.0 && invalid.Combat.Armor == 0.0 && invalid.Combat.Hull == 0.0,
            "presence read rewrote invalid persisted Combat state");
        Require(double.IsNaN(damaged.Combat!.Shields),
            "presence read normalized non-finite source state in-place instead of remaining read-only");

        hostilePatrol.Combat!.Order = MilitaryOrderType.Retreat;
        var compatibilityAfterRetreat = CombatSystemPresenceCalculator.Assess(
            galaxy,
            hostility,
            owner.Id,
            system.Id);
        var exposureAfterRetreat = controlView.AssessInterdiction(galaxy, system.Id, owner.Id);
        Require(!compatibilityAfterRetreat.HasHostileInterdiction &&
                compatibilityAfterRetreat.HostileArmedVessels == 0 &&
                !exposureAfterRetreat.IsThreatened &&
                exposureAfterRetreat.HostileCombatEffectiveArmedVessels == 0,
            "retreat did not remove hostile effective presence consistently across both contracts");

        Console.WriteLine("PASS: Combat system presence contracts share canonical vessel evaluation");
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

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
