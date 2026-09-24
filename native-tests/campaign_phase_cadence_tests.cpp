// Seeded parity oracle for coordinator phase cadence (simulation LOD).
//
// The gate for per-phase tier demotion: a captured GalaxyPayloadV16 JSON
// digest after every strategic step must stay byte-identical between the
// all-Active baseline and any adopted demotion policy. The oracle proves
// invariance per scenario — demotion of a phase that is inert for the
// whole trace is byte-exact by construction, while demoting a phase that
// mutates must (and does) diverge, which keeps the oracle honest.
//
// Run: stellar_campaign_phase_cadence_tests <catalog.json>

#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/galaxy_payload_json.hpp>
#include <stellar/core/galaxy_payload_persistence.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/engine/simulation_executor.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace stellar::core;
using stellar::engine::SimulationTier;

namespace {

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}

struct TracePlan {
  // (step index, phase) — wakes issued before that step's advance().
  std::vector<std::pair<int, std::string>> wakes;
  std::vector<std::pair<std::string, SimulationTier>> tiers;
  bool minimal_world{}; // no fleets/shipyards/construction projects
};

// Advances a freshly seeded campaign one strategic day per step and
// captures a byte-exact persistence digest after each — the oracle's
// comparison surface. Capture runs through the real Player16 payload
// path, so a digest covers every authoritative field the save boundary
// sees (and normalizes it the same way).
std::vector<std::string> run_trace(const std::vector<CatalogStar> &catalog,
                                   std::int64_t seed, int steps,
                                   const TracePlan &plan) {
  PersistableFreshCampaignOptions seed_options{};
  seed_options.created_at_utc = "2026-01-01T00:00:00Z";
  seed_options.system_count = 250;
  seed_options.pre_warp_civilization_count = 2;
  seed_options.ancient_civilization_count = 0;
  auto campaign = seed_persistable_fresh_campaign(seed, catalog,
                                                  seed_options);
  if (plan.minimal_world) {
    campaign.fleets.clear();
    // The per-civ state rows are required by the capture boundary —
    // neutralize their contents instead of dropping the rows.
    for (auto &construction : campaign.construction) {
      construction.active_project_id.reset();
      construction.active_project_progress = 0.0;
      construction.active_project_authorization_credits = 0.0;
      construction.queued_projects.clear();
    }
    for (auto &shipyard : campaign.shipyards) {
      shipyard.active_design_id.reset();
      shipyard.active_order_id.reset();
      shipyard.active_build_progress = 0.0;
      shipyard.active_authorization_credits = 0.0;
      shipyard.queued_builds.clear();
    }
  }
  CampaignSimulationState state(std::move(campaign));
  GalaxySimulationStepCoordinator coordinator{};
  for (const auto &[name, tier] : plan.tiers)
    coordinator.set_phase_tier(name, tier);
  std::vector<std::string> digests;
  digests.reserve(static_cast<std::size_t>(steps));
  for (int step = 0; step < steps; ++step) {
    for (const auto &[at, name] : plan.wakes)
      if (at == step)
        coordinator.wake_phase(name);
    (void)coordinator.advance(&state, 1.0);
    digests.push_back(encode_galaxy_payload_v16_json(
        capture_galaxy_payload_v16(
            state.campaign(),
            GalaxyPayloadCaptureOptions{static_cast<double>(step + 1),
                                        "phase-cadence-oracle",
                                        "2026-01-01T00:00:00Z"})));
  }
  return digests;
}

void require_same_trace(const std::vector<std::string> &actual,
                        const std::vector<std::string> &expected,
                        const std::string &label) {
  require(actual.size() == expected.size(), label + ": trace length");
  for (std::size_t i = 0; i < actual.size(); ++i)
    require(actual[i] == expected[i],
            label + ": digest diverged at step " + std::to_string(i + 1));
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: campaign_phase_cadence_tests <catalog.json>");
    const auto catalog = load_nearby_catalog(argv[1]);
    require(!catalog.empty(), "catalog must not be empty");
    constexpr std::int64_t kSeed = 424242;
    constexpr int kSteps = 12;

    // Phase cadence policy surface: defaults, validation, wake plumbing.
    {
      GalaxySimulationStepCoordinator coordinator{};
      require(coordinator.phase_tier("economy") == SimulationTier::Active,
              "phases must default to the Active tier");
      coordinator.set_phase_tier("economy", SimulationTier::Background);
      require(coordinator.phase_tier("economy") == SimulationTier::Background,
              "set_phase_tier must take effect");
      require(GalaxySimulationStepCoordinator::phase_index("freight") ==
                  std::optional<std::size_t>{8},
              "phase_index must map the documented phase order");
      bool threw = false;
      try {
        coordinator.set_phase_tier("bogus", SimulationTier::Nearby);
      } catch (const std::invalid_argument &) {
        threw = true;
      }
      require(threw, "set_phase_tier must reject unknown phases");
      threw = false;
      try {
        (void)coordinator.phase_tier("bogus");
      } catch (const std::invalid_argument &) {
        threw = true;
      }
      require(threw, "phase_tier must reject unknown phases");
      threw = false;
      try {
        coordinator.wake_phase("bogus");
      } catch (const std::invalid_argument &) {
        threw = true;
      }
      require(threw, "wake_phase must reject unknown phases");
    }

    // Baseline determinism: identical seeds and cadence must reproduce
    // byte-identical authoritative state every step.
    const auto baseline = run_trace(catalog, kSeed, kSteps, {});
    const auto repeated = run_trace(catalog, kSeed, kSteps, {});
    require_same_trace(repeated, baseline, "all-active determinism");

    // Inert-phase parity: with fleets, shipyards and construction
    // stripped, the exploration/freight/combat/colonization phases have
    // no inputs to act on for the whole trace — demoting all four to the
    // Background tier must remain byte-identical to the baseline while
    // the still-Active economy/strategic phases keep the world live.
    TracePlan minimal{};
    minimal.minimal_world = true;
    const auto minimal_baseline = run_trace(catalog, kSeed, kSteps, minimal);
    minimal.tiers = {{"exploration", SimulationTier::Background},
                     {"freight", SimulationTier::Background},
                     {"combat", SimulationTier::Background},
                     {"colonization", SimulationTier::Background}};
    const auto demoted = run_trace(catalog, kSeed, kSteps, minimal);
    require_same_trace(demoted, minimal_baseline,
                       "inert-phase demotion parity");

    // Divergence honesty: demoting a phase that actually integrates
    // (economy, Background period 16 over 24 steps -> 1 coarse run at
    // tick 16) must produce a different trace — the oracle detects
    // semantic change rather than vacuously passing.
    {
      constexpr int kLong = 24;
      const auto full = run_trace(catalog, kSeed, kLong, {});
      TracePlan active_demote{};
      active_demote.tiers = {{"economy", SimulationTier::Background}};
      const auto coarse = run_trace(catalog, kSeed, kLong, active_demote);
      bool diverged = false;
      for (std::size_t i = 0; i < full.size(); ++i)
        if (full[i] != coarse[i]) {
          diverged = true;
          break;
        }
      require(diverged,
              "demoting an active phase must change the trace — the "
              "oracle must not pass vacuously");
    }

    // Cadence accounting and the wake path: a Dormant phase never runs
    // on cadence; wake_phase() delivers exactly one out-of-cadence run
    // that consumes the accumulated elapsed span.
    {
      PersistableFreshCampaignOptions seed_options{};
      seed_options.created_at_utc = "2026-01-01T00:00:00Z";
      seed_options.system_count = 250;
      seed_options.pre_warp_civilization_count = 2;
      seed_options.ancient_civilization_count = 0;
      auto campaign = seed_persistable_fresh_campaign(kSeed, catalog,
                                                      seed_options);
      CampaignSimulationState state(std::move(campaign));
      GalaxySimulationStepCoordinator coordinator{};
      coordinator.set_phase_tier("economy", SimulationTier::Dormant);
      for (int i = 0; i < 6; ++i)
        (void)coordinator.advance(&state, 1.0);
      const auto *stats = coordinator.executor().domain_stats("economy");
      require(!stats || stats->runs == 0,
              "a Dormant phase must not run on cadence");
      const std::string dormant_digest = encode_galaxy_payload_v16_json(
          capture_galaxy_payload_v16(
              state.campaign(),
              GalaxyPayloadCaptureOptions{6.0, "phase-cadence-oracle",
                                          "2026-01-01T00:00:00Z"}));
      coordinator.wake_phase("economy");
      (void)coordinator.advance(&state, 1.0);
      stats = coordinator.executor().domain_stats("economy");
      require(stats && stats->runs == 1,
              "wake_phase must deliver exactly one dormant run");
      const std::string woken_digest = encode_galaxy_payload_v16_json(
          capture_galaxy_payload_v16(
              state.campaign(),
              GalaxyPayloadCaptureOptions{7.0, "phase-cadence-oracle",
                                          "2026-01-01T00:00:00Z"}));
      require(woken_digest != dormant_digest,
              "the woken economy run must integrate the accumulated "
              "elapsed span — state must change");
    }

    std::cout << "campaign phase cadence oracle: determinism, inert-"
                 "demotion parity, divergence honesty and dormant wake "
                 "verified\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "campaign phase cadence failure: " << e.what() << "\n";
    return 1;
  }
}
