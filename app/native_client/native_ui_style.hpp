#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_ui_skin.hpp>

namespace stellar::native_ui_style {
using stellar::native_map::Color;
using stellar::native_map::DrawList;
using stellar::native_map::FilledRectangle;
using stellar::native_map::Line;
using stellar::native_map::Point;
using stellar::native_map::StrokedRectangle;
using stellar::native_map::UiRect;

inline void panel(DrawList &out, UiRect bounds, bool hover, bool active) {
  stellar::engine::ui_skin::control(out,bounds,hover,active);
}
inline void menu_panel(DrawList &out, UiRect bounds) {
  stellar::engine::ui_skin::surface(out,bounds);
}
} // namespace stellar::native_ui_style
