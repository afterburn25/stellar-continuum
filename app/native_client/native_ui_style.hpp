#pragma once

#include <stellar/engine/native_map_platform.hpp>

namespace stellar::native_ui_style {
using stellar::native_map::Color;
using stellar::native_map::DrawList;
using stellar::native_map::FilledRectangle;
using stellar::native_map::Line;
using stellar::native_map::Point;
using stellar::native_map::StrokedRectangle;
using stellar::native_map::UiRect;

inline void panel(DrawList &out, UiRect bounds, bool hover, bool active) {
  const Color base = active ? Color{15, 74, 88, 250}
                            : hover ? Color{16, 57, 76, 248}
                                    : Color{8, 28, 47, 244};
  out.overlay.emplace_back(FilledRectangle{bounds, base});
  out.overlay.emplace_back(FilledRectangle{{bounds.x + 1.f, bounds.y + 1.f,
      bounds.width - 2.f, 2.f}, active ? Color{101, 232, 202, 170}
                                     : Color{90, 172, 205, 120}});
  out.overlay.emplace_back(StrokedRectangle{bounds, active ? Color{118, 238, 205, 255}
                                                           : Color{82, 155, 194, 230}});
  out.overlay.emplace_back(Line{{bounds.x + 3.f, bounds.y + bounds.height - 3.f},
      {bounds.x + bounds.width - 3.f, bounds.y + bounds.height - 3.f},
      Color{7, 13, 25, 180}});
}
inline void menu_panel(DrawList &out, UiRect bounds) {
  out.overlay.emplace_back(FilledRectangle{bounds, {5, 16, 31, 247}});
  out.overlay.emplace_back(FilledRectangle{{bounds.x + 2.f, bounds.y + 2.f,
      bounds.width - 4.f, 3.f}, {77, 193, 207, 150}});
  out.overlay.emplace_back(StrokedRectangle{bounds, {99, 190, 211, 250}});
}
} // namespace stellar::native_ui_style
