#pragma once

#include "map_camera.hpp"
#include "native_fleet_controller.hpp"

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <span>
#include <unordered_map>

namespace stellar::native_fleet_ui {

struct FleetRouteEffectStats {
  int routed_fleets{};
  int drawn_legs{};
  int chevron_segments{};
  int trail_strokes{};
  int position_circles{};
  int emitted_lines{};
};

// Appends the preserved map's active-fleet route presentation: a faint broad
// under-stroke, a dashed course line, a chevron on long legs, three bounded
// trail strokes behind the state-owned position and its progress circle. Only
// active player-owned fleets with destinations render; the courses are the
// player's own mission data and every system position is already a public map
// marker, so no leg is hidden by the survey filter. Everything derives from
// authoritative fleet state, so the effects freeze with the simulation and
// never invent animation while the campaign is paused.
FleetRouteEffectStats append_fleet_route_effects(
    stellar::native_map::DrawList &out,
    std::span<const stellar::native_fleet::NativeOwnFleet> fleets,
    const std::unordered_map<int, const stellar::core::StellarSystem *>
        &systems_by_id,
    const stellar::native_map::Camera &camera, int width, int height);

} // namespace stellar::native_fleet_ui
