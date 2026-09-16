#include "native_territory_overlay.hpp"

#include <stellar/engine/foundation.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
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

// Paints one region into a shared color atlas. Interior cells reproduce
// exactly; clipped boundary polygons are scan-converted at four texels per
// grid cell for a smooth edge.
void paint_fill(std::vector<std::uint8_t> &pixels, int width, int height,
                const NativeTerritoryRegion &region,
                const NativeTerritoryFogMask &fog, float cell_size,
                Color color) {
  const int texels = fill_texels_per_cell;
  const float texel = cell_size / texels;

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
        pixels[offset] = color.r;
        pixels[offset + 1] = color.g;
        pixels[offset + 2] = color.b;
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
        pixels[offset] = color.r;
        pixels[offset + 1] = color.g;
        pixels[offset + 2] = color.b;
        pixels[offset + 3] = 255;
      }
  }
}

[[nodiscard]] std::shared_ptr<const native_map::RgbaImage>
rasterize_fills(std::span<const NativeTerritoryRegion> regions,
                const NativeTerritoryFogMask &fog, float cell_size,
                int observer) {
  const int width = fog.width * fill_texels_per_cell;
  const int height = fog.height * fill_texels_per_cell;
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(width) * height * 4, 0);
  for (const auto &region : regions)
    paint_fill(pixels, width, height, region, fog, cell_size,
               native_territory_color(region.civilization_id, observer));
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

struct NativeTerritoryOverlay::Preparation final {
  struct Prepared final {
    NativeTerritoryProjection projection;
    std::shared_ptr<const native_map::RgbaImage> fog;
    std::shared_ptr<const native_map::RgbaImage> fill;
    std::size_t bytes{};
    int observer{};
    std::uint64_t fingerprint{}, generation{}, epoch{};
  };
  struct Cell final {
    std::mutex mutex;
    bool done{};
    std::uint64_t fingerprint{}, generation{}, epoch{};
    std::optional<Prepared> result;
    std::exception_ptr error;
  };
  struct Request final {
    NativeTerritoryInput input;
    std::uint64_t fingerprint{}, generation{}, epoch{};
    std::function<void()> before_prepare;
  };

  explicit Preparation() : owner(std::this_thread::get_id()), jobs(1) {}
  std::thread::id owner;
  stellar::engine::JobSystem jobs;
  std::shared_ptr<Cell> active;
  std::optional<Request> queued;
  std::uint64_t requested_fingerprint{}, requested_generation{}, epoch{};
  std::uint64_t failed_fingerprint{}, failed_generation{}, failed_epoch{};
  std::exception_ptr failed_error;
  bool failure_reported{};
  std::function<void()> test_hook;

  void require_owner() const {
    if (std::this_thread::get_id() != owner)
      throw std::logic_error("Native territory preparation is owner-thread only.");
  }
  static Prepared prepare(Request request) {
    Prepared output;
    output.projection = build_native_territory_projection(request.input);
    output.fog = rasterize_fog(output.projection.fog);
    if (output.fog->byte_size() > NativeTerritoryOverlay::maximum_cached_image_bytes)
      throw std::length_error("Native territory fog exceeds its image cache budget.");
    const float cell_size = output.projection.fog.size.x / output.projection.fog.width;
    output.fill = rasterize_fills(output.projection.territories, output.projection.fog,
                                 cell_size, request.input.observer_civilization_id);
    output.bytes = output.fog->byte_size() + output.fill->byte_size();
    if (output.bytes > NativeTerritoryOverlay::maximum_cached_image_bytes)
      throw std::length_error("Native territory fill exceeds its image cache budget.");
    output.observer = request.input.observer_civilization_id;
    output.fingerprint = request.fingerprint;
    output.generation = request.generation;
    output.epoch = request.epoch;
    return output;
  }
  void start(Request request) {
    active = std::make_shared<Cell>();
    const auto cell = active;
    cell->fingerprint = request.fingerprint;
    cell->generation = request.generation;
    cell->epoch = request.epoch;
    (void)jobs.submit([cell, request = std::move(request)]() mutable {
      std::optional<Prepared> result;
      std::exception_ptr error;
      try {
        if (request.before_prepare) request.before_prepare();
        result.emplace(prepare(std::move(request)));
      }
      catch (...) { error = std::current_exception(); }
      std::lock_guard lock(cell->mutex);
      cell->result = std::move(result);
      cell->error = std::move(error);
      cell->done = true;
    });
  }
};

NativeTerritoryOverlay::NativeTerritoryOverlay()
    : preparation_(std::make_unique<Preparation>()) {}
NativeTerritoryOverlay::~NativeTerritoryOverlay() = default;

void NativeTerritoryOverlay::request_update(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims,
    std::uint64_t generation) {
  preparation_->require_owner();
  auto input = capture_native_territory_input(world, observer_civilization_id,
                                              observer_claims, coordinate_scale);
  const auto fingerprint = native_territory_fingerprint(input);
  const bool same_request =
      preparation_->requested_fingerprint == fingerprint &&
      preparation_->requested_generation == generation;
  if (same_request && (preparation_->active || preparation_->queued ||
                       (valid_ && fingerprint_ == fingerprint)))
    return;
  if (preparation_->failed_error &&
      preparation_->failed_fingerprint == fingerprint &&
      preparation_->failed_generation == generation &&
      preparation_->failed_epoch == preparation_->epoch)
    return;
  preparation_->failed_error = {};
  preparation_->failure_reported = false;
  preparation_->requested_fingerprint = fingerprint;
  preparation_->requested_generation = generation;
  Preparation::Request request{std::move(input), fingerprint, generation,
                               preparation_->epoch, preparation_->test_hook};
  if (preparation_->active &&
      preparation_->active->fingerprint == request.fingerprint &&
      preparation_->active->generation == request.generation &&
      preparation_->active->epoch == request.epoch) {
    // A→B→A: the active A is already the latest requested result. Discard B
    // instead of submitting a duplicate A after the active job completes.
    preparation_->queued.reset();
    return;
  }
  if (!preparation_->active) {
    preparation_->start(std::move(request));
    return;
  }
  // One coalesced replacement owns the newest detached input. The active job
  // runs to completion privately; no Core object is retained by it.
  preparation_->queued = std::move(request);
}

void NativeTerritoryOverlay::poll() {
  if (!preparation_) return;
  preparation_->require_owner();
  if (preparation_->active) {
    std::optional<Preparation::Prepared> result;
    std::exception_ptr error;
    std::uint64_t finished_fingerprint{}, finished_generation{}, finished_epoch{};
    bool done{};
    {
      std::lock_guard lock(preparation_->active->mutex);
      done = preparation_->active->done;
      if (done) {
        finished_fingerprint = preparation_->active->fingerprint;
        finished_generation = preparation_->active->generation;
        finished_epoch = preparation_->active->epoch;
        result = std::move(preparation_->active->result);
        error = preparation_->active->error;
      }
    }
    if (done) {
      preparation_->active.reset();
      if (error && finished_fingerprint == preparation_->requested_fingerprint &&
          finished_generation == preparation_->requested_generation &&
          finished_epoch == preparation_->epoch) {
        preparation_->failed_error = error;
        preparation_->failed_fingerprint = finished_fingerprint;
        preparation_->failed_generation = finished_generation;
        preparation_->failed_epoch = finished_epoch;
        if (preparation_->queued &&
            preparation_->queued->fingerprint == finished_fingerprint &&
            preparation_->queued->generation == finished_generation &&
            preparation_->queued->epoch == finished_epoch)
          preparation_->queued.reset();
        if (!preparation_->failure_reported) {
          preparation_->failure_reported = true;
          std::rethrow_exception(error);
        }
      } else if (result && result->fingerprint == preparation_->requested_fingerprint &&
                 result->generation == preparation_->requested_generation &&
                 result->epoch == preparation_->epoch) {
        projection_ = std::move(result->projection);
        fog_image_ = std::move(result->fog);
        fill_image_ = std::move(result->fill);
        cached_image_bytes_ = result->bytes;
        fingerprint_ = result->fingerprint;
        observer_ = result->observer;
        valid_ = true;
      }
    }
  }
  if (!preparation_->active && preparation_->queued) {
    auto request = std::move(*preparation_->queued);
    preparation_->queued.reset();
    preparation_->start(std::move(request));
  }
}

bool NativeTerritoryOverlay::pending() const noexcept {
  return preparation_ && (preparation_->active || preparation_->queued);
}

void NativeTerritoryOverlay::set_preparation_hook_for_tests(
    std::function<void()> hook) {
  preparation_->require_owner();
  if (preparation_->active || preparation_->queued)
    throw std::logic_error("Preparation hook may only change while idle.");
  preparation_->test_hook = std::move(hook);
}

void NativeTerritoryOverlay::update(
    const core::FreshCampaignState &world, int observer_civilization_id,
    std::span<const core::TerritorialClaimSnapshot> observer_claims) {
  preparation_->require_owner();
  if (pending())
    throw std::logic_error("Synchronous territory update cannot mix with a worker request.");
  const auto fingerprint =
      native_territory_fingerprint(world, observer_civilization_id,
                                   observer_claims, coordinate_scale);
  if (valid_ && fingerprint == fingerprint_) return;
  auto projection =
      build_native_territory_projection(world, observer_civilization_id,
                                        observer_claims, coordinate_scale);
  auto fog_image = rasterize_fog(projection.fog);
  if (fog_image->byte_size() > maximum_cached_image_bytes)
    throw std::length_error("Native territory fog exceeds its image cache budget.");
  const std::size_t fog_bytes = fog_image->byte_size();
  const float cell_size =
      projection.fog.size.x / projection.fog.width;
  auto fill_image = rasterize_fills(projection.territories, projection.fog,
                                    cell_size, observer_civilization_id);
  if (fill_image->byte_size() > maximum_cached_image_bytes - fog_bytes)
    throw std::length_error("Native territory fill exceeds its image cache budget.");
  const std::size_t cached_image_bytes = fog_bytes + fill_image->byte_size();
  projection_ = std::move(projection);
  fog_image_ = std::move(fog_image);
  fill_image_ = std::move(fill_image);
  fingerprint_ = fingerprint;
  cached_image_bytes_ = cached_image_bytes;
  observer_ = observer_civilization_id;
  valid_ = true;
}

void NativeTerritoryOverlay::clear() noexcept {
  if (preparation_) {
    // A running worker is not joined here. Dropping the queued replacement and
    // changing the requested key makes every in-flight result stale at once.
    preparation_->queued.reset();
    preparation_->requested_fingerprint = 0;
    ++preparation_->epoch;
    ++preparation_->requested_generation;
    preparation_->failed_error = {};
    preparation_->failure_reported = false;
  }
  projection_ = {};
  fog_image_.reset();
  fill_image_.reset();
  fingerprint_ = 0;
  cached_image_bytes_ = 0;
  observer_ = 0;
  valid_ = false;
}

NativeTerritoryDrawStats NativeTerritoryOverlay::append(
    native_map::DrawList &out, const native_map::Camera &camera, int width,
    int height, float fitted_pixels_per_world,
    NativeTerritoryRenderStyle style) const {
  preparation_->require_owner();
  NativeTerritoryDrawStats stats;
  if (!valid_) return stats;
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

  if (fog_image_) {
    out.world.emplace_back(native_map::Image{
        fog_image_, rect_for(projection_.fog.position, projection_.fog.size),
        std::nullopt,
        map_alpha({0x05, 0x0b, 0x12, 255}, .095f + .095f * detail),
        std::nullopt});
    ++stats.fog_images;
  }

  if (fill_image_) {
    out.world.emplace_back(native_map::Image{
        fill_image_,
        rect_for(projection_.fog.position, projection_.fog.size),
        std::nullopt,
        // The former 3–5 alpha values disappeared into the regional artwork.
        // This remains translucent while making continuous ownership legible.
        map_alpha({255, 255, 255, 255}, .16f * detail),
        std::nullopt});
    ++stats.fill_images;
  }

  // Contours — the same loops that bound the rasterized fill, drawn as lines.
  for (const auto &region : projection_.territories) {
    const Color color = map_alpha(
        native_territory_color(region.civilization_id, observer_),
        .72f * detail);
    for (const auto &contour : region.contours) {
      if (contour.size() < 3) continue;
      for (std::size_t index = 0; index < contour.size(); ++index) {
        out.world.emplace_back(native_map::Line{
            project(contour[index]),
            project(contour[(index + 1) % contour.size()]), color});
        ++stats.contour_segments;
      }
    }
  }

  // Region labels are suppressed at the complete-galaxy overview unless a
  // civilization holds more than one anchor, exactly like the reference.
  if (style.draw_labels) {
    for (const auto &region : projection_.territories) {
      if (region.anchors.empty() ||
          (overview > .82f && region.anchors.size() < 2))
        continue;
      const Point point = project(region.label_position);
      std::string label = region.civilization_name;
      for (auto &character : label)
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
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
        ++stats.claim_segments;
        previous = next;
      }
    }
  }
  return stats;
}

} // namespace stellar::native_territory
