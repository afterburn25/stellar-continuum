#pragma once

#include "native_surface_building_raster.hpp"
#include "native_surface_viewport.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <memory>
#include <optional>

namespace stellar::native_colony_ui {

struct PlacedSurfaceBuildingImage {
  stellar::native_map::Image image;
  stellar::native_map::UiRect projected_bounds{};
  stellar::native_map::Point ground_screen{};
  float depth{};
};

// Pure placement of an already prepared fixed-oblique sprite. No cache,
// preparation, upload, or campaign access occurs here.
[[nodiscard]] std::optional<PlacedSurfaceBuildingImage>
place_surface_building_image(
    const std::shared_ptr<const
        stellar::native_surface_building::PreparedSurfaceBuildingRaster> &,
    const stellar::native_surface_building::SurfaceBuildingStateKey &expected,
    const stellar::native_surface_building::SurfaceBuildingRasterSpec &,
    float world_x, float world_z, const SurfaceViewport &,
    stellar::native_map::UiRect terrain) noexcept;

} // namespace stellar::native_colony_ui
