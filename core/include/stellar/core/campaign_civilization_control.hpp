#pragma once
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/civilization_control.hpp>
#include <algorithm>

namespace stellar::core {
[[nodiscard]] inline bool campaign_civilization_uses_ai(
    const FreshCampaignState &campaign, int civilization_id) {
  const auto civilization = std::ranges::find(campaign.civilizations,
      civilization_id, &Civilization::id);
  if (civilization == campaign.civilizations.end()) return false;
  return !civilization->is_player ||
      (campaign.developer_provenance && campaign.developer_provenance->player_ai_control &&
       civilization_id == campaign.player_civilization_id);
}
[[nodiscard]] inline CivilizationControlQuery campaign_civilization_control(
    const FreshCampaignState &campaign) {
  return [&campaign](int id) { return campaign_civilization_uses_ai(campaign, id); };
}
}
