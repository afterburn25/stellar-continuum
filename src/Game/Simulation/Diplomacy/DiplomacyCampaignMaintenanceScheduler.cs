using System;

namespace Game.Simulation.Diplomacy;

public sealed record DiplomacyCampaignMaintenancePolicy(
    long ReviewIntervalTicks,
    long ContactStaleAfterTicks,
    long ProposalLifetimeTicks)
{
    public static DiplomacyCampaignMaintenancePolicy EarlyReleaseDefault { get; } = new(
        DiplomacyCampaignClock.TicksForWholeDays(1),
        DiplomacyCampaignClock.TicksForWholeDays(90),
        DiplomacyCampaignClock.TicksForWholeDays(30));

    public void Validate()
    {
        if (ReviewIntervalTicks <= 0)
            throw new ArgumentOutOfRangeException(nameof(ReviewIntervalTicks));
        if (ContactStaleAfterTicks <= 0)
            throw new ArgumentOutOfRangeException(nameof(ContactStaleAfterTicks));
        if (ProposalLifetimeTicks <= 0)
            throw new ArgumentOutOfRangeException(nameof(ProposalLifetimeTicks));
    }
}

public sealed record DiplomacyCampaignMaintenanceResult(
    bool Ran,
    long ReviewTick,
    DiplomaticContactAgingResult ContactAging,
    DiplomaticProposalLifecycleReviewResult ProposalLifecycle)
{
    public static DiplomacyCampaignMaintenanceResult NotDue(long nowTick) => new(
        false,
        nowTick,
        new DiplomaticContactAgingResult(0, 0),
        new DiplomaticProposalLifecycleReviewResult(0, 0));
}

/// <summary>
/// Campaign-level low-frequency Diplomacy maintenance. The scheduler performs only a scalar
/// cadence check on ordinary frames. Bounded contact/proposal scans occur when a review is due.
/// Reviews are idempotent at a given tick: stale contacts and expired proposals transition only
/// once, so the scheduler checkpoint itself does not need to be persisted in save v9.
/// </summary>
public sealed class DiplomacyCampaignMaintenanceScheduler
{
    private readonly DiplomacyState _state;
    private readonly DiplomacyCampaignMaintenancePolicy _policy;
    private long _nextReviewTick;
    private long _lastReviewTick = -1;
    private bool _initialized;

    public DiplomacyCampaignMaintenanceScheduler(
        DiplomacyState state,
        DiplomacyCampaignMaintenancePolicy? policy = null)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));
        _policy = policy ?? DiplomacyCampaignMaintenancePolicy.EarlyReleaseDefault;
        _policy.Validate();
    }

    public long NextReviewTick => _nextReviewTick;
    public long LastReviewTick => _lastReviewTick;

    /// <summary>
    /// Reinitializes transient cadence state after a campaign is created or loaded. The next
    /// frame may review immediately so an overdue persisted proposal/contact is not hidden for
    /// an extra interval after loading.
    /// </summary>
    public void Reset(long nowTick, bool reviewImmediately = true)
    {
        if (nowTick < 0)
            throw new ArgumentOutOfRangeException(nameof(nowTick));

        _lastReviewTick = -1;
        _nextReviewTick = reviewImmediately
            ? nowTick
            : SaturatingAdd(nowTick, _policy.ReviewIntervalTicks);
        _initialized = true;
    }

    public DiplomacyCampaignMaintenanceResult ReviewIfDue(long nowTick)
    {
        if (nowTick < 0)
            throw new ArgumentOutOfRangeException(nameof(nowTick));

        if (!_initialized)
            Reset(nowTick, reviewImmediately: true);

        if (nowTick < _nextReviewTick || nowTick == _lastReviewTick)
            return DiplomacyCampaignMaintenanceResult.NotDue(nowTick);

        var contactAging = new DiplomaticContactAgingService(_state).Review(
            nowTick,
            _policy.ContactStaleAfterTicks);
        var proposalLifecycle = new DiplomaticProposalLifecycleService(_state).Review(
            nowTick,
            _policy.ProposalLifetimeTicks);

        _lastReviewTick = nowTick;
        _nextReviewTick = SaturatingAdd(nowTick, _policy.ReviewIntervalTicks);

        return new DiplomacyCampaignMaintenanceResult(
            true,
            nowTick,
            contactAging,
            proposalLifecycle);
    }

    private static long SaturatingAdd(long value, long increment)
    {
        if (increment <= 0)
            throw new ArgumentOutOfRangeException(nameof(increment));
        return value > long.MaxValue - increment ? long.MaxValue : value + increment;
    }
}
