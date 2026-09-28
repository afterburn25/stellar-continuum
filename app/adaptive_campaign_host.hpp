#pragma once

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

struct AdaptiveCampaignHostOptions {
  std::int64_t seed{8374837};
  int systems{500};
  int repeats{1};
  int simulation_ticks{40};
  double step_days{0.25};
  int pre_warp_civilizations{6};
  int ancient_civilizations{1};
  std::string player_species{"terran_baseline"};
  int autosave_every{0};
  int stress_fleets{0};
  // When true, AI civilizations run their colonies through the
  // civilization automation coordinator (canonical construction
  // commands). Disable for parity-strict comparisons.
  bool civilization_automation{true};
  // Data-authored event-chain definitions root (e.g. data/events).
  // Empty disables the scripted-event feed entirely.
  std::filesystem::path events_root;
  // Headless runs resolve player-bound chain choices deterministically;
  // AI civilizations always auto-resolve.
  bool scripted_player_auto_choose{true};
  // Mid-run save→restore→continue determinism check: at this tick a
  // developer save is captured; after the uninterrupted run completes, a
  // second runtime restores from it, advances the remaining ticks, and
  // its final diagnostic hash must match. 0 disables.
  int verify_continuation_tick{0};
  std::filesystem::path asset_root;
  std::filesystem::path output;
};

using CampaignDiagnosticBuilder =
    std::function<nlohmann::json(const stellar::core::FreshCampaignState &)>;

int run_adaptive_campaign_host(
    const AdaptiveCampaignHostOptions &options,
    std::span<const stellar::core::CatalogStar> stellar_catalog,
    CampaignDiagnosticBuilder campaign_diagnostic);
