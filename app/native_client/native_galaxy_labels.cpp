#include "native_galaxy_labels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <unordered_map>

namespace stellar::native_galaxy_ui {
namespace {
using native_map::Point;
using native_map::TextExtent;
using native_map::UiRect;

constexpr float cell_size = 64.f;
constexpr float label_gap = 4.f;
constexpr float collision_padding = 2.f;
constexpr float maximum_layout_coordinate = 1'000'000.f;
constexpr int maximum_text_extent = 16'384;

[[nodiscard]] bool finite_rect(const UiRect value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.width) && std::isfinite(value.height);
}

[[nodiscard]] bool positive_rect(const UiRect value) noexcept {
  if (!finite_rect(value) || value.width <= 0.f || value.height <= 0.f)
    return false;
  const double right = static_cast<double>(value.x) + value.width;
  const double bottom = static_cast<double>(value.y) + value.height;
  return std::abs(value.x) <= maximum_layout_coordinate &&
         std::abs(value.y) <= maximum_layout_coordinate &&
         value.width <= maximum_layout_coordinate &&
         value.height <= maximum_layout_coordinate &&
         std::abs(right) <= maximum_layout_coordinate &&
         std::abs(bottom) <= maximum_layout_coordinate;
}

[[nodiscard]] bool overlaps(const UiRect left, const UiRect right) noexcept {
  return left.x < right.x + right.width && right.x < left.x + left.width &&
         left.y < right.y + right.height && right.y < left.y + left.height;
}

[[nodiscard]] bool inside(const UiRect bounds, const UiRect viewport) noexcept {
  return bounds.x >= viewport.x && bounds.y >= viewport.y &&
         bounds.x + bounds.width <= viewport.x + viewport.width &&
         bounds.y + bounds.height <= viewport.y + viewport.height;
}

[[nodiscard]] UiRect inflate(const UiRect bounds, const float amount) noexcept {
  return {bounds.x - amount, bounds.y - amount,
          bounds.width + amount * 2.f, bounds.height + amount * 2.f};
}

[[nodiscard]] std::uint64_t cell_key(const int x, const int y) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u) |
         static_cast<std::uint32_t>(y);
}

struct CellRange {
  int x0{}, x1{}, y0{}, y1{};
};

[[nodiscard]] CellRange cells_for(const UiRect bounds) noexcept {
  constexpr float edge_epsilon = .001f;
  return {static_cast<int>(std::floor(bounds.x / cell_size)),
          static_cast<int>(
              std::floor((bounds.x + bounds.width - edge_epsilon) / cell_size)),
          static_cast<int>(std::floor(bounds.y / cell_size)),
          static_cast<int>(std::floor(
              (bounds.y + bounds.height - edge_epsilon) / cell_size))};
}

[[nodiscard]] bool candidate_before(const NativeGalaxyLabelCandidate &left,
                                    const NativeGalaxyLabelCandidate &right) {
  if (left.selected != right.selected) return left.selected;
  if (left.kind != right.kind)
    return static_cast<int>(left.kind) > static_cast<int>(right.kind);
  if (left.priority != right.priority) return left.priority > right.priority;
  if (left.stable_id != right.stable_id) return left.stable_id < right.stable_id;
  return left.label.value < right.label.value;
}

struct PlacementOptions {
  std::array<UiRect, 48> bounds{};
  std::size_t count{};
};

[[nodiscard]] PlacementOptions
placement_options(const NativeGalaxyLabelCandidate &candidate,
                  const TextExtent extent) {
  // Empire labels render a one-pixel drop shadow; their collision/audit bounds
  // cover the union of the measured foreground and that shadow.
  const float shadow = candidate.kind != NativeGalaxyLabelKind::system ? 1.f : 0.f;
  const float width = static_cast<float>(extent.width) + shadow;
  const float height = static_cast<float>(extent.height) + shadow;
  const float clearance = candidate.anchor_radius + label_gap;
  PlacementOptions options;
  if (candidate.kind != NativeGalaxyLabelKind::system) {
    const float horizontal_step = std::max(28.f, width * .25f);
    const float vertical_step = std::max(24.f, height + 10.f);
    for (int ring = 0; ring < 6; ++ring) {
      const float distance =
          clearance + 16.f + static_cast<float>(ring) * horizontal_step;
      options.bounds[options.count++] =
          {candidate.anchor.x - width * .5f,
           candidate.anchor.y - distance - height, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x - width * .5f,
           candidate.anchor.y + distance, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x + distance,
           candidate.anchor.y - height * .5f, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x - distance - width,
           candidate.anchor.y - height * .5f, width, height};
    }
    for (int ring = 0; ring < 6; ++ring) {
      const float horizontal =
          clearance + 16.f + static_cast<float>(ring) * horizontal_step;
      const float vertical =
          clearance + 16.f + static_cast<float>(ring) * vertical_step;
      options.bounds[options.count++] =
          {candidate.anchor.x + horizontal,
           candidate.anchor.y - vertical - height, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x - horizontal - width,
           candidate.anchor.y - vertical - height, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x + horizontal,
           candidate.anchor.y + vertical, width, height};
      options.bounds[options.count++] =
          {candidate.anchor.x - horizontal - width,
           candidate.anchor.y + vertical, width, height};
    }
    std::stable_sort(
        options.bounds.begin(),
        options.bounds.begin() + static_cast<std::ptrdiff_t>(options.count),
        [&](const UiRect left, const UiRect right) {
          const auto distance_squared = [&](const UiRect bounds) {
            const double dx = bounds.x + bounds.width * .5f - candidate.anchor.x;
            const double dy = bounds.y + bounds.height * .5f - candidate.anchor.y;
            return dx * dx + dy * dy;
          };
          return distance_squared(left) < distance_squared(right);
        });
    return options;
  }
  // System names stay below their own marker at every zoom. Crowded names
  // may step downward; they never jump to the sides or above the object.
  for(int row=0;row<3;++row)
    options.bounds[options.count++] =
        {candidate.anchor.x-width*.5f,candidate.anchor.y+clearance+row*(height+4.f),width,height};
  // A tight but still padded placement can keep a selected name visible in a
  // narrow gap between its marker and the HUD without moving it sideways.
  options.bounds[options.count++] =
      {candidate.anchor.x-width*.5f,candidate.anchor.y+candidate.anchor_radius+2.f,width,height};
  return options;
}

} // namespace

NativeGalaxyLabelLayoutStats inspect_native_galaxy_labels(
    const std::vector<NativeGalaxyLabelPlacement> &placements,
    const UiRect viewport,
    const std::vector<NativeGalaxyLabelObstacle> &obstacles) {
  if (!positive_rect(viewport))
    throw std::invalid_argument("Galaxy label viewport must be finite and positive.");

  NativeGalaxyLabelLayoutStats stats;
  stats.placed = placements.size();
  for (std::size_t left = 0; left < placements.size(); ++left) {
    if (!inside(placements[left].bounds, viewport)) ++stats.outside_viewport;
    if (placements[left].selected) ++stats.selected_placed;
    for (std::size_t right = left + 1; right < placements.size(); ++right)
      if (overlaps(placements[left].bounds, placements[right].bounds))
        ++stats.label_overlaps;

    bool hit_any_obstacle = false;
    for (const auto &obstacle : obstacles) {
      if (!overlaps(placements[left].bounds, obstacle.bounds)) continue;
      hit_any_obstacle = true;
      if (obstacle.kind == NativeGalaxyLabelObstacleKind::hud)
        ++stats.hud_overlaps;
      else
        ++stats.star_overlaps;
    }
    if (hit_any_obstacle) ++stats.obstacle_overlaps;
  }
  return stats;
}

NativeGalaxyLabelLayout layout_native_galaxy_labels(
    std::vector<NativeGalaxyLabelCandidate> candidates, const UiRect viewport,
    std::vector<NativeGalaxyLabelObstacle> obstacles,
    const NativeGalaxyLabelMeasurer &measure) {
  if (!positive_rect(viewport))
    throw std::invalid_argument("Galaxy label viewport must be finite and positive.");
  if (!measure)
    throw std::invalid_argument("Galaxy label layout requires a text measurer.");
  // Projected bodies may be millions of pixels away at close map zoom. Only
  // their intersection with this viewport can block a label. Clip before
  // applying spatial-grid limits, using doubles so finite float endpoints
  // cannot overflow while adding an oversized footprint's width or height.
  std::erase_if(obstacles, [&](auto &obstacle) {
    const auto bounds = obstacle.bounds;
    if (!finite_rect(bounds) || bounds.width <= 0.f || bounds.height <= 0.f)
      throw std::invalid_argument(
          "Galaxy label obstacles must be finite and positive.");
    const double left = std::max<double>(bounds.x, viewport.x);
    const double top = std::max<double>(bounds.y, viewport.y);
    const double right = std::min(static_cast<double>(bounds.x) + bounds.width,
                                  static_cast<double>(viewport.x) + viewport.width);
    const double bottom = std::min(static_cast<double>(bounds.y) + bounds.height,
                                   static_cast<double>(viewport.y) + viewport.height);
    if (left >= right || top >= bottom) return true;
    obstacle.bounds = {static_cast<float>(left), static_cast<float>(top),
                       static_cast<float>(right - left), static_cast<float>(bottom - top)};
    return false;
  });

  NativeGalaxyLabelLayout result;
  candidates.erase(
      std::remove_if(candidates.begin(), candidates.end(),
                     [&](const NativeGalaxyLabelCandidate &candidate) {
                       return !std::isfinite(candidate.anchor.x) ||
                              !std::isfinite(candidate.anchor.y) ||
                              !viewport.contains(candidate.anchor) ||
                              !std::isfinite(candidate.anchor_radius) ||
                              candidate.anchor_radius < 0.f ||
                              !std::isfinite(candidate.priority) ||
                              candidate.label.value.empty() ||
                              candidate.label.font_pixel_size <= 0 ||
                              !std::isfinite(candidate.label.wrap_width) ||
                              candidate.label.wrap_width < 0.f;
                     }),
      candidates.end());
  result.stats.candidates = candidates.size();
  result.stats.selected_requested = static_cast<std::size_t>(std::count_if(
      candidates.begin(), candidates.end(),
      [](const auto &candidate) { return candidate.selected; }));

  std::sort(candidates.begin(), candidates.end(), candidate_before);
  std::vector<NativeGalaxyLabelCandidate> system_candidates;
  std::vector<NativeGalaxyLabelCandidate> empire_candidates;
  for (auto &candidate : candidates) {
    auto &destination = candidate.kind == NativeGalaxyLabelKind::empire
                            ? empire_candidates
                            : system_candidates;
    destination.push_back(std::move(candidate));
  }
  const auto empire_limit = std::min(maximum_measured_empire_labels,
                                     empire_candidates.size());
  const auto system_limit = std::min(maximum_measured_labels - empire_limit,
                                     system_candidates.size());
  candidates.clear();
  candidates.reserve(system_limit + empire_limit);
  std::move(system_candidates.begin(),
            system_candidates.begin() +
                static_cast<std::ptrdiff_t>(system_limit),
            std::back_inserter(candidates));
  std::move(empire_candidates.begin(),
            empire_candidates.begin() +
                static_cast<std::ptrdiff_t>(empire_limit),
            std::back_inserter(candidates));
  std::sort(candidates.begin(), candidates.end(), candidate_before);

  std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
  const auto blocked_by_label = [&](const UiRect padded) {
    const auto range = cells_for(padded);
    for (int y = range.y0; y <= range.y1; ++y)
      for (int x = range.x0; x <= range.x1; ++x)
        if (const auto found = cells.find(cell_key(x, y));
            found != cells.end())
          for (const auto index : found->second)
            if (overlaps(padded,
                         inflate(result.placements[index].bounds,
                                 collision_padding)))
              return true;
    return false;
  };

  for (auto &candidate : candidates) {
    const auto extent = measure(candidate.label);
    ++result.stats.measured;
    if (extent.width <= 0 || extent.height <= 0 ||
        extent.width > maximum_text_extent ||
        extent.height > maximum_text_extent)
      continue;

    const auto options = placement_options(candidate, extent);
    for (std::size_t option = 0; option < options.count; ++option) {
      const auto bounds = options.bounds[option];
      const auto padded = inflate(bounds, collision_padding);
      if (!inside(padded, viewport) || blocked_by_label(padded) ||
          std::ranges::any_of(obstacles, [&](const auto &obstacle) {
            return overlaps(padded, obstacle.bounds);
          }))
        continue;

      candidate.label.at = {bounds.x, bounds.y};
      candidate.label.align = native_map::TextAlign::Left;
      const auto index = result.placements.size();
      result.placements.push_back({candidate.kind, candidate.stable_id,
                                   candidate.selected, candidate.anchor,
                                   std::move(candidate.label), bounds});
      const auto range = cells_for(padded);
      for (int y = range.y0; y <= range.y1; ++y)
        for (int x = range.x0; x <= range.x1; ++x)
          cells[cell_key(x, y)].push_back(index);
      break;
    }
  }

  const auto audit =
      inspect_native_galaxy_labels(result.placements, viewport, obstacles);
  result.stats.placed = audit.placed;
  result.stats.selected_placed = audit.selected_placed;
  result.stats.label_overlaps = audit.label_overlaps;
  result.stats.obstacle_overlaps = audit.obstacle_overlaps;
  result.stats.hud_overlaps = audit.hud_overlaps;
  result.stats.star_overlaps = audit.star_overlaps;
  result.stats.outside_viewport = audit.outside_viewport;
  return result;
}

} // namespace stellar::native_galaxy_ui
