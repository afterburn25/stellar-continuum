#include "native_colony_workspace.hpp"
#include "native_surface_workspace.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

Point center(UiRect value) {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

NativeColonyView colony() {
  NativeColonyView value;
  value.campaign_generation = 5;
  value.revision = 7;
  value.player_civilization_id = 1;
  value.system_id = 2;
  value.body_id = 3;
  value.colony_id = 4;
  value.colony_name = "Earth";
  value.body_display_name = "Earth";
  value.solid_surface = true;
  value.building_capacity = 16;
  value.available_buildings.push_back(
      {.type_id = "fabricator",
       .name = "Surface Fabricator",
       .description = "Produces construction material for surface projects.",
       .industry_cost = 120.,
       .authorization_budget_units = 35.,
       .formatted_authorization = "$35 UED",
       .footprint_radius = 18.f,
       .power_demand = 2.,
       .workforce_required_millions = .04});
  value.construction_sites.push_back(
      {.building_id = 9,
       .type_id = "fabricator",
       .name = "Surface Fabricator",
       .x = 90.f,
       .z = 70.f,
       .rotation_degrees = 90.f,
       .industry_progress = 30.,
       .industry_cost = 120.,
       .progress_fraction = .25,
       .construction_stage = "Foundation",
       .remaining_construction_materials = 90.});
  return value;
}

void responsive_layout() {
  for (const auto [width, height] : {std::pair{640, 360}, {1280, 720}, {1920, 1080},
                                     {2560, 1440}, {3840, 2160}}) {
    const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
    require(layout.surface.contains(center(layout.back)), "back escaped surface");
    require(layout.surface.contains(center(layout.palette)), "palette escaped surface");
    require(layout.surface.contains(center(layout.terrain)), "terrain escaped surface");
    require(layout.surface.contains(center(layout.inspector)), "inspector escaped surface");
    require(layout.confirmation.contains(center(layout.confirm)), "confirm escaped modal");
    require(layout.confirmation.contains(center(layout.cancel)), "cancel escaped modal");
    require(layout.palette.x + layout.palette.width <= layout.terrain.x,
            "palette overlaps terrain");
    require(layout.terrain.x + layout.terrain.width <= layout.inspector.x,
            "terrain overlaps inspector");
  }
}

void anchored_camera() {
  const UiRect terrain{250, 80, 700, 620};
  SurfaceViewport camera{17., -23., .6};
  const Point anchor{610, 420};
  const auto before = camera.screen_to_world(anchor, terrain);
  const auto zoomed = camera.zoomed_at(1.8f, anchor, terrain, .08, 4.);
  const auto after = zoomed.screen_to_world(anchor, terrain);
  require(std::abs(before.first - after.first) < 1e-9 &&
              std::abs(before.second - after.second) < 1e-9,
          "surface zoom moved the anchored coordinate");
}

void input_and_confirmation() {
  NativeSurfaceWorkspace workspace;
  workspace.open(colony(), 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  const Point palette_point{layout.palette_rows.x + 12.f,
                            layout.palette_rows.y + 20.f};
  require(workspace.handle({InputEventType::LeftPressed, palette_point}, 1280,
                           720).captured,
          "palette press was not captured");
  require(workspace.selected_type_id() == std::optional<std::string>{"fabricator"},
          "palette did not select the canonical type id");
  const auto terrain_point = center(layout.terrain);
  (void)workspace.handle({InputEventType::PointerMove, terrain_point}, 1280, 720);
  auto pending = workspace.take_preview_request();
  require(pending && pending->kind == SurfaceWorkspaceCommandKind::PreviewPlacement &&
              !pending->open_confirmation,
          "hover did not coalesce to a placement preview");
  require(!workspace.take_preview_request(), "preview request was not drained");
  NativeSurfacePlacementQuote quote;
  quote.campaign_generation = 5;
  quote.quote_revision = 11;
  quote.type_id = "fabricator";
  quote.building_name = "Surface Fabricator";
  quote.x = pending->x;
  quote.z = pending->z;
  quote.accepted = true;
  quote.formatted_authorization = "$35 UED";
  quote.industry_cost = 120.;
  quote.message = "Placement is available.";
  workspace.set_placement_quote(quote, false);
  const Point batched_point{terrain_point.x+1.f,terrain_point.y+1.f};
  (void)workspace.handle({InputEventType::PointerMove,batched_point,{1.f,1.f}},1280,720);
  (void)workspace.handle({InputEventType::LeftPressed, batched_point}, 1280, 720);
  const auto clicked = workspace.handle({InputEventType::LeftReleased, batched_point},
                                        1280, 720);
  require(clicked.kind == SurfaceWorkspaceCommandKind::PreviewPlacement &&
              clicked.open_confirmation,
          "click without drag did not request exact confirmation");
  quote.x=clicked.x;quote.z=clicked.z;workspace.set_placement_quote(quote, true);
  require(!workspace.take_preview_request(),
          "a queued hover preview survived opening exact confirmation");
  require(workspace.handle({InputEventType::LeftPressed, center(layout.confirm)},
                           1280, 720).kind ==
              SurfaceWorkspaceCommandKind::ConfirmPlacement,
          "accepted placement could not confirm");

  workspace.open(colony(), 1280, 720);
  const auto start = center(layout.terrain);
  (void)workspace.handle({InputEventType::LeftPressed, start}, 1280, 720);
  const auto moved = Point{start.x + 40.f, start.y + 20.f};
  (void)workspace.handle({InputEventType::PointerMove, moved,
                          {40.f, 20.f}}, 1280, 720);
  require(workspace.handle({InputEventType::LeftReleased, moved}, 1280, 720).kind ==
              SurfaceWorkspaceCommandKind::None,
          "surface drag became a placement or selection");
  workspace.close();workspace.open(colony(),1280,720);const auto reopened=workspace.viewport();
  (void)workspace.handle({InputEventType::PointerMove,{moved.x+5.f,moved.y+5.f},{5.f,5.f}},1280,720);
  require(workspace.viewport().center_x==reopened.center_x&&workspace.viewport().center_z==reopened.center_z,
          "reopening resumed a stale surface drag");
}

void site_removal_and_refresh() {
  NativeSurfaceWorkspace workspace;
  auto view = colony();
  workspace.open(view, 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  auto site = workspace.viewport().world_to_screen(90., 70., layout.terrain);
  site.x+=11.f;site.y+=11.f;
  (void)workspace.handle({InputEventType::LeftPressed, site}, 1280, 720);
  (void)workspace.handle({InputEventType::LeftReleased, site}, 1280, 720);
  require(workspace.selected_building_id() == std::optional<int>{9},
          "existing surface site was not selected");
  require(workspace.handle({InputEventType::LeftPressed, center(layout.remove)},
                           1280, 720).kind ==
              SurfaceWorkspaceCommandKind::PreviewRemoval,
          "site removal quote was not requested through input");
  NativeSurfaceRemovalQuote removal;
  removal.campaign_generation = 5;
  removal.quote_revision = 12;
  removal.building_id = 9;
  removal.building_name = "Surface Fabricator";
  removal.accepted = true;
  removal.cancellation = true;
  removal.formatted_refund = "$17.5 UED";
  removal.message = "Construction can be cancelled.";
  workspace.set_removal_quote(removal);
  require(workspace.handle({InputEventType::EscapePressed}, 1280, 720).kind ==
              SurfaceWorkspaceCommandKind::CancelQuote,
          "Escape did not cancel only the detached removal quote");
  view.revision++;
  view.construction_sites.clear();
  workspace.set_view(std::move(view));
  require(!workspace.selected_building_id() && !workspace.removal_quote(),
          "refresh retained a removed site or stale quote");
}

void building_actions() {
  auto view = colony();
  auto &site = view.construction_sites.front();
  site.complete = true;
  site.enabled = true;
  site.condition = .6;
  site.can_upgrade = true;
  site.upgrade_name = "Advanced Fabricator";
  site.upgrade_credit_budget_units = 42.;
  site.upgrade_industry_cost = 210.;
  site.can_afford_upgrade = true;
  site.repair_industry_cost = 24.;
  site.can_afford_repair = true;
  view.hub_name = "Command center";
  view.hub_upgrade_available = true;
  view.can_afford_hub_upgrade = true;
  NativeSurfaceWorkspace workspace;
  workspace.open(view, 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  auto point = workspace.viewport().world_to_screen(90., 70., layout.terrain);
  point.x += 11.f;
  point.y += 11.f;
  (void)workspace.handle({InputEventType::LeftPressed, point}, 1280, 720);
  (void)workspace.handle({InputEventType::LeftReleased, point}, 1280, 720);
  require(workspace.selected_building_id() == std::optional<int>{9},
          "action fixture site was not selected");
  const auto press = [&](UiRect rect) {
    return workspace.handle({InputEventType::LeftPressed, center(rect)}, 1280,
                            720);
  };
  require(press(layout.upgrade).kind ==
              SurfaceWorkspaceCommandKind::UpgradeBuilding,
          "UPGRADE did not emit an upgrade order");
  require(press(layout.repair).kind ==
              SurfaceWorkspaceCommandKind::RepairBuilding,
          "REPAIR did not emit a repair order");
  const auto shutdown = press(layout.toggle_operation);
  require(shutdown.kind == SurfaceWorkspaceCommandKind::SetBuildingEnabled &&
              shutdown.building_id == 9 && !shutdown.flag,
          "SHUT DOWN did not emit an enabled=false order");
  const auto prioritize = press(layout.priority);
  require(prioritize.kind == SurfaceWorkspaceCommandKind::SetBuildingPriority &&
              prioritize.building_id == 9 && prioritize.flag,
          "PRIORITIZE did not emit a prioritized=true order");
  site.can_afford_repair = false;
  view.revision++;
  workspace.set_view(view);
  require(press(layout.repair).kind == SurfaceWorkspaceCommandKind::None,
          "unaffordable repair emitted an order");
  site.upgrade_lock_reason = "Requires orbital dockyard.";
  workspace.set_view(view);
  require(press(layout.upgrade).kind == SurfaceWorkspaceCommandKind::None,
          "locked upgrade emitted an order");
  site.can_afford_repair = true;
  site.upgrade_lock_reason.clear();
  workspace.set_view(view);
  const auto empty = center(layout.terrain);
  (void)workspace.handle({InputEventType::LeftPressed, empty}, 1280, 720);
  (void)workspace.handle({InputEventType::LeftReleased, empty}, 1280, 720);
  require(!workspace.selected_building_id(), "terrain click kept a selection");
  require(press(layout.hub_upgrade).kind ==
              SurfaceWorkspaceCommandKind::UpgradeHub,
          "UPGRADE HUB did not emit a hub order");
  view.can_afford_hub_upgrade = false;
  workspace.set_view(view);
  require(press(layout.hub_upgrade).kind == SurfaceWorkspaceCommandKind::None,
          "unaffordable hub upgrade emitted an order");
}

void colony_surface_entry() {
  NativeColonyWorkspace colony_ui;
  colony_ui.open(colony());
  const auto layout = ColonyWorkspaceLayout::for_viewport(1280, 720);
  require(colony_ui.handle({InputEventType::LeftPressed,
                            center(layout.open_surface)}, 1280, 720).kind ==
              ColonyWorkspaceCommandKind::OpenSurface,
          "owned solid body did not expose Open Surface through input");
}

void palette_clipping() {
  auto view=colony();
  for(int index=0;index<12;++index){auto option=view.available_buildings.front();option.type_id="module-"+std::to_string(index);option.name="Module "+std::to_string(index);view.available_buildings.push_back(std::move(option));}
  NativeSurfaceWorkspace workspace;workspace.open(std::move(view),1280,720);
  const auto layout=SurfaceWorkspaceLayout::for_viewport(1280,720);
  (void)workspace.handle({InputEventType::Wheel,center(layout.palette),{},-3},1280,720);
  DrawList draw;workspace.render(draw,1280,720);
  for(const auto&item:draw.overlay)if(const auto*label=std::get_if<Text>(&item);label&&label->value.starts_with("Module ")){
    require(label->clip.has_value(),"scrolled palette text lacks a clip");
    const auto&clip=*label->clip;
    require(clip.x>=layout.palette_rows.x&&clip.y>=layout.palette_rows.y&&clip.x+clip.width<=layout.palette_rows.x+layout.palette_rows.width&&clip.y+clip.height<=layout.palette_rows.y+layout.palette_rows.height,"partial palette text escaped its rows viewport");
  }
}

void pending_ghost_cancellation() {
  NativeSurfaceWorkspace workspace;
  workspace.open(colony(), 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  const Point palette{layout.palette_rows.x + 10, layout.palette_rows.y + 10};
  const auto terrain = center(layout.terrain);
  auto select = [&] {
    (void)workspace.handle({InputEventType::LeftPressed, palette}, 1280, 720);
    (void)workspace.handle({InputEventType::PointerMove, terrain}, 1280, 720);
  };
  select();
  (void)workspace.handle({InputEventType::EscapePressed}, 1280, 720);
  require(!workspace.take_preview_request() && !workspace.selected_type_id(),
          "Escape left a queued ghost that could reappear after cancellation");
  select();
  NativeSurfacePlacementQuote quote;
  quote.quote_revision = 9;
  workspace.set_placement_quote(quote, false);
  const auto cancelled = workspace.handle({InputEventType::EscapePressed}, 1280, 720);
  require(cancelled.kind == SurfaceWorkspaceCommandKind::CancelQuote &&
              !workspace.take_preview_request(),
          "cancelling an existing ghost left a queued replacement");
  select();
  (void)workspace.handle({InputEventType::LeftPressed, terrain}, 1280, 720);
  (void)workspace.handle({InputEventType::PointerMove,
                          {terrain.x + 20, terrain.y}, {20, 0}}, 1280, 720);
  require(!workspace.take_preview_request(),
          "dragging retained a ghost assessed before the camera moved");
}
} // namespace

int main() try {
  responsive_layout();
  anchored_camera();
  input_and_confirmation();
  site_removal_and_refresh();
  building_actions();
  colony_surface_entry();
  palette_clipping();
  pending_ghost_cancellation();
  std::cout << "native surface workspace tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
