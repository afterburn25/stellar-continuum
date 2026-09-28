using System;

namespace Game.Simulation.Diplomacy;

/// <summary>
/// Shared deterministic conversion between the campaign's floating-point simulation-day clock
/// and Diplomacy's integer event chronology. Integer ticks keep snapshots stable and sortable;
/// they do not imply per-tick simulation work.
/// </summary>
public static class DiplomacyCampaignClock
{
    public const long TicksPerSimulationDay = 1000;

    public static long FromSimulationDays(double simulationDays)
    {
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays));

        var scaled = Math.Floor(simulationDays * TicksPerSimulationDay);
        return scaled >= long.MaxValue ? long.MaxValue : (long)scaled;
    }

    public static long TicksForWholeDays(long days)
    {
        if (days <= 0)
            throw new ArgumentOutOfRangeException(nameof(days));
        if (days > long.MaxValue / TicksPerSimulationDay)
            return long.MaxValue;
        return days * TicksPerSimulationDay;
    }
}
