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
const Circle &circle_at(const DrawList &draw, std::size_t index) {
  const auto *circle = std::get_if<Circle>(&draw.world.at(index));
  if (!circle) throw std::runtime_error("expected ordered galaxy marker circle");
  return *circle;
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

void unexplored_alpha_preserves_resources_and_dims_every_component() {
  NativeGalaxyStarMarkerRenderer renderer;
  const NativeGalaxyStarAppearance appearance{
      GalaxyStarVisualClass::g_yellow_dwarf, GalaxyStarVisualClass::m_red_dwarf,
      GalaxyStarVisualClass::white_dwarf};
  DrawList visible;
  renderer.append(visible, {100, 100}, 4.f, appearance, true);
  DrawList dimmed;
  renderer.append(dimmed, {100, 100}, 4.f, appearance, true, std::nullopt, .4f);
  require(visible.world.size() == 5 && dimmed.world.size() == 5,
          "selected triple marker did not retain every visual component while dimmed");
  require(circle_at(dimmed, 0).color.a == 21 && circle_at(dimmed, 1).color.a == 62,
          "selection or contrast disc ignored unexplored alpha");
  for (std::size_t index = 2; index != 5; ++index) {
    const auto &full = image_at(visible, index);
    const auto &dim = image_at(dimmed, index);
    require(dim.resource == full.resource && dim.tint.a == 102,
            "primary, secondary, or tertiary marker did not share and dim its immutable image");
    require_transparent_border(*dim.resource);
  }
  require(renderer.stats().generated_resources == 3,
          "unexplored alpha regenerated marker resources");

  DrawList invisible;
  renderer.append(invisible, {100, 100}, 4.f, appearance, true, std::nullopt, 0.f);
  require(std::ranges::all_of(invisible.world, [](const WorldCommand &command) {
    if (const auto *circle = std::get_if<Circle>(&command)) return circle->color.a == 0;
    if (const auto *image = std::get_if<Image>(&command)) return image->tint.a == 0;
    return false;
  }), "zero unexplored alpha emitted a visible marker primitive");

  for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), -.01f}) {
    bool rejected{};
    try { renderer.append(invisible, {100, 100}, 4.f, appearance, false, std::nullopt, invalid); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "invalid unexplored alpha was accepted");
  }
}

} // namespace

int main() try {
  for(const int height:{720,1080,1440,2160}){
    const auto start=galaxy_star_core_radius(1.,height);
    const auto near=galaxy_star_core_radius(5.,height);
    const auto close=galaxy_star_core_radius(30.,height);
    require(start>=4.f&&near>start*2.f&&close>near*2.f,"star markers stayed tiny or failed to grow with zoom");
    require(galaxy_star_core_radius(5.,height,GalaxyStarVisualClass::giant)>near*1.5f,"giant marker lost its larger silhouette");
    NativeGalaxyStarMarkerRenderer renderer;DrawList a,b;
    renderer.append(a,{100,100},start,{},false);renderer.append(b,{100,100},close,{},false);
    require(image_at(b,1).destination.width>image_at(a,1).destination.width*4.f,"zoom did not enlarge drawn stars");
    require(image_at(a,1).resource==image_at(b,1).resource,"zoom allocated a new star texture");
  }
  shared_discrete_resources();
  compact_and_multiplicity_cues();
  hard_budget_and_validation();
  unexplored_alpha_preserves_resources_and_dims_every_component();
  std::cout << "native galaxy star marker tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
