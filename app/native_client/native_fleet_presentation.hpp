#pragma once

#include "native_fleet_controller.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_fleet_ui {

struct ObservedSystemName {
  int system_id{};
  std::string name;
  bool known{};
};

struct FleetMarkerOffset {
  int fleet_id{};
  stellar::native_map::Point pixels{};
};

[[nodiscard]] std::string observer_safe_fleet_message(
    std::string_view message, std::span<const ObservedSystemName> systems,
    const stellar::engine::LocalizationTable *locale = nullptr);

[[nodiscard]] std::vector<FleetMarkerOffset> deterministic_fleet_marker_offsets(
    std::span<const stellar::native_fleet::NativeOwnFleet> fleets);

} // namespace stellar::native_fleet_ui
