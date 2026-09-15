#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/adaptive_research_funding.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <string>
#include <vector>

namespace stellar::core {

struct AdaptiveResearchCampaignEvent {
  int civilization_id{};
  std::string node_id;
  std::string message;
  bool is_outcome{};
};

class AdaptiveResearchCampaignSimulation final {
public:
  static constexpr int planetary_research_network_lab_count = 4;

  // The world and campaign are borrowed and mutated only for this call. The
  // campaign's strategic runtime must remain alive and unmoved. Returned
  // events own their values. A zero-day call performs no lookup or mutation.
  [[nodiscard]] std::vector<AdaptiveResearchCampaignEvent>
  advance(FreshCampaignState &world, AdaptiveResearchCampaignState &campaign, double elapsed_days,
          double current_simulation_day) const;
};

class AdaptiveResearchCampaignProgression final {
public:
  // Promotes only source-authored pre-warp civilizations whose campaign state
  // already exposes experimental interstellar transit. Input vector order is
  // retained and no research state is mutated.
  static void synchronize_development_stages(FreshCampaignState &world,
                                             const AdaptiveResearchCampaignState &campaign);
};

} // namespace stellar::core
