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

void reconnaissance_projection(CampaignFrame &frame) {
  NativeFleetController controller;
  constexpr std::uint64_t generation = 73;
  auto initial = controller.build(frame, generation);
  auto &world = frame.runtime().world().campaign();
  const auto scout_row = std::ranges::find_if(initial.own_fleets, [](const auto &fleet) {
    return fleet.role == FleetRole::Scout;
  });
  require(scout_row != initial.own_fleets.end(), "Authored fixture lacks a scout for reconnaissance projection.");
  auto scout = std::ranges::find(world.fleets, scout_row->id, &FleetState::id);
  require(scout != world.fleets.end(), "Reconnaissance scout disappeared.");
  const auto system = std::ranges::find_if(world.systems, [&](const auto &candidate) {
    return world.knowledge.system_survey_level(world.player_civilization_id, candidate.id) <
           SystemSurveyLevel::partially_surveyed;
  });
  require(system != world.systems.end(), "Fixture lacks a reconnaissance-grade system.");
  const auto original = *scout;
  scout->current_system_id = system->id;
  scout->destination_system_id.reset();
  scout->transit_phase = FleetTransitPhase::None;
  scout->hold_requested = false;
  scout->reconnaissance_system_id = system->id;
  scout->reconnaissance_days_completed = 1.;
  const auto work = controller.build(frame, generation);
  const auto work_row = std::ranges::find(work.own_fleets, scout->id, &NativeOwnFleet::id);
  require(work_row != work.own_fleets.end() && work_row->reconnaissance &&
              !work_row->reconnaissance->completed &&
              work_row->reconnaissance->days_completed == 1. &&
              work_row->reconnaissance->required_days == ExplorationSimulation::scout_reconnaissance_days,
          "Stationed scout did not project canonical local reconnaissance work.");
  require(scout->reconnaissance_system_id == system->id &&
              scout->reconnaissance_days_completed == 1.,
          "Read-only reconnaissance projection mutated authoritative fleet work.");
  scout->hold_requested = true;
  const auto held = controller.build(frame, generation);
  const auto held_row = std::ranges::find(held.own_fleets, scout->id, &NativeOwnFleet::id);
  require(held_row != held.own_fleets.end() && held_row->reconnaissance &&
              held_row->reconnaissance->held,
          "Held reconnaissance did not retain the fleet's canonical hold state.");
  scout->transit_phase = FleetTransitPhase::LocalDeparture;
  const auto departing = controller.build(frame, generation);
  require(!std::ranges::find(departing.own_fleets, scout->id,
                             &NativeOwnFleet::id)->reconnaissance,
          "Moving scout exposed local reconnaissance work.");
  scout->transit_phase = FleetTransitPhase::None;
  scout->destination_system_id = system->id;
  const auto travelling = controller.build(frame, generation);
  require(!std::ranges::find(travelling.own_fleets, scout->id,
                             &NativeOwnFleet::id)->reconnaissance,
          "Travelling scout exposed local reconnaissance work.");
  scout->destination_system_id.reset();
  scout->civilization_id = world.player_civilization_id + 1;
  const auto foreign = controller.build(frame, generation);
  require(std::ranges::find(foreign.own_fleets, scout->id, &NativeOwnFleet::id) ==
              foreign.own_fleets.end(),
          "Foreign scout reconnaissance was exposed to the player.");
  scout->civilization_id = world.player_civilization_id;
  world.knowledge.record_reconnaissance(world.player_civilization_id, system->id);
  scout->reconnaissance_system_id.reset();
  const auto known_elsewhere = controller.build(frame, generation);
  require(!std::ranges::find(known_elsewhere.own_fleets, scout->id,
                             &NativeOwnFleet::id)->reconnaissance,
          "Known system without this scout's recorder exposed reconnaissance completion.");
  scout->reconnaissance_system_id = system->id;
  scout->reconnaissance_days_completed = ExplorationSimulation::scout_reconnaissance_days;
  const auto complete = controller.build(frame, generation);
  const auto complete_row = std::ranges::find(complete.own_fleets, scout->id, &NativeOwnFleet::id);
  require(complete_row != complete.own_fleets.end() && complete_row->reconnaissance &&
              complete_row->reconnaissance->completed && !complete_row->reconnaissance->fully_surveyed,
          "Matching scout recorder did not project completed reconnaissance.");
  world.knowledge.mark_system_fully_surveyed(world.player_civilization_id, system->id);
  const auto surveyed = controller.build(frame, generation);
  const auto surveyed_row = std::ranges::find(surveyed.own_fleets, scout->id, &NativeOwnFleet::id);
  require(surveyed_row != surveyed.own_fleets.end() && surveyed_row->reconnaissance &&
              surveyed_row->reconnaissance->fully_surveyed,
          "Completed reconnaissance did not preserve full-survey knowledge.");
  *scout = original;
}

void science_survey_projection(CampaignFrame &frame) {
  NativeFleetController controller;
  constexpr std::uint64_t generation = 74;
  const auto initial = controller.build(frame, generation);
  auto &world = frame.runtime().world().campaign();
  require(!initial.own_fleets.empty(), "Authored fixture lacks an owned fleet.");
  auto science = std::ranges::find(world.fleets, initial.own_fleets.front().id,
                                 &FleetState::id);
  require(science != world.fleets.end(), "Science vessel disappeared.");
  const auto system = std::ranges::find_if(world.systems, [&](const auto &candidate) {
    return world.knowledge.system_survey_level(world.player_civilization_id, candidate.id) <
           SystemSurveyLevel::fully_surveyed;
  });
  require(system != world.systems.end(), "Fixture lacks a surveyable system.");
  const auto original = *science;
  // This projection fixture predates science vessels; the earned runtime replay
  // separately validates a science ship built through ordinary player actions.
  science->role = FleetRole::Science;
  world.knowledge.record_reconnaissance(world.player_civilization_id, system->id);
  world.knowledge.advance_system_survey(world.player_civilization_id, system->id, .2);
  science->current_system_id = system->id;
  science->destination_system_id.reset();
  science->transit_phase = FleetTransitPhase::None;
  science->hold_requested = false;
  const auto work = controller.build(frame, generation);
  const auto work_row = std::ranges::find(work.own_fleets, science->id, &NativeOwnFleet::id);
  require(work_row != work.own_fleets.end() && work_row->science_survey &&
              !work_row->science_survey->completed &&
              work_row->science_survey->progress > 0. &&
              work_row->science_survey->progress < 1.,
          "Stationed science vessel did not project authoritative survey progress.");
  require(science->current_system_id == system->id && !science->destination_system_id &&
              science->transit_phase == FleetTransitPhase::None,
          "Science survey projection mutated authoritative fleet state.");
  science->hold_requested = true;
  const auto held = controller.build(frame, generation);
  const auto held_row = std::ranges::find(held.own_fleets, science->id, &NativeOwnFleet::id);
  require(held_row != held.own_fleets.end() && held_row->science_survey &&
              held_row->science_survey->held,
          "Held science survey did not retain canonical hold state.");
  science->transit_phase = FleetTransitPhase::LocalDeparture;
  const auto moving = controller.build(frame, generation);
  const auto moving_row = std::ranges::find(moving.own_fleets, science->id, &NativeOwnFleet::id);
  require(moving_row != moving.own_fleets.end() && !moving_row->science_survey,
          "Moving science vessel exposed local survey work.");
  science->transit_phase = FleetTransitPhase::None;
  science->destination_system_id = system->id;
  const auto travelling = controller.build(frame, generation);
  const auto travelling_row = std::ranges::find(travelling.own_fleets, science->id, &NativeOwnFleet::id);
  require(travelling_row != travelling.own_fleets.end() && !travelling_row->science_survey,
          "Travelling science vessel exposed local survey work.");
  science->destination_system_id.reset();
  science->civilization_id = world.player_civilization_id + 1;
  const auto foreign = controller.build(frame, generation);
  require(std::ranges::find(foreign.own_fleets, science->id, &NativeOwnFleet::id) ==
              foreign.own_fleets.end(),
          "Foreign science survey was exposed to the player.");
  science->civilization_id = world.player_civilization_id;
  world.knowledge.mark_system_fully_surveyed(world.player_civilization_id, system->id);
  const auto complete = controller.build(frame, generation);
  const auto complete_row = std::ranges::find(complete.own_fleets, science->id, &NativeOwnFleet::id);
  require(complete_row != complete.own_fleets.end() && complete_row->science_survey &&
              complete_row->science_survey->completed &&
              complete_row->science_survey->progress == 1.,
          "Fully surveyed science mission was not projected as complete.");
  *science = original;
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

  // An armed selected fleet alone receives a quoted strategic order; the
  // quote cannot be replayed after Core has consumed it.
  auto armed = std::ranges::find(world.fleets, scout->id, &FleetState::id);
  require(armed != world.fleets.end(), "Selected fleet disappeared before military quote test.");
  armed->role = FleetRole::Military;
  armed->combat = create_initial_fleet_combat_state(std::nullopt, FleetRole::Military);
  armed->current_system_id = world.systems.front().id;
  armed->destination_system_id.reset();
  armed->transit_phase = FleetTransitPhase::None;
  require(controller.select(frame, generation + 1, armed->id).accepted,
          "Armed fleet could not be selected.");
  const auto armed_view = controller.build(frame, generation + 1);
  const auto armed_row = std::ranges::find(armed_view.own_fleets, armed->id, &NativeOwnFleet::id);
  require(armed_row != armed_view.own_fleets.end() && armed_row->military_order_quote,
          "Selected armed fleet did not receive a military quote.");
  auto order_quote = *armed_row->military_order_quote;
  const auto fresh_military_quote = [&] {
    const auto projected = controller.build(frame, generation + 1);
    const auto row = std::ranges::find(projected.own_fleets, armed->id, &NativeOwnFleet::id);
    require(row != projected.own_fleets.end() && row->military_order_quote, "Military quote refresh failed.");
    return *row->military_order_quote;
  };
  armed->transit_progress += .001;
  const auto advancing_progress = armed->transit_progress;
  const auto advancing_route = armed->planned_route_system_ids;
  require(fresh_military_quote() == order_quote &&
              controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted &&
              armed->transit_progress == advancing_progress && armed->planned_route_system_ids == advancing_route,
          "Ordinary travel progress cancelled a valid click or Hold changed the route.");
  order_quote = fresh_military_quote();
  armed->combat->target_fleet_id = 999;
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "An intervening attack target change did not invalidate the military quote.");
  armed->combat->target_fleet_id.reset();
  const auto target_refreshed = controller.build(frame, generation + 1);
  order_quote = *std::ranges::find(target_refreshed.own_fleets, armed->id,
                                    &NativeOwnFleet::id)->military_order_quote;
  armed->combat->order = MilitaryOrderType::Retreat;
  armed->combat->retreat_started = true;
  armed->combat->retreat_progress_days = .5;
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "An intervening retreat change did not invalidate the military quote.");
  armed->combat = create_initial_fleet_combat_state(std::nullopt, FleetRole::Military);
  const auto retreat_refreshed = controller.build(frame, generation + 1);
  order_quote = *std::ranges::find(retreat_refreshed.own_fleets, armed->id,
                                    &NativeOwnFleet::id)->military_order_quote;
  const auto defended_system = *armed->current_system_id;
  const auto defend = controller.issue_selected_military_order(frame, order_quote,
                                                                MilitaryOrderType::Defend);
  require(defend.accepted && armed->combat->order == MilitaryOrderType::Defend &&
              armed->combat->defend_system_id == defended_system,
          "Defend did not bind the live current system through Core.");
  const auto command_view = controller.build(frame, generation + 1);
  order_quote = *std::ranges::find(command_view.own_fleets, armed->id,
                                    &NativeOwnFleet::id)->military_order_quote;
  for (const auto altered : {0, 1}) {
    auto tampered_order = order_quote;
    if (altered == 0) ++tampered_order.campaign_generation;
    else ++tampered_order.observer_id;
    require(!controller.issue_selected_military_order(frame, tampered_order, MilitaryOrderType::Hold).accepted,
            "Generation or observer tampering reached the military command.");
  }
  const auto other = std::ranges::find_if(world.fleets, [&](const auto &fleet) {
    return fleet.id != armed->id && fleet.is_active && fleet.civilization_id == view.player_civilization_id;
  });
  require(other != world.fleets.end() && controller.select(frame, generation + 1, other->id).accepted &&
              !controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "A changed selection retained military authorization.");
  require(controller.select(frame, generation + 1, armed->id).accepted,
          "Cannot restore armed selection after stale-command test.");
  order_quote = fresh_military_quote();
  armed->combat = create_initial_fleet_combat_state(std::nullopt, FleetRole::Scout);
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "An unarmed live fleet retained military authorization.");
  armed->combat = create_initial_fleet_combat_state(std::nullopt, FleetRole::Military);
  order_quote = fresh_military_quote();
  armed->is_active = false;
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "A removed fleet retained military authorization.");
  armed->is_active = true;
  order_quote = fresh_military_quote();
  const auto original_owner = armed->civilization_id;
  armed->civilization_id = original_owner + 1;
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "A foreign fleet retained military authorization.");
  armed->civilization_id = original_owner;
  order_quote = fresh_military_quote();
  world.active_combat_encounter = CampaignMassiveEncounter{.system_id = defended_system};
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "Strategic military order was accepted during tactical combat.");
  world.active_combat_encounter.reset();
  // The duplicate test is deliberately last: vector growth may invalidate
  // `armed`, and a duplicate ID must never be dispatched through Core.
  const auto duplicate = *armed;
  world.fleets.push_back(duplicate);
  require(!controller.issue_selected_military_order(frame, order_quote, MilitaryOrderType::Hold).accepted,
          "Duplicate fleet identity retained military authorization.");
}

void civilian_recovery(CampaignFrame &frame) {
  NativeFleetController controller;
  constexpr std::uint64_t generation = 30;
  auto view = controller.build(frame, generation);
  require(std::ranges::none_of(view.own_fleets, [](const auto &f) { return f.recovery.has_value(); }),
          "Unselected outliner rows unnecessarily planned recovery routes.");
  auto &world = frame.runtime().world().campaign();
  auto &colony = *std::ranges::find_if(world.fleets, [&](const auto &f) {
    return f.civilization_id == view.player_civilization_id && f.role == FleetRole::Colony;
  });
  const int id = colony.id;
  const auto original = colony;
  require(controller.select(frame, generation, id).accepted, "Cannot select recovery colony ship.");
  const auto quote = [&] {
    auto projected = controller.build(frame, generation);
    const auto selected = std::ranges::find(projected.own_fleets, id, &NativeOwnFleet::id);
    require(selected != projected.own_fleets.end() && selected->recovery &&
            !selected->recovery_message.empty(), "Civilian recovery projection missing.");
    return *selected->recovery;
  };
  auto first = quote();
  {
    const auto projected = controller.build(frame, generation);
    const auto selected = std::ranges::find(projected.own_fleets, id, &NativeOwnFleet::id);
    require(selected != projected.own_fleets.end() && selected->locate,
            "Selected owned civilian fleet has no Locate quote.");
    const auto before_position = colony.position;
    const auto located = controller.locate_selected(frame, *selected->locate);
    require(located.accepted && located.fleet_id == id &&
                located.position.x == before_position.x && located.position.y == before_position.y &&
                colony.position.x == before_position.x && colony.position.y == before_position.y,
            "Locate did not resolve the authoritative owned position read-only.");
    auto stale_locate = *selected->locate;
    ++stale_locate.mission_order_revision;
    require(!controller.locate_selected(frame, stale_locate).accepted,
            "Tampered Locate quote was accepted.");
  }
  require(controller.issue_civilian_recovery(frame, first, NativeCivilianRecoveryAction::Hold).accepted &&
          colony.hold_requested && colony.destination_system_id == original.destination_system_id,
          "Hold failed or replaced existing mission.");
  require(!controller.issue_civilian_recovery(frame, first, NativeCivilianRecoveryAction::Resume).accepted &&
          colony.hold_requested, "Old hold state could issue a new recovery action.");
  require(controller.issue_civilian_recovery(frame, quote(), NativeCivilianRecoveryAction::Resume).accepted &&
          !colony.hold_requested, "Resume failed.");
  colony.destination_planetary_body_id = 4;
  colony.settlement_body_id = 4;
  colony.settlement_days_completed = 3.5;
  const auto paid = colony;
  const auto paid_quote = quote();
  const auto warning = controller.issue_civilian_recovery(frame, paid_quote,
      NativeCivilianRecoveryAction::ReturnToBase);
  require(!warning.accepted && warning.requires_confirmation &&
          warning.message.contains("no refund") && warning.message.contains("Colonists remain aboard") &&
          colony.destination_planetary_body_id == paid.destination_planetary_body_id &&
          colony.settlement_days_completed == 3.5 && !colony.return_to_base_requested,
          "First return click abandoned paid work or omitted consequences.");
  for (int changed = 0; changed != 9; ++changed) {
    colony = paid;
    auto stale = paid_quote;
    if (changed == 0) ++stale.campaign_generation;
    if (changed == 1) ++stale.observer_id;
    if (changed == 2) ++colony.mission_order_revision;
    if (changed == 3) colony.destination_planetary_body_id = 9;
    if (changed == 4) colony.settlement_days_completed += 1;
    if (changed == 5) colony.hold_requested = true;
    if (changed == 6) colony.is_active = false;
    if (changed == 7) colony.civilization_id += 1;
    if (changed == 8) colony.role = FleetRole::Military;
    const auto before = colony;
    const auto denied = controller.issue_civilian_recovery(frame, stale,
        NativeCivilianRecoveryAction::ReturnToBase, true);
    require(!denied.accepted && !denied.requires_confirmation &&
            colony.destination_planetary_body_id == before.destination_planetary_body_id &&
            colony.settlement_days_completed == before.settlement_days_completed &&
            colony.hold_requested == before.hold_requested &&
            colony.return_to_base_requested == before.return_to_base_requested,
            "Stale/foreign/inactive recovery confirmation mutated a mission.");
  }
  colony = paid;
  const int scout_id = std::ranges::find_if(view.own_fleets, [](const auto &f) {
    return f.role == FleetRole::Scout;
  })->id;
  require(controller.select(frame, generation, scout_id).accepted, "Cannot switch recovery selection.");
  require(!controller.issue_civilian_recovery(frame, paid_quote,
      NativeCivilianRecoveryAction::ReturnToBase, true).accepted &&
      colony.settlement_days_completed == 3.5, "Changed selection retained destructive authorization.");
  require(controller.select(frame, generation, id).accepted, "Cannot restore recovery selection.");
  require(controller.issue_civilian_recovery(frame, quote(), NativeCivilianRecoveryAction::ReturnToBase, true).accepted &&
          !colony.destination_planetary_body_id && !colony.settlement_body_id &&
          colony.settlement_days_completed == 0 && colony.prevent_automatic_settlement &&
          colony.embarked_population_millions == paid.embarked_population_millions,
          "Confirmed return did not use Core abandonment rules or lost colonists.");
  // Between systems the command must preserve the current leg and queue recovery.
  colony = original;
  colony.current_system_id.reset();
  colony.destination_system_id = 1;
  colony.planned_route_system_ids = {1};
  colony.transit_phase = FleetTransitPhase::InterstellarWarp;
  colony.transit_progress = .4;
  require(controller.issue_civilian_recovery(frame, quote(), NativeCivilianRecoveryAction::ReturnToBase).accepted &&
          colony.return_to_base_requested && !colony.current_system_id &&
          colony.transit_progress == .4 && colony.planned_route_system_ids == std::vector<int>{1},
          "Return teleported a travelling ship or skipped its current lane.");
  colony = original;
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
  auto reconnaissance_frame = loaded_frame(fs::absolute(argv[1]), fs::absolute(argv[3]),
                                           fs::absolute(argv[4]));
  reconnaissance_projection(reconnaissance_frame);
  auto science_frame = loaded_frame(fs::absolute(argv[1]), fs::absolute(argv[3]),
                                    fs::absolute(argv[4]));
  science_survey_projection(science_frame);
  observer_and_commands(frame);
  auto recovery_frame = loaded_frame(fs::absolute(argv[1]), fs::absolute(argv[3]),
                                    fs::absolute(argv[4]));
  civilian_recovery(recovery_frame);
  empty_fresh_campaign(fs::absolute(argv[1]), fs::absolute(argv[2]));
  std::cout << "Native fleet ownership, intelligence secrecy, route denial, canonical order, frame transit and generation tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native fleet controller test failed: " << error.what() << '\n';
  return 1;
}
