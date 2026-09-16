// Native logistics panel tests — the SUPPLY NETWORK presentation model behind
// the reference LogisticsNetworkPanel + Main.Logistics: summary line, home-
// system node rows, shortfall guidance, sparse-campaign safety and rendering.
#include "native_logistics.hpp"

#include <iostream>
#include <string>

using namespace stellar::native_logistics;
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
  campaign.systems = {home};

  Civilization player;
  player.id = 1;
  player.name = "Terrans";
  player.home_system_id = 1;
  player.is_player = true;
  campaign.civilizations = {player};

  Colony own;
  own.id = 7;
  own.civilization_id = 1;
  own.system_id = 1;
  own.name = "Sol Prime";
  own.population_millions = 812.5;
  campaign.colonies = {own};

  CivilizationEconomy economy;
  economy.civilization_id = 1;
  campaign.economies = {economy};
  ConstructionState construction;
  construction.civilization_id = 1;
  campaign.construction = {construction};
  return campaign;
}

}  // namespace

int main() {
  {
    // A campaign without an economy row reports the initializing state.
    auto campaign = campaign_fixture();
    campaign.economies.clear();
    const auto logistics = build_home_logistics(campaign, 1);
    check(!logistics.ready && logistics.nodes.empty(),
          "a missing economy must not fabricate a network");
    check(logistics_summary_line(campaign, 1) == "Supply initializing…",
          "a missing economy must report the initializing summary");
  }
  {
    // A home-system colony produces a homeworld node with kind/status labels.
    const auto campaign = campaign_fixture();
    const auto logistics = build_home_logistics(campaign, 1);
    check(logistics.ready, "a home colony must build a network");
    check(logistics.system_name == "Sol",
          "the header must name the home system");
    check(!logistics.nodes.empty() &&
              logistics.nodes.front().kind_label == "Homeworld",
          "the first home colony must be the homeworld node");
    check(!logistics.nodes.front().status.empty(),
          "each node must carry a supply status");
    check(!logistics.guidance.empty(),
          "the network must always carry guidance");
    check(logistics_summary_line(campaign, 1).find("Supply") == 0,
          "the summary line must lead with the supply condition");
  }
  {
    // Kind labels match the reference FormatNodeKind table.
    check(logistics_node_kind_label(LogisticsNodeKind::OrbitalHub) ==
              "Orbital hub",
          "orbital hub label");
    check(logistics_node_kind_label(LogisticsNodeKind::ResourceSite) ==
              "Resource site",
          "resource site label");
    check(logistics_node_kind_label(LogisticsNodeKind::Shipyard) == "Shipyard",
          "shipyard label");
  }
  {
    // The view toggles, contains panel input, and closes via its button.
    const auto campaign = campaign_fixture();
    const auto logistics = build_home_logistics(campaign, 1);
    NativeLogisticsView view;
    check(!view.visible(), "the panel must start hidden");
    view.toggle();
    check(view.visible(), "toggle must open the panel");
    const auto layout = logistics_layout_for(logistics, 1600, 900);
    native_map::InputEvent inside{native_map::InputEventType::LeftPressed,
                                  {layout.panel.x + 5.f, layout.panel.y + 5.f}};
    check(view.handle(inside, logistics, 1600, 900),
          "presses inside the panel must be contained");
    check(view.visible(), "an interior press must not close the panel");
    native_map::InputEvent outside{native_map::InputEventType::LeftPressed,
                                   {10.f, 400.f}};
    check(!view.handle(outside, logistics, 1600, 900),
          "presses outside the panel must pass through");
    native_map::InputEvent close{native_map::InputEventType::LeftReleased,
                                 {layout.close_button.x + 5.f,
                                  layout.close_button.y + 5.f}};
    check(view.handle(close, logistics, 1600, 900) && !view.visible(),
          "the close button must dismiss the panel");
  }
  {
    // The renderer emits a bounded panel with metric tiles and node cards.
    const auto campaign = campaign_fixture();
    const auto logistics = build_home_logistics(campaign, 1);
    const auto layout = logistics_layout_for(logistics, 1600, 900);
    check(layout.panel.width > 200.f && layout.node_cards.size() ==
              std::min<std::size_t>(8, logistics.nodes.size()),
          "layout must bound one card per node up to eight");
    NativeLogisticsView view;
    view.open();
    native_map::DrawList out;
    view.render(out, logistics, 1600, 900);
    check(!out.overlay.empty(), "the panel must emit overlay commands");
  }

  if (failures) {
    std::cerr << failures << " native logistics test(s) failed\n";
    return 1;
  }
  std::cout << "native logistics tests passed\n";
  return 0;
}
