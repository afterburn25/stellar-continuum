#include "native_surface_building_layer.hpp"
#include "native_surface_scene.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace stellar::native_colony_ui {
native_map::Point SurfaceViewport::world_to_screen(
    double x, double z, native_map::UiRect terrain) const noexcept {
  return {static_cast<float>(terrain.x + terrain.width * .5 +
                             (x - center_x) * pixels_per_unit),
          static_cast<float>(terrain.y + terrain.height * .5 +
                             (z - center_z) * pixels_per_unit)};
}
} // namespace stellar::native_colony_ui

namespace {
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;
using namespace stellar::native_surface_building;

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

SurfaceBuildingState state_for(const NativeSurfaceSite &site) {
  return {.type_id = site.type_id,
          .rotation_degrees = site.rotation_degrees,
          .complete = site.complete,
          .progress_fraction = site.progress_fraction,
          .powered = site.powered,
          .enabled = site.enabled,
          .staffed = site.staffed,
          .condition = site.condition};
}

std::shared_ptr<const PreparedSurfaceBuildingRaster>
prepared(const SurfaceBuildingStateKey &key,
         const SurfaceBuildingRasterSpec &spec, Point anchor = {32, 32}) {
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(spec.width) * spec.height * 4, 255);
  auto result = std::make_shared<PreparedSurfaceBuildingRaster>();
  result->state = key;
  result->image = RgbaImage::create(spec.width, spec.height, std::move(pixels));
  result->projection = {anchor,
                        {1, 1, static_cast<float>(spec.width - 2),
                         static_cast<float>(spec.height - 2)},
                        17};
  result->input_triangles = 1;
  result->rasterized_triangles = 1;
  result->pixel_tests = 1;
  return result;
}

NativeSurfaceSite site(int id, float x, float z) {
  NativeSurfaceSite result;
  result.building_id = id;
  result.type_id = "fabricator";
  result.x = x;
  result.z = z;
  result.rotation_degrees = 90;
  result.progress_fraction = 1;
  result.complete = result.powered = result.enabled = result.staffed = true;
  result.condition = 1;
  return result;
}

void anchor_scale_pan_and_clip() {
  auto key = *normalize_surface_building_state(state_for(site(1, 12, -8))).key;
  SurfaceBuildingRasterSpec spec;
  spec.width = 100;
  spec.height = 80;
  spec.camera.half_extent = 20;
  const auto image = prepared(key, spec, {25, 60});
  const UiRect terrain{40, 20, 400, 300};
  const SurfaceViewport viewport{2, -3, 2};
  const auto placed = place_surface_building_image(
      image, key, spec, 12, -8, viewport, terrain);
  require(placed.has_value(), "valid nonsquare sprite did not place");
  const auto ground = viewport.world_to_screen(12, -8, terrain);
  require(std::abs(placed->image.destination.width - 80) < .001f &&
              std::abs(placed->image.destination.height - 80) < .001f,
          "orthographic world extent did not scale independently of resolution");
  require(std::abs(placed->image.destination.x + 20 - ground.x) < .001f &&
              std::abs(placed->image.destination.y + 60 - ground.y) < .001f,
          "ground anchor did not align through per-axis source scaling");
  require(placed->image.rotation_degrees == 0,
          "canonical yaw was applied twice at image placement");
  require(placed->image.clip && placed->image.clip->x == terrain.x &&
              placed->image.clip->width == terrain.width,
          "prepared image was not clipped to terrain");
  const auto panned = place_surface_building_image(
      image, key, spec, 12, -8, {7, -1, 2}, terrain);
  require(panned && std::abs(panned->ground_screen.x - ground.x + 10) < .001f &&
              std::abs(panned->ground_screen.y - ground.y + 4) < .001f,
          "pan did not move the anchored sprite with its world center");
}

std::size_t image_index(const DrawList &draw) {
  for (std::size_t index = 0; index < draw.overlay.size(); ++index)
    if (std::holds_alternative<Image>(draw.overlay[index]))
      return index;
  return draw.overlay.size();
}

std::size_t color_index(const DrawList &draw, Color wanted) {
  for (std::size_t index = 0; index < draw.overlay.size(); ++index)
    if (const auto *mesh = std::get_if<TriangleMesh>(&draw.overlay[index]);
        mesh && mesh->color.r == wanted.r && mesh->color.g == wanted.g &&
        mesh->color.b == wanted.b && mesh->color.a == wanted.a)
      return index;
  return draw.overlay.size();
}

void scene_ready_fallback_and_order() {
  const UiRect terrain{0, 0, 500, 400};
  const SurfaceViewport viewport{0, 0, 1};
  const auto value = site(7, 90, 70);
  const auto key = *normalize_surface_building_state(state_for(value)).key;
  SurfaceBuildingRasterSpec spec;
  spec.width = spec.height = 64;
  spec.camera.half_extent = 30;
  auto raster = prepared(key, spec);
  ReadySurfaceBuildingImage ready{key, spec, raster};
  SurfaceBuildingReadyProvider provider =
      [&](std::optional<int> id) -> std::optional<ReadySurfaceBuildingImage> {
    return id == value.building_id ? std::optional{ready} : std::nullopt;
  };
  NativeSurfaceScene scene;
  DrawList draw;
  const auto diagnostics =
      scene.append(draw, viewport, terrain, {value}, value.building_id, 0,
                   &provider);
  const auto sprite = image_index(draw);
  const auto road = color_index(draw, {31, 39, 44, 235});
  const auto selection = color_index(draw, {250, 221, 111, 255});
  require(diagnostics.replaced_structures == 1 && sprite < draw.overlay.size(),
          "usable ready image did not replace its fallback structure");
  require(road < sprite && sprite < selection,
          "ready structure escaped road/selection painter order");
  require(diagnostics.road_segments > 0,
          "ready replacement removed the canonical road graph");
  const auto &road_mesh = std::get<TriangleMesh>(draw.overlay[road]);
  const auto reaches = [&](Point point) {
    return std::ranges::any_of(road_mesh.vertices, [&](Point vertex) {
      return std::hypot(vertex.x-point.x, vertex.y-point.y) <= 3.f;
    });
  };
  require(reaches(viewport.world_to_screen(0,0,terrain)) &&
          reaches(viewport.world_to_screen(value.x,value.z,terrain)),
          "cosmetic road endpoints stopped before the projected foundations");
  const auto center = viewport.world_to_screen(value.x, value.z, terrain);
  require(NativeSurfaceScene::contains_site(value, center, viewport, terrain),
          "ready image changed canonical footprint selection");

  ready.expected_state.rotation_degrees = 0;
  DrawList stale;
  const auto stale_diagnostics =
      scene.append(stale, viewport, terrain, {value}, {}, 0, &provider);
  require(stale_diagnostics.replaced_structures == 0 &&
              image_index(stale) == stale.overlay.size(),
          "stale state replaced the fallback mesh");
  require(color_index(stale, {139, 151, 153, 250}) < stale.overlay.size(),
          "stale state removed the baseline structure");

  ready.expected_state = key;
  ready.prepared = prepared(key, spec, {-1, 32});
  DrawList invalid;
  const auto invalid_diagnostics =
      scene.append(invalid, viewport, terrain, {value}, {}, 0, &provider);
  require(invalid_diagnostics.replaced_structures == 0 &&
              image_index(invalid) == invalid.overlay.size(),
          "invalid metadata replaced the fallback mesh");

  const auto fallback = site(6, 75, 35);
  ready.expected_state = key;
  ready.prepared = raster;
  DrawList mixed;
  const auto mixed_diagnostics = scene.append(
      mixed, viewport, terrain, {fallback, value}, {}, 0, &provider);
  const auto fallback_structure = color_index(mixed, {139, 151, 153, 250});
  require(mixed_diagnostics.replaced_structures == 1 &&
              fallback_structure < image_index(mixed),
          "fallback and ready structures were not sorted by ground depth");

  auto offscreen = value;
  offscreen.z = -220;
  const auto offscreen_key =
      *normalize_surface_building_state(state_for(offscreen)).key;
  ready.expected_state = offscreen_key;
  ready.prepared = prepared(offscreen_key, spec);
  DrawList tall;
  const auto tall_diagnostics =
      scene.append(tall, viewport, terrain, {offscreen}, {}, 0, &provider);
  require(tall_diagnostics.replaced_structures == 1 &&
              image_index(tall) < tall.overlay.size(),
          "image bounds were culled only because the ground footprint was offscreen");
}

void disabled_provider_is_equivalent() {
  const UiRect terrain{0, 0, 500, 400};
  const SurfaceViewport viewport{0, 0, 1};
  const auto value = site(8, 90, 70);
  NativeSurfaceScene first_scene, second_scene;
  DrawList implicit, explicit_null;
  const auto first =
      first_scene.append(implicit, viewport, terrain, {value}, value.building_id,
                         2);
  const auto second = second_scene.append(explicit_null, viewport, terrain,
                                          {value}, value.building_id, 2,
                                          nullptr);
  require(first.sites == second.sites && first.meshes == second.meshes &&
              first.triangles == second.triangles &&
              first.road_segments == second.road_segments &&
              implicit.overlay.size() == explicit_null.overlay.size(),
          "disabled provider changed baseline scene diagnostics");
  for (std::size_t index = 0; index < implicit.overlay.size(); ++index) {
    const auto *left = std::get_if<TriangleMesh>(&implicit.overlay[index]);
    const auto *right = std::get_if<TriangleMesh>(&explicit_null.overlay[index]);
    bool same_vertices = left && right &&
                         left->vertices.size() == right->vertices.size();
    for (std::size_t vertex = 0;
         same_vertices && vertex < left->vertices.size(); ++vertex)
      same_vertices = left->vertices[vertex].x == right->vertices[vertex].x &&
                      left->vertices[vertex].y == right->vertices[vertex].y;
    require(left && right && left->color.r == right->color.r &&
                left->color.g == right->color.g && same_vertices &&
                left->indices == right->indices,
            "disabled provider changed baseline draw commands");
  }
}

void ready_foundations_do_not_duplicate_flat_aprons() {
  const auto value = site(7, 90, 70);
  const auto site_key = *normalize_surface_building_state(state_for(value)).key;
  const auto hub_key = *normalize_surface_building_state(
      SurfaceBuildingState{.complete=true, .progress_fraction=1.,
                          .powered=true, .enabled=true, .staffed=true,
                          .hub_level=2}).key;
  SurfaceBuildingRasterSpec spec;
  spec.width = spec.height = 64;
  ReadySurfaceBuildingImage site_image{site_key, spec, prepared(site_key, spec)};
  ReadySurfaceBuildingImage hub_image{hub_key, spec, prepared(hub_key, spec)};
  SurfaceBuildingReadyProvider ready = [&](std::optional<int> id)
      -> std::optional<ReadySurfaceBuildingImage> {
    return id ? site_image : hub_image;
  };
  NativeSurfaceScene scene;
  DrawList draw;
  const auto result = scene.append(draw, {0,0,1}, {0,0,500,400}, {value}, {}, 2, &ready);
  require(result.replaced_structures == 2, "foundation fixture did not draw ready structures");
  // Ready sprites already contain oblique foundations. Compare mesh colors
  // against the prior flat ground shape to catch the raised-bowl regression.
  require(color_index(draw, {76, 84, 87, 235}) == draw.overlay.size(),
          "ready foundation retained an incompatible flat apron");
}

void pending_provider_remains_bounded() {
  std::vector<NativeSurfaceSite> sites;
  sites.reserve(128);
  for (int index = 0; index < 128; ++index) {
    const auto radians = static_cast<float>(index) * 6.283185307f / 128.f;
    sites.push_back(site(index + 1, std::cos(radians) * 150.f,
                         std::sin(radians) * 150.f));
  }
  SurfaceBuildingReadyProvider pending = [](std::optional<int>) {
    return std::optional<ReadySurfaceBuildingImage>{};
  };
  DrawList draw;
  NativeSurfaceScene scene;
  const auto diagnostics = scene.append(draw, {0, 0, 1}, {0, 0, 700, 700},
                                        sites, {}, 0, &pending);
  require(diagnostics.sites == 128 && diagnostics.triangles <= 8192 &&
              diagnostics.road_segments <= 192 &&
              diagnostics.replaced_structures == 0,
          "pending provider broke the bounded dense fallback scene");
}

} // namespace

int main() {
  try {
    anchor_scale_pan_and_clip();
    scene_ready_fallback_and_order();
    disabled_provider_is_equivalent();
    ready_foundations_do_not_duplicate_flat_aprons();
    pending_provider_remains_bounded();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native surface building layer test failed: " << error.what()
              << '\n';
    return 1;
  }
}
