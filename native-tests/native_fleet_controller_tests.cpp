#include "native_fleet_controller.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_recovery.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;
using namespace stellar::native_fleet;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read fixture.");
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

void write(const fs::path &path, std::string_view value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output) throw std::runtime_error("Cannot write campaign fixture.");
}

[[nodiscard]] std::string source_player17(const fs::path &fixture) {
  const auto root = Json::parse(read(fixture));
  for (const auto &row : root.at("Rows"))
    if (row.at("Name") == "valid-current17")
      return row.at("InputJson").get<std::string>();
  throw std::runtime_error("Player17 fixture lacks valid-current17.");
}

[[nodiscard]] CampaignFrame loaded_frame(const fs::path &research_root,
                                         const fs::path &fixture,
                                         const fs::path &scratch) {
  const auto path = scratch / "fleet-player17.json";
  write(path, source_player17(fixture));
  auto loaded = load_existing_player_campaign_v17(
      path, [research_root] {
        return load_adaptive_research_strategic_runtime(research_root);
      });
  StrategicClock clock;
  clock.restore(loaded.campaign.simulation_days());
  clock.set_speed(StrategicSpeed::Normal);
  return CampaignFrame(std::move(loaded.campaign).activate(), std::move(clock),
                       CampaignFramePolicy::Player);
}

void observer_and_commands(CampaignFrame &frame) {
  NativeFleetController controller;
  constexpr std::uint64_t generation = 11;
  auto view = controller.build(frame, generation);
  require(view.own_fleets.size() == 2 && view.foreign_contacts.empty(),
          "Owned fleet projection or unobserved-enemy secrecy failed.");
  require(std::ranges::all_of(view.own_fleets, [](const auto &fleet) {
            return !fleet.name.empty() && fleet.combat_status.has_value() &&
                   fleet.combat_power >= 0.;
          }),
          "Owned fleet status was not sourced from Core.");

  auto &world = frame.runtime().world().campaign();
  const auto enemy = std::ranges::find_if(world.fleets, [&](const auto &fleet) {
    return fleet.is_active && fleet.civilization_id != view.player_civilization_id;
  });
  require(enemy != world.fleets.end(), "Authored fixture lacks a foreign fleet.");
  const auto foreign_revision = enemy->mission_order_revision;
  const auto foreign_destination = enemy->destination_system_id;
  const auto foreign_position = enemy->position;
  const auto foreign_select = controller.select(frame, generation, enemy->id);
  require(!foreign_select.accepted &&
              enemy->mission_order_revision == foreign_revision &&
              enemy->destination_system_id == foreign_destination &&
              enemy->position.x == foreign_position.x &&
              enemy->position.y == foreign_position.y,
          "Foreign selection was accepted or mutated the campaign.");

  const auto expected_enemy_power = own_fleet_combat_power(*enemy);
  observe_fleet_combat_power(
      {world.civilizations, world.fleets, world.combat_intelligence},
      view.player_civilization_id, *enemy, frame.clock().simulation_days(),
      false, true);
  view = controller.build(frame, generation);
  require(view.foreign_contacts.size() == 1 &&
              view.foreign_contacts.front().fleet_id == enemy->id &&
              view.foreign_contacts.front().observed_power ==
                  expected_enemy_power &&
              view.foreign_contacts.front().evidence == "Combat scanner",
          "Recorded enemy power was not projected exactly once.");

  const int reverse_hits[] = {view.own_fleets.back().id,
                              view.own_fleets.front().id, enemy->id};
  require(controller.select_next_hit(frame, generation, reverse_hits).accepted &&
              controller.selection() == view.own_fleets.front().id,
          "Map hit selection did not start at the lowest owned fleet ID.");
  require(controller.select_next_hit(frame, generation, reverse_hits).accepted &&
              controller.selection() == view.own_fleets.back().id,
          "Repeated map hit selection did not cycle in owned fleet ID order.");

  const auto scout = std::ranges::find_if(view.own_fleets, [](const auto &fleet) {
    return fleet.role == FleetRole::Scout;
  });
  require(scout != view.own_fleets.end(), "Authored fixture lacks a scout fleet.");
  require(controller.select(frame, generation, scout->id).accepted,
          "Owned scout could not be selected.");
  auto live_scout =
      std::ranges::find(world.fleets, scout->id, &FleetState::id);
  require(live_scout != world.fleets.end(), "Selected scout disappeared.");
  std::optional<NativeFleetRoutePreview> plain_travel;
  for (const auto &system : world.systems) {
    if (ExplorationMissionPlanner::needs_survey_work(
            world.knowledge, *live_scout, system.id))
      continue;
    auto candidate =
        controller.preview_selected_route(frame, generation, system.id);
    if (candidate.command_available) {
      plain_travel = std::move(candidate);
      break;
    }
  }
  require(plain_travel && plain_travel->route_supported &&
              plain_travel->route_authoritative,
          "No covered authoritative scout travel target was available.");
  const auto scout_revision = live_scout->mission_order_revision;
  const auto scout_destination = live_scout->destination_system_id;
  const auto scout_phase = live_scout->transit_phase;
  const auto scout_route = live_scout->planned_route_system_ids;
  const auto survey = ExplorationSimulation{}.issue_survey_order(
      {world.systems, world.bodies, world.fleets, world.colonies,
       world.knowledge, frame.runtime().world().lanes()},
      live_scout->id, plain_travel->target_system_id);
  require(!survey.accepted &&
              live_scout->mission_order_revision == scout_revision &&
              live_scout->destination_system_id == scout_destination &&
              live_scout->transit_phase == scout_phase &&
              live_scout->planned_route_system_ids == scout_route,
          "Survey-only rejection mutated the covered scout order.");
  const auto scout_order =
      controller.issue_selected_route(frame, *plain_travel);
  require(scout_order.accepted &&
              live_scout->mission_order_revision == scout_revision + 1,
          "Plain travel did not use the canonical exploration command.");

  const auto colony = std::ranges::find_if(view.own_fleets, [](const auto &fleet) {
    return fleet.role == FleetRole::Colony;
  });
  require(colony != view.own_fleets.end(), "Authored fixture lacks a colony fleet.");
  require(controller.select(frame, generation, colony->id).accepted,
          "Owned fleet could not be selected.");
  auto live = std::ranges::find(world.fleets, colony->id, &FleetState::id);
  require(live != world.fleets.end(), "Selected fleet disappeared.");
  require(live->current_system_id.has_value(),
          "Authored colony fleet is not stationed at a system.");

  const auto original_range = live->maximum_leg_range_light_years;
  const auto original_revision = live->mission_order_revision;
  const auto original_destination = live->destination_system_id;
  live->maximum_leg_range_light_years = .000001;
  const auto denied = controller.preview_selected_route(
      frame, generation,
      world.systems.front().id == *live->current_system_id
          ? world.systems.back().id
          : world.systems.front().id);
  require(!denied.route_supported && !denied.command_available,
          "Impossible lane range was presented as orderable.");
  const auto denied_order = controller.issue_selected_route(frame, denied);
  require(!denied_order.accepted &&
              live->mission_order_revision == original_revision &&
              live->destination_system_id == original_destination,
          "Denied route mutated fleet orders.");
  live->maximum_leg_range_light_years = original_range;

  std::optional<NativeFleetRoutePreview> accepted_preview;
  for (const auto &system : world.systems) {
    if (system.id == *live->current_system_id) continue;
    auto candidate =
        controller.preview_selected_route(frame, generation, system.id);
    if (candidate.command_available) {
      accepted_preview = std::move(candidate);
      break;
    }
  }
  require(accepted_preview && accepted_preview->route_supported &&
              accepted_preview->route_authoritative &&
              accepted_preview->route_system_ids.size() >= 2 &&
              accepted_preview->route_distance_light_years > 0. &&
              accepted_preview->estimated_transit_days.has_value(),
          "No authoritative linked route was available in authored campaign.");
  const auto stale_preview = *accepted_preview;
  const auto stale = controller.issue_selected_route(
      frame, NativeFleetRoutePreview{
                 .campaign_generation = generation + 1,
                 .fleet_id = accepted_preview->fleet_id,
                 .expected_mission_order_revision =
                     accepted_preview->expected_mission_order_revision,
                 .target_system_id = accepted_preview->target_system_id,
                 .route_supported = true,
                 .route_authoritative = true,
                 .command_available = true});
  require(!stale.accepted && live->mission_order_revision == original_revision,
          "Stale campaign route mutated the fleet.");

  const auto ordered = controller.issue_selected_route(frame, stale_preview);
  require(ordered.accepted && live->destination_system_id ==
                                  stale_preview.target_system_id &&
              live->mission_order_revision == original_revision + 1,
          "Canonical colony transit command did not retain its route.");
  const auto before_position = live->position;
  const auto before_phase = live->transit_phase;
  const auto before_progress = live->transit_progress;
  (void)frame.advance(.1);
  live = std::ranges::find(world.fleets, colony->id, &FleetState::id);
  require(live != world.fleets.end(),
          "Ordered fleet disappeared during campaign advancement.");
  require(live->position.x != before_position.x ||
              live->position.y != before_position.y ||
              live->transit_phase != before_phase ||
              live->transit_progress != before_progress,
          "Fleet transit did not progress through CampaignFrame advancement.");

  (void)controller.build(frame, generation + 1);
  const auto old_window = controller.issue_selected_route(frame, stale_preview);
  require(!old_window.accepted,
          "A preview from the replaced campaign generation was accepted.");
  bool rejected_generation_rollback{};
  try {
    (void)controller.build(frame, generation);
  } catch (const std::invalid_argument &) {
    rejected_generation_rollback = true;
  }
  require(rejected_generation_rollback,
          "A stale window rebound the controller to an older generation.");
}

void empty_fresh_campaign(const fs::path &research_root,
                          const fs::path &catalog_path) {
  auto world = seed_persistable_fresh_campaign(
      105500, load_nearby_catalog(catalog_path),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(
                          load_adaptive_research_strategic_runtime(research_root),
                          std::move(world)),
                      StrategicClock{}, CampaignFramePolicy::Player);
  NativeFleetController controller;
  require(controller.build(frame, 1).own_fleets.empty(),
          "Pre-warp fresh campaign invented an initial fleet.");
}
} // namespace

int main(int argc, char **argv) try {
  if (argc != 5)
    throw std::invalid_argument(
        "Usage: native_fleet_controller_tests <research-root> <catalog> <Player17-fixture> <scratch>");
  auto frame = loaded_frame(fs::absolute(argv[1]), fs::absolute(argv[3]),
                            fs::absolute(argv[4]));
  observer_and_commands(frame);
  empty_fresh_campaign(fs::absolute(argv[1]), fs::absolute(argv[2]));
  std::cout << "Native fleet ownership, intelligence secrecy, route denial, canonical order, frame transit and generation tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native fleet controller test failed: " << error.what() << '\n';
  return 1;
}
