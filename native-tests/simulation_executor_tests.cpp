#include <stellar/engine/simulation_executor.hpp>

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using stellar::engine::SimulationExecutor;
using stellar::engine::SimulationTask;
using stellar::engine::SimulationTier;
using stellar::engine::SimulationTickContext;

SimulationTask task(SimulationTier tier, std::string domain,
                    std::function<void(const SimulationTickContext&)> run) {
    SimulationTask t;
    t.run = std::move(run);
    t.tier = tier;
    t.domain = std::move(domain);
    return t;
}

} // namespace

int main() {
    using namespace stellar::engine;

    // Cadence: tasks fire at their tier's period and see elapsed_ticks.
    {
        SimulationExecutor exec;
        int active = 0, background = 0;
        std::uint64_t last_bg_elapsed = 0;
        exec.add(1, task(SimulationTier::Active, "sys",
                         [&](const SimulationTickContext& c) {
                             ++active;
                             check(c.elapsed_ticks == 1, "active elapsed is 1");
                         }));
        exec.add(2, task(SimulationTier::Background, "sys",
                         [&](const SimulationTickContext& c) {
                             ++background;
                             last_bg_elapsed = c.elapsed_ticks;
                         }));
        for (int i = 0; i < 32; ++i) exec.advance();
        check(active == 32, "active task runs every tick");
        check(background == 2, "background task runs every 16 ticks");
        check(last_bg_elapsed == 16, "coarse task sees accumulated elapsed");
    }

    // Dirty wakeup: out-of-cadence single run, flag clears.
    {
        SimulationExecutor exec;
        int runs = 0;
        std::uint64_t wake_elapsed = 0;
        bool saw_event = false;
        exec.add(5, task(SimulationTier::Background, "econ",
                         [&](const SimulationTickContext& c) {
                             ++runs;
                             wake_elapsed = c.elapsed_ticks;
                             saw_event = c.event_wake;
                         }));
        exec.advance(); // registered at tick 0: not due until tick 16
        check(runs == 0, "background task not due at tick 1");
        exec.mark_dirty(5);
        const auto report = exec.advance();
        check(runs == 1 && saw_event, "dirty task runs out of cadence");
        check(wake_elapsed == 2, "dirty run sees elapsed since registration");
        check(report.dirty_wakeups == 1, "report counts dirty wakeup");
        check(!exec.dirty(5), "dirty flag cleared after run");
        exec.advance();
        check(runs == 1, "no repeat run after dirty consumed");
    }

    // Event wake of a dormant task: runs once with dormant elapsed.
    {
        SimulationExecutor exec;
        int runs = 0;
        std::uint64_t elapsed = 0;
        exec.add(7, task(SimulationTier::Dormant, "econ",
                         [&](const SimulationTickContext& c) {
                             ++runs;
                             elapsed = c.elapsed_ticks;
                         }));
        for (int i = 0; i < 10; ++i) exec.advance();
        check(runs == 0, "dormant task never cadence-runs");
        check(exec.dormant_elapsed(7) == 10, "dormant elapsed accumulates");
        exec.wake(7);
        exec.advance();
        check(runs == 1 && elapsed == 11, "woken dormant runs with accumulated elapsed");
        check(exec.dormant_elapsed(7) == 0, "dormant run consumes accumulation");
        exec.advance();
        check(runs == 1, "dormant stays dormant after event run");
    }

    // Dependency ordering: dependent runs after dependency when both due;
    // ordering-only when the dependency is not due.
    {
        SimulationExecutor exec;
        std::vector<std::uint64_t> order;
        auto rec = [&](std::uint64_t key) {
            return task(SimulationTier::Active, "sys",
                        [&, key](const SimulationTickContext&) { order.push_back(key); });
        };
        auto dependent = rec(20);
        dependent.depends_on = {10};
        exec.add(20, std::move(dependent));
        exec.add(10, rec(10));
        exec.advance();
        check(order.size() == 2 && order[0] == 10 && order[1] == 20,
              "dependency runs before dependent");

        // Dependency at a coarser tier does not gate the dependent.
        SimulationExecutor exec2;
        int dependent_runs = 0;
        auto dep2 = task(SimulationTier::Active, "sys",
                         [&](const SimulationTickContext&) { ++dependent_runs; });
        dep2.depends_on = {30};
        exec2.add(31, dep2);
        exec2.add(30, task(SimulationTier::Background, "sys",
                           [](const SimulationTickContext&) {}));
        exec2.advance();
        check(dependent_runs == 1, "coarse dependency does not gate dependent");
    }

    // Determinism: identical construction produces identical execution order.
    {
        const auto run_sequence = [] {
            SimulationExecutor exec;
            std::vector<std::uint64_t> order;
            for (std::uint64_t key = 1; key <= 12; ++key) {
                auto t = task(key % 2 ? SimulationTier::Active : SimulationTier::Nearby,
                              "sys",
                              [&, key](const SimulationTickContext&) {
                                  order.push_back(key);
                              });
                if (key >= 4) t.depends_on = {key - 1};
                exec.add(key, std::move(t));
            }
            for (int i = 0; i < 8; ++i) exec.advance();
            return order;
        };
        check(run_sequence() == run_sequence(), "execution order is deterministic");
    }

    // Budget: max_tasks defers remainder; deferred items keep accumulating
    // elapsed and re-enter eligibility next tick.
    {
        SimulationExecutor exec;
        int a = 0, b = 0;
        std::uint64_t b_elapsed = 0;
        exec.add(1, task(SimulationTier::Active, "sys",
                         [&](const SimulationTickContext&) { ++a; }));
        exec.add(2, task(SimulationTier::Active, "sys",
                         [&](const SimulationTickContext& c) {
                             ++b;
                             b_elapsed = c.elapsed_ticks;
                         }));
        SimulationBudget budget;
        budget.max_tasks = 1;
        const auto r1 = exec.advance(budget);
        check(r1.eligible == 2 && r1.ran == 1 && r1.deferred == 1,
              "budget caps tasks per tick");
        const auto r2 = exec.advance(budget);
        // Aging: the deferred item (elapsed 2) outranks the item that ran
        // last tick (elapsed 1) — budget pressure cannot starve keys.
        check(r2.deferred == 1 && a == 1 && b == 1 && b_elapsed == 2,
              "still capped while backlog exists; most overdue wins");
        const auto r3 = exec.advance();
        check(r3.ran == 2 && b == 2, "uncapped tick drains backlog");
        check(b_elapsed == 1, "drained task ran with its current elapsed");
    }

    // Pause semantics.
    {
        SimulationExecutor exec;
        int runs = 0;
        exec.add(1, task(SimulationTier::Active, "sys",
                         [&](const SimulationTickContext&) { ++runs; }));
        exec.set_paused(true);
        const auto report = exec.advance();
        check(report.paused && runs == 0, "paused advance runs nothing");
        check(exec.tick() == 0, "paused advance does not tick the clock");
        exec.set_paused(false);
        exec.advance();
        check(runs == 1, "resume runs normally");
    }

    // Domain stats and wake_domain.
    {
        SimulationExecutor exec;
        int econ = 0, mil = 0;
        exec.add(1, task(SimulationTier::Active, "economy",
                         [&](const SimulationTickContext&) { ++econ; }));
        exec.add(2, task(SimulationTier::Dormant, "military",
                         [&](const SimulationTickContext&) { ++mil; }));
        exec.wake_domain("military");
        exec.advance();
        check(econ == 1 && mil == 1, "wake_domain wakes matching tasks");
        const auto* stats = exec.domain_stats("economy");
        check(stats && stats->runs == 1, "domain stats recorded");
        const auto domains = exec.domains();
        check(domains.size() == 2, "domains enumerated");
        check(exec.total_wakeups() == 1, "wakeup total recorded");
    }

    // evaluate_tiers: bulk promotion/demotion is deterministic.
    {
        SimulationExecutor exec;
        for (std::uint64_t key = 0; key < 10; ++key)
            exec.add(key, task(SimulationTier::Normal, "sys",
                               [](const SimulationTickContext&) {}));
        exec.evaluate_tiers([](SimulationExecutor::Key key, SimulationTier) {
            return key < 3 ? SimulationTier::Active : SimulationTier::Dormant;
        });
        const auto counts = exec.tier_counts();
        check(counts[static_cast<std::size_t>(SimulationTier::Active)] == 3,
              "evaluate_tiers promotes");
        check(counts[static_cast<std::size_t>(SimulationTier::Dormant)] == 7,
              "evaluate_tiers demotes");
    }

    // Parallel step: dependency waves respected, results identical to
    // sequential, jobs counted. Tasks write only their own slot and read
    // only earlier-wave dependency slots (executor determinism contract).
    {
        const auto run_mode = [](bool parallel) {
            JobSystem jobs{4};
            SimulationExecutor exec;
            std::vector<int> state(64, 0);
            for (std::uint64_t key = 0; key < 64; ++key) {
                const bool has_dep = key > 0 && key % 8 == 0;
                auto t = task(SimulationTier::Active, "sys",
                              [&, key, has_dep](const SimulationTickContext&) {
                                  state[key] = static_cast<int>(key) +
                                               (has_dep ? state[key - 8] : 0);
                              });
                if (has_dep) t.depends_on = {key - 8};
                exec.add(key, std::move(t));
            }
            const auto report =
                parallel ? exec.advance_parallel(jobs) : exec.advance();
            return std::pair{state, report};
        };
        const auto seq = run_mode(false);
        const auto par = run_mode(true);
        check(seq.first == par.first,
              "parallel step produces identical task state");
        check(par.second.ran == 64 && par.second.jobs_submitted == 64,
              "all tasks submitted as jobs");
        check(par.first[56] == 224,
              "dependency chain accumulates across waves");
    }

    // Error handling: duplicate/empty/unknown.
    {
        SimulationExecutor exec;
        bool threw = false;
        try {
            exec.add(1, SimulationTask{});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "empty task rejected");
        exec.add(1, task(SimulationTier::Active, "sys",
                         [](const SimulationTickContext&) {}));
        threw = false;
        try {
            exec.add(1, task(SimulationTier::Active, "sys",
                             [](const SimulationTickContext&) {}));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "duplicate key rejected");
        threw = false;
        try {
            exec.wake(99);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "wake of unknown key throws");
    }

    if (failures != 0) {
        std::cerr << failures << " executor checks failed\n";
        return 1;
    }
    std::cout << "SimulationExecutor cadence/wakeup/dependency/budget tests passed\n";
    return 0;
}
