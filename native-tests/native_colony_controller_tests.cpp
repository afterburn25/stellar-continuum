#include "native_colony_controller.hpp"
#include <stellar/core/campaign_observation.hpp>
#include <stellar/core/developer_campaign.hpp>
#include "native_surface_status.hpp"
#include "native_settlement_mission_controller.hpp"

#include "../../app/native_client/native_system_view.hpp"

#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/surface_construction.hpp>

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
                        view.science_per_day,
                        view.active_research_lab_units};
  require(std::ranges::all_of(values, [](const double value) {
            return std::isfinite(value);
          }),
          "colony projection emitted non-finite operational telemetry");
}

void surface_operation_tests(const fs::path &research_root, const fs::path &catalog) {
  using stellar::native_colony_ui::surface_site_status;
  auto frame = make_frame(research_root, catalog, 127506);
  auto &world = frame.runtime().world().campaign();
  const auto player = world.player_civilization_id;
  auto &colony = *std::ranges::find(world.colonies, player, &Colony::civilization_id);
  require(colony.planetary_body_id.has_value(), "operation fixture lacks a body");
  world.knowledge.mark_system_fully_surveyed(player, colony.system_id);
  NativeSystemViewController systems;
  const auto system = systems.build(frame, 7, colony.system_id);
  NativeColonyController controller;
  colony.population_millions = 1.;
  colony.surface_buildings.clear();
  for (int id = 1; id <= 6; ++id) {
    SurfaceBuilding building;
    building.id = 9000 + id;
    building.type_id = id == 1 ? "power_generator" : "science_lab";
    building.x = static_cast<float>(id * 70);
    building.is_complete = id != 6;
    building.is_enabled = id != 4;
    building.condition = id == 5 ? minimum_operational_condition : 1.;
    building.industry_progress = building.is_complete ?
        find_surface_building(building.type_id)->industry_cost : 20.;
    colony.surface_buildings.push_back(building);
  }
  (void)frame.advance(1.);
  const auto project = [&] {
    const auto output = surface_colony_output(colony);
    const auto result = controller.build(frame, 7, *system.snapshot,
                                         *colony.planetary_body_id);
    require(result.view.has_value(), "operation fixture was not projected");
    for (const auto &site : result.view->construction_sites) {
      require(site.powered == std::ranges::contains(output.powered_building_ids, site.building_id) &&
                  site.staffed == std::ranges::contains(output.staffed_building_ids, site.building_id),
              "surface status diverged from authoritative allocation");
    }
    return *result.view;
  };
  const auto label = [](const NativeColonyView &view, int id) {
    const auto site = std::ranges::find(view.construction_sites, id,
                                       &NativeSurfaceSite::building_id);
    require(site != view.construction_sites.end(), "missing operational site");
    return surface_site_status(*site).label;
  };
  const auto normal = project();
  const auto *research = frame.runtime().research().try_get_civilization(player);
  require(research != nullptr, "operation fixture has no adaptive research state");
  const auto context_id = "colony:" + std::to_string(colony.id);
  int expected_active_facilities{};
  double expected_effective_labs{};
  const auto &institution_catalog =
      frame.runtime().research_runtime().authority().expertise_catalog();
  for (const auto &institution : research->expertise().institutions()) {
    if (institution.context_id != std::optional<std::string>{context_id} ||
        institution.active_count <= 0)
      continue;
    expected_active_facilities += institution.active_count;
    expected_effective_labs += institution_catalog
                                   .get_institution(institution.institution_archetype_id)
                                   .effective_lab_units * institution.active_count;
  }
  require(normal.active_research_facilities == expected_active_facilities &&
              std::abs(normal.active_research_lab_units - expected_effective_labs) < 1e-9,
          "colony research capacity was not projected from active local institutions");
  const auto unchanged = project();
  require(unchanged.revision == normal.revision,
          "an unchanged colony projection advanced its revision");
  const auto site = [&](const NativeColonyView &view, const int id)
      -> const NativeSurfaceSite & {
    const auto found = std::ranges::find(view.construction_sites, id,
                                         &NativeSurfaceSite::building_id);
    require(found != view.construction_sites.end(),
            "missing management projection site");
    return *found;
  };
  const auto &generator = site(normal, 9001);
  const auto &lab = site(normal, 9002);
  const auto &damaged = site(normal, 9005);
  require(generator.essential_service && !lab.essential_service &&
              lab.can_upgrade && lab.pending_upgrade_type_id == std::nullopt &&
              lab.upgrade_name ==
                  find_surface_building("advanced_science_lab")->name &&
              lab.upgrade_credit_budget_units ==
                  surface_upgrade_authorization_cost(
                      ConstructionReadView{world.civilizations, world.bodies,
                                           world.construction, world.colonies,
                                           world.economies, {}, {}},
                      colony, *find_surface_building("science_lab")) &&
              lab.upgrade_industry_cost ==
                  find_surface_building("science_lab")->upgrade_industry_cost &&
              damaged.repair_industry_cost ==
                  surface_repair_industry_cost(colony.surface_buildings[4]),
          "surface management projection diverged from canonical costs or service priority");
  require(generator.can_upgrade && !generator.upgrade_lock_reason.empty(),
          "surface upgrade lock was not projected for a visible upgrade path");
  const auto hub_cost = surface_hub_upgrade_cost(
      ConstructionReadView{world.civilizations, world.bodies,
                           world.construction, world.colonies,
                           world.economies, {}, {}},
      colony);
  require(!normal.hub_name.empty() && normal.hub_upgrade_available ==
              hub_cost.has_value() &&
              normal.hub_upgrade_credit_budget_units ==
                  (hub_cost ? hub_cost->credit_cost : 0.) &&
              normal.hub_upgrade_industry_cost ==
                  (hub_cost ? hub_cost->industry_cost : 0.),
          "hub management projection diverged from canonical upgrade costs");
  const auto original_revision = normal.revision;
  colony.surface_buildings[1].pending_upgrade_type_id = "advanced_science_lab";
  colony.surface_buildings[1].upgrade_days_remaining = 4.;
  const auto pending = project();
  require(!site(pending, 9002).can_upgrade &&
              site(pending, 9002).upgrade_days_remaining == 4. &&
              pending.revision > original_revision,
          "pending surface upgrade remained actionable or failed to invalidate revision");
  colony.surface_buildings[1].pending_upgrade_type_id.reset();
  colony.surface_buildings[1].upgrade_days_remaining = 0.;
  auto &economy = *std::ranges::find(world.economies, player,
                                      &CivilizationEconomy::civilization_id);
  const auto credits = economy.credits;
  const auto industry = economy.industry;
  economy.credits = 0.;
  economy.industry = 0.;
  const auto unaffordable = project();
  require(site(unaffordable, 9002).can_upgrade &&
              !site(unaffordable, 9002).can_afford_upgrade &&
              !site(unaffordable, 9005).can_afford_repair &&
              unaffordable.revision > pending.revision,
          "surface management affordability did not invalidate the quote");
  economy.credits = credits;
  economy.industry = industry;
  colony.surface_hub_upgrade_days_remaining = 3.;
  const auto pending_hub = project();
  require(!pending_hub.hub_upgrade_available &&
              pending_hub.hub_upgrade_days_remaining == 3.,
          "pending hub expansion remained actionable");
  colony.surface_hub_upgrade_days_remaining = 0.;
  require(label(normal, 9002) == "OPERATING" && label(normal, 9004) == "DISABLED" &&
              label(normal, 9005) == "REPAIR NEEDED" && label(normal, 9006) == "CONSTRUCTION",
          "facility completion was confused with operational readiness");
  colony.surface_buildings.front().is_enabled = false;
  const auto low_power = project();
  require(label(low_power, 9001) == "DISABLED" && label(low_power, 9002) == "OPERATING" &&
              label(low_power, 9003) == "NO POWER" && low_power.revision > normal.revision,
          "power allocation or revision was not visible to the player");
  colony.surface_buildings.front().is_enabled = true;
  colony.population_millions = .08;
  const auto low_labor = project();
  require(label(low_labor, 9001) == "OPERATING" && label(low_labor, 9002) == "NO WORKERS" &&
              label(low_labor, 9005) == "REPAIR NEEDED" && low_labor.revision > low_power.revision,
          "unstaffed or damaged site misleadingly reported a power shortage");
  require(label(normal, 9002) == "OPERATING" && label(low_power, 9003) == "NO POWER",
          "refresh mutated an earlier value-owned view");
}

void developer_inspection_tests(const fs::path &research_root, const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog, 127500);
  auto& world = frame.runtime().world().campaign();
  auto foreign = std::ranges::find_if(world.colonies, [&](const auto& c) {
    return c.civilization_id != world.player_civilization_id && c.planetary_body_id;
  });
  require(foreign != world.colonies.end(), "Missing alien colony fixture");
  world.knowledge = CivilizationKnowledgeState{};
  NativeSystemViewController systems; NativeColonyController colonies;
  require(!systems.build(frame, 8, foreign->system_id).snapshot,
          "Developer frame timing granted secret data without provenance");
  world.developer_provenance.emplace();
  require(!world.developer_provenance->full_exploration, "Fixture accidentally revealed the map");
  auto economy = std::ranges::find(world.economies, foreign->civilization_id, &CivilizationEconomy::civilization_id);
  require(economy != world.economies.end(), "Missing alien economy");
  economy->credits = 987654.; economy->industry = 12345.;
  foreign->population_millions = 4321.;
  const auto capture = [&] { return capture_developer_campaign_json(frame.runtime(),
      {0., "inspection-test", "2044-05-06T07:08:09Z"}); };
  const auto before = capture();
  const auto system = systems.build(frame, 8, foreign->system_id);
  require(system.snapshot && std::ranges::all_of(system.snapshot->bodies, [](const auto& b) { return b.details && b.world_class; }),
          "Developer inspection withheld physical data or world classification");
  const auto result = colonies.build(frame, 8, *system.snapshot, *foreign->planetary_body_id);
  require(result.view && result.view->developer_inspection && result.view->foreign_settlement && !result.view->observer_only,
          "Developer alien colony still uses restricted observer view");
  const auto& view = *result.view;
  require(view.owner_civilization_id == foreign->civilization_id && view.player_civilization_id == world.player_civilization_id &&
      view.population_millions == 4321. && view.treasury_budget_units == 987654. && view.stored_industry == 12345. &&
      view.construction_sites.size() == foreign->surface_buildings.size(), "Alien statistics use player data or omit structures");
  require_finite(view);
  require(before == capture(), "Developer inspection changed canonical campaign state");
  foreign->population_millions += 1.;
  const auto refreshed = colonies.build(frame, 8, *system.snapshot, *foreign->planetary_body_id);
  require(refreshed.view && refreshed.view->revision > view.revision && refreshed.view->population_millions == 4322.,
          "Developer colony statistics did not refresh");
  world.developer_provenance.reset();
  require(!colonies.build(frame, 8, *system.snapshot, *foreign->planetary_body_id).view,
          "Stale developer snapshot leaked after returning to ordinary observation");
  world.knowledge.mark_system_fully_surveyed(world.player_civilization_id, foreign->system_id);
  require(!colonies.build(frame, 8, *system.snapshot, *foreign->planetary_body_id).view,
          "Full exploration alone revealed alien internal statistics");
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
  require(frozen.homeworld, "seeded home colony lost its Core homeworld identity");
  const auto dependent = std::ranges::find_if(world.colonies, [&](const auto &candidate) {
    return candidate.civilization_id == player && candidate.system_id == owned->system_id &&
           candidate.id != owned->id && candidate.planetary_body_id;
  });
  if (dependent != world.colonies.end()) {
    const auto other = controller.build(frame, 7, *system.snapshot, *dependent->planetary_body_id);
    require(other.view && !other.view->homeworld,
            "another colony in the home system received capital identity");
  }
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
  developer_inspection_tests(research_root, catalog);
  inspector_tests(research_root, catalog);
  surface_operation_tests(research_root, catalog);
  mission_case(research_root, catalog, false);
  mission_case(research_root, catalog, true);
  stale_mission_test(research_root, catalog);
  admission_revalidation_tests(research_root, catalog);
  std::cout << "native colony controllers: 8/8 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native colony controller tests failed: " << error.what() << '\n';
  return 1;
}
