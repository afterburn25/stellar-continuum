#include <stellar/engine/simulation_scheduler.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool due(const std::vector<stellar::engine::SimulationScheduler::Key>& v,
         stellar::engine::SimulationScheduler::Key key) {
    return std::find(v.begin(), v.end(), key) != v.end();
}

} // namespace

int main() {
    using namespace stellar::engine;

    // Tier cadence: Active every tick, Nearby every 2, Normal every 4,
    // Background every 16 (default policy).
    {
        SimulationScheduler scheduler;
        scheduler.add(1, SimulationTier::Active);
        scheduler.add(2, SimulationTier::Nearby);
        scheduler.add(3, SimulationTier::Normal);
        scheduler.add(4, SimulationTier::Background);
        scheduler.add(5, SimulationTier::Dormant);

        int active_runs = 0, nearby_runs = 0, normal_runs = 0, background_runs = 0;
        for (int t = 0; t < 32; ++t) {
            const auto due_now = scheduler.advance();
            active_runs += due(due_now[0], 1) ? 1 : 0;
            nearby_runs += due(due_now[1], 2) ? 1 : 0;
            normal_runs += due(due_now[2], 3) ? 1 : 0;
            background_runs += due(due_now[3], 4) ? 1 : 0;
            check(due_now[4].empty(), "dormant never appears in due lists");
        }
        check(active_runs == 32, "active tier runs every tick");
        check(nearby_runs == 16, "nearby tier runs every 2 ticks");
        check(normal_runs == 8, "normal tier runs every 4 ticks");
        check(background_runs == 2, "background tier runs every 16 ticks");
    }

    // Dormant accumulation: elapsed time accrues and resets on consume.
    {
        SimulationScheduler scheduler;
        scheduler.add(9, SimulationTier::Active);
        scheduler.advance();
        scheduler.set_tier(9, SimulationTier::Dormant);
        for (int i = 0; i < 10; ++i) scheduler.advance();
        check(scheduler.dormant_elapsed(9) == 10, "dormant elapsed accumulates");
        scheduler.dormant_consumed(9);
        scheduler.advance();
        check(scheduler.dormant_elapsed(9) == 1, "consume resets accumulation");
        scheduler.set_tier(9, SimulationTier::Active);
        check(scheduler.dormant_elapsed(9) == 0, "leaving dormant clears accumulation");
    }

    // Re-tiering mid-run preserves the item; remove/unknown checks.
    {
        SimulationScheduler scheduler;
        scheduler.add(3, SimulationTier::Normal);
        scheduler.set_tier(3, SimulationTier::Active);
        check(scheduler.tier(3) == SimulationTier::Active, "set_tier changes tier");
        const auto due_now = scheduler.advance();
        check(due(due_now[0], 3), "re-tiered item runs at new cadence");
        scheduler.remove(3);
        check(!scheduler.contains(3), "removed item is gone");
        bool threw = false;
        try {
            scheduler.set_tier(99, SimulationTier::Active);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "re-tier of unknown key throws");
        bool bad_tier = false;
        try {
            scheduler.add(7, static_cast<SimulationTier>(9));
        } catch (const std::invalid_argument&) {
            bad_tier = true;
        }
        check(bad_tier, "out-of-range tier rejected");
    }

    // Custom policy + tier counts.
    {
        SimulationTierPolicy policy;
        policy.periods = {1, 1, 3, 5, 0};
        SimulationScheduler scheduler{policy};
        scheduler.add(1, SimulationTier::Normal);
        scheduler.add(2, SimulationTier::Normal);
        scheduler.add(3, SimulationTier::Dormant);
        const auto counts = scheduler.tier_counts();
        check(counts[2] == 2 && counts[4] == 1, "tier_counts reflects membership");
        int runs = 0;
        for (int i = 0; i < 15; ++i) {
            const auto due_now = scheduler.advance();
            runs += due(due_now[2], 1) ? 1 : 0;
        }
        check(runs == 5, "custom period honored");
    }

    // Zero period (non-dormant) is rejected by the policy.
    {
        SimulationTierPolicy bad;
        bad.periods[1] = 0;
        bool threw = false;
        try {
            SimulationScheduler scheduler{bad};
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "zero non-dormant period rejected");
    }

    if (failures != 0) {
        std::cerr << failures << " scheduler checks failed\n";
        return 1;
    }
    std::cout << "SimulationScheduler tier cadence and dormant tests passed\n";
    return 0;
}
