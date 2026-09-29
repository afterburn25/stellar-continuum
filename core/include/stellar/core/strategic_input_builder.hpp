#pragma once

#include <stellar/core/exploration_planning.hpp>
#include <stellar/core/logistics.hpp>
#include <stellar/core/strategic_input_support.hpp>
#include <stellar/core/strategic_intent.hpp>

#include <functional>
#include <span>
#include <string_view>

namespace stellar::core {

struct SettlementBodiesIndex;

struct StrategicInputWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::span<const Colony> colonies;
  std::span<const FleetState> fleets;
  std::span<const CivilizationEconomy> economies;
  std::span<const TechnologyState> technologies;
  std::span<const ConstructionState> construction;
  const CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;
  // Optional caller-shared system->bodies catalog index; when set, the
  // colonization-opportunity probe iterates surveyed systems instead of
  // rescanning `bodies`. Must describe the same catalog.
  const SettlementBodiesIndex *bodies_index{nullptr};
  // Optional caller-shared exploration planning index; when set, the
  // supported-mission-target probe reuses its memoized survey-work lists
  // and per-civilization reach batches instead of rescanning `systems`.
  ExplorationPlanningSharedIndex *planning_shared{nullptr};

  [[nodiscard]] ExplorationPlanningWorldView exploration_view() const {
    return {systems, bodies, fleets, colonies, knowledge, lanes};
  }
  [[nodiscard]] CombatReadinessView combat_readiness_view() const {
    return {civilizations, fleets};
  }
};

using StrategicLogisticsQuery = std::function<CivilizationLogisticsSnapshot(
    const StrategicInputWorldView &, int)>;
using StrategicShipbuildingCapabilityQuery = std::function<bool(
    const StrategicInputWorldView &, int, std::string_view)>;
using StrategicExplorationPlanQuery = std::function<ExplorationMissionPlan(
    ExplorationPlanningWorldView, int, int)>;

class CivilizationStrategicInputBuilder {
public:
  explicit CivilizationStrategicInputBuilder(
      StrategicLogisticsQuery logistics = {},
      StrategicShipbuildingCapabilityQuery shipbuilding_capabilities = {},
      StrategicExplorationPlanQuery exploration = {});

  [[nodiscard]] CivilizationOwnState build(StrategicInputWorldView world,
                                           int civilization_id) const;

private:
  StrategicLogisticsQuery logistics_;
  StrategicShipbuildingCapabilityQuery shipbuilding_capabilities_;
  StrategicExplorationPlanQuery exploration_;
  // When a caller injects an exploration query it stays authoritative for the
  // supported-work check (providers may observe call order or synthesize
  // plans). The default path answers the same existence question with the
  // planner's lazy probe instead of building a full plan per fleet.
  bool exploration_injected_{};
};

} // namespace stellar::core
