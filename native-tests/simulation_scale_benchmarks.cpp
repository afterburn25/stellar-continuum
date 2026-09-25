// SimulationExecutor scale benchmark harness (simulation_scale_* ctest
// entries). argv[1] = registered system tasks, argv[2] = ticks.
//
// Models one simulation task per star system across a plausible LOD
// distribution — 2% Active, 8% Nearby, 30% Normal, 40% Background,
// 20% Dormant — over four domains (economy/colony/fleet/population).
// Each task runs deterministic own-slot work scaled by elapsed ticks;
// periodic domain wakes and full wake_all catch-ups exercise the
// event-driven path. The same registration runs serially and through
// advance_parallel on the JobSystem — final state checksums must match.
//
// Reports per mode: mean/p50/p95/p99/peak tick wall time, jobs
// submitted/completed, wakeup count, tier distribution, per-domain
// run totals, and task-state bytes.

#include <stellar/engine/simulation_executor.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using stellar::engine::JobPriority;
using stellar::engine::JobSystem;
using stellar::engine::SimulationExecutor;
using stellar::engine::SimulationTask;
using stellar::engine::SimulationTier;
using stellar::engine::SimulationTierPolicy;

// Deterministic per-system workload: 8 lanes of FNV-ish mixing scaled by
// elapsed — stands in for a coarse simulation tick without pulling real
// game systems into the benchmark.
struct BenchSystem {
    std::uint64_t lanes[8]{};
};

void register_systems(SimulationExecutor& exec, std::vector<BenchSystem>& state,
                      std::size_t n) {
    static const char* kDomains[] = {"economy", "colony", "fleet", "population"};
    for (std::uint64_t k = 0; k < n; ++k) {
        SimulationTask t;
        const std::uint64_t bucket = k % 100;
        t.tier = bucket < 2    ? SimulationTier::Active
                 : bucket < 10 ? SimulationTier::Nearby
                 : bucket < 40 ? SimulationTier::Normal
                 : bucket < 80 ? SimulationTier::Background
                               : SimulationTier::Dormant;
        t.domain = kDomains[k % 4];
        if (k % 10 == 0 && k > 0) t.depends_on = {k / 10};
        t.priority = k % 5 == 0 ? JobPriority::High : JobPriority::Normal;
        t.run = [&state, k](const auto& ctx) {
            auto& s = state[k];
            for (int lane = 0; lane < 8; ++lane)
                s.lanes[lane] =
                    s.lanes[lane] * 1099511628211ull ^
                    (ctx.elapsed_ticks + k * 2654435761ull +
                     static_cast<std::uint64_t>(lane));
        };
        exec.add(k, t);
    }
}

void summarize(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto at = [&](double q) {
        return values.empty()
                   ? 0.0
                   : values[std::min<std::size_t>(
                         values.size() - 1,
                         static_cast<std::size_t>(q * values.size()))];
    };
    const double mean =
        std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(std::max<std::size_t>(values.size(), 1));
    std::cout << "mean_us=" << mean / 1e3 << " p50_us=" << at(.5) / 1e3
              << " p95_us=" << at(.95) / 1e3 << " p99_us=" << at(.99) / 1e3
              << " peak_us=" << (values.empty() ? 0.0 : values.back()) / 1e3;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: simulation_scale_benchmarks <systems> [ticks]\n";
        return 1;
    }
    const std::size_t systems = std::stoull(argv[1]);
    const int ticks = argc >= 3 ? std::stoi(argv[2]) : 120;

    SimulationTierPolicy policy;
    policy.periods = {1, 2, 4, 16, 0};
    SimulationExecutor serial{policy}, parallel{policy};
    JobSystem jobs;
    std::vector<BenchSystem> state_serial(systems), state_parallel(systems);
    register_systems(serial, state_serial, systems);
    register_systems(parallel, state_parallel, systems);

    std::vector<double> serial_ns, parallel_ns;
    serial_ns.reserve(static_cast<std::size_t>(ticks));
    parallel_ns.reserve(static_cast<std::size_t>(ticks));
    for (int i = 0; i < ticks; ++i) {
        if (i % 10 == 3) {
            serial.wake_domain("fleet");
            parallel.wake_domain("fleet");
        }
        if (i % 25 == 11) {
            serial.wake_all();
            parallel.wake_all();
        }
        serial_ns.push_back(
            static_cast<double>(serial.advance().wall_ns));
        parallel_ns.push_back(
            static_cast<double>(parallel.advance_parallel(jobs).wall_ns));
    }

    std::uint64_t checksum_a = 0, checksum_b = 0;
    for (std::size_t k = 0; k < systems; ++k)
        for (int lane = 0; lane < 8; ++lane) {
            checksum_a ^= state_serial[k].lanes[lane] + lane;
            checksum_b ^= state_parallel[k].lanes[lane] + lane;
        }
    if (checksum_a != checksum_b) {
        std::cerr << "FAIL: serial and parallel benchmark states diverged\n";
        return 1;
    }

    const auto stats = jobs.stats();
    const auto tiers = serial.tier_counts();
    std::cout << "systems=" << systems << " ticks=" << ticks << " serial[";
    summarize(std::move(serial_ns));
    std::cout << "] parallel[";
    summarize(std::move(parallel_ns));
    std::cout << "] jobs_submitted=" << stats.submitted
              << " jobs_completed=" << stats.completed
              << " wakeups=" << serial.total_wakeups()
              << " tiers=[active:" << tiers[0] << " nearby:" << tiers[1]
              << " normal:" << tiers[2] << " background:" << tiers[3]
              << " dormant:" << tiers[4]
              << "] task_state_bytes=" << systems * sizeof(BenchSystem);
    for (const auto& domain : serial.domains()) {
        const auto* st = serial.domain_stats(domain);
        std::cout << " " << domain << "_runs=" << st->runs << " " << domain
                  << "_ns=" << st->total_ns;
    }
    std::cout << '\n';
    return 0;
}
