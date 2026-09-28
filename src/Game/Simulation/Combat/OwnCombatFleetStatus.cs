using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// Non-mutating exact-own Combat state for one active physical vessel. This contract is suitable
/// for the owning civilization's UI/AI only. It intentionally does not expose foreign fleet state
/// or manufacture an Attack target identity.
/// </summary>
public sealed record OwnCombatFleetStatus(
    int FleetId,
    string FleetName,
    FleetRole Role,
    int? CurrentSystemId,
    string CombatProfileId,
    double Shields,
    double MaximumShields,
    double Armor,
    double MaximumArmor,
    double Hull,
    double MaximumHull,
    double WeaponCooldownRemainingDays,
    MilitaryOrderType CurrentOrder,
    bool HasAssignedAttackTarget,
    int? DefendSystemId,
    double RetreatProgressDays,
    double RetreatDelayDays,
    bool IsArmed,
    bool IsCombatEffective,
    bool IsDisengaged,
    double CurrentStrength,
    double MaximumStrength,
    double RepairDeficit)
{
    private const double Epsilon = 0.0000001;

    public double CurrentDurability => Shields + Armor + Hull;
    public double MaximumDurability => MaximumShields + MaximumArmor + MaximumHull;
    public double DurabilityRatio => MaximumDurability <= Epsilon
        ? 1.0
        : Math.Clamp(CurrentDurability / MaximumDurability, 0.0, 1.0);
    public double HullIntegrityRatio => MaximumHull <= Epsilon
        ? 1.0
        : Math.Clamp(Hull / MaximumHull, 0.0, 1.0);
    public bool IsDamaged => RepairDeficit > Epsilon;
    public bool HasHullDamage => Hull + Epsilon < MaximumHull;
    public bool IsRetreating => CurrentOrder == MilitaryOrderType.Retreat;
    public bool CanFireNow =>
        IsArmed && IsCombatEffective && WeaponCooldownRemainingDays <= Epsilon;
    public double RetreatProgressRatio => !IsRetreating || RetreatDelayDays <= Epsilon
        ? 0.0
        : Math.Clamp(RetreatProgressDays / RetreatDelayDays, 0.0, 1.0);
}

/// <summary>
/// Exact-own aggregate plus stable per-vessel Combat status. The aggregate is the canonical
/// CombatReadinessCalculator result so summary and vessel detail cannot evolve independently.
/// </summary>
public sealed record OwnCombatFleetStatusView(
    int CivilizationId,
    CombatReadinessSummary Summary,
    IReadOnlyList<OwnCombatFleetStatus> Fleets);

/// <summary>
/// Builds exact-own read-only vessel status without calling CombatProfileRegistry.EnsureState.
/// Missing/unknown Combat payloads are evaluated as pristine role defaults; malformed finite
/// values are clamped for presentation while the authoritative source object remains untouched.
/// </summary>
public static class OwnCombatFleetStatusBuilder
{
    public static OwnCombatFleetStatusView Build(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!galaxy.Civilizations.Any(civilization => civilization.Id == civilizationId))
            throw new InvalidOperationException($"Unknown civilization {civilizationId}.");

        var fleets = galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == civilizationId)
            .OrderBy(fleet => fleet.Id)
            .Select(BuildFleet)
            .ToArray();

        return new OwnCombatFleetStatusView(
            civilizationId,
            CombatReadinessCalculator.Build(galaxy, civilizationId),
            fleets);
    }

    private static OwnCombatFleetStatus BuildFleet(FleetState fleet)
    {
        var readiness = CombatReadinessCalculator.ReadFleet(fleet);
        var state = fleet.Combat;
        CombatProfileDefinition profile;
        var usesPersistedState = false;

        if (state is not null && CombatProfileRegistry.TryGet(state.ProfileId, out var resolvedProfile))
        {
            profile = resolvedProfile;
            usesPersistedState = true;
        }
        else
        {
            profile = CombatProfileRegistry.Get(CombatProfileRegistry.DefaultProfileId(fleet.Role));
        }

        var shields = usesPersistedState ? ClampFinite(state!.Shields, profile.MaxShields) : profile.MaxShields;
        var armor = usesPersistedState ? ClampFinite(state!.Armor, profile.MaxArmor) : profile.MaxArmor;
        var hull = usesPersistedState ? ClampFinite(state!.Hull, profile.MaxHull) : profile.MaxHull;
        var cooldown = usesPersistedState ? NonNegativeFinite(state!.WeaponCooldownRemainingDays) : 0.0;
        var order = usesPersistedState && Enum.IsDefined(state!.Order)
            ? state.Order
            : MilitaryOrderType.Hold;
        var retreatProgress = usesPersistedState
            ? Math.Min(NonNegativeFinite(state!.RetreatProgressDays), Math.Max(0.0, profile.RetreatDelayDays))
            : 0.0;
        var disengaged = usesPersistedState &&
                         state!.IsDisengaged &&
                         state.DisengagedSystemId == fleet.CurrentSystemId;

        return new OwnCombatFleetStatus(
            fleet.Id,
            fleet.Name,
            fleet.Role,
            fleet.CurrentSystemId,
            profile.Id,
            shields,
            profile.MaxShields,
            armor,
            profile.MaxArmor,
            hull,
            profile.MaxHull,
            cooldown,
            order,
            usesPersistedState && order == MilitaryOrderType.Attack && state!.TargetFleetId.HasValue,
            usesPersistedState && order == MilitaryOrderType.Defend ? state!.DefendSystemId : null,
            retreatProgress,
            Math.Max(0.0, profile.RetreatDelayDays),
            readiness.IsArmed,
            readiness.IsCombatEffective,
            disengaged,
            readiness.CurrentStrength,
            readiness.MaximumStrength,
            readiness.RepairDeficit);
    }

    private static double ClampFinite(double value, double maximum) =>
        Math.Clamp(double.IsFinite(value) ? value : 0.0, 0.0, Math.Max(0.0, maximum));

    private static double NonNegativeFinite(double value) =>
        double.IsFinite(value) ? Math.Max(0.0, value) : 0.0;
}
