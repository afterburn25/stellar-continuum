#include "native_ship_design_controller.hpp"

#include <stellar/core/adaptive_research_campaign.hpp>
#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/shipyard_state.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar::core;
using namespace stellar::native_ship_design;

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] FreshCampaignState fresh_world(const fs::path &catalog) {
  return seed_persistable_fresh_campaign(
      108500, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
}

// A fresh campaign where the player has researched the ship hull
// capabilities and completed the orbital shipyard — every catalog hull is
// unlocked for authoring.
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
  require(construction != world.construction.end(),
          "Fresh player construction state is missing.");
  construction->completed_project_ids.push_back("orbital_shipyard");
  return CampaignFrame(IntegratedAdaptiveCampaignRuntime::restore_research(
                           std::move(research_runtime), std::move(world),
                           snapshot, {}, 0.),
                       StrategicClock{}, CampaignFramePolicy::Player);
}

void view_projects_catalog(CampaignFrame &frame) {
  NativeShipDesignController controller;
  const auto view = controller.build(frame, 1);
  require(view.campaign_generation == 1 && view.design_revision == 1,
          "Design view did not bind to the campaign generation.");
  require(view.hulls.size() == ship_hull_catalog().size() &&
              view.components.size() == ship_component_catalog().size(),
          "Design view did not project the full hull/component catalogs.");
  require(view.designs.empty(),
          "Fresh campaign reported authored designs.");
  const auto survey = std::ranges::find(view.hulls, "survey_frame",
                                        &NativeHullOption::id);
  require(survey != view.hulls.end() && !survey->lock_reason &&
              survey->slot_count ==
                  static_cast<int>(get_ship_hull("survey_frame").slots.size()),
          "Unlocked survey hull was not projected as usable.");
  const auto scoop = std::ranges::find(view.components, "fuel_scoop",
                                       &NativeComponentOption::id);
  require(scoop != view.components.end() && !scoop->lock_reason &&
              scoop->slot == ShipComponentSlot::Utility,
          "Baseline component was not projected as usable.");
}

void compose_previews_resolution(CampaignFrame &frame) {
  NativeShipDesignController controller;
  (void)controller.build(frame, 1);
  const auto composition =
      controller.compose(frame, "survey_frame",
                         std::vector<std::string>{"ion_drive",
                                                  "survey_warp_core",
                                                  "repair_bay"});
  require(composition.can_commit && composition.issues.empty(),
          "Valid draft was rejected by the composition preview.");
  const auto hull = get_ship_hull("survey_frame");
  require(composition.industry_cost > hull.industry_cost &&
              composition.max_hull > hull.max_hull &&
              composition.crew > hull.crew_complement_individuals,
          "Composition did not resolve component contributions.");
  const auto missing_warp = controller.compose(
      frame, "survey_frame", std::vector<std::string>{"ion_drive"});
  require(!missing_warp.can_commit && !missing_warp.issues.empty(),
          "Draft missing a required warp slot was committable.");
  const auto bad_hull = controller.compose(
      frame, "nonexistent_hull", std::vector<std::string>{});
  require(!bad_hull.can_commit, "Unknown hull draft was committable.");
}

void commit_retire_and_revision(CampaignFrame &frame) {
  NativeShipDesignController controller;
  auto view = controller.build(frame, 7);
  const auto outcome = controller.commit(
      frame, 7, view.design_revision, "Test Surveyor", "survey_frame",
      {"ion_drive", "survey_warp_core", "fuel_scoop"});
  require(outcome.accepted && outcome.design_id,
          "Authoritative commit rejected a valid authored design.");
  auto &campaign = frame.runtime().world().campaign();
  require(campaign.authored_ship_designs.size() == 1 &&
              campaign.authored_ship_designs.front().id ==
                  *outcome.design_id &&
              campaign.authored_ship_designs.front()
                      .owner_civilization_id ==
                  campaign.player_civilization_id,
          "Committed design did not reach authoritative campaign state.");

  // The UI refreshes after each command; a commit replayed against the
  // pre-refresh view revision is then rejected before validation.
  (void)controller.build(frame, 7);
  const auto stale = controller.commit(
      frame, 7, view.design_revision, "Stale", "survey_frame",
      {"ion_drive", "survey_warp_core"});
  require(!stale.accepted &&
              campaign.authored_ship_designs.size() == 1,
          "Stale bureau revision committed a design.");
  // A commit against a stale campaign generation is rejected outright.
  const auto wrong_generation = controller.commit(
      frame, 99, view.design_revision, "Wrong", "survey_frame",
      {"ion_drive", "survey_warp_core"});
  require(!wrong_generation.accepted,
          "Wrong campaign generation committed a design.");

  view = controller.build(frame, 7);
  require(view.designs.size() == 1 && view.designs.front().valid &&
              view.designs.front().can_retire &&
              view.designs.front().name == "Test Surveyor",
          "Committed design did not project as a valid owned row.");

  const auto renamed = controller.rename(
      frame, 7, view.design_revision, *outcome.design_id,
      "Renamed Surveyor", "Updated description.");
  require(renamed.accepted &&
              campaign.authored_ship_designs.front().name ==
                  "Renamed Surveyor",
          "Rename command did not update authoritative metadata.");

  // An in-flight shipyard order must block retirement.
  ShipyardState yard;
  yard.civilization_id = campaign.player_civilization_id;
  ShipBuildOrderState order;
  order.design_id = *outcome.design_id;
  yard.queued_builds.push_back(order);
  campaign.shipyards.push_back(yard);
  view = controller.build(frame, 7);
  require(!view.designs.front().can_retire &&
              view.designs.front().retire_blocker,
          "Referenced design did not project a retirement blocker.");
  campaign.shipyards.clear();

  view = controller.build(frame, 7);
  const auto retired = controller.retire(frame, 7, view.design_revision,
                                         *outcome.design_id);
  require(retired.accepted && campaign.authored_ship_designs.empty(),
          "Retirement did not remove the authored design.");
  const auto stale_retire = controller.retire(frame, 7, view.design_revision,
                                              "ghost");
  require(!stale_retire.accepted,
          "Retirement succeeded against a stale revision.");
}

void locked_campaign_rejects(CampaignFrame &frame) {
  NativeShipDesignController controller;
  const auto view = controller.build(frame, 3);
  require(!view.hulls.empty() &&
              std::ranges::all_of(view.hulls, [](const auto &hull) {
                return hull.lock_reason.has_value();
              }),
          "Unresearched hulls were not projected as locked.");
  const auto composition =
      controller.compose(frame, "survey_frame",
                         std::vector<std::string>{"ion_drive",
                                                  "survey_warp_core"});
  require(!composition.can_commit,
          "Composition preview ignored missing hull capabilities.");
  const auto outcome = controller.commit(
      frame, 3, view.design_revision, "Locked", "survey_frame",
      {"ion_drive", "survey_warp_core"});
  require(!outcome.accepted &&
              frame.runtime().world().campaign()
                  .authored_ship_designs.empty(),
          "Unresearched campaign committed an authored design.");
}

} // namespace

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument(
        "Usage: native_ship_design_controller_tests <research-root> <catalog>");
  auto enabled = enabled_frame(fs::absolute(argv[1]), fs::absolute(argv[2]));
  auto preview = enabled_frame(fs::absolute(argv[1]), fs::absolute(argv[2]));
  auto commands = enabled_frame(fs::absolute(argv[1]), fs::absolute(argv[2]));
  auto locked_fresh = CampaignFrame(
      IntegratedAdaptiveCampaignRuntime::create_fresh(
          load_adaptive_research_strategic_runtime(fs::absolute(argv[1])),
          fresh_world(fs::absolute(argv[2]))),
      StrategicClock{}, CampaignFramePolicy::Player);
  view_projects_catalog(enabled);
  compose_previews_resolution(preview);
  commit_retire_and_revision(commands);
  locked_campaign_rejects(locked_fresh);
  std::cout << "Native ship design controller tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native ship design controller test failed: " << error.what()
            << '\n';
  return 1;
}
