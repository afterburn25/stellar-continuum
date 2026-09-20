#include <stellar/core/shipbuilding.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] bool near(double left, double right) {
  return std::abs(left - right) <= .000001;
}

struct TestWorld {
  std::vector<Civilization> civilizations;
  std::vector<StellarSystem> systems;
  std::vector<ConstructionState> construction;
  std::vector<ShipyardState> shipyards;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<FleetState> fleets;
  std::vector<ShipbuildingCapabilities> capabilities;

  [[nodiscard]] ShipbuildingWorld view() {
    return {civilizations, systems, construction, shipyards, colonies,
            economies, fleets, capabilities, {}};
  }
};

[[nodiscard]] TestWorld make_world() {
  TestWorld result;
  result.civilizations.push_back(
      {.id = 1,
       .name = "Terran Union",
       .home_system_id = 10,
       .is_player = true,
       .development_stage = CivilizationDevelopmentStage::WarpCapable,
       .species_id = "terran_baseline"});
  StellarSystem system;
  system.id = 10;
  system.name = "Sol";
  system.position = {2., 3., std::nullopt};
  result.systems.push_back(std::move(system));
  result.construction.push_back(
      {.civilization_id = 1,
       .completed_project_ids = {"orbital_shipyard"}});
  result.shipyards.push_back({.civilization_id = 1});
  result.colonies.push_back({.id = 20,
                             .civilization_id = 1,
                             .system_id = 10,
                             .name = "Earth",
                             .population_species_id = "terran_baseline",
                             .population_millions = 3000.});
  result.economies.push_back(
      {.civilization_id = 1, .credits = 10000., .industry = 10000.});
  result.capabilities.push_back(
      {1, {"spacecraft_construction",
           "experimental_interstellar_transit"}});
  return result;
}

void accepted_assessment_is_non_mutating_and_committed_once() {
  auto world = make_world();
  const auto before_credits = world.economies.front().credits;
  const auto before_population = world.colonies.front().population_millions;
  const auto before_sequence = world.shipyards.front().next_order_sequence;
  const auto assessed = assess_start_ship_build(world.view().read(), 1,
                                                "colony_ship");
  require(assessed.can_start && !assessed.will_queue && !assessed.blocker &&
              assessed.design_id == "colony_ship" &&
              assessed.design_name == "Interstellar Colony Ship" &&
              near(assessed.industry_cost, 1500.) &&
              near(assessed.credit_cost, 180.) &&
              near(assessed.population_cost_millions, 250.) &&
              near(assessed.minimum_source_population_millions, 750.) &&
              assessed.pending_build_count == 0 &&
              assessed.maximum_pending_builds == 8 &&
              assessed.prepared_order_id == "shipyard-1-1" &&
              assessed.population_source_colony_id == 20 &&
              assessed.population_species_id == "terran_baseline" &&
              assessed.population_source_current_millions &&
              near(*assessed.population_source_current_millions, 3000.),
          "Accepted assessment omitted canonical requirements or source.");
  require(near(world.economies.front().credits, before_credits) &&
              near(world.colonies.front().population_millions,
                   before_population) &&
              world.shipyards.front().next_order_sequence == before_sequence &&
              !world.shipyards.front().active_design_id,
          "Read-only assessment mutated shipbuilding state.");

  const auto started = start_ship_build(world.view(), 1, "colony_ship");
  const auto &yard = world.shipyards.front();
  require(started.accepted && yard.active_design_id == "colony_ship" &&
              yard.active_order_id == assessed.prepared_order_id &&
              near(world.economies.front().credits, before_credits - 180.) &&
              near(world.colonies.front().population_millions,
                   before_population - 250.) &&
              near(yard.reserved_population_millions, 250.) &&
              yard.reserved_population_species_id == "terran_baseline" &&
              yard.reserved_population_source_colony_id == 20 &&
              yard.next_order_sequence == before_sequence + 1,
          "Commit did not consume the assessed canonical values exactly once.");

  const auto queued = assess_start_ship_build(world.view().read(), 1,
                                              "warp_scout");
  require(queued.can_start && queued.will_queue &&
              queued.pending_build_count == 1 &&
              queued.prepared_order_id == "shipyard-1-2" &&
              near(queued.minimum_source_population_millions, 0.) &&
              !queued.population_source_colony_id,
          "Queued non-population assessment was not canonical.");
}

void denials_match_commit_and_do_not_mutate() {
  auto population = make_world();
  population.colonies.front().population_millions = 749.;
  const auto population_assessment = assess_start_ship_build(
      population.view().read(), 1, "colony_ship");
  require(!population_assessment.can_start &&
              population_assessment.blocker ==
                  "At least 750 million population is required before reserving colonists for this ship." &&
              population_assessment.population_source_colony_id == 20 &&
              population_assessment.population_source_current_millions &&
              near(*population_assessment.population_source_current_millions,
                   749.),
          "Population precommit denial lost its exact requirement or source.");
  const auto population_result =
      start_ship_build(population.view(), 1, "colony_ship");
  require(!population_result.accepted &&
              population_result.message == *population_assessment.blocker &&
              near(population.economies.front().credits, 10000.) &&
              near(population.colonies.front().population_millions, 749.) &&
              !population.shipyards.front().active_design_id,
          "Population assessment and command denial diverged or mutated state.");

  auto funding = make_world();
  funding.economies.front().credits = 179.999;
  funding.colonies.front().population_millions = 1.;
  const auto funding_assessment =
      assess_start_ship_build(funding.view().read(), 1, "colony_ship");
  const auto funding_result = start_ship_build(funding.view(), 1, "colony_ship");
  require(!funding_assessment.can_start && funding_assessment.blocker &&
              funding_result.message == *funding_assessment.blocker &&
              funding_assessment.blocker->find("required to authorize") !=
                  std::string::npos &&
              !funding_assessment.population_source_colony_id &&
              near(funding.colonies.front().population_millions, 1.),
          "Funding no longer precedes population in the canonical failure order.");

  auto changed = make_world();
  const auto ready =
      assess_start_ship_build(changed.view().read(), 1, "warp_scout");
  require(ready.can_start, "Scout precommit unexpectedly failed.");
  changed.economies.front().credits = 0.;
  const auto revalidated = start_ship_build(changed.view(), 1, "warp_scout");
  require(!revalidated.accepted && !changed.shipyards.front().active_design_id &&
              near(changed.economies.front().credits, 0.),
          "Commit trusted a stale precommit assessment instead of revalidating.");
}

void first_failure_order_and_identity_diagnostics() {
  auto full = make_world();
  auto &yard = full.shipyards.front();
  yard.active_design_id = "warp_scout";
  yard.active_order_id = "shipyard-1-1";
  for (int sequence = 2; sequence <= 8; ++sequence)
    yard.queued_builds.push_back(
        {"shipyard-1-" + std::to_string(sequence), "warp_scout", 70.});
  yard.next_order_sequence = 9;
  const auto full_result =
      assess_start_ship_build(full.view().read(), 1, "unknown-design");
  require(!full_result.can_start && full_result.blocker ==
              "The shipyard queue is full (8 pending vessels maximum)." &&
              !full_result.design_id,
          "Queue capacity no longer precedes design lookup.");

  auto identity = make_world();
  identity.shipyards.front().next_order_sequence = 0;
  const auto identity_assessment =
      assess_start_ship_build(identity.view().read(), 1, "warp_scout");
  const auto identity_result =
      start_ship_build(identity.view(), 1, "warp_scout");
  require(!identity_assessment.can_start &&
              identity_assessment.blocker ==
                  "This shipyard cannot allocate another stable order identity." &&
              identity_result.message == *identity_assessment.blocker &&
              near(identity.economies.front().credits, 10000.) &&
              !identity.shipyards.front().active_design_id,
          "Stable-identity assessment diverged from the command.");
}
} // namespace

void atomic_batches_and_canonical_reorder() {
  auto world=make_world();
  world.economies.front().credits=400.;
  const auto quotes=assess_ship_build_batches(world.view().read(),1,"colony_ship");
  require(quotes.size()==8 && quotes[1].can_start && !quotes[2].can_start,
      "Batch quotes ignored cumulative authorization cost");
  require(near(quotes[1].credit_cost,360.) && near(quotes[1].population_cost_millions,500.) &&
      near(quotes[1].industry_cost,3000.) && near(quotes[1].minimum_build_days_at_full_shipyard_rate,150.),
      "Batch quote omitted canonical total costs/time");
  require(near(world.economies.front().credits,400.) && world.shipyards.front().pending_build_count()==0 &&
      near(world.colonies.front().population_millions,3000.),"Batch quote mutated state");
  const auto sequence=world.shipyards.front().next_order_sequence;
  require(!start_ship_build_batch(world.view(),1,"colony_ship",3).accepted &&
      world.shipyards.front().pending_build_count()==0 &&
      world.shipyards.front().next_order_sequence==sequence &&
      near(world.economies.front().credits,400.) && near(world.colonies.front().population_millions,3000.),
      "Failed batch partially charged or reserved population/IDs");
  require(start_ship_build_batch(world.view(),1,"colony_ship",2).accepted &&
      world.shipyards.front().pending_build_count()==2 && near(world.economies.front().credits,40.) &&
      near(world.colonies.front().population_millions,2500.),"Accepted batch not committed exactly once");
  auto population=make_world();population.colonies.front().population_millions=800.;
  const auto population_quotes=assess_ship_build_batches(population.view().read(),1,"colony_ship");
  require(population_quotes.front().can_start && !population_quotes[1].can_start &&
      !start_ship_build_batch(population.view(),1,"colony_ship",2).accepted &&
      near(population.colonies.front().population_millions,800.),"Batch violated retained colony population");
  auto queue=make_world();
  require(start_ship_build_batch(queue.view(),1,"warp_scout",3).accepted,"Scout batch setup");
  auto& yard=queue.shipyards.front();yard.active_build_progress=17.;
  const auto active=*yard.active_order_id, first=yard.queued_builds[0].order_id,last=yard.queued_builds[1].order_id;
  const double credits=queue.economies.front().credits;
  require(move_queued_ship_build(queue.view(),1,last,-1).accepted && yard.queued_builds[0].order_id==last &&
      yard.queued_builds[1].order_id==first && *yard.active_order_id==active && near(yard.active_build_progress,17.) &&
      near(queue.economies.front().credits,credits),"Reorder changed active work or authorization");
  require(!move_queued_ship_build(queue.view(),1,active,1).accepted &&
      !move_queued_ship_build(queue.view(),2,last,1).accepted &&
      !move_queued_ship_build(queue.view(),1,last,-1).accepted,"Reorder admitted active/foreign/boundary order");
  require(!start_ship_build_batch(queue.view(),1,"warp_scout",6).accepted && yard.pending_build_count()==3,
      "Overflowing batch partially filled queue");
  const auto events=advance_shipbuilding_for_civilization(queue.view(),1,10000.,100.);
  require(events.size()==1 && yard.active_order_id==last,"Completion did not promote reordered canonical queue");
  require(cancel_ship_build(queue.view(),1,first).accepted && yard.queued_builds.empty(),"Reordered cancellation failed");
}

int main() try {
  atomic_batches_and_canonical_reorder();
  accepted_assessment_is_non_mutating_and_committed_once();
  denials_match_commit_and_do_not_mutate();
  first_failure_order_and_identity_diagnostics();
  std::cout << "Shipbuilding precommit assessment and shared command validation tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Shipbuilding assessment test failed: " << error.what() << '\n';
  return 1;
}
