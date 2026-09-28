#include "native_orbital_structure.hpp"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace stellar::native_orbital;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
std::size_t opaque_texels(const stellar::native_map::RgbaImage &image) {
  std::size_t count = 0;
  for (std::size_t i = 3; i < image.pixels().size(); i += 4)
    if (image.pixels()[i] > 0) ++count;
  return count;
}
void transparent_border(const stellar::native_map::RgbaImage &image) {
  const auto &p = image.pixels();
  for (int i = 0; i < image.width(); ++i) {
    for (const int y : {0, image.height() - 1})
      require(p[static_cast<std::size_t>(y * image.width() + i) * 4 + 3] == 0,
              "orbital structure leaked onto the texture border");
    for (const int x : {0, image.width() - 1})
      require(p[static_cast<std::size_t>(x * image.width() + i) * 4 + 3] == 0,
              "orbital structure leaked onto the texture border");
  }
}

void phases() {
  require(NativeOrbitalStructureRenderer::phase_for_progress(0) == 1 &&
              NativeOrbitalStructureRenderer::phase_for_progress(.25) == 1 &&
              NativeOrbitalStructureRenderer::phase_for_progress(.26) == 2 &&
              NativeOrbitalStructureRenderer::phase_for_progress(.5) == 2 &&
              NativeOrbitalStructureRenderer::phase_for_progress(.51) == 3 &&
              NativeOrbitalStructureRenderer::phase_for_progress(.99) == 4 &&
              NativeOrbitalStructureRenderer::phase_for_progress(1) == 4,
          "construction phase mapping diverged from the reference staging");
}

void rendering() {
  NativeOrbitalStructureRenderer renderer;
  const auto early = renderer.image("orbital_shipyard", 1);
  const auto staged = renderer.image("orbital_shipyard", 4);
  const auto launch = renderer.image("orbital_launch_complex", 4);
  const auto network = renderer.image("asteroid_resource_network", 4);
  const auto fallback = renderer.image("unknown_future_project", 2);
  for (const auto &image : {early, staged, launch, network, fallback}) {
    require(image && image->width() ==
                         NativeOrbitalStructureRenderer::texture_size &&
                 image->height() ==
                     NativeOrbitalStructureRenderer::texture_size,
            "orbital structure returned an unexpected texture size");
    transparent_border(*image);
    require(opaque_texels(*image) > 400,
            "orbital structure rasterized an empty silhouette");
  }
  require(staged->pixels() != early->pixels() &&
              staged->pixels() != launch->pixels() &&
              launch->pixels() != network->pixels() &&
              network->pixels() != fallback->pixels(),
          "distinct structures or stages collapsed to identical pixels");
  require(opaque_texels(*staged) > opaque_texels(*early),
          "later construction phases did not add visible structure");
  require(renderer.image("orbital_shipyard", 1) == early,
          "structure cache did not return the same immutable resource");
  NativeOrbitalStructureRenderer second;
  require(second.image("orbital_shipyard", 4)->pixels() == staged->pixels(),
          "structure rasterization is not deterministic across renderers");
  for (int phase = 1; phase <= 4; ++phase)
    for (const char *id : {"orbital_shipyard", "orbital_launch_complex",
                           "asteroid_resource_network", "outpost_one",
                           "outpost_two"})
      (void)renderer.image(id, phase);
  require(renderer.cached_images() <=
              NativeOrbitalStructureRenderer::maximum_cached_images,
          "structure cache exceeded its bounded capacity");
}
} // namespace

int main() try {
  phases();
  rendering();
  std::cout << "native orbital structure cases passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "native orbital structure failed: " << error.what() << '\n';
  return 1;
}
