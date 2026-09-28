using Game.Presentation;

namespace Game.CoreRuntime.Validation;

internal static class CampaignLoadingTimelineValidation
{
    [Game.Validation.RegressionCheck]
    public static void RealReadinessAndMinimumPacingControlCompletion()
    {
        var fast = new CampaignLoadingTimeline();
        fast.Advance(.1, .1, .25, false, false);
        var initial = fast.DisplayedPercent;
        fast.Advance(.1, .2, 1, true, true);
        Require(fast.DisplayedPercent > initial && fast.DisplayedPercent < 100 && !fast.CanDismiss,
            "a fast load completed before the minimum splash interval");
        fast.Advance(3.3, 3.5, 1, true, true);
        Require(fast.DisplayedPercent is >= 40 and <= 55 && !fast.CanDismiss,
            "a fast load stalled near completion instead of advancing continuously at mid-interval");
        fast.Advance(3.5, 7, 1, true, true);
        Require(fast.CanDismiss && fast.DisplayedPercent == 100,
            "a ready fast load did not complete at the paced minimum");

        var slow = new CampaignLoadingTimeline();
        slow.Advance(10, 10, 1, true, false, operationFraction: .44);
        Require(!slow.CanDismiss && slow.DisplayedPercent < 100,
            "elapsed time reported completion before the campaign was actually ready");
        Require(slow.DisplayedPercent is >= 60 and <= 75,
            "a slow operation did not track its real completed-stage fraction");
        slow.Advance(.1, 10.1, 1, true, true);
        Require(slow.CanDismiss && slow.DisplayedPercent == 100,
            "a slow load stayed open after real readiness and the minimum interval");

        var failed = new CampaignLoadingTimeline();
        failed.Advance(9, 9, 1, true, false, failed: true);
        Require(!failed.CanDismiss && failed.DisplayedPercent < 100,
            "a failed load falsely displayed completion");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
