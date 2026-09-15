#include <stellar/core/campaign_coordinator.hpp>

#include <stellar/core/civilian_recovery.hpp>
#include <stellar/core/own_combat_fleet_status.hpp>
#include <stellar/core/strategic_input_support.hpp>

#include <algorithm>
#include <stdexcept>

namespace stellar::core {
namespace {
FreshCampaignState &require_campaign(CampaignSimulationState *state) {
  if (!state)
    throw std::invalid_argument("Value cannot be null. (Parameter 'galaxy')");
  return state->campaign();
}
CombatWorldView combat_world(FreshCampaignState &c) {
  return {c.systems, c.fleets};
}
ColonizationWorldView colonization_world(CampaignSimulationState &state) {
  auto &c = state.campaign();
  return {c.systems, c.bodies, c.civilizations, c.colonies, c.fleets,
          c.economies, c.knowledge, state.lanes()};
}
FreightWorldView freight_world(CampaignSimulationState &state) {
  auto &c = state.campaign();
  return {c.systems, c.civilizations, c.bodies, c.construction, c.fleets,
          c.colonies, c.economies, state.lanes()};
}
CivilianRecoveryWorldView recovery_world(CampaignSimulationState &state) {
  auto &c = state.campaign();
  return {c.systems, c.colonies, c.fleets, state.lanes()};
}
constexpr auto preview_unavailable =
    "Military-order preview is unavailable for a coordinator constructed "
    "with a standalone CombatSimulation. Supply a matched "
    "CombatCommandRuntime so preview and issuance share one hostility policy.";
} // namespace

CombatOrderResult GalaxySimulationStepCoordinator::issue_military_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    const MilitaryOrder &order) {
  auto &c = require_campaign(state);
  if (auto *matched = std::get_if<CombatCommandRuntime>(&combat_))
    return matched->issue_order(combat_world(c), civilization_id, fleet_id,
                                order);
  return std::get<CombatSimulation>(combat_).issue_order(
      combat_world(c), civilization_id, fleet_id, order);
}

CombatBatchOrderResult GalaxySimulationStepCoordinator::issue_military_orders(
    CampaignSimulationState *state, int civilization_id,
    std::span<const int> fleet_ids, const MilitaryOrder &order) {
  auto &c = require_campaign(state);
  if (auto *matched = std::get_if<CombatCommandRuntime>(&combat_))
    return matched->issue_orders(combat_world(c), civilization_id, fleet_ids,
                                 order);
  return issue_combat_batch(std::get<CombatSimulation>(combat_),
                            combat_world(c), civilization_id, fleet_ids,
                            order);
}

CombatOrderResult
GalaxySimulationStepCoordinator::issue_engage_hostiles_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id) {
  if (auto *matched = std::get_if<CombatCommandRuntime>(&combat_))
    return matched->issue_engage_hostiles(combat_world(require_campaign(state)), civilization_id,
                                          fleet_id);
  return {false,
          "Engage Hostiles requires the campaign's matched combat command runtime."};
}

CombatOrderResult
GalaxySimulationStepCoordinator::issue_military_deployment_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int destination_system_id) {
  auto &c = require_campaign(state);
  const auto fleet = std::find_if(c.fleets.begin(), c.fleets.end(),
                                  [&](const auto &candidate) {
                                    return candidate.id == fleet_id &&
                                           candidate.is_active &&
                                           candidate.civilization_id == civilization_id &&
                                           candidate.role == FleetRole::Military;
                                  });
  if (fleet == c.fleets.end())
    return {false,
            "No controllable active military fleet with that identity is available."};
  const auto destination =
      std::find_if(c.systems.begin(), c.systems.end(), [&](const auto &system) {
        return system.id == destination_system_id;
      });
  if (destination == c.systems.end())
    return {false,
            "The selected deployment destination is not a valid star system."};
  if (fleet->current_system_id == destination_system_id &&
      !fleet->destination_system_id)
    return {false, fleet->name + " is already stationed in " +
                       destination->name + "."};

  auto &lanes = state->lanes();
  const auto reach = subsystems_.exploration.assess_operational_reach(
      {c.systems, c.bodies, c.fleets, c.colonies, c.knowledge, lanes}, fleet_id,
      destination_system_id);
  if (!reach.is_supported)
    return {false, reach.reason};

  const auto hold = issue_military_order(
      state, civilization_id, fleet_id,
      MilitaryOrder{MilitaryOrderType::Hold, {}, {}});
  if (!hold.accepted)
    return hold;
  assign_fleet_route({c.systems, c.colonies, lanes}, *fleet,
                     destination_system_id, reach);
  fleet->destination_planetary_body_id.reset();
  return {true, fleet->name + " is deploying to " + destination->name + ". " +
                    reach.reason};
}

CombatOrderPreview GalaxySimulationStepCoordinator::preview_military_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    const MilitaryOrder &order) const {
  const auto *matched = std::get_if<CombatCommandRuntime>(&combat_);
  if (!matched)
    throw std::runtime_error(preview_unavailable);
  return matched->preview_order(combat_world(require_campaign(state)), civilization_id, fleet_id,
                                order);
}

CombatBatchOrderPreview
GalaxySimulationStepCoordinator::preview_military_orders(
    CampaignSimulationState *state, int civilization_id,
    std::span<const int> fleet_ids, const MilitaryOrder &order) const {
  const auto *matched = std::get_if<CombatCommandRuntime>(&combat_);
  if (!matched)
    throw std::runtime_error(preview_unavailable);
  return matched->preview_orders(combat_world(require_campaign(state)), civilization_id, fleet_ids,
                                 order);
}

MilitaryForceSummary
GalaxySimulationStepCoordinator::get_own_military_force_summary(
    CampaignSimulationState *state, int civilization_id) {
  auto &c = require_campaign(state);
  return combat_simulation().get_own_military_force_summary(combat_world(c),
                                                             civilization_id);
}

CombatReadinessSummary
GalaxySimulationStepCoordinator::get_own_combat_readiness_summary(
    CampaignSimulationState *state, int civilization_id) const {
  auto &c = require_campaign(state);
  return combat_readiness({c.civilizations, c.fleets}, civilization_id);
}

OwnCombatFleetStatusView
GalaxySimulationStepCoordinator::get_own_combat_fleet_status(
    CampaignSimulationState *state, int civilization_id) const {
  auto &c = require_campaign(state);
  return build_own_combat_fleet_status({c.civilizations, c.fleets},
                                       civilization_id);
}

ColonizationOpportunityPlan
GalaxySimulationStepCoordinator::get_colony_opportunity_plan(
    CampaignSimulationState *state, int fleet_id, int maximum) const {
  require_campaign(state);
  return subsystems_.colonization.get_opportunity_plan(
      colonization_world(*state), fleet_id, maximum);
}

ResourceOutpostOpportunityPlan
GalaxySimulationStepCoordinator::get_resource_outpost_opportunity_plan(
    CampaignSimulationState *state, int fleet_id, int maximum) const {
  require_campaign(state);
  return subsystems_.colonization.get_resource_outpost_opportunity_plan(
      colonization_world(*state), fleet_id, maximum);
}

ResourceOutpostOrderAssessment
GalaxySimulationStepCoordinator::assess_resource_outpost_fleet_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int destination_system_id, int body_id) const {
  auto &c = require_campaign(state);
  const auto fleet = std::find_if(c.fleets.begin(), c.fleets.end(), [&](const auto &x) {
    return x.id == fleet_id && x.civilization_id == civilization_id &&
           ResourceOutpostOpportunityPlanner::is_outpost_fleet(x);
  });
  if (fleet == c.fleets.end())
    return {false, "No controllable staffed resource-outpost vessel with that fleet ID is available.", {}};
  return subsystems_.colonization.assess_resource_outpost_order(
      colonization_world(*state), fleet->id, destination_system_id, body_id);
}

ColonizationOrderAssessment
GalaxySimulationStepCoordinator::assess_colony_fleet_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int destination_system_id, int body_id) const {
  auto &c = require_campaign(state);
  const auto fleet = std::find_if(c.fleets.begin(), c.fleets.end(), [&](const auto &x) {
    return x.id == fleet_id && x.is_active &&
           x.civilization_id == civilization_id && x.role == FleetRole::Colony &&
           x.embarked_population_millions > 0.0 &&
           !ResourceOutpostOpportunityPlanner::is_outpost_fleet(x);
  });
  if (fleet == c.fleets.end())
    return {false, "No controllable populated colony ship with that fleet ID is available.", {}};
  return subsystems_.colonization.assess_colony_order(
      colonization_world(*state), fleet->id, destination_system_id, body_id);
}

ColonyOrderResult
GalaxySimulationStepCoordinator::issue_resource_outpost_fleet_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int destination_system_id, int body_id) const {
  auto &c = require_campaign(state);
  const auto fleet = std::find_if(c.fleets.begin(), c.fleets.end(), [&](const auto &x) {
    return x.id == fleet_id && x.civilization_id == civilization_id &&
           ResourceOutpostOpportunityPlanner::is_outpost_fleet(x);
  });
  if (fleet == c.fleets.end())
    return {false, "No controllable staffed resource-outpost vessel with that fleet ID is available."};
  return subsystems_.colonization.issue_resource_outpost_fleet_order(
      colonization_world(*state), fleet->id, destination_system_id, body_id);
}

ColonyOrderResult GalaxySimulationStepCoordinator::issue_colony_fleet_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int destination_system_id, int body_id) const {
  auto &c = require_campaign(state);
  const auto fleet = std::find_if(c.fleets.begin(), c.fleets.end(), [&](const auto &x) {
    return x.id == fleet_id && x.is_active &&
           x.civilization_id == civilization_id && x.role == FleetRole::Colony &&
           x.embarked_population_millions > 0.0;
  });
  if (fleet == c.fleets.end())
    return {false, "No controllable populated colony ship with that fleet ID is available."};
  return subsystems_.colonization.issue_colony_fleet_order(
      colonization_world(*state), fleet->id, destination_system_id, body_id);
}

FreightOrderResult GalaxySimulationStepCoordinator::issue_freight_transit_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int target_system_id) const {
  require_campaign(state);
  return subsystems_.freight.issue_transit_order(
      freight_world(*state), civilization_id, fleet_id, target_system_id);
}
FreightOrderResult
GalaxySimulationStepCoordinator::issue_freight_collection_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    int outpost_id) const {
  require_campaign(state);
  return subsystems_.freight.issue_collection_order(
      freight_world(*state), civilization_id, fleet_id, outpost_id);
}
CivilianFleetHoldOrderResult
GalaxySimulationStepCoordinator::issue_civilian_hold_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id) const {
  require_campaign(state);
  return hold_civilian_fleet(recovery_world(*state), civilization_id, fleet_id);
}
CivilianFleetHoldOrderResult
GalaxySimulationStepCoordinator::issue_civilian_resume_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id) const {
  require_campaign(state);
  return resume_civilian_fleet(recovery_world(*state), civilization_id,
                               fleet_id);
}
CivilianFleetReturnOrderResult
GalaxySimulationStepCoordinator::preview_civilian_return_to_base(
    CampaignSimulationState *state, int civilization_id, int fleet_id) const {
  require_campaign(state);
  return preview_civilian_fleet_return(recovery_world(*state), civilization_id,
                                       fleet_id);
}
CivilianFleetReturnOrderResult
GalaxySimulationStepCoordinator::issue_civilian_return_to_base_order(
    CampaignSimulationState *state, int civilization_id, int fleet_id,
    bool confirm) const {
  require_campaign(state);
  return request_civilian_fleet_return(recovery_world(*state), civilization_id,
                                       fleet_id, confirm);
}

} // namespace stellar::core
