#include <stellar/core/own_combat_fleet_status.hpp>

#include <stellar/core/detail/combat_readiness.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr double epsilon = 0.0000001;

double nonnegative_finite(double value) noexcept {
  return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}
} // namespace

double OwnCombatFleetStatus::current_durability() const noexcept {
  return shields + armor + hull;
}
double OwnCombatFleetStatus::maximum_durability() const noexcept {
  return maximum_shields + maximum_armor + maximum_hull;
}
double OwnCombatFleetStatus::durability_ratio() const noexcept {
  const auto maximum = maximum_durability();
  return maximum <= epsilon
             ? 1.0
             : std::clamp(current_durability() / maximum, 0.0, 1.0);
}
double OwnCombatFleetStatus::hull_integrity_ratio() const noexcept {
  return maximum_hull <= epsilon ? 1.0
                                 : std::clamp(hull / maximum_hull, 0.0, 1.0);
}
bool OwnCombatFleetStatus::is_damaged() const noexcept {
  return repair_deficit > epsilon;
}
bool OwnCombatFleetStatus::has_hull_damage() const noexcept {
  return hull + epsilon < maximum_hull;
}
bool OwnCombatFleetStatus::is_retreating() const noexcept {
  return current_order == MilitaryOrderType::Retreat;
}
bool OwnCombatFleetStatus::can_fire_now() const noexcept {
  return is_armed && is_combat_effective &&
         weapon_cooldown_remaining_days <= epsilon;
}
double OwnCombatFleetStatus::retreat_progress_ratio() const noexcept {
  return !is_retreating() || retreat_delay_days <= epsilon
             ? 0.0
             : std::clamp(retreat_progress_days / retreat_delay_days, 0.0, 1.0);
}

OwnCombatFleetStatusView
build_own_combat_fleet_status(CombatReadinessView world, int civilization_id) {
  if (std::none_of(world.civilizations.begin(), world.civilizations.end(),
                   [=](const Civilization &civilization) {
                     return civilization.id == civilization_id;
                   }))
    throw std::runtime_error("Unknown civilization " +
                             std::to_string(civilization_id) + ".");

  OwnCombatFleetStatusView result;
  result.civilization_id = civilization_id;

  std::vector<const FleetState *> fleets;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id == civilization_id)
      fleets.push_back(&fleet);
  std::stable_sort(fleets.begin(), fleets.end(),
                   [](const FleetState *left, const FleetState *right) {
                     return left->id < right->id;
                   });

  result.fleets.reserve(fleets.size());
  for (const auto *fleet : fleets) {
    const auto readiness = detail::read_fleet_combat_readiness(*fleet);
    const auto *state = readiness.persisted_state;
    const auto *profile = readiness.profile;
    const auto retreat_progress =
        state ? std::min(nonnegative_finite(state->retreat_progress_days),
                         std::max(0.0, profile->retreat_delay_days))
              : 0.0;
    result.fleets.push_back(
        {fleet->id,
         fleet->name,
         fleet->role,
         fleet->current_system_id,
         profile->id,
         readiness.shields,
         profile->max_shields,
         readiness.armor,
         profile->max_armor,
         readiness.hull,
         profile->max_hull,
         state ? nonnegative_finite(state->weapon_cooldown_remaining_days)
               : 0.0,
         readiness.order,
         state && readiness.order == MilitaryOrderType::Attack &&
             state->target_fleet_id.has_value(),
         state && readiness.order == MilitaryOrderType::Defend
             ? state->defend_system_id
             : std::nullopt,
         retreat_progress,
         std::max(0.0, profile->retreat_delay_days),
         readiness.is_armed,
         readiness.is_combat_effective,
         readiness.is_disengaged,
         readiness.current_strength,
         readiness.maximum_strength,
         readiness.repair_deficit});
  }
  result.summary = combat_readiness(world, civilization_id);
  return result;
}

} // namespace stellar::core
