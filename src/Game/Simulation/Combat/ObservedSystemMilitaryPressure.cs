using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.AI;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

public enum ObservedSystemThreatState
{
    NoObservedForeignContact,
    UnidentifiedContactObserved,
    IdentifiedNonHostileContactObserved,
    HostileContactObserved,
}

/// <summary>
/// Observer-safe military context for one system. Own force state is exact because it belongs to
/// the observer. Foreign entries are derived only from observer-local Diplomacy contact records
/// plus civilization-wide intelligence estimates; this contract never reads authoritative foreign
/// fleet composition or claims that a civilization-wide estimate is physically present locally.
/// </summary>
public sealed record ObservedSystemMilitaryPressure(
    int ObserverCivilizationId,
    int SystemId,
    int OwnCombatEffectiveArmedVessels,
    double OwnCombatEffectiveArmedStrength,
    ObservedSystemThreatState ThreatState,
    int UnidentifiedContactCount,
    IReadOnlyList<ObservedForeignMilitaryContact> ForeignContacts)
{
    public bool HasObservedHostileContact =>
        ForeignContacts.Any(contact => contact.IsPoliticallyHostile || contact.ContactCondition == ContactCondition.Hostile);
}

/// <summary>
/// A foreign contact last observed in the queried system. Military estimate fields describe the
/// civilization as a whole, not local fleet strength, and remain null when no legitimate estimate
/// exists in the supplied KnowledgeSnapshot.
/// </summary>
public sealed record ObservedForeignMilitaryContact(
    string ContactId,
    int CivilizationId,
    ContactCondition ContactCondition,
    DiplomaticPoliticalState PoliticalState,
    double ContactConfidence,
    long LastContactObservationTick,
    double? EstimatedCivilizationMilitaryLow,
    double? EstimatedCivilizationMilitaryHigh,
    double? MilitaryEstimateConfidence,
    long? LastMilitaryObservationTick)
{
    public bool IsPoliticallyHostile => PoliticalState is DiplomaticPoliticalState.Hostile or DiplomaticPoliticalState.AtWar;
}

public sealed class ObserverSystemMilitaryPressureView
{
    public ObservedSystemMilitaryPressure Build(
        GalaxyState galaxy,
        int observerCivilizationId,
        int systemId,
        DiplomaticStateView diplomacy,
        KnowledgeSnapshot knowledge)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(diplomacy);
        ArgumentNullException.ThrowIfNull(knowledge);

        if (!galaxy.Civilizations.Any(civilization => civilization.Id == observerCivilizationId))
            throw new InvalidOperationException($"Unknown civilization {observerCivilizationId}.");
        if (!galaxy.Systems.Any(system => system.Id == systemId))
            throw new InvalidOperationException($"Unknown star system {systemId}.");
        if (diplomacy.ObserverCivilizationId != observerCivilizationId)
            throw new InvalidOperationException("Diplomacy view belongs to a different observer.");

        var ownVessels = 0;
        var ownStrength = 0.0;
        foreach (var fleet in galaxy.Fleets
                     .Where(fleet => fleet.IsActive &&
                                     fleet.CivilizationId == observerCivilizationId &&
                                     fleet.CurrentSystemId == systemId)
                     .OrderBy(fleet => fleet.Id))
        {
            var readiness = CombatReadinessCalculator.ReadFleet(fleet);
            if (!readiness.IsArmed || !readiness.IsCombatEffective)
                continue;

            ownVessels++;
            ownStrength += readiness.CurrentStrength;
        }

        var relationships = diplomacy.Relationships
            .ToDictionary(relationship => relationship.OtherCivilizationId);
        var unidentified = 0;
        var foreign = new List<ObservedForeignMilitaryContact>();

        foreach (var contact in diplomacy.Contacts
                     .Where(contact => contact.LastObservedSystemId == systemId)
                     .OrderBy(contact => contact.ContactId, StringComparer.Ordinal))
        {
            if (contact.TargetCivilizationId is not int foreignCivilizationId ||
                contact.Awareness < ContactAwareness.Identified)
            {
                unidentified++;
                continue;
            }

            relationships.TryGetValue(foreignCivilizationId, out var relationship);
            knowledge.Civilizations.TryGetValue(foreignCivilizationId, out var known);
            var hasMilitaryEstimate = known?.HasMilitaryEstimate == true;
            foreign.Add(new ObservedForeignMilitaryContact(
                contact.ContactId,
                foreignCivilizationId,
                contact.Condition,
                relationship?.PoliticalState ?? DiplomaticPoliticalState.Unknown,
                Clamp01(contact.Confidence),
                Math.Max(0L, contact.LastObservedTick),
                hasMilitaryEstimate ? Math.Max(0.0, known!.EstimatedMilitaryLow) : null,
                hasMilitaryEstimate ? Math.Max(Math.Max(0.0, known!.EstimatedMilitaryLow), known.EstimatedMilitaryHigh) : null,
                hasMilitaryEstimate ? Clamp01(known!.EstimateConfidence) : null,
                hasMilitaryEstimate ? Math.Max(0L, known!.LastMilitaryObservationTick) : null));
        }

        var threatState = foreign.Any(contact => contact.IsPoliticallyHostile || contact.ContactCondition == ContactCondition.Hostile)
            ? ObservedSystemThreatState.HostileContactObserved
            : foreign.Count > 0
                ? ObservedSystemThreatState.IdentifiedNonHostileContactObserved
                : unidentified > 0
                    ? ObservedSystemThreatState.UnidentifiedContactObserved
                    : ObservedSystemThreatState.NoObservedForeignContact;

        return new ObservedSystemMilitaryPressure(
            observerCivilizationId,
            systemId,
            ownVessels,
            ownStrength,
            threatState,
            unidentified,
            foreign);
    }

    private static double Clamp01(double value) =>
        double.IsFinite(value) ? Math.Clamp(value, 0.0, 1.0) : 0.0;
}
