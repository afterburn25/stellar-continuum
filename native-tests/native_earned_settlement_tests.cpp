#include "native_fleet_controller.hpp"
#include "native_settlement_mission_controller.hpp"
#include "native_settlement_preparation.hpp"
#include "native_shipyard_controller.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/player_campaign_save.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar::core;
using namespace stellar::native_colony;
using namespace stellar::native_fleet;
using namespace stellar::native_settlement_preparation;
using namespace stellar::native_shipyard;

namespace {
constexpr std::uint64_t generation = 1;
constexpr int maximum_systems = 24;
constexpr double maximum_days = 5000.;
constexpr auto maximum_wall_time = std::chrono::seconds(300);

struct ReplayBounds {
  double before_day{};
  double end_day{};
  std::chrono::steady_clock::time_point deadline;
};

void require(const bool value, const std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

void advance(CampaignFrame &frame) {
  require(frame.clock().speed() == StrategicSpeed::Normal,
          "earned progression changed Normal strategic speed");
  require(frame.clock().backlog_days() == 0.,
          "earned progression accumulated strategic-clock backlog");
  const auto before = frame.clock().simulation_days();
  const auto result = frame.advance(1. / 64.);
  require(result.route == CampaignFrameRoute::Strategic,
          "earned progression left the strategic CampaignFrame route");
  require(result.completed_substeps.size() == 1 &&
              result.completed_substeps.front() == 1. / 64. &&
              frame.clock().simulation_days() - before == 1. / 64. &&
              frame.clock().backlog_days() == 0.,
          "earned progression did not complete one exact Normal 1/64-day step");
}

CampaignFrame load_earned_frame(const fs::path &research, const fs::path &save) {
  auto loaded = load_existing_player_campaign_v17(save, [research] {
    return load_adaptive_research_strategic_runtime(research);
  });
  require(loaded.campaign.galaxy().seed == 115501,
          "earned source is not seed 115501");
  require(loaded.campaign.galaxy().player_civilization_id == 0,
          "earned source is not player 0");
  require(std::isfinite(loaded.campaign.simulation_days()) &&
              loaded.campaign.simulation_days() > 0.,
          "earned source has no valid elapsed campaign time");
  // Route safety and generation versions can change the completion date. Bind
  // the replay to the earned full-survey state, not an obsolete absolute day.
  const auto &world = loaded.campaign.galaxy();
  const auto science = std::ranges::find_if(world.fleets, [&](const auto &fleet) {
    return fleet.civilization_id == world.player_civilization_id && fleet.is_active &&
        fleet.role == FleetRole::Science && fleet.current_system_id &&
        !fleet.destination_system_id && fleet.transit_phase == FleetTransitPhase::None &&
        world.knowledge.system_survey_level(world.player_civilization_id,
            *fleet.current_system_id) == SystemSurveyLevel::fully_surveyed &&
        std::ranges::none_of(world.colonies, [&](const auto &colony) {
          return colony.civilization_id == world.player_civilization_id &&
              colony.system_id == *fleet.current_system_id;
        });
  });
  require(science != world.fleets.end() &&
      std::ranges::any_of(world.fleets, [&](const auto &fleet) {
        return fleet.civilization_id == world.player_civilization_id && fleet.is_active &&
            fleet.role == FleetRole::Scout && fleet.current_system_id == science->current_system_id &&
            !fleet.destination_system_id && fleet.transit_phase == FleetTransitPhase::None;
      }), "earned source lacks the scout/science completed non-colony survey checkpoint");
  StrategicClock clock;
  clock.restore(loaded.campaign.simulation_days());
  clock.set_speed(StrategicSpeed::Normal);
  return {std::move(loaded.campaign).activate(), std::move(clock),
          CampaignFramePolicy::Player};
}

const NativeOwnFleet *find_idle_fleet(const NativeFleetMapView &view,
                                      const FleetRole role) {
  const auto found = std::ranges::find_if(view.own_fleets, [=](const auto &fleet) {
    return fleet.role == role && fleet.current_system_id &&
           !fleet.destination_system_id && fleet.transit_phase == FleetTransitPhase::None;
  });
  return found == view.own_fleets.end() ? nullptr : &*found;
}

struct Target {
  int system_id{}, body_id{};
  NativeSettlementMissionKind kind{};
};

std::optional<NativeFleetRoutePreview> visible_route(
    CampaignFrame &frame, NativeFleetController &controller, const int fleet_id) {
  const auto &world = frame.runtime().world().campaign();
  const auto player_id = world.player_civilization_id;
  if (!controller.select(frame, generation, fleet_id).accepted) return std::nullopt;
  const auto known_systems = world.knowledge.known_systems(player_id);
  std::vector<NativeFleetRoutePreview> choices;
  for (const int system_id : world.knowledge.known_systems(player_id)) {
    if (world.knowledge.system_survey_level(player_id, system_id) >=
        SystemSurveyLevel::partially_surveyed)
      continue;
    const auto preview = controller.preview_selected_route(frame, generation, system_id);
    if (preview.command_available && preview.route_supported &&
        preview.route_authoritative && preview.route_distance_light_years > 0. &&
        !preview.route_system_ids.empty() && std::ranges::all_of(
            preview.route_system_ids, [&](const int route_system_id) {
              return std::ranges::find(known_systems, route_system_id) !=
                     known_systems.end();
            }))
      choices.push_back(preview);
  }
  if (choices.empty()) return std::nullopt;
  std::ranges::sort(choices, [](const auto &left, const auto &right) {
    return left.route_distance_light_years == right.route_distance_light_years
               ? left.target_system_id < right.target_system_id
               : left.route_distance_light_years < right.route_distance_light_years;
  });
  return choices.front();
}

bool wait_for_survey(CampaignFrame &frame, NativeFleetController &controller,
                     const int fleet_id, const int system_id,
                     const SystemSurveyLevel required, std::string &terminal,
                     const ReplayBounds &bounds) {
  const auto player_id = frame.runtime().world().campaign().player_civilization_id;
  while (frame.clock().simulation_days() < bounds.end_day &&
         std::chrono::steady_clock::now() < bounds.deadline) {
    if (frame.runtime().world().campaign().knowledge.system_survey_level(player_id, system_id) >= required)
      return true;
    advance(frame);
    (void)controller.build(frame, generation);
  }
  terminal = "survey " + std::string(std::chrono::steady_clock::now() >= bounds.deadline ? "wall-time" : "day") + " bound reached for fleet " + std::to_string(fleet_id) +
             " at system " + std::to_string(system_id);
  return false;
}

std::optional<Target> surveyed_target(CampaignFrame &frame, const int system_id) {
  const auto &world = frame.runtime().world().campaign();
  const auto player_id = world.player_civilization_id;
  if (world.knowledge.system_survey_level(player_id, system_id) !=
      SystemSurveyLevel::fully_surveyed)
    return std::nullopt;
  for (const auto &body : world.bodies) {
    if (body.system_id != system_id) continue;
    const auto preparation = build_settlement_preparation(frame, generation, system_id, body.id);
    if (!preparation) continue;
    const bool occupied = std::ranges::any_of(world.colonies, [=](const auto &colony) {
      return colony.system_id == system_id;
    });
    if (!occupied && preparation->solid_surface && !preparation->native_pre_warp_life &&
        preparation->site_can_found_current_colony)
      return Target{system_id, body.id, NativeSettlementMissionKind::Colony};
    if (!occupied && preparation->solid_surface && !preparation->native_pre_warp_life &&
        preparation->rare_resource &&
        preparation->suitability.viability == SpeciesColonizationViability::Unsuitable)
      return Target{system_id, body.id, NativeSettlementMissionKind::ResourceOutpost};
  }
  return std::nullopt;
}

std::optional<int> build_settlement_ship(CampaignFrame &frame,
                                         NativeShipyardController &shipyard,
                                         const NativeSettlementMissionKind kind,
                                         std::string &terminal,
                                         const ReplayBounds &bounds) {
  const auto design_id = kind == NativeSettlementMissionKind::Colony
                             ? "colony_ship" : "resource_outpost_ship";
  const auto before = frame.runtime().world().campaign().fleets.size();
  auto view = shipyard.build(frame, generation);
  const auto design = std::ranges::find(view.available_designs, std::string(design_id),
                                        &NativeShipDesign::id);
  if (design == view.available_designs.end() || !design->can_start) {
    terminal = "paid " + std::string(design_id) + " build unavailable: " +
               (design == view.available_designs.end() ? "design not projected" :
                design->start_blocker.value_or("not startable"));
    return std::nullopt;
  }
  const auto started = shipyard.start(frame, generation, view.shipyard_revision, design_id);
  if (!started.accepted) {
    terminal = "paid " + std::string(design_id) + " build rejected: " + started.message;
    return std::nullopt;
  }
  const auto after_start = shipyard.build(frame, generation);
  const auto order = std::ranges::find(after_start.orders, std::string(design_id),
                                       &NativeShipyardOrder::design_id);
  require(order != after_start.orders.end() &&
              order->authorization_credits == design->credit_cost &&
              order->reserved_population_millions == design->population_cost_millions &&
              after_start.treasury_credits == view.treasury_credits - design->credit_cost,
          "paid shipyard start did not charge and reserve canonical vessel terms");
  while (frame.clock().simulation_days() < bounds.end_day &&
         std::chrono::steady_clock::now() < bounds.deadline) {
    advance(frame);
    const auto &fleets = frame.runtime().world().campaign().fleets;
    if (fleets.size() > before) {
      const auto found = std::ranges::find_if(fleets, [&](const auto &fleet) {
        return fleet.design_id == design_id && fleet.civilization_id ==
               frame.runtime().world().campaign().player_civilization_id;
      });
      if (found != fleets.end()) {
        require(found->role == FleetRole::Colony &&
                    found->embarked_population_millions == design->population_cost_millions,
                "completed paid build did not create the quoted populated settlement vessel");
        return found->id;
      }
    }
  }
  terminal = "paid " + std::string(design_id) + " build exceeded " +
             (std::chrono::steady_clock::now() >= bounds.deadline ? "wall-time" : "5000-day") + " bound";
  return std::nullopt;
}

void roundtrip(CampaignFrame &frame, const fs::path &research) {
  const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(), "0.1.7-alpha",
                                               "2044-05-06T07:08:09Z"};
  const auto encoded = encode_player_campaign_v17_json(
      PreparedPlayerCampaignSave::capture(frame.runtime(), options).payload());
  auto restored = restore_player_campaign_v17_json(
      load_adaptive_research_strategic_runtime(research), encoded);
  auto resumed = std::move(restored).activate();
  const auto recaptured = encode_player_campaign_v17_json(
      PreparedPlayerCampaignSave::capture(resumed, options).payload());
  require(nlohmann::json::parse(encoded) == nlohmann::json::parse(recaptured),
          "earned settlement Player17 roundtrip changed campaign state");
}

void checkpoint(CampaignFrame &frame, const fs::path &directory,
                const std::string_view name) {
  if (directory.empty()) return;
  const auto path = directory / (std::string(name) + ".player17.json");
  require(!fs::exists(path), "earned checkpoint would overwrite an existing file");
  const PlayerCampaignCaptureOptions options{frame.clock().simulation_days(), "0.1.7-alpha",
                                               "2044-05-06T07:08:09Z"};
  write_prepared_player_campaign(path,
      PreparedPlayerCampaignSave::capture(frame.runtime(), options), false);
}

void emit(const std::string_view terminal, CampaignFrame &frame, const ReplayBounds &bounds,
          const std::vector<int> &visited, const std::vector<std::string> &steps,
          const std::optional<Target> &target = std::nullopt,
          const std::optional<int> fleet_id = std::nullopt,
          const std::optional<int> colony_id = std::nullopt) {
  nlohmann::json evidence{{"kind", "earned_settlement"}, {"terminal", terminal},
                          {"before_day", bounds.before_day},
                          {"after_day", frame.clock().simulation_days()},
                          {"step_days", 1.0 / 64.0}, {"visited_systems", visited},
                          {"ordered_steps", steps}};
  const auto &world = frame.runtime().world().campaign();
  const auto economy = std::ranges::find(world.economies, world.player_civilization_id,
                                         &CivilizationEconomy::civilization_id);
  evidence["outcomes"] = {{"fleets", world.fleets.size()}, {"colonies", world.colonies.size()},
                          {"construction_states", world.construction.size()},
                          {"treasury", economy == world.economies.end() ? 0. : economy->credits}};
  if (target) evidence["target"] = {{"system_id", target->system_id},
                                      {"body_id", target->body_id},
                                      {"kind", target->kind == NativeSettlementMissionKind::Colony ? "colony" : "outpost"}};
  if (fleet_id) evidence["fleet_id"] = *fleet_id;
  if (colony_id) evidence["colony_id"] = *colony_id;
  std::cout << evidence.dump() << '\n';
}

void authored_default_check(const fs::path &research, const fs::path &catalog) {
  auto world = seed_persistable_fresh_campaign(
      115501, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(
                          load_adaptive_research_strategic_runtime(research), std::move(world)),
                      StrategicClock{}, CampaignFramePolicy::Player);
  NativeFleetController fleets;
  const auto view = fleets.build(frame, generation);
  require(view.own_fleets.empty(), "authored fresh boundary unexpectedly has earned fleets");
  require(!fleets.select(frame, generation, 1).accepted,
          "authored controller boundary selected a nonexistent fleet");
  auto &campaign = frame.runtime().world().campaign();
  const auto player = campaign.player_civilization_id;
  const auto body = std::ranges::find_if(campaign.bodies, [&](const auto &value) {
    return campaign.knowledge.system_survey_level(player, value.system_id) ==
           SystemSurveyLevel::fully_surveyed;
  });
  require(body != campaign.bodies.end(), "authored observer check lacks a fully surveyed home body");
  campaign.player_civilization_id = -1;
  require(!build_settlement_preparation(frame, generation, body->system_id, body->id),
          "authored observer check exposed settlement facts for an invalid player");
  std::cout << "authored helper validation passed; earned replay skipped (use --earned <Player17>)\n";
}

bool earned_progression(const fs::path &research, const fs::path &save,
                        const fs::path &output_directory) {
  auto frame = load_earned_frame(research, save);
  NativeFleetController fleets;
  NativeShipyardController shipyard;
  NativeSettlementMissionController settlements;
  std::string terminal;
  std::optional<Target> target;
  std::vector<int> visited;
  std::vector<std::string> steps;
  const ReplayBounds bounds{frame.clock().simulation_days(),
      frame.clock().simulation_days() + maximum_days,
      std::chrono::steady_clock::now() + maximum_wall_time};

  for (int explored{}; explored < maximum_systems && frame.clock().simulation_days() < bounds.end_day &&
       std::chrono::steady_clock::now() < bounds.deadline; ++explored) {
    const auto scout_view = fleets.build(frame, generation);
    const auto scout = find_idle_fleet(scout_view, FleetRole::Scout);
    if (!scout) { terminal = "no idle earned scout is available"; break; }
    const auto route = visible_route(frame, fleets, scout->id);
    if (!route) { terminal = "no observer-visible authoritative exploration route remains"; break; }
    const auto scout_order = fleets.issue_selected_route(frame, *route);
    if (!scout_order.accepted) { terminal = "scout route rejected: " + scout_order.message; break; }
    if (!wait_for_survey(frame, fleets, scout->id, route->target_system_id,
                         SystemSurveyLevel::partially_surveyed, terminal, bounds)) break;
    visited.push_back(route->target_system_id);
    steps.push_back("scout reconnaissance " + std::to_string(route->target_system_id));
    std::cout << nlohmann::json{{"kind", "earned_settlement_visit"},
                                {"system_id", route->target_system_id},
                                {"day", frame.clock().simulation_days()},
                                {"stage", "reconnaissance"}}.dump() << std::endl;

    const auto science_view = fleets.build(frame, generation);
    const auto science = find_idle_fleet(science_view, FleetRole::Science);
    if (!science || !fleets.select(frame, generation, science ? science->id : -1).accepted) {
      terminal = "no idle earned science vessel is available"; break;
    }
    const auto science_route = fleets.preview_selected_route(frame, generation, route->target_system_id);
    const auto known = frame.runtime().world().campaign().knowledge.known_systems(
        frame.runtime().world().campaign().player_civilization_id);
    if (!science_route.command_available || !science_route.route_supported ||
        !science_route.route_authoritative ||
        !std::ranges::all_of(science_route.route_system_ids, [&](const int id) {
          return std::ranges::find(known, id) != known.end();
        })) {
      terminal = "science vessel cannot reach the scout-surveyed system: " + science_route.message;
      break;
    }
    const auto science_order = fleets.issue_selected_route(frame, science_route);
    if (!science_order.accepted) { terminal = "science route rejected: " + science_order.message; break; }
    if (!wait_for_survey(frame, fleets, science->id, route->target_system_id,
                         SystemSurveyLevel::fully_surveyed, terminal, bounds)) break;
    steps.push_back("science full survey " + std::to_string(route->target_system_id));
    if ((target = surveyed_target(frame, route->target_system_id))) break;
  }

  if (!target) {
    if (terminal.empty()) terminal = std::chrono::steady_clock::now() >= bounds.deadline
        ? "300-second wall-time bound reached" : "24-system or 5000-day exploration bound reached";
    emit("blocked: " + terminal, frame, bounds, visited, steps);
    return false;
  }
  checkpoint(frame, output_directory, "eligible-site");
  const auto colony_fleet = build_settlement_ship(frame, shipyard, target->kind, terminal, bounds);
  if (!colony_fleet) {
    emit("blocked: " + terminal, frame, bounds, visited, steps, target);
    return false;
  }
  checkpoint(frame, output_directory, "populated-vessel");
  steps.push_back("paid build " + std::to_string(*colony_fleet));
  const auto quote = settlements.preview_exact(frame, generation, *colony_fleet,
                                                target->system_id, target->body_id);
  if (!quote.accepted) {
    emit("blocked: exact quote rejected: " + quote.message, frame, bounds, visited, steps, target, colony_fleet);
    return false;
  }
  const auto known = frame.runtime().world().campaign().knowledge.known_systems(
      frame.runtime().world().campaign().player_civilization_id);
  require(quote.candidate && quote.candidate->reach.route_system_ids &&
      std::ranges::all_of(*quote.candidate->reach.route_system_ids,
      [&](const int id) { return std::ranges::find(known, id) != known.end(); }),
      "earned settlement route contains a system outside observer knowledge");
  require(quote.kind == target->kind && quote.personnel_millions > 0.,
          "exact settlement quote changed the candidate kind or lost embarked population");
  steps.push_back("exact quote authorization=" + std::to_string(quote.authorization_budget_units) +
                  " population=" + std::to_string(quote.personnel_millions));
  require(quote.requires_new_authorization && quote.funded,
          "newly built settlement vessel lacks a funded new-expedition quote");
  const auto treasury_before_expedition = quote.treasury_budget_units;
  const auto issued = settlements.issue_exact(frame, generation, quote.revision);
  if (!issued.accepted) {
    emit("blocked: exact order rejected: " + issued.message, frame, bounds, visited, steps, target, colony_fleet);
    return false;
  }
  const auto economy = std::ranges::find(frame.runtime().world().campaign().economies,
                                         frame.runtime().world().campaign().player_civilization_id,
                                         &CivilizationEconomy::civilization_id);
  require(economy != frame.runtime().world().campaign().economies.end() &&
              economy->credits == treasury_before_expedition - quote.authorization_budget_units,
          "accepted expedition did not apply the exact quoted treasury charge");
  const auto player_id = frame.runtime().world().campaign().player_civilization_id;
  bool saved_partial{};
  steps.push_back("exact settlement order " + std::to_string(*colony_fleet));
  const auto order_day = frame.clock().simulation_days();
  while (frame.clock().simulation_days() < bounds.end_day &&
         std::chrono::steady_clock::now() < bounds.deadline) {
    advance(frame);
    if (!saved_partial) {
      const auto status = settlements.live_status(frame, generation, *colony_fleet);
      if (status && status->settlement_days_completed > 0.) {
        checkpoint(frame, output_directory, "partial-establishment");
        saved_partial = true;
      }
    }
    const auto &world = frame.runtime().world().campaign();
    const auto colony = std::ranges::find_if(world.colonies, [&](const auto &value) {
      return value.civilization_id == player_id && value.system_id == target->system_id &&
             value.planetary_body_id == target->body_id;
    });
    if (colony != world.colonies.end()) {
      const auto consumed = std::ranges::find(world.fleets, *colony_fleet, &FleetState::id);
      require(consumed == world.fleets.end() || !consumed->is_active,
              "establishment created a colony without consuming its settlement fleet");
      require(colony->population_millions > 0. && frame.clock().simulation_days() > order_day && saved_partial,
              "settlement founding skipped populated timed establishment");
      roundtrip(frame, research);
      checkpoint(frame, output_directory, "founded");
      emit("established", frame, bounds, visited, steps, target, colony_fleet, colony->id);
      return true;
    }
  }
  emit(std::chrono::steady_clock::now() >= bounds.deadline
           ? "blocked: establishment wall-time bound exceeded"
           : "blocked: establishment exceeded 5000-day bound",
       frame, bounds, visited, steps, target, colony_fleet);
  return false;
}
}  // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3 || argc == 5 || argc == 7,
            "Usage: native_earned_settlement_tests <research-root> <catalog> [--earned <Player17> [--output-dir <existing-dir>]]");
    const auto research = fs::absolute(argv[1]);
    const auto catalog = fs::absolute(argv[2]);
    if (argc == 3) {
      authored_default_check(research, catalog);
      return 0;
    }
    require(std::string_view(argv[3]) == "--earned", "unknown earned-settlement option");
    const fs::path source(argv[4]);
    require(source.is_absolute() && fs::is_regular_file(source),
            "--earned requires an existing absolute Player17 source");
    fs::path output_directory;
    if (argc == 7) {
      require(std::string_view(argv[5]) == "--output-dir", "unknown earned-settlement option");
      output_directory = fs::path(argv[6]);
      require(output_directory.is_absolute() && fs::is_directory(output_directory),
              "--output-dir requires an existing absolute directory");
      for (const auto name : {"eligible-site", "populated-vessel", "partial-establishment", "founded"})
        require(!fs::exists(output_directory / (std::string(name) + ".player17.json")),
                "--output-dir refuses to overwrite earned checkpoint files");
    }
    return earned_progression(research, source, output_directory) ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "native earned settlement failed: " << error.what() << '\n';
    return 1;
  }
}
