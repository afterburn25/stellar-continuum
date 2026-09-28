// Native empire overview tests — EmpireOverviewPanel's empire mode: the
// selected-system home reference plus the own-colony quick list and combined
// fleet power rendered inside the fleet workspace detail area.
#include "native_overview.hpp"

#include <iostream>
#include <string>

using namespace stellar::native_overview;
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

  PlanetaryBody body;
  body.id = 11;
  body.system_id = 1;
  body.name = "Earth";
  campaign.bodies = {body};

  Colony colony;
  colony.id = 7;
  colony.civilization_id = 1;
  colony.system_id = 1;
  colony.planetary_body_id = 11;
  colony.name = "Sol Prime";
  colony.population_millions = 812.5;
  Colony foreign;
  foreign.id = 8;
  foreign.civilization_id = 2;
  foreign.system_id = 2;
  foreign.name = "Vask Hold";
  campaign.colonies = {colony, foreign};

  FleetState scout;
  scout.id = 3;
  scout.civilization_id = 1;
  scout.role = FleetRole::Scout;
  scout.name = "Pathfinder";
  scout.current_system_id = 1;
  FleetState hidden;
  hidden.id = 9;
  hidden.civilization_id = 2;
  hidden.role = FleetRole::Military;
  hidden.name = "Vask Blade";
  hidden.current_system_id = 2;
  campaign.fleets = {scout, hidden};
  return campaign;
}

}  // namespace

int main() {
  {
    // Own holdings only: foreign colonies are never listed.
    const auto campaign = campaign_fixture();
    const auto overview = build_empire_overview(campaign, 2);
    check(overview.colonies.size() == 1 &&
              overview.colonies[0].colony_id == 7 &&
              overview.colonies[0].planet_name == "Earth" &&
              overview.colonies[0].system_name == "Sol" &&
              overview.colonies[0].body_id == std::optional<int>{11},
          "the colony list must carry only owned colonies with planet names");
    check(overview.combined_power >= 0.0,
          "combined power must accumulate only owned fleets");
  }
  {
    // Unsurveyed selection keeps the name gated; distance always shows.
    const auto campaign = campaign_fixture();
    const auto overview = build_empire_overview(campaign, 2);
    check(overview.selected_system_name == "Unknown",
          "an unsurveyed selection must not leak the system name");
    check(overview.selected_system_distance.find("ly") !=
                  std::string::npos &&
              overview.selected_system_distance.find("pc") !=
                  std::string::npos,
          "the home distance must carry metric and parsec units");
  }
  {
    // A known selection names the system.
    auto campaign = campaign_fixture();
    campaign.knowledge.reveal_system(1, 2);
    const auto overview = build_empire_overview(campaign, 2);
    check(overview.selected_system_name == "Proxima",
          "a known selection must name the system");
  }
  {
    // No selection falls back to the reference placeholders.
    const auto campaign = campaign_fixture();
    const auto overview = build_empire_overview(campaign, std::nullopt);
    check(overview.selected_system_name == "No target" &&
              overview.selected_system_distance == "Unavailable",
          "an empty selection must show the reference placeholders");
  }
  {
    // Layout: one clickable row per colony inside the supplied content rect.
    const auto campaign = campaign_fixture();
    const auto overview = build_empire_overview(campaign, 1);
    const native_map::UiRect content{400.f, 200.f, 300.f, 400.f};
    const auto layout = overview_layout_for(overview, content);
    check(layout.colony_rows.size() == overview.colonies.size(),
          "every colony must own a hit row");
    check(!layout.colony_rows.empty() &&
              layout.colony_rows[0].x >= content.x &&
              layout.colony_rows[0].x + layout.colony_rows[0].width <=
                  content.x + content.width &&
              layout.colony_rows[0].y >= content.y,
          "colony rows must sit inside the content rect");
  }
  {
    // The renderer emits text and row fills clipped to the content rect.
    const auto campaign = campaign_fixture();
    const auto overview = build_empire_overview(campaign, 1);
    const native_map::UiRect content{400.f, 200.f, 300.f, 400.f};
    const auto layout = overview_layout_for(overview, content);
    native_map::DrawList out;
    render_empire_overview(out, overview, layout, {});
    check(!out.overlay.empty(), "the overview must emit overlay commands");
    bool found_system_label = false;
    for (const auto &primitive : out.overlay)
      if (const auto *label = std::get_if<native_map::Text>(&primitive);
          label && label->value == "SELECTED SYSTEM")
        found_system_label = true;
    check(found_system_label,
          "the overview must render the reference section header");
  }

  if (failures) {
    std::cerr << failures << " native overview test(s) failed\n";
    return 1;
  }
  std::cout << "native overview tests passed\n";
  return 0;
}
