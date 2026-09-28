using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// Read-only validation result for a proposed military order. The preview intentionally mirrors
/// CombatSimulation.IssueOrder acceptance rules without applying order state, normalizing legacy
/// Combat payloads, clearing disengagement, or otherwise mutating authoritative simulation state.
/// </summary>
public sealed record CombatOrderPreview(
    int CivilizationId,
    int FleetId,
    MilitaryOrder Order,
    bool Accepted,
    string Message);

/// <summary>
/// Non-mutating military-order validator for UI/AI planning. Political permission remains supplied
/// by the same Diplomacy-owned ICombatHostilityView used by CombatSimulation.
/// </summary>
public sealed class CombatOrderPreviewService
{
    private readonly ICombatHostilityView _hostilityView;

    public CombatOrderPreviewService(ICombatHostilityView? hostilityView = null)
    {
        _hostilityView = hostilityView ?? PeacefulCombatHostilityView.Instance;
    }

    public CombatOrderPreview Preview(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(order);

        var fleet = galaxy.Fleets.FirstOrDefault(candidate =>
            candidate.Id == fleetId &&
            candidate.CivilizationId == civilizationId &&
            candidate.IsActive);
        if (fleet is null)
            return Rejected(civilizationId, fleetId, order, "No active fleet with that identity belongs to the civilization.");

        var profile = ReadProfile(fleet);
        switch (order.Type)
        {
            case MilitaryOrderType.Hold:
                return Accepted(civilizationId, fleetId, order, $"{fleet.Name} can hold position.");

            case MilitaryOrderType.Defend:
            {
                if (!profile.HasWeapon)
                    return Rejected(civilizationId, fleetId, order, $"{fleet.Name} has no combat-capable weapon system.");

                var systemId = order.DefendSystemId ?? fleet.CurrentSystemId;
                if (systemId is null ||
                    fleet.CurrentSystemId != systemId ||
                    !galaxy.Systems.Any(system => system.Id == systemId.Value))
                {
                    return Rejected(
                        civilizationId,
                        fleetId,
                        order,
                        "A defend order currently requires the fleet to be present in the defended system.");
                }

                return Accepted(civilizationId, fleetId, order, $"{fleet.Name} can defend system {systemId.Value}.");
            }

            case MilitaryOrderType.Attack:
            {
                if (!profile.HasWeapon)
                    return Rejected(civilizationId, fleetId, order, $"{fleet.Name} has no combat-capable weapon system.");
                if (order.TargetFleetId is null)
                    return Rejected(civilizationId, fleetId, order, "An attack order requires a target fleet.");

                var target = galaxy.Fleets.FirstOrDefault(candidate =>
                    candidate.Id == order.TargetFleetId.Value && candidate.IsActive);
                if (target is null || target.CivilizationId == civilizationId)
                    return Rejected(civilizationId, fleetId, order, "The requested target is not a valid hostile fleet.");
                if (fleet.CurrentSystemId is null || fleet.CurrentSystemId != target.CurrentSystemId)
                    return Rejected(civilizationId, fleetId, order, "The target must be in the same system before combat can begin.");

                if (IsDisengagedHere(target, fleet.CurrentSystemId.Value))
                {
                    return Rejected(
                        civilizationId,
                        fleetId,
                        order,
                        "The target has tactically disengaged from this system-level engagement.");
                }

                if (!_hostilityView.AreHostile(civilizationId, target.CivilizationId))
                {
                    return Rejected(
                        civilizationId,
                        fleetId,
                        order,
                        "Diplomatic/political state does not currently permit a hostile engagement.");
                }

                return Accepted(civilizationId, fleetId, order, $"{fleet.Name} can attack {target.Name}.");
            }

            case MilitaryOrderType.Retreat:
                return Accepted(civilizationId, fleetId, order, $"{fleet.Name} can attempt to disengage.");

            default:
                return Rejected(civilizationId, fleetId, order, "Unknown military order.");
        }
    }

    private static CombatProfileDefinition ReadProfile(FleetState fleet)
    {
        var profileId = fleet.Combat?.ProfileId;
        return !string.IsNullOrWhiteSpace(profileId) && CombatProfileRegistry.TryGet(profileId, out var profile)
            ? profile
            : CombatProfileRegistry.Get(CombatProfileRegistry.DefaultProfileId(fleet.Role));
    }

    private static bool IsDisengagedHere(FleetState fleet, int systemId)
    {
        var state = fleet.Combat;
        if (state is null || !CombatProfileRegistry.TryGet(state.ProfileId, out _))
            return false;

        return state.IsDisengaged && state.DisengagedSystemId == systemId;
    }

    private static CombatOrderPreview Accepted(
        int civilizationId,
        int fleetId,
        MilitaryOrder order,
        string message) =>
        new(civilizationId, fleetId, order, true, message);

    private static CombatOrderPreview Rejected(
        int civilizationId,
        int fleetId,
        MilitaryOrder order,
        string message) =>
        new(civilizationId, fleetId, order, false, message);
}
