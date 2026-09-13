#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/construction_state.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class TechnologyCategory { Industry, Propulsion, Sensors, Physics, Ftl };

struct TechnologyDefinition {
  std::string id;
  std::string name;
  std::string description;
  double research_cost{};
  std::vector<std::string> prerequisites;
  std::vector<std::string> required_projects;
  TechnologyCategory category{};
};

class OrderedTechnologyIdSet {
public:
  bool insert(std::string id);
  bool contains(std::string_view id) const;
  std::span<const std::string> values() const noexcept;

private:
  std::vector<std::string> values_;
};

struct TechnologyState {
  int civilization_id{};
  // Unique values in first-insertion order, matching source HashSet
  // enumeration.
  OrderedTechnologyIdSet completed_technology_ids;
  std::optional<std::string> active_research_id;
  double active_research_progress{};
};

std::span<const TechnologyDefinition> legacy_technology_catalog();
const TechnologyDefinition &get_legacy_technology(std::string_view id);
std::vector<TechnologyDefinition>
available_legacy_technologies(const TechnologyState &state,
                              const ConstructionState &construction);
std::vector<TechnologyState>
seed_legacy_technologies(std::span<const Civilization> civilizations);

} // namespace stellar::core
