#include "native_galaxy_star_markers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace stellar::native_galaxy_ui {
namespace {
using namespace stellar::native_map;

struct Palette {
  Color color;
  bool dark_center{};
  bool compact{};
  bool beam{};
};

Palette palette(GalaxyStarVisualClass value) {
  switch (value) {
  case GalaxyStarVisualClass::m_red_dwarf: return {{255, 112, 90, 255}};
  case GalaxyStarVisualClass::k_orange_dwarf: return {{255, 158, 85, 255}};
  case GalaxyStarVisualClass::g_yellow_dwarf: return {{255, 216, 121, 255}};
  case GalaxyStarVisualClass::f_yellow_white_dwarf: return {{255, 241, 199, 255}};
  case GalaxyStarVisualClass::a_white_star: return {{232, 243, 255, 255}};
  case GalaxyStarVisualClass::hot_blue_star: return {{136, 191, 255, 255}};
  case GalaxyStarVisualClass::giant: return {{255, 118, 92, 255}};
  case GalaxyStarVisualClass::white_dwarf: return {{217, 237, 255, 255}, false, true};
  case GalaxyStarVisualClass::neutron_star: return {{121, 207, 255, 255}, false, true};
  case GalaxyStarVisualClass::black_hole: return {{155, 135, 217, 255}, true, true};
  case GalaxyStarVisualClass::protostar: return {{255, 176, 101, 255}};
  case GalaxyStarVisualClass::pulsar: return {{103, 220, 255, 255}, false, true, true};
  default: return {{185, 200, 225, 255}};
  }
}

std::uint8_t channel(float value) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(value * 255.f), 0l, 255l));
}

float antialiased_line(const float distance, const float half_width) {
  constexpr float antialias = 2.f / NativeGalaxyStarMarkerRenderer::texture_size;
  return 1.f - std::clamp((distance - half_width) / antialias, 0.f, 1.f);
}

std::shared_ptr<const RgbaImage> make_marker(GalaxyStarVisualClass visual) {
  constexpr int size = NativeGalaxyStarMarkerRenderer::texture_size;
  const auto style = palette(visual);
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size * size * 4));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (x == 0 || y == 0 || x == size - 1 || y == size - 1) continue;
      const float px = (2.f * (x + .5f) / size - 1.f);
      const float py = (2.f * (y + .5f) / size - 1.f);
      const float radius = std::hypot(px, py);
      const float edge = 1.f - std::clamp((radius - .78f) / .20f, 0.f, 1.f);
      const float glow = std::exp(-radius * 4.6f) * .48f;
      const float ray_x = antialiased_line(std::abs(py), .012f) *
                          std::pow(std::max(0.f, 1.f - std::abs(px)), 1.45f) *
                          .68f;
      const float ray_y = antialiased_line(std::abs(px), .010f) *
                          std::pow(std::max(0.f, 1.f - std::abs(py)), 1.6f) *
                          (style.compact ? .48f : .28f);
      const float diagonal = style.beam
                                 ? antialiased_line(std::abs(px + py * .55f),
                                                    .010f) *
                                       std::max(0.f, 1.f - radius) * .62f
                                 : 0.f;
      const float core_width = style.compact ? .105f : .145f;
      const float core = std::exp(-(radius * radius) /
                                  (2.f * core_width * core_width));
      const float lens = style.dark_center
                             ? std::max(0.f, 1.f - std::abs(radius - .25f) / .065f)
                             : 0.f;
      const float horizon = style.dark_center
                                ? 1.f - std::clamp((radius - .16f) / .035f, 0.f, 1.f)
                                : 0.f;
      const float light_alpha = edge * std::clamp(
          std::max({glow, ray_x, ray_y, diagonal,
                    style.dark_center ? std::max(lens, horizon) : core}),
          0.f, 1.f);
      const float contrast = edge * .30f *
          std::exp(-std::pow((radius - .37f) / .13f, 2.f));
      const float alpha = std::max(light_alpha, contrast);
      if (alpha < .002f) continue;
      const float white = style.dark_center ? lens * .20f : core * .52f;
      const float light_mix = light_alpha / (light_alpha + contrast + .0001f);
      const auto offset = static_cast<std::size_t>((y * size + x) * 4);
      if (horizon > .5f) {
        pixels[offset] = pixels[offset + 1] = 3;
        pixels[offset + 2] = 7;
      } else {
        const float red = std::min(1.f, (style.color.r / 255.f) * .92f + white);
        const float green = std::min(1.f, (style.color.g / 255.f) * .92f + white);
        const float blue = std::min(1.f, (style.color.b / 255.f) * .92f + white);
        pixels[offset] = channel(red * light_mix + .012f * (1.f - light_mix));
        pixels[offset + 1] = channel(green * light_mix + .020f * (1.f - light_mix));
        pixels[offset + 2] = channel(blue * light_mix + .035f * (1.f - light_mix));
      }
      pixels[offset + 3] = channel(alpha);
    }
  }
  return RgbaImage::create(size, size, std::move(pixels));
}
} // namespace

struct NativeGalaxyStarMarkerRenderer::Storage {
  struct Entry {
    GalaxyStarVisualClass visual{};
    std::shared_ptr<const RgbaImage> image;
  };
  std::vector<Entry> entries;
  std::size_t bytes{}, generated{};

  std::shared_ptr<const RgbaImage> obtain(GalaxyStarVisualClass visual) {
    const auto found = std::ranges::find(entries, visual, &Entry::visual);
    if (found != entries.end()) return found->image;
    auto image = make_marker(visual);
    if (entries.size() >= NativeGalaxyStarMarkerRenderer::maximum_cached_resources ||
        bytes + image->byte_size() > NativeGalaxyStarMarkerRenderer::maximum_cached_bytes) {
      throw std::logic_error("The fixed galaxy star marker palette exceeded its budget.");
    }
    bytes += image->byte_size();
    entries.push_back({visual, image});
    ++generated;
    return image;
  }
};

NativeGalaxyStarMarkerRenderer::NativeGalaxyStarMarkerRenderer()
    : storage_(std::make_unique<Storage>()) {}
NativeGalaxyStarMarkerRenderer::~NativeGalaxyStarMarkerRenderer() = default;
NativeGalaxyStarMarkerRenderer::NativeGalaxyStarMarkerRenderer(
    NativeGalaxyStarMarkerRenderer &&) noexcept = default;
NativeGalaxyStarMarkerRenderer &NativeGalaxyStarMarkerRenderer::operator=(
    NativeGalaxyStarMarkerRenderer &&) noexcept = default;

void NativeGalaxyStarMarkerRenderer::append(DrawList &out, Point center,
                                              float core_radius,
                                              const NativeGalaxyStarAppearance &appearance,
                                              bool selected,
                                              std::optional<UiRect> clip) {
  if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
      !std::isfinite(core_radius) || core_radius <= 0.f) {
    throw std::invalid_argument("Galaxy star marker geometry must be finite and positive.");
  }
  const auto place = [&](GalaxyStarVisualClass visual, Point offset, float scale) {
    const float extent = core_radius * 3.5f * scale;
    out.world.emplace_back(Image{storage_->obtain(visual),
                                 {center.x + offset.x - extent,
                                  center.y + offset.y - extent,
                                  extent * 2.f, extent * 2.f},
                                 std::nullopt, {255, 255, 255, 255}, clip});
  };
  if (selected) {
    out.world.emplace_back(Circle{center, core_radius * 1.9f,
                                  {111, 225, 255, 52}});
  }
  out.world.emplace_back(
      Circle{center, std::clamp(core_radius * 1.85f, 3.5f, 6.f),
             {1, 4, 9, 155}});
  place(appearance.primary, {}, 1.f);
  if (appearance.secondary) {
    place(*appearance.secondary,
          {core_radius * .88f, -core_radius * .48f}, .70f);
  }
  if (appearance.tertiary) {
    place(*appearance.tertiary,
          {-core_radius * .82f, core_radius * .54f}, .58f);
  }
}

NativeGalaxyStarMarkerStats NativeGalaxyStarMarkerRenderer::stats() const noexcept {
  return {storage_->entries.size(), storage_->bytes, storage_->generated};
}
void NativeGalaxyStarMarkerRenderer::clear() noexcept {
  storage_->entries.clear();
  storage_->bytes = 0;
}
} // namespace stellar::native_galaxy_ui
