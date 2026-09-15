#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/fleet_state.hpp>
#include <stellar/core/legacy_technology.hpp>

#include <span>
#include <string_view>

namespace stellar::core {

struct CombatReadinessSummary {
  int civilization_id{};
  int active_vessels{};
  int active_armed_vessels{};
  int combat_effective_armed_vessels{};
  int damaged_vessels{};
  int hull_damaged_vessels{};
  int retreating_vessels{};
  int disengaged_vessels{};
  double current_durability{};
  double maximum_durability{};
  double current_armed_strength{};
  double maximum_armed_strength{};
  double combat_effective_armed_strength{};
  double total_repair_deficit{};

  [[nodiscard]] double durability_ratio() const noexcept;
  [[nodiscard]] double armed_strength_ratio() const noexcept;
};

struct CombatReadinessView {
  std::span<const Civilization> civilizations;
  std::span<const FleetState> fleets;
};

[[nodiscard]] CombatReadinessSummary
combat_readiness(CombatReadinessView world, int civilization_id);

[[nodiscard]] bool prototype_construction_has_capability(
    std::span<const TechnologyState> technologies, int civilization_id,
    std::string_view capability_id) noexcept;

[[nodiscard]] bool prototype_shipbuilding_has_capability(
    std::span<const TechnologyState> technologies, int civilization_id,
    std::string_view capability_id) noexcept;

} // namespace stellar::core
