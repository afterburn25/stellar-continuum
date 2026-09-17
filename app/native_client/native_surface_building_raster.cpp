#include "native_surface_building_raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace stellar::native_surface_building {
namespace {

constexpr float maximum_camera_coordinate = 10'000.f;
constexpr float minimum_vector_length_squared = 1.e-6f;
constexpr float maximum_projected_coordinate = 1.e7f;

Vec3 subtract(Vec3 a, Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 multiply(Vec3 value, float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}
float dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float length_squared(Vec3 value) { return dot(value, value); }
Vec3 normalized(Vec3 value) {
  const auto length = std::sqrt(length_squared(value));
  return multiply(value, 1.f / length);
}
bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) &&
         std::abs(value.x) <= maximum_camera_coordinate &&
         std::abs(value.y) <= maximum_camera_coordinate &&
         std::abs(value.z) <= maximum_camera_coordinate;
}
bool finite(Material value) {
  return std::isfinite(value.r) && std::isfinite(value.g) &&
         std::isfinite(value.b) && value.r >= 0 && value.r <= 1 &&
         value.g >= 0 && value.g <= 1 && value.b >= 0 && value.b <= 1;
}

struct CameraBasis {
  Vec3 forward{}, right{}, up{}, light{};
};

std::optional<CameraBasis> basis(const SurfaceBuildingRasterCamera &camera) {
  if (!finite(camera.eye) || !finite(camera.target) ||
      !finite(camera.world_up) || !finite(camera.light_direction) ||
      !std::isfinite(camera.half_extent) || camera.half_extent < .1f ||
      camera.half_extent > maximum_camera_coordinate ||
      !std::isfinite(camera.brightness) || camera.brightness < .1f ||
      camera.brightness > 4.f)
    return std::nullopt;
  const auto view = subtract(camera.target, camera.eye);
  if (length_squared(view) < minimum_vector_length_squared ||
      length_squared(camera.world_up) < minimum_vector_length_squared ||
      length_squared(camera.light_direction) < minimum_vector_length_squared)
    return std::nullopt;
  const auto forward = normalized(view);
  const auto view_right = cross(forward, camera.world_up);
  if (length_squared(view_right) < minimum_vector_length_squared)
    return std::nullopt;
  const auto right = normalized(view_right);
  return CameraBasis{forward, right, cross(right, forward),
                     normalized(camera.light_direction)};
}

bool valid_geometry(const SurfaceBuildingGeometry &geometry) {
  if (geometry.triangles.empty() ||
      geometry.triangles.size() > maximum_geometry_triangles ||
      !std::isfinite(geometry.canonical_footprint_radius) ||
      geometry.canonical_footprint_radius <= 0 ||
      geometry.canonical_footprint_radius > maximum_camera_coordinate ||
      !finite(geometry.ground_anchor))
    return false;
  for (const auto &triangle : geometry.triangles)
    if (!finite(triangle.a) || !finite(triangle.b) || !finite(triangle.c) ||
        !finite(triangle.material))
      return false;
  return true;
}

Vec3 project_point(Vec3 point, const SurfaceBuildingRasterSpec &spec,
                   const CameraBasis &camera) {
  const auto relative = subtract(point, spec.camera.eye);
  const auto cx = dot(relative, camera.right);
  const auto cy = dot(relative, camera.up);
  return {(cx / spec.camera.half_extent + 1.f) * .5f * spec.width,
          (1.f - (cy / spec.camera.half_extent + 1.f) * .5f) * spec.height,
          dot(relative, camera.forward)};
}

bool finite_projection(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) &&
         std::abs(value.x) <= maximum_projected_coordinate &&
         std::abs(value.y) <= maximum_projected_coordinate &&
         std::abs(value.z) <= maximum_projected_coordinate;
}

SurfaceBuildingRasterResult failure(SurfaceBuildingRasterError error,
                                    std::string message) {
  return {nullptr, error, std::move(message)};
}

} // namespace

SurfaceBuildingRasterSpecResult validate_surface_building_raster_spec(
    const SurfaceBuildingRasterSpec &spec) {
  const auto pixels = static_cast<std::uint64_t>(std::max(0, spec.width)) *
                      static_cast<std::uint64_t>(std::max(0, spec.height));
  if (spec.width <= 0 || spec.height <= 0 ||
      spec.width > maximum_raster_dimension ||
      spec.height > maximum_raster_dimension ||
      pixels > maximum_raster_pixels || !basis(spec.camera))
    return {std::nullopt, SurfaceBuildingRasterError::InvalidSpec,
            "Surface building raster specification is invalid or unbounded."};
  return {spec, SurfaceBuildingRasterError::None, {}};
}

SurfaceBuildingProjectionResult project_surface_building(
    const SurfaceBuildingGeometry &geometry,
    const SurfaceBuildingRasterSpec &spec) {
  const auto checked = validate_surface_building_raster_spec(spec);
  const auto camera = basis(spec.camera);
  if (!checked || !camera)
    return {std::nullopt, SurfaceBuildingRasterError::InvalidSpec,
            "Surface building raster specification is invalid or unbounded."};
  if (!valid_geometry(geometry))
    return {std::nullopt, SurfaceBuildingRasterError::InvalidGeometry,
            "Surface building geometry is invalid or unbounded."};

  auto minimum_x = std::numeric_limits<float>::max();
  auto minimum_y = std::numeric_limits<float>::max();
  auto maximum_x = -std::numeric_limits<float>::max();
  auto maximum_y = -std::numeric_limits<float>::max();
  for (const auto &triangle : geometry.triangles)
    for (const auto point : {triangle.a, triangle.b, triangle.c}) {
      const auto projected = project_point(point, spec, *camera);
      if (!finite_projection(projected) || projected.z <= 0)
        return {std::nullopt, SurfaceBuildingRasterError::InvalidGeometry,
                "Surface building geometry cannot be projected safely."};
      minimum_x = std::min(minimum_x, projected.x);
      minimum_y = std::min(minimum_y, projected.y);
      maximum_x = std::max(maximum_x, projected.x);
      maximum_y = std::max(maximum_y, projected.y);
    }
  const auto anchor = project_point(geometry.ground_anchor, spec, *camera);
  if (!finite_projection(anchor) || anchor.z <= 0)
    return {std::nullopt, SurfaceBuildingRasterError::InvalidGeometry,
            "Surface building ground anchor cannot be projected safely."};

  const auto left = std::clamp(minimum_x, 0.f, static_cast<float>(spec.width));
  const auto top = std::clamp(minimum_y, 0.f, static_cast<float>(spec.height));
  const auto right = std::clamp(maximum_x, 0.f,
                                static_cast<float>(spec.width));
  const auto bottom = std::clamp(maximum_y, 0.f,
                                 static_cast<float>(spec.height));
  if (right <= left || bottom <= top)
    return {std::nullopt, SurfaceBuildingRasterError::InvalidGeometry,
            "Surface building projection does not intersect the image."};
  return {SurfaceBuildingProjection{{anchor.x, anchor.y},
                                    {left, top, right - left, bottom - top},
                                    geometry.canonical_footprint_radius},
          SurfaceBuildingRasterError::None,
          {}};
}

SurfaceBuildingRasterResult prepare_surface_building_raster(
    std::shared_ptr<const SurfaceBuildingGeometry> geometry,
    const SurfaceBuildingRasterSpec &spec) {
  if (!geometry)
    return failure(SurfaceBuildingRasterError::InvalidGeometry,
                   "Surface building geometry is missing.");
  const auto projected_metadata = project_surface_building(*geometry, spec);
  if (!projected_metadata)
    return failure(projected_metadata.error, projected_metadata.message);
  const auto camera = basis(spec.camera);
  if (!camera)
    return failure(SurfaceBuildingRasterError::InvalidSpec,
                   "Surface building raster camera is invalid.");

  struct ProjectedTriangle {
    std::array<Vec3, 3> points{};
    Vec3 normal{};
    Material material{};
    int minimum_x{}, minimum_y{}, maximum_x{}, maximum_y{};
  };
  std::vector<ProjectedTriangle> triangles;
  triangles.reserve(geometry->triangles.size());
  std::size_t pixel_tests{};
  for (const auto &triangle : geometry->triangles) {
    const auto normal = normalized(cross(subtract(triangle.b, triangle.a),
                                         subtract(triangle.c, triangle.a)));
    if (!finite(normal) || dot(normal, camera->forward) >= 0)
      continue;
    ProjectedTriangle projected{{project_point(triangle.a, spec, *camera),
                                 project_point(triangle.b, spec, *camera),
                                 project_point(triangle.c, spec, *camera)},
                                normal,
                                triangle.material};
    const auto low_x = std::min({projected.points[0].x, projected.points[1].x,
                                 projected.points[2].x});
    const auto low_y = std::min({projected.points[0].y, projected.points[1].y,
                                 projected.points[2].y});
    const auto high_x = std::max({projected.points[0].x, projected.points[1].x,
                                  projected.points[2].x});
    const auto high_y = std::max({projected.points[0].y, projected.points[1].y,
                                  projected.points[2].y});
    if (high_x < 0 || high_y < 0 || low_x >= spec.width ||
        low_y >= spec.height)
      continue;
    const auto bounded_low_x = std::clamp(low_x, 0.f, spec.width - 1.f);
    const auto bounded_low_y = std::clamp(low_y, 0.f, spec.height - 1.f);
    const auto bounded_high_x = std::clamp(high_x, 0.f, spec.width - 1.f);
    const auto bounded_high_y = std::clamp(high_y, 0.f, spec.height - 1.f);
    projected.minimum_x = static_cast<int>(std::floor(bounded_low_x));
    projected.minimum_y = static_cast<int>(std::floor(bounded_low_y));
    projected.maximum_x = static_cast<int>(std::ceil(bounded_high_x));
    projected.maximum_y = static_cast<int>(std::ceil(bounded_high_y));
    const auto work = static_cast<std::size_t>(projected.maximum_x -
                                               projected.minimum_x + 1) *
                      static_cast<std::size_t>(projected.maximum_y -
                                               projected.minimum_y + 1);
    if (work > maximum_raster_pixel_tests - pixel_tests)
      return failure(SurfaceBuildingRasterError::WorkBudgetExceeded,
                     "Surface building raster exceeded its pixel-test budget.");
    pixel_tests += work;
    triangles.push_back(projected);
  }

  const auto pixels_count = static_cast<std::size_t>(spec.width) * spec.height;
  std::vector<std::uint8_t> pixels(pixels_count * 4, 0);
  std::vector<float> depth(pixels_count,
                           std::numeric_limits<float>::infinity());
  const auto edge = [](Vec3 a, Vec3 b, float x, float y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
  };
  std::size_t rasterized{};
  for (const auto &triangle : triangles) {
    const auto area = edge(triangle.points[0], triangle.points[1],
                           triangle.points[2].x, triangle.points[2].y);
    if (!std::isfinite(area) || std::abs(area) < 1.e-6f)
      continue;
    ++rasterized;
    const auto lambert =
        std::max(0.f, -dot(triangle.normal, camera->light));
    const auto shade = [&](float channel) {
      const auto lit = channel * (.34f + 1.45f * lambert) +
                       (triangle.material.emissive ? channel * 1.15f : 0.f);
      return static_cast<std::uint8_t>(
          std::clamp(lit * spec.camera.brightness, 0.f, 1.f) * 255.f);
    };
    const auto red = shade(triangle.material.r);
    const auto green = shade(triangle.material.g);
    const auto blue = shade(triangle.material.b);
    for (auto y = triangle.minimum_y; y <= triangle.maximum_y; ++y)
      for (auto x = triangle.minimum_x; x <= triangle.maximum_x; ++x) {
        const auto px = static_cast<float>(x) + .5f;
        const auto py = static_cast<float>(y) + .5f;
        const auto w0 = edge(triangle.points[1], triangle.points[2], px, py) /
                        area;
        const auto w1 = edge(triangle.points[2], triangle.points[0], px, py) /
                        area;
        const auto w2 = edge(triangle.points[0], triangle.points[1], px, py) /
                        area;
        if (w0 < 0 || w1 < 0 || w2 < 0)
          continue;
        const auto z = w0 * triangle.points[0].z +
                       w1 * triangle.points[1].z +
                       w2 * triangle.points[2].z;
        const auto texel = static_cast<std::size_t>(y) * spec.width + x;
        if (z >= depth[texel])
          continue;
        depth[texel] = z;
        const auto offset = texel * 4;
        pixels[offset] = red;
        pixels[offset + 1] = green;
        pixels[offset + 2] = blue;
        pixels[offset + 3] = 255;
      }
  }
  auto image = native_map::RgbaImage::create(spec.width, spec.height,
                                              std::move(pixels));
  if (!image)
    return failure(SurfaceBuildingRasterError::ImageCreationFailed,
                   "Surface building image creation failed.");
  auto prepared = std::make_shared<PreparedSurfaceBuildingRaster>();
  prepared->state = geometry->state;
  prepared->image = std::move(image);
  prepared->projection = *projected_metadata.projection;
  prepared->input_triangles = geometry->triangles.size();
  prepared->rasterized_triangles = rasterized;
  prepared->pixel_tests = pixel_tests;
  return {std::move(prepared), SurfaceBuildingRasterError::None, {}};
}

SurfaceBuildingRasterResult prepare_surface_building_raster(
    const SurfaceBuildingStateKey &state,
    const SurfaceBuildingRasterSpec &spec) {
  auto geometry = prepare_geometry(state);
  if (!geometry)
    return failure(SurfaceBuildingRasterError::InvalidState,
                   std::move(geometry.message));
  return prepare_surface_building_raster(std::move(geometry.geometry), spec);
}

} // namespace stellar::native_surface_building
