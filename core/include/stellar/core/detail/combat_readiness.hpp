#pragma once

#include <stellar/core/fleet_state.hpp>

namespace stellar::core::detail {

struct FleetCombatReadinessSnapshot {
  const CombatProfileDefinition *profile{};
  const FleetCombatState *persisted_state{};
  double shields{}, armor{}, hull{}, current_durability{}, maximum_durability{},
      repair_deficit{}, missing_hull{}, current_strength{}, maximum_strength{};
  MilitaryOrderType order{MilitaryOrderType::Hold};
  bool is_armed{}, is_combat_effective{}, is_retreating{}, is_disengaged{};

  [[nodiscard]] bool uses_persisted_state() const noexcept {
    return persisted_state != nullptr;
  }
};

[[nodiscard]] FleetCombatReadinessSnapshot
read_fleet_combat_readiness(const FleetState &fleet);

} // namespace stellar::core::detail
