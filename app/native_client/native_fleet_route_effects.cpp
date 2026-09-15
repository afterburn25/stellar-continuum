#include "native_fleet_route_effects.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace stellar::native_fleet_ui {
namespace {
using namespace stellar::native_map;

[[nodiscard]] Color route_color(const int alpha) noexcept {
  return {102, 232, 164, static_cast<std::uint8_t>(alpha)};
}
} // namespace

FleetRouteEffectStats append_fleet_route_effects(
    DrawList &out,
    std::span<const stellar::native_fleet::NativeOwnFleet> fleets,
    const std::unordered_map<int, const stellar::core::StellarSystem *>
        &systems_by_id,
    const Camera &camera, const int width, const int height) {
  FleetRouteEffectStats stats;
  const auto lines_before = out.lines.size();
  for (const auto &fleet : fleets) {
    if (!fleet.destination_system_id) continue;
    ++stats.routed_fleets;
    auto start =
        camera.project({fleet.position.x, fleet.position.y}, width, height);
    const std::array<int, 1> fallback{*fleet.destination_system_id};
    const std::span<const int> legs =
        fleet.planned_route_system_ids.empty()
            ? std::span<const int>(fallback)
            : std::span<const int>(fleet.planned_route_system_ids);
    bool current_leg = true;
    for (const int leg_target : legs) {
      const auto target = systems_by_id.find(leg_target);
      if (target == systems_by_id.end()) break;
      const auto end = camera.project(
          {target->second->position.x, target->second->position.y}, width,
          height);
      const float dx = end.x - start.x, dy = end.y - start.y;
      const float length = std::hypot(dx, dy);
      if (length > 0.f) {
        ++stats.drawn_legs;
        const float hx = dx / length, hy = dy / length, nx = -hy, ny = hx;
        for (int stripe = -2; stripe <= 2; stripe += 2)
          out.lines.push_back({{start.x + nx * stripe, start.y + ny * stripe},
                               {end.x + nx * stripe, end.y + ny * stripe},
                               route_color(18)});
        for (float cursor = 0.f; cursor + 8.f <= length; cursor += 16.f) {
          const float t0 = cursor / length, t1 = (cursor + 8.f) / length;
          out.lines.push_back({{start.x + dx * t0, start.y + dy * t0},
                               {start.x + dx * t1, start.y + dy * t1},
                               route_color(153)});
        }
        if (length > 40.f) {
          ++stats.chevron_segments;
          const Point tip{start.x + dx * .62f, start.y + dy * .62f};
          out.lines.push_back({tip,
                               {tip.x - hx * 7.f + nx * 3.5f,
                                tip.y - hy * 7.f + ny * 3.5f},
                               route_color(255)});
          out.lines.push_back({tip,
                               {tip.x - hx * 7.f - nx * 3.5f,
                                tip.y - hy * 7.f - ny * 3.5f},
                               route_color(255)});
        }
        if (current_leg && length > 4.f) {
          stats.trail_strokes += 3;
          for (int trail = 0; trail < 3; ++trail) {
            const float offset = 4.f + trail * 4.f;
            out.lines.push_back(
                {{start.x - hx * offset, start.y - hy * offset},
                 {start.x - hx * (offset + 2.4f),
                  start.y - hy * (offset + 2.4f)},
                 route_color(148 - trail * 41)});
          }
          ++stats.position_circles;
          out.circles.push_back({start, 2.2f, route_color(219)});
        }
      }
      start = end;
      current_leg = false;
    }
  }
  stats.emitted_lines = static_cast<int>(out.lines.size() - lines_before);
  return stats;
}
} // namespace stellar::native_fleet_ui
