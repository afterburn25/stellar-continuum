#include "native_economy.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>

using namespace stellar::core;
using namespace stellar::native_economy;

namespace {
int failures{};
void check(const bool value, const char* message) {
  if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

FreshCampaignState fixture() {
  FreshCampaignState world;
  world.player_civilization_id = 7;
  world.systems = {{.id = 12, .name = "Sol"}};
  world.civilizations = {{.id = 7, .name = "Actual", .home_system_id = 12, .is_player = true},
                         {.id = 9, .name = "Foreign", .home_system_id = 13}};
  world.colonies = {{.id = 1, .civilization_id = 7, .system_id = 12, .name = "Earth", .population_millions = 800.}};
  world.economies = {{.civilization_id = 7, .credits = 500., .industry = 200., .last_industry_per_second = 2.},
                     {.civilization_id = 9, .credits = 9999999., .industry = 9999999.}};
  world.construction = {{.civilization_id = 7}};
  return world;
}

CampaignFrame frame(const std::filesystem::path& research, const std::filesystem::path& catalog) {
  auto world = seed_persistable_fresh_campaign(662901, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  auto runtime = load_adaptive_research_strategic_runtime(research);
  return {IntegratedAdaptiveCampaignRuntime::create_fresh(std::move(runtime), std::move(world)),
          StrategicClock{}, CampaignFramePolicy::Player};
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  {
    const auto view = build_economy_view(fixture(), nullptr, std::nullopt);
    check(view.state == EconomyState::Ready, "valid actual player should produce an economy view");
    check(view.cards[0].label == "RESERVES" && view.cards[5].label == "MATERIALS / DAY",
          "view retains the canonical six-card order");
    check(view.cards[5].value == "+2.00 / DAY",
          "material flow remains raw materials per day rather than sovereign currency");
    check(view.income_rows.size() == 2 && view.cost_rows.size() == 7 &&
          view.cost_rows.back().suffix.contains("RESERVED"),
          "view retains the canonical income, costs, and reservation row");
    check(view.priority_status.contains("Balanced (1:1)"), "null priority defaults to Balanced");
  }
  {
    auto world = fixture(); world.economies.erase(world.economies.begin());
    const auto missing_economy = build_economy_view(world, nullptr, std::nullopt);
    check(missing_economy.state == EconomyState::Unavailable &&
          missing_economy.player_civilization_id == world.player_civilization_id &&
          !missing_economy.diagnostic.empty(),
          "missing player economy is explicit unavailable rather than initialized data");
    world = fixture(); world.civilizations.front().is_player = false;
    check(build_economy_view(world, nullptr, std::nullopt).state == EconomyState::Unavailable,
          "a non-player actual observer cannot project foreign economy data");
    world = fixture(); world.civilizations.push_back(world.civilizations.front());
    check(build_economy_view(world, nullptr, std::nullopt).state == EconomyState::Unavailable,
          "a duplicated actual observer cannot select arbitrary telemetry");
    world = fixture(); world.economies.push_back(world.economies.front());
    check(build_economy_view(world, nullptr, std::nullopt).state == EconomyState::Unavailable,
          "a duplicated actual economy is explicitly unavailable");
    world = fixture(); world.economies.front().credits = std::numeric_limits<double>::quiet_NaN();
    check(build_economy_view(world, nullptr, std::nullopt).state == EconomyState::Failed,
          "non-finite player values are never formatted");
    world = fixture(); world.economies.front().industry = std::numeric_limits<double>::max();
    check(build_economy_view(world, nullptr, std::nullopt).state == EconomyState::Ready,
          "a huge finite material store formats safely without integer rounding");
    world = fixture(); world.colonies.clear();
    const auto surplus = build_economy_view(world, nullptr, std::nullopt);
    check(surplus.state == EconomyState::Ready && surplus.treasury_healthy,
          "a surplus treasury accepts Core's infinite runway because it is not displayed");
  }
  {
    auto world = fixture();
    CivilizationIndustryAllocation foreign{.civilization_id = 9, .construction_allocated = 99., .shipbuilding_allocated = 1.};
    const auto view = build_economy_view(world, nullptr, foreign);
    check(!view.priority_status.contains("99.0"), "foreign historical allocation is ignored");
    foreign.civilization_id = 7; foreign.construction_allocated = std::numeric_limits<double>::infinity();
    check(!build_economy_view(world, nullptr, foreign).priority_status.contains("inf"),
          "non-finite historical allocation is ignored");
  }
  {
    auto live = frame(argv[1], argv[2]);
    int calls{};
    NativeEconomyController controller([&](const FreshCampaignState& campaign,
                                           const AdaptiveResearchCampaignState*,
                                           const std::optional<CivilizationIndustryAllocation>& allocation) {
      ++calls;
      if (calls == 1) throw std::runtime_error(std::string(400, 'x'));
      return build_economy_view(campaign, nullptr, allocation);
    });
    check(controller.refresh(live, 4, std::nullopt), "first controller admission runs projector");
    check(controller.view().state == EconomyState::Failed && controller.view().diagnostic.size() <= 256,
          "projection failure is latched with bounded diagnostics");
    check(!controller.refresh(live, 4, std::nullopt) && calls == 1,
          "latched failure does not recompute every frame");
    check(controller.refresh(live, 4, std::nullopt, true) && calls == 2 &&
          controller.view().state == EconomyState::Ready,
          "explicit retry performs exactly one further projection");
    const auto revision = controller.view().revision;
    check(controller.refresh(live, 4, std::nullopt) && controller.view().revision == revision,
          "unchanged rendered content preserves revision");
    check(!controller.refresh(live, 3, std::nullopt) && calls == 3,
          "an older generation cannot replace the current economy view");
    auto& campaign = live.runtime().world().campaign();
    const auto actor = campaign.player_civilization_id;
    const auto before_player_priority = std::ranges::find(campaign.economies, actor,
        &CivilizationEconomy::civilization_id)->industry_priority;
    const auto before_foreign = std::ranges::find_if(campaign.economies,
        [actor](const auto& item) { return item.civilization_id != actor; });
    const auto foreign_credits = before_foreign == campaign.economies.end() ? 0. : before_foreign->credits;
    check(!controller.change_priority(live, 4, revision + 1, IndustryPriority::ShipbuildingFirst).accepted &&
          std::ranges::find(campaign.economies, actor, &CivilizationEconomy::civilization_id)->industry_priority == before_player_priority &&
          (before_foreign == campaign.economies.end() || std::ranges::find_if(campaign.economies,
              [actor](const auto& item) { return item.civilization_id != actor; })->credits == foreign_credits),
          "stale priority command changes no canonical state");
    const auto accepted = controller.change_priority(live, 4, revision, IndustryPriority::ShipbuildingFirst);
    check(accepted.accepted, "current priority command reaches canonical Core command");
    check(!controller.change_priority(live, 4, revision, IndustryPriority::Balanced).accepted,
          "accepted priority command consumes its displayed revision");
    check(!controller.change_priority(live, 4, 0, static_cast<IndustryPriority>(99)).accepted,
          "invalid priority enum is denied");
    check(std::ranges::find(campaign.economies, actor, &CivilizationEconomy::civilization_id)->industry_priority ==
              IndustryPriority::ShipbuildingFirst, "accepted command only updates the player priority");
    check(controller.refresh(live, 4, std::nullopt) && controller.view().revision != 0,
          "refresh after priority command creates a new actionable view revision");
    const auto same_choice = controller.change_priority(live, 4, controller.view().revision,
                                                        IndustryPriority::ShipbuildingFirst);
    check(same_choice.accepted && controller.refresh(live, 4, std::nullopt) &&
          controller.view().revision != 0 &&
          controller.change_priority(live, 4, controller.view().revision,
                                     IndustryPriority::Balanced).accepted,
          "a same-priority click refreshes to an actionable revision for the next choice");
    check(controller.refresh(live, 4, std::nullopt), "refresh after priority command creates a current view");
    const auto current_revision = controller.view().revision;
    campaign.economies.erase(std::ranges::find(campaign.economies, actor,
                                                &CivilizationEconomy::civilization_id));
    check(!controller.change_priority(live, 4, current_revision, IndustryPriority::Balanced).accepted,
          "loss of the current economy record rejects the command without mutation");
  }
  if (failures) return 1;
  std::cout << "native economy tests passed\n";
}
