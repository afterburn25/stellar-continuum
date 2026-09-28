using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// Deterministic early-release combat resolution on the existing strategic fleet/vessel model.
/// Target searches are limited to explicit attacks and defenders in the same system; there is
/// no global per-frame all-versus-all scan and no projectile entity simulation.
/// </summary>
public sealed class CombatSimulation
{
    private const double Epsilon = 0.0000001;

    private readonly ICombatHostilityView _hostilityView;
    private HashSet<EngagementKey> _activeEngagements = new();

    public CombatSimulation(ICombatHostilityView? hostilityView = null)
    {
        _hostilityView = hostilityView ?? PeacefulCombatHostilityView.Instance;
    }

    public CombatOrderResult IssueOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order)
    {
        var fleet = galaxy.Fleets.FirstOrDefault(candidate =>
            candidate.Id == fleetId &&
            candidate.CivilizationId == civilizationId &&
            candidate.IsActive);
        if (fleet is null)
            return new CombatOrderResult(false, "No active fleet with that identity belongs to the civilization.");

        var state = CombatProfileRegistry.EnsureState(fleet);
        var profile = CombatProfileRegistry.Get(state.ProfileId);

        switch (order.Type)
        {
            case MilitaryOrderType.Hold:
                SetHold(state, preserveDisengagement: true);
                return new CombatOrderResult(true, $"{fleet.Name} is holding position.");

            case MilitaryOrderType.Defend:
            {
                if (!profile.HasWeapon)
                    return new CombatOrderResult(false, $"{fleet.Name} has no combat-capable weapon system.");

                var systemId = order.DefendSystemId ?? fleet.CurrentSystemId;
                if (systemId is null || fleet.CurrentSystemId != systemId || !galaxy.Systems.Any(system => system.Id == systemId.Value))
                    return new CombatOrderResult(false, "A defend order currently requires the fleet to be present in the defended system.");

                state.Order = MilitaryOrderType.Defend;
                state.TargetFleetId = null;
                state.DefendSystemId = systemId;
                state.RetreatProgressDays = 0.0;
                state.RetreatStarted = false;
                state.IsDisengaged = false;
                state.DisengagedSystemId = null;
                return new CombatOrderResult(true, $"{fleet.Name} is defending system {systemId.Value}.");
            }

            case MilitaryOrderType.Attack:
            {
                if (!profile.HasWeapon)
                    return new CombatOrderResult(false, $"{fleet.Name} has no combat-capable weapon system.");
                if (order.TargetFleetId is null)
                    return new CombatOrderResult(false, "An attack order requires a target fleet.");

                var target = galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == order.TargetFleetId.Value && candidate.IsActive);
                if (target is null || target.CivilizationId == civilizationId)
                    return new CombatOrderResult(false, "The requested target is not a valid hostile fleet.");
                if (fleet.CurrentSystemId is null || fleet.CurrentSystemId != target.CurrentSystemId)
                    return new CombatOrderResult(false, "The target must be in the same system before combat can begin.");

                var targetState = CombatProfileRegistry.EnsureState(target);
                if (targetState.IsDisengaged && targetState.DisengagedSystemId == fleet.CurrentSystemId)
                    return new CombatOrderResult(false, "The target has tactically disengaged from this system-level engagement.");
                if (!_hostilityView.AreHostile(civilizationId, target.CivilizationId))
                    return new CombatOrderResult(false, "Diplomatic/political state does not currently permit a hostile engagement.");

                state.Order = MilitaryOrderType.Attack;
                state.TargetFleetId = target.Id;
                state.DefendSystemId = null;
                state.RetreatProgressDays = 0.0;
                state.RetreatStarted = false;
                state.IsDisengaged = false;
                state.DisengagedSystemId = null;
                return new CombatOrderResult(true, $"{fleet.Name} is attacking {target.Name}.");
            }

            case MilitaryOrderType.Retreat:
                state.Order = MilitaryOrderType.Retreat;
                state.TargetFleetId = null;
                state.DefendSystemId = null;
                state.RetreatProgressDays = 0.0;
                state.RetreatStarted = false;
                return new CombatOrderResult(true, $"{fleet.Name} is attempting to disengage.");

            default:
                return new CombatOrderResult(false, "Unknown military order.");
        }
    }

    public IReadOnlyList<CombatEvent> Advance(GalaxyState galaxy, double simulationDeltaDays)
    {
        if (simulationDeltaDays <= 0.0)
            return Array.Empty<CombatEvent>();

        var events = new List<CombatEvent>();
        var activeById = galaxy.Fleets
            .Where(fleet => fleet.IsActive)
            .OrderBy(fleet => fleet.Id)
            .ToDictionary(fleet => fleet.Id);

        foreach (var fleet in activeById.Values)
            CombatProfileRegistry.EnsureState(fleet);

        var targetMap = BuildTargetMap(galaxy, activeById);
        var engagementSet = BuildEngagementSet(targetMap);
        EmitNewEngagements(galaxy, targetMap, engagementSet, events);

        var fireActions = BuildFireActions(activeById, targetMap, simulationDeltaDays);
        ApplyFireActions(galaxy, fireActions, events);
        ProcessRetreats(galaxy, targetMap, simulationDeltaDays, events);

        var survivingById = galaxy.Fleets
            .Where(fleet => fleet.IsActive)
            .OrderBy(fleet => fleet.Id)
            .ToDictionary(fleet => fleet.Id);
        var remainingTargets = BuildTargetMap(galaxy, survivingById);
        var remainingEngagements = BuildEngagementSet(remainingTargets);
        EmitEndedEngagements(galaxy, remainingEngagements, events);
        _activeEngagements = remainingEngagements;

        return events;
    }

    public MilitaryForceSummary GetOwnMilitaryForceSummary(GalaxyState galaxy, int civilizationId)
    {
        var count = 0;
        var current = 0.0;
        var maximum = 0.0;

        foreach (var fleet in galaxy.Fleets.Where(candidate => candidate.IsActive && candidate.CivilizationId == civilizationId))
        {
            var state = CombatProfileRegistry.EnsureState(fleet);
            var profile = CombatProfileRegistry.Get(state.ProfileId);
            if (!profile.HasWeapon)
                continue;

            count++;
            var currentDurability = state.Shields + state.Armor + state.Hull;
            var maximumDurability = profile.MaxShields + profile.MaxArmor + profile.MaxHull;
            var hullReadiness = profile.MaxHull <= Epsilon ? 0.0 : Math.Clamp(state.Hull / profile.MaxHull, 0.0, 1.0);
            var maximumOffense = profile.SustainedDamagePerDay * 3.0;
            current += currentDurability + maximumOffense * hullReadiness;
            maximum += maximumDurability + maximumOffense;
        }

        return new MilitaryForceSummary(civilizationId, count, current, maximum);
    }

    private Dictionary<int, int> BuildTargetMap(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, FleetState> activeById)
    {
        var targets = new Dictionary<int, int>();

        // Explicit attack orders are the only source of aggression. Invalid or stale
        // attacks are cleared instead of searching the galaxy for a replacement target.
        foreach (var attacker in activeById.Values.OrderBy(fleet => fleet.Id))
        {
            var state = CombatProfileRegistry.EnsureState(attacker);
            if (state.Order != MilitaryOrderType.Attack || state.TargetFleetId is null)
                continue;

            if (!activeById.TryGetValue(state.TargetFleetId.Value, out var target) ||
                !CanEngage(attacker, target))
            {
                SetHold(state, preserveDisengagement: true);
                continue;
            }

            targets[attacker.Id] = target.Id;
        }

        var explicitAttacks = targets.OrderBy(pair => pair.Key).ToArray();

        // A directly attacked armed fleet may return fire locally without invoking
        // civilization-level strategy. It chooses the lowest-id current attacker so
        // unordered collections cannot change authoritative results.
        foreach (var attack in explicitAttacks)
        {
            if (!activeById.TryGetValue(attack.Key, out var attacker) ||
                !activeById.TryGetValue(attack.Value, out var defender) ||
                targets.ContainsKey(defender.Id))
                continue;

            var defenderState = CombatProfileRegistry.EnsureState(defender);
            var defenderProfile = CombatProfileRegistry.Get(defenderState.ProfileId);
            if (defenderState.Order == MilitaryOrderType.Retreat || defenderState.IsDisengaged || !defenderProfile.HasWeapon)
                continue;

            targets[defender.Id] = attacker.Id;
        }

        var defenseThreats = BuildDefenseThreatIndex(activeById, explicitAttacks);

        foreach (var defender in activeById.Values.OrderBy(fleet => fleet.Id))
        {
            if (targets.ContainsKey(defender.Id))
                continue;

            var state = CombatProfileRegistry.EnsureState(defender);
            var profile = CombatProfileRegistry.Get(state.ProfileId);
            if (state.Order != MilitaryOrderType.Defend ||
                state.IsDisengaged ||
                !profile.HasWeapon ||
                state.DefendSystemId is null ||
                defender.CurrentSystemId != state.DefendSystemId)
                continue;

            var key = new DefenseThreatKey(defender.CivilizationId, state.DefendSystemId.Value);
            if (!defenseThreats.TryGetValue(key, out var threatId) ||
                !activeById.TryGetValue(threatId, out var hostile) ||
                !CanEngage(defender, hostile))
                continue;

            targets[defender.Id] = threatId;
        }

        return targets;
    }

    private Dictionary<DefenseThreatKey, int> BuildDefenseThreatIndex(
        IReadOnlyDictionary<int, FleetState> activeById,
        IReadOnlyList<KeyValuePair<int, int>> explicitAttacks)
    {
        var threats = new Dictionary<DefenseThreatKey, int>();

        foreach (var attack in explicitAttacks)
        {
            if (!activeById.TryGetValue(attack.Key, out var hostile) ||
                !activeById.TryGetValue(attack.Value, out var threatened) ||
                threatened.CurrentSystemId is not int systemId ||
                hostile.CurrentSystemId != systemId ||
                !_hostilityView.AreHostile(threatened.CivilizationId, hostile.CivilizationId))
                continue;

            var key = new DefenseThreatKey(threatened.CivilizationId, systemId);
            if (!threats.TryGetValue(key, out var currentThreatId) || hostile.Id < currentThreatId)
                threats[key] = hostile.Id;
        }

        return threats;
    }

    private bool CanEngage(FleetState attacker, FleetState target)
    {
        if (!attacker.IsActive || !target.IsActive || attacker.CivilizationId == target.CivilizationId)
            return false;
        if (attacker.CurrentSystemId is null || attacker.CurrentSystemId != target.CurrentSystemId)
            return false;

        var attackerState = CombatProfileRegistry.EnsureState(attacker);
        var targetState = CombatProfileRegistry.EnsureState(target);
        if (attackerState.IsDisengaged || targetState.IsDisengaged)
            return false;

        return _hostilityView.AreHostile(attacker.CivilizationId, target.CivilizationId);
    }

    private static List<FireAction> BuildFireActions(
        IReadOnlyDictionary<int, FleetState> activeById,
        IReadOnlyDictionary<int, int> targetMap,
        double simulationDeltaDays)
    {
        var actions = new List<FireAction>();
        var sourcesWithTargets = new HashSet<int>();

        foreach (var targetPair in targetMap.OrderBy(pair => pair.Key))
        {
            if (!activeById.TryGetValue(targetPair.Key, out var source) ||
                !activeById.TryGetValue(targetPair.Value, out var target))
                continue;

            var sourceState = CombatProfileRegistry.EnsureState(source);
            var targetState = CombatProfileRegistry.EnsureState(target);
            var profile = CombatProfileRegistry.Get(sourceState.ProfileId);
            sourcesWithTargets.Add(source.Id);

            if (!profile.HasWeapon || sourceState.Order == MilitaryOrderType.Retreat || sourceState.IsDisengaged)
            {
                sourceState.WeaponCooldownRemainingDays = Math.Max(0.0, sourceState.WeaponCooldownRemainingDays - simulationDeltaDays);
                continue;
            }

            var engagementDelta = simulationDeltaDays;
            if (targetState.Order == MilitaryOrderType.Retreat)
            {
                var targetProfile = CombatProfileRegistry.Get(targetState.ProfileId);
                var timeUntilEscape = Math.Max(0.0, targetProfile.RetreatDelayDays - targetState.RetreatProgressDays);
                engagementDelta = Math.Min(engagementDelta, timeUntilEscape);
            }

            var volleys = AdvanceWeaponTimer(sourceState, profile, engagementDelta);
            if (volleys > 0)
                actions.Add(new FireAction(source.Id, target.Id, volleys, profile.WeaponDamage * volleys));

            var nonEngagedRemainder = Math.Max(0.0, simulationDeltaDays - engagementDelta);
            if (nonEngagedRemainder > 0.0)
                sourceState.WeaponCooldownRemainingDays = Math.Max(0.0, sourceState.WeaponCooldownRemainingDays - nonEngagedRemainder);
        }

        foreach (var fleet in activeById.Values.Where(fleet => !sourcesWithTargets.Contains(fleet.Id)))
        {
            var state = CombatProfileRegistry.EnsureState(fleet);
            state.WeaponCooldownRemainingDays = Math.Max(0.0, state.WeaponCooldownRemainingDays - simulationDeltaDays);
        }

        return actions;
    }

    private static int AdvanceWeaponTimer(
        FleetCombatState state,
        CombatProfileDefinition profile,
        double elapsedDays)
    {
        if (!profile.HasWeapon || elapsedDays <= 0.0)
            return 0;

        var cooldown = Math.Max(0.0, state.WeaponCooldownRemainingDays);
        if (cooldown > elapsedDays + Epsilon)
        {
            state.WeaponCooldownRemainingDays = cooldown - elapsedDays;
            return 0;
        }

        var remainingAfterFirstVolley = Math.Max(0.0, elapsedDays - cooldown);
        var additionalVolleys = remainingAfterFirstVolley <= Epsilon
            ? 0
            : Math.Max(0, (int)Math.Floor((remainingAfterFirstVolley - Epsilon) / profile.WeaponIntervalDays));
        var volleys = 1 + additionalVolleys;
        var elapsedSinceLastVolley = remainingAfterFirstVolley - additionalVolleys * profile.WeaponIntervalDays;
        state.WeaponCooldownRemainingDays = Math.Max(0.0, profile.WeaponIntervalDays - elapsedSinceLastVolley);
        if (state.WeaponCooldownRemainingDays <= Epsilon)
            state.WeaponCooldownRemainingDays = 0.0;
        return volleys;
    }

    private static void ApplyFireActions(
        GalaxyState galaxy,
        IReadOnlyList<FireAction> actions,
        ICollection<CombatEvent> events)
    {
        var allById = galaxy.Fleets.ToDictionary(fleet => fleet.Id);

        foreach (var action in actions.OrderBy(item => item.TargetFleetId).ThenBy(item => item.SourceFleetId))
        {
            if (!allById.TryGetValue(action.SourceFleetId, out var source) ||
                !allById.TryGetValue(action.TargetFleetId, out var target))
                continue;

            var targetState = CombatProfileRegistry.EnsureState(target);
            if (targetState.Hull <= Epsilon)
                continue;

            var damage = ApplyDamage(targetState, action.Damage);
            if (damage.TotalApplied <= Epsilon)
                continue;

            events.Add(new CombatEvent(
                CombatEventType.DamageApplied,
                target.CurrentSystemId,
                source.CivilizationId,
                source.Id,
                target.CivilizationId,
                target.Id,
                damage.ShieldDamage,
                damage.ArmorDamage,
                damage.HullDamage,
                $"{source.Name} hit {target.Name}: {damage.TotalApplied:0.#} damage ({damage.ShieldDamage:0.#} shields, {damage.ArmorDamage:0.#} armor, {damage.HullDamage:0.#} hull)."));

            if (targetState.Hull > Epsilon || !target.IsActive)
                continue;

            var embarkedPopulationCasualties = Math.Max(0.0, target.EmbarkedPopulationMillions);
            target.EmbarkedPopulationMillions = 0.0;
            target.EmbarkedPopulationSpeciesId = null;
            target.IsActive = false;
            target.DestinationSystemId = null;
            target.PlannedRouteSystemIds.Clear();
            target.DestinationPlanetaryBodyId = null;
            SetHold(targetState, preserveDisengagement: false);

            var casualtySuffix = embarkedPopulationCasualties > Epsilon
                ? $" {embarkedPopulationCasualties:0.###} million embarked population were lost."
                : string.Empty;
            events.Add(new CombatEvent(
                CombatEventType.FleetDestroyed,
                target.CurrentSystemId,
                source.CivilizationId,
                source.Id,
                target.CivilizationId,
                target.Id,
                0.0,
                0.0,
                0.0,
                $"{target.Name} was destroyed by {source.Name}.{casualtySuffix}",
                embarkedPopulationCasualties));
        }
    }

    private void ProcessRetreats(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, int> targetMap,
        double simulationDeltaDays,
        ICollection<CombatEvent> events)
    {
        var allById = galaxy.Fleets.ToDictionary(fleet => fleet.Id);
        var activelyThreatenedFleetIds = BuildActivelyThreatenedFleetIds(allById, targetMap);

        foreach (var fleet in galaxy.Fleets.Where(candidate => candidate.IsActive).OrderBy(candidate => candidate.Id))
        {
            var state = CombatProfileRegistry.EnsureState(fleet);
            if (state.Order != MilitaryOrderType.Retreat)
                continue;

            if (!state.RetreatStarted)
            {
                state.RetreatStarted = true;
                events.Add(new CombatEvent(
                    CombatEventType.FleetRetreatInitiated,
                    fleet.CurrentSystemId,
                    fleet.CivilizationId,
                    fleet.Id,
                    null,
                    null,
                    0.0,
                    0.0,
                    0.0,
                    $"{fleet.Name} began tactical disengagement."));
            }

            var hasActiveThreat = activelyThreatenedFleetIds.Contains(fleet.Id);

            var profile = CombatProfileRegistry.Get(state.ProfileId);
            if (hasActiveThreat)
                state.RetreatProgressDays += simulationDeltaDays;
            else
                state.RetreatProgressDays = profile.RetreatDelayDays;

            if (state.RetreatProgressDays + Epsilon < profile.RetreatDelayDays)
                continue;

            state.IsDisengaged = true;
            state.DisengagedSystemId = fleet.CurrentSystemId;
            state.Order = MilitaryOrderType.Hold;
            state.TargetFleetId = null;
            state.DefendSystemId = null;
            state.RetreatProgressDays = 0.0;
            state.RetreatStarted = false;
            events.Add(new CombatEvent(
                CombatEventType.FleetEscaped,
                fleet.CurrentSystemId,
                fleet.CivilizationId,
                fleet.Id,
                null,
                null,
                0.0,
                0.0,
                0.0,
                $"{fleet.Name} successfully disengaged."));
        }
    }

    private HashSet<int> BuildActivelyThreatenedFleetIds(
        IReadOnlyDictionary<int, FleetState> allById,
        IReadOnlyDictionary<int, int> targetMap)
    {
        var threatened = new HashSet<int>();
        foreach (var pair in targetMap)
        {
            if (!allById.TryGetValue(pair.Key, out var attacker) ||
                !attacker.IsActive ||
                !allById.TryGetValue(pair.Value, out var target) ||
                !target.IsActive ||
                attacker.CurrentSystemId != target.CurrentSystemId ||
                !_hostilityView.AreHostile(attacker.CivilizationId, target.CivilizationId))
                continue;

            threatened.Add(target.Id);
        }

        return threatened;
    }

    private void EmitNewEngagements(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, int> targetMap,
        IReadOnlySet<EngagementKey> current,
        ICollection<CombatEvent> events)
    {
        var allById = galaxy.Fleets.ToDictionary(fleet => fleet.Id);
        foreach (var key in current.Where(key => !_activeEngagements.Contains(key)).OrderBy(key => key.FirstFleetId).ThenBy(key => key.SecondFleetId))
        {
            if (!allById.TryGetValue(key.FirstFleetId, out var first) ||
                !allById.TryGetValue(key.SecondFleetId, out var second))
                continue;

            var actor = ChooseAggressor(first, second, targetMap);
            var target = actor.Id == first.Id ? second : first;
            events.Add(new CombatEvent(
                CombatEventType.EngagementStarted,
                actor.CurrentSystemId ?? target.CurrentSystemId,
                actor.CivilizationId,
                actor.Id,
                target.CivilizationId,
                target.Id,
                0.0,
                0.0,
                0.0,
                $"{actor.Name} engaged {target.Name}."));
        }
    }

    private void EmitEndedEngagements(
        GalaxyState galaxy,
        IReadOnlySet<EngagementKey> remaining,
        ICollection<CombatEvent> events)
    {
        var allById = galaxy.Fleets.ToDictionary(fleet => fleet.Id);
        foreach (var key in _activeEngagements.Where(key => !remaining.Contains(key)).OrderBy(key => key.FirstFleetId).ThenBy(key => key.SecondFleetId))
        {
            if (!allById.TryGetValue(key.FirstFleetId, out var first) ||
                !allById.TryGetValue(key.SecondFleetId, out var second))
                continue;

            events.Add(new CombatEvent(
                CombatEventType.EngagementEnded,
                first.CurrentSystemId ?? second.CurrentSystemId,
                first.CivilizationId,
                first.Id,
                second.CivilizationId,
                second.Id,
                0.0,
                0.0,
                0.0,
                $"Engagement between {first.Name} and {second.Name} ended."));
        }
    }

    private static HashSet<EngagementKey> BuildEngagementSet(IReadOnlyDictionary<int, int> targets) =>
        targets.Select(pair => EngagementKey.Create(pair.Key, pair.Value)).ToHashSet();

    private static FleetState ChooseAggressor(
        FleetState first,
        FleetState second,
        IReadOnlyDictionary<int, int> targetMap)
    {
        var firstState = CombatProfileRegistry.EnsureState(first);
        if (firstState.Order == MilitaryOrderType.Attack && firstState.TargetFleetId == second.Id)
            return first;

        var secondState = CombatProfileRegistry.EnsureState(second);
        if (secondState.Order == MilitaryOrderType.Attack && secondState.TargetFleetId == first.Id)
            return second;

        if (targetMap.TryGetValue(first.Id, out var firstTarget) && firstTarget == second.Id)
            return first;
        return second;
    }

    private static DamageResult ApplyDamage(FleetCombatState state, double incomingDamage)
    {
        var remaining = Math.Max(0.0, incomingDamage);
        var shieldDamage = Math.Min(state.Shields, remaining);
        state.Shields -= shieldDamage;
        remaining -= shieldDamage;

        var armorDamage = Math.Min(state.Armor, remaining);
        state.Armor -= armorDamage;
        remaining -= armorDamage;

        var hullDamage = Math.Min(state.Hull, remaining);
        state.Hull -= hullDamage;
        return new DamageResult(shieldDamage, armorDamage, hullDamage);
    }

    private static void SetHold(FleetCombatState state, bool preserveDisengagement)
    {
        state.Order = MilitaryOrderType.Hold;
        state.TargetFleetId = null;
        state.DefendSystemId = null;
        state.RetreatProgressDays = 0.0;
        state.RetreatStarted = false;
        if (!preserveDisengagement)
        {
            state.IsDisengaged = false;
            state.DisengagedSystemId = null;
        }
    }

    private readonly record struct DefenseThreatKey(int CivilizationId, int SystemId);

    private readonly record struct EngagementKey(int FirstFleetId, int SecondFleetId)
    {
        public static EngagementKey Create(int firstFleetId, int secondFleetId) =>
            firstFleetId <= secondFleetId
                ? new EngagementKey(firstFleetId, secondFleetId)
                : new EngagementKey(secondFleetId, firstFleetId);
    }

    private sealed record FireAction(int SourceFleetId, int TargetFleetId, int Volleys, double Damage);

    private sealed record DamageResult(double ShieldDamage, double ArmorDamage, double HullDamage)
    {
        public double TotalApplied => ShieldDamage + ArmorDamage + HullDamage;
    }
}
