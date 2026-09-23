#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <stellar/engine/foundation.hpp>
#include <stellar/engine/simulation_scheduler.hpp>

namespace stellar::engine {

// SimulationExecutor — the civilization-scale step driver built on
// SimulationScheduler's cadence/LOD core.
//
// Model: owners register one SimulationTask per simulatable thing (a
// star system, colony, fleet, economy node, AI planner — anything keyed
// by a stable uint64). Each advance() is one authoritative tick:
//
//   1. eligibility — items due at their tier cadence, plus dirty and
//      event-woken items (woken Dormant items run once with their
//      accumulated dormant elapsed ticks — Dormant is the
//      event-driven tier);
//   2. ordering — topological over each task's depends_on keys
//      (dependencies that are not due this tick do not gate; they only
//      order concurrently-due work), ties broken by JobPriority then
//      key — always deterministic;
//   3. budget — wall-time and/or task-count caps; deferred items are
//      simply not marked ran, so their elapsed keeps accumulating and
//      they re-enter eligibility next tick (catch-up is delivered as
//      elapsed_ticks > 1 — coarse tasks must integrate it);
//   4. execution — inline (advance) or JobSystem waves
//      (advance_parallel).
//
// Determinism contract: eligibility, ordering, elapsed accounting and
// deferral are fully deterministic. In advance_parallel, tasks in the
// same dependency wave run concurrently — task bodies must mutate only
// state owned by their own key (or publish to owner-managed merge
// buffers); the executor never merges results itself. Time
// acceleration/pause-at-speed belong to the caller's clock
// (core StrategicClock); the executor only sees ticks.

struct SimulationTickContext {
    Tick tick{};
    std::uint64_t elapsed_ticks{1}; // ticks since this task last ran
    SimulationTier tier{};
    bool event_wake{};              // ran because of wake()/dirty, not cadence
};

struct SimulationTask {
    std::function<void(const SimulationTickContext&)> run;
    SimulationTier tier{SimulationTier::Normal};
    JobPriority priority{JobPriority::Normal};
    std::string domain; // diagnostic grouping: "economy", "colony", ...
    // Keys that must run before this task when both are eligible in the
    // same tick. Ordering-only — a dependency not due this tick does not
    // hold this task back.
    std::vector<std::uint64_t> depends_on;
};

struct SimulationBudget {
    std::chrono::nanoseconds max_wall_time{0}; // 0 = unbounded
    std::size_t max_tasks{0};                  // 0 = unbounded
};

struct SimulationDomainStats {
    std::uint64_t runs{};
    std::uint64_t total_ns{};
    std::uint64_t max_ns{};
    std::uint64_t last_tick_ns{};
};

struct SimulationStepReport {
    Tick tick{};
    bool paused{};
    std::size_t eligible{};
    std::size_t ran{};
    std::size_t deferred{};
    std::size_t dirty_wakeups{};
    std::size_t event_wakeups{};
    std::uint64_t wall_ns{};
    std::size_t jobs_submitted{};
};

class SimulationExecutor {
public:
    using Key = SimulationScheduler::Key;

    explicit SimulationExecutor(SimulationTierPolicy policy = {});

    void add(Key key, SimulationTask task); // throws on duplicate/empty
    void remove(Key key);
    [[nodiscard]] bool contains(Key key) const;
    [[nodiscard]] std::size_t size() const noexcept;
    void clear();

    // LOD promotion/demotion between tiers.
    void set_tier(Key key, SimulationTier tier);
    [[nodiscard]] SimulationTier tier(Key key) const;
    // Bulk re-tier: fn(key, current_tier) -> new tier. Iterates keys in
    // sorted order so promotion decisions are deterministic.
    template <class F> void evaluate_tiers(F&& fn) {
        std::vector<Key> keys;
        keys.reserve(tasks_.size());
        for (const auto& [key, _] : tasks_) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        for (const Key key : keys) {
            const SimulationTier next = fn(key, tasks_.at(key).tier);
            if (next != tasks_.at(key).tier) set_tier(key, next);
        }
    }

    // Dirty wakeup: the item runs once at the next advance regardless of
    // cadence, then the flag clears. Event wakeup: wake() schedules a
    // single out-of-cadence run; wake_domain() wakes every task in a
    // domain; wake_all() schedules a full catch-up tick.
    void mark_dirty(Key key);
    void clear_dirty(Key key);
    void wake(Key key);
    void wake_domain(std::string_view domain);
    void wake_all();
    [[nodiscard]] bool dirty(Key key) const;

    void set_paused(bool paused) noexcept { paused_ = paused; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }

    [[nodiscard]] Tick tick() const noexcept { return scheduler_.tick(); }
    [[nodiscard]] std::uint64_t dormant_elapsed(Key key) const;
    void dormant_consumed(Key key);
    // All dormant items and their accumulated ticks — for bulk analytic
    // propagation passes (e.g. background economy catch-up).
    [[nodiscard]] std::vector<std::pair<Key, std::uint64_t>> dormant_items() const;

    SimulationStepReport advance(SimulationBudget budget = {});
    SimulationStepReport advance_parallel(JobSystem& jobs,
                                          SimulationBudget budget = {});

    [[nodiscard]] const SimulationDomainStats* domain_stats(
        std::string_view domain) const;
    [[nodiscard]] std::vector<std::string> domains() const;
    // Recent per-tick wall times (ns), oldest first — percentile source.
    [[nodiscard]] const std::deque<std::uint64_t>& tick_history() const;
    [[nodiscard]] std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)>
        tier_counts() const;
    [[nodiscard]] std::uint64_t total_wakeups() const noexcept { return total_wakeups_; }
    [[nodiscard]] const SimulationScheduler& scheduler() const noexcept { return scheduler_; }

    // --- persistence -------------------------------------------------
    // Serializable executor state: scheduler bookkeeping plus pending
    // dirty/event wakeups and the pause flag. Task callbacks are code —
    // the owner re-registers tasks after load, then calls restore_state.
    // Diagnostic counters (domain stats, tick history, wakeup totals)
    // are intentionally not persisted; they are observability, not
    // authoritative simulation state.
    struct State {
        std::uint32_t version{1};
        SimulationScheduler::State scheduler;
        std::vector<Key> dirty; // sorted
        std::vector<Key> wake;  // sorted
        bool paused{};
    };
    [[nodiscard]] State capture_state() const;
    // Applies scheduler/wakeup/pause state. Tasks must already be
    // registered — snapshot keys with no registered task are skipped and
    // returned (sorted) so the caller can flag migration concerns;
    // dirty/wake flags for unregistered keys are dropped rather than
    // parked, because plan_tick would fail on a taskless key.
    std::vector<Key> restore_state(const State& state);

private:
    // One eligible execution unit for this tick.
    struct WorkItem {
        Key key;
        std::uint64_t elapsed;
        SimulationTier tier;
        JobPriority priority;
        bool event_wake;   // carried into the tick context
        bool dirty_flag;   // ran with the dirty flag set
        bool dormant;
    };
    // Ordered eligible work for the current tick: topo over depends_on,
    // ties by (priority, key). begin_tick must already have run.
    std::vector<WorkItem> plan_tick();
    void finish_item(const WorkItem& item, std::uint64_t task_ns);

    SimulationScheduler scheduler_;
    std::unordered_map<Key, SimulationTask> tasks_;
    std::unordered_set<Key> dirty_;
    std::unordered_set<Key> wake_;
    bool paused_{};
    std::unordered_map<std::string, SimulationDomainStats> domain_stats_;
    std::array<std::size_t, static_cast<std::size_t>(SimulationTier::Count)> tier_runs_{};
    std::deque<std::uint64_t> tick_history_;
    std::uint64_t total_wakeups_{};
};

} // namespace stellar::engine
