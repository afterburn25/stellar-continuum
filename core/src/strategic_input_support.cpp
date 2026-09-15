#include <stellar/core/strategic_input_support.hpp>

#include <stellar/core/detail/combat_readiness.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace stellar::core {
namespace {
constexpr double epsilon = 0.0000001;
}
double CombatReadinessSummary::durability_ratio() const noexcept {
  return maximum_durability <= 0.0
             ? 1.0
             : std::clamp(current_durability / maximum_durability, 0.0, 1.0);
}

double CombatReadinessSummary::armed_strength_ratio() const noexcept {
  return maximum_armed_strength <= 0.0
             ? 1.0
             : std::clamp(current_armed_strength / maximum_armed_strength, 0.0,
                          1.0);
}

CombatReadinessSummary combat_readiness(CombatReadinessView world,
                                        int civilization_id) {
  if (std::none_of(world.civilizations.begin(), world.civilizations.end(),
                   [=](const auto &civilization) {
                     return civilization.id == civilization_id;
                   }))
    throw std::runtime_error("Unknown civilization " +
                             std::to_string(civilization_id) + ".");

  CombatReadinessSummary result;
  result.civilization_id = civilization_id;
  std::vector<const FleetState *> fleets;
  for (const auto &fleet : world.fleets)
    if (fleet.is_active && fleet.civilization_id == civilization_id)
      fleets.push_back(&fleet);
  std::stable_sort(
      fleets.begin(), fleets.end(),
      [](const auto *left, const auto *right) { return left->id < right->id; });

  for (const auto *fleet : fleets) {
    ++result.active_vessels;
    const auto snapshot = detail::read_fleet_combat_readiness(*fleet);
    result.current_durability += snapshot.current_durability;
    result.maximum_durability += snapshot.maximum_durability;
    result.total_repair_deficit += snapshot.repair_deficit;
    if (snapshot.repair_deficit > epsilon)
      ++result.damaged_vessels;
    if (snapshot.missing_hull > epsilon)
      ++result.hull_damaged_vessels;
    if (snapshot.is_retreating)
      ++result.retreating_vessels;
    if (snapshot.is_disengaged)
      ++result.disengaged_vessels;
    if (!snapshot.is_armed)
      continue;
    ++result.active_armed_vessels;
    result.current_armed_strength += snapshot.current_strength;
    result.maximum_armed_strength += snapshot.maximum_strength;
    if (!snapshot.is_combat_effective)
      continue;
    ++result.combat_effective_armed_vessels;
    result.combat_effective_armed_strength += snapshot.current_strength;
  }
  return result;
}

bool prototype_construction_has_capability(
    std::span<const TechnologyState> technologies, int civilization_id,
    std::string_view capability_id) noexcept {
  const auto item = std::find_if(
      technologies.begin(), technologies.end(), [=](const auto &technology) {
        return technology.civilization_id == civilization_id;
      });
  return item != technologies.end() &&
         item->completed_technology_ids.contains(capability_id);
}

bool prototype_shipbuilding_has_capability(
    std::span<const TechnologyState> technologies, int civilization_id,
    std::string_view capability_id) noexcept {
  const auto item = std::find_if(
      technologies.begin(), technologies.end(), [=](const auto &technology) {
        return technology.civilization_id == civilization_id;
      });
  if (item == technologies.end())
    return false;
  if (capability_id == "spacecraft_construction")
    return item->completed_technology_ids.contains("orbital_industry");
  if (capability_id == "experimental_interstellar_transit")
    return item->completed_technology_ids.contains("prototype_warp_drive");
  return false;
}
} // namespace stellar::core
