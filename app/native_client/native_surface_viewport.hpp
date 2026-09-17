#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <utility>
namespace stellar::native_colony_ui {
struct SurfaceViewport {
  double center_x{}, center_z{}, pixels_per_unit{.5};
  [[nodiscard]] stellar::native_map::Point world_to_screen(double, double, stellar::native_map::UiRect) const noexcept;
  [[nodiscard]] std::pair<double,double> screen_to_world(stellar::native_map::Point, stellar::native_map::UiRect) const noexcept;
  [[nodiscard]] SurfaceViewport translated(float,float) const noexcept;
  [[nodiscard]] SurfaceViewport zoomed_at(float,stellar::native_map::Point,stellar::native_map::UiRect,double,double) const noexcept;
};
} // namespace stellar::native_colony_ui
