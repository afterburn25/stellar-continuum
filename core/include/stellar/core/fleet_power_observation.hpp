#pragma once

#include <string>

namespace stellar::core {

struct FleetPowerObservation {
  int observer_id{};
  int fleet_id{};
  double power{};
  double observed_day{};
  std::string evidence;

  [[nodiscard]] bool operator==(const FleetPowerObservation &other) const;
};

} // namespace stellar::core
