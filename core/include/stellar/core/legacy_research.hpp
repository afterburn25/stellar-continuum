#pragma once

#include <stellar/core/legacy_technology.hpp>

#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct LegacyResearchEvent {
  int civilization_id{};
  std::string technology_id;
  std::string message;
};

struct LegacyResearchOrderResult {
  bool accepted{};
  std::string message;
};

struct LegacyResearchWorldView {
  std::span<Civilization> civilizations;
  std::span<TechnologyState> technologies;
  std::span<const ConstructionState> construction;
  std::span<CivilizationEconomy> economies;
};

class LegacyResearchSimulation {
public:
  std::vector<LegacyResearchEvent>
  advance(LegacyResearchWorldView world) const;

  std::vector<LegacyResearchEvent>
  advance_for_civilization(LegacyResearchWorldView world,
                           int civilization_id) const;

  LegacyResearchOrderResult start_research(LegacyResearchWorldView world,
                                            int civilization_id,
                                            std::optional<std::string_view>
                                                technology_id) const;

private:
  std::vector<LegacyResearchEvent>
  advance_core(LegacyResearchWorldView world,
               std::optional<int> only_civilization_id) const;
};

} // namespace stellar::core
