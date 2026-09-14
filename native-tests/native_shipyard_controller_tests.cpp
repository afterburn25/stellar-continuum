#include "native_shipyard_controller.hpp"

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/ship_designs.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace stellar::core;
using namespace stellar::native_shipyard;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] bool near(double left, double right) {
  return std::abs(left - right) <= .000001;
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

[[nodiscard]] CampaignFrame source_frame(const fs::path &research_root,
                                         const fs::path &fixture,
                                         const fs::path &scratch) {
  const auto path = scratch / "shipyard-player17.json";
  write(path, source_player17(fixture));
  auto loaded = load_existing_player_campaign_v17(
      path, [research_root] {
        return load_adaptive_research_strategic_runtime(research_root);
      });
  StrategicClock clock;
  clock.restore(loaded.campaign.simulation_days());
  return CampaignFrame(std::move(loaded.campaign).activate(), std::move(clock),
                       CampaignFramePolicy::Player);
}

[[nodiscard]] FreshCampaignState fresh_world(const fs::path &catalog) {
  return seed_persistable_fresh_campaign(
      108500, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
}

[[nodiscard]] CampaignFrame enabled_frame(const fs::path &research_root,
                                          const fs::path &catalog) {
  auto world = fresh_world(catalog);
  auto research_runtime =
      load_adaptive_research_strategic_runtime(research_root);
  auto research = AdaptiveResearchCampaignFactory(research_runtime).create(world);
  auto snapshot =
      AdaptiveResearchCampaignSnapshotCodec(research_runtime).capture(research);
  const auto civilization = std::ranges::find(
      snapshot.civilizations, world.player_civilization_id,
      &AdaptiveResearchCampaignCivilizationSnapshot::civilization_id);
  require(civilization != snapshot.civilizations.end(),
          "Fresh player research snapshot is missing.");
  auto &capabilities =
      civilization->research.research.research.research.core.capabilities;
  capabilities.push_back({"spacecraft_construction", std::nullopt});
  capabilities.push_back(
      {"experimental_interstellar_transit", std::nullopt});

  const auto construction = std::ranges::find(
      world.construction, world.player_civilization_id,
      &ConstructionState::civilization_id);
  const auto economy = std::ranges::find(
      world.economies, world.player_civilization_id,
      &CivilizationEconomy::civilization_id);
  require(construction != world.construction.end() &&
              economy != world.economies.end(),
          "Fresh player shipbuilding state is incomplete.");
  construction->completed_project_ids.push_back("orbital_shipyard");
  economy->credits = 1000.;
  economy->industry = 200.;

  return CampaignFrame(IntegratedAdaptiveCampaignRuntime::restore_research(
                           std::move(research_runtime), std::move(world), snapshot,
                           {}, 0.),
                       StrategicClock{}, CampaignFramePolicy::Player);
}

void locked_source_and_fresh(const fs::path &research_root,
                             const fs::path &catalog,
                             const fs::path &fixture,
                             const fs::path &scratch) {
  auto source = source_frame(research_root, fixture, scratch);
  NativeShipyardController source_controller;
  auto source_view = source_controller.build(source, 20);
  require(source_view.available_designs.empty() && source_view.orders.empty() &&
              !source_view.orbital_shipyard_complete,
          "Actual-source Player17 did not retain its locked empty shipyard.");
  const auto &source_world = source.runtime().world().campaign();
  require(source_view.player_civilization_id ==
              source_world.player_civilization_id &&
              source_view.home_system_id ==
                  std::ranges::find(source_world.civilizations,
                                    source_world.player_civilization_id,
                                    &Civilization::id)->home_system_id,
          "Player shipyard ownership was projected from the wrong civilization.");
  const auto source_credits = source_view.treasury_credits;
  const auto source_fleets = source_world.fleets.size();
  const auto source_denied = source_controller.start(
      source, 20, source_view.shipyard_revision, "warp_scout");
  require(!source_denied.accepted &&
              near(source_controller.build(source, 20).treasury_credits,
                   source_credits) &&
              source_world.fleets.size() == source_fleets,
          "Locked source campaign accepted or mutated a vessel order.");

  auto fresh = fresh_world(catalog);
  require(fresh.fleets.empty(), "Canonical fresh campaign invented a fleet.");
  CampaignFrame frame(IntegratedAdaptiveCampaignRuntime::create_fresh(
                          load_adaptive_research_strategic_runtime(research_root),
                          std::move(fresh)),
                      StrategicClock{}, CampaignFramePolicy::Player);
  NativeShipyardController controller;
  const auto view = controller.build(frame, 1);
  require(view.available_designs.empty() && view.orders.empty() &&
              !view.orbital_shipyard_complete &&
              frame.runtime().world().campaign().fleets.empty(),
          "Pre-warp fresh campaign exposed or constructed a ship.");
}

void canonical_orders(const fs::path &research_root,
                      const fs::path &catalog) {
  auto frame = enabled_frame(research_root, catalog);
  auto &world = frame.runtime().world().campaign();
  NativeShipyardController controller;
  constexpr std::uint64_t generation = 30;
  auto view = controller.build(frame, generation);
  require(world.fleets.empty() && view.orbital_shipyard_complete &&
              view.available_designs.size() == ship_design_catalog().size() &&
              view.orders.empty() && view.currency.name == "United Earth Dollar" &&
              view.currency.code == "UED" &&
              view.formatted_treasury == view.currency.format(1000.),
          "Authored enabled state did not expose the canonical design catalog.");
  for (const auto &projected : view.available_designs) {
    const auto &design = get_ship_design(projected.id);
    const auto performance = effective_ship_propulsion(
        {world.construction, {}, [&](int civilization, std::string_view id) {
           return civilization == view.player_civilization_id &&
                  (id == "spacecraft_construction" ||
                   id == "experimental_interstellar_transit");
         }},
        view.player_civilization_id, design);
    require(projected.name == design.name &&
                near(projected.industry_cost, design.industry_cost) &&
                near(projected.credit_cost, design.credit_cost) &&
                near(projected.minimum_build_days_at_full_shipyard_rate,
                     design.industry_cost / shipbuilding_industry_per_day) &&
                near(projected.strategic_speed, performance.strategic_speed) &&
                near(projected.maximum_leg_range_light_years,
                     performance.maximum_leg_range_light_years) &&
                projected.formatted_credit_cost ==
                    view.currency.format(design.credit_cost) &&
                projected.can_start && !projected.start_blocker &&
                !projected.will_queue,
            "Ship design requirements or performance were re-derived.");
  }
  const auto player = std::ranges::find(
      world.civilizations, view.player_civilization_id, &Civilization::id);
  require(player != world.civilizations.end(),
          "Player civilization disappeared from shipyard state.");
  const auto economy = std::ranges::find(
      world.economies, view.player_civilization_id,
      &CivilizationEconomy::civilization_id);
  require(economy != world.economies.end(), "Player economy disappeared.");
  const auto human_revision = view.shipyard_revision;
  const auto human_treasury = view.formatted_treasury;
  const auto before_currency_change_credits = economy->credits;
  const auto before_currency_change_orders =
      static_cast<int>(view.orders.size());
  player->species_id = "pelagic_high_pressure";
  const auto stale_currency = controller.start(
      frame, generation, view.shipyard_revision, "warp_scout");
  require(!stale_currency.accepted &&
              near(economy->credits, before_currency_change_credits) &&
              world.shipyards.front().pending_build_count() ==
                  before_currency_change_orders,
          "A currency change accepted or mutated an old shipyard quote.");
  view = controller.build(frame, generation);
  require(view.shipyard_revision > human_revision &&
              view.currency.name == "Tide Mark" &&
              view.currency.code == "TM" && view.currency.symbol == "◈" &&
              view.formatted_treasury != human_treasury,
          "Species currency change reused the old shipyard revision.");
  const auto pelagic_revision = view.shipyard_revision;
  player->species_id = "terran_baseline";
  view = controller.build(frame, generation);
  require(view.shipyard_revision > pelagic_revision &&
              view.currency.code == "UED",
          "Restored player currency did not invalidate the shipyard view.");

  const auto unchanged_fleets = world.fleets.size();
  const auto stale = controller.start(frame, generation + 1,
                                      view.shipyard_revision, "warp_scout");
  require(!stale.accepted && world.fleets.size() == unchanged_fleets,
          "Foreign campaign generation accepted a ship order.");
  const auto unknown = controller.start(frame, generation,
                                        view.shipyard_revision, "hidden-design");
  require(!unknown.accepted, "Unknown design was accepted.");

  economy->credits = 0.;
  const auto denial_credits = economy->credits;
  const auto denial_orders = world.shipyards.front().pending_build_count();
  const auto denied = controller.start(frame, generation,
                                       view.shipyard_revision, "warp_scout");
  require(!denied.accepted &&
              denied.message.find("required to authorize") != std::string::npos &&
              near(economy->credits, denial_credits) &&
              world.shipyards.front().pending_build_count() == denial_orders,
          "A current canonical funding denial was hidden as a stale view or mutated state.");
  view = controller.build(frame, generation);
  const auto denied_design = std::ranges::find(
      view.available_designs, std::string("warp_scout"),
      &NativeShipDesign::id);
  require(denied_design != view.available_designs.end() &&
              !denied_design->can_start && denied_design->start_blocker &&
              denied_design->start_blocker->find("required to authorize") !=
                  std::string::npos,
          "Canonical precommit funding denial was not projected.");
  economy->credits = 1000.;
  view = controller.build(frame, generation);
  const auto colony_readiness = std::ranges::find(
      view.available_designs, std::string("colony_ship"),
      &NativeShipDesign::id);
  require(colony_readiness != view.available_designs.end() &&
              colony_readiness->can_start &&
              near(colony_readiness->minimum_source_population_millions,
                   750.) &&
              colony_readiness->population_source_colony_id &&
              colony_readiness->population_species_id == "terran_baseline" &&
              colony_readiness->population_source_current_millions,
          "Canonical colony-ship population readiness was not projected.");
  const auto before_scout = economy->credits;
  const auto scout = controller.start(frame, generation,
                                      view.shipyard_revision, "warp_scout");
  require(scout.accepted && near(economy->credits, before_scout - 70.) &&
              world.fleets.empty(),
          "Canonical scout authorization did not reserve credits exactly.");
  const auto reused = controller.start(frame, generation,
                                       view.shipyard_revision, "science_vessel");
  require(!reused.accepted, "A consumed shipyard view accepted another action.");

  view = controller.build(frame, generation);
  require(view.orders.size() == 1 && view.orders.front().active &&
              near(view.orders.front().industry_progress, 0.) &&
              near(view.orders.front().authorization_credits, 70.) &&
              view.orders.front().formatted_refund ==
                  view.currency.format(view.orders.front().refund_credits),
          "Active ship order projection lost canonical accounting.");
  Colony *source_colony{};
  for (auto &colony : world.colonies)
    if (colony.civilization_id == view.player_civilization_id &&
        (!source_colony ||
         colony.population_millions > source_colony->population_millions))
      source_colony = &colony;
  require(source_colony,
          "Authored player has no canonical population source.");
  const auto before_colony_credits = economy->credits;
  const auto before_colony_population = source_colony->population_millions;
  const auto before_queued_denial_orders =
      world.shipyards.front().pending_build_count();
  economy->credits = 0.;
  const auto queued_denied = controller.start(
      frame, generation, view.shipyard_revision, "colony_ship");
  require(!queued_denied.accepted &&
              queued_denied.message.find("required to authorize") !=
                  std::string::npos &&
              near(source_colony->population_millions,
                   before_colony_population) &&
              world.shipyards.front().pending_build_count() ==
                  before_queued_denial_orders,
          "A queued-design funding loss was hidden as stale or mutated state.");
  economy->credits = before_colony_credits;
  const auto colony_ship = controller.start(frame, generation,
                                            view.shipyard_revision,
                                            "colony_ship");
  require(colony_ship.accepted &&
              near(economy->credits, before_colony_credits - 180.) &&
              near(source_colony->population_millions,
                   before_colony_population - 250.) &&
              world.fleets.empty(),
          "Canonical queued authorization did not reserve credits and population exactly.");

  view = controller.build(frame, generation);
  require(view.orders.size() == 2 && !view.orders.back().active &&
              view.orders.back().can_cancel &&
              near(view.orders.back().refund_credits, 180.) &&
              near(view.orders.back().reserved_population_millions, 250.) &&
              view.orders.back().reserved_population_species_id ==
                  source_colony->population_species_id &&
              view.orders.back().reserved_population_source_colony_id ==
                  source_colony->id,
          "Queued order or cancellation quote was projected incorrectly.");
  const auto queued_id = view.orders.back().order_id;
  const auto cancel_queued = controller.cancel(
      frame, generation, view.shipyard_revision, queued_id);
  require(cancel_queued.accepted && near(cancel_queued.refunded_credits, 180.) &&
              near(economy->credits, before_colony_credits) &&
              near(source_colony->population_millions,
                   before_colony_population) &&
              world.fleets.empty(),
          "Canonical queued cancellation did not return its authorization and population.");

  view = controller.build(frame, generation);
  const auto active_id = view.orders.front().order_id;
  (void)frame.advance(.25);
  view = controller.build(frame, generation);
  require(view.orders.size() == 1 && view.orders.front().active &&
              view.orders.front().industry_progress > 0. &&
              view.orders.front().industry_remaining <
                  get_ship_design("warp_scout").industry_cost &&
              world.fleets.empty(),
          "CampaignFrame did not advance the canonical active build safely.");
  const auto expected_refund = view.orders.front().refund_credits;
  const auto credits_before_cancel = economy->credits;
  const auto cancel_active = controller.cancel(
      frame, generation, view.shipyard_revision, active_id);
  require(cancel_active.accepted &&
              near(cancel_active.refunded_credits, expected_refund) &&
              near(economy->credits,
                   credits_before_cancel + expected_refund) &&
              world.fleets.empty(),
          "Canonical partial-progress cancellation accounting diverged.");

  const auto owner_credits = economy->credits;
  const auto owner_orders = controller.build(frame, generation).orders.size();
  bool wrong_thread_rejected{};
  std::thread foreign([&] {
    try {
      (void)controller.build(frame, generation);
    } catch (const std::logic_error &) {
      wrong_thread_rejected = true;
    }
  });
  foreign.join();
  require(wrong_thread_rejected && near(economy->credits, owner_credits) &&
              controller.build(frame, generation).orders.size() == owner_orders,
          "Wrong-thread shipyard access was not rejected before mutation.");

  (void)controller.build(frame, generation + 1);
  const auto old_window = controller.start(frame, generation, 1, "warp_scout");
  require(!old_window.accepted, "A replaced campaign window accepted an action.");
  bool rollback_rejected{};
  try {
    (void)controller.build(frame, generation);
  } catch (const std::invalid_argument &) {
    rollback_rejected = true;
  }
  require(rollback_rejected,
          "A stale campaign generation rebound the shipyard controller.");
}

void affordable_balance_change_start(const fs::path &research_root,
                                     const fs::path &catalog) {
  auto frame = enabled_frame(research_root, catalog);
  auto &world = frame.runtime().world().campaign();
  NativeShipyardController controller;
  constexpr std::uint64_t generation = 31;
  const auto view = controller.build(frame, generation);
  auto &economy = *std::ranges::find(
      world.economies, view.player_civilization_id,
      &CivilizationEconomy::civilization_id);
  const auto before_orders = world.shipyards.front().pending_build_count();
  economy.credits -= 1.;
  const auto before_start = economy.credits;
  const auto started = controller.start(
      frame, generation, view.shipyard_revision, "warp_scout");
  require(started.accepted && near(economy.credits, before_start - 70.) &&
              world.shipyards.front().pending_build_count() == before_orders + 1,
          "An affordable balance change invalidated or misapplied a canonical start.");
}
} // namespace

int main(int argc, char **argv) try {
  if (argc != 5)
    throw std::invalid_argument(
        "Usage: native_shipyard_controller_tests <research-root> <catalog> <Player17-fixture> <scratch>");
  const auto research = fs::absolute(argv[1]);
  const auto catalog = fs::absolute(argv[2]);
  locked_source_and_fresh(research, catalog, fs::absolute(argv[3]),
                          fs::absolute(argv[4]));
  canonical_orders(research, catalog);
  affordable_balance_change_start(research, catalog);
  std::cout << "Native shipyard source/fresh locks, canonical requirements, queue, progress, cancellation and generation tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native shipyard controller test failed: " << error.what()
            << '\n';
  return 1;
}
