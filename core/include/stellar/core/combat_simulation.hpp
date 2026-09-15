#pragma once

#include <stellar/core/fleet_state.hpp>

#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {

struct MilitaryOrder {
  MilitaryOrderType type{};
  std::optional<int> target_fleet_id, defend_system_id;
};
struct CombatOrderResult {
  bool accepted{};
  std::string message;
};
enum class CombatEventType {
  EngagementStarted,
  DamageApplied,
  FleetRetreatInitiated,
  FleetEscaped,
  FleetDestroyed,
  EngagementEnded
};
struct CombatEvent {
  CombatEventType type{};
  std::optional<int> system_id;
  int actor_civilization_id{}, actor_fleet_id{};
  std::optional<int> target_civilization_id, target_fleet_id;
  double shield_damage{}, armor_damage{}, hull_damage{};
  std::string message;
  double embarked_population_casualties_millions{};
};
struct MilitaryForceSummary {
  int civilization_id{}, active_combat_vessels{};
  double current_strength{}, maximum_strength{};
};
struct CombatWorldView {
  std::span<const StellarSystem> systems;
  std::span<FleetState> fleets;
};
using CombatHostilityView = std::function<bool(int, int)>;

class CombatSimulation {
public:
  explicit CombatSimulation(CombatHostilityView hostility = {});
  CombatOrderResult issue_order(CombatWorldView world, int civilization_id,
                                int fleet_id, const MilitaryOrder &order);
  std::vector<CombatEvent> advance(CombatWorldView world,
                                   double simulation_delta_days);
  MilitaryForceSummary get_own_military_force_summary(CombatWorldView world,
                                                      int civilization_id);

private:
  CombatHostilityView hostility_;
  std::set<std::pair<int, int>> active_engagements_;
};
} // namespace stellar::core
