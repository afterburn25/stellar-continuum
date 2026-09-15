#include "native_colony_workspace.hpp"
#include "native_surface_scene.hpp"
#include "native_surface_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace {
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error(std::string(message));
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
  value.construction_sites.push_back({.building_id = 9,
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
  for (const auto [width, height] : {std::pair{640, 360},
                                     {1280, 720},
                                     {1920, 1080},
                                     {2560, 1440},
                                     {3840, 2160}}) {
    const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
    const auto navigation = NativeUiLayout::for_viewport(width, height);
    if (height >= 720)
      require(layout.surface.x >=
                  navigation.research.x + navigation.research.width,
              "surface overlaps navigation rail");
    require(layout.surface.contains(center(layout.back)),
            "back escaped surface");
    require(layout.surface.contains(center(layout.palette)),
            "palette escaped surface");
    require(layout.surface.contains(center(layout.terrain)),
            "terrain escaped surface");
    require(layout.surface.contains(center(layout.inspector)),
            "inspector escaped surface");
    require(layout.confirmation.contains(center(layout.confirm)),
            "confirm escaped modal");
    require(layout.confirmation.contains(center(layout.cancel)),
            "cancel escaped modal");
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
  for (int index = 0; index < 10000; ++index)
    camera = camera.translated(-1000.f, 1000.f)
                 .zoomed_at(1.2f, anchor, terrain, .08, 4.);
  require(std::abs(camera.center_x) <= 2048. &&
              std::abs(camera.center_z) <= 2048.,
          "prolonged surface pan escaped the finite camera range");
  NativeSurfaceScene scene;
  DrawList draw;
  (void)scene.append(draw, camera, terrain, {}, {}, 0);
}

void input_and_confirmation() {
  NativeSurfaceWorkspace workspace;
  workspace.open(colony(), 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  const Point palette_point{layout.palette_rows.x + 12.f,
                            layout.palette_rows.y + 20.f};
  require(
      workspace.handle({InputEventType::LeftPressed, palette_point}, 1280, 720)
          .captured,
      "palette press was not captured");
  require(workspace.selected_type_id() ==
              std::optional<std::string>{"fabricator"},
          "palette did not select the canonical type id");
  const auto terrain_point = center(layout.terrain);
  (void)workspace.handle({InputEventType::PointerMove, terrain_point}, 1280,
                         720);
  auto pending = workspace.take_preview_request();
  require(pending &&
              pending->kind == SurfaceWorkspaceCommandKind::PreviewPlacement &&
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
  const Point batched_point{terrain_point.x + 1.f, terrain_point.y + 1.f};
  (void)workspace.handle(
      {InputEventType::PointerMove, batched_point, {1.f, 1.f}}, 1280, 720);
  (void)workspace.handle({InputEventType::LeftPressed, batched_point}, 1280,
                         720);
  const auto clicked = workspace.handle(
      {InputEventType::LeftReleased, batched_point}, 1280, 720);
  require(clicked.kind == SurfaceWorkspaceCommandKind::PreviewPlacement &&
              clicked.open_confirmation,
          "click without drag did not request exact confirmation");
  quote.x = clicked.x;
  quote.z = clicked.z;
  workspace.set_placement_quote(quote, true);
  require(!workspace.take_preview_request(),
          "a queued hover preview survived opening exact confirmation");
  require(workspace
                  .handle({InputEventType::LeftPressed, center(layout.confirm)},
                          1280, 720)
                  .kind == SurfaceWorkspaceCommandKind::ConfirmPlacement,
          "accepted placement could not confirm");

  workspace.open(colony(), 1280, 720);
  const auto start = center(layout.terrain);
  (void)workspace.handle({InputEventType::LeftPressed, start}, 1280, 720);
  const auto moved = Point{start.x + 40.f, start.y + 20.f};
  (void)workspace.handle({InputEventType::PointerMove, moved, {40.f, 20.f}},
                         1280, 720);
  require(
      workspace.handle({InputEventType::LeftReleased, moved}, 1280, 720).kind ==
          SurfaceWorkspaceCommandKind::None,
      "surface drag became a placement or selection");
  workspace.close();
  workspace.open(colony(), 1280, 720);
  const auto reopened = workspace.viewport();
  (void)workspace.handle(
      {InputEventType::PointerMove, {moved.x + 5.f, moved.y + 5.f}, {5.f, 5.f}},
      1280, 720);
  require(workspace.viewport().center_x == reopened.center_x &&
              workspace.viewport().center_z == reopened.center_z,
          "reopening resumed a stale surface drag");
}

void site_removal_and_refresh() {
  NativeSurfaceWorkspace workspace;
  auto view = colony();
  workspace.open(view, 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  auto site = workspace.viewport().world_to_screen(90., 70., layout.terrain);
  site.x += 11.f;
  site.y += 11.f;
  (void)workspace.handle({InputEventType::LeftPressed, site}, 1280, 720);
  (void)workspace.handle({InputEventType::LeftReleased, site}, 1280, 720);
  require(workspace.selected_building_id() == std::optional<int>{9},
          "existing surface site was not selected");
  require(workspace
                  .handle({InputEventType::LeftPressed, center(layout.remove)},
                          1280, 720)
                  .kind == SurfaceWorkspaceCommandKind::PreviewRemoval,
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

void colony_surface_entry() {
  NativeColonyWorkspace colony_ui;
  colony_ui.open(colony());
  const auto layout = ColonyWorkspaceLayout::for_viewport(1280, 720);
  require(colony_ui
                  .handle({InputEventType::LeftPressed,
                           center(layout.open_surface)},
                          1280, 720)
                  .kind == ColonyWorkspaceCommandKind::OpenSurface,
          "owned solid body did not expose Open Surface through input");
}

void palette_clipping() {
  auto view = colony();
  for (int index = 0; index < 12; ++index) {
    auto option = view.available_buildings.front();
    option.type_id = "module-" + std::to_string(index);
    option.name = "Module " + std::to_string(index);
    view.available_buildings.push_back(std::move(option));
  }
  NativeSurfaceWorkspace workspace;
  workspace.open(std::move(view), 1280, 720);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  (void)workspace.handle(
      {InputEventType::Wheel, center(layout.palette), {}, -3}, 1280, 720);
  DrawList draw;
  workspace.render(draw, 1280, 720);
  for (const auto &item : draw.overlay)
    if (const auto *label = std::get_if<Text>(&item);
        label && label->value.starts_with("Module ")) {
      require(label->clip.has_value(), "scrolled palette text lacks a clip");
      const auto &clip = *label->clip;
      require(clip.x >= layout.palette_rows.x &&
                  clip.y >= layout.palette_rows.y &&
                  clip.x + clip.width <=
                      layout.palette_rows.x + layout.palette_rows.width &&
                  clip.y + clip.height <=
                      layout.palette_rows.y + layout.palette_rows.height,
              "partial palette text escaped its rows viewport");
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
  const auto cancelled =
      workspace.handle({InputEventType::EscapePressed}, 1280, 720);
  require(cancelled.kind == SurfaceWorkspaceCommandKind::CancelQuote &&
              !workspace.take_preview_request(),
          "cancelling an existing ghost left a queued replacement");
  select();
  (void)workspace.handle({InputEventType::LeftPressed, terrain}, 1280, 720);
  (void)workspace.handle(
      {InputEventType::PointerMove, {terrain.x + 20, terrain.y}, {20, 0}}, 1280,
      720);
  require(!workspace.take_preview_request(),
          "dragging retained a ghost assessed before the camera moved");
}

bool same_color(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

const TriangleMesh *mesh_with_color(const DrawList &draw, Color color) {
  for (const auto &item : draw.overlay)
    if (const auto *mesh = std::get_if<TriangleMesh>(&item);
        mesh && same_color(mesh->color, color))
      return mesh;
  return nullptr;
}

float segment_distance(Point a, Point b, Point point) {
  const auto dx = b.x - a.x, dy = b.y - a.y;
  const auto length_squared = dx * dx + dy * dy;
  const auto t =
      length_squared > 0
          ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) /
                           length_squared,
                       0.f, 1.f)
          : 0.f;
  return std::hypot(a.x + dx * t - point.x, a.y + dy * t - point.y);
}

void surface_scene_geometry() {
  auto view = colony();
  view.surface_hub_level = 2;
  view.construction_sites.front().complete = true;
  view.construction_sites.front().powered = true;
  view.construction_sites.front().staffed = true;
  auto lab = view.construction_sites.front();
  lab.building_id = 10;
  lab.type_id = "science_lab";
  lab.x = -90.f;
  lab.z = 70.f;
  lab.rotation_degrees = 30.f;
  lab.complete = false;
  lab.progress_fraction = .5;
  view.construction_sites.push_back(lab);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  SurfaceViewport camera{0., 0., .55};
  NativeSurfaceScene scene;
  DrawList draw;
  const auto stats =
      scene.append(draw, camera, layout.terrain, view.construction_sites,
                   lab.building_id, view.surface_hub_level);
  require(stats.sites == 2 && stats.meshes > 1 && stats.triangles > 10,
          "surface scene did not build bounded typed geometry");
  require(stats.road_segments > 0,
          "surface scene did not create visual service roads");
  std::size_t mesh_count{};
  for (const auto &item : draw.overlay)
    if (const auto *mesh = std::get_if<TriangleMesh>(&item)) {
      ++mesh_count;
      require(mesh->clip && mesh->clip->x == layout.terrain.x &&
                  mesh->clip->y == layout.terrain.y &&
                  mesh->clip->width == layout.terrain.width &&
                  mesh->clip->height == layout.terrain.height,
              "surface triangle mesh escaped terrain clipping");
      require(mesh->indices.size() % 3 == 0 && !mesh->vertices.empty(),
              "surface mesh is not indexed triangles");
    }
  require(mesh_count == stats.meshes,
          "surface diagnostics disagree with emitted meshes");
  const auto center = camera.world_to_screen(lab.x, lab.z, layout.terrain);
  require(
      NativeSurfaceScene::contains_site(lab, center, camera, layout.terrain),
      "surface hit testing does not use the footprint projection");
  require(!NativeSurfaceScene::contains_site(lab, {center.x + 30.f, center.y},
                                             camera, layout.terrain),
          "surface hit testing ignores the canonical footprint radius");

  const auto *road = mesh_with_color(draw, {31, 39, 44, 235});
  const auto *selection = mesh_with_color(draw, {250, 221, 111, 255});
  require(road && selection, "road or selection layer was not emitted");
  std::size_t road_index = draw.overlay.size(),
              selection_index = draw.overlay.size();
  for (std::size_t index = 0; index < draw.overlay.size(); ++index) {
    const auto *mesh = std::get_if<TriangleMesh>(&draw.overlay[index]);
    if (mesh == road)
      road_index = index;
    if (mesh == selection)
      selection_index = index;
  }
  require(road_index < selection_index,
          "painter layers did not place roads below selection");
  const auto footprint = NativeSurfaceScene::footprint_radius(lab) *
                         static_cast<float>(camera.pixels_per_unit);
  for (const auto vertex : selection->vertices)
    require(std::hypot(vertex.x - center.x, vertex.y - center.y) > footprint,
            "selection ring filled or covered the building footprint");
}

void surface_scene_rotation_and_families() {
  auto site = colony().construction_sites.front();
  site.complete = site.enabled = site.powered = site.staffed = true;
  const UiRect terrain{0, 0, 900, 700};
  const SurfaceViewport camera{0, 0, 1};
  NativeSurfaceScene scene;
  DrawList horizontal, vertical, lab;
  site.rotation_degrees = 0;
  (void)scene.append(horizontal, camera, terrain, {site}, {}, 0);
  site.rotation_degrees = 90;
  (void)scene.append(vertical, camera, terrain, {site}, {}, 0);
  auto science = site;
  science.type_id = "advanced_science_lab";
  (void)scene.append(lab, camera, terrain, {science}, {}, 0);
  const auto *horizontal_shell =
      mesh_with_color(horizontal, {139, 151, 153, 250});
  const auto *vertical_shell = mesh_with_color(vertical, {139, 151, 153, 250});
  const auto *lab_glass = mesh_with_color(lab, {54, 116, 134, 245});
  require(horizontal_shell && vertical_shell && lab_glass,
          "typed structure materials were not emitted");
  require(std::abs(horizontal_shell->vertices[0].x -
                   vertical_shell->vertices[0].x) > 1.f,
          "persisted building rotation did not rotate its silhouette");
  require(lab_glass->indices.size() != horizontal_shell->indices.size(),
          "advanced functional family fell back to the generic silhouette");
}

void surface_scene_route_cache_and_bounds() {
  auto site = colony().construction_sites.front();
  site.complete = site.enabled = site.powered = site.staffed = true;
  site.x = 90.f;
  site.z = 70.f;
  const UiRect terrain{0, 0, 1200, 1200};
  NativeSurfaceScene scene;
  DrawList first, moved, zoomed;
  const auto first_stats =
      scene.append(first, {0, 0, 1}, terrain, {site}, {}, 0);
  site.x += .01f;
  (void)scene.append(moved, {0, 0, 1}, terrain, {site}, {}, 0);
  (void)scene.append(zoomed, {0, 0, 2}, terrain, {site}, {}, 0);
  const auto *first_road = mesh_with_color(first, {31, 39, 44, 235});
  const auto *moved_road = mesh_with_color(moved, {31, 39, 44, 235});
  const auto *zoomed_road = mesh_with_color(zoomed, {31, 39, 44, 235});
  require(first_stats.road_segments > 0 && first_road && moved_road &&
              zoomed_road,
          "route geometry was not available for cache checks");
  const auto first_start =
      Point{(first_road->vertices[0].x + first_road->vertices[5].x) * .5f,
            (first_road->vertices[0].y + first_road->vertices[5].y) * .5f};
  const auto last = first_road->vertices.size() - 6;
  const auto first_end = Point{
      (first_road->vertices[last + 1].x + first_road->vertices[last + 2].x) *
          .5f,
      (first_road->vertices[last + 1].y + first_road->vertices[last + 2].y) *
          .5f};
  const auto original_site_center =
      Point{center(terrain).x + 90.f, center(terrain).y + 70.f};
  require(std::abs(std::hypot(first_start.x - center(terrain).x,
                              first_start.y - center(terrain).y) -
                   24.f) < .01f &&
              std::abs(std::hypot(first_end.x - original_site_center.x,
                                  first_end.y - original_site_center.y) -
                       NativeSurfaceScene::footprint_radius(site)) < .01f,
          "service road terminals did not touch the hub and site aprons");
  bool route_changed =
      first_road->vertices.size() != moved_road->vertices.size();
  for (std::size_t index = 0;
       !route_changed && index < first_road->vertices.size(); ++index)
    route_changed =
        first_road->vertices[index].x != moved_road->vertices[index].x ||
        first_road->vertices[index].y != moved_road->vertices[index].y;
  require(route_changed,
          "an exact persisted-position change reused a stale route");
  const auto screen_center = center(terrain);
  const auto moved_world_x = moved_road->vertices.front().x - screen_center.x;
  const auto zoomed_world_x =
      (zoomed_road->vertices.front().x - screen_center.x) / 2.f;
  require(std::abs(moved_world_x - zoomed_world_x) < .001f,
          "road width or path changed with camera zoom");

  auto blocker = site;
  blocker.building_id = 88;
  blocker.x = 55.f;
  blocker.z = 42.f;
  DrawList diverted;
  (void)scene.append(diverted, {0, 0, 1}, terrain, {site, blocker}, {}, 0);
  const auto *diverted_roads = mesh_with_color(diverted, {31, 39, 44, 235});
  require(diverted_roads && diverted_roads->vertices.size() % 6 == 0,
          "diverted route geometry was malformed");
  const auto blocker_screen =
      Point{center(terrain).x + blocker.x, center(terrain).y + blocker.z};
  bool reached_target_apron = false;
  const auto target_screen =
      Point{center(terrain).x + site.x, center(terrain).y + site.z};
  for (std::size_t index = 0;
       index < diverted_roads->vertices.size() && !reached_target_apron;
       index += 6) {
    const auto a = Point{(diverted_roads->vertices[index].x +
                          diverted_roads->vertices[index + 5].x) *
                             .5f,
                         (diverted_roads->vertices[index].y +
                          diverted_roads->vertices[index + 5].y) *
                             .5f};
    const auto b = Point{(diverted_roads->vertices[index + 1].x +
                          diverted_roads->vertices[index + 2].x) *
                             .5f,
                         (diverted_roads->vertices[index + 1].y +
                          diverted_roads->vertices[index + 2].y) *
                             .5f};
    require(segment_distance(a, b, blocker_screen) >=
                NativeSurfaceScene::footprint_radius(blocker) + 4.4f,
            "a service-road centerline breached a persisted footprint");
    reached_target_apron =
        std::abs(std::hypot(b.x - target_screen.x, b.y - target_screen.y) -
                 NativeSurfaceScene::footprint_radius(site)) < .01f;
  }
  require(reached_target_apron,
          "diverted target road did not reach its own apron edge");

  std::vector<NativeSurfaceSite> many;
  many.reserve(128);
  for (int index = 0; index < 128; ++index) {
    auto value = site;
    value.building_id = index + 1;
    const float angle = static_cast<float>(index) * 2.f * 3.14159265f / 128.f;
    const float radius = 150.f + static_cast<float>(index % 4) * 70.f;
    value.x = std::cos(angle) * radius;
    value.z = std::sin(angle) * radius;
    many.push_back(value);
  }
  auto invalid = many;
  invalid.front().x = std::numeric_limits<float>::quiet_NaN();
  bool rejected_invalid = false;
  try {
    DrawList ignored;
    (void)scene.append(ignored, {0, 0, 1}, terrain, invalid, {}, 0);
  } catch (const std::invalid_argument &) {
    rejected_invalid = true;
  }
  require(rejected_invalid,
          "invalid persisted geometry was not rejected cleanly");
  const auto started = std::chrono::steady_clock::now();
  DrawList bounded;
  const auto stats = scene.append(bounded, {0, 0, 1}, terrain, many, {}, 0);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(stats.sites == 128 && stats.triangles <= 8192 &&
              stats.road_segments <= 192 && stats.meshes <= stats.triangles,
          "128-site scene was not bounded or skipped invalid geometry");
  const auto *dense_bodies = mesh_with_color(bounded, {139, 151, 153, 250});
  require(dense_bodies && dense_bodies->indices.size() / 18 == stats.sites,
          "dense LOD did not draw one structure body for every visible site");
  for (const auto &item : bounded.overlay) {
    const auto *mesh = std::get_if<TriangleMesh>(&item);
    if (!mesh)
      continue;
    require(mesh->clip && mesh->clip->x == terrain.x &&
                mesh->clip->y == terrain.y &&
                mesh->clip->width == terrain.width &&
                mesh->clip->height == terrain.height &&
                mesh->indices.size() % 3 == 0,
            "bounded scene emitted an unclipped or malformed mesh");
    for (const auto vertex : mesh->vertices)
      require(std::isfinite(vertex.x) && std::isfinite(vertex.y),
              "bounded scene emitted a non-finite vertex");
    for (const auto index : mesh->indices)
      require(index >= 0 &&
                  static_cast<std::size_t>(index) < mesh->vertices.size(),
              "bounded scene emitted an invalid triangle index");
  }
  require(elapsed < std::chrono::seconds(2),
          "cold 128-site routing exceeded its bounded timing allowance");
  DrawList cached;
  const auto cached_start = std::chrono::steady_clock::now();
  (void)scene.append(cached, {0, 0, 1}, terrain, many, {}, 0);
  require(std::chrono::steady_clock::now() - cached_start <
              std::chrono::seconds(1),
          "cached 128-site routing exceeded its bounded timing allowance");
}
} // namespace

int main() try {
  responsive_layout();
  anchored_camera();
  input_and_confirmation();
  site_removal_and_refresh();
  colony_surface_entry();
  palette_clipping();
  pending_ghost_cancellation();
  surface_scene_geometry();
  surface_scene_rotation_and_families();
  surface_scene_route_cache_and_bounds();
  std::cout << "native surface workspace tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
