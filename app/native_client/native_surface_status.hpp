#pragma once

#include "native_colony_controller.hpp"
#include <string_view>

namespace stellar::native_colony_ui {
enum class SurfaceSiteStatusKind { Constructing, Disabled, Damaged, Unstaffed, Unpowered, Operating };
struct SurfaceSiteStatus {
  SurfaceSiteStatusKind kind;
  std::string_view label, detail;
};

// Explain the first prerequisite that prevents operation. Core does not power
// unstaffed or broken facilities, so a false power flag alone is not a shortage.
[[nodiscard]] inline SurfaceSiteStatus surface_site_status(
    const stellar::native_colony::NativeSurfaceSite& site) noexcept {
  if (!site.complete)
    return {SurfaceSiteStatusKind::Constructing, "CONSTRUCTION",
            "Requires construction materials before operation."};
  if (!site.enabled)
    return {SurfaceSiteStatusKind::Disabled, "DISABLED",
            "This facility is switched off."};
  if (site.condition <= stellar::core::minimum_operational_condition)
    return {SurfaceSiteStatusKind::Damaged, "REPAIR NEEDED",
            "Condition is too low for operation."};
  if (!site.staffed)
    return {SurfaceSiteStatusKind::Unstaffed, "NO WORKERS",
            "Colony workforce cannot staff this facility."};
  if (!site.powered)
    return {SurfaceSiteStatusKind::Unpowered, "NO POWER",
            "Increase generation or reduce competing power demand."};
  return {SurfaceSiteStatusKind::Operating, "OPERATING",
          "Staffed and supplied with power."};
}
} // namespace stellar::native_colony_ui
