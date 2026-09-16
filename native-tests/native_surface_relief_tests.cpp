#include "native_surface_relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

using namespace stellar::native_surface;
using stellar::native_map::RgbaImage;

namespace {
int failures{0};
void check(const bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
} // namespace

int main() {
  const auto rocky = NativeSurfacePaletteClass::rocky;
  const auto oceanic = NativeSurfacePaletteClass::oceanic;
  NativeSurfaceRelief relief;

  const auto image = relief.image(0., 0., .5, 400, 300, rocky, .5);
  check(image != nullptr, "relief image is produced for a valid viewport");
  check(image && image->width() == 200 && image->height() == 150,
        "relief renders at half destination resolution");
  check(image && image->pixels().size() == 200ull * 150ull * 4ull,
        "relief pixel buffer is RGBA-complete");

  // Every texel is opaque and the banded palette produces tonal variety.
  if (image) {
    const auto &pixels = image->pixels();
    bool any_alpha_gap = false;
    std::set<std::uint32_t> tones;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
      any_alpha_gap |= pixels[i + 3] != 255;
      tones.emplace((static_cast<std::uint32_t>(pixels[i]) << 16) |
                    (static_cast<std::uint32_t>(pixels[i + 1]) << 8) |
                    pixels[i + 2]);
    }
    check(!any_alpha_gap, "relief underlay is fully opaque");
    check(tones.size() > 8,
          "palette bands produce more than a flat fill tone");
  }

  // The cache returns the identical resource for a quantized-identical view.
  const auto again = relief.image(0., 0., .5, 400, 300, rocky, .5);
  check(again == image, "quantized-identical views reuse the cached relief");
  check(relief.cached_images() == 1, "cache holds exactly one relief image");

  // Panning beyond a destination pixel produces fresh shading; sub-pixel
  // pans stay cached.
  const auto sub_pixel =
      relief.image(1. / .5 * .4, 0., .5, 400, 300, rocky, .5);
  check(sub_pixel == image, "sub-pixel pans reuse the cached relief");
  const auto panned = relief.image(40., 0., .5, 400, 300, rocky, .5);
  check(panned && panned != image, "pixel-scale pans regenerate the relief");
  check(relief.cached_images() == 2, "cache tracks distinct viewports");

  // A different surface palette produces a different underlay.
  const auto water =
      relief.image(0., 0., .5, 400, 300, oceanic, .5);
  check(water && water != image,
        "palette swaps regenerate the relief under a new key");
  if (water && image) check(water->pixels() != image->pixels(),
                            "palette swaps change the shaded texels");

  // Oceanic palettes read blue-dominant versus the rocky earth tones;
  // sample a far-field texel outside the hub's civic paving ring.
  if (water) {
    const auto &pixels = water->pixels();
    const auto field =
        (static_cast<std::size_t>(20) * 200 + 30) * 4;
    check(pixels[field + 2] > pixels[field],
          "oceanic relief is blue-dominant away from the paving ring");
  }

  // A different body seed shifts the noise bands.
  const auto other_seed =
      relief.image(0., 0., .5, 400, 300, rocky, .875);
  check(other_seed && other_seed != image,
        "seed swaps regenerate the relief under a new key");
  if (other_seed && image)
    check(other_seed->pixels() != image->pixels(),
          "seed swaps change the shaded texels");

  // Degenerate viewports decline gracefully instead of rendering garbage.
  check(!relief.image(0., 0., 0., 400, 300, rocky, .5),
        "non-positive scale is rejected");
  check(!relief.image(0., 0., .5, 0, 300, rocky, .5),
        "degenerate bounds are rejected");
  check(!relief.image(std::nan(""), 0., .5, 400, 300, rocky, .5),
        "non-finite centers are rejected");
  check(!relief.image(0., 0., .5, 400, 300, rocky, std::nan("")),
        "non-finite seeds are rejected");

  // The cache is bounded: distinct far views evict the oldest entry.
  for (int i = 0; i < 8; ++i)
    (void)relief.image(2000. + i * 900., i * 733., .5, 400, 300, rocky,
                       .5);
  check(relief.cached_images() <= NativeSurfaceRelief::maximum_cached_images,
        "relief cache stays bounded under viewport churn");

  // The reference seed helper reproduces the body's shader seed.
  check(terrain_seed(0) == 0., "terrain seed zero maps to zero");
  check(terrain_seed(1) > 0. && terrain_seed(1) < 1.,
        "terrain seed stays inside the shader's unit range");

  if (failures) {
    std::cerr << failures << " relief checks failed\n";
    return 1;
  }
  std::cout << "native surface relief checks passed\n";
  return 0;
}
