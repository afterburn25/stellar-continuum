using System;
using System.Collections.Generic;

namespace Game.Simulation.AI;

/// <summary>
/// The only strategic world-state an AI evaluator is allowed to consume.
/// It intentionally contains observations and estimates, never authoritative enemy state.
/// </summary>
public sealed class KnowledgeSnapshot
{
    public required long ObservedAtTick { get; init; }
    public required IReadOnlyDictionary<int, KnownCivilization> Civilizations { get; init; }
}

public sealed record KnownCivilization(
    int CivilizationId,
    double Trust,
    double EstimatedMilitaryLow,
    double EstimatedMilitaryHigh,
    double EstimateConfidence,
    long LastMilitaryObservationTick,
    bool HasSharedBorder,
    double KnownTradeDependence,
    double KnownWarExhaustion,
    bool KnownToBeAtWar,
    bool HasDefenseTreatyWithObserver
)
{
    /// <summary>
    /// True only when EstimatedMilitaryLow/High originate from a legitimate observer-local
    /// military intelligence source. Knowing a civilization diplomatically does not imply knowing
    /// its fleet strength. Defaults true for existing explicit intelligence/test construction.
    /// </summary>
    public bool HasMilitaryEstimate { get; init; } = true;

    public double EstimatedMilitaryMidpoint => (EstimatedMilitaryLow + EstimatedMilitaryHigh) * 0.5;

    public double Freshness(long nowTick, long staleAfterTicks)
    {
        if (staleAfterTicks <= 0) return 0.0;
        var age = Math.Max(0L, nowTick - LastMilitaryObservationTick);
        return Math.Clamp(1.0 - (double)age / staleAfterTicks, 0.0, 1.0);
    }
}
