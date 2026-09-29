#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/settlement_planning.hpp>

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace stellar::core;

void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}

bool same_reach(const MissionReachAssessment &a, const MissionReachAssessment &b) {
  return a.is_supported == b.is_supported &&
         a.is_authoritative == b.is_authoritative && a.reason == b.reason &&
         a.route_system_ids == b.route_system_ids &&
         a.route_distance_light_years == b.route_distance_light_years &&
         a.arrival_fuel_light_years == b.arrival_fuel_light_years;
}

bool same_plan(const ColonizationOpportunityPlan &a,
               const ColonizationOpportunityPlan &b) {
  if (a.fleet_id != b.fleet_id || a.civilization_id != b.civilization_id ||
      a.status != b.status || a.candidates.size() != b.candidates.size())
    return false;
  for (std::size_t i = 0; i < a.candidates.size(); ++i) {
    const auto &x = a.candidates[i];
    const auto &y = b.candidates[i];
    if (x.system_id != y.system_id ||
        x.planetary_body_id != y.planetary_body_id ||
        x.can_order != y.can_order || x.reason != y.reason ||
        x.system_occupied != y.system_occupied ||
        x.system_reserved_by_friendly_colony_mission !=
            y.system_reserved_by_friendly_colony_mission ||
        x.reserved_by_fleet_id != y.reserved_by_fleet_id ||
        !same_reach(x.reach, y.reach))
      return false;
  }
  return true;
}

int main(int argc, char **argv) try {
  check(argc == 2, "Expected astronomy catalog.");
  auto world = seed_persistable_fresh_campaign(
      4261837, load_nearby_catalog(argv[1]),
      {"2050-03-21T00:00:00Z", 500, 3, 0, "terran_baseline",
       StellarPopulationOptions{}});
  InterstellarLaneNetwork lanes(world.systems);
  const int civilization_id = world.player_civilization_id;

  FleetState fleet;
  fleet.id = 9811;
  fleet.civilization_id = civilization_id;
  fleet.name = "Shared index colony ship";
  fleet.is_active = true;
  fleet.role = FleetRole::Colony;
  fleet.current_system_id = world.systems[0].id;
  fleet.fuel_capacity_light_years = 1200;
  fleet.fuel_remaining_light_years = 1200;
  fleet.maximum_leg_range_light_years = 420;
  fleet.embarked_population_millions = 5;
  fleet.embarked_population_species_id = "terran_baseline";
  world.fleets.push_back(fleet);

  for (int i = 1; i <= 40; ++i)
    world.knowledge.mark_system_fully_surveyed(civilization_id,
                                             world.systems[i].id);

  auto view = [&]() {
    return SettlementPlanningWorldView{
        world.systems, world.bodies, world.civilizations, world.colonies,
        world.fleets,  world.economies, world.knowledge,    lanes};
  };

  ColonizationOpportunityPlanner planner;
  SettlementPlanningSharedIndex shared;

  // Shared-index plans must be identical to per-call plans, and stable
  // across repeated calls on the same index.
  const auto plain = planner.build_plan(
      view(), fleet.id, ColonizationOpportunityPlanner::hard_maximum_candidates);
  const auto via_shared =
      planner.build_plan(
          view(), fleet.id,
          ColonizationOpportunityPlanner::hard_maximum_candidates, nullptr,
          &shared);
  const auto via_shared_again =
      planner.build_plan(
          view(), fleet.id,
          ColonizationOpportunityPlanner::hard_maximum_candidates, nullptr,
          &shared);
  check(same_plan(plain, via_shared),
        "Shared planning index changed the opportunity plan.");
  check(same_plan(via_shared, via_shared_again),
        "Repeated shared-index planning returned different results.");
  check(!via_shared.candidates.empty(),
        "Fixture produced no colony candidates to compare.");

  // Survey-level changes must invalidate the suitability memo: newly
  // surveyed systems have to appear exactly as an unshared call reports.
  const auto before = via_shared.candidates.size();
  for (int i = 41; i <= 120 && i < static_cast<int>(world.systems.size());
       ++i)
    world.knowledge.mark_system_fully_surveyed(civilization_id,
                                               world.systems[i].id);
  const auto after_survey =
      planner.build_plan(
          view(), fleet.id,
          ColonizationOpportunityPlanner::hard_maximum_candidates, nullptr,
          &shared);
  const auto fresh_after_survey = planner.build_plan(
      view(), fleet.id, ColonizationOpportunityPlanner::hard_maximum_candidates);
  check(same_plan(after_survey, fresh_after_survey),
        "Stale suitability memo survived a survey-level change.");
  check(after_survey.candidates.size() >= before,
        "Newly surveyed systems shrank the suitability candidate list.");

  // Colony growth must invalidate the cached reach batch: the batch holds
  // a span over the colonies vector, which can reallocate on push_back.
  OperationalReachWorldView reach_view{world.systems, world.colonies, lanes};
  auto &batch = shared.reach_batch(reach_view, civilization_id);
  (void)batch.assess(fleet, world.systems[2].id,
                     InterstellarMissionKind::Colony);

  Colony colony;
  colony.id = 700001;
  colony.civilization_id = civilization_id;
  colony.system_id = world.systems[2].id;
  colony.planetary_body_id = 0;
  colony.name = "Guard outpost";
  colony.population_species_id = "terran_baseline";
  colony.population_millions = 1;
  world.colonies.push_back(colony);

  OperationalReachWorldView grown_view{world.systems, world.colonies, lanes};
  auto &grown_batch = shared.reach_batch(grown_view, civilization_id);
  OperationalReachBatch fresh_batch(grown_view, civilization_id);
  for (const auto &system : world.systems)
    check(same_reach(
              grown_batch.assess(fleet, system.id,
                                 InterstellarMissionKind::Colony),
              fresh_batch.assess(fleet, system.id,
                                 InterstellarMissionKind::Colony)),
          "Reach batch retained a stale colonies span after growth.");

  // A second growth bumps the size again, so the guard rebuilds once more
  // and still agrees with a fresh batch.
  colony.id = 700002;
  world.colonies.push_back(colony);
  OperationalReachWorldView grown_again{world.systems, world.colonies, lanes};
  auto &regrown = shared.reach_batch(grown_again, civilization_id);
  OperationalReachBatch fresh_again(grown_again, civilization_id);
  check(same_reach(regrown.assess(fleet, world.systems[3].id,
                                  InterstellarMissionKind::Colony),
                   fresh_again.assess(fleet, world.systems[3].id,
                                      InterstellarMissionKind::Colony)),
        "Reach batch did not rebuild after the second colony growth.");

  // The full planner must agree too: a founded colony flips
  // system_occupied for candidates in that system.
  const auto with_colony =
      planner.build_plan(
          view(), fleet.id,
          ColonizationOpportunityPlanner::hard_maximum_candidates, nullptr,
          &shared);
  const auto fresh_with_colony = planner.build_plan(
      view(), fleet.id, ColonizationOpportunityPlanner::hard_maximum_candidates);
  check(same_plan(with_colony, fresh_with_colony),
        "Shared index diverged after colony founding.");

  std::cout << "settlement shared index tests passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
