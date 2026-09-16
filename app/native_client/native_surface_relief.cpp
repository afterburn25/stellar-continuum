#include "native_surface_relief.hpp"

#include <stellar/core/surface_construction.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace stellar::native_surface {
namespace {

using native_map::RgbaImage;

struct Rgb3 {
  double r, g, b;
};

Rgb3 mix(const Rgb3 a, const Rgb3 b, const double t) noexcept {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t};
}

Rgb3 scale(const Rgb3 value, const Rgb3 factor) noexcept {
  return {value.r * factor.r, value.g * factor.g, value.b * factor.b};
}

// Reference WorldPalette table (PlanetSurfaceView.ApplyWorldPalette):
// low/high vegetation tones and exposed dirt tones per surface class.
struct TerrainPalette {
  Rgb3 low, high, exposed_low, exposed_high;
};

TerrainPalette palette_for(const NativeSurfacePaletteClass value) noexcept {
  constexpr auto c = [](const unsigned int hex) {
    return Rgb3{static_cast<double>((hex >> 16) & 0xff) / 255.,
                static_cast<double>((hex >> 8) & 0xff) / 255.,
                static_cast<double>(hex & 0xff) / 255.};
  };
  switch (value) {
    case NativeSurfacePaletteClass::frozen:
      return {c(0x485967), c(0x9bb6c3), c(0x697986), c(0xd2e3e7)};
    case NativeSurfacePaletteClass::hot:
      return {c(0x4f2417), c(0xa34a22), c(0x2d1714), c(0x75402a)};
    case NativeSurfacePaletteClass::airless:
      return {c(0x34363b), c(0x777b82), c(0x202126), c(0x51545c)};
    case NativeSurfacePaletteClass::oceanic:
      return {c(0x123f53), c(0x2f8793), c(0x1a5867), c(0x58aab0)};
    case NativeSurfacePaletteClass::reducing:
      return {c(0x293f30), c(0x65733b), c(0x453822), c(0x8a7540)};
    case NativeSurfacePaletteClass::rocky:
      return {c(0x3b322b), c(0x777064), c(0x2d2723), c(0x62564a)};
    case NativeSurfacePaletteClass::temperate:
      return {c(0x26382a), c(0x59634b), c(0x3f382d), c(0x695a46)};
  }
  return {c(0x26382a), c(0x59634b), c(0x3f382d), c(0x695a46)};
}

// Deterministic value noise matching colony_terrain.gdshader's
// hash/noise pair so the banded terrain tones read the same way.
double fract(const double value) noexcept {
  return value - std::floor(value);
}

double hash2(const double x, const double y) noexcept {
  return fract(std::sin(x * 127.1 + y * 311.7) * 43758.5453);
}

double noise2(const double x, const double y) noexcept {
  const double ix = std::floor(x), iy = std::floor(y);
  const double fx = x - ix, fy = y - iy;
  const double ux = fx * fx * (3. - 2. * fx),
               uy = fy * fy * (3. - 2. * fy);
  return (hash2(ix, iy) * (1. - ux) + hash2(ix + 1., iy) * ux) * (1. - uy) +
         (hash2(ix, iy + 1.) * (1. - ux) + hash2(ix + 1., iy + 1.) * ux) *
             uy;
}

double smoothstep(const double edge0, const double edge1,
                  const double value) noexcept {
  const double t =
      std::clamp((value - edge0) / (edge1 - edge0), 0., 1.);
  return t * t * (3. - 2. * t);
}

} // namespace

std::shared_ptr<const RgbaImage>
NativeSurfaceRelief::image(const double center_x, const double center_z,
                           const double pixels_per_unit, const int width,
                           const int height,
                           const NativeSurfacePaletteClass palette_class,
                           const double seed) {
  if (!std::isfinite(center_x) || !std::isfinite(center_z) ||
      !std::isfinite(pixels_per_unit) || pixels_per_unit <= 0. ||
      !std::isfinite(seed) || width < 8 || height < 8)
    return nullptr;
  // Quantize the view to the destination-pixel grid so sub-pixel pans keep
  // the cached relief; ppu snaps to 1e-3 to bound keys under wheel zoom.
  const Key key{static_cast<long long>(std::llround(center_x * pixels_per_unit)),
                static_cast<long long>(std::llround(center_z * pixels_per_unit)),
                static_cast<long long>(std::llround(pixels_per_unit * 1000.)),
                width, height, static_cast<int>(palette_class),
                static_cast<int>(std::lround(seed * 1023.))};
  if (const auto found = cache_.find(key); found != cache_.end())
    return found->second;
  if (cache_.size() >= maximum_cached_images) cache_.erase(cache_.begin());

  const TerrainPalette palette = palette_for(palette_class);
  const int texel_width = std::max(4, width / resolution_scale),
            texel_height = std::max(4, height / resolution_scale);
  const double texel_step = static_cast<double>(resolution_scale) /
                            pixels_per_unit;
  const double origin_x = center_x - width * .5 / pixels_per_unit,
               origin_z = center_z - height * .5 / pixels_per_unit;
  // Direction toward the reference's ColonySun (DirectionalLight3D at
  // RotationDegrees -32/-36), plus its ambient term; the soft knee below
  // stands in for the filmic tonemap.
  const double light_x = -.4985, light_y = .5299, light_z = .6861;
  const Rgb3 ambient{.42 * .494, .42 * .608, .42 * .667};
  const Rgb3 sun{1.55, 1.55 * .906, 1.55 * .773};
  const double seed_x = seed * 173.7, seed_z = seed * 311.9;
  const double detail_aa = std::max(texel_step / 5., .004);
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(texel_width) * texel_height * 4);
  for (int row = 0; row < texel_height; ++row) {
    const double z = origin_z + (static_cast<double>(row) + .5) * texel_step;
    for (int column = 0; column < texel_width; ++column) {
      const double x =
          origin_x + (static_cast<double>(column) + .5) * texel_step;
      const auto terrain_h = [](const double wx, const double wz) {
        return static_cast<double>(core::surface_terrain_height(
            static_cast<float>(wx), static_cast<float>(wz)));
      };
      // The reference normals use a fixed +-1 m finite difference.
      const double nx = terrain_h(x - 1., z) - terrain_h(x + 1., z),
                   nz = terrain_h(x, z - 1.) - terrain_h(x, z + 1.);
      const double length = std::sqrt(nx * nx + 4. + nz * nz);
      const double normal_y = 2. / length;
      const double diffuse = std::max(
          0., (nx * light_x + 2. * light_y + nz * light_z) / length);

      const double radius = std::hypot(x, z);
      const double broad =
          noise2(x * .012 + seed_x, z * .012 + seed_z) * .6 +
          noise2(x * .054 + seed_z, z * .054 + seed_x) * .4;
      const double fine =
          noise2(x * .42 + seed_x * 2.7, z * .42 + seed_z * 2.7);
      const double mottled =
          noise2(x * .14 + 17. + seed_x * .43,
                 z * .14 + 41. + seed_z * .43);
      const Rgb3 grass = mix(palette.low, palette.high, broad);
      const Rgb3 dirt =
          mix(palette.exposed_low, palette.exposed_high, broad);
      const double clearing = 1. - smoothstep(20., 38., radius);
      const double rocky = smoothstep(.06, .24, 1. - normal_y);
      const Rgb3 base =
          mix(grass, dirt, std::max(clearing * .58, rocky));
      const double district_center = 1. - smoothstep(12., 42., radius);
      const double district_wear = .94 + mottled * .06;
      // The shader's photographed detail texture supplies only luma
      // (terrain_detail_chroma is 0); a fine noise octave substitutes.
      const double micro_luma = noise2(
          x * .46 + seed_x * .013 * 5., z * .46 + seed_z * .013 * 5.);
      const double terrain_micro =
          std::clamp(micro_luma / .32, .36, 1.9);
      const double drainage = noise2(x * .006 + 81. + seed_x * .19,
                                     z * .006 + 13. + seed_z * .19);
      const double basin =
          noise2(x * .0021 + seed_x * 1.7, z * .0021 + seed_z * 1.7) * .62 +
          noise2(x * .009 + 19. + seed_x, z * .009 + 67. + seed_z) * .38;
      const double stratum =
          std::abs(noise2(x * .018 + seed_z, z * .018 + seed_x) - .5) * 2.;
      const Rgb3 cool_shadow = scale(base, {.79, .86, .88});
      Rgb3 natural =
          mix(cool_shadow, base, smoothstep(.22, .78, drainage));
      natural = mix(natural, scale(natural, {.67, .73, .70}),
                    smoothstep(.16, .39, basin) * .34);
      natural = mix(natural, scale(natural, {1.16, 1.08, .91}),
                    smoothstep(.72, .94, stratum) * .16);
      const double brightness_fine = .70 + fine * .14;
      natural = scale(natural, {brightness_fine, brightness_fine,
                                brightness_fine});
      const double wear =
          1. + (district_wear - 1.) * district_center * .45;
      natural = scale(natural, {wear, wear, wear});
      natural = scale(natural,
                      {1. + (terrain_micro - 1.) * .46,
                       1. + (terrain_micro - 1.) * .46,
                       1. + (terrain_micro - 1.) * .46});

      // Permanent civic paving inside the hub apron, with the reference's
      // 5 m tile joints resolved against the texel footprint.
      const double town = 1. - smoothstep(30., 40., radius);
      const double tile_x = std::abs(fract(x / 5.) - .5),
                   tile_z = std::abs(fract(z / 5.) - .5);
      const double joint = std::max(
          smoothstep(.485 - detail_aa, .495 + detail_aa, tile_x),
          smoothstep(.485 - detail_aa, .495 + detail_aa, tile_z));
      const Rgb3 paving = scale(
          mix({.095, .112, .119}, {.145, .153, .151}, mottled),
          {1. - joint * .14, 1. - joint * .14, 1. - joint * .14});
      const Rgb3 albedo = mix(natural, paving, town);

      const auto channel = [&](const double albedo_channel,
                               const double sun_channel,
                               const double ambient_channel) {
        const double lit = albedo_channel *
                           (ambient_channel + sun_channel * diffuse);
        // Soft knee approximating the reference's filmic tonemap.
        const double toned = lit <= .92 ? lit : .92 + (lit - .92) * .25;
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(toned * 255.), 0l, 255l));
      };
      const auto offset =
          (static_cast<std::size_t>(row) * texel_width + column) * 4;
      pixels[offset] = channel(albedo.r, sun.r, ambient.r);
      pixels[offset + 1] = channel(albedo.g, sun.g, ambient.g);
      pixels[offset + 2] = channel(albedo.b, sun.b, ambient.b);
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
