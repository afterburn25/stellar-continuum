#include "native_settlement_mission_controller.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_persistence.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>

using namespace stellar::core;
using namespace stellar::native_colony;
namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

CampaignFrame make_frame(const fs::path &research_root,
                         const fs::path &catalog, std::int64_t seed) {
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

FleetState authored_vessel(const FreshCampaignState &world, bool outpost,
                           int id) {
  const auto player = std::ranges::find(world.civilizations,
                                         world.player_civilization_id,
                                         &Civilization::id);
  const auto home = std::ranges::find(world.systems, player->home_system_id,
                                       &StellarSystem::id);
  FleetState fleet;
  fleet.id = id;
  fleet.civilization_id = world.player_civilization_id;
  fleet.name = outpost ? "Authored Exact Outpost" : "Authored Exact Colony";
  fleet.role = FleetRole::Colony;
  fleet.design_id = outpost ? "resource_outpost_ship" : "colony_ship";
  fleet.current_system_id = player->home_system_id;
  fleet.position = {home->position.x, home->position.y};
  fleet.embarked_population_millions = outpost ? .8 : 4.5;
  fleet.embarked_population_species_id = player->species_id;
  fleet.maximum_leg_range_light_years = 1'000'000.;
  fleet.fuel_capacity_light_years = 1'000'000.;
  fleet.fuel_remaining_light_years = 1'000'000.;
  fleet.strategic_speed = 1'000'000.;
  return fleet;
}

int append_vessel(FreshCampaignState &world, bool outpost) {
  int id{};
  for (const auto &fleet : world.fleets) id = std::max(id, fleet.id + 1);
  world.fleets.push_back(authored_vessel(world, outpost, id));
  return id;
}

void survey_all(FreshCampaignState &world) {
  for (const auto &system : world.systems)
    world.knowledge.mark_system_fully_surveyed(world.player_civilization_id,
                                                system.id);
}

const NativeSettlementMissionView &mission(
    const std::vector<NativeSettlementMissionView> &views, int fleet_id) {
  const auto found = std::ranges::find(views, fleet_id,
                                       &NativeSettlementMissionView::fleet_id);
  if (found == views.end()) throw std::runtime_error("settlement vessel was not projected");
  return *found;
}

bool suggested(const NativeSettlementMissionView &view, int system_id,
               int body_id) {
  return std::ranges::any_of(view.candidates, [&](const auto &candidate) {
    return candidate.system_id == system_id && candidate.body_id == body_id;
  });
}

void outside_eight_multi_hop_colony(const fs::path &research_root,
                                    const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog, 132500);
  auto &simulation = frame.runtime().world();
  auto &world = simulation.campaign();
  survey_all(world);
  const int fleet_id = append_vessel(world, false);
  NativeSettlementMissionController controller;
  const auto suggestions = controller.build(frame, 12);
  const auto &bounded = mission(suggestions, fleet_id);
  require(bounded.candidates.size() <= 8, "native suggestion list exceeded its bound");
  const auto full = frame.runtime().core().get_colony_opportunity_plan(
      &simulation, fleet_id, 64);
  const auto target = std::ranges::find_if(full.candidates, [&](const auto &item) {
    return item.can_order && !suggested(bounded, item.system_id,
                                       item.planetary_body_id) &&
           item.reach.route_system_ids && item.reach.route_system_ids->size() > 2;
  });
  require(target != full.candidates.end(),
          "deterministic fixture has no orderable multi-hop colony target outside the first eight suggestions");
  const auto fleet_before = *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  auto &economy = *std::ranges::find(world.economies,
                                      world.player_civilization_id,
                                      &CivilizationEconomy::civilization_id);
  const double credits_before = economy.credits;
  const auto colonies_before = world.colonies.size();
  const double day_before = frame.clock().simulation_days();
  const auto payload_before = encode_player_campaign_v17_json(
      capture_player_campaign_v17(
          frame.runtime(), {day_before, "gate132", "2044-05-06T07:08:09Z"}));
  const auto preview = controller.preview_exact(
      frame, 12, fleet_id, target->system_id, target->planetary_body_id);
  require(preview.accepted && preview.candidate &&
              preview.requires_new_authorization &&
              preview.authorization_budget_units == 120. &&
              preview.candidate->system_id == target->system_id &&
              preview.candidate->body_id == target->planetary_body_id &&
              !suggested(bounded, target->system_id, target->planetary_body_id),
          "exact target preview remained restricted to the first eight suggestions");
  const auto &fleet_after_preview = *std::ranges::find(world.fleets, fleet_id,
                                                        &FleetState::id);
  const auto payload_after = encode_player_campaign_v17_json(
      capture_player_campaign_v17(
          frame.runtime(), {day_before, "gate132", "2044-05-06T07:08:09Z"}));
  require(economy.credits == credits_before && world.colonies.size() == colonies_before &&
              frame.clock().simulation_days() == day_before &&
              fleet_after_preview.mission_order_revision == fleet_before.mission_order_revision &&
              fleet_after_preview.destination_system_id == fleet_before.destination_system_id &&
              payload_after == payload_before,
          "exact target preview mutated canonical campaign state");
  economy.credits += 7.;
  const double authorization_base = economy.credits;
  const auto outcome = controller.issue_exact(frame, 12, preview.revision);
  require(outcome.accepted, "exact multi-hop colony order failed: " + outcome.message);
  const auto &ordered = *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  require(ordered.destination_system_id == target->system_id &&
              ordered.destination_planetary_body_id == target->planetary_body_id &&
              std::abs((authorization_base - economy.credits) - 120.) < 1e-9,
          "exact colony issue bypassed canonical paid route assignment");
  const auto active_status = controller.build(frame, 12);
  const auto &active = mission(active_status, fleet_id);
  require(active.destination_system_id == target->system_id &&
              active.destination_body_id == target->planetary_body_id &&
              active.settlement_days_completed == 0. &&
              active.establishment_days == 30.,
          "colony mission view lost canonical destination or establishment status");
  const auto retargets = frame.runtime().core().get_colony_opportunity_plan(
      &simulation, fleet_id, 64);
  const auto retarget = std::ranges::find_if(
      retargets.candidates, [&](const auto &item) {
        return item.can_order &&
               (item.system_id != target->system_id ||
                item.planetary_body_id != target->planetary_body_id);
      });
  require(retarget != retargets.candidates.end(),
          "colony retarget fixture has no second canonical destination");
  const auto retarget_preview = controller.preview_exact(
      frame, 12, fleet_id, retarget->system_id, retarget->planetary_body_id);
  require(retarget_preview.accepted &&
              !retarget_preview.requires_new_authorization &&
              retarget_preview.authorization_budget_units == 0.,
          "an authorized colony retarget quoted a second expedition charge");
  const double before_retarget = economy.credits;
  const auto retargeted =
      controller.issue_exact(frame, 12, retarget_preview.revision);
  require(retargeted.accepted && economy.credits == before_retarget,
          "an authorized colony retarget charged the expedition again");
}

void same_system_outpost(const fs::path &research_root,
                         const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog, 132501);
  auto &simulation = frame.runtime().world();
  auto &world = simulation.campaign();
  survey_all(world);
  const int fleet_id = append_vessel(world, true);
  NativeSettlementMissionController controller;
  const auto suggestions = controller.build(frame, 13);
  const auto &bounded = mission(suggestions, fleet_id);
  const auto full = frame.runtime().core().get_resource_outpost_opportunity_plan(
      &simulation, fleet_id, 64);
  const auto target = std::ranges::find_if(full.candidates, [&](const auto &item) {
    return item.can_order && !suggested(bounded, item.system_id,
                                        item.planetary_body_id);
  });
  require(target != full.candidates.end(),
          "deterministic fixture has no orderable outpost target outside the first eight suggestions");
  auto &fleet = *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  const auto system = std::ranges::find(world.systems, target->system_id,
                                         &StellarSystem::id);
  fleet.current_system_id = target->system_id;
  fleet.position = {system->position.x, system->position.y};
  const auto preview = controller.preview_exact(
      frame, 13, fleet_id, target->system_id, target->planetary_body_id);
  require(preview.accepted && preview.candidate &&
              preview.kind == NativeSettlementMissionKind::ResourceOutpost &&
              preview.requires_new_authorization &&
              preview.authorization_budget_units == 90. &&
              preview.candidate->reach.is_supported &&
              preview.candidate->distance_from_fleet == 0.,
          "canonical exact assessment rejected a same-system outpost target");
  auto &economy = *std::ranges::find(world.economies,
                                      world.player_civilization_id,
                                      &CivilizationEconomy::civilization_id);
  const double credits_before = economy.credits;
  const auto outcome = controller.issue_exact(frame, 13, preview.revision);
  require(outcome.accepted && std::abs((credits_before - economy.credits) - 90.) < 1e-9 &&
              fleet.destination_planetary_body_id == target->planetary_body_id,
          "same-system exact outpost did not use canonical paid command");
  const auto active_status = controller.build(frame, 13);
  const auto &active = mission(active_status, fleet_id);
  require(active.destination_body_id == target->planetary_body_id &&
              active.settlement_days_completed == 0. &&
              active.establishment_days == 20.,
          "outpost mission view lost canonical destination or establishment status");
  const auto retargets =
      frame.runtime().core().get_resource_outpost_opportunity_plan(
          &simulation, fleet_id, 64);
  const auto retarget = std::ranges::find_if(
      retargets.candidates, [&](const auto &item) {
        return item.can_order &&
               (item.system_id != target->system_id ||
                item.planetary_body_id != target->planetary_body_id);
      });
  require(retarget != retargets.candidates.end(),
          "outpost retarget fixture has no second canonical destination");
  const auto retarget_preview = controller.preview_exact(
      frame, 13, fleet_id, retarget->system_id, retarget->planetary_body_id);
  require(retarget_preview.accepted &&
              !retarget_preview.requires_new_authorization &&
              retarget_preview.authorization_budget_units == 0.,
          "an authorized outpost retarget quoted a second expedition charge");
  const double before_retarget = economy.credits;
  const auto retargeted =
      controller.issue_exact(frame, 13, retarget_preview.revision);
  require(retargeted.accepted && economy.credits == before_retarget,
          "an authorized outpost retarget charged the expedition again");
}

void authorization_state_change(const fs::path &research_root,
                                const fs::path &catalog, bool outpost,
                                std::int64_t seed) {
  auto frame = make_frame(research_root, catalog, seed);
  auto &simulation = frame.runtime().world();
  auto &world = simulation.campaign();
  survey_all(world);
  const int fleet_id = append_vessel(world, outpost);
  int system_id{}, body_id{};
  if (outpost) {
    const auto plan =
        frame.runtime().core().get_resource_outpost_opportunity_plan(
            &simulation, fleet_id, 64);
    const auto target = std::ranges::find_if(
        plan.candidates, [](const auto &item) { return item.can_order; });
    require(target != plan.candidates.end(),
            "authorization-state outpost fixture has no destination");
    system_id = target->system_id;
    body_id = target->planetary_body_id;
  } else {
    const auto plan = frame.runtime().core().get_colony_opportunity_plan(
        &simulation, fleet_id, 64);
    const auto target = std::ranges::find_if(
        plan.candidates, [](const auto &item) { return item.can_order; });
    require(target != plan.candidates.end(),
            "authorization-state colony fixture has no destination");
    system_id = target->system_id;
    body_id = target->planetary_body_id;
  }
  NativeSettlementMissionController controller;
  const auto preview =
      controller.preview_exact(frame, 14, fleet_id, system_id, body_id);
  require(preview.accepted && preview.requires_new_authorization,
          "new expedition did not quote its canonical authorization");
  auto &fleet = *std::ranges::find(world.fleets, fleet_id, &FleetState::id);
  fleet.destination_planetary_body_id = body_id;
  const auto before = fleet;
  const auto &economy = *std::ranges::find(
      world.economies, world.player_civilization_id,
      &CivilizationEconomy::civilization_id);
  const double credits_before = economy.credits;
  const auto denied = controller.issue_exact(frame, 14, preview.revision);
  require(!denied.accepted && economy.credits == credits_before &&
              fleet.mission_order_revision == before.mission_order_revision &&
              fleet.destination_system_id == before.destination_system_id &&
              fleet.destination_planetary_body_id ==
                  before.destination_planetary_body_id,
          "changed expedition authorization state used an old exact preview");
}

struct Prepared {
  CampaignFrame frame;
  int fleet_id{}, system_id{}, body_id{};
};

Prepared prepare_valid(const fs::path &research_root, const fs::path &catalog,
                       std::int64_t seed) {
  auto frame = make_frame(research_root, catalog, seed);
  auto &world = frame.runtime().world().campaign();
  survey_all(world);
  const int fleet_id = append_vessel(world, false);
  const auto plan = frame.runtime().core().get_colony_opportunity_plan(
      &frame.runtime().world(), fleet_id, 64);
  const auto candidate = std::ranges::find_if(plan.candidates,
                                               [](const auto &x) { return x.can_order; });
  if (candidate == plan.candidates.end())
    throw std::runtime_error("revalidation fixture has no colony target");
  return {std::move(frame), fleet_id, candidate->system_id,
          candidate->planetary_body_id};
}

void bounded_live_status(const fs::path &research_root,
                         const fs::path &catalog) {
  auto prepared = prepare_valid(research_root, catalog, 132509);
  auto &world = prepared.frame.runtime().world().campaign();
  NativeSettlementMissionController controller;
  const auto exact = controller.preview_exact(
      prepared.frame, 25, prepared.fleet_id, prepared.system_id,
      prepared.body_id);
  require(exact.accepted,
          "live-status fixture could not prepare an exact command");

  const auto selected = controller.live_status(prepared.frame, 25,
                                                prepared.fleet_id);
  require(selected && selected->fleet_id == prepared.fleet_id &&
              selected->status == "Ready for settlement orders" &&
              !selected->destination_system_id &&
              !selected->destination_body_id,
          "owned populated settlement vessel lost bounded live status");

  const auto original = *std::ranges::find(world.fleets, prepared.fleet_id,
                                            &FleetState::id);
  auto foreign = original;
  foreign.id += 1;
  foreign.civilization_id = world.player_civilization_id + 1;
  auto inactive = original;
  inactive.id += 2;
  inactive.is_active = false;
  auto empty = original;
  empty.id += 3;
  empty.embarked_population_millions = 0.;
  world.fleets.push_back(std::move(foreign));
  world.fleets.push_back(std::move(inactive));
  world.fleets.push_back(std::move(empty));
  for (int index = 0; index < 128; ++index) {
    auto unrelated = original;
    unrelated.id += 100 + index;
    unrelated.role = FleetRole::Scout;
    world.fleets.push_back(std::move(unrelated));
  }
  require(!controller.live_status(prepared.frame, 25, original.id + 1) &&
              !controller.live_status(prepared.frame, 25, original.id + 2) &&
              !controller.live_status(prepared.frame, 25, original.id + 3),
          "bounded live status exposed a foreign, inactive, or empty vessel");
  require(controller.live_status(prepared.frame, 25, prepared.fleet_id).has_value(),
          "unrelated fleets displaced the selected live status");

  const auto outcome = controller.issue_exact(prepared.frame, 25,
                                               exact.revision);
  require(outcome.accepted,
          "a live-status refresh invalidated the prepared exact command");
}

void denial_revalidation(const fs::path &research_root,
                         const fs::path &catalog) {
  {
    auto prepared = prepare_valid(research_root, catalog, 132502);
    auto &world = prepared.frame.runtime().world().campaign();
    NativeSettlementMissionController controller;
    const auto preview = controller.preview_exact(prepared.frame, 20,
        prepared.fleet_id, prepared.system_id, prepared.body_id);
    require(preview.accepted, "funding fixture preview failed");
    auto &economy = *std::ranges::find(world.economies,
        world.player_civilization_id, &CivilizationEconomy::civilization_id);
    economy.credits = 0.;
    const auto before = *std::ranges::find(world.fleets, prepared.fleet_id,
                                            &FleetState::id);
    const auto denied = controller.issue_exact(prepared.frame, 20, preview.revision);
    const auto &after = *std::ranges::find(world.fleets, prepared.fleet_id,
                                           &FleetState::id);
    require(!denied.accepted && economy.credits == 0. &&
                after.mission_order_revision == before.mission_order_revision &&
                after.destination_system_id == before.destination_system_id,
            "current funding denial spent or ordered through an exact quote");
  }
  {
    auto prepared = prepare_valid(research_root, catalog, 132503);
    auto &world = prepared.frame.runtime().world().campaign();
    NativeSettlementMissionController controller;
    const auto preview = controller.preview_exact(prepared.frame, 21,
        prepared.fleet_id, prepared.system_id, prepared.body_id);
    world.knowledge = CivilizationKnowledgeState{};
    const auto before = *std::ranges::find(world.fleets, prepared.fleet_id,
                                            &FleetState::id);
    const auto denied = controller.issue_exact(prepared.frame, 21, preview.revision);
    const auto &after = *std::ranges::find(world.fleets, prepared.fleet_id,
                                           &FleetState::id);
    require(!denied.accepted && after.mission_order_revision == before.mission_order_revision &&
                after.destination_system_id == before.destination_system_id,
            "revoked knowledge issued an exact settlement order");
  }
  {
    auto prepared = prepare_valid(research_root, catalog, 132504);
    auto &world = prepared.frame.runtime().world().campaign();
    NativeSettlementMissionController controller;
    const auto first = controller.preview_exact(prepared.frame, 22,
        prepared.fleet_id, prepared.system_id, prepared.body_id);
    const auto second = controller.preview_exact(prepared.frame, 22,
        prepared.fleet_id, prepared.system_id, prepared.body_id);
    require(!controller.issue_exact(prepared.frame, 22, first.revision).accepted,
            "a superseded exact preview remained usable");
    auto &fleet = *std::ranges::find(world.fleets, prepared.fleet_id,
                                      &FleetState::id);
    ++fleet.mission_order_revision;
    const auto before_destination = fleet.destination_system_id;
    require(!controller.issue_exact(prepared.frame, 22, second.revision).accepted &&
                fleet.destination_system_id == before_destination,
            "changed mission order revision used an exact preview");
    require(!controller.issue_exact(prepared.frame, 21, second.revision).accepted,
            "an old campaign generation used an exact preview");
  }
  {
    auto prepared = prepare_valid(research_root, catalog, 132505);
    auto &world = prepared.frame.runtime().world().campaign();
    NativeSettlementMissionController controller;
    auto &fleet = *std::ranges::find(world.fleets, prepared.fleet_id,
                                      &FleetState::id);
    fleet.civilization_id = world.player_civilization_id + 1;
    const auto preview = controller.preview_exact(prepared.frame, 23,
        prepared.fleet_id, prepared.system_id, prepared.body_id);
    require(!preview.accepted && !preview.candidate &&
                preview.message.find("populated settlement vessel") != std::string::npos,
            "foreign fleet exact preview exposed target facts");
  }
  {
    auto prepared = prepare_valid(research_root, catalog, 132506);
    auto &world = prepared.frame.runtime().world().campaign();
    const auto system = std::ranges::find(world.systems, prepared.system_id,
                                           &StellarSystem::id);
    const auto body = std::ranges::find_if(
        world.bodies, [&](const PlanetaryBody &item) {
          return item.id == prepared.body_id &&
                 item.system_id == prepared.system_id;
        });
    require(system != world.systems.end() &&
                body != world.bodies.end(),
            "unsurveyed fixture target is absent");
    const auto system_name = system->name;
    const auto body_name = body->name;
    world.knowledge = CivilizationKnowledgeState{};
    world.knowledge.reveal_system(world.player_civilization_id,
                                  prepared.system_id);
    NativeSettlementMissionController controller;
    const auto denied = controller.preview_exact(
        prepared.frame, 24, prepared.fleet_id, prepared.system_id,
        prepared.body_id);
    require(!denied.accepted && !denied.candidate &&
                denied.message.find(system_name) == std::string::npos &&
                denied.message.find(body_name) == std::string::npos,
            "unsurveyed exact preview leaked destination facts");
  }
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 3, "Usage: native_settlement_targeting_tests <research-root> <catalog>");
  const auto research_root = fs::absolute(argv[1]);
  const auto catalog = fs::absolute(argv[2]);
  outside_eight_multi_hop_colony(research_root, catalog);
  same_system_outpost(research_root, catalog);
  authorization_state_change(research_root, catalog, false, 132507);
  authorization_state_change(research_root, catalog, true, 132508);
  bounded_live_status(research_root, catalog);
  denial_revalidation(research_root, catalog);
  std::cout << "native exact settlement targeting: 17/17 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native exact settlement targeting failed: " << error.what() << '\n';
  return 1;
}
