#include "native_surface_relief.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

using namespace stellar::native_surface;
using stellar::native_map::Color;
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
  const Color rocky{38, 30, 24, 255};
  NativeSurfaceRelief relief;

  const auto image = relief.image(0., 0., .5, 400, 300, rocky);
  check(image != nullptr, "relief image is produced for a valid viewport");
  check(image && image->width() == 200 && image->height() == 150,
        "relief renders at half destination resolution");
  check(image && image->pixels().size() == 200ull * 150ull * 4ull,
        "relief pixel buffer is RGBA-complete");

  // Every texel is opaque and stays inside the colony palette.
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
    check(tones.size() > 4,
          "hillshade produces more than a flat fill tone");
  }

  // The cache returns the identical resource for a quantized-identical view.
  const auto again = relief.image(0., 0., .5, 400, 300, rocky);
  check(again == image, "quantized-identical views reuse the cached relief");
  check(relief.cached_images() == 1, "cache holds exactly one relief image");

  // Panning beyond a destination pixel produces fresh shading; sub-pixel
  // pans stay cached.
  const auto sub_pixel = relief.image(1. / .5 * .4, 0., .5, 400, 300, rocky);
  check(sub_pixel == image, "sub-pixel pans reuse the cached relief");
  const auto panned = relief.image(40., 0., .5, 400, 300, rocky);
  check(panned && panned != image, "pixel-scale pans regenerate the relief");
  check(relief.cached_images() == 2, "cache tracks distinct viewports");

  // A different surface palette produces a different underlay.
  const auto frozen =
      relief.image(0., 0., .5, 400, 300, Color{28, 44, 54, 255});
  check(frozen && frozen != image,
        "palette swaps regenerate the relief under a new key");
  if (frozen && image) check(frozen->pixels() != image->pixels(),
                             "palette swaps change the shaded texels");

  // Degenerate viewports decline gracefully instead of rendering garbage.
  check(!relief.image(0., 0., 0., 400, 300, rocky),
        "non-positive scale is rejected");
  check(!relief.image(0., 0., .5, 0, 300, rocky),
        "degenerate bounds are rejected");
  check(!relief.image(std::nan(""), 0., .5, 400, 300, rocky),
        "non-finite centers are rejected");

  // The cache is bounded: distinct far views evict the oldest entry.
  for (int i = 0; i < 8; ++i)
    (void)relief.image(2000. + i * 900., i * 733., .5, 400, 300, rocky);
  check(relief.cached_images() <= NativeSurfaceRelief::maximum_cached_images,
        "relief cache stays bounded under viewport churn");

  if (failures) {
    std::cerr << failures << " relief checks failed\n";
    return 1;
  }
  std::cout << "native surface relief checks passed\n";
  return 0;
}
