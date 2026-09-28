#include <stellar/core/campaign_coordinator.hpp>
#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

using namespace stellar::core;
namespace fs = std::filesystem;

namespace {

void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}

CivilizationControlQuery all_ai_control() {
  return [](int) { return true; };
}

ColonizationWorldView colonization_world(CampaignSimulationState &simulation) {
  auto &world = simulation.campaign();
  return {world.systems,    world.bodies,     world.civilizations,
          world.colonies, world.fleets,     world.economies,
          world.knowledge, simulation.lanes(), all_ai_control()};
}

ExplorationAdvanceWorldView exploration_world(CampaignSimulationState &simulation) {
  auto &world = simulation.campaign();
  return {world.systems, world.bodies, world.civilizations, world.fleets,
          world.colonies, world.economies, world.knowledge, simulation.lanes(),
          all_ai_control()};
}

FleetState authored_colony_ship(const FreshCampaignState &world, int id) {
  const auto player = std::ranges::find(world.civilizations,
                                        world.player_civilization_id,
                                        &Civilization::id);
  const auto home = std::ranges::find(world.systems, player->home_system_id,
                                      &StellarSystem::id);
  FleetState fleet;
  fleet.id = id;
  fleet.civilization_id = world.player_civilization_id;
  fleet.name = "Autonomous Settlement Vessel";
  fleet.role = FleetRole::Colony;
  fleet.design_id = "colony_ship";
  fleet.current_system_id = player->home_system_id;
  fleet.position = {home->position.x, home->position.y};
  fleet.embarked_population_millions = 4.5;
  fleet.embarked_population_species_id = player->species_id;
  fleet.maximum_leg_range_light_years = 1'000'000.;
  fleet.fuel_capacity_light_years = 1'000'000.;
  fleet.fuel_remaining_light_years = 1'000'000.;
  fleet.strategic_speed = 1'000'000.;
  return fleet;
}

int append_colony_ship(FreshCampaignState &world) {
  int id{};
  for (const auto &fleet : world.fleets)
    id = std::max(id, fleet.id + 1);
  world.fleets.push_back(authored_colony_ship(world, id));
  return id;
}

void survey_all(FreshCampaignState &world) {
  for (const auto &system : world.systems)
    world.knowledge.mark_system_fully_surveyed(world.player_civilization_id,
                                             system.id);
}

// Regression: an autonomous colony fleet whose planner target lies in the
// fleet's current system must still complete settlement. Previously the
// establishment tick fell through to the opportunity planner, which reset
// settlement progress and re-issued the route order every tick.
void same_system_ai_settlement(const fs::path &catalog_path) {
  CampaignSimulationState simulation(
      seed_fresh_campaign(132500, load_nearby_catalog(catalog_path), 500, 6, 1,
                          "terran_baseline"));
  auto &world = simulation.campaign();
  survey_all(world);
  const int fleet_id = append_colony_ship(world);
  auto &economy = *std::ranges::find(world.economies,
                                     world.player_civilization_id,
                                     &CivilizationEconomy::civilization_id);
  economy.credits = 100'000.;

  const ColonizationSimulation colonization;
  const ExplorationSimulation exploration;

  // Find an orderable body outside the home system and station the vessel in
  // its system, so the planner must issue a same-system settlement order.
  const auto initial_plan = colonization.get_opportunity_plan(
      colonization_world(simulation), fleet_id, 64);
  const auto target = std::ranges::find_if(
      initial_plan.candidates, [](const auto &candidate) {
        return candidate.can_order;
      });
  require(target != initial_plan.candidates.end(),
          "seeded campaign has no orderable colony target for the vessel");
  auto &fleet =
      *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  const auto &target_system =
      *std::ranges::find(world.systems, target->system_id, &StellarSystem::id);
  fleet.current_system_id = target->system_id;
  fleet.position = {target_system.position.x, target_system.position.y};
  fleet.local_transit_position = {};
  fleet.local_transit_start = {};
  fleet.local_transit_target = {};

  const auto colonies_before = world.colonies.size();
  double observed_settlement_progress = 0.;
  bool route_order_observed = false;
  std::optional<ColonizationEvent> established;
  for (int tick = 0; tick < 40 && !established; ++tick) {
    (void)exploration.advance(exploration_world(simulation), 5.0);
    const auto events =
        colonization.advance(colonization_world(simulation), 5.0);
    const auto &vessel =
        *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
    route_order_observed = route_order_observed ||
                           vessel.destination_system_id.has_value();
    observed_settlement_progress =
        std::max(observed_settlement_progress, vessel.settlement_days_completed);
    if (!events.empty())
      established = events.front();
  }

  require(established.has_value(),
          "autonomous same-system colony order never completed settlement");
  require(world.colonies.size() == colonies_before + 1,
          "settlement did not create exactly one colony");
  const auto &vessel =
      *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  require(!vessel.is_active && vessel.embarked_population_millions == 0. &&
              !vessel.embarked_population_species_id,
          "settlement vessel was not consumed by the completed colony");
  const auto &colony = *std::ranges::find(world.colonies, established->colony_id,
                                          &Colony::id);
  require(colony.civilization_id == world.player_civilization_id &&
              colony.system_id == target->system_id &&
              colony.planetary_body_id == target->planetary_body_id,
          "colony was not founded on the planner-selected body");
  require(observed_settlement_progress > 0.,
          "settlement progress never accumulated before completion");
}

// Companion case: an AI colony fleet already holding a route order to a
// surveyed viable body must finish establishing rather than being replanned.
void in_transit_ai_settlement(const fs::path &catalog_path) {
  CampaignSimulationState simulation(
      seed_fresh_campaign(132500, load_nearby_catalog(catalog_path), 500, 6, 1,
                          "terran_baseline"));
  auto &world = simulation.campaign();
  survey_all(world);
  const int fleet_id = append_colony_ship(world);
  auto &economy = *std::ranges::find(world.economies,
                                     world.player_civilization_id,
                                     &CivilizationEconomy::civilization_id);
  economy.credits = 100'000.;

  const ColonizationSimulation colonization;
  const ExplorationSimulation exploration;

  std::optional<ColonizationEvent> established;
  for (int tick = 0; tick < 40 && !established; ++tick) {
    (void)exploration.advance(exploration_world(simulation), 5.0);
    const auto events =
        colonization.advance(colonization_world(simulation), 5.0);
    if (!events.empty())
      established = events.front();
  }
  require(established.has_value(),
          "autonomous colony order never completed settlement in 200 days");
  const auto &vessel =
      *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  require(!vessel.is_active,
          "settlement vessel remained active after founding a colony");
}

} // namespace

int main(int argc, char **argv) try {
  require(argc == 2,
          "Usage: colonization_ai_settlement_tests <astronomy-catalog>");
  const auto catalog = fs::absolute(argv[1]);
  same_system_ai_settlement(catalog);
  in_transit_ai_settlement(catalog);
  std::cout << "colonization AI settlement: 2/2 scenarios passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "colonization AI settlement failed: " << error.what() << '\n';
  return 1;
}
