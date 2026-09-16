#include "native_surface_building_geometry.hpp"
#include <stellar/core/surface_economy.hpp>
#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>
#include <utility>

namespace stellar::native_surface_building {
namespace {

constexpr float degrees = std::numbers::pi_v<float> / 180.f;
constexpr float maximum_coordinate = 10'000.f;

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

Vec3 rotate(Vec3 value, Vec3 rotation_degrees) {
  const auto rz = rotation_degrees.z * degrees;
  const auto rx = rotation_degrees.x * degrees;
  const auto ry = rotation_degrees.y * degrees;
  Vec3 result{value.x * std::cos(rz) - value.y * std::sin(rz),
              value.x * std::sin(rz) + value.y * std::cos(rz), value.z};
  result = {result.x,
            result.y * std::cos(rx) - result.z * std::sin(rx),
            result.y * std::sin(rx) + result.z * std::cos(rx)};
  return {result.x * std::cos(ry) + result.z * std::sin(ry), result.y,
          -result.x * std::sin(ry) + result.z * std::cos(ry)};
}

Material hex(std::uint32_t value, bool emissive = false) {
  return {static_cast<float>((value >> 16) & 0xffu) / 255.f,
          static_cast<float>((value >> 8) & 0xffu) / 255.f,
          static_cast<float>(value & 0xffu) / 255.f, emissive};
}

const Material shell = hex(0x35444d), metal = hex(0x18242b),
               bronze = hex(0x9a7040), solar = hex(0x071d34),
               glass = hex(0x071823), light = hex(0x70bed0, true),
               amber = hex(0xe39a42, true), concrete = hex(0x596064),
               offline = hex(0xc84c3f, true), scaffold = hex(0xbd954c),
               offline_ring = hex(0x6e2a20), workforce = hex(0x559fb3, true);

class Builder {
public:
  explicit Builder(float yaw) : yaw_{0, yaw, 0} {
    triangles_.reserve(768);
  }

  void triangle(Vec3 a, Vec3 b, Vec3 c, Material material) {
    if (triangles_.size() >= maximum_geometry_triangles) {
      overflow_ = true;
      return;
    }
    triangles_.push_back(
        {rotate(a, yaw_), rotate(b, yaw_), rotate(c, yaw_), material});
  }

  void box(Vec3 at, Vec3 size, Material material, Vec3 rotation = {}) {
    constexpr std::array<Vec3, 8> vertices{{
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}}};
    constexpr std::array<std::array<int, 3>, 12> faces{{
        {0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
        {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {0, 4, 7}, {0, 7, 3}}};
    const Vec3 half{size.x * .5f, size.y * .5f, size.z * .5f};
    const auto point = [&](int index) {
      return add(rotate(scale(vertices[static_cast<std::size_t>(index)], half),
                        rotation),
                 at);
    };
    for (const auto &face : faces)
      triangle(point(face[0]), point(face[1]), point(face[2]), material);
  }

  void cylinder(Vec3 at, float top, float bottom, float height,
                Material material, Vec3 rotation = {}, int segments = 20) {
    segments = std::clamp(segments, 3, 64);
    const auto point = [&](float radius, float y, int index) {
      const auto angle = 2.f * std::numbers::pi_v<float> * index / segments;
      return add(rotate({std::cos(angle) * radius, y,
                         std::sin(angle) * radius}, rotation), at);
    };
    const auto bottom_center = add(rotate({0, -height * .5f, 0}, rotation), at);
    const auto top_center = add(rotate({0, height * .5f, 0}, rotation), at);
    for (int index = 0; index < segments; ++index) {
      const auto next = (index + 1) % segments;
      const auto b0 = point(bottom, -height * .5f, index);
      const auto b1 = point(bottom, -height * .5f, next);
      const auto t0 = point(top, height * .5f, index);
      const auto t1 = point(top, height * .5f, next);
      triangle(b0, t1, b1, material);
      triangle(b0, t0, t1, material);
      triangle(bottom_center, b0, b1, material);
      triangle(top_center, t1, t0, material);
    }
  }

  void ellipsoid(Vec3 at, Vec3 radii, Material material, Vec3 rotation = {},
                 int segments = 16, int rings = 10) {
    segments = std::clamp(segments, 3, 48);
    rings = std::clamp(rings, 3, 32);
    const auto point = [&](int ring, int index) {
      const auto phi = std::numbers::pi_v<float> * ring / rings;
      const auto wrapped_index =
          ((index % segments) + segments) % segments;
      const auto theta = 2.f * std::numbers::pi_v<float> * wrapped_index /
                         segments;
      return add(rotate({std::cos(theta) * std::sin(phi) * radii.x,
                         -std::cos(phi) * radii.y,
                         std::sin(theta) * std::sin(phi) * radii.z}, rotation),
                 at);
    };
    const auto bottom = add(rotate({0, -radii.y, 0}, rotation), at);
    const auto top = add(rotate({0, radii.y, 0}, rotation), at);
    for (int index = 0; index < segments; ++index)
      triangle(bottom, point(1, index), point(1, index + 1), material);
    for (int ring = 1; ring < rings - 1; ++ring)
      for (int index = 0; index < segments; ++index) {
        triangle(point(ring, index), point(ring + 1, index + 1),
                 point(ring, index + 1), material);
        triangle(point(ring, index), point(ring + 1, index),
                 point(ring + 1, index + 1), material);
      }
    for (int index = 0; index < segments; ++index)
      triangle(point(rings - 1, index + 1), point(rings - 1, index), top,
               material);
  }

  [[nodiscard]] bool overflow() const noexcept { return overflow_; }
  std::vector<Triangle> take() { return std::move(triangles_); }

private:
  Vec3 yaw_{};
  std::vector<Triangle> triangles_;
  bool overflow_{};
};

float footprint(std::string_view type_id) {
  // The catalog owns placement size; a visual family must not invent it.
  return stellar::core::find_surface_building(type_id)->footprint_radius;
}

bool known_type(std::string_view type_id) {
  constexpr std::array<std::string_view, 14> types{
      "power_generator",          "science_lab",
      "fabricator",               "trade_hub",
      "habitat_complex",          "controlled_agriculture",
      "water_reclamation",        "grid_battery",
      "cargo_terminal",           "advanced_power_generator",
      "advanced_science_lab",     "advanced_fabricator",
      "advanced_trade_hub",       "advanced_habitat_complex",
  };
  return std::find(types.begin(), types.end(), type_id) != types.end() &&
         stellar::core::find_surface_building(type_id) != nullptr;
}

void generator(Builder &mesh) {
  mesh.cylinder({0, 5, 0}, 3.1f, 3.8f, 7, shell);
  for (int index = 0; index < 3; ++index)
    mesh.cylinder({0, 3.4f + index * 2.f, 0}, 3.35f, 3.35f, .38f, light);
  for (int side : {-1, 1}) {
    const auto value = static_cast<float>(side);
    mesh.box({value * 7, 3, 0}, {1, 3.6f, 1}, bronze);
    mesh.box({value * 7, 5, 0}, {6.2f, .25f, 10}, solar,
             {0, 0, value * -12.6f});
  }
}

void lab(Builder &mesh) {
  mesh.cylinder({0, 3, 0}, 8, 9, 3.5f, shell);
  mesh.ellipsoid({0, 4.2f, 0}, {7.4f, 5.55f, 7.4f}, glass);
  mesh.cylinder({0, 4.6f, 0}, 8, 8, .3f, light);
  for (int side : {-1, 1}) {
    const auto value = static_cast<float>(side);
    mesh.box({value * 9, 3.1f, 0}, {5, 3.4f, 8}, shell);
    mesh.box({value * 11.55f, 3.8f, 0}, {.15f, 1.4f, 5}, glass);
  }
  mesh.cylinder({5, 10, 5}, .2f, .35f, 6, metal, {}, 12);
  mesh.ellipsoid({5, 13, 5}, {2.2f, .55f, 2.2f}, shell, {20, 0, 17});
}

void fabricator(Builder &mesh) {
  mesh.box({0, 4.5f, 0}, {20, 6, 15}, shell);
  mesh.box({0, 7.85f, 0}, {21, .7f, 16}, metal);
  mesh.box({0, 3.8f, 7.65f}, {9, 4.5f, .25f}, glass);
  for (int index = -2; index <= 2; ++index)
    mesh.box({index * 1.8f, 3.8f, 7.85f}, {.3f, 4.5f, .4f}, bronze);
  for (int side : {-1, 1}) {
    const auto value = static_cast<float>(side);
    mesh.box({value * 12, 7.2f, 0}, {.8f, 13, .8f}, bronze);
    mesh.cylinder({value * 5, 10, -4}, 1.7f, 1.7f, 4, metal);
  }
  mesh.box({0, 13.6f, 0}, {25, 1, 1.8f}, bronze);
  mesh.box({2, 12.7f, 0}, {3, 1, 2.3f}, metal);
  mesh.box({2, 10.7f, 0}, {.15f, 3, .15f}, light);
}

void trade_hub(Builder &mesh) {
  mesh.cylinder({0, 1.7f, 0}, 9, 10, 2.2f, shell, {}, 12);
  for (int level = 0; level < 3; ++level) {
    const auto radius = 7.2f - level * 1.25f;
    mesh.cylinder({0, 4.2f + level * 2.35f, 0}, radius, radius + .45f, 2.4f,
                  level == 1 ? glass : metal, {}, 12);
  }
  for (int side = 0; side < 4; ++side) {
    const auto angle = side * std::numbers::pi_v<float> * .5f;
    mesh.box({std::cos(angle) * 10.5f, 3.2f, std::sin(angle) * 10.5f},
             {5.5f, 3.8f, 3.2f}, glass, {0, -angle / degrees, 0});
  }
  mesh.cylinder({0, 12, 0}, .35f, .5f, 7, bronze, {}, 10);
  mesh.ellipsoid({0, 16, 0}, {1.25f, 1.25f, 1.25f}, light);
}

void habitat(Builder &mesh) {
  mesh.cylinder({0, 1.3f, 0}, 10, 11, 1.8f, metal);
  for (int index = 0; index < 3; ++index) {
    const auto angle = index * 2.f * std::numbers::pi_v<float> / 3.f;
    mesh.ellipsoid({std::cos(angle) * 6.5f, 4.1f,
                    std::sin(angle) * 6.5f},
                   {5.4f, 3.348f, 5.4f}, index == 0 ? glass : shell);
  }
  mesh.cylinder({0, 7, 0}, 2.2f, 2.8f, 8, bronze, {}, 12);
  mesh.ellipsoid({0, 11.5f, 0}, {1.1f, 1.1f, 1.1f}, light);
}

void battery(Builder &mesh) {
  for (int bank = -1; bank <= 1; ++bank) {
    const auto x = bank * 5.8f;
    mesh.box({x, 4.3f, 0}, {5.2f, 5.8f, 3.4f}, shell);
    for (int level = 0; level < 4; ++level)
      mesh.box({x, 2 + level * 1.55f, 0}, {4.5f, .18f, 3.48f}, light);
  }
  mesh.cylinder({0, 6.1f, 0}, 1.1f, 1.35f, 8.5f, bronze, {}, 12);
}

void cargo(Builder &mesh) {
  mesh.box({0, 1.2f, 0}, {22, 1.2f, 15}, metal);
  for (int row = -1; row <= 1; ++row)
    for (int column = -2; column <= 2; ++column)
      mesh.box({column * 3.7f, 2.9f, row * 3.3f}, {3.2f, 2.2f, 2.6f},
               (row + column) % 2 == 0 ? bronze : shell);
  for (int side : {-1, 1}) {
    const auto value = static_cast<float>(side);
    mesh.box({value * 10.5f, 7, -6}, {.8f, 11, .8f}, bronze);
    mesh.box({value * 6.5f, 12, -6}, {9, .7f, .8f}, bronze);
    mesh.box({value * 2.5f, 8.5f, -6}, {.5f, 7, .5f}, metal);
  }
  mesh.box({0, 4.1f, 7}, {7, 4.5f, 5}, glass);
  mesh.ellipsoid({0, 8.2f, 7}, {.9f, .9f, .9f}, light);
}

void generic(Builder &mesh, float radius) {
  mesh.cylinder({0, 4, 0}, radius * .5f, radius * .62f, 6, shell, {}, 8);
  mesh.box({0, 8.4f, 0}, {radius * .7f, 1.2f, radius * .7f}, metal);
  mesh.ellipsoid({0, 9.6f, 0}, {.8f, .8f, .8f}, light);
}

void scaffolding(Builder &mesh, float radius,
                 ConstructionVisualStage construction_stage) {
  for (float x : {-radius * .72f, radius * .72f})
    for (float z : {-radius * .72f, radius * .72f})
      mesh.box({x, 6.5f, z}, {.24f, 13, .24f}, scaffold);
  for (int level = 1; level <= 3; ++level)
    for (int side : {-1, 1}) {
      const auto value = static_cast<float>(side);
      mesh.box({0, level * 4.f, value * radius * .72f},
               {radius * 1.44f, .2f, .2f}, scaffold);
      mesh.box({value * radius * .72f, level * 4.f, 0},
               {.2f, .2f, radius * 1.44f}, scaffold);
    }
  if (construction_stage != ConstructionVisualStage::Foundation) {
    mesh.box({0, 13.6f, 0}, {radius * 1.45f, .12f, .3f}, light);
    mesh.box({0, 13.6f, 0}, {.28f, .28f, radius * 1.18f}, amber);
  }
  if (construction_stage == ConstructionVisualStage::Fitted)
    for (int side : {-1, 1}) {
      const auto value = static_cast<float>(side);
      mesh.box({value * radius * .74f, 9.5f, 0},
               {.12f, 5.5f, radius * 1.3f}, amber,
               {0, 0, value * 8.f});
    }
}

void building(Builder &mesh, std::string_view type_id, bool structure_started) {
  const auto advanced = type_id.starts_with("advanced_");
  const auto base = advanced ? type_id.substr(9) : type_id;
  if (structure_started) {
    if (base == "power_generator") generator(mesh);
    else if (base == "science_lab") lab(mesh);
    else if (base == "fabricator" || base == "water_reclamation") fabricator(mesh);
    else if (base == "trade_hub") trade_hub(mesh);
    else if (base == "habitat_complex" || base == "controlled_agriculture") habitat(mesh);
    else if (base == "grid_battery") battery(mesh);
    else if (base == "cargo_terminal") cargo(mesh);
    else generic(mesh, footprint(type_id));
    if (advanced) {
      const auto radius = footprint(type_id);
      mesh.cylinder({0, 12.4f, 0}, radius * .52f, radius * .52f, .3f, light,
                    {}, 24);
      for (int index = 0; index < 4; ++index) {
        const auto angle = index * std::numbers::pi_v<float> * .5f;
        mesh.ellipsoid({std::cos(angle) * radius * .62f, 10.6f,
                        std::sin(angle) * radius * .62f},
                       {.7f, .7f, .7f}, amber);
      }
    }
  }
}

void hub(Builder &mesh, int level, bool capital, bool outpost) {
  mesh.cylinder({0, .6f, 0}, 19, 20, 1.2f, metal, {}, 8);
  mesh.cylinder({0, 3.7f, 0}, 12, 14, 5, shell, {}, 8);
  mesh.cylinder({0, 6.7f, 0}, 8, 11, 1, metal, {}, 8);
  mesh.cylinder({0, 8.7f, 0}, 5, 7, 3, glass, {}, 8);
  mesh.cylinder({0, 10.4f, 0}, 5.8f, 5.8f, .35f, light, {}, 8);
  mesh.cylinder({0, 15.7f, 0}, .35f, .65f, 11, shell, {}, 12);
  mesh.box({0, 19, 0}, {7, .4f, .4f}, metal);
  mesh.ellipsoid({0, 22, 0}, {1.5f, 1.5f, 1.5f}, light);
  for (int step = 0; step < 3; ++step)
    mesh.box({0, .18f + step * .26f, 19.8f + step * 1.7f},
             {7.5f + step * 1.7f, .34f, 2.4f}, concrete);
  mesh.box({0, 3, 14.15f}, {5.2f, 3.4f, .32f}, glass);
  for (int index = 0; index < 4; ++index) {
    const auto angle = index * std::numbers::pi_v<float> * .5f;
    mesh.box({std::sin(angle) * 16, 1.35f, std::cos(angle) * 16},
             {4.5f, .22f, 8}, bronze, {0, angle / degrees, 0});
  }
  if (level >= 2) {
    mesh.cylinder({0, 1.25f, 0}, 22.5f, 22.5f, .22f, bronze, {}, 48);
    for (int index = 0; index < 4; ++index) {
      const auto angle = index * std::numbers::pi_v<float> * .5f + .78539816f;
      const auto x = std::sin(angle) * 14.5f;
      const auto z = std::cos(angle) * 14.5f;
      mesh.cylinder({x, 5.3f, z}, 2.2f, 2.8f, 8.5f, shell, {}, 10);
      mesh.cylinder({x, 9.9f, z}, 1.1f, 1.35f, .7f, metal, {}, 12);
    }
  }
  if (level >= 3) {
    mesh.cylinder({0, 12.2f, 0}, 8.8f, 8.8f, .45f,
                  capital ? bronze : metal, {}, 32);
    for (int index = 0; index < 8; ++index) {
      const auto angle = index * std::numbers::pi_v<float> * .25f;
      mesh.ellipsoid({std::sin(angle) * 8.2f, 12.7f,
                      std::cos(angle) * 8.2f}, {.42f, .42f, .42f}, light);
    }
    mesh.cylinder({3.4f, 16.2f, 0}, .12f, .18f, 6, metal, {}, 8);
    mesh.ellipsoid({3.4f, 19.3f, 0}, {2.125f, .375f, 2.125f}, glass);
  }
  if (outpost)
    for (int index = 0; index < 4; ++index) {
      const auto angle = index * std::numbers::pi_v<float> * .5f;
      mesh.ellipsoid({std::sin(angle) * 20, 1.8f, std::cos(angle) * 20},
                     {.38f, .38f, .38f}, amber);
    }
}

bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::abs(value.x) <= maximum_coordinate &&
         std::abs(value.y) <= maximum_coordinate &&
         std::abs(value.z) <= maximum_coordinate;
}

} // namespace

SurfaceBuildingGeometryResult
prepare_geometry(const SurfaceBuildingStateKey &state) {
  const auto valid_stage =
      state.construction_stage == ConstructionVisualStage::Foundation ||
      state.construction_stage == ConstructionVisualStage::Structure ||
      state.construction_stage == ConstructionVisualStage::Fitted;
  const auto valid_condition =
      state.condition_state == ConditionVisualState::Normal ||
      state.condition_state == ConditionVisualState::Repair ||
      state.condition_state == ConditionVisualState::Critical;
  if (state.type_id.size() > maximum_type_id_bytes ||
      (!state.hub_level && state.type_id.empty()) ||
      (!state.hub_level && !known_type(state.type_id)) ||
      (state.hub_level && !state.type_id.empty()) ||
      !std::isfinite(state.rotation_degrees) ||
      state.rotation_degrees < 0 || state.rotation_degrees >= 360 ||
      state.hub_level < 0 || state.hub_level > 3 || !valid_stage ||
      !valid_condition ||
      (state.complete && state.construction_stage != ConstructionVisualStage::Fitted) ||
      (state.capital && state.hub_level < 3) || (state.outpost && !state.hub_level))
    return {nullptr, SurfaceBuildingGeometryError::InvalidState,
            "Surface building state is invalid or unbounded."};

  Builder builder(state.rotation_degrees);
  const auto radius = state.hub_level > 0 ? (state.hub_level >= 2 ? 23.f : 20.f)
                                          : footprint(state.type_id);
  builder.cylinder({0, .7f, 0}, radius * .85f, radius * .91f, 1.4f, metal,
                   {}, 8);
  const auto structure_started =
      state.complete ||
      state.construction_stage != ConstructionVisualStage::Foundation;
  if (state.hub_level > 0) {
    if (structure_started)
      hub(builder, state.hub_level, state.capital, state.outpost);
  } else {
    building(builder, state.type_id, structure_started);
  }
  if (!state.complete)
    scaffolding(builder, radius, state.construction_stage);

  const auto operational = state.complete && state.powered && state.enabled;
  if (structure_started)
    builder.ellipsoid({0, 13, 0}, {.6f, .6f, .6f},
                      operational ? light : offline);
  if (!state.enabled || !state.powered)
    builder.cylinder({0, 1.55f, 0}, radius * .94f, radius * .94f, .2f,
                     offline_ring, {}, 24);
  if (state.complete && !state.staffed)
    builder.cylinder({0, 1.85f, 0}, radius * .8f, radius * .8f, .18f,
                     workforce, {}, 24);
  if (state.condition_state != ConditionVisualState::Normal) {
    const auto critical =
        state.condition_state == ConditionVisualState::Critical;
    const auto condition_radius = radius * (critical ? .57f : .72f);
    builder.cylinder({0, 2.15f, 0}, condition_radius, condition_radius, .16f,
                     critical ? offline : amber, {}, 24);
  }

  if (builder.overflow())
    return {nullptr, SurfaceBuildingGeometryError::GeometryBudgetExceeded,
            "Surface building geometry exceeded its triangle budget."};
  auto geometry = std::make_shared<SurfaceBuildingGeometry>();
  geometry->state = state;
  geometry->triangles = builder.take();
  geometry->canonical_footprint_radius = state.hub_level > 0
      ? stellar::core::surface_hub_radius : radius;
  geometry->has_scaffolding = !state.complete;
  if (geometry->triangles.empty())
    return {nullptr, SurfaceBuildingGeometryError::InvalidGeometry,
            "Surface building geometry is empty."};

  const auto high = std::numeric_limits<float>::max();
  geometry->model_bounds.minimum = {high, high, high};
  geometry->model_bounds.maximum = {-high, -high, -high};
  for (const auto &triangle : geometry->triangles)
    for (const auto point : {triangle.a, triangle.b, triangle.c}) {
      if (!finite(point))
        return {nullptr, SurfaceBuildingGeometryError::InvalidGeometry,
                "Surface building geometry contains invalid coordinates."};
      geometry->model_bounds.minimum.x = std::min(geometry->model_bounds.minimum.x, point.x);
      geometry->model_bounds.minimum.y = std::min(geometry->model_bounds.minimum.y, point.y);
      geometry->model_bounds.minimum.z = std::min(geometry->model_bounds.minimum.z, point.z);
      geometry->model_bounds.maximum.x = std::max(geometry->model_bounds.maximum.x, point.x);
      geometry->model_bounds.maximum.y = std::max(geometry->model_bounds.maximum.y, point.y);
      geometry->model_bounds.maximum.z = std::max(geometry->model_bounds.maximum.z, point.z);
    }
  return {std::move(geometry), SurfaceBuildingGeometryError::None, {}};
}

SurfaceBuildingStateResult
normalize_surface_building_state(const SurfaceBuildingState &state) {
  if (state.type_id.size() > maximum_type_id_bytes ||
      (!state.hub_level && state.type_id.empty()) ||
      (!state.hub_level && !known_type(state.type_id)) ||
      (state.hub_level && !state.type_id.empty()) ||
      !std::isfinite(state.rotation_degrees) ||
      std::abs(state.rotation_degrees) > 1'000'000.f ||
      !std::isfinite(state.progress_fraction) ||
      state.progress_fraction < 0 || state.progress_fraction > 1 ||
      !std::isfinite(state.condition) || state.condition < 0 ||
      state.condition > 1 || state.hub_level < 0 || state.hub_level > 3)
    return {std::nullopt, SurfaceBuildingGeometryError::InvalidState,
            "Surface building state is invalid or unbounded."};

  auto stage = ConstructionVisualStage::Foundation;
  if (state.complete || state.progress_fraction >= 2. / 3.)
    stage = ConstructionVisualStage::Fitted;
  else if (state.progress_fraction >= 1. / 3.)
    stage = ConstructionVisualStage::Structure;
  auto condition = ConditionVisualState::Normal;
  if (state.condition < .35)
    condition = ConditionVisualState::Critical;
  else if (state.condition < .75)
    condition = ConditionVisualState::Repair;
  auto rotation = std::fmod(std::fmod(state.rotation_degrees, 360.f) + 360.f,
                            360.f);
  if (rotation == 0)
    rotation = 0;

  return {SurfaceBuildingStateKey{
              state.type_id,
              rotation,
              state.complete,
              stage,
              state.powered,
              state.enabled,
              state.staffed,
              condition,
              state.hub_level,
              state.capital && state.hub_level >= 3,
              state.outpost && state.hub_level > 0,
          },
          SurfaceBuildingGeometryError::None,
          {}};
}

SurfaceBuildingGeometryResult
prepare_geometry(const SurfaceBuildingState &state) {
  auto normalized = normalize_surface_building_state(state);
  if (!normalized)
    return {nullptr, normalized.error, std::move(normalized.message)};
  return prepare_geometry(*normalized.key);
}

} // namespace stellar::native_surface_building
