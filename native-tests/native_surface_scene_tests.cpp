#include "native_surface_scene.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
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
std::size_t opaque(const RgbaImage &image) {
  std::size_t count = 0;
  const auto &pixels = image.pixels();
  for (std::size_t i = 3; i < pixels.size(); i += 4)
    if (pixels[i] > 0) ++count;
  return count;
}
std::uint64_t luminance(const RgbaImage &image) {
  std::uint64_t sum = 0;
  const auto &pixels = image.pixels();
  for (std::size_t i = 0; i + 2 < pixels.size(); i += 4)
    sum += pixels[i] + pixels[i + 1] + pixels[i + 2];
  return sum;
}
} // namespace

int main() {
  NativeSurfaceSceneRenderer scene;

  // Every reference silhouette renders a bounded, non-empty sprite.
  for (const char *type : {"power_generator", "science_lab", "fabricator",
                           "trade_hub", "habitat_complex",
                           "controlled_agriculture", "water_reclamation",
                           "grid_battery", "cargo_terminal", "unknown_kind"}) {
    const auto sprite = scene.image(type, 3, true, false);
    check(sprite && sprite->width() == NativeSurfaceSceneRenderer::texture_size,
          "building sprite must be the bounded texture size");
    check(opaque(*sprite) > 200, "building sprite must contain geometry");
  }

  // Distinct silhouettes must not collapse to the generic fallback.
  const auto generator = scene.image("power_generator", 3, true, false);
  const auto habitat = scene.image("habitat_complex", 3, true, false);
  check(generator != habitat &&
            generator->pixels() != habitat->pixels(),
        "distinct building types must render distinct sprites");

  // Deterministic: identical state produces byte-identical cached pixels.
  const auto again = scene.image("power_generator", 3, true, false);
  check(again == generator, "repeated renders must return the cached sprite");

  // Construction staging: pad+scaffold, structure+scaffold, complete.
  check(NativeSurfaceSceneRenderer::phase_for_progress(0.) == 1,
        "zero progress must render the scaffold phase");
  check(NativeSurfaceSceneRenderer::phase_for_progress(1.) == 3,
        "complete progress must render the finished phase");
  for (int phase = 1; phase <= 3; ++phase) {
    const auto staged = scene.image("trade_hub", phase, true, false);
    check(staged && opaque(*staged) > 100,
          "each construction phase must render");
  }
  check(scene.image("trade_hub", 1, true, false)->pixels() !=
            scene.image("trade_hub", 3, true, false)->pixels(),
        "construction phases must differ visually");

  // Unpowered and prioritized states change the sprite.
  check(scene.image("science_lab", 3, false, false)->pixels() !=
            scene.image("science_lab", 3, true, false)->pixels(),
        "offline buildings must differ visually");
  check(scene.image("science_lab", 3, true, true)->pixels() !=
            scene.image("science_lab", 3, true, false)->pixels(),
        "prioritized buildings must carry a visible halo");
  const auto dim = scene.image("fabricator", 3, false, false);
  const auto lit = scene.image("fabricator", 3, true, false);
  check(luminance(*dim) < luminance(*lit),
        "unpowered sprites must be darker");

  // Advanced-tier upgrades add the reference's second silhouette layer.
  check(scene.image("advanced_power_generator", 3, true, false)->pixels() !=
            scene.image("power_generator", 3, true, false)->pixels(),
        "advanced tier must extend the base silhouette");

  // Hub levels render distinct staged sprites.
  for (int level = 1; level <= 3; ++level) {
    const auto hub = scene.hub_image(level, false, false);
    check(hub && opaque(*hub) > 300, "hub sprite must contain geometry");
  }
  check(scene.hub_image(1, false, false)->pixels() !=
            scene.hub_image(3, false, false)->pixels(),
        "hub levels must differ visually");
  check(scene.hub_image(3, true, false)->pixels() !=
            scene.hub_image(3, false, false)->pixels(),
        "capital hubs must differ visually");
  check(scene.hub_image(1, false, true)->pixels() !=
            scene.hub_image(1, false, false)->pixels(),
        "outpost hubs must differ visually");

  // Cache stays bounded under churn.
  for (int i = 0; i < 80; ++i)
    (void)scene.image("churn_" + std::to_string(i), 3, true, false);
  check(scene.cached_images() <=
            NativeSurfaceSceneRenderer::maximum_cached_images,
        "sprite cache must stay bounded");

  if (failures) {
    std::cerr << failures << " surface scene check(s) failed\n";
    return 1;
  }
  std::cout << "native_surface_scene: all checks passed\n";
  return 0;
}
