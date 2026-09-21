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
namespace native_colony = stellar::native_colony;

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
    const auto layout = mission_layout_for(board, {}, 0, 1600, 900, false);
    check(layout.cards.size() == 1, "one mission must produce one card rect");

    NativeMissionView view;
    view.open();
    native_map::InputEvent close_event;
    close_event.type = native_map::InputEventType::LeftReleased;
    close_event.position = {layout.close_button.x + 4.f,
                            layout.close_button.y + 4.f};
    const auto close_command = view.handle(close_event, board, {}, {}, 1600, 900);
    check(close_command.kind == MissionViewCommandKind::Close &&
              close_command.captured,
          "the close button must issue a captured Close command");

    native_map::InputEvent map_press;
    map_press.type = native_map::InputEventType::LeftPressed;
    map_press.position = {40.f, 400.f};
    const auto map_command = view.handle(map_press, board, {}, {}, 1600, 900);
    check(!map_command.captured,
          "a press outside the panel must reach the map");

    native_map::InputEvent panel_press;
    panel_press.type = native_map::InputEventType::LeftPressed;
    panel_press.position = {layout.panel.x + 30.f, layout.panel.y + 60.f};
    const auto panel_command = view.handle(panel_press, board, {}, {}, 1600, 900);
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
    check(!view.handle(press, board, {}, {}, 1600, 900).captured,
          "a hidden view must ignore input");
    native_map::DrawList out;
    view.render(out, board, {}, {}, 1600, 900);
    check(out.overlay.empty(), "a hidden view must not render");
    view.open();
    view.render(out, board, {}, {}, 1600, 900);
    check(!out.overlay.empty(), "a visible view must render the panel");
  }
  {
    // Colony-sites tab: no populated colony ships → unavailable state.
    const auto selection = colony_site_selection({}, 0, 0);
    check(!selection.available && !selection.fleet_id,
          "an empty fleet list must leave the site browser unavailable");
    check(selection.details.find("No populated colony ships") !=
              std::string::npos,
          "the empty sites state must carry the reference guidance");
  }
  {
    // Colony-sites tab with a fleet view: clamps indices, carries site ids.
    native_colony::NativeSettlementMissionView view;
    view.fleet_id = 21;
    view.fleet_name = "CSV Horizon";
    view.personnel_species_name = "Terran Baseline";
    view.personnel_millions = 2.5;
    view.can_receive_orders = true;
    view.treasury_budget_units = 500.0;
    view.authorization_budget_units = 120.0;
    view.formatted_authorization = "120 SC";
    view.formatted_treasury = "500 SC";
    native_colony::NativeSettlementCandidate site;
    site.system_id = 2;
    site.body_id = 7;
    site.system_name = "Proxima";
    site.body_name = "Proxima b";
    site.can_order = true;
    site.reason = "Within operational reach.";
    site.viability = SpeciesColonizationViability::NaturallyViable;
    site.reach.is_supported = true;
    site.reach.is_authoritative = true;
    view.candidates = {site};
    const std::vector<native_colony::NativeSettlementMissionView> fleets{
        view};
    const auto selection = colony_site_selection(fleets, 4, 9);
    check(selection.fleet_index == 0 && selection.site_index == 0,
          "site indices must clamp into the bounded lists");
    check(selection.fleet_id == 21 && selection.site_system_id == 2 &&
              selection.site_body_id == 7,
          "the selection must carry the fleet and site identities");
    check(selection.can_settle,
          "an orderable, funded site must permit settlement");
    check(selection.details.find("Colony ship 1/1: CSV Horizon") !=
                  std::string::npos &&
              selection.details.find("Proxima / Proxima b") !=
                  std::string::npos &&
              selection.details.find("natural") != std::string::npos,
          "the site details must match the reference builder");
  }
  {
    // Unfunded site: can_settle drops and the status asks for funding.
    native_colony::NativeSettlementMissionView view;
    view.fleet_id = 21;
    view.fleet_name = "CSV Horizon";
    view.can_receive_orders = true;
    view.treasury_budget_units = 40.0;
    view.authorization_budget_units = 120.0;
    view.formatted_authorization = "120 SC";
    view.formatted_treasury = "40 SC";
    native_colony::NativeSettlementCandidate site;
    site.system_id = 2;
    site.body_id = 7;
    site.can_order = true;
    site.reason = "Within operational reach.";
    view.candidates = {site};
    const std::vector<native_colony::NativeSettlementMissionView> fleets{
        view};
    const auto selection = colony_site_selection(fleets, 0, 0);
    check(!selection.can_settle,
          "an unfunded site must not permit settlement");
    check(selection.status.find("Settlement requires 120 SC") !=
              std::string::npos,
          "the status must report the funding shortfall");
  }
  {
    // Owned colony rows: own-only filtering, names resolved.
    auto campaign = campaign_fixture();
    Colony own;
    own.id = 30;
    own.civilization_id = 1;
    own.system_id = 1;
    own.name = "Landing";
    own.population_millions = 12.5;
    Colony foreign;
    foreign.id = 31;
    foreign.civilization_id = 2;
    foreign.system_id = 2;
    campaign.colonies = {foreign, own};
    const auto rows = build_owned_colony_rows(campaign);
    check(rows.size() == 1 && rows.front().colony_id == 30 &&
              rows.front().system_name == "Sol" &&
              rows.front().planet_name == "Orbital habitat",
          "owned colony rows must filter foreign colonies");
    check(!rows.front().is_resource_outpost && !rows.front().can_land &&
              !rows.front().can_request_freight &&
              rows.front().freight_reason.empty(),
          "a colony without a body offers no land or freight actions");
  }
  {
    // Owned colony actions: Land gating and the outpost Collect flow
    // (reference UiOwnedColonySnapshot + FindAvailableFreighter).
    auto campaign = campaign_fixture();
    PlanetaryBody solid;
    solid.id = 9;
    solid.system_id = 1;
    solid.name = "Meridian";
    solid.environment.has_solid_surface = true;
    campaign.bodies = {solid};

    Colony home;
    home.id = 30;
    home.civilization_id = 1;
    home.system_id = 1;
    home.name = "Landing";
    home.planetary_body_id = 9;
    Colony outpost;
    outpost.id = 31;
    outpost.civilization_id = 1;
    outpost.system_id = 2;
    outpost.name = "Pit 7";
    outpost.kind = SettlementKind::ResourceOutpost;
    outpost.planetary_body_id = 9;
    outpost.stored_extracted_materials = 40.0;
    campaign.colonies = {home, outpost};

    // No idle freighter → Collect gated with the reference reason.
    auto rows = build_owned_colony_rows(campaign);
    check(rows.size() == 2 && rows.front().can_land,
          "a solid-surface colony must allow landing");
    check(rows.back().is_resource_outpost &&
              !rows.back().can_request_freight &&
              rows.back().freight_reason.find(
                  "Build an Interstellar Bulk Freighter") !=
                  std::string::npos,
          "a freighter-less outpost must report the build-a-freighter reason");

    // An idle bulk freighter at a developed colony unlocks Collect.
    FleetState freighter;
    freighter.id = 44;
    freighter.civilization_id = 1;
    freighter.name = "FTL Meridian";
    freighter.role = FleetRole::Logistics;
    freighter.design_id = "bulk_freighter";
    freighter.is_active = true;
    freighter.current_system_id = 1;
    freighter.cargo_material_capacity = 85;
    campaign.fleets = {freighter};
    rows = build_owned_colony_rows(campaign);
    check(rows.back().can_request_freight &&
              rows.back().freight_reason.find(
                  "Dispatch FTL Meridian to collect up to 85 material units") !=
                  std::string::npos,
          "an idle freighter must unlock Collect with the dispatch reason");
    check(find_available_freighter(campaign) &&
              find_available_freighter(campaign)->id == 44,
          "the freighter scan must return the idle bulk freighter");

    // A committed freighter (destination assigned) is not available.
    campaign.fleets.front().destination_system_id = 2;
    check(!find_available_freighter(campaign),
          "an assigned freighter must not satisfy the availability scan");
  }
  {
    // Collect/Land clicks issue the new commands from the sites tab.
    auto campaign = campaign_fixture();
    PlanetaryBody solid;
    solid.id = 9;
    solid.system_id = 1;
    solid.environment.has_solid_surface = true;
    campaign.bodies = {solid};
    Colony outpost;
    outpost.id = 31;
    outpost.civilization_id = 1;
    outpost.system_id = 1;
    outpost.name = "Pit 7";
    outpost.kind = SettlementKind::ResourceOutpost;
    outpost.planetary_body_id = 9;
    outpost.stored_extracted_materials = 40.0;
    Colony home;
    home.id = 30;
    home.civilization_id = 1;
    home.system_id = 1;
    home.planetary_body_id = 9;
    campaign.colonies = {home, outpost};
    const auto colonies = build_owned_colony_rows(campaign);
    const auto board = build_mission_board(campaign);
    const std::vector<native_colony::NativeSettlementMissionView> fleets{};

    NativeMissionView panel;
    panel.open();
    const auto selection = colony_site_selection(fleets, 0, 0);
    const auto layout = mission_layout_for(board, selection, colonies.size(),
                                           1600, 900, true);
    native_map::InputEvent tab;
    tab.type = native_map::InputEventType::LeftReleased;
    tab.position = {layout.sites_tab.x + 4.f, layout.sites_tab.y + 4.f};
    (void)panel.handle(tab, board, fleets, colonies, 1600, 900);

    native_map::InputEvent collect;
    collect.type = native_map::InputEventType::LeftReleased;
    collect.position = {layout.colony_collect_buttons[1].x + 4.f,
                        layout.colony_collect_buttons[1].y + 4.f};
    const auto collect_command =
        panel.handle(collect, board, fleets, colonies, 1600, 900);
    check(collect_command.kind ==
              MissionViewCommandKind::CollectOutpostFreight &&
              collect_command.colony_id == 31,
          "an outpost Collect button must issue CollectOutpostFreight");

    // A non-outpost row has no clickable Collect affordance.
    native_map::InputEvent collect_home;
    collect_home.type = native_map::InputEventType::LeftReleased;
    collect_home.position = {layout.colony_collect_buttons[0].x + 4.f,
                             layout.colony_collect_buttons[0].y + 4.f};
    const auto declined =
        panel.handle(collect_home, board, fleets, colonies, 1600, 900);
    check(declined.kind == MissionViewCommandKind::None && declined.captured,
          "a non-outpost row must not issue a freight command");

    native_map::InputEvent land;
    land.type = native_map::InputEventType::LeftReleased;
    land.position = {layout.colony_land_buttons[0].x + 4.f,
                     layout.colony_land_buttons[0].y + 4.f};
    const auto land_command =
        panel.handle(land, board, fleets, colonies, 1600, 900);
    check(land_command.kind == MissionViewCommandKind::LandColony &&
              land_command.colony_id == 30,
          "a solid-surface colony Land button must issue LandColony");
  }
  {
    // Sites tab interaction: tab switch, select-ship focus, colony View.
    auto campaign = campaign_fixture();
    const auto board = build_mission_board(campaign);
    native_colony::NativeSettlementMissionView view;
    view.fleet_id = 21;
    view.fleet_name = "CSV Horizon";
    view.can_receive_orders = true;
    view.treasury_budget_units = 500.0;
    view.authorization_budget_units = 120.0;
    native_colony::NativeSettlementCandidate site;
    site.system_id = 2;
    site.body_id = 7;
    site.can_order = true;
    view.candidates = {site};
    const std::vector<native_colony::NativeSettlementMissionView> fleets{
        view};
    Colony own;
    own.id = 30;
    own.civilization_id = 1;
    own.system_id = 1;
    own.name = "Landing";
    campaign.colonies = {own};
    const auto colonies = build_owned_colony_rows(campaign);

    NativeMissionView panel;
    panel.open();
    const auto selection = colony_site_selection(fleets, 0, 0);
    const auto layout =
        mission_layout_for(board, selection, colonies.size(), 1600, 900,
                           true);

    // Open the sites tab.
    native_map::InputEvent tab;
    tab.type = native_map::InputEventType::LeftReleased;
    tab.position = {layout.sites_tab.x + 4.f, layout.sites_tab.y + 4.f};
    const auto tab_command =
        panel.handle(tab, board, fleets, colonies, 1600, 900);
    check(tab_command.captured &&
              tab_command.kind == MissionViewCommandKind::None,
          "the sites tab must be captured without a command");

    // Select ship on map → FocusFleet.
    native_map::InputEvent pick;
    pick.type = native_map::InputEventType::LeftReleased;
    pick.position = {layout.select_ship.x + 4.f, layout.select_ship.y + 4.f};
    const auto pick_command =
        panel.handle(pick, board, fleets, colonies, 1600, 900);
    check(pick_command.kind == MissionViewCommandKind::FocusFleet &&
              pick_command.fleet_id == 21,
          "select-ship must issue FocusFleet for the chosen fleet");

    // Owned colony View → OpenColony.
    native_map::InputEvent open;
    open.type = native_map::InputEventType::LeftReleased;
    open.position = {layout.colony_view_buttons.front().x + 4.f,
                     layout.colony_view_buttons.front().y + 4.f};
    const auto open_command =
        panel.handle(open, board, fleets, colonies, 1600, 900);
    check(open_command.kind == MissionViewCommandKind::OpenColony &&
              open_command.colony_id == 30,
          "a colony View button must issue OpenColony");

    native_map::DrawList out;
    panel.render(out, board, fleets, colonies, 1600, 900);
    check(!out.overlay.empty(), "the sites tab must render");
  }
  if (failures > 0) {
    std::cerr << failures << " mission panel checks failed\n";
    return 1;
  }
  std::cout << "native missions tests passed\n";
  return 0;
}
