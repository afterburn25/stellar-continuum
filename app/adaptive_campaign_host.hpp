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
  std::filesystem::path asset_root;
  std::filesystem::path output;
};

using CampaignDiagnosticBuilder =
    std::function<nlohmann::json(const stellar::core::FreshCampaignState &)>;

int run_adaptive_campaign_host(
    const AdaptiveCampaignHostOptions &options,
    std::span<const stellar::core::CatalogStar> stellar_catalog,
    CampaignDiagnosticBuilder campaign_diagnostic);
