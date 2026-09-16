#include "native_surface_building_geometry.hpp"
#include "native_surface_building_raster.hpp"
#include <stellar/core/surface_economy.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace surface = stellar::native_surface_building;

namespace {

void verify(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

surface::SurfaceBuildingState building(std::string type = "fabricator") {
  surface::SurfaceBuildingState value;
  value.type_id = std::move(type);
  value.complete = true;
  value.progress_fraction = 1;
  value.powered = true;
  value.enabled = true;
  value.staffed = true;
  return value;
}

std::uint64_t image_hash(const stellar::native_map::RgbaImage &image) {
  std::uint64_t result = 1469598103934665603ull;
  for (const auto byte : image.pixels()) {
    result ^= byte;
    result *= 1099511628211ull;
  }
  return result;
}

surface::Vec3 subtract(surface::Vec3 left, surface::Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

surface::Vec3 cross(surface::Vec3 left, surface::Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

float dot(surface::Vec3 left, surface::Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

surface::Vec3 centroid(const surface::Triangle &triangle) {
  return {(triangle.a.x + triangle.b.x + triangle.c.x) / 3.f,
          (triangle.a.y + triangle.b.y + triangle.c.y) / 3.f,
          (triangle.a.z + triangle.b.z + triangle.c.z) / 3.f};
}

void verify_outward(const surface::Triangle &triangle, surface::Vec3 center,
                    const char *message) {
  const auto normal = cross(subtract(triangle.b, triangle.a),
                            subtract(triangle.c, triangle.a));
  verify(dot(normal, subtract(centroid(triangle), center)) > 1e-5f, message);
}

void normalized_visual_keys_are_stable() {
  auto first = building();
  first.complete = false;
  first.progress_fraction = .72;
  first.condition = .7;
  auto second = first;
  second.progress_fraction = .99;
  second.condition = .4;
  const auto first_key = surface::normalize_surface_building_state(first);
  const auto second_key = surface::normalize_surface_building_state(second);
  verify(first_key && second_key, "valid states did not normalize");
  verify(*first_key.key == *second_key.key,
         "routine progress or condition changed the visual cache identity");

  second.progress_fraction = .2;
  verify(*surface::normalize_surface_building_state(second).key !=
             *first_key.key,
         "construction visual threshold did not change the key");
  auto equivalent_yaw = first;
  equivalent_yaw.rotation_degrees = 360;
  verify(surface::normalize_surface_building_state(equivalent_yaw)
                 .key->rotation_degrees == 0,
         "authoritative yaw was not normalized for stable identity");
  first.complete = true;
  first.progress_fraction = .1;
  second = first;
  second.progress_fraction = 1;
  verify(*surface::normalize_surface_building_state(first).key ==
             *surface::normalize_surface_building_state(second).key,
         "authoritatively complete states retained progress churn");
}

void rotation_anchor_and_bounds_are_preserved() {
  auto zero = building();
  auto quarter = zero;
  quarter.rotation_degrees = 90;
  const auto zero_geometry = surface::prepare_geometry(zero);
  const auto quarter_geometry = surface::prepare_geometry(quarter);
  verify(zero_geometry && quarter_geometry, "rotated geometry failed");
  verify(zero_geometry.geometry->triangles.size() ==
             quarter_geometry.geometry->triangles.size(),
         "rotation changed geometry topology");
  const auto &before = zero_geometry.geometry->triangles.at(32).a;
  const auto &after = quarter_geometry.geometry->triangles.at(32).a;
  verify(std::abs(after.x - before.z) < .01f &&
             std::abs(after.z + before.x) < .01f &&
             std::abs(after.y - before.y) < .01f,
         "canonical yaw was not applied to actual geometry");
  verify(std::isfinite(quarter_geometry.geometry->model_bounds.minimum.x) &&
             quarter_geometry.geometry->model_bounds.minimum.x <= after.x &&
             quarter_geometry.geometry->model_bounds.maximum.x >= after.x &&
             quarter_geometry.geometry->model_bounds.minimum.z <= after.z &&
             quarter_geometry.geometry->model_bounds.maximum.z >= after.z,
         "rotated model bounds do not contain rotated geometry");
  verify(zero_geometry.geometry->ground_anchor.x == 0 &&
             zero_geometry.geometry->ground_anchor.y == 0 &&
             zero_geometry.geometry->ground_anchor.z == 0 &&
             quarter_geometry.geometry->ground_anchor.x == 0 &&
             quarter_geometry.geometry->ground_anchor.z == 0,
         "rotation moved the ground-center anchor");

  const auto projection = surface::project_surface_building(
      *quarter_geometry.geometry, {});
  verify(static_cast<bool>(projection),
         "valid rotated geometry did not project");
  verify(projection.projection->projected_bounds.width > 0 &&
             projection.projection->projected_bounds.height > 0,
         "projected sprite bounds are empty");
  verify(projection.projection->canonical_footprint_radius == 17,
         "canonical picking footprint was not retained separately");
}

void primitive_faces_point_outward() {
  const auto prepared = surface::prepare_geometry(building("fabricator"));
  verify(static_cast<bool>(prepared), "primitive winding geometry failed");
  const auto &triangles = prepared.geometry->triangles;

  // Every four triangles form one segment of the eight-sided foundation
  // cylinder: two wall faces followed by the bottom and top caps.
  verify(triangles.size() >= 32, "foundation cylinder geometry is incomplete");
  for (std::size_t index = 0; index < 32; ++index)
    verify_outward(triangles[index], {0, .7f, 0},
                   "cylinder face points into the solid");

  // The fabricator's first structure primitive immediately follows the
  // foundation and is a box centered at this point.
  verify(triangles.size() >= 44, "fabricator box geometry is incomplete");
  for (std::size_t index = 32; index < 44; ++index)
    verify_outward(triangles[index], {0, 4.5f, 0},
                   "box face points into the solid");

  // A complete operational structure ends with the 16-by-10 spherical status
  // marker: 16 bottom, 256 body and 16 top triangles.
  constexpr std::size_t marker_triangles = 288;
  verify(triangles.size() >= marker_triangles,
         "ellipsoid status marker geometry is incomplete");
  for (std::size_t index = triangles.size() - marker_triangles;
       index < triangles.size(); ++index)
    verify_outward(triangles[index], {0, 13, 0},
                   "ellipsoid face points into the solid");
}

void construction_and_operational_states_are_visible() {
  auto early = building("science_lab");
  early.complete = false;
  early.progress_fraction = .1;
  auto late = early;
  late.progress_fraction = .999999;
  auto complete = late;
  complete.complete = true;
  const auto early_geometry = surface::prepare_geometry(early);
  const auto late_geometry = surface::prepare_geometry(late);
  const auto complete_geometry = surface::prepare_geometry(complete);
  verify(early_geometry && late_geometry && complete_geometry,
         "construction geometry failed");
  verify(early_geometry.geometry->has_scaffolding &&
             late_geometry.geometry->has_scaffolding,
         "unfinished construction lost scaffolding near completion");
  verify(!complete_geometry.geometry->has_scaffolding,
         "authoritatively complete building retained scaffolding");
  verify(late_geometry.geometry->triangles.size() >
             early_geometry.geometry->triangles.size(),
         "construction stages have no geometric distinction");

  auto normal = building("power_generator");
  const auto normal_raster =
      surface::prepare_surface_building_raster(
          *surface::normalize_surface_building_state(normal).key, {});
  auto offline = normal;
  offline.powered = false;
  const auto offline_raster =
      surface::prepare_surface_building_raster(
          *surface::normalize_surface_building_state(offline).key, {});
  auto unstaffed = normal;
  unstaffed.staffed = false;
  const auto unstaffed_geometry = surface::prepare_geometry(unstaffed);
  auto damaged = normal;
  damaged.condition = .2;
  const auto damaged_geometry = surface::prepare_geometry(damaged);
  verify(normal_raster && offline_raster && unstaffed_geometry &&
             damaged_geometry,
         "operational state preparation failed");
  verify(image_hash(*normal_raster.prepared->image) !=
             image_hash(*offline_raster.prepared->image),
         "powered state did not affect the raster");
  verify(unstaffed_geometry.geometry->triangles.size() >
             surface::prepare_geometry(normal).geometry->triangles.size(),
         "workforce state did not affect geometry");
  verify(damaged_geometry.geometry->triangles.size() >
             surface::prepare_geometry(normal).geometry->triangles.size(),
         "condition state did not affect geometry");
}

void hub_levels_and_capital_state_are_distinct() {
  auto hub = building("");
  hub.hub_level = 1;
  const auto level_one = surface::prepare_geometry(hub);
  hub.hub_level = 2;
  const auto level_two = surface::prepare_geometry(hub);
  hub.hub_level = 3;
  const auto level_three = surface::prepare_geometry(hub);
  auto capital = hub;
  capital.capital = true;
  const auto ordinary_raster = surface::prepare_surface_building_raster(
      *surface::normalize_surface_building_state(hub).key, {});
  const auto capital_raster = surface::prepare_surface_building_raster(
      *surface::normalize_surface_building_state(capital).key, {});
  verify(static_cast<bool>(level_one), "hub level one preparation failed");
  verify(static_cast<bool>(level_two), "hub level two preparation failed");
  verify(static_cast<bool>(level_three), "hub level three preparation failed");
  verify(static_cast<bool>(ordinary_raster),
         "ordinary hub raster preparation failed");
  verify(static_cast<bool>(capital_raster),
         "capital hub raster preparation failed");
  verify(level_one.geometry->triangles.size() <
             level_two.geometry->triangles.size() &&
             level_two.geometry->triangles.size() <
                 level_three.geometry->triangles.size(),
         "hub levels do not have distinct geometry");
  verify(image_hash(*ordinary_raster.prepared->image) !=
             image_hash(*capital_raster.prepared->image),
         "capital state did not affect the final hub raster");
}

void raster_is_bounded_and_reports_metadata() {
  const auto geometry = surface::prepare_geometry(building("cargo_terminal"));
  verify(static_cast<bool>(geometry), "cargo geometry failed");
  surface::SurfaceBuildingRasterSpec spec;
  spec.width = 160;
  spec.height = 144;
  const auto prepared =
      surface::prepare_surface_building_raster(geometry.geometry, spec);
  verify(static_cast<bool>(prepared), "valid bounded raster failed");
  verify(prepared.prepared->image->width() == 160 &&
             prepared.prepared->image->height() == 144,
         "raster dimensions changed");
  verify(prepared.prepared->input_triangles ==
             geometry.geometry->triangles.size() &&
             prepared.prepared->rasterized_triangles <=
                 prepared.prepared->input_triangles &&
             prepared.prepared->pixel_tests <=
                 surface::maximum_raster_pixel_tests,
         "raster counters exceed their contracts");
  verify(prepared.prepared->projection.ground_anchor_pixels.x >= 0 &&
             prepared.prepared->projection.ground_anchor_pixels.x <= 160 &&
             prepared.prepared->projection.ground_anchor_pixels.y >= 0 &&
             prepared.prepared->projection.ground_anchor_pixels.y <= 144,
         "ground anchor projection is outside the prepared sprite");
}

void supported_detail_matrix() {
  std::size_t maximum_work{};
  for (const auto &definition : stellar::core::surface_building_catalog()) {
    const auto geometry = surface::prepare_geometry(building(definition.id));
    verify(geometry && geometry.geometry->canonical_footprint_radius == definition.footprint_radius,
           "Prepared geometry diverged from the authoritative catalog footprint.");
  }
  for (const int dimension : {256, 512}) {
    surface::SurfaceBuildingRasterSpec spec;
    spec.width = spec.height = dimension;
    for (const char *type : {"power_generator", "science_lab", "fabricator", "trade_hub",
         "habitat_complex", "controlled_agriculture", "water_reclamation", "grid_battery",
         "cargo_terminal", "advanced_power_generator", "advanced_science_lab", "advanced_fabricator",
         "advanced_trade_hub", "advanced_habitat_complex"}) {
      for (float rotation : {0.f, 90.f}) {
        auto state = building(type); state.rotation_degrees = rotation;
        const auto key = surface::normalize_surface_building_state(state);
        const auto result = surface::prepare_surface_building_raster(*key.key, spec);
        if (!result) throw std::runtime_error(std::string(type) + " at " + std::to_string(dimension) + ": " + result.message);
        verify(result.prepared->pixel_tests <= surface::maximum_raster_pixel_tests, "Supported detail exceeded raster budget.");
        const auto bounds = result.prepared->projection.projected_bounds;
        verify(bounds.x > 0 && bounds.y > 0 && bounds.x + bounds.width < dimension && bounds.y + bounds.height < dimension,
               "Default building camera clips the model against its image boundary.");
        maximum_work = std::max(maximum_work, result.prepared->pixel_tests);
      }
    }
    for (int level : {1, 2, 3}) for (bool complete : {false, true}) {
      auto state = building(""); state.hub_level = level; state.capital = true;
      state.outpost = true; state.complete = complete; state.progress_fraction = .99;
      const auto key = surface::normalize_surface_building_state(state);
      const auto result = surface::prepare_surface_building_raster(*key.key, spec);
      if (!result) throw std::runtime_error("Hub " + std::to_string(level) + " at " + std::to_string(dimension) + ": " + result.message);
      verify(result.prepared->projection.canonical_footprint_radius == stellar::core::surface_hub_radius,
             "Hub footprint diverged from Core placement exclusion radius.");
      const auto bounds = result.prepared->projection.projected_bounds;
      verify(bounds.x > 0 && bounds.y > 0 && bounds.x + bounds.width < dimension && bounds.y + bounds.height < dimension,
             "Default hub camera clips the model against its image boundary.");
      maximum_work = std::max(maximum_work, result.prepared->pixel_tests);
    }
  }
  std::cout << "Building raster matrix: 68 variants, maximum pixel tests=" << maximum_work << '\n';
}

void invalid_and_unbounded_inputs_fail_cleanly() {
  auto invalid = building();
  invalid.rotation_degrees = std::numeric_limits<float>::quiet_NaN();
  verify(!surface::normalize_surface_building_state(invalid),
         "NaN rotation was accepted");
  invalid = building();
  invalid.progress_fraction = std::numeric_limits<double>::infinity();
  verify(!surface::prepare_geometry(invalid), "infinite progress was accepted");
  invalid = building(std::string(surface::maximum_type_id_bytes + 1, 'x'));
  verify(!surface::prepare_geometry(invalid), "oversize type ID was accepted");
  invalid = building();
  invalid.condition = -1;
  verify(!surface::prepare_geometry(invalid), "invalid condition was accepted");
  invalid = building("untrusted_unknown_type");
  verify(!surface::prepare_geometry(invalid), "unknown type was accepted");
  auto invalid_key = *surface::normalize_surface_building_state(building()).key;
  invalid_key.rotation_degrees = 360;
  verify(!surface::prepare_geometry(invalid_key),
         "noncanonical key yaw was accepted");
  invalid_key = *surface::normalize_surface_building_state(building()).key;
  invalid_key.construction_stage =
      static_cast<surface::ConstructionVisualStage>(255);
  verify(!surface::prepare_geometry(invalid_key),
         "invalid key construction stage was accepted");
  invalid_key = *surface::normalize_surface_building_state(building()).key;
  invalid_key.construction_stage = surface::ConstructionVisualStage::Foundation;
  verify(!surface::prepare_geometry(invalid_key), "Complete direct key accepted an unfinished stage.");
  invalid_key = *surface::normalize_surface_building_state(building()).key;
  invalid_key.capital = true;
  verify(!surface::prepare_geometry(invalid_key), "Non-hub key accepted capital identity.");

  surface::SurfaceBuildingRasterSpec spec;
  spec.width = surface::maximum_raster_dimension + 1;
  verify(!surface::validate_surface_building_raster_spec(spec),
         "oversize raster was accepted");
  spec = {};
  spec.camera.half_extent = std::numeric_limits<float>::quiet_NaN();
  verify(!surface::validate_surface_building_raster_spec(spec),
         "NaN camera was accepted");
  spec = {};
  spec.camera.world_up = spec.camera.eye;
  spec.camera.world_up = {0, 0, 0};
  verify(!surface::validate_surface_building_raster_spec(spec),
         "degenerate camera was accepted");

  auto excessive = std::make_shared<surface::SurfaceBuildingGeometry>();
  excessive->canonical_footprint_radius = 12;
  excessive->triangles.resize(surface::maximum_geometry_triangles + 1);
  verify(!surface::prepare_surface_building_raster(excessive, {}),
         "oversize triangle list was accepted");

  auto expensive = std::make_shared<surface::SurfaceBuildingGeometry>();
  expensive->canonical_footprint_radius = 12;
  const surface::Triangle screen_filling{{-30, 0, -30},
                                         {0, 0, 30},
                                         {30, 0, -30},
                                         {.5f, .5f, .5f, false}};
  expensive->triangles.assign(surface::maximum_geometry_triangles,
                              screen_filling);
  const auto rejected = surface::prepare_surface_building_raster(expensive, {});
  verify(!rejected &&
             rejected.error == surface::SurfaceBuildingRasterError::WorkBudgetExceeded,
         "excessive bounded raster work was not rejected before rasterization");
}

} // namespace

int main() {
  try {
    normalized_visual_keys_are_stable();
    rotation_anchor_and_bounds_are_preserved();
    primitive_faces_point_outward();
    construction_and_operational_states_are_visible();
    hub_levels_and_capital_state_are_distinct();
    raster_is_bounded_and_reports_metadata();
    supported_detail_matrix();
    invalid_and_unbounded_inputs_fail_cleanly();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "native surface building geometry test failed: "
              << error.what() << '\n';
    return 1;
  }
}
