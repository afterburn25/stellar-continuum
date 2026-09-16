#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <tuple>

namespace stellar::native_surface {

// Renders the reference client's terrain relief (PlanetSurfaceView's
// heightfield ground, SurfaceConstruction.TerrainHeight) as a cached
// hillshade underlay for the top-down terrain viewport. Each texel samples
// the authoritative surface_terrain_height and shades by slope against a
// fixed north-west light, tinted by the colony's surface palette color.
// Texels are computed at half destination resolution; the Image blit scales
// the result up to the terrain rect.
class NativeSurfaceRelief final {
 public:
  inline static constexpr std::size_t maximum_cached_images = 4;
  inline static constexpr int resolution_scale = 2;

  // center_x/center_z are the world-space viewport center; pixels_per_unit
  // is the viewport scale; width/height are the destination rect pixels;
  // base is the colony's flat terrain tint (the same palette color the
  // workspace uses for its fill).
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
  image(double center_x, double center_z, double pixels_per_unit, int width,
        int height, native_map::Color base);
  [[nodiscard]] std::size_t cached_images() const noexcept {
    return cache_.size();
  }

 private:
  using Key = std::tuple<long long, long long, long long, int, int,
                         unsigned int>;
  std::map<Key, std::shared_ptr<const native_map::RgbaImage>> cache_;
};

} // namespace stellar::native_surface
