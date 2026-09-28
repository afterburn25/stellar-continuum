using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.AI;

/// <summary>
/// Supplies observer-local strategic knowledge to Civilization AI. Implementations must expose
/// only facts the observing civilization can legitimately know; authoritative hidden rival state
/// is never an acceptable implementation source.
/// </summary>
public interface IStrategicKnowledgeProvider
{
    KnowledgeSnapshot Build(int observerCivilizationId, long nowTick);
}

/// <summary>
/// Compatibility/default provider for isolated simulation usage that has no Diplomacy owner.
/// Preserves the pre-save-v9 behavior: AI coordinates own state but assumes no foreign knowledge.
/// </summary>
public sealed class EmptyStrategicKnowledgeProvider : IStrategicKnowledgeProvider
{
    public KnowledgeSnapshot Build(int observerCivilizationId, long nowTick)
    {
        _ = observerCivilizationId;
        if (nowTick < 0) throw new ArgumentOutOfRangeException(nameof(nowTick));
        return new KnowledgeSnapshot
        {
            ObservedAtTick = nowTick,
            Civilizations = new Dictionary<int, KnownCivilization>(),
        };
    }
}

/// <summary>
/// Adapts the persisted campaign Diplomacy owner into Civilization AI's fair-information
/// KnowledgeSnapshot. The adapter consumes only DiplomacyState.BuildViewFor(observer), which is
/// already filtered for contact identity, bilateral relationship visibility, agreements and event
/// audiences. It never reads the authoritative Diplomacy snapshot or Galaxy foreign fleets.
///
/// Diplomacy currently does not contain a lawful military-strength observation, so every mapped
/// civilization explicitly carries HasMilitaryEstimate=false. Known war can still drive defensive
/// caution, but offensive/threat strength evaluation remains disabled until a real intelligence
/// source provides an observer-local estimate.
/// </summary>
public sealed class DiplomacyStrategicKnowledgeProvider : IStrategicKnowledgeProvider
{
    private readonly DiplomacyState _diplomacy;

    public DiplomacyStrategicKnowledgeProvider(DiplomacyState diplomacy)
    {
        _diplomacy = diplomacy ?? throw new ArgumentNullException(nameof(diplomacy));
    }

    public KnowledgeSnapshot Build(int observerCivilizationId, long nowTick)
    {
        if (observerCivilizationId < 0) throw new ArgumentOutOfRangeException(nameof(observerCivilizationId));
        if (nowTick < 0) throw new ArgumentOutOfRangeException(nameof(nowTick));

        var view = _diplomacy.BuildViewFor(observerCivilizationId);
        var relationships = view.Relationships.ToDictionary(
            relationship => relationship.OtherCivilizationId);

        var targets = view.Contacts
            .Where(contact =>
                contact.TargetCivilizationId.HasValue &&
                contact.Awareness >= ContactAwareness.Identified)
            .Select(contact => contact.TargetCivilizationId!.Value)
            .Where(target => target != observerCivilizationId)
            .Distinct()
            .OrderBy(target => target)
            .ToArray();

        var known = new Dictionary<int, KnownCivilization>(targets.Length);
        foreach (var target in targets)
        {
            relationships.TryGetValue(target, out var relationship);

            // AI's legacy Trust axis permits [-1,1], while Diplomacy stores separate [0,1]
            // trust and hostility. Their observer-visible difference is a faithful relationship
            // sentiment projection without inventing information.
            var trust = relationship is null
                ? 0.0
                : Math.Clamp(relationship.Trust - relationship.Hostility, -1.0, 1.0);
            var atWar = relationship?.PoliticalState == DiplomaticPoliticalState.AtWar;

            known[target] = new KnownCivilization(
                CivilizationId: target,
                Trust: trust,
                EstimatedMilitaryLow: 0.0,
                EstimatedMilitaryHigh: 0.0,
                EstimateConfidence: 0.0,
                LastMilitaryObservationTick: 0,
                HasSharedBorder: false,
                KnownTradeDependence: 0.0,
                KnownWarExhaustion: 0.0,
                KnownToBeAtWar: atWar,
                HasDefenseTreatyWithObserver: false)
            {
                HasMilitaryEstimate = false,
            };
        }

        return new KnowledgeSnapshot
        {
            ObservedAtTick = nowTick,
            Civilizations = known,
        };
    }
}
