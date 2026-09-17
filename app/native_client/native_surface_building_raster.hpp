#pragma once

#include "native_surface_building_geometry.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace stellar::native_surface_building {

inline constexpr int maximum_raster_dimension = 512;
inline constexpr std::size_t maximum_raster_pixels = 512u * 512u;
inline constexpr std::size_t maximum_raster_pixel_tests = 8'000'000;

// A fixed-oblique orthographic camera is the default presentation. This is a
// bounded sprite projection, not a navigable 3D camera.
struct SurfaceBuildingRasterCamera {
  Vec3 eye{34, 34, 34};
  Vec3 target{0, 7, 0};
  Vec3 world_up{0, 1, 0};
  float half_extent{31};
  Vec3 light_direction{-.42f, -.78f, -.46f};
  float brightness{1};

  bool operator==(const SurfaceBuildingRasterCamera &) const = default;
};

struct SurfaceBuildingRasterSpec {
  int width{128};
  int height{128};
  SurfaceBuildingRasterCamera camera{};

  bool operator==(const SurfaceBuildingRasterSpec &) const = default;
};

struct SurfaceBuildingProjection {
  native_map::Point ground_anchor_pixels{};
  native_map::UiRect projected_bounds{};
  float canonical_footprint_radius{};
};

enum class SurfaceBuildingRasterError {
  None,
  InvalidState,
  InvalidGeometry,
  InvalidSpec,
  WorkBudgetExceeded,
  ImageCreationFailed,
};

struct SurfaceBuildingRasterSpecResult {
  std::optional<SurfaceBuildingRasterSpec> spec;
  SurfaceBuildingRasterError error{SurfaceBuildingRasterError::None};
  std::string message;
  [[nodiscard]] explicit operator bool() const noexcept {
    return spec.has_value();
  }
};

struct SurfaceBuildingProjectionResult {
  std::optional<SurfaceBuildingProjection> projection;
  SurfaceBuildingRasterError error{SurfaceBuildingRasterError::None};
  std::string message;
  [[nodiscard]] explicit operator bool() const noexcept {
    return projection.has_value();
  }
};

struct PreparedSurfaceBuildingRaster {
  SurfaceBuildingStateKey state;
  std::shared_ptr<const native_map::RgbaImage> image;
  SurfaceBuildingProjection projection;
  std::size_t input_triangles{};
  std::size_t rasterized_triangles{};
  std::size_t pixel_tests{};
};

struct SurfaceBuildingRasterResult {
  std::shared_ptr<const PreparedSurfaceBuildingRaster> prepared;
  SurfaceBuildingRasterError error{SurfaceBuildingRasterError::None};
  std::string message;
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(prepared);
  }
};

// Cheap validation suitable for the owner thread before queue admission.
[[nodiscard]] SurfaceBuildingRasterSpecResult
validate_surface_building_raster_spec(const SurfaceBuildingRasterSpec &spec);

// Computes placement metadata without allocating an image or depth buffer.
[[nodiscard]] SurfaceBuildingProjectionResult
project_surface_building(const SurfaceBuildingGeometry &geometry,
                         const SurfaceBuildingRasterSpec &spec);

[[nodiscard]] SurfaceBuildingRasterResult
prepare_surface_building_raster(
    std::shared_ptr<const SurfaceBuildingGeometry> geometry,
    const SurfaceBuildingRasterSpec &spec);

// Worker-friendly end-to-end entry point. It captures only immutable values
// and performs both geometry and raster preparation off the owner draw path.
[[nodiscard]] SurfaceBuildingRasterResult
prepare_surface_building_raster(const SurfaceBuildingStateKey &state,
                                const SurfaceBuildingRasterSpec &spec);

} // namespace stellar::native_surface_building
