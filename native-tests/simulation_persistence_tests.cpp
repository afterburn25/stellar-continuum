#include <stellar/engine/simulation_executor.hpp>

#include <cmath>
#include <iostream>
#include <map>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;
using Key = SimulationExecutor::Key;

SimulationTierPolicy fast_policy() {
    SimulationTierPolicy p;
    p.periods = {1, 2, 4, 8, 0}; // Active..Dormant
    return p;
}

} // namespace

int main() {
    // Scheduler state round-trip: tiers, tick, elapsed bookkeeping and
    // dormant accumulation survive capture/restore exactly.
    {
        SimulationScheduler sched(fast_policy());
        sched.add(1, SimulationTier::Active);
        sched.add(2, SimulationTier::Normal);
        sched.add(3, SimulationTier::Dormant);
        sched.advance(); // tick 1 — 1 runs
        sched.advance(); // tick 2 — 1 runs
        const auto state = sched.capture_state();

        SimulationScheduler restored(fast_policy());
        restored.restore_state(state);
        check(restored.tick() == sched.tick(), "tick restored");
        check(restored.tier(3) == SimulationTier::Dormant, "tier restored");
        check(restored.dormant_elapsed(3) == sched.dormant_elapsed(3),
              "dormant elapsed preserved");
        check(restored.elapsed_since_run(2) == sched.elapsed_since_run(2),
              "elapsed preserved");

        // Continue both in lockstep: identical due lists.
        for (int i = 0; i < 8; ++i) {
            const auto a = sched.advance();
            const auto b = restored.advance();
            check(a == b, "restored scheduler advances identically");
        }
    }

    // Executor round-trip: run a mixed-tier executor, capture, rebuild a
    // fresh executor over re-registered tasks, restore, and verify both
    // produce identical run sequences afterwards.
    {
        std::map<SimulationExecutor::Key, std::uint64_t> acc_a;
        std::vector<SimulationExecutor::Key> order_a;
        const auto make_tasks = [&](SimulationExecutor& exec,
                                    std::map<Key, std::uint64_t>& acc,
                                    std::vector<Key>* order) {
            const std::pair<Key, SimulationTier> spec[] = {
                {10, SimulationTier::Active},
                {20, SimulationTier::Nearby},
                {30, SimulationTier::Normal},
                {40, SimulationTier::Background},
                {50, SimulationTier::Dormant},
            };
            for (const auto& [key, tier] : spec) {
                SimulationTask t;
                t.tier = tier;
                t.domain = "test";
                t.run = [key, &acc, order](const SimulationTickContext& ctx) {
                    acc[key] += ctx.elapsed_ticks;
                    if (order) order->push_back(key);
                };
                exec.add(key, std::move(t));
            }
        };

        SimulationExecutor a(fast_policy());
        make_tasks(a, acc_a, &order_a);
        for (int i = 0; i < 6; ++i) a.advance();
        a.mark_dirty(30);   // pending dirty wakeup at save time
        a.wake(50);         // pending event wakeup for a dormant task
        a.set_paused(false);
        const auto state = a.capture_state();
        // Compare only post-restore behaviour - the accumulators hold
        // a's pre-capture ticks otherwise.
        acc_a.clear();
        order_a.clear();

        std::map<SimulationExecutor::Key, std::uint64_t> acc_b;
        std::vector<SimulationExecutor::Key> order_b;
        SimulationExecutor b(fast_policy());
        make_tasks(b, acc_b, &order_b);
        const auto unmatched = b.restore_state(state);
        check(unmatched.empty(), "all snapshot keys matched");
        check(b.dirty(30), "dirty flag restored");
        check(b.tick() == a.tick(), "executor tick restored");

        for (int i = 0; i < 12; ++i) {
            const auto ra = a.advance();
            const auto rb = b.advance();
            check(ra.eligible == rb.eligible && ra.ran == rb.ran &&
                      ra.dirty_wakeups == rb.dirty_wakeups &&
                      ra.event_wakeups == rb.event_wakeups,
                  "restored executor reports identical");
        }
        check(order_a == order_b, "identical run order after restore");
        check(acc_a == acc_b, "identical elapsed accumulation after restore");
        // The pending dirty + wake fired on the first post-restore tick.
        check(acc_b.at(30) > 0 && acc_b.at(50) > 0, "pending wakeups ran");
    }

    // Unmatched snapshot keys are reported, not applied — a taskless key
    // must not enter the scheduler where plan_tick would fail on it.
    {
        SimulationExecutor a(fast_policy());
        std::uint64_t runs = 0;
        SimulationTask t;
        t.tier = SimulationTier::Active;
        t.run = [&](const SimulationTickContext&) { ++runs; };
        a.add(1, t);
        a.add(2, t);
        const auto state = a.capture_state();

        SimulationExecutor b(fast_policy());
        b.add(1, t); // only key 1 re-registered — 2 was removed before load
        const auto unmatched = b.restore_state(state);
        check(unmatched == std::vector<SimulationExecutor::Key>{2},
              "unmatched key reported");
        check(!b.scheduler().contains(2), "taskless key not scheduled");
        b.advance();
        check(runs == 1, "only registered task ran after partial restore");
    }

    // Pause survives the round-trip.
    {
        SimulationExecutor a(fast_policy());
        SimulationTask t;
        t.tier = SimulationTier::Active;
        t.run = [](const SimulationTickContext&) {};
        a.add(1, t);
        a.set_paused(true);
        SimulationExecutor b(fast_policy());
        b.add(1, t);
        b.restore_state(a.capture_state());
        check(b.paused(), "pause restored");
        check(b.advance().paused, "restored executor stays paused");
    }

    if (failures != 0) {
        std::cerr << failures << " simulation persistence checks failed\n";
        return 1;
    }
    std::cout << "SimulationExecutor persistence round-trip tests passed\n";
    return 0;
}
