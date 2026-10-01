// Coordinator phase-cost profile — the measurement half of the
// simulation-LOD workstream. The parity oracle proves a demotion is
// *safe*; this profile reports which phases are *worth* demoting by
// timing each of the 12 coordinator phase tasks over seeded strategic
// steps through the coordinator's own PerformanceCounter plumbing.
//
// Run: stellar_campaign_phase_profile_tests <catalog.json>
// Prints per-phase mean/max and the phase's share of step wall time.

#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: campaign_phase_profile_tests <catalog.json>");
    const auto catalog = load_nearby_catalog(argv[1]);
    require(!catalog.empty(), "catalog must not be empty");

    PersistableFreshCampaignOptions seed_options{};
    seed_options.created_at_utc = "2026-01-01T00:00:00Z";
    seed_options.system_count = 500;
    seed_options.pre_warp_civilization_count = 6;
    seed_options.ancient_civilization_count = 1;
    auto campaign = seed_persistable_fresh_campaign(900913, catalog,
                                                  seed_options);
    CampaignSimulationState state(std::move(campaign));
    GalaxySimulationStepCoordinator coordinator{};
    coordinator.set_profiling_enabled(true);

    constexpr int kSteps = 8;
    std::vector<std::uint64_t> step_ns;
    step_ns.reserve(kSteps);
    for (int i = 0; i < kSteps; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      (void)coordinator.advance(&state, 1.0);
      step_ns.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - t0)
              .count()));
    }

    const auto &counters = coordinator.performance_counters();
    std::uint64_t phase_total = 0;
    for (const auto &counter : counters)
      phase_total += counter.total_nanoseconds;
    require(phase_total > 0, "profiling must record phase samples");

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "campaign phase profile: " << kSteps
              << " strategic steps x 1.0 day, 500 systems / 7 civs\n";
    std::cout << "  phase                mean_ms    max_ms   share\n";
    for (std::size_t i = 0;
         i < GalaxySimulationStepCoordinator::phase_names.size(); ++i) {
      const auto &counter = counters[i];
      require(counter.samples == kSteps,
              std::string(GalaxySimulationStepCoordinator::phase_names[i]) +
                  " must have one sample per step");
      const double mean_ms =
          static_cast<double>(counter.total_nanoseconds) /
          static_cast<double>(counter.samples) / 1e6;
      const double max_ms =
          static_cast<double>(counter.maximum_nanoseconds) / 1e6;
      const double share =
          100.0 * static_cast<double>(counter.total_nanoseconds) /
          static_cast<double>(phase_total);
      std::cout << "  " << std::left << std::setw(20)
                << GalaxySimulationStepCoordinator::phase_names[i]
                << std::right << std::setw(8) << mean_ms << std::setw(10)
                << max_ms << std::setw(7) << share << "%\n";
    }
    const auto mean_step =
        std::accumulate(step_ns.begin(), step_ns.end(), std::uint64_t{0}) /
        step_ns.size();
    std::cout << "  step wall mean " << std::setw(8)
              << static_cast<double>(mean_step) / 1e6 << " ms\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "campaign phase profile failure: " << e.what() << "\n";
    return 1;
  }
}
