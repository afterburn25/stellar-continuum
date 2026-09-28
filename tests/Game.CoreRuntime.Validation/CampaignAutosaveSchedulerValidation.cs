using System;
using System.Runtime.CompilerServices;
using Game.Campaign;

namespace Game.CoreRuntime.Validation;

internal static class CampaignAutosaveSchedulerValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateCadenceAndLargeTimeJumps();
        ValidateFailureBackoff();
        ValidateResetAndManualSaveSemantics();
        ValidateInvalidInputs();
        Console.WriteLine("PASS: scheduled campaign autosave cadence and retry backoff");
    }

    private static void ValidateCadenceAndLargeTimeJumps()
    {
        var scheduler = new CampaignAutosaveScheduler();
        scheduler.Reset(0.0);

        RequireNear(scheduler.NextDueDay, 30.0, "new campaign did not schedule autosave 30 days ahead");
        Require(!scheduler.IsDue(29.999), "autosave became due before the interval elapsed");
        Require(scheduler.IsDue(30.0), "autosave was not due at the interval boundary");

        scheduler.MarkSuccess(30.0);
        RequireNear(scheduler.NextDueDay, 60.0, "successful autosave did not schedule the next interval");
        Require(!scheduler.IsDue(30.0), "successful autosave remained due at the same simulation time");

        Require(scheduler.IsDue(95.0), "large simulation-time jump did not preserve the overdue autosave");
        scheduler.MarkSuccess(95.0);
        RequireNear(scheduler.NextDueDay, 125.0, "large jump attempted catch-up boundaries instead of one new interval");
        Require(!scheduler.IsDue(95.0), "large-jump autosave would repeat in the same frame");
    }

    private static void ValidateFailureBackoff()
    {
        var scheduler = new CampaignAutosaveScheduler();
        scheduler.Reset(0.0);
        Require(scheduler.IsDue(30.0), "failure test did not reach the first due point");

        scheduler.MarkFailure(30.0);
        RequireNear(scheduler.NextDueDay, 31.0, "failed autosave did not back off by one simulation day");
        Require(!scheduler.IsDue(30.999), "failed autosave would hammer persistence before retry delay");
        Require(scheduler.IsDue(31.0), "failed autosave did not become retryable after backoff");

        scheduler.MarkSuccess(31.0);
        RequireNear(scheduler.NextDueDay, 61.0, "successful retry did not restore normal cadence");
    }

    private static void ValidateResetAndManualSaveSemantics()
    {
        var scheduler = new CampaignAutosaveScheduler();

        scheduler.Reset(95.0);
        RequireNear(scheduler.NextDueDay, 125.0, "loaded campaign did not defer autosave by a full interval");

        scheduler.Reset(30.0);
        RequireNear(scheduler.NextDueDay, 60.0, "exact-boundary load incorrectly triggered immediate autosave");

        scheduler.MarkSuccess(42.5);
        RequireNear(scheduler.NextDueDay, 72.5, "manual successful save did not reset autosave cadence");
    }

    private static void ValidateInvalidInputs()
    {
        RequireThrows(() => new CampaignAutosaveScheduler(new CampaignAutosavePolicy(0.0, 1.0)), "zero interval was accepted");
        RequireThrows(() => new CampaignAutosaveScheduler(new CampaignAutosavePolicy(30.0, double.NaN)), "invalid retry delay was accepted");

        var scheduler = new CampaignAutosaveScheduler();
        RequireThrows(() => scheduler.Reset(-1.0), "negative reset time was accepted");
        RequireThrows(() => scheduler.IsDue(double.NaN), "NaN due-check time was accepted");
        RequireThrows(() => scheduler.MarkSuccess(double.PositiveInfinity), "infinite success time was accepted");
        RequireThrows(() => scheduler.MarkFailure(-0.01), "negative failure time was accepted");
    }

    private static void RequireThrows(Action action, string message)
    {
        try
        {
            action();
        }
        catch (ArgumentOutOfRangeException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
