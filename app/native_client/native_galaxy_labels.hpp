#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <functional>
#include <vector>

namespace stellar::native_galaxy_ui {

enum class NativeGalaxyLabelKind { system, empire, central_object };
enum class NativeGalaxyLabelObstacleKind { hud, star };

struct NativeGalaxyLabelCandidate {
  NativeGalaxyLabelKind kind{NativeGalaxyLabelKind::system};
  int stable_id{};
  native_map::Point anchor{};
  float anchor_radius{};
  native_map::Text label;
  bool selected{};
  double priority{};
};

struct NativeGalaxyLabelObstacle {
  native_map::UiRect bounds{};
  NativeGalaxyLabelObstacleKind kind{NativeGalaxyLabelObstacleKind::hud};
};

struct NativeGalaxyLabelPlacement {
  NativeGalaxyLabelKind kind{NativeGalaxyLabelKind::system};
  int stable_id{};
  bool selected{};
  native_map::Point anchor{};
  native_map::Text label;
  native_map::UiRect bounds{};
};

struct NativeGalaxyLabelLayoutStats {
  std::size_t candidates{};
  std::size_t measured{};
  std::size_t placed{};
  std::size_t selected_requested{};
  std::size_t selected_placed{};
  std::size_t label_overlaps{};
  std::size_t obstacle_overlaps{};
  std::size_t hud_overlaps{};
  std::size_t star_overlaps{};
  std::size_t outside_viewport{};
};

struct NativeGalaxyLabelLayout {
  std::vector<NativeGalaxyLabelPlacement> placements;
  NativeGalaxyLabelLayoutStats stats;
};

using NativeGalaxyLabelMeasurer =
    std::function<native_map::TextExtent(const native_map::Text &)>;

inline constexpr std::size_t maximum_measured_labels = 128;
inline constexpr std::size_t maximum_measured_empire_labels = 32;

// Finite positive obstacle footprints may extend arbitrarily beyond the view;
// layout clips them to the bounded viewport before collision checks.
[[nodiscard]] NativeGalaxyLabelLayout layout_native_galaxy_labels(
    std::vector<NativeGalaxyLabelCandidate> candidates,
    native_map::UiRect viewport,
    std::vector<NativeGalaxyLabelObstacle> obstacles,
    const NativeGalaxyLabelMeasurer &measure);

[[nodiscard]] NativeGalaxyLabelLayoutStats inspect_native_galaxy_labels(
    const std::vector<NativeGalaxyLabelPlacement> &placements,
    native_map::UiRect viewport,
    const std::vector<NativeGalaxyLabelObstacle> &obstacles);

} // namespace stellar::native_galaxy_ui
