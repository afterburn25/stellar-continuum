// Native missions panel tests — the observer-safe MISSIONS & SETTLEMENT board
// ported from the reference ExplorationMissionPanel + ExplorationMissionStatus:
// owned scout/science/colony fleet cards with phase, destination, ETA and
// mission summary text.
#include "native_missions.hpp"

#include <iostream>
#include <string>

using namespace stellar::native_missions;
using namespace stellar::core;
namespace native_map = stellar::native_map;

namespace {

int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

FreshCampaignState campaign_fixture() {
  FreshCampaignState campaign;
  campaign.player_civilization_id = 1;

  StellarSystem home;
  home.id = 1;
  home.name = "Sol";
  home.position = {0.f, 0.f, 0.f};
  StellarSystem target;
  target.id = 2;
  target.name = "Proxima";
  target.position = {4.24f, 0.f, 0.f};
  campaign.systems = {home, target};

  Civilization player;
  player.id = 1;
  player.name = "Terrans";
  player.home_system_id = 1;
  player.is_player = true;
  campaign.civilizations = {player};

  campaign.knowledge.reveal_system(1, 1);
  campaign.knowledge.reveal_system(1, 2);
  return campaign;
}

FleetState scout_fleet() {
  FleetState fleet;
  fleet.id = 10;
  fleet.civilization_id = 1;
  fleet.name = "SCV Pathfinder";
  fleet.role = FleetRole::Scout;
  fleet.is_active = true;
  return fleet;
}

}  // namespace

int main() {
  {
    // No mission fleets → empty board.
    const auto campaign = campaign_fixture();
    const auto board = build_mission_board(campaign);
    check(board.missions.empty(), "an empty fleet list must build an empty board");
  }
  {
    // Foreign and military fleets are excluded; mission fleets are owned-only.
    auto campaign = campaign_fixture();
    FleetState foreign = scout_fleet();
    foreign.civilization_id = 2;
    FleetState military = scout_fleet();
    military.id = 11;
    military.role = FleetRole::Military;
    campaign.fleets = {foreign, military};
    const auto board = build_mission_board(campaign);
    check(board.missions.empty(),
          "foreign and military fleets must not appear on the board");
  }
  {
    // Traveling scout: traveling phase, destination name, transit ETA.
    auto campaign = campaign_fixture();
    auto fleet = scout_fleet();
    fleet.current_system_id = 1;
    fleet.destination_system_id = 2;
    fleet.position = {0.f, 0.f};
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    check(board.missions.size() == 1, "one traveling scout must produce one card");
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::traveling,
          "a fleet with a destination must be traveling");
    check(card.destination == "Proxima",
          "the card must carry the destination system name");
    check(card.eta.find("days remaining") != std::string::npos,
          "a traveling fleet must report a transit ETA");
    check(card.summary.find("is traveling to Proxima") != std::string::npos,
          "the summary must match the reference traveling text");
    check(card.summary.find("transit days remain") != std::string::npos,
          "the summary must carry the transit estimate");
  }
  {
    // Held fleet at a station: awaiting order with the hold message.
    auto campaign = campaign_fixture();
    auto fleet = scout_fleet();
    fleet.current_system_id = 1;
    fleet.hold_requested = true;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::awaiting_order,
          "a held fleet at a station must await orders");
    check(card.summary.find("is held at Sol") != std::string::npos,
          "the hold summary must name the current system");
  }
  {
    // On-station scout at a detected (unsurveyed) system: scouting.
    auto campaign = campaign_fixture();
    auto fleet = scout_fleet();
    fleet.current_system_id = 2;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::scouting,
          "a scout at an unsurveyed system must be scouting");
    check(card.summary.find("is scouting Proxima") != std::string::npos,
          "the scout summary must name the system");
    check(card.summary.find("game days remain") != std::string::npos,
          "the scout summary must carry the remaining recon days");
  }
  {
    // On-station scout at a surveyed system: awaiting order.
    auto campaign = campaign_fixture();
    campaign.knowledge.mark_system_fully_surveyed(1, 2);
    auto fleet = scout_fleet();
    fleet.current_system_id = 2;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::awaiting_order &&
              card.eta == "Ready for orders",
          "a finished scout must await orders with the ready ETA");
  }
  {
    // On-station science at a partially surveyed system: science survey.
    auto campaign = campaign_fixture();
    campaign.knowledge.advance_system_survey(1, 2, .5);
    auto fleet = scout_fleet();
    fleet.role = FleetRole::Science;
    fleet.current_system_id = 2;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::science_survey,
          "a science fleet over a partial survey must be surveying");
    check(card.summary.find("conducting a detailed survey of Proxima") !=
              std::string::npos,
          "the science summary must name the system");
    check(card.summary.find("detailed-survey days remain") !=
              std::string::npos,
          "a partial survey must carry a known estimate");
  }
  {
    // Colony fleet carrying colonists at an unsurveyed system.
    auto campaign = campaign_fixture();
    auto fleet = scout_fleet();
    fleet.role = FleetRole::Colony;
    fleet.current_system_id = 2;
    fleet.embarked_population_millions = 2.5;
    fleet.embarked_population_species_id = "terran_baseline";
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::awaiting_order,
          "an unsurveyed site must not authorize settlement");
    check(card.summary.find("Terran Baseline") != std::string::npos &&
              card.summary.find("completed science survey is still required") !=
                  std::string::npos,
          "the colony summary must name the species and survey gate");
  }
  {
    // Unfunded operations suspend every mission.
    auto campaign = campaign_fixture();
    CivilizationEconomy economy;
    economy.civilization_id = 1;
    economy.last_base_operations_funding_fraction = 0.0;
    campaign.economies = {economy};
    auto fleet = scout_fleet();
    fleet.current_system_id = 1;
    fleet.destination_system_id = 2;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto &card = board.missions.front();
    check(card.phase == NativeMissionPhase::awaiting_order &&
              card.summary.find("operations are unfunded") != std::string::npos,
          "zero funding must suspend mission fleets");
  }
  {
    // Take(8): the board never exceeds eight cards.
    auto campaign = campaign_fixture();
    for (int i = 0; i < 12; ++i) {
      auto fleet = scout_fleet();
      fleet.id = 100 + i;
      fleet.current_system_id = 1;
      campaign.fleets.push_back(fleet);
    }
    const auto board = build_mission_board(campaign);
    check(board.missions.size() == 8, "the board must cap at eight missions");
    check(board.missions.front().fleet_id == 100,
          "missions must be ordered by fleet id");
  }
  {
    // Layout + hit behavior: close button issues Close; outside is free.
    auto campaign = campaign_fixture();
    auto fleet = scout_fleet();
    fleet.current_system_id = 1;
    fleet.destination_system_id = 2;
    campaign.fleets = {fleet};
    const auto board = build_mission_board(campaign);
    const auto layout = mission_layout_for(board, 1600, 900);
    check(layout.cards.size() == 1, "one mission must produce one card rect");

    NativeMissionView view;
    view.open();
    native_map::InputEvent close_event;
    close_event.type = native_map::InputEventType::LeftReleased;
    close_event.position = {layout.close_button.x + 4.f,
                            layout.close_button.y + 4.f};
    const auto close_command = view.handle(close_event, board, 1600, 900);
    check(close_command.kind == MissionViewCommandKind::Close &&
              close_command.captured,
          "the close button must issue a captured Close command");

    native_map::InputEvent map_press;
    map_press.type = native_map::InputEventType::LeftPressed;
    map_press.position = {40.f, 400.f};
    const auto map_command = view.handle(map_press, board, 1600, 900);
    check(!map_command.captured,
          "a press outside the panel must reach the map");

    native_map::InputEvent panel_press;
    panel_press.type = native_map::InputEventType::LeftPressed;
    panel_press.position = {layout.panel.x + 30.f, layout.panel.y + 60.f};
    const auto panel_command = view.handle(panel_press, board, 1600, 900);
    check(panel_command.captured &&
              panel_command.kind == MissionViewCommandKind::None,
          "a press inside the panel must be captured without a command");
  }
  {
    // Hidden views ignore input and emit no overlay.
    auto campaign = campaign_fixture();
    campaign.fleets = {scout_fleet()};
    const auto board = build_mission_board(campaign);
    NativeMissionView view;
    native_map::InputEvent press;
    press.type = native_map::InputEventType::LeftPressed;
    press.position = {1400.f, 200.f};
    check(!view.handle(press, board, 1600, 900).captured,
          "a hidden view must ignore input");
    native_map::DrawList out;
    view.render(out, board, 1600, 900);
    check(out.overlay.empty(), "a hidden view must not render");
    view.open();
    view.render(out, board, 1600, 900);
    check(!out.overlay.empty(), "a visible view must render the panel");
  }
  if (failures > 0) {
    std::cerr << failures << " mission panel checks failed\n";
    return 1;
  }
  std::cout << "native missions tests passed\n";
  return 0;
}
