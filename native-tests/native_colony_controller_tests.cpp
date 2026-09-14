#include "native_colony_controller.hpp"
#include "native_settlement_mission_controller.hpp"

#include "../../app/native_client/native_system_view.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>

using namespace stellar::core;
using namespace stellar::native_colony;
using namespace stellar::native_system;
namespace fs = std::filesystem;

namespace {
void require(const bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

CampaignFrame make_frame(const fs::path &research_root,
                         const fs::path &catalog, const std::int64_t seed) {
  auto world = seed_persistable_fresh_campaign(
      seed, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  StrategicClock clock;
  clock.set_speed(StrategicSpeed::Demo);
  return CampaignFrame(
      IntegratedAdaptiveCampaignRuntime::create_fresh(
          load_adaptive_research_strategic_runtime(research_root),
          std::move(world)),
      std::move(clock), CampaignFramePolicy::Developer);
}

void require_finite(const NativeColonyView &view) {
  const double values[]{view.treasury_budget_units,
                        view.stored_industry,
                        view.population_millions,
                        view.infrastructure,
                        view.stability,
                        view.working_age_population_millions,
                        view.employed_population_millions,
                        view.unemployed_population_millions,
                        view.employment_rate,
                        view.food_capacity_millions,
                        view.water_capacity_millions,
                        view.housing_capacity_millions,
                        view.supported_population_millions,
                        view.sustenance_support_ratio,
                        view.food_reserve_days,
                        view.water_reserve_days,
                        view.power_supply,
                        view.power_demand,
                        view.credits_per_day,
                        view.industry_per_day,
                        view.science_per_day};
  require(std::ranges::all_of(values, [](const double value) {
            return std::isfinite(value);
          }),
          "colony projection emitted non-finite operational telemetry");
}

void inspector_tests(const fs::path &research_root, const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog, 127500);
  auto &world = frame.runtime().world().campaign();
  const auto player = world.player_civilization_id;
  const auto owned = std::ranges::find(world.colonies, player,
                                        &Colony::civilization_id);
  require(owned != world.colonies.end() && owned->planetary_body_id,
          "fresh Player17 lacks an owned body settlement");
  world.knowledge.mark_system_fully_surveyed(player, owned->system_id);
  NativeSystemViewController systems;
  const auto system = systems.build(frame, 7, owned->system_id);
  require(system.snapshot.has_value(),
          "known owned system did not produce a snapshot");

  NativeColonyController controller;
  const auto result = controller.build(frame, 7, *system.snapshot,
                                       *owned->planetary_body_id);
  require(result.view.has_value(),
          "owned colony did not produce native telemetry");
  const auto frozen = *result.view;
  require(frozen.player_civilization_id == player &&
              frozen.colony_id == owned->id &&
              frozen.body_id == *owned->planetary_body_id,
          "colony projection lost its ownership binding");
  require_finite(frozen);
  require(std::ranges::is_sorted(frozen.construction_sites, {},
                                 &NativeSurfaceSite::building_id),
          "surface construction sites are not deterministic");

  owned->name += " changed";
  require(frozen.colony_name != owned->name,
          "owned colony snapshot retained a live string reference");
  const auto refreshed = controller.build(frame, 7, *system.snapshot,
                                           *owned->planetary_body_id);
  require(refreshed.view && refreshed.view->revision > frozen.revision,
          "changed operational state did not invalidate the colony revision");
  if (owned->surface_buildings.empty()) {
    const auto &definition = surface_building_catalog().front();
    owned->surface_buildings.push_back(
        {.id = 9001, .type_id = definition.id, .x = 4.f, .z = 5.f});
  }
  const auto with_site = controller.build(frame, 7, *system.snapshot,
                                           *owned->planetary_body_id);
  require(with_site.view && !with_site.view->construction_sites.empty(),
          "authored surface site was not projected");
  const auto before_relocation_revision = with_site.view->revision;
  owned->surface_buildings.front().x += 1.f;
  owned->surface_buildings.front().z -= 2.f;
  owned->surface_buildings.front().rotation_degrees += 15.f;
  const auto relocated = controller.build(frame, 7, *system.snapshot,
                                           *owned->planetary_body_id);
  require(relocated.view &&
              relocated.view->revision > before_relocation_revision &&
              relocated.view->construction_sites.front().x ==
                  owned->surface_buildings.front().x &&
              !relocated.view->construction_sites.front().name.empty(),
          "surface site placement/name projection did not invalidate revision");

  owned->population_millions = 1'000'000'000.;
  owned->stored_food_population_days_millions = 5'000'000'000.;
  owned->stored_water_population_days_millions = 3'000'000'000.;
  const auto deficit_reserves = controller.build(
      frame, 7, *system.snapshot, *owned->planetary_body_id);
  require(deficit_reserves.view &&
              deficit_reserves.view->supported_population_millions <
                  deficit_reserves.view->population_millions &&
              deficit_reserves.view->food_reserve_days == 5. &&
              deficit_reserves.view->water_reserve_days == 3.,
          "colony telemetry projected tomorrow's depleted reserves instead of current stored days");

  auto unknown = *system.snapshot;
  unknown.system_id = world.systems.back().id;
  const auto denied = controller.build(frame, 7, unknown,
                                       *owned->planetary_body_id);
  require(!denied.view,
          "a spoofed unknown-system snapshot exposed owned colony telemetry");
  auto foreign = *system.snapshot;
  ++foreign.observer_civilization_id;
  require(!controller.build(frame, 7, foreign, *owned->planetary_body_id).view,
          "a foreign observer exposed owned colony telemetry");
  bool stale_threw{};
  try {
    (void)controller.build(frame, 6, *system.snapshot,
                           *owned->planetary_body_id);
  } catch (const std::invalid_argument &) {
    stale_threw = true;
  }
  require(stale_threw, "an older campaign generation replaced colony state");

  bool wrong_thread_threw{};
  std::thread wrong([&] {
    try {
      (void)controller.build(frame, 7, *system.snapshot,
                             *owned->planetary_body_id);
    } catch (const std::logic_error &) {
      wrong_thread_threw = true;
    }
  });
  wrong.join();
  require(wrong_thread_threw,
          "colony projection accepted a non-owner simulation thread");
}

FleetState authored_vessel(const FreshCampaignState &world, const bool outpost,
                            const int id) {
  const auto player = std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
  FleetState fleet;
  fleet.id = id;
  fleet.civilization_id = world.player_civilization_id;
  fleet.name = outpost ? "Authored Outpost Vessel" : "Authored Colony Vessel";
  fleet.role = FleetRole::Colony;
  fleet.design_id = outpost ? "resource_outpost_ship" : "colony_ship";
  fleet.current_system_id = player->home_system_id;
  const auto home = std::ranges::find(world.systems, player->home_system_id,
                                       &StellarSystem::id);
  fleet.position = {home->position.x, home->position.y};
  fleet.embarked_population_millions = outpost ? 0.8 : 4.5;
  fleet.embarked_population_species_id = player->species_id;
  fleet.maximum_leg_range_light_years = 1'000'000.;
  fleet.fuel_capacity_light_years = 1'000'000.;
  fleet.fuel_remaining_light_years = 1'000'000.;
  fleet.strategic_speed = 1'000'000.;
  return fleet;
}

int next_fleet_id(const FreshCampaignState &world) {
  int result{};
  for (const auto &fleet : world.fleets) result = std::max(result, fleet.id + 1);
  return result;
}

void mission_case(const fs::path &research_root, const fs::path &catalog,
                  const bool outpost) {
  auto frame = make_frame(research_root, catalog, outpost ? 127502 : 127501);
  auto &world = frame.runtime().world().campaign();
  const auto player = world.player_civilization_id;
  for (const auto &system : world.systems)
    world.knowledge.mark_system_fully_surveyed(player, system.id);
  const int fleet_id = next_fleet_id(world);
  world.fleets.push_back(authored_vessel(world, outpost, fleet_id));

  NativeSettlementMissionController controller;
  auto views = controller.build(frame, 11);
  const auto view = std::ranges::find(views, fleet_id,
                                      &NativeSettlementMissionView::fleet_id);
  require(view != views.end(), "authored settlement vessel was not projected");
  require(view->kind == (outpost ? NativeSettlementMissionKind::ResourceOutpost
                                 : NativeSettlementMissionKind::Colony),
          "canonical vessel kind was not preserved");
  require(view->authorization_budget_units == (outpost ? 90. : 120.),
          "canonical expedition authorization changed");
  const auto candidate = std::ranges::find_if(
      view->candidates, [](const auto &item) { return item.can_order; });
  require(candidate != view->candidates.end(),
          "authored fully surveyed campaign has no orderable settlement site");
  const auto destination_system = candidate->system_id;
  const auto destination_body = candidate->body_id;
  const auto revision = view->revision;
  const auto credits_before = std::ranges::find(
                                  world.economies, player,
                                  &CivilizationEconomy::civilization_id)
                                  ->credits;
  const auto colonies_before = world.colonies.size();
  const auto order_day = frame.clock().simulation_days();
  const auto result = controller.issue(frame, 11, revision, fleet_id,
                                       destination_system, destination_body);
  require(result.accepted, "canonical settlement command rejected: " +
                               result.message);
  const auto credits_after = std::ranges::find(
                                 world.economies, player,
                                 &CivilizationEconomy::civilization_id)
                                 ->credits;
  require(std::abs((credits_before - credits_after) -
                   (outpost ? 90. : 120.)) < 1e-9,
          "settlement authorization was not charged exactly once");
  require(world.colonies.size() == colonies_before,
          "issuing a settlement mission created a colony immediately");

  bool saw_establishment{}, consumed{};
  double first_establishment_progress{};
  for (int day = 0; day < 600 && !consumed; ++day) {
    const auto step = frame.advance(1.);
    require(step.route == CampaignFrameRoute::Strategic,
            "settlement smoke left strategic CampaignFrame routing");
    const auto fleet = std::ranges::find(world.fleets, fleet_id, &FleetState::id);
    require(fleet != world.fleets.end(), "settlement vessel disappeared");
    if (fleet->settlement_days_completed > 0. && !saw_establishment) {
      saw_establishment = true;
      first_establishment_progress = fleet->settlement_days_completed;
    }
    consumed = !fleet->is_active;
  }
  require(saw_establishment && first_establishment_progress <= 1.0000001,
          "arrival incorrectly counted a full establishment step");
  require(consumed, "settlement vessel was not consumed after bounded progress");
  const auto required_establishment_days = outpost ? 20. : 30.;
  require(frame.clock().simulation_days() - order_day + 1e-9 >=
              required_establishment_days,
          "settlement completed without its canonical establishment duration");
  require(world.colonies.size() == colonies_before + 1,
          "settlement completion did not create exactly one settlement");
  const auto created = std::ranges::find_if(world.colonies, [&](const Colony &c) {
    return c.civilization_id == player && c.system_id == destination_system &&
           c.planetary_body_id == destination_body;
  });
  require(created != world.colonies.end(),
          "completed settlement has the wrong owner or destination");
  require(created->kind ==
              (outpost ? SettlementKind::ResourceOutpost
                       : SettlementKind::Colony),
          "completed settlement has the wrong canonical kind");
}

void stale_mission_test(const fs::path &research_root,
                        const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog, 127503);
  auto &world = frame.runtime().world().campaign();
  const auto player = world.player_civilization_id;
  for (const auto &system : world.systems)
    world.knowledge.mark_system_fully_surveyed(player, system.id);
  const int fleet_id = next_fleet_id(world);
  world.fleets.push_back(authored_vessel(world, false, fleet_id));
  NativeSettlementMissionController controller;
  const auto views = controller.build(frame, 3);
  const auto projected = std::ranges::find(
      views, fleet_id, &NativeSettlementMissionView::fleet_id);
  require(projected != views.end(), "stale fixture vessel was not projected");
  const auto &view = *projected;
  const auto candidate = std::ranges::find_if(
      view.candidates, [](const auto &item) { return item.can_order; });
  require(candidate != view.candidates.end(), "stale fixture has no candidate");
  const auto before = world.fleets.back();
  world.fleets.back().mission_order_revision++;
  const auto result = controller.issue(frame, 3, view.revision, fleet_id,
                                       candidate->system_id, candidate->body_id);
  require(!result.accepted && !world.fleets.back().destination_system_id &&
              world.fleets.back().embarked_population_millions ==
                  before.embarked_population_millions,
          "stale settlement quote mutated its vessel");
  const auto old_generation = controller.issue(
      frame, 2, view.revision, fleet_id, candidate->system_id,
      candidate->body_id);
  require(!old_generation.accepted,
          "an old campaign generation issued a settlement order");
}

void admission_revalidation_tests(const fs::path &research_root,
                                  const fs::path &catalog) {
  auto prepare = [&](const std::int64_t seed) {
    auto frame = make_frame(research_root, catalog, seed);
    auto &world = frame.runtime().world().campaign();
    const auto player = world.player_civilization_id;
    for (const auto &system : world.systems)
      world.knowledge.mark_system_fully_surveyed(player, system.id);
    const auto fleet_id = next_fleet_id(world);
    world.fleets.push_back(authored_vessel(world, false, fleet_id));
    return frame;
  };

  {
    auto frame = prepare(127504);
    auto &world = frame.runtime().world().campaign();
    const auto player = world.player_civilization_id;
    NativeSettlementMissionController controller;
    const auto views = controller.build(frame, 20);
    require(views.size() == 1 && !views.front().candidates.empty(),
            "funding revalidation fixture has no settlement opportunity");
    const auto &view = views.front();
    const auto candidate = std::ranges::find_if(
        view.candidates, [](const auto &item) { return item.can_order; });
    require(candidate != view.candidates.end(),
            "funding revalidation fixture has no orderable destination");
    const auto fleet_id = view.fleet_id;
    const auto destination_system = candidate->system_id;
    const auto destination_body = candidate->body_id;
    const auto revision = view.revision;
    auto &economy = *std::ranges::find(world.economies, player,
                                        &CivilizationEconomy::civilization_id);
    economy.credits = 0.;
    const auto fleet_before = *std::ranges::find(world.fleets, fleet_id,
                                                  &FleetState::id);
    const auto result = controller.issue(frame, 20, revision, fleet_id,
                                         destination_system, destination_body);
    const auto &fleet_after = *std::ranges::find(world.fleets, fleet_id,
                                                  &FleetState::id);
    require(!result.accepted && economy.credits == 0. &&
                fleet_after.mission_order_revision ==
                    fleet_before.mission_order_revision &&
                fleet_after.destination_system_id ==
                    fleet_before.destination_system_id &&
                fleet_after.destination_planetary_body_id ==
                    fleet_before.destination_planetary_body_id,
            "current canonical funding denial spent funds or ordered a fleet");
  }

  {
    auto frame = prepare(127505);
    auto &world = frame.runtime().world().campaign();
    NativeSettlementMissionController controller;
    const auto views = controller.build(frame, 21);
    require(views.size() == 1 && !views.front().candidates.empty(),
            "knowledge revalidation fixture has no settlement opportunity");
    const auto &view = views.front();
    const auto candidate = std::ranges::find_if(
        view.candidates, [](const auto &item) { return item.can_order; });
    require(candidate != view.candidates.end(),
            "knowledge revalidation fixture has no orderable destination");
    const auto fleet_id = view.fleet_id;
    const auto destination_system = candidate->system_id;
    const auto destination_body = candidate->body_id;
    const auto revision = view.revision;
    const auto fleet_before = *std::ranges::find(world.fleets, fleet_id,
                                                  &FleetState::id);
    world.knowledge = CivilizationKnowledgeState{};
    const auto result = controller.issue(frame, 21, revision, fleet_id,
                                         destination_system, destination_body);
    const auto &fleet_after = *std::ranges::find(world.fleets, fleet_id,
                                                  &FleetState::id);
    require(!result.accepted &&
                fleet_after.mission_order_revision ==
                    fleet_before.mission_order_revision &&
                fleet_after.destination_system_id ==
                    fleet_before.destination_system_id &&
                fleet_after.destination_planetary_body_id ==
                    fleet_before.destination_planetary_body_id,
            "revoked observer knowledge issued or mutated a settlement order");
  }
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 3, "Usage: native_colony_controller_tests <research-root> <catalog>");
  const auto research_root = fs::absolute(argv[1]);
  const auto catalog = fs::absolute(argv[2]);
  inspector_tests(research_root, catalog);
  mission_case(research_root, catalog, false);
  mission_case(research_root, catalog, true);
  stale_mission_test(research_root, catalog);
  admission_revalidation_tests(research_root, catalog);
  std::cout << "native colony controllers: 6/6 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native colony controller tests failed: " << error.what() << '\n';
  return 1;
}
