using System;

namespace Game.Presentation;

/// <summary>Deterministic presentation pacing for real campaign-loading milestones.</summary>
public sealed class CampaignLoadingTimeline
{
    public const double MinimumDisplaySeconds = 7.0;

    public double DisplayedPercent { get; private set; }
    public bool CanDismiss { get; private set; }

    public void Advance(double deltaSeconds, double elapsedSeconds, double assetFraction,
        bool operationStarted, bool operationReady, bool failed = false, double operationFraction = 0)
    {
        if (!double.IsFinite(deltaSeconds) || deltaSeconds < 0) throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
        if (!double.IsFinite(elapsedSeconds) || elapsedSeconds < 0) throw new ArgumentOutOfRangeException(nameof(elapsedSeconds));
        if (!double.IsFinite(assetFraction)) throw new ArgumentOutOfRangeException(nameof(assetFraction));
        if (!double.IsFinite(operationFraction)) throw new ArgumentOutOfRangeException(nameof(operationFraction));

        assetFraction = Math.Clamp(assetFraction, 0, 1);
        operationFraction = Math.Clamp(operationFraction, 0, 1);
        var realMilestoneTarget = 8 + assetFraction * 32;
        if (operationStarted) realMilestoneTarget = Math.Max(realMilestoneTarget, 45 + operationFraction * 50);
        if (operationReady) realMilestoneTarget = 99;
        var pacedCap = 99 * Math.Clamp(elapsedSeconds / MinimumDisplaySeconds, 0, 1);
        var target = elapsedSeconds < MinimumDisplaySeconds
            ? Math.Min(realMilestoneTarget, pacedCap)
            : realMilestoneTarget;
        if (failed) target = Math.Min(target, 99);

        var smoothing = 1 - Math.Exp(-5 * deltaSeconds);
        DisplayedPercent = Math.Max(DisplayedPercent,
            DisplayedPercent + (target - DisplayedPercent) * smoothing);
        CanDismiss = !failed && operationReady && elapsedSeconds >= MinimumDisplaySeconds;
        if (CanDismiss) DisplayedPercent = 100;
    }
}
