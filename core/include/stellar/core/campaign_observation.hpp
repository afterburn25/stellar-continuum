#pragma once

#include <stellar/core/fresh_campaign.hpp>
#include <algorithm>

namespace stellar::core {

// Presentation access only. Never change AI knowledge, ownership, diplomacy or
// command authority to inspect an isolated developer campaign.
[[nodiscard]] inline bool developer_observation(const FreshCampaignState &world,
                                               int observer) {
  if (!world.developer_provenance || observer != world.player_civilization_id)
    return false;
  const auto player = std::ranges::find(world.civilizations, observer,
                                        &Civilization::id);
  return player != world.civilizations.end() && player->is_player;
}

[[nodiscard]] inline SystemSurveyLevel observation_survey_level(
    const FreshCampaignState &world, int observer, int system) {
  return developer_observation(world, observer)
      ? SystemSurveyLevel::fully_surveyed
      : world.knowledge.system_survey_level(observer, system);
}

[[nodiscard]] inline bool can_inspect_settlement(
    const FreshCampaignState &world, int observer, const Colony &colony) {
  return colony.civilization_id == observer || developer_observation(world, observer);
}

} // namespace stellar::core
