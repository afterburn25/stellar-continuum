#include "native_territory_overlay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace stellar::native_territory {
namespace {

using native_map::Color;
using native_map::Point;
using native_map::UiRect;

constexpr int fill_texels_per_cell = 4;
constexpr float tau = 6.2831853071795864769f;

[[nodiscard]] std::uint8_t alpha_channel(float opacity) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(opacity * 255.f), 0l, 255l));
}
[[nodiscard]] Color map_alpha(Color color, float opacity) {
  color.a = alpha_channel(opacity);
  return color;
}

[[nodiscard]] bool point_in_polygon(Point point,
                                    std::span<const Point> polygon) {
  bool inside = false;
  for (std::size_t index = 0, count = polygon.size(); index < count; ++index) {
    const Point a = polygon[index], b = polygon[(index + 1) % count];
    if ((a.y > point.y) == (b.y > point.y)) continue;
    const float crossing_x =
        a.x + (b.x - a.x) * (point.y - a.y) / (b.y - a.y);
    if (point.x < crossing_x) inside = !inside;
  }
  return inside;
}

// Rasterizes one civilization's smooth fill into a white-alpha mask over the
// fog extent. Interior cells reproduce exactly; the clipped boundary polygons
// are scan-converted at four texels per grid cell for a smooth edge.
[[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
rasterize_fill(const NativeTerritoryRegion &region,
               const NativeTerritoryFogMask &fog, float cell_size) {
  const int texels = fill_texels_per_cell;
  const int width = fog.width * texels, height = fog.height * texels;
  const float texel = cell_size / texels;
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(width) * height * 4, 0);

  const auto fill_rect = [&](Point position, Point size) {
    const int x0 = std::clamp(
        static_cast<int>(std::floor((position.x - fog.position.x) / texel)), 0,
        width);
    const int x1 = std::clamp(
        static_cast<int>(std::ceil(
            (position.x + size.x - fog.position.x) / texel)),
        0, width);
    const int y0 = std::clamp(
        static_cast<int>(std::floor((position.y - fog.position.y) / texel)), 0,
        height);
    const int y1 = std::clamp(
        static_cast<int>(std::ceil(
            (position.y + size.y - fog.position.y) / texel)),
        0, height);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const auto offset =
            (static_cast<std::size_t>(y) * width + x) * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
        pixels[offset + 3] = 255;
      }
  };

  for (const auto &run : region.fill_runs) fill_rect(run.position, run.size);

  for (const auto &polygon : region.fill_polygons) {
    Point min{std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()};
    Point max{std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()};
    for (const auto point : polygon) {
      min.x = std::min(min.x, point.x);
      min.y = std::min(min.y, point.y);
      max.x = std::max(max.x, point.x);
      max.y = std::max(max.y, point.y);
    }
    const int x0 = std::clamp(
        static_cast<int>(std::floor((min.x - fog.position.x) / texel)), 0,
        width);
    const int x1 = std::clamp(
        static_cast<int>(std::ceil((max.x - fog.position.x) / texel)), 0,
        width);
    const int y0 = std::clamp(
        static_cast<int>(std::floor((min.y - fog.position.y) / texel)), 0,
        height);
    const int y1 = std::clamp(
        static_cast<int>(std::ceil((max.y - fog.position.y) / texel)), 0,
        height);
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const Point sample{fog.position.x + (x + .5f) * texel,
                           fog.position.y + (y + .5f) * texel};
        if (!point_in_polygon(sample, polygon)) continue;
        const auto offset =
            (static_cast<std::size_t>(y) * width + x) * 4;
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
        pixels[offset + 3] = 255;
      }
  }
  return native_map::RgbaImage::create(width, height, std::move(pixels));
}

[[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
rasterize_fog(const NativeTerritoryFogMask &fog) {
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(fog.width) * fog.height * 4, 0);
  for (std::size_t index = 0; index < fog.alpha.size(); ++index) {
    const auto offset = index * 4;
    pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
    pixels[offset + 3] = fog.alpha[index];
  }
  return native_map::RgbaImage::create(fog.width, fog.height,
                                       std::move(pixels));
}

} // namespace

void NativeTerritoryOverlay::update(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims) {
  const auto fingerprint =
      native_territory_fingerprint(world, observer_civilization_id,
                                   observer_claims, coordinate_scale);
  if (valid_ && fingerprint == fingerprint_) return;
  auto projection =
      build_native_territory_projection(world, observer_civilization_id,
                                        observer_claims, coordinate_scale);
  fog_image_ = rasterize_fog(projection.fog);
  fill_images_.clear();
  fill_images_.reserve(projection.territories.size());
  const float cell_size =
      projection.fog.size.x / projection.fog.width;
  for (const auto &region : projection.territories)
    fill_images_.push_back(
        {region.civilization_id, rasterize_fill(region, projection.fog,
                                                cell_size)});
  projection_ = std::move(projection);
  fingerprint_ = fingerprint;
  observer_ = observer_civilization_id;
  valid_ = true;
}

void NativeTerritoryOverlay::clear() noexcept {
  projection_ = {};
  fog_image_.reset();
  fill_images_.clear();
  fingerprint_ = 0;
  observer_ = 0;
  valid_ = false;
}

void NativeTerritoryOverlay::append(native_map::DrawList &out,
                                    const native_map::Camera &camera,
                                    int width, int height,
                                    float fitted_pixels_per_world) const {
  if (!valid_) return;
  const float world_per_scaled = 1.f / coordinate_scale;
  const float scale = static_cast<float>(camera.pixels_per_world);
  const auto project = [&](Point scaled) {
    return camera.project({scaled.x * world_per_scaled,
                           scaled.y * world_per_scaled},
                          width, height);
  };
  const auto rect_for = [&](Point position, Point size) {
    const Point top_left = project(position);
    return UiRect{top_left.x, top_left.y,
                  size.x * scale * world_per_scaled,
                  size.y * scale * world_per_scaled};
  };
  const float overview = native_territory_overview_blend(
      scale * world_per_scaled, fitted_pixels_per_world * world_per_scaled);
  const float detail = native_territory_detail(overview);

  if (fog_image_)
    out.world.emplace_back(native_map::Image{
        fog_image_, rect_for(projection_.fog.position, projection_.fog.size),
        std::nullopt,
        map_alpha({0x05, 0x0b, 0x12, 255}, .095f + .095f * detail),
        std::nullopt});

  for (const auto &fill : fill_images_)
    out.world.emplace_back(native_map::Image{
        fill.image,
        rect_for(projection_.fog.position, projection_.fog.size),
        std::nullopt,
        map_alpha(native_territory_color(fill.civilization_id, observer_),
                  .042f * detail),
        std::nullopt});

  // Contours — the same loops that bound the rasterized fill, drawn as lines.
  for (const auto &region : projection_.territories) {
    const Color color = map_alpha(
        native_territory_color(region.civilization_id, observer_),
        .55f * detail);
    for (const auto &contour : region.contours) {
      if (contour.size() < 3) continue;
      for (std::size_t index = 0; index < contour.size(); ++index)
        out.world.emplace_back(native_map::Line{
            project(contour[index]),
            project(contour[(index + 1) % contour.size()]), color});
    }
  }

  // Region labels are suppressed at the complete-galaxy overview unless a
  // civilization holds more than one anchor, exactly like the reference.
  for (const auto &region : projection_.territories) {
    if (region.anchors.empty() ||
        (overview > .82f && region.anchors.size() < 2))
      continue;
    const Point point = project(region.label_position);
    std::string label = region.civilization_name;
    for (auto &character : label)
      character = static_cast<char>(std::toupper(
          static_cast<unsigned char>(character)));
    out.world.emplace_back(native_map::Text{
        {point.x + 1.f, point.y + 1.f}, label,
        map_alpha({0x05, 0x0b, 0x12, 255}, .9f), 13, 0.f, std::nullopt,
        native_map::TextAlign::Center});
    out.world.emplace_back(native_map::Text{
        point, label,
        map_alpha(native_territory_color(region.civilization_id, observer_),
                  .82f * detail),
        13, 0.f, std::nullopt, native_map::TextAlign::Center});
  }

  // Territorial claims read as dashed arcs over the claimed system.
  constexpr int segments = 24;
  for (const auto &claim : projection_.claims) {
    const Point point = project(claim.position);
    const float radius = claim.radius * scale * world_per_scaled;
    if (radius <= 5.f) continue;
    const Color color =
        map_alpha(native_territory_color(claim.civilization_id, observer_),
                  .74f * detail);
    for (int index = 0; index < segments; index += 2) {
      const float start = tau * index / segments;
      const float end = tau * (index + 1) / segments;
      // DrawArc pointCount=4 → three chord segments per dash.
      Point previous{point.x + std::cos(start) * radius,
                     point.y + std::sin(start) * radius};
      for (int part = 1; part <= 3; ++part) {
        const float angle = start + (end - start) * part / 3.f;
        const Point next{point.x + std::cos(angle) * radius,
                         point.y + std::sin(angle) * radius};
        out.world.emplace_back(native_map::Line{previous, next, color});
        previous = next;
      }
    }
  }
}

} // namespace stellar::native_territory
