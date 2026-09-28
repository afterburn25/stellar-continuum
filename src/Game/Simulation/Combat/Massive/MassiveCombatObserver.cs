using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;

namespace Game.Simulation.Combat.Massive;

public static class MassiveCombatObserver
{
    public static MassiveCombatSnapshot BuildSnapshot(MassiveCombatBattleState battle, int observerCivilizationId, IMassiveCombatSensorView sensors)
    {
        ArgumentNullException.ThrowIfNull(battle); ArgumentNullException.ThrowIfNull(sensors);
        var formations = battle.Formations.Where(x => x.Active &&
            (x.CivilizationId == observerCivilizationId || sensors.Confidence(observerCivilizationId, x.Id) > 0)).OrderBy(x => x.Id).Select(formation =>
        {
            var own = formation.CivilizationId == observerCivilizationId;
            var confidence = own ? 1f : Math.Clamp(sensors.Confidence(observerCivilizationId, formation.Id), 0, 1);
            var exact = own || confidence >= .999f;
            var count = formation.ActiveShipCount;
            var uncertainty = exact ? 0 : Math.Max(1, (int)MathF.Ceiling(count * (1f - confidence) * .45f));
            float? strengthLow = null, strengthHigh = null;
            if (own || sensors.CanEstimateCombatPower(observerCivilizationId, formation.Id))
            {
                var strength = MassiveCombatPowerCalculator.FormationPower(formation);
                var spread = exact ? 0 : strength * (1f - confidence) * .5f;
                strengthLow = Math.Max(0, strength - spread); strengthHigh = strength + spread;
            }
            var mayIdentify = own || sensors.IdentifiesImportantVessels(observerCivilizationId, formation.Id);
            var mayIdentifyCohorts = own || sensors.IdentifiesCohorts(observerCivilizationId, formation.Id);
            var mayEstimatePower = own || sensors.CanEstimateCombatPower(observerCivilizationId, formation.Id);
            var cohorts = mayIdentifyCohorts
                ? formation.Cohorts.Where(x => x.ActiveCount > 0).OrderBy(x => x.Id)
                    .Select(x => ObservedCohort(x.Id, x.DesignId, x.ActiveCount, exact, confidence, true)).ToArray()
                : count > 0
                    ? [ObservedCohort(formation.Id, "Unidentified ships", count, exact, confidence, false)]
                    : Array.Empty<MassiveObservedCohort>();
            var important = mayIdentify
                ? formation.ImportantVessels.Where(x => !x.Destroyed && !x.Escaped).OrderBy(x => x.Id)
                    .Select(x => new MassiveObservedVessel(x.Id, x.Name, x.DesignId,
                        mayEstimatePower ? MassiveCombatPowerCalculator.ImportantVesselPower(formation.Loadout, x) : null,
                        x.IsFlagship, x.IsCarrier, x.IsInterdictor, x.HullFraction < .3f)).ToArray()
                : Array.Empty<MassiveObservedVessel>();
            return new MassiveObservedFormation(formation.Id, formation.CivilizationId,
                own || confidence >= .65f ? formation.Name : "Unidentified formation", formation.Position, formation.Velocity,
                formation.Shape, Math.Max(0, count - uncertainty), count + uncertainty, strengthLow, strengthHigh,
                exact, mayIdentify && formation.Loadout.Modules.Any(x => x.Kind == MassiveModuleKind.WarpInterdictor && x.Enabled && x.Condition > .05f),
                own && formation.WarpBlocked, own ? formation.WarpSpoolProgress : 0,
                mayEstimatePower ? MassiveCombatPowerCalculator.EffectivePerShipPower(formation) : null, cohorts, important,
                HeadingRadians(formation));
        }).ToArray();
        var formationsById = battle.Formations.ToDictionary(formation => formation.Id);
        var visibleEvents = battle.Events
            .Where(x => x.ActorCivilizationId == observerCivilizationId || x.TargetCivilizationId == observerCivilizationId ||
                sensors.Confidence(observerCivilizationId, x.ActorFormationId) >= .65f)
            .TakeLast(128)
            .Select(x => ObserveEvent(x, observerCivilizationId, sensors, formationsById))
            .ToArray();
        var salvos = battle.ActiveSalvos.OrderBy(salvo => salvo.Id)
            .Select(salvo => ObserveSalvo(salvo, observerCivilizationId, sensors, formationsById))
            .Where(salvo => salvo is not null)
            .Cast<MassiveObservedMissileSalvo>()
            .ToList().AsReadOnly();
        return new(battle.BattleId, battle.Tick, battle.SimulatedSeconds,
            battle.Formations.Where(x => x.CivilizationId == observerCivilizationId).Sum(x => x.ActiveShipCount),
            Array.AsReadOnly(formations), Array.AsReadOnly(visibleEvents), salvos);
    }

    private static MassiveObservedCohort ObservedCohort(long id, string displayClass, int count, bool exact, float confidence, bool identified)
    {
        var uncertainty = exact ? 0 : Math.Max(1, (int)MathF.Ceiling(count * (1f - confidence) * .45f));
        return new(id, displayClass, Math.Max(0, count - uncertainty), count + uncertainty, identified);
    }

    private static MassiveObservedCombatEvent ObserveEvent(MassiveCombatEvent value, int observerCivilizationId,
        IMassiveCombatSensorView sensors, IReadOnlyDictionary<long, MassiveFormationState> formations)
    {
        var actorKnown = value.ActorCivilizationId == observerCivilizationId || sensors.Confidence(observerCivilizationId, value.ActorFormationId) >= .65f;
        var targetKnown = value.TargetFormationId is null || value.TargetCivilizationId == observerCivilizationId ||
            sensors.Confidence(observerCivilizationId, value.TargetFormationId.Value) >= .65f;
        var impact = ImpactPosition(value, actorKnown,
            targetKnown && (actorKnown || value.TargetCivilizationId == observerCivilizationId), formations);
        if (actorKnown && targetKnown)
            return new(value.Sequence, value.Tick, value.Type, value.ActorCivilizationId, value.ActorFormationId,
                value.TargetCivilizationId, value.TargetFormationId, value.Magnitude, value.Position, value.Message, true, impact);

        if (actorKnown)
            return new(value.Sequence, value.Tick, value.Type, value.ActorCivilizationId, value.ActorFormationId,
                null, null, null, value.Position, "An observed formation acted against an unidentified contact.", false, null);

        // A hit on an owned unit is observable, but it cannot reveal an unscanned attacker's
        // identity, location, weapon volume, or formation name through an event side channel.
        return new(value.Sequence, value.Tick, value.Type, null, null,
            value.TargetCivilizationId == observerCivilizationId ? value.TargetCivilizationId : null,
            value.TargetCivilizationId == observerCivilizationId ? value.TargetFormationId : null,
            null, null, "An unidentified hostile action affected a friendly formation.", false, impact);
    }

    private static float HeadingRadians(MassiveFormationState formation)
    {
        var heading = formation.Heading.Vector;
        if (heading.LengthSquared() < .000001f) heading = formation.Velocity.Vector;
        return heading.LengthSquared() < .000001f ? 0 : MathF.Atan2(heading.Y, heading.X);
    }

    private static MassiveObservedMissileSalvo? ObserveSalvo(MassiveMissileSalvoState salvo,
        int observerCivilizationId, IMassiveCombatSensorView sensors,
        IReadOnlyDictionary<long, MassiveFormationState> formations)
    {
        if (!formations.TryGetValue(salvo.SourceFormationId, out var source) ||
            !formations.TryGetValue(salvo.TargetFormationId, out var target)) return null;
        // A salvo may remain authoritative until the next fixed tick removes it, but there is
        // no remaining target to present once destruction or withdrawal has completed.
        if (!target.Active) return null;
        var sourceOwn = source.CivilizationId == observerCivilizationId;
        var targetOwn = target.CivilizationId == observerCivilizationId;
        var sourceConfidence = sourceOwn ? 1f : Math.Clamp(sensors.Confidence(observerCivilizationId, source.Id), 0, 1);
        var targetConfidence = targetOwn ? 1f : Math.Clamp(sensors.Confidence(observerCivilizationId, target.Id), 0, 1);
        var sourceKnown = sourceOwn || sourceConfidence >= .65f;
        var targetKnown = targetOwn || targetConfidence >= .65f;
        if (!sourceOwn && !targetOwn && !(sourceKnown && targetKnown)) return null;

        float? progress = salvo.LaunchPosition is not null && salvo.InitialFlightSeconds > 0 && sourceKnown && targetKnown
            ? Math.Clamp(1f - salvo.RemainingSeconds / salvo.InitialFlightSeconds, 0, 1)
            : null;
        var pathKnown = progress is not null;
        var sourcePosition = sourceKnown ? salvo.LaunchPosition ?? source.Position : (MassivePoint?)null;
        var targetPosition = targetKnown ? target.Position : (MassivePoint?)null;
        MassivePoint? currentPosition = pathKnown
            ? (MassivePoint)Vector2.Lerp(salvo.LaunchPosition!.Value, target.Position, progress!.Value)
            : null;
        int? countLow = null, countHigh = null;
        if (sourceKnown)
        {
            var exact = sourceOwn || sourceConfidence >= .999f;
            var uncertainty = exact ? 0 : Math.Max(1, (int)MathF.Ceiling(salvo.MissileCount * (1 - sourceConfidence) * .45f));
            countLow = Math.Max(0, salvo.MissileCount - uncertainty);
            countHigh = salvo.MissileCount + uncertainty;
        }
        return new(salvo.Id, sourceKnown ? source.Id : null, sourcePosition,
            targetKnown ? target.Id : null, targetPosition, currentPosition, salvo.RemainingSeconds, progress,
            countLow, countHigh, targetOwn);
    }

    private static MassivePoint? ImpactPosition(MassiveCombatEvent value, bool actorKnown, bool targetKnown,
        IReadOnlyDictionary<long, MassiveFormationState> formations)
    {
        if (value.Type is not (MassiveCombatEventType.Damage or MassiveCombatEventType.MissileIntercepted or
            MassiveCombatEventType.FormationDestroyed)) return null;
        if (value.TargetFormationId is long targetId && targetKnown && formations.TryGetValue(targetId, out var target))
            return target.Position;
        return value.Type == MassiveCombatEventType.MissileIntercepted && actorKnown ? value.Position : null;
    }
}
