#include "native_surface_construction_controller.hpp"

#include "native_system_view.hpp"

#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/persistable_fresh_campaign.hpp>
#include <stellar/core/player_campaign_persistence.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <locale>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace stellar::core;
using namespace stellar::native_colony;
using namespace stellar::native_system;
namespace fs = std::filesystem;

namespace {
void require(const bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

CampaignFrame make_frame(const fs::path &research_root,
                         const fs::path &catalog) {
  auto world = seed_persistable_fresh_campaign(
      130500, load_nearby_catalog(catalog),
      {"2044-05-06T07:08:09Z", 500, 6, 1, "terran_baseline"});
  StrategicClock clock;
  clock.set_speed(StrategicSpeed::Demo);
  return CampaignFrame(
      IntegratedAdaptiveCampaignRuntime::create_fresh(
          load_adaptive_research_strategic_runtime(research_root),
          std::move(world)),
      std::move(clock), CampaignFramePolicy::Developer);
}

CivilizationEconomy &economy(CampaignFrame &frame) {
  auto &world = frame.runtime().world().campaign();
  return *std::ranges::find(world.economies, world.player_civilization_id,
                            &CivilizationEconomy::civilization_id);
}

Colony &colony(CampaignFrame &frame, const int colony_id) {
  auto &colonies = frame.runtime().world().campaign().colonies;
  return *std::ranges::find(colonies, colony_id, &Colony::id);
}

ConstructionState &construction(CampaignFrame &frame) {
  auto &states = frame.runtime().world().campaign().construction;
  const auto civilization_id =
      frame.runtime().world().campaign().player_civilization_id;
  return *std::ranges::find(states, civilization_id,
                            &ConstructionState::civilization_id);
}

std::string surface_signature(const Colony &value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  for (const auto &site : value.surface_buildings)
    out << site.id << ':' << site.type_id << ':' << site.x << ':' << site.z
        << ':' << site.rotation_degrees << ':' << site.industry_progress << ':'
        << site.is_complete << ':' << site.is_enabled << ':'
        << site.pending_upgrade_type_id.value_or("") << ':'
        << site.upgrade_days_remaining << ':' << site.operating_priority << ':'
        << site.condition << ':' << site.stored_power_days << ';';
  return out.str();
}

struct SelectedColony {
  NativeSystemSnapshot system;
  NativeColonyView colony;
};

SelectedColony select_home(CampaignFrame &frame, const std::uint64_t generation,
                           NativeSystemViewController &systems,
                           NativeColonyController &colonies) {
  auto &world = frame.runtime().world().campaign();
  const auto player = world.player_civilization_id;
  const auto owned = std::ranges::find(world.colonies, player,
                                        &Colony::civilization_id);
  require(owned != world.colonies.end() && owned->planetary_body_id,
          "fresh surface test lacks an owned body colony");
  world.knowledge.mark_system_fully_surveyed(player, owned->system_id);
  auto system = systems.build(frame, generation, owned->system_id);
  require(system.snapshot.has_value(), "owned known system is unavailable");
  auto view = colonies.build(frame, generation, *system.snapshot,
                             *owned->planetary_body_id);
  require(view.view.has_value(), "owned colony telemetry is unavailable");
  return {*system.snapshot, *view.view};
}

NativeSurfacePlacementQuote accepted_quote(
    NativeSurfaceConstructionController &controller, CampaignFrame &frame,
    const std::uint64_t generation, const NativeColonyView &view,
    const std::string &type_id, const float start = 90.f) {
  for (int row = 0; row < 8; ++row)
    for (int column = 0; column < 8; ++column) {
      auto quote = controller.preview_placement(
          frame, generation, view, type_id, start + column * 45.f,
          start + row * 45.f, 375.f);
      if (quote.accepted) return quote;
    }
  throw std::runtime_error("canonical assessment found no bounded placement");
}

void quote_and_progress_tests(const fs::path &research_root,
                              const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog);
  NativeSystemViewController systems;
  NativeColonyController colonies;
  auto selected = select_home(frame, 1, systems, colonies);
  require(!selected.colony.available_buildings.empty(),
          "owned colony exposes no canonical building option");
  const auto type = selected.colony.available_buildings.front().type_id;
  NativeSurfaceConstructionController controller;
  const auto sites_before = surface_signature(colony(frame, selected.colony.colony_id));
  const auto credits_before = economy(frame).credits;
  auto quote = accepted_quote(controller, frame, 1, selected.colony, type);
  require(surface_signature(colony(frame, selected.colony.colony_id)) == sites_before &&
              economy(frame).credits == credits_before,
          "placement preview mutated campaign state");
  const auto expected_preview =
      "Authorize construction for " + quote.formatted_authorization +
      ". Materials are consumed as work progresses.";
  require(quote.message == expected_preview &&
              quote.message.find("placed and authorized") == std::string::npos,
          "accepted placement preview used committed-construction wording");
  const auto superseded = quote;
  const auto denied_preview = controller.preview_placement(
      frame, 1, selected.colony, "not-a-building", 0.f, 0.f, 0.f);
  require(!denied_preview.accepted &&
              !controller.confirm_placement(frame, 1, superseded).accepted,
          "a newer denied motion preview left an older quote active");
  quote = accepted_quote(controller, frame, 1, selected.colony, type);
  const auto detached = quote;
  require(controller.cancel_quote(1, quote.quote_revision),
          "accepted UI quote could not be cancelled");
  require(!controller.confirm_placement(frame, 1, detached).accepted &&
              surface_signature(colony(frame, selected.colony.colony_id)) == sites_before &&
              economy(frame).credits == credits_before,
          "cancelled UI quote mutated simulation state");

  quote = accepted_quote(controller, frame, 1, selected.colony, type);
  const auto charge = quote.authorization_budget_units;
  const auto placed = controller.confirm_placement(frame, 1, quote);
  require(placed.accepted &&
              std::abs(economy(frame).credits - (credits_before - charge)) < 1e-9,
          "canonical placement did not deduct the quoted authorization");
  require(placed.message == quote.building_name + " placed and authorized for " +
                                 quote.formatted_authorization +
                                 ". Construction uses available materials.",
          "confirmed placement did not retain the canonical commit message");
  const auto site_id =
      colony(frame, selected.colony.colony_id).surface_buildings.back().id;
  const auto progress_before =
      colony(frame, selected.colony.colony_id).surface_buildings.back()
          .industry_progress;
  require(!colony(frame, selected.colony.colony_id).surface_buildings.back().is_complete &&
              site_id == quote.prepared_building_id,
          "canonical placement did not create the quoted incomplete site");
  (void)frame.advance(1.);
  const auto progressed = std::ranges::find(
      colony(frame, selected.colony.colony_id).surface_buildings, site_id,
      &SurfaceBuilding::id);
  require(progressed != colony(frame, selected.colony.colony_id).surface_buildings.end() &&
              progressed->industry_progress > progress_before,
          "surface site did not progress through CampaignFrame");

  selected = select_home(frame, 1, systems, colonies);
  const auto removal = controller.preview_removal(
      frame, 1, selected.colony, progressed->id);
  require(removal.accepted && removal.cancellation &&
              removal.refund_budget_units > 0.,
          "incomplete site did not produce a cancellation/refund quote");
  const auto credits_before_refund = economy(frame).credits;
  require(controller.confirm_removal(frame, 1, removal).accepted &&
              std::abs(economy(frame).credits -
                           (credits_before_refund + removal.refund_budget_units)) <
                  1e-9,
          "incomplete cancellation did not apply its canonical refund");

  selected = select_home(frame, 1, systems, colonies);
  auto demolition_placement =
      accepted_quote(controller, frame, 1, selected.colony, type, -320.f);
  require(controller.confirm_placement(frame, 1, demolition_placement).accepted,
          "demolition fixture placement failed");
  auto &completed = *std::ranges::find(
      colony(frame, selected.colony.colony_id).surface_buildings,
      demolition_placement.prepared_building_id, &SurfaceBuilding::id);
  completed.is_complete = true;
  selected = select_home(frame, 1, systems, colonies);
  const auto demolition = controller.preview_removal(
      frame, 1, selected.colony, completed.id);
  const auto before_demolition_credits = economy(frame).credits;
  require(demolition.accepted && !demolition.cancellation &&
              demolition.refund_budget_units == 0. &&
              controller.confirm_removal(frame, 1, demolition).accepted &&
              economy(frame).credits == before_demolition_credits,
          "completed demolition wrote a refund");
}

void changed_world_tests(const fs::path &research_root,
                         const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog);
  NativeSystemViewController systems;
  NativeColonyController colonies;
  auto selected = select_home(frame, 4, systems, colonies);
  NativeSurfaceConstructionController controller;
  const auto type = selected.colony.available_buildings.front().type_id;
  auto quote = accepted_quote(controller, frame, 4, selected.colony, type);
  economy(frame).credits = 0.;
  const auto before = surface_signature(colony(frame, selected.colony.colony_id));
  const auto denied = controller.confirm_placement(frame, 4, quote);
  require(!denied.accepted && economy(frame).credits == 0. &&
              surface_signature(colony(frame, selected.colony.colony_id)) == before,
          "current funding denial mutated surface state");

  economy(frame).credits = 500.;
  quote = accepted_quote(controller, frame, 4, selected.colony, type);
  colony(frame, selected.colony.colony_id).surface_buildings.push_back(
      {.id = quote.prepared_building_id,
       .type_id = type,
       .x = quote.x,
       .z = quote.z,
       .rotation_degrees = quote.normalized_rotation_degrees});
  const auto count = colony(frame, selected.colony.colony_id).surface_buildings.size();
  require(!controller.confirm_placement(frame, 4, quote).accepted &&
              colony(frame, selected.colony.colony_id).surface_buildings.size() == count,
          "changed overlap/identifier terms accepted a stale placement");

  selected = select_home(frame, 4, systems, colonies);
  const auto removal = controller.preview_removal(
      frame, 4, selected.colony,
      colony(frame, selected.colony.colony_id).surface_buildings.back().id);
  colony(frame, selected.colony.colony_id).surface_buildings.back().is_complete = true;
  const auto credits = economy(frame).credits;
  require(!controller.confirm_removal(frame, 4, removal).accepted &&
              economy(frame).credits == credits,
          "changed cancellation/demolition terms accepted a stale removal");

  selected = select_home(frame, 4, systems, colonies);
  quote = accepted_quote(controller, frame, 4, selected.colony, type, -250.f);
  frame.runtime().world().campaign().knowledge = CivilizationKnowledgeState{};
  require(!controller.confirm_placement(frame, 4, quote).accepted,
          "revoked observer knowledge accepted a surface command");
  frame.runtime().world().campaign().knowledge.mark_system_fully_surveyed(
      selected.colony.player_civilization_id, selected.colony.system_id);
  auto foreign = selected.colony;
  ++foreign.player_civilization_id;
  const auto foreign_quote = controller.preview_placement(
      frame, 4, foreign, type, 20.f, 20.f, 0.f);
  require(!foreign_quote.accepted,
          "foreign colony view produced an accepted surface quote");
}

void management_quote_tests(const fs::path &research_root,
                            const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog);
  NativeSystemViewController systems;
  NativeColonyController colonies;
  const auto selected = select_home(frame, 12, systems, colonies);
  NativeSurfaceConstructionController controller;
  auto &owned = colony(frame, selected.colony.colony_id);
  auto &funds = economy(frame);
  funds.credits = 10000.;
  funds.industry = 10000.;
  constexpr int building_id = 910001;
  owned.surface_buildings.push_back(
      {.id = building_id,
       .type_id = "science_lab",
       .is_complete = true,
       .is_enabled = true,
       .condition = .5});
  auto site = [&]() -> SurfaceBuilding & {
    return *std::ranges::find(owned.surface_buildings, building_id,
                              &SurfaceBuilding::id);
  };

  const auto before_preview = surface_signature(owned);
  const auto credits_before_preview = funds.credits;
  const auto industry_before_preview = funds.industry;
  const auto cancelled = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      building_id);
  require(cancelled.accepted && cancelled.industry_cost > 0. &&
              !cancelled.formatted_authorization.empty() &&
              surface_signature(owned) == before_preview &&
              funds.credits == credits_before_preview &&
              funds.industry == industry_before_preview &&
              controller.cancel_quote(12, cancelled.quote_revision),
          "management preview or cancellation changed campaign state");
  require(!controller.confirm_management(frame, 12, cancelled).accepted &&
              surface_signature(owned) == before_preview,
          "cancelled management quote remained confirmable");

  const auto invalid = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      -1);
  require(!invalid.accepted, "missing management target produced a quote");
  auto foreign = selected.colony;
  ++foreign.player_civilization_id;
  require(!controller
               .preview_management(frame, 12, foreign,
                                   NativeSurfaceManagementAction::RepairBuilding,
                                   building_id)
               .accepted,
          "foreign colony view produced a management quote");

  auto repair = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      building_id);
  require(repair.accepted && repair.building_name == "Science lab" &&
              repair.industry_cost == surface_repair_industry_cost(site()) &&
              !controller.confirm_management(frame, 13, repair).accepted,
          "stale generation management quote was accepted");
  repair = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      building_id);
  const auto repaired_cost = repair.industry_cost;
  const auto industry_before_repair = funds.industry;
  require(controller.confirm_management(frame, 12, repair).accepted &&
              site().condition == 1. &&
              std::abs(funds.industry - (industry_before_repair - repaired_cost)) <
                  1e-9 &&
              !controller.confirm_management(frame, 12, repair).accepted,
          "repair management quote did not charge exactly once");

  auto priority = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::SetPriority,
      building_id, true);
  auto tampered = priority;
  tampered.description += " changed";
  require(priority.accepted &&
              !controller.confirm_management(frame, 12, tampered).accepted &&
              site().operating_priority == 0,
          "tampered management quote changed the building");
  priority = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::SetPriority,
      building_id, true);
  require(controller.confirm_management(frame, 12, priority).accepted &&
              site().operating_priority == 1,
          "priority management quote did not use the canonical order");

  auto enabled = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::SetEnabled,
      building_id, false);
  site().condition = .8;
  require(enabled.accepted &&
              !controller.confirm_management(frame, 12, enabled).accepted &&
              site().is_enabled,
          "changed building state accepted a stale management quote");
  enabled = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::SetEnabled,
      building_id, false);
  require(controller.confirm_management(frame, 12, enabled).accepted &&
              !site().is_enabled,
          "enabled management quote did not use the canonical order");

  auto unaffordable = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      building_id);
  funds.industry = 0.;
  require(unaffordable.accepted &&
              !controller.confirm_management(frame, 12, unaffordable).accepted &&
              site().condition == .8,
          "changed affordability accepted a repair quote");
  funds.industry = 10000.;
  repair = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::RepairBuilding,
      building_id);
  require(controller.confirm_management(frame, 12, repair).accepted &&
              site().condition == 1.,
          "repair was not recoverable after an affordability refresh");

  const auto upgrade = controller.preview_management(
      frame, 12, selected.colony,
      NativeSurfaceManagementAction::UpgradeBuilding, building_id);
  const auto credits_before_upgrade = funds.credits;
  const auto industry_before_upgrade = funds.industry;
  require(upgrade.accepted && upgrade.authorization_budget_units > 0. &&
              upgrade.industry_cost > 0. &&
              controller.confirm_management(frame, 12, upgrade).accepted &&
              site().pending_upgrade_type_id.has_value() &&
              std::abs(funds.credits -
                       (credits_before_upgrade -
                        upgrade.authorization_budget_units)) < 1e-9 &&
              std::abs(funds.industry -
                       (industry_before_upgrade - upgrade.industry_cost)) <
                  1e-9,
          "building upgrade did not preserve its quoted canonical costs");

  owned.surface_hub_level = 1;
  owned.surface_hub_upgrade_days_remaining = 0.;
  auto &projects = construction(frame).completed_project_ids;
  std::erase(projects, "industrial_automation");
  projects.push_back("industrial_automation");
  auto hub = controller.preview_management(
      frame, 12, selected.colony, NativeSurfaceManagementAction::UpgradeHub);
  std::erase(projects, "industrial_automation");
  require(hub.accepted &&
              !controller.confirm_management(frame, 12, hub).accepted &&
              owned.surface_hub_upgrade_days_remaining == 0.,
          "changed hub prerequisite accepted a stale quote");
  projects.push_back("industrial_automation");
  hub = controller.preview_management(frame, 12, selected.colony,
                                      NativeSurfaceManagementAction::UpgradeHub);
  const auto credits_before_hub = funds.credits;
  const auto industry_before_hub = funds.industry;
  require(hub.accepted && hub.authorization_budget_units > 0. &&
              hub.industry_cost > 0. &&
              controller.confirm_management(frame, 12, hub).accepted &&
              owned.surface_hub_upgrade_days_remaining > 0. &&
              std::abs(funds.credits -
                       (credits_before_hub - hub.authorization_budget_units)) <
                  1e-9 &&
              std::abs(funds.industry -
                       (industry_before_hub - hub.industry_cost)) < 1e-9,
          "hub upgrade did not preserve its quoted canonical costs");
}

void body_membership_tests(const fs::path &research_root,
                           const fs::path &catalog) {
  {
    auto frame = make_frame(research_root, catalog);
    NativeSystemViewController systems;
    NativeColonyController colonies;
    const auto selected = select_home(frame, 15, systems, colonies);
    NativeSurfaceConstructionController controller;
    auto &owned = colony(frame, selected.colony.colony_id);
    owned.surface_hub_level = 1;
    owned.surface_hub_upgrade_days_remaining = 0.;
    economy(frame).credits = 10000.;
    economy(frame).industry = 10000.;
    auto &projects = construction(frame).completed_project_ids;
    std::erase(projects, "industrial_automation");
    projects.push_back("industrial_automation");
    const auto quote = controller.preview_management(
        frame, 15, selected.colony, NativeSurfaceManagementAction::UpgradeHub);
    require(quote.accepted, "deleted-body fixture could not quote a hub upgrade");
    auto &bodies = frame.runtime().world().campaign().bodies;
    std::erase_if(bodies, [&](const PlanetaryBody &body) {
      return body.id == selected.colony.body_id;
    });
    require(!controller.confirm_management(frame, 15, quote).accepted &&
                !controller
                     .preview_management(frame, 15, selected.colony,
                                         NativeSurfaceManagementAction::UpgradeHub)
                     .accepted,
            "a deleted planetary body retained surface-management authority");
  }

  {
    auto frame = make_frame(research_root, catalog);
    NativeSystemViewController systems;
    NativeColonyController colonies;
    const auto selected = select_home(frame, 16, systems, colonies);
    NativeSurfaceConstructionController controller;
    auto &owned = colony(frame, selected.colony.colony_id);
    owned.surface_hub_level = 1;
    owned.surface_hub_upgrade_days_remaining = 0.;
    economy(frame).credits = 10000.;
    economy(frame).industry = 10000.;
    auto &projects = construction(frame).completed_project_ids;
    std::erase(projects, "industrial_automation");
    projects.push_back("industrial_automation");
    const auto quote = controller.preview_management(
        frame, 16, selected.colony, NativeSurfaceManagementAction::UpgradeHub);
    require(quote.accepted,
            "moved-body fixture could not quote a hub upgrade");
    auto &bodies = frame.runtime().world().campaign().bodies;
    const auto body = std::ranges::find(bodies, selected.colony.body_id,
                                        &PlanetaryBody::id);
    require(body != bodies.end(), "moved-body fixture lost the selected body");
    ++body->system_id;
    require(!controller.confirm_management(frame, 16, quote).accepted &&
                !controller
                     .preview_management(frame, 16, selected.colony,
                                         NativeSurfaceManagementAction::UpgradeHub)
                     .accepted,
            "a body moved to another system retained surface-management authority");
  }
}

void paused_reload_test(const fs::path &research_root,
                        const fs::path &catalog) {
  auto frame = make_frame(research_root, catalog);
  NativeSystemViewController systems;
  NativeColonyController colonies;
  auto selected = select_home(frame, 8, systems, colonies);
  NativeSurfaceConstructionController controller;
  const auto quote = accepted_quote(controller, frame, 8, selected.colony,
                                    selected.colony.available_buildings.front().type_id);
  frame.clock().set_speed(StrategicSpeed::Paused);
  const auto payload = capture_player_campaign_v17(
      frame.runtime(), {frame.clock().simulation_days(), "test", "2044-05-06T07:08:09Z"});
  auto restored = restore_player_campaign_v17(
      load_adaptive_research_strategic_runtime(research_root), payload);
  StrategicClock clock;
  clock.restore(restored.simulation_days());
  clock.set_speed(StrategicSpeed::Paused);
  CampaignFrame reloaded(std::move(restored).activate(), std::move(clock),
                         CampaignFramePolicy::Player);
  NativeSystemViewController reloaded_systems;
  NativeColonyController reloaded_colonies;
  auto reselected = select_home(reloaded, 9, reloaded_systems, reloaded_colonies);
  const auto sites_before =
      surface_signature(colony(reloaded, reselected.colony.colony_id));
  require(!controller.confirm_placement(reloaded, 9, quote).accepted,
          "pre-load generation quote survived campaign replacement");
  const auto step = reloaded.advance(1.);
  require(step.completed_substeps.size() == 1 &&
              step.completed_substeps.front() == 0.,
          "paused player frame did not preserve its canonical zero update");
  require(surface_signature(colony(reloaded, reselected.colony.colony_id)) ==
              sites_before,
          "paused reloaded campaign changed surface construction state");
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 3,
          "Usage: native_surface_controller_tests <research-root> <catalog>");
  const auto research_root = fs::absolute(argv[1]);
  const auto catalog = fs::absolute(argv[2]);
  quote_and_progress_tests(research_root, catalog);
  changed_world_tests(research_root, catalog);
  management_quote_tests(research_root, catalog);
  body_membership_tests(research_root, catalog);
  paused_reload_test(research_root, catalog);
  std::cout << "native surface controller: 5/5 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native surface controller failed: " << error.what() << '\n';
  return 1;
}
