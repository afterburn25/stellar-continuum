// Native economy panel tests — the ECONOMY presentation model behind the
// reference dashboard Economy page (SOVEREIGN TREASURY cards, treasury
// health, INDUSTRIAL PRIORITY toggles, DAILY CASH FLOW rows).
#include "native_economy.hpp"

#include <iostream>
#include <string>

using namespace stellar::native_economy;
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
  economy.credits = 1250.0;
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
    const auto view = build_economy_view(campaign, nullptr, std::nullopt);
    check(!view.ready, "a missing economy must not fabricate a treasury");
  }
  {
    // The player economy produces the six reference cards and flow rows.
    const auto campaign = campaign_fixture();
    const auto view = build_economy_view(campaign, nullptr, std::nullopt);
    check(view.ready, "a player economy must build a treasury view");
    check(view.cards[0].label == "RESERVES" &&
              view.cards[1].label == "NET / DAY" &&
              view.cards[2].label == "INCOME / DAY" &&
              view.cards[3].label == "COSTS / DAY" &&
              view.cards[4].label == "MATERIALS IN STORAGE" &&
              view.cards[5].label == "MATERIALS / DAY",
          "the six treasury cards must match the reference order");
    check(view.income_rows.size() == 2 && view.cost_rows.size() == 7,
          "the cash flow must carry two income and seven cost rows");
    check(view.cost_rows.back().label == "RESEARCH PROGRAMS" &&
              view.cost_rows.back().suffix.find("RESERVED") !=
                  std::string::npos,
          "the research row must carry the milestone reservation tail");
    check(!view.treasury_status.empty(),
          "the treasury health line must always be populated");
    check(view.priority_status.find("Current choice: Balanced (1:1)") !=
              std::string::npos,
          "a default economy must report the Balanced 1:1 priority");
    check(view.priority_status.find("applies when demand competes") !=
              std::string::npos,
          "without a last allocation the generic priority guidance shows");
  }
  {
    // Stored priority + last allocation surface the reference detail text.
    auto campaign = campaign_fixture();
    campaign.economies.front().industry_priority =
        IndustryPriority::InfrastructureFirst;
    CivilizationIndustryAllocation allocation;
    allocation.civilization_id = 1;
    allocation.construction_weight = 3.0;
    allocation.shipbuilding_weight = 1.0;
    allocation.construction_allocated = 42.5;
    allocation.shipbuilding_allocated = 14.2;
    const auto view = build_economy_view(campaign, nullptr, allocation);
    check(view.industry_priority == IndustryPriority::InfrastructureFirst,
          "the stored priority must drive the view");
    check(view.priority_status.find("Infrastructure first (3:1)") !=
                  std::string::npos &&
              view.priority_status.find("42.5 materials to infrastructure") !=
                  std::string::npos,
          "the last-allocation variant must report its split");
  }
  {
    // The panel captures inside presses, closes on X, and reports the
    // priority toggles in reference order.
    const auto campaign = campaign_fixture();
    const auto view = build_economy_view(campaign, nullptr, std::nullopt);
    const auto layout = economy_layout_for(view, 1600, 900);

    NativeEconomyPanel panel;
    native_map::InputEvent press;
    press.type = native_map::InputEventType::LeftReleased;
    press.position = {layout.panel.x + 4.f, layout.panel.y + 4.f};
    auto command = panel.handle(press, view, 1600, 900);
    check(command.kind == EconomyCommandKind::None && !command.captured,
          "a hidden panel must not capture input");

    panel.open();
    command = panel.handle(press, view, 1600, 900);
    check(command.captured && command.kind == EconomyCommandKind::None,
          "an inside press must be captured without a command");

    native_map::InputEvent priority;
    priority.type = native_map::InputEventType::LeftReleased;
    priority.position = {layout.priority_buttons[1].x + 4.f,
                         layout.priority_buttons[1].y + 4.f};
    command = panel.handle(priority, view, 1600, 900);
    check(command.kind == EconomyCommandKind::SetIndustryPriority &&
              command.priority == IndustryPriority::InfrastructureFirst,
          "the middle toggle must issue InfrastructureFirst");

    native_map::InputEvent outside;
    outside.type = native_map::InputEventType::LeftReleased;
    outside.position = {layout.panel.x - 20.f, layout.panel.y - 20.f};
    command = panel.handle(outside, view, 1600, 900);
    check(!command.captured, "an outside press must reach the map");

    native_map::InputEvent close;
    close.type = native_map::InputEventType::LeftReleased;
    close.position = {layout.close_button.x + 4.f, layout.close_button.y + 4.f};
    command = panel.handle(close, view, 1600, 900);
    check(command.kind == EconomyCommandKind::Close && command.captured &&
              !panel.visible(),
          "the close button must close the panel and capture the press");
  }
  {
    // The render path produces overlay geometry for an open panel.
    const auto campaign = campaign_fixture();
    const auto view = build_economy_view(campaign, nullptr, std::nullopt);
    NativeEconomyPanel panel;
    panel.open();
    native_map::DrawList draw;
    panel.render(draw, view, 1600, 900);
    check(!draw.overlay.empty(), "an open panel must emit overlay geometry");
  }
  if (failures) {
    std::cerr << failures << " native economy check(s) failed\n";
    return 1;
  }
  std::cout << "native economy tests passed\n";
  return 0;
}
