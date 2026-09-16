#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::native_map {

inline constexpr float native_navigation_content_left = 70.f;

[[nodiscard]] inline constexpr bool native_navigation_available(
    bool menu_open, bool settlement_visible, bool diplomacy_modal,
    bool surface_modal, bool audio_settings_visible) noexcept {
  return !menu_open && !settlement_visible && !diplomacy_modal &&
         !surface_modal && !audio_settings_visible;
}

enum class UiAction {
  None,
  Pause,
  Speed,
  Research,
  Shipyard,
  Construction,
  Diplomacy,
  Continue,
  NewGame,
  Save,
  Load,
  Settings,
  Support,
  Notifications,
  Exit,
  Supply,
  Economy,
  Colonies
};

struct NativeUiLayout {
  float scale{};
  int control_font_pixels{};
  int metric_font_pixels{};
  int heading_font_pixels{};
  UiRect pause;
  UiRect speed;
  UiRect research;
  UiRect shipyard;
  UiRect construction;
  UiRect diplomacy;
  UiRect day_text;
  UiRect status_text;
  UiRect menu_panel;
  UiRect menu_heading;
  UiRect continue_button;
  UiRect save_button;
  UiRect load_button;
  UiRect exit_button;
  UiRect settings_button;
  UiRect notifications;
  UiRect support_button;
  UiRect new_game_button;
  UiRect supply;
  UiRect economy;
  UiRect colonies;

  [[nodiscard]] static NativeUiLayout for_viewport(int width,
                                                    int height) noexcept {
    const auto screen_width = static_cast<float>(width);
    const auto screen_height = static_cast<float>(height);
    const auto requested_scale = std::max(1.f, screen_height / 900.f);
    const auto width_scale = std::max(.5f, (screen_width - 36.f) / 300.f);
    const auto height_scale = std::max(.5f, (screen_height - 36.f) / 430.f);
    const auto scale = std::min({requested_scale, width_scale, height_scale});
    const auto center_x = screen_width * .5f;
    const auto center_y = screen_height * .5f;
    const auto inset = 18.f * scale;
    const auto button_width = 184.f * scale;
    const auto button_height = 38.f * scale;
    const auto gap = 9.f * scale;
    const auto first_y = center_y - 145.f * scale;
    const UiRect panel{center_x - 150.f * scale,
                       center_y - 215.f * scale,
                       300.f * scale,
                       430.f * scale};
    const auto status_x = inset + 200.f * scale;
    const auto status_width = std::max(
        0.f, std::min(720.f * scale, screen_width - status_x - inset - 114.f * scale));
    const auto rail_size = 44.f * scale;
    const auto rail_gap = 8.f * scale;
    const auto rail_y = std::min(138.f * scale, screen_height - inset - 7.f * rail_size - 6.f * rail_gap);

    return {
        scale,
        static_cast<int>(std::lround(17.f * scale)),
        static_cast<int>(std::lround(15.f * scale)),
        static_cast<int>(std::lround(22.f * scale)),
        {inset, inset, 76.f * scale, 32.f * scale},
        {inset + 86.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset, rail_y, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap), rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 2.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 3.f, rail_size, rail_size},
        {inset, 52.f * scale, 230.f * scale, 20.f * scale},
        {status_x, inset, status_width, 32.f * scale},
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
         first_y + (button_height + gap) * 6.f, button_width, button_height},
        {center_x - button_width * .5f,
         first_y + (button_height + gap) * 3.f, button_width, button_height},
        {screen_width - inset - 96.f * scale, inset, 96.f * scale, 32.f * scale},
        {center_x - button_width * .5f,
         first_y + (button_height + gap) * 4.f, button_width, button_height},
        {center_x - button_width * .5f,
         first_y + (button_height + gap) * 5.f, button_width, button_height},
        {inset, rail_y + (rail_size + rail_gap) * 4.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 5.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 6.f, rail_size, rail_size}};
  }

  [[nodiscard]] UiAction hit(Point point, bool menu_open) const noexcept {
    if (menu_open) {
      if (continue_button.contains(point)) return UiAction::Continue;
      if (save_button.contains(point)) return UiAction::Save;
      if (load_button.contains(point)) return UiAction::Load;
      if (settings_button.contains(point)) return UiAction::Settings;
      if (support_button.contains(point)) return UiAction::Support;
      if (new_game_button.contains(point)) return UiAction::NewGame;
      if (exit_button.contains(point)) return UiAction::Exit;
      return UiAction::None;
    }
    if (pause.contains(point)) return UiAction::Pause;
    if (speed.contains(point)) return UiAction::Speed;
    if (notifications.contains(point)) return UiAction::Notifications;
    if (research.contains(point)) return UiAction::Research;
    if (shipyard.contains(point)) return UiAction::Shipyard;
    if (construction.contains(point)) return UiAction::Construction;
    if (diplomacy.contains(point)) return UiAction::Diplomacy;
    if (supply.contains(point)) return UiAction::Supply;
    if (economy.contains(point)) return UiAction::Economy;
    if (colonies.contains(point)) return UiAction::Colonies;
    return UiAction::None;
  }
};

} // namespace stellar::native_map
