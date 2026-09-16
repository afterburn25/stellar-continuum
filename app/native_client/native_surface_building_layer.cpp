#include "native_surface_building_layer.hpp"

#include <algorithm>
#include <cmath>

namespace stellar::native_colony_ui {
namespace {
using namespace stellar::native_map;
using namespace stellar::native_surface_building;

bool finite(Point value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}
bool valid_rect(UiRect value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.width) && std::isfinite(value.height) &&
         value.width > 0 && value.height > 0;
}
bool intersects(UiRect left, UiRect right) {
  return left.x < right.x + right.width &&
         left.x + left.width > right.x && left.y < right.y + right.height &&
         left.y + left.height > right.y;
}
} // namespace

std::optional<PlacedSurfaceBuildingImage> place_surface_building_image(
    const std::shared_ptr<const PreparedSurfaceBuildingRaster> &prepared,
    const SurfaceBuildingStateKey &expected,
    const SurfaceBuildingRasterSpec &spec, float world_x, float world_z,
    const SurfaceViewport &viewport, UiRect terrain) noexcept {
  if (!prepared || !prepared->image || !(prepared->state == expected) ||
      prepared->image->width() != spec.width ||
      prepared->image->height() != spec.height ||
      !std::isfinite(world_x) || !std::isfinite(world_z) ||
      !std::isfinite(viewport.pixels_per_unit) ||
      viewport.pixels_per_unit <= 0 || !valid_rect(terrain) ||
      !std::isfinite(spec.camera.half_extent) ||
      spec.camera.half_extent <= 0)
    return std::nullopt;
  const auto &projection = prepared->projection;
  if (!finite(projection.ground_anchor_pixels) ||
      projection.ground_anchor_pixels.x < 0 ||
      projection.ground_anchor_pixels.x > spec.width ||
      projection.ground_anchor_pixels.y < 0 ||
      projection.ground_anchor_pixels.y > spec.height ||
      !valid_rect(projection.projected_bounds) ||
      projection.projected_bounds.x < 0 || projection.projected_bounds.y < 0 ||
      projection.projected_bounds.x + projection.projected_bounds.width >
          spec.width ||
      projection.projected_bounds.y + projection.projected_bounds.height >
          spec.height)
    return std::nullopt;
  const auto ground = viewport.world_to_screen(world_x, world_z, terrain);
  const auto extent = static_cast<float>(2. * spec.camera.half_extent *
                                         viewport.pixels_per_unit);
  if (!finite(ground) || !std::isfinite(extent) || extent <= 0 ||
      extent > 1'000'000)
    return std::nullopt;
  const auto source_width = static_cast<float>(spec.width);
  const auto source_height = static_cast<float>(spec.height);
  UiRect destination{
      ground.x - projection.ground_anchor_pixels.x * extent / source_width,
      ground.y - projection.ground_anchor_pixels.y * extent / source_height,
      extent,
      extent,
  };
  const UiRect content_bounds{
      destination.x + projection.projected_bounds.x * extent / source_width,
      destination.y + projection.projected_bounds.y * extent / source_height,
      projection.projected_bounds.width * extent / source_width,
      projection.projected_bounds.height * extent / source_height,
  };
  if (!valid_rect(destination) || !valid_rect(content_bounds) ||
      !intersects(content_bounds, terrain))
    return std::nullopt;
  return PlacedSurfaceBuildingImage{
      Image{prepared->image, destination, std::nullopt, {255, 255, 255, 255},
            terrain, 0},
      content_bounds,
      ground,
      ground.y,
  };
}

} // namespace stellar::native_colony_ui
