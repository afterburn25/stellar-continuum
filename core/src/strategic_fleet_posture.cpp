#include <stellar/core/strategic_fleet_posture.hpp>

#include <stellar/core/combat_state.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <algorithm>
#include <stdexcept>

namespace stellar::core {
namespace {

bool at_war(const DiplomaticStateView &view, int civilization) noexcept {
  for (const auto &war : view.wars)
    if (!war.resolved_at_tick &&
        (war.aggressor_civilization_id == civilization ||
         war.defender_civilization_id == civilization))
      return true;
  return false;
}

bool defends(const CivilizationStrategicReview &review) noexcept {
  const auto *primary = review.plan.primary_priority();
  return primary != nullptr &&
         primary->type == StrategicPriorityType::Defend;
}

} // namespace

StrategicFleetPostureExecutor::Result StrategicFleetPostureExecutor::execute(
    FreshCampaignState &campaign, int civilization,
    const CivilizationTraits &traits,
    const CivilizationStrategicReview &review,
    const DiplomaticStateView &view, std::int64_t now_tick) const {
  if (civilization < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'civilizationId')");
  if (now_tick < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'nowTick')");
  Result result;

  // Wartime and defense-led fleets hold weapons free; peacetime fleets
  // hold fire unless ordered. Honor-bound fleets never auto-retreat —
  // pragmatic fleets disengage earlier as survival priority rises.
  const bool weapons_free =
      at_war(view, civilization) || defends(review);
  FleetDoctrine desired;
  desired.posture = weapons_free ? FleetDoctrinePosture::EngageAtWill
                                 : FleetDoctrinePosture::HoldFast;
  desired.auto_retreat_hull_fraction =
      !weapons_free || traits.honor_bound
          ? 0.0
          : std::clamp(0.10 + 0.40 * traits.survival_priority, 0.0, 0.5);

  std::vector<FleetState *> owned;
  for (auto &fleet : campaign.fleets)
    if (fleet.is_active && fleet.civilization_id == civilization &&
        fleet.role == FleetRole::Military)
      owned.push_back(&fleet);
  std::ranges::sort(owned, {}, [](const FleetState *f) { return f->id; });
  for (auto *fleet : owned) {
    if (fleet->doctrine.posture == desired.posture &&
        fleet->doctrine.auto_retreat_hull_fraction ==
            desired.auto_retreat_hull_fraction)
      continue;
    if (set_fleet_doctrine(*fleet, desired))
      ++result.doctrines_changed;
  }
  return result;
}

} // namespace stellar::core
