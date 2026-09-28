using System;

namespace Game.Simulation;

/// <summary>
/// Strategic time is measured in in-game days. At Normal speed one real second requests
/// one in-game day. Higher speeds request proportionally more days while backlog protection
/// keeps the UI responsive when a machine cannot sustain the requested rate.
/// </summary>
public sealed class SimulationClock
{
    public enum SpeedLevel
    {
        Paused = 0,
        Normal = 1,
        Fast = 2,
        VeryFast = 3,
        Maximum = 4,
        Demo = 5,
    }

    private readonly double[] _multipliers = { 0.0, 1.0, 2.0, 3.0, 8.0, 24.0 };

    public SpeedLevel Speed { get; private set; } = SpeedLevel.Normal;
    private SpeedLevel _lastRunningSpeed = SpeedLevel.Normal;
    public SpeedLevel ResumeSpeed => _lastRunningSpeed;
    public double SimulationDays { get; private set; }
    public double EffectiveMultiplier { get; private set; } = 1.0;
    public double RequestedMultiplier => _multipliers[(int)Speed];
    public double BacklogDays { get; private set; }

    public void SetSpeed(SpeedLevel speed)
    {
        if (speed != SpeedLevel.Paused)
            _lastRunningSpeed = speed;
        Speed = speed;
    }

    public void Resume() => Speed = _lastRunningSpeed;

    public void SelectResumeSpeed(SpeedLevel speed)
    {
        if (speed == SpeedLevel.Paused)
            throw new ArgumentOutOfRangeException(nameof(speed), "Resume speed cannot be paused.");
        _lastRunningSpeed = speed;
    }

    public void Restore(double simulationDays)
    {
        SimulationDays = Math.Max(0.0, simulationDays);
        BacklogDays = 0.0;
    }

    public double Advance(double realDeltaSeconds, double maxSimulationStepDays = 0.25)
    {
        var requested = RequestedMultiplier;
        if (requested <= 0.0)
        {
            EffectiveMultiplier = 0.0;
            return 0.0;
        }

        var requestedDays = realDeltaSeconds * requested;
        var accepted = Math.Min(requestedDays, maxSimulationStepDays);
        BacklogDays = Math.Max(0.0, BacklogDays + requestedDays - accepted);

        var drain = Math.Min(BacklogDays, maxSimulationStepDays * 0.20);
        accepted += drain;
        BacklogDays -= drain;

        SimulationDays += accepted;
        EffectiveMultiplier = realDeltaSeconds > 0.0 ? accepted / realDeltaSeconds : requested;
        return accepted;
    }

    /// <summary>Demo frame budget; a stalled frame cannot queue an unbounded catch-up burst.</summary>
    public double AdvanceBoundedFrame(double realDeltaSeconds, double maximumDays = 1.0, double maximumBacklogDays = 2.0)
    {
        if (!double.IsFinite(realDeltaSeconds) || realDeltaSeconds < 0 ||
            !double.IsFinite(maximumDays) || maximumDays <= 0 ||
            !double.IsFinite(maximumBacklogDays) || maximumBacklogDays < 0)
            throw new ArgumentOutOfRangeException(nameof(realDeltaSeconds), "Frame time and budgets must be finite and nonnegative; step budget must be positive.");
        if (RequestedMultiplier <= 0 || realDeltaSeconds == 0)
        {
            EffectiveMultiplier = 0;
            return 0;
        }
        var requestedDays = Math.Min(realDeltaSeconds, (maximumDays + maximumBacklogDays) / RequestedMultiplier) * RequestedMultiplier;
        var available = requestedDays + Math.Min(BacklogDays, maximumBacklogDays);
        var accepted = Math.Min(available, maximumDays);
        BacklogDays = Math.Min(maximumBacklogDays, Math.Max(0, available - accepted));
        SimulationDays += accepted;
        EffectiveMultiplier = accepted / realDeltaSeconds;
        return accepted;
    }
}
