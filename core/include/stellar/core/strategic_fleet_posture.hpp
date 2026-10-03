#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/strategic_intent.hpp>

#include <cstdint>

namespace stellar::core {

struct FreshCampaignState;

// Fleet-posture action channel for AI civilizations. Translates a
// strategic review into standing fleet doctrines — every military
// fleet the civilization owns is set through the validated
// `set_fleet_doctrine` path so persisted state stays within the same
// contract player-issued doctrines obey. War state comes from the
// acting civilization's own observer-scoped diplomatic view; the
// review supplies defense priorities and the traits shape how readily
// a fleet disengages.
class StrategicFleetPostureExecutor {
public:
  struct Result {
    int doctrines_changed{};
  };

  // `view` must be the acting civilization's own observer-scoped
  // diplomatic view. `now_tick` is the strategic review clock (whole
  // campaign days); doctrines carry no timestamp, so the tick only
  // validates the call's currency.
  [[nodiscard]] Result execute(FreshCampaignState &campaign,
                               int civilization,
                               const CivilizationTraits &traits,
                               const CivilizationStrategicReview &review,
                               const DiplomaticStateView &view,
                               std::int64_t now_tick) const;
};

} // namespace stellar::core
