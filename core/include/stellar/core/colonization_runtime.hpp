#pragma once

#include <stellar/core/settlement_planning.hpp>

#include <functional>
#include <optional>

namespace stellar::core {

struct ColonizationWorldView {
  std::span<const StellarSystem> systems;
  std::span<const PlanetaryBody> bodies;
  std::span<const Civilization> civilizations;
  std::vector<Colony> &colonies;
  std::span<FleetState> fleets;
  std::span<CivilizationEconomy> economies;
  const CivilizationKnowledgeState &knowledge;
  InterstellarLaneNetwork &lanes;

  [[nodiscard]] SettlementPlanningWorldView planning() const {
    return {systems, bodies,    civilizations, colonies,
            fleets,  economies, knowledge,     lanes};
  }
  [[nodiscard]] OperationalReachWorldView reach() const {
    return {systems, colonies, lanes};
  }
};

struct ColonyOrderResult {
  bool accepted{};
  std::string message;
};

struct ColonizationEvent {
  int civilization_id{}, fleet_id{}, system_id{}, colony_id{};
  std::string message;
};

struct SettlementExpeditionAuthorizationTerms {
  bool is_new_expedition{};
  double charge_budget_units{};
};

class ColonizationSimulation {
public:
  static constexpr double colony_expedition_credit_cost = 120.0;
  static constexpr double colony_establishment_days = 30.0;
  static constexpr double outpost_establishment_days = 20.0;
  static constexpr double resource_outpost_expedition_credit_cost = 90.0;

  explicit ColonizationSimulation(SettlementReachAssessment reach = {});

  [[nodiscard]] static double establishment_days(const FleetState &fleet);
  [[nodiscard]] static SettlementExpeditionAuthorizationTerms
  colony_expedition_authorization(const FleetState &fleet) noexcept;
  [[nodiscard]] static SettlementExpeditionAuthorizationTerms
  resource_outpost_expedition_authorization(const FleetState &fleet) noexcept;
  [[nodiscard]] std::vector<ColonizationEvent>
  advance(ColonizationWorldView world, double simulation_days = 1.0) const;
  [[nodiscard]] ColonyOrderResult
  issue_transit_order(ColonizationWorldView world, int civilization_id,
                      int fleet_id, int destination_system_id) const;
  [[nodiscard]] ColonizationOpportunityPlan get_opportunity_plan(
      ColonizationWorldView world, int fleet_id,
      int maximum_candidates =
          ColonizationOpportunityPlanner::default_maximum_candidates) const;
  static void abandon_mission_for_transit(FleetState &fleet);
  [[nodiscard]] ResourceOutpostOpportunityPlan
  get_resource_outpost_opportunity_plan(
      ColonizationWorldView world, int fleet_id,
      int maximum_candidates =
          ResourceOutpostOpportunityPlanner::default_maximum_candidates) const;
  [[nodiscard]] ResourceOutpostOrderAssessment
  assess_resource_outpost_order(ColonizationWorldView world, int fleet_id,
                                int destination_system_id,
                                int planetary_body_id) const;
  [[nodiscard]] ColonyOrderResult
  issue_resource_outpost_fleet_order(ColonizationWorldView world, int fleet_id,
                                     int destination_system_id,
                                     int planetary_body_id) const;
  [[nodiscard]] ColonizationOrderAssessment
  assess_colony_order(ColonizationWorldView world, int fleet_id,
                      int destination_system_id, int planetary_body_id) const;
  [[nodiscard]] ColonyOrderResult
  issue_player_colony_order(ColonizationWorldView world, int civilization_id,
                            int destination_system_id) const;
  [[nodiscard]] ColonyOrderResult
  issue_player_colony_order(ColonizationWorldView world, int civilization_id,
                            int destination_system_id,
                            int planetary_body_id) const;
  [[nodiscard]] ColonyOrderResult
  issue_colony_fleet_order(ColonizationWorldView world, int fleet_id,
                           int destination_system_id,
                           int planetary_body_id) const;
  [[nodiscard]] MissionReachAssessment
  assess_operational_reach(ColonizationWorldView world, int fleet_id,
                           int destination_system_id) const;
  [[nodiscard]] std::optional<PlanetaryBody>
  resolve_compatibility_colony_world(ColonizationWorldView world,
                                     const Colony &colony) const;

private:
  SettlementReachAssessment reach_;
  ColonizationOpportunityPlanner opportunity_planner_;
  ResourceOutpostOpportunityPlanner outpost_planner_;
};

} // namespace stellar::core
