#include "native_outpost_freight_controller.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_persistence.hpp>
using namespace stellar::core;
using namespace stellar::native_colony;
namespace fs = std::filesystem;
namespace {
void require(bool v, const std::string &m) {
  if (!v)
    throw std::runtime_error(m);
}
std::string capture(CampaignFrame &f) {
  return encode_player_campaign_v17_json(capture_player_campaign_v17(
      f.runtime(),
      {f.clock().simulation_days(), "freight-test", "2044-05-06T07:08:09Z"}));
}
FleetState freighter(int id, int player, int system) {
  FleetState f;
  f.id = id;
  f.name = "Freighter " + std::to_string(id);
  f.civilization_id = player;
  f.role = FleetRole::Logistics;
  f.design_id = "bulk_freighter";
  f.current_system_id = system;
  f.cargo_material_capacity = 100.;
  return f;
}
} // namespace
int main(int argc, char **argv) try {
  require(argc == 3, "usage");
  auto seeded = seed_persistable_fresh_campaign(
      127506, load_nearby_catalog(fs::absolute(argv[2])),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  StrategicClock clock;
  clock.set_speed(StrategicSpeed::Paused);
  CampaignFrame frame(
      IntegratedAdaptiveCampaignRuntime::create_fresh(
          load_adaptive_research_strategic_runtime(fs::absolute(argv[1])),
          std::move(seeded)),
      std::move(clock), CampaignFramePolicy::Developer);
  auto &world = frame.runtime().world().campaign();
  const int player = world.player_civilization_id;
  auto home = std::ranges::find_if(world.colonies, [&](const Colony &c) {
    return c.civilization_id == player && c.kind == SettlementKind::Colony;
  });
  require(home != world.colonies.end() && home->planetary_body_id, "home");
  const int home_colony_id = home->id;
  const int home_system_id = home->system_id;
  const int home_body_id = *home->planetary_body_id;
  auto source =
      std::ranges::find(world.bodies, home_body_id, &PlanetaryBody::id);
  require(source != world.bodies.end(), "body");
  auto deposit = *source;
  deposit.id = 99990;
  deposit.name = "Test Deposit";
  deposit.has_rare_resource = true;
  world.bodies.push_back(deposit);
  Colony outpost = *home;
  outpost.id = 99991;
  outpost.planetary_body_id = deposit.id;
  outpost.kind = SettlementKind::ResourceOutpost;
  outpost.name = "Test Outpost";
  outpost.stored_extracted_materials = 20.;
  outpost.remaining_extractable_materials = 500.;
  world.colonies.push_back(outpost);
  const auto remote_body =
      std::ranges::find_if(world.bodies, [&](const PlanetaryBody &body) {
        return body.system_id != home_system_id;
      });
  require(remote_body != world.bodies.end(), "remote body");
  Colony remote_home = outpost;
  remote_home.id = 99989;
  remote_home.system_id = remote_body->system_id;
  remote_home.planetary_body_id = remote_body->id;
  remote_home.kind = SettlementKind::Colony;
  remote_home.name = "Remote Test Home";
  remote_home.stored_extracted_materials = 0.;
  remote_home.remaining_extractable_materials.reset();
  world.colonies.push_back(remote_home);
  world.knowledge.mark_system_fully_surveyed(player, home_system_id);
  auto unreachable = freighter(98999, player, remote_home.system_id);
  unreachable.maximum_leg_range_light_years = 0.01;
  unreachable.fuel_remaining_light_years = 0.01;
  world.fleets.push_back(unreachable);
  auto stranded = freighter(99000, player, home_system_id);
  stranded.current_system_id.reset();
  world.fleets.push_back(stranded);
  auto busy = freighter(99001, player, home_system_id);
  busy.destination_system_id = home_system_id;
  world.fleets.push_back(busy);
  auto loaded = freighter(99002, player, home_system_id);
  loaded.cargo_materials = 1.;
  world.fleets.push_back(loaded);
  world.fleets.push_back(freighter(99003, player, home_system_id));
  world.fleets.push_back(freighter(99004, player, home_system_id));
  auto eligible = [&]() -> FleetState & {
    return *std::ranges::find(world.fleets, 99003, &FleetState::id);
  };
  auto higher = [&]() -> FleetState & {
    return *std::ranges::find(world.fleets, 99004, &FleetState::id);
  };
  NativeColonyView view;
  view.campaign_generation = 1;
  view.player_civilization_id = player;
  view.colony_id = outpost.id;
  view.body_id = deposit.id;
  view.system_id = home_system_id;
  view.resource_outpost = true;
  NativeOutpostFreightController controller;
  auto before = capture(frame);
  auto quote = controller.preview(frame, 1, view);
  require(quote.accepted, "preview: " + quote.message);
  require(quote.fleet_id == 99003,
          "unreachable lower-ID freighter blocked the reachable candidate");
  require(capture(frame) == before, "preview mutation");
  auto result = controller.issue(frame, 1, quote.revision);
  require(result.accepted, "issue: " + result.message);
  require(eligible().freight_target_outpost_id == outpost.id, "Core dispatch");
  auto accepted = capture(frame);
  require(!controller.issue(frame, 1, quote.revision).accepted &&
              capture(frame) == accepted,
          "replay");
  eligible() = freighter(99003, player, home_system_id);
  view.campaign_generation = 2;
  quote = controller.preview(frame, 2, view);
  require(quote.accepted, "mutation setup");
  eligible().fuel_remaining_light_years -= 1.;
  before = capture(frame);
  require(!controller.issue(frame, 2, quote.revision).accepted &&
              capture(frame) == before,
          "fuel mutation");
  eligible().fuel_remaining_light_years += 1.;
  view.campaign_generation = 3;
  quote = controller.preview(frame, 3, view);
  require(quote.accepted, "duplicate setup");
  world.fleets.push_back(eligible());
  before = capture(frame);
  require(!controller.issue(frame, 3, quote.revision).accepted &&
              capture(frame) == before,
          "duplicate mutation");
  world.fleets.pop_back();
  view.campaign_generation = 4;
  quote = controller.preview(frame, 4, view);
  require(quote.accepted, "number setup");
  eligible().mission_order_revision += 1;
  before = capture(frame);
  require(!controller.issue(frame, 4, quote.revision).accepted &&
              capture(frame) == before,
          "mission revision mutation");
  eligible().mission_order_revision -= 1;
  view.campaign_generation = 5;
  quote = controller.preview(frame, 5, view);
  controller.clear();
  before = capture(frame);
  require(!controller.issue(frame, 5, quote.revision).accepted &&
              capture(frame) == before,
          "clear");
  auto wrong = view;
  wrong.campaign_generation = 6;
  wrong.player_civilization_id++;
  require(!controller.preview(frame, 6, wrong).accepted, "wrong observer");

  view.campaign_generation = 5;
  before = capture(frame);
  require(!controller.preview(frame, 5, view).accepted &&
              capture(frame) == before,
          "stale generation replaced the active generation or mutated state");

  view.campaign_generation = 7;
  quote = controller.preview(frame, 7, view);
  require(quote.accepted, "unpaused confirmation setup failed");
  frame.clock().set_speed(StrategicSpeed::Normal);
  before = capture(frame);
  require(!controller.issue(frame, 7, quote.revision).accepted &&
              capture(frame) == before,
          "unpaused confirmation mutated canonical state");
  frame.clock().set_speed(StrategicSpeed::Paused);

  eligible().mission_order_revision = std::numeric_limits<int>::max();
  higher().destination_system_id = home_system_id;
  view.campaign_generation = 8;
  before = capture(frame);
  require(!controller.preview(frame, 8, view).accepted &&
              capture(frame) == before,
          "mission revision exhaustion escaped copied-state preflight");
  eligible().mission_order_revision = 0;
  higher().destination_system_id.reset();

  before = capture(frame);
  world.economies.push_back(*std::ranges::find(
      world.economies, player, &CivilizationEconomy::civilization_id));
  const auto duplicate_economy_count = world.economies.size();
  view.campaign_generation = 9;
  require(!controller.preview(frame, 9, view).accepted &&
              world.economies.size() == duplicate_economy_count,
          "duplicate player economy was accepted or structurally mutated");
  world.economies.pop_back();
  require(capture(frame) == before,
          "duplicate player economy rejection mutated canonical state");

  before = capture(frame);
  world.civilizations.push_back(
      *std::ranges::find(world.civilizations, player, &Civilization::id));
  const auto duplicate_player_count = world.civilizations.size();
  view.campaign_generation = 10;
  require(!controller.preview(frame, 10, view).accepted &&
              world.civilizations.size() == duplicate_player_count,
          "duplicate player civilization was accepted or structurally mutated");
  world.civilizations.pop_back();
  require(capture(frame) == before,
          "duplicate player civilization rejection mutated canonical state");

  auto second_home_body = deposit;
  second_home_body.id = 99988;
  second_home_body.name = "Second Home Body";
  second_home_body.has_rare_resource = false;
  world.bodies.push_back(second_home_body);
  Colony second_home = outpost;
  second_home.id = 99987;
  second_home.planetary_body_id = second_home_body.id;
  second_home.kind = SettlementKind::Colony;
  second_home.name = "Second Developed Home";
  second_home.stored_extracted_materials = 0.;
  second_home.remaining_extractable_materials.reset();
  world.colonies.push_back(second_home);
  view.campaign_generation = 11;
  before = capture(frame);
  quote = controller.preview(frame, 11, view);
  require(
      quote.accepted && quote.home_colony_id == home_colony_id,
      "multiple developed homes did not retain Core's canonical first home");
  require(capture(frame) == before,
          "multiple-home preview mutated canonical state");
  controller.clear();

  eligible().strategic_speed = std::numeric_limits<double>::quiet_NaN();
  higher().strategic_speed = std::numeric_limits<double>::quiet_NaN();
  view.campaign_generation = 12;
  require(!controller.preview(frame, 12, view).accepted,
          "invalid numeric fleet was selectable");
  std::cout << "native outpost freight controller checks passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << "native outpost freight controller tests failed: " << e.what()
            << '\n';
  return 1;
}
