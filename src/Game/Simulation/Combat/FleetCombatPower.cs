using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

public sealed record FleetPowerObservation(int ObserverId, int FleetId, double Power, double ObservedDay, string Evidence);

/// <summary>One display rating, calculated from real installed equipment and present damage; not an auto-resolve formula.</summary>
public static class FleetCombatPower
{
    public const int MaximumObservations = 4096;
    public const int MaximumObservationsPerObserver = 2048;
    public static double OwnPower(FleetState fleet)
    {
        if (!fleet.IsActive) return 0;
        var profile = CombatProfileRegistry.TryGet(fleet.Combat?.ProfileId ?? string.Empty, out var found)
            ? found : CombatProfileRegistry.Get(CombatProfileRegistry.DefaultProfileId(fleet.Role));
        var loadout = fleet.TacticalLoadout ?? MassiveCombatLoadouts.FromLegacy(profile);
        var maximum = profile.MaxShields + profile.MaxArmor + profile.MaxHull;
        var present = fleet.Combat is { } state ? state.Shields + state.Armor + state.Hull : maximum;
        return MassiveCombatPowerCalculator.PerShipPower(loadout) * Math.Clamp(present / Math.Max(1, maximum), 0, 1);
    }

    public static double? ObservedPower(GalaxyState galaxy, int observerId, FleetState target)
    {
        if (target.CivilizationId == observerId) return OwnPower(target);
        // An observation is a dated reading, never permission to fetch live hidden enemy state.
        return galaxy.CombatIntelligence.LastOrDefault(x => x.ObserverId == observerId && x.FleetId == target.Id)?.Power;
    }

    public static void Observe(GalaxyState galaxy, int observerId, FleetState target, double day, bool engaged, bool scanningCapability)
        => ObserveMany(galaxy, observerId, [target], day, engaged, scanningCapability);

    /// <summary>
    /// Records scanner readings only where an active observer vessel and foreign vessel have
    /// authoritative same-system presence. Owning scanner technology alone reveals nothing remote.
    /// </summary>
    public static int RecordSensorContacts(GalaxyState galaxy, int observerId, double day, bool scanningCapability)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (observerId < 0 || !double.IsFinite(day) || day < 0 || !galaxy.Civilizations.Any(x => x.Id == observerId))
            throw new ArgumentOutOfRangeException(nameof(observerId), "Scanner observation requires a valid campaign observer and time.");
        if (!scanningCapability) return 0;
        var occupiedSystems = galaxy.Fleets.Where(x => x.IsActive && x.CivilizationId == observerId && x.CurrentSystemId is not null)
            .Select(x => x.CurrentSystemId!.Value).ToHashSet();
        if (occupiedSystems.Count == 0) return 0;
        var visible = galaxy.Fleets.Where(x => x.IsActive && x.CivilizationId != observerId &&
                x.CurrentSystemId is int systemId && occupiedSystems.Contains(systemId))
            .OrderBy(x => x.Id).Take(MaximumObservationsPerObserver).ToArray();
        if (visible.Length == 0) return 0;
        ObserveMany(galaxy, observerId, visible, day, engaged: false, scanningCapability: true);
        return visible.Length;
    }

    public static void ObserveMany(GalaxyState galaxy, int observerId, IEnumerable<FleetState> targets, double day,
        bool engaged, bool scanningCapability)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(targets);
        if (observerId < 0 || !double.IsFinite(day) || day < 0)
            throw new ArgumentOutOfRangeException(nameof(day), "Combat intelligence requires valid campaign identities and time.");
        if (!engaged && !scanningCapability) return;
        ObserveMany(galaxy, observerId, targets, day, engaged, scanningCapability,
            galaxy.Fleets.ToDictionary(fleet => fleet.Id));
    }

    internal static void ObserveMany(GalaxyState galaxy, int observerId, IEnumerable<FleetState> targets, double day,
        bool engaged, bool scanningCapability, IReadOnlyDictionary<int, FleetState> fleetMap)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(targets);
        ArgumentNullException.ThrowIfNull(fleetMap);
        if (observerId < 0 || !double.IsFinite(day) || day < 0)
            throw new ArgumentOutOfRangeException(nameof(day), "Combat intelligence requires valid campaign identities and time.");
        if (!engaged && !scanningCapability) return;
        var observed = targets.OrderBy(x => x.Id).Take(MaximumObservationsPerObserver).ToArray();
        if (observed.Any(target => !fleetMap.TryGetValue(target.Id, out var member) || !ReferenceEquals(member, target)))
            throw new ArgumentOutOfRangeException(nameof(targets), "Combat intelligence target is not a campaign fleet.");
        var observedIds = observed.Select(x => x.Id).ToHashSet();
        var tacticalLoadoutPower = new Dictionary<MassiveCombatLoadout, float>(ReferenceEqualityComparer.Instance);
        var legacyProfilePower = new Dictionary<string, float>(StringComparer.Ordinal);
        galaxy.CombatIntelligence.RemoveAll(x => x.ObserverId == observerId && observedIds.Contains(x.FleetId));
        galaxy.CombatIntelligence.AddRange(observed.Select(target => new FleetPowerObservation(observerId, target.Id,
            OwnPower(target, tacticalLoadoutPower, legacyProfilePower), day, engaged ? "Engagement" : "Combat scanner")));
        var observerOverflow = galaxy.CombatIntelligence.Count(x => x.ObserverId == observerId) - MaximumObservationsPerObserver;
        if (observerOverflow > 0)
        {
            var evict = galaxy.CombatIntelligence.Where(x => x.ObserverId == observerId)
                .OrderBy(x => x.ObservedDay).ThenBy(x => x.FleetId).Take(observerOverflow).ToHashSet();
            galaxy.CombatIntelligence.RemoveAll(evict.Contains);
        }
        // Retain bounded intelligence, not every historic reading of every vessel.
        if (galaxy.CombatIntelligence.Count > MaximumObservations)
            galaxy.CombatIntelligence.RemoveRange(0, galaxy.CombatIntelligence.Count - MaximumObservations);
    }

    private static double OwnPower(FleetState fleet, IDictionary<MassiveCombatLoadout, float> tacticalLoadoutPower,
        IDictionary<string, float> legacyProfilePower)
    {
        if (!fleet.IsActive) return 0;
        var profile = CombatProfileRegistry.TryGet(fleet.Combat?.ProfileId ?? string.Empty, out var found)
            ? found : CombatProfileRegistry.Get(CombatProfileRegistry.DefaultProfileId(fleet.Role));
        var perShip = fleet.TacticalLoadout is { } loadout && tacticalLoadoutPower.TryGetValue(loadout, out var cachedLoadoutPower)
            ? cachedLoadoutPower
            : fleet.TacticalLoadout is null && legacyProfilePower.TryGetValue(profile.Id, out var cachedProfilePower)
                ? cachedProfilePower : float.NaN;
        if (float.IsNaN(perShip))
        {
            perShip = MassiveCombatPowerCalculator.PerShipPower(fleet.TacticalLoadout ?? MassiveCombatLoadouts.FromLegacy(profile));
            if (fleet.TacticalLoadout is not null) tacticalLoadoutPower[fleet.TacticalLoadout] = perShip;
            else legacyProfilePower[profile.Id] = perShip;
        }
        var maximum = profile.MaxShields + profile.MaxArmor + profile.MaxHull;
        var present = fleet.Combat is { } state ? state.Shields + state.Armor + state.Hull : maximum;
        return perShip * Math.Clamp(present / Math.Max(1, maximum), 0, 1);
    }
}
