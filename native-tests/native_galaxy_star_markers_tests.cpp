#include "native_galaxy_star_markers.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_galaxy_ui;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}
const Image &image_at(const DrawList &draw, std::size_t index) {
  const auto *image = std::get_if<Image>(&draw.world.at(index));
  if (!image) throw std::runtime_error("expected ordered galaxy marker image");
  return *image;
}
std::uint8_t alpha(const RgbaImage &image, int x, int y) {
  return image.pixels().at(
      static_cast<std::size_t>((y * image.width() + x) * 4 + 3));
}
std::uint8_t red(const RgbaImage &image, int x, int y) {
  return image.pixels().at(
      static_cast<std::size_t>((y * image.width() + x) * 4));
}
void require_transparent_border(const RgbaImage &image) {
  for (int index = 0; index < image.width(); ++index) {
    require(alpha(image, index, 0) == 0 &&
                alpha(image, index, image.height() - 1) == 0 &&
                alpha(image, 0, index) == 0 &&
                alpha(image, image.width() - 1, index) == 0,
            "galaxy marker outer border was not fully transparent");
  }
}

void shared_discrete_resources() {
  NativeGalaxyStarMarkerRenderer renderer;
  DrawList first;
  renderer.append(first, {120, 90}, 2.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf}, false);
  require(first.world.size() == 2 &&
              std::holds_alternative<Circle>(first.world[0]) &&
              std::holds_alternative<Image>(first.world[1]),
          "single star did not order its contrast disc before its image");
  const auto resource = image_at(first, 1).resource;
  require(resource->width() == 64 && resource->height() == 64,
          "galaxy marker resource exceeded its small fixed tier");
  require(alpha(*resource, 0, 0) == 0 && alpha(*resource, 32, 32) > 220,
          "galaxy marker has a square frame or lacks a crisp core");
  require_transparent_border(*resource);
  require(alpha(*resource, 32, 32) > alpha(*resource, 36, 32) &&
              alpha(*resource, 36, 32) > alpha(*resource, 40, 32),
          "stellar core was a hard flat disc instead of a smooth light profile");
  require(alpha(*resource, 8, 32) > 14 &&
              alpha(*resource, 8, 32) > alpha(*resource, 8, 24),
          "thin horizontal diffraction ray was not antialiased visibly");
  require(alpha(*resource, 32, 8) > 6 &&
              alpha(*resource, 32, 8) > alpha(*resource, 24, 8),
          "thin vertical diffraction ray was not antialiased visibly");
  require(red(*resource, 8, 32) > 200,
          "faint ray color was darkened a second time before alpha blending");
  require(alpha(*resource, 44, 40) > 30 && red(*resource, 44, 40) < 150,
          "radial contrast annulus cannot separate a marker from bright artwork");
  DrawList moved;
  renderer.append(moved, {410, 330}, 5.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf}, false);
  require(image_at(moved, 1).resource == resource &&
              renderer.stats().generated_resources == 1,
          "position, scale, or system identity regenerated a shared class marker");
}

void compact_and_multiplicity_cues() {
  NativeGalaxyStarMarkerRenderer renderer;
  DrawList unknown;
  renderer.append(unknown, {50, 50}, 2.f, {}, false);
  require(unknown.world.size() == 2 &&
              std::holds_alternative<Circle>(unknown.world[0]) &&
              std::holds_alternative<Image>(unknown.world[1]),
          "unknown observer marker invented multiplicity or compact-object cues");
  DrawList black_hole;
  renderer.append(black_hole, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::black_hole}, false);
  const auto &hole = *image_at(black_hole, 1).resource;
  const auto center = static_cast<std::size_t>((32 * 64 + 32) * 4);
  require(hole.pixels()[center] < 80 && hole.pixels()[center + 1] < 80,
          "black-hole marker did not retain its dark center");
  DrawList pulsar;
  renderer.append(pulsar, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::pulsar}, false);
  require(image_at(pulsar, 1).resource != image_at(black_hole, 1).resource,
          "pulsar and black-hole markers collapsed to one style");
  DrawList triple;
  renderer.append(triple, {50, 50}, 3.f,
                  {GalaxyStarVisualClass::g_yellow_dwarf,
                   GalaxyStarVisualClass::m_red_dwarf,
                   GalaxyStarVisualClass::white_dwarf}, false);
  require(triple.world.size() == 4 &&
              std::holds_alternative<Circle>(triple.world[0]) &&
              std::ranges::all_of(triple.world.begin() + 1,
                                  triple.world.end(),
                                  [](const WorldCommand &command) {
                                    return std::holds_alternative<Image>(command);
                                  }),
          "observer-approved triple did not render exactly three stellar images");
}

void hard_budget_and_validation() {
  NativeGalaxyStarMarkerRenderer renderer;
  constexpr GalaxyStarVisualClass visuals[] = {
      GalaxyStarVisualClass::unknown, GalaxyStarVisualClass::m_red_dwarf,
      GalaxyStarVisualClass::k_orange_dwarf,
      GalaxyStarVisualClass::g_yellow_dwarf,
      GalaxyStarVisualClass::f_yellow_white_dwarf,
      GalaxyStarVisualClass::a_white_star, GalaxyStarVisualClass::hot_blue_star,
      GalaxyStarVisualClass::giant, GalaxyStarVisualClass::white_dwarf,
      GalaxyStarVisualClass::neutron_star, GalaxyStarVisualClass::black_hole,
      GalaxyStarVisualClass::protostar, GalaxyStarVisualClass::pulsar};
  DrawList draw;
  for (const auto visual : visuals) {
    renderer.append(draw, {0, 0}, 1.f, {visual}, false);
    require_transparent_border(*image_at(draw, draw.world.size() - 1).resource);
  }
  const auto stats = renderer.stats();
  require(stats.cached_resources == std::size(visuals) &&
              stats.cached_resources ==
                  NativeGalaxyStarMarkerRenderer::maximum_cached_resources &&
              stats.cached_bytes ==
                  NativeGalaxyStarMarkerRenderer::maximum_cached_bytes,
          "fixed canonical marker palette did not match its hard cache budget");
  bool rejected{};
  try {
    renderer.append(draw, {0, 0}, std::numeric_limits<float>::quiet_NaN(), {},
                    false);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "nonfinite galaxy marker geometry was accepted");
  renderer.clear();
  require(renderer.stats().cached_resources == 0 && renderer.stats().cached_bytes == 0,
          "marker cache clear retained resources");
}

} // namespace

int main() try {
  shared_discrete_resources();
  compact_and_multiplicity_cues();
  hard_budget_and_validation();
  std::cout << "native galaxy star marker tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
