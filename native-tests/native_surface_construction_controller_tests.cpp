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
  paused_reload_test(research_root, catalog);
  std::cout << "native surface controller: 3/3 bounded cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native surface controller failed: " << error.what() << '\n';
  return 1;
}
