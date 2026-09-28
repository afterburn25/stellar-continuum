using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Combat.Massive;

/// <summary>Real-time tactical clock, independent from strategic campaign days.</summary>
public sealed class MassiveCombatClock
{
    public static IReadOnlyList<double> AllowedSpeeds { get; } = Array.AsReadOnly<double>([0, .25, .5, 1, 2, 4]);
    public double SpeedMultiplier { get; private set; } = 1;

    public void SetSpeed(double multiplier)
    {
        if (!double.IsFinite(multiplier) || !AllowedSpeeds.Contains(multiplier))
            throw new ArgumentOutOfRangeException(nameof(multiplier), "Tactical speed must be paused, .25×, .5×, 1×, 2×, or 4×.");
        SpeedMultiplier = multiplier;
    }

    public double AcceptFrame(double realDeltaSeconds, double maximumFrameSeconds = .25)
    {
        if (!double.IsFinite(realDeltaSeconds) || realDeltaSeconds < 0 ||
            !double.IsFinite(maximumFrameSeconds) || maximumFrameSeconds <= 0)
            throw new ArgumentOutOfRangeException(nameof(realDeltaSeconds));
        return Math.Min(realDeltaSeconds, maximumFrameSeconds) * SpeedMultiplier;
    }
}
