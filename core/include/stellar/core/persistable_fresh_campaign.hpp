#pragma once

#include <stellar/core/fresh_campaign.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace stellar::core {

struct PersistableFreshCampaignOptions {
  std::string created_at_utc;
  int system_count{500};
  int pre_warp_civilization_count{6};
  int ancient_civilization_count{1};
  std::string player_species_id{"terran_baseline"};
  std::optional<StellarPopulationOptions> stellar_population;
  bool developer_full_coverage{};
  std::optional<GalaxyGenerationConfig> configuration;
};

// Creates the canonical FullGalaxy new-campaign composition and attaches the
// owned metadata required by the persistence capture boundary. The timestamp
// remains opaque and is copied exactly from the caller-owned clock boundary.
FreshCampaignState seed_persistable_fresh_campaign(
    std::int64_t seed, std::span<const CatalogStar> catalog,
    const PersistableFreshCampaignOptions &options);

} // namespace stellar::core
