using System;

namespace Game.Campaign;

public sealed record CampaignAutosavePolicy(
    double IntervalDays = 30.0,
    double FailureRetryDays = 1.0)
{
    public CampaignAutosavePolicy Validate()
    {
        if (!double.IsFinite(IntervalDays) || IntervalDays <= 0.0)
            throw new ArgumentOutOfRangeException(nameof(IntervalDays), "Autosave interval must be finite and positive.");
        if (!double.IsFinite(FailureRetryDays) || FailureRetryDays <= 0.0)
            throw new ArgumentOutOfRangeException(nameof(FailureRetryDays), "Autosave retry delay must be finite and positive.");
        return this;
    }
}

/// <summary>
/// Plain-C# campaign autosave cadence. It performs only scalar time checks and never writes
/// persistence itself. A successful load/new/manual/automatic save schedules the next automatic
/// save after one full interval; a failed write backs off before another automatic attempt.
/// Large simulation-time jumps produce one save attempt rather than catch-up write loops.
/// </summary>
public sealed class CampaignAutosaveScheduler
{
    private readonly CampaignAutosavePolicy _policy;
    private double _nextDueDay = double.PositiveInfinity;

    public CampaignAutosaveScheduler(CampaignAutosavePolicy? policy = null)
    {
        _policy = (policy ?? new CampaignAutosavePolicy()).Validate();
    }

    public double NextDueDay => _nextDueDay;

    public void Reset(double currentSimulationDays)
    {
        ValidateDays(currentSimulationDays, nameof(currentSimulationDays));
        _nextDueDay = AddBounded(currentSimulationDays, _policy.IntervalDays);
    }

    public bool IsDue(double currentSimulationDays)
    {
        ValidateDays(currentSimulationDays, nameof(currentSimulationDays));
        return currentSimulationDays >= _nextDueDay;
    }

    public void MarkSuccess(double currentSimulationDays)
    {
        ValidateDays(currentSimulationDays, nameof(currentSimulationDays));
        _nextDueDay = AddBounded(currentSimulationDays, _policy.IntervalDays);
    }

    public void MarkFailure(double currentSimulationDays)
    {
        ValidateDays(currentSimulationDays, nameof(currentSimulationDays));
        _nextDueDay = AddBounded(currentSimulationDays, _policy.FailureRetryDays);
    }

    private static double AddBounded(double current, double delay)
    {
        var next = current + delay;
        return double.IsFinite(next) ? next : double.MaxValue;
    }

    private static void ValidateDays(double days, string parameterName)
    {
        if (!double.IsFinite(days) || days < 0.0)
            throw new ArgumentOutOfRangeException(parameterName, "Simulation days must be finite and non-negative.");
    }
}
