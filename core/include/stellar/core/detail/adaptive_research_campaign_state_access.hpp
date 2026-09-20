#pragma once

#include <stellar/core/adaptive_research_campaign.hpp>

#include <span>
#include <string_view>

namespace stellar::core::detail {

struct AdaptiveResearchMilestoneConsumption {
  bool found{};
  double consumed_credits{};
  double remaining_credits{};
};

// Controlled mutation seam for the later funding simulation and snapshot
// restore. Callers must own string/span inputs before invoking a mutation when
// those inputs can alias campaign storage.
class AdaptiveResearchCampaignStateAccess final {
public:
  [[nodiscard]] static AdaptiveResearchCivilizationState &
  get_civilization(AdaptiveResearchCampaignState &campaign,
                   int civilization_id);
  static void restore_plan(AdaptiveResearchCampaignState &campaign,
                           int civilization_id, const AdaptiveResearchPlan &plan);

  static void reserve_project_milestones(
      AdaptiveResearchCampaignState &campaign, int civilization_id,
      std::string_view node_id, double authorization_credits,
      double milestone_credits);

  [[nodiscard]] static AdaptiveResearchMilestoneConsumption
  consume_project_milestone(AdaptiveResearchCampaignState &campaign,
                            int civilization_id, std::string_view node_id,
                            bool final_milestone);

  static void restore_project_funding(
      AdaptiveResearchCampaignState &campaign, int civilization_id,
      std::span<const AdaptiveResearchProjectFundingSnapshot> snapshots);
  static void release_project_funding(AdaptiveResearchCampaignState &campaign,
                                      int civilization_id, std::string_view node_id);
};

} // namespace stellar::core::detail
