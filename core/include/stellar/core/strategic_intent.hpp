#pragma once

#include <stellar/core/fleet_role.hpp>
#include <stellar/core/industry_allocation.hpp>
#include <stellar/core/shipbuilding.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::core {

enum class StrategicPriorityType {
  StabilizeSupply, ExpandIndustry, ExpandResearch, Explore, Colonize,
  BuildFleet, Defend, ImproveRelations
};

struct CivilizationOwnState {
  double military_strength{}, supply_coverage_ratio{}, industry_reserve{},
      research_capacity{};
  bool has_available_research{}, has_unexplored_reachable_systems{},
      has_known_colonization_opportunity{}, can_build_interstellar_ships{},
      has_fleet_capacity_shortfall{};
};
struct StrategicPriority {
  StrategicPriorityType type{};
  double score{};
  std::string reason;
};
struct CivilizationStrategicPlan {
  int civilization_id{};
  std::int64_t generated_at_tick{}, review_after_tick{};
  std::vector<StrategicPriority> priorities;
  const StrategicPriority *primary_priority() const noexcept;
};
struct CivilizationStrategicIntent {
  int civilization_id{};
  std::int64_t generated_at_tick{}, review_after_tick{};
  std::unordered_map<StrategicPriorityType, double> weights;
  std::optional<FleetRole> preferred_new_fleet_role;
  bool defer_new_colonization{};
  std::string summary;
  double get_weight(StrategicPriorityType type) const noexcept;
};
struct CivilizationStrategicReview {
  CivilizationOwnState own_state;
  CivilizationStrategicPlan plan;
  CivilizationStrategicIntent intent;
};

class CivilizationStrategicIntentBuilder {
public:
  CivilizationStrategicIntent build(const CivilizationStrategicPlan &plan) const;
};

class StrategicIndustryPriorityProvider {
public:
  [[nodiscard]] std::size_t published_intent_count() const noexcept;
  void publish(const CivilizationStrategicReview &review);
  void publish(const CivilizationStrategicIntent &intent);
  void remove(int civilization_id) noexcept;
  void clear() noexcept;
  [[nodiscard]] IndustryPriorityWeights get_weights(
      int civilization_id) const noexcept;
private:
  std::unordered_map<int, CivilizationStrategicIntent> intents_;
};

class StrategicShipbuildingPreferenceProvider {
public:
  [[nodiscard]] std::size_t published_preference_count() const noexcept;
  void publish(const CivilizationStrategicReview &review);
  void publish(const CivilizationStrategicIntent &intent);
  void remove(int civilization_id) noexcept;
  void clear() noexcept;
  [[nodiscard]] ShipbuildingStrategicPreference get_preference(
      int civilization_id) const;
private:
  std::unordered_map<int, ShipbuildingStrategicPreference> preferences_;
};
} // namespace stellar::core
