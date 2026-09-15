#include <stellar/core/detail/combat_readiness.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::core::detail {
namespace {
constexpr double epsilon = 0.0000001;

double clamp_finite(double value, double maximum) noexcept {
  return std::clamp(std::isfinite(value) ? value : 0.0, 0.0,
                    std::max(0.0, maximum));
}
} // namespace

FleetCombatReadinessSnapshot
read_fleet_combat_readiness(const FleetState &fleet) {
  const FleetCombatState *state = fleet.combat ? &*fleet.combat : nullptr;
  const CombatProfileDefinition *profile =
      state ? find_combat_profile(state->profile_id) : nullptr;
  const bool persisted = profile != nullptr;
  if (!profile)
    profile = &get_combat_profile(default_combat_profile_id(fleet.role));

  const double shields =
      persisted ? clamp_finite(state->shields, profile->max_shields)
                : profile->max_shields;
  const double armor = persisted
                           ? clamp_finite(state->armor, profile->max_armor)
                           : profile->max_armor;
  const double hull = persisted ? clamp_finite(state->hull, profile->max_hull)
                                : profile->max_hull;
  const double current_durability = shields + armor + hull;
  const double maximum_durability =
      profile->max_shields + profile->max_armor + profile->max_hull;
  const double missing_hull = std::max(0.0, profile->max_hull - hull);
  const double repair_deficit =
      std::max(0.0, maximum_durability - current_durability);
  const int order_value = persisted ? static_cast<int>(state->order) : 0;
  const auto order = persisted && order_value >= 0 && order_value <= 3
                         ? state->order
                         : MilitaryOrderType::Hold;
  const bool retreating = order == MilitaryOrderType::Retreat;
  const bool disengaged =
      persisted && state->is_disengaged &&
      state->disengaged_system_id == fleet.current_system_id;
  const double hull_readiness =
      profile->max_hull <= epsilon
          ? 0.0
          : std::clamp(hull / profile->max_hull, 0.0, 1.0);
  const double maximum_offense = profile->sustained_damage_per_day() * 3.0;
  const double current_strength =
      current_durability + maximum_offense * hull_readiness;
  const double maximum_strength = maximum_durability + maximum_offense;
  const bool armed = profile->has_weapon();
  return {profile,
          persisted ? state : nullptr,
          shields,
          armor,
          hull,
          current_durability,
          maximum_durability,
          repair_deficit,
          missing_hull,
          current_strength,
          maximum_strength,
          order,
          armed,
          armed && hull > epsilon && !retreating && !disengaged,
          retreating,
          disengaged};
}

} // namespace stellar::core::detail
