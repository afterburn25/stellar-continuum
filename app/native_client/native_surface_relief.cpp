#include "native_surface_relief.hpp"

#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace stellar::native_surface {
namespace {

using native_map::Color;
using native_map::RgbaImage;

unsigned int color_key(const Color color) noexcept {
  return (static_cast<unsigned int>(color.r) << 16) |
         (static_cast<unsigned int>(color.g) << 8) |
         static_cast<unsigned int>(color.b);
}

} // namespace

std::shared_ptr<const RgbaImage>
NativeSurfaceRelief::image(const double center_x, const double center_z,
                           const double pixels_per_unit, const int width,
                           const int height, const Color base) {
  if (!std::isfinite(center_x) || !std::isfinite(center_z) ||
      !std::isfinite(pixels_per_unit) || pixels_per_unit <= 0. ||
      width < 8 || height < 8)
    return nullptr;
  // Quantize the view to the destination-pixel grid so sub-pixel pans keep
  // the cached relief; ppu snaps to 1e-3 to bound keys under wheel zoom.
  const Key key{static_cast<long long>(std::llround(center_x * pixels_per_unit)),
                static_cast<long long>(std::llround(center_z * pixels_per_unit)),
                static_cast<long long>(std::llround(pixels_per_unit * 1000.)),
                width, height, color_key(base)};
  if (const auto found = cache_.find(key); found != cache_.end())
    return found->second;
  if (cache_.size() >= maximum_cached_images) cache_.erase(cache_.begin());

  const int texel_width = std::max(4, width / resolution_scale),
            texel_height = std::max(4, height / resolution_scale);
  const double texel_step = static_cast<double>(resolution_scale) /
                            pixels_per_unit;
  const double origin_x = center_x - width * .5 / pixels_per_unit,
               origin_z = center_z - height * .5 / pixels_per_unit;
  // Finite-difference the heightfield at texel spacing; the light is a fixed
  // north-west source matching the building sprites' shading.
  const double step = texel_step;
  const float light_x = -.55f, light_y = .78f, light_z = -.48f;
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(texel_width) * texel_height * 4);
  for (int row = 0; row < texel_height; ++row) {
    const double z = origin_z + (static_cast<double>(row) + .5) * texel_step;
    for (int column = 0; column < texel_width; ++column) {
      const double x =
          origin_x + (static_cast<double>(column) + .5) * texel_step;
      const auto terrain_h = [&](const double wx, const double wz) {
        return static_cast<double>(core::surface_terrain_height(
            static_cast<float>(wx), static_cast<float>(wz)));
      };
      const double dx = (terrain_h(x + step, z) - terrain_h(x - step, z)) /
                        (2. * step),
                   dz = (terrain_h(x, z + step) - terrain_h(x, z - step)) /
                        (2. * step);
      const double nx = -dx, nz = -dz, ny = 1.;
      const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
      const double lit = std::max(
          0., (nx * light_x + ny * light_y + nz * light_z) / length);
      // The rim highlands also lift slightly in tone so elevation reads even
      // on gentle slopes; shading stays within the colony palette.
      const double shade = std::clamp(.62 + .5 * lit, 0., 1.);
      const double altitude =
          std::clamp(terrain_h(x, z) / 30., 0., 1.) * .18;
      const double brightness = std::clamp(shade + altitude, 0., 1.6);
      const auto channel = [&](const std::uint8_t value) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(static_cast<double>(value) * brightness), 0l, 255l));
      };
      const auto offset =
          (static_cast<std::size_t>(row) * texel_width + column) * 4;
      pixels[offset] = channel(base.r);
      pixels[offset + 1] = channel(base.g);
      pixels[offset + 2] = channel(base.b);
      pixels[offset + 3] = 255;
    }
  }
  auto rendered =
      RgbaImage::create(texel_width, texel_height, std::move(pixels));
  if (!rendered)
    throw std::runtime_error("Surface terrain relief image creation failed.");
  cache_.emplace(key, rendered);
  return rendered;
}

} // namespace stellar::native_surface
