#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <tuple>

namespace stellar::native_surface {

// Reference PlanetSurfaceView surface palettes (Main.Surface.cs
// SurfaceVisualClass): the terrain shader tints its heightfield from a
// per-class low/high/exposed palette. The enum mirrors the reference's
// string classes so the relief renderer can reproduce the same terrain
// bands in the top-down view.
enum class NativeSurfacePaletteClass {
  temperate,
  reducing,
  rocky,
  oceanic,
  frozen,
  hot,
  airless,
};

// The reference derives terrain_seed from the body id so every world gets
// a stable noise offset (PlanetSurfaceView.ApplyWorldPalette).
[[nodiscard]] inline double terrain_seed(const int body_id) noexcept {
  return static_cast<double>(
             (static_cast<unsigned int>(body_id) * 2654435761u) & 1023u) /
         1023.;
}

// Renders the reference client's terrain (PlanetSurfaceView's
// colony_terrain.gdshader heightfield ground) as a cached hillshade
// underlay for the top-down terrain viewport. Each texel samples the
// authoritative surface_terrain_height for its slope normal, reproduces
// the shader's seeded noise bands (grass/dirt broad mix, slope-exposed
// rock, hub clearing + civic paving ring, drainage/basin/stratum
// modulation), and lights it against a fixed north-west sun. Texels are
// computed at half destination resolution; the Image blit scales the
// result up to the terrain rect.
class NativeSurfaceRelief final {
 public:
  inline static constexpr std::size_t maximum_cached_images = 4;
  inline static constexpr int resolution_scale = 2;

  // center_x/center_z are the world-space viewport center; pixels_per_unit
  // is the viewport scale; width/height are the destination rect pixels;
  // palette_class selects the body's reference surface palette and seed
  // the per-body noise offset (see terrain_seed).
  [[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
  image(double center_x, double center_z, double pixels_per_unit, int width,
        int height, NativeSurfacePaletteClass palette_class, double seed);
  [[nodiscard]] std::size_t cached_images() const noexcept {
    return cache_.size();
  }

 private:
  using Key = std::tuple<long long, long long, long long, int, int, int,
                         int>;
  std::map<Key, std::shared_ptr<const native_map::RgbaImage>> cache_;
};

} // namespace stellar::native_surface
