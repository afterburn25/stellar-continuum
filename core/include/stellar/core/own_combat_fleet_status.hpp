#pragma once

#include <stellar/core/strategic_input_support.hpp>

#include <optional>
#include <string>
#include <vector>

namespace stellar::core {

struct OwnCombatFleetStatus {
  int fleet_id{};
  std::string fleet_name;
  FleetRole role{};
  std::optional<int> current_system_id;
  std::string combat_profile_id;
  double shields{}, maximum_shields{}, armor{}, maximum_armor{}, hull{},
      maximum_hull{}, weapon_cooldown_remaining_days{};
  MilitaryOrderType current_order{MilitaryOrderType::Hold};
  bool has_assigned_attack_target{};
  std::optional<int> defend_system_id;
  double retreat_progress_days{}, retreat_delay_days{};
  bool is_armed{}, is_combat_effective{}, is_disengaged{};
  double current_strength{}, maximum_strength{}, repair_deficit{};

  [[nodiscard]] double current_durability() const noexcept;
  [[nodiscard]] double maximum_durability() const noexcept;
  [[nodiscard]] double durability_ratio() const noexcept;
  [[nodiscard]] double hull_integrity_ratio() const noexcept;
  [[nodiscard]] bool is_damaged() const noexcept;
  [[nodiscard]] bool has_hull_damage() const noexcept;
  [[nodiscard]] bool is_retreating() const noexcept;
  [[nodiscard]] bool can_fire_now() const noexcept;
  [[nodiscard]] double retreat_progress_ratio() const noexcept;
};

struct OwnCombatFleetStatusView {
  int civilization_id{};
  CombatReadinessSummary summary;
  std::vector<OwnCombatFleetStatus> fleets;
};

[[nodiscard]] OwnCombatFleetStatusView
build_own_combat_fleet_status(CombatReadinessView world, int civilization_id);

} // namespace stellar::core
