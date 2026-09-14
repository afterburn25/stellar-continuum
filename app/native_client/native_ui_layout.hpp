#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::native_map {

enum class UiAction { None, Pause, Speed, Research, Continue, Save, Load, Exit };

struct NativeUiLayout {
  float scale{};
  int control_font_pixels{};
  int metric_font_pixels{};
  int heading_font_pixels{};
  UiRect pause;
  UiRect speed;
  UiRect research;
  UiRect day_text;
  UiRect status_text;
  UiRect menu_panel;
  UiRect menu_heading;
  UiRect continue_button;
  UiRect save_button;
  UiRect load_button;
  UiRect exit_button;

  [[nodiscard]] static NativeUiLayout for_viewport(int width,
                                                    int height) noexcept {
    const auto screen_width = static_cast<float>(width);
    const auto screen_height = static_cast<float>(height);
    const auto requested_scale = std::max(1.f, screen_height / 900.f);
    const auto width_scale = std::max(.5f, (screen_width - 36.f) / 300.f);
    const auto height_scale = std::max(.5f, (screen_height - 36.f) / 284.f);
    const auto scale = std::min({requested_scale, width_scale, height_scale});
    const auto center_x = screen_width * .5f;
    const auto center_y = screen_height * .5f;
    const auto inset = 18.f * scale;
    const auto button_width = 184.f * scale;
    const auto button_height = 38.f * scale;
    const auto gap = 9.f * scale;
    const auto first_y = center_y - 38.f * scale;
    const UiRect panel{center_x - 150.f * scale,
                       center_y - 126.f * scale,
                       300.f * scale,
                       284.f * scale};
    const auto status_width = std::max(
        0.f, std::min(720.f * scale, screen_width - inset * 2.f));

    return {
        scale,
        static_cast<int>(std::lround(17.f * scale)),
        static_cast<int>(std::lround(15.f * scale)),
        static_cast<int>(std::lround(22.f * scale)),
        {inset, inset, 76.f * scale, 32.f * scale},
        {inset + 86.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset + 200.f * scale, inset, 112.f * scale, 32.f * scale},
        {inset, 56.f * scale, 230.f * scale, 22.f * scale},
        {inset, 80.f * scale, status_width, 44.f * scale},
        panel,
        {panel.x + 12.f * scale, panel.y + 20.f * scale,
         panel.width - 24.f * scale, 34.f * scale},
        {center_x - button_width * .5f, first_y, button_width,
         button_height},
        {center_x - button_width * .5f, first_y + button_height + gap,
         button_width, button_height},
        {center_x - button_width * .5f,
         first_y + (button_height + gap) * 2.f, button_width, button_height},
        {center_x - button_width * .5f,
         first_y + (button_height + gap) * 3.f, button_width, button_height}};
  }

  [[nodiscard]] UiAction hit(Point point, bool menu_open) const noexcept {
    if (menu_open) {
      if (continue_button.contains(point)) return UiAction::Continue;
      if (save_button.contains(point)) return UiAction::Save;
      if (load_button.contains(point)) return UiAction::Load;
      if (exit_button.contains(point)) return UiAction::Exit;
      return UiAction::None;
    }
    if (pause.contains(point)) return UiAction::Pause;
    if (speed.contains(point)) return UiAction::Speed;
    if (research.contains(point)) return UiAction::Research;
    return UiAction::None;
  }
};

} // namespace stellar::native_map
