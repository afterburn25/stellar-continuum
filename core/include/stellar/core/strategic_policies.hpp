#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/strategic_intent.hpp>
#include <stellar/core/strategic_planning.hpp>

#include <cstdint>

namespace stellar::core {

struct FreshCampaignState;

// Bounded empire-policy action channel for AI civilizations. Translates
// a strategic review into at most one authoritative `set_empire_policy`
// change per call — war posture from the observer-visible war ledger,
// frontier posture from colonization/defense priorities, and economic
// and research stances from the plan's own priorities and traits. Every
// change flows through the command API, so cooldowns, unknown policies
// and unknown civilizations can never be produced.
class StrategicPolicyExecutor {
public:
  struct Result {
    int policies_changed{};
  };

  // `view` must be the acting civilization's own observer-scoped
  // diplomatic view and `knowledge` its strategic knowledge snapshot at
  // `now_tick`. `now_tick` is the strategic review clock (whole campaign
  // days); the policy change is stamped on the campaign diplomacy clock
  // (1000 ticks/day) so cooldowns share the same domain as
  // player-issued changes.
  [[nodiscard]] Result execute(FreshCampaignState &campaign,
                               int civilization,
                               const CivilizationTraits &traits,
                               const CivilizationStrategicReview &review,
                               const StrategicKnowledgeSnapshot &knowledge,
                               const DiplomaticStateView &view,
                               std::int64_t now_tick) const;
};

} // namespace stellar::core
