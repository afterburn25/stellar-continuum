using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// Authoritative physical military-presence summary for subsystem integration.
/// This is not a fair-information/player-intelligence view: presentation code must not
/// use it to reveal exact hidden foreign force strength.
/// </summary>
public sealed record SystemMilitaryPresenceSummary(
    int CivilizationId,
    int SystemId,
    SystemMilitaryPosture Posture,
    int OwnArmedVessels,
    int HostileArmedVessels,
    int NonHostileForeignArmedVessels,
    int HostileCivilizations,
    double OwnCurrentStrength,
    double HostileCurrentStrength)
{
    public bool HasHostileInterdiction => HostileArmedVessels > 0;
    public bool IsContested => OwnArmedVessels > 0 && HostileArmedVessels > 0;
}

public enum SystemMilitaryPosture
{
    Clear,
    Secured,
    Interdicted,
    Contested,
}

/// <summary>
/// Reconstructible, non-persisted calculation of combat-effective armed presence in one system.
/// Retreating/disengaged/destroyed vessels do not exert military control. Political hostility
/// is consulted once per represented foreign civilization rather than once per vessel.
///
/// Vessel readiness/strength is delegated to CombatReadinessCalculator's assembly-local evaluator
/// so this compatibility surface cannot drift from exact-own readiness or the newer authoritative
/// system-control view on invalid profiles, non-finite damage, retreat, or disengagement.
/// </summary>
public static class CombatSystemPresenceCalculator
{
    public static SystemMilitaryPresenceSummary Assess(
        GalaxyState galaxy,
        ICombatHostilityView hostilityView,
        int civilizationId,
        int systemId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(hostilityView);

        if (!galaxy.Civilizations.Any(civilization => civilization.Id == civilizationId))
            throw new InvalidOperationException($"Unknown civilization {civilizationId}.");
        if (!galaxy.Systems.Any(system => system.Id == systemId))
            throw new InvalidOperationException($"Unknown system {systemId}.");

        var groups = galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CurrentSystemId == systemId)
            .Select(TryBuildPresence)
            .Where(presence => presence is not null)
            .Select(presence => presence!)
            .GroupBy(presence => presence.CivilizationId)
            .OrderBy(group => group.Key)
            .ToArray();

        var ownCount = 0;
        var hostileCount = 0;
        var nonHostileForeignCount = 0;
        var hostileCivilizations = 0;
        var ownStrength = 0.0;
        var hostileStrength = 0.0;

        foreach (var group in groups)
        {
            var count = group.Count();
            var strength = group.Sum(presence => presence.CurrentStrength);

            if (group.Key == civilizationId)
            {
                ownCount += count;
                ownStrength += strength;
                continue;
            }

            // This compatibility summary is directional: a foreign civilization is an
            // interdictor only when it is permitted to initiate hostile Combat against the
            // assessed civilization. The newer system-control view separately exposes the
            // broader any-direction contested-system concept.
            if (hostilityView.AreHostile(group.Key, civilizationId))
            {
                hostileCivilizations++;
                hostileCount += count;
                hostileStrength += strength;
            }
            else
            {
                nonHostileForeignCount += count;
            }
        }

        var posture = (ownCount > 0, hostileCount > 0) switch
        {
            (false, false) => SystemMilitaryPosture.Clear,
            (true, false) => SystemMilitaryPosture.Secured,
            (false, true) => SystemMilitaryPosture.Interdicted,
            _ => SystemMilitaryPosture.Contested,
        };

        return new SystemMilitaryPresenceSummary(
            civilizationId,
            systemId,
            posture,
            ownCount,
            hostileCount,
            nonHostileForeignCount,
            hostileCivilizations,
            ownStrength,
            hostileStrength);
    }

    private static VesselPresence? TryBuildPresence(FleetState fleet)
    {
        var readiness = CombatReadinessCalculator.ReadFleet(fleet);
        if (!readiness.IsArmed || !readiness.IsCombatEffective)
            return null;

        return new VesselPresence(fleet.CivilizationId, readiness.CurrentStrength);
    }

    private sealed record VesselPresence(int CivilizationId, double CurrentStrength);
}
