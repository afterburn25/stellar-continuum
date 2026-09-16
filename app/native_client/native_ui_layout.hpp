#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <cmath>

namespace stellar::native_map {

enum class UiAction {
  None,
  Pause,
  Speed,
  Research,
  Shipyard,
  Construction,
  Diplomacy,
  Notifications,
  Logistics,
  Missions,
  Economy,
  Continue,
  Save,
  Load,
  NewGame,
  Developer,
  DevTools,
  PlayerMode,
  Audio,
  Voice,
  Video,
  Support,
  Exit
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
  UiRect notifications;
  UiRect logistics;
  UiRect missions;
  UiRect economy;
  UiRect day_text;
  UiRect status_text;
  UiRect menu_panel;
  UiRect menu_heading;
  UiRect continue_button;
  UiRect save_button;
  UiRect load_button;
  UiRect new_game_button;
  UiRect developer_button;
  UiRect dev_tools_button;
  UiRect player_button;
  UiRect audio_button;
  UiRect voice_button;
  UiRect video_button;
  UiRect support_button;
  UiRect exit_button;
  UiRect mode_text;
  bool developer_menu{};

  // Source: MainMenuLayer campaign menu. Developer mode inserts a DEV TOOLS
  // row and a PLAYER CAMPAIGN row; Player mode shows a single DEVELOPMENT
  // row. The panel grows by the inserted rows.
  [[nodiscard]] static NativeUiLayout for_viewport(int width, int height,
                                                    bool developer = false) noexcept {
    const auto screen_width = static_cast<float>(width);
    const auto screen_height = static_cast<float>(height);
    const auto requested_scale = std::max(1.f, screen_height / 900.f);
    const auto width_scale = std::max(.5f, (screen_width - 36.f) / 300.f);
    const float row_count = developer ? 11.f : 10.f;
    const float panel_height = 88.f + row_count * 47.f + 34.f;
    const auto height_scale =
        std::max(.5f, (screen_height - 36.f) / panel_height);
    const auto scale = std::min({requested_scale, width_scale, height_scale});
    const auto center_x = screen_width * .5f;
    const auto center_y = screen_height * .5f;
    const auto inset = 18.f * scale;
    const auto button_width = 184.f * scale;
    const auto button_height = 38.f * scale;
    const auto gap = 9.f * scale;
    const UiRect panel{center_x - 150.f * scale,
                       center_y - panel_height * scale * .5f,
                       300.f * scale,
                       panel_height * scale};
    const auto first_y = panel.y + 88.f * scale;
    const auto status_width = std::max(
        0.f, std::min(720.f * scale, screen_width - inset * 2.f));
    const auto row = [&](const float index) {
      return UiRect{center_x - button_width * .5f,
                    first_y + (button_height + gap) * index, button_width,
                    button_height};
    };
    // Rows 0-3 are fixed; the mode rows then shift the settings tail.
    const float tail = developer ? 2.f : 1.f;

    return {
        scale,
        static_cast<int>(std::lround(17.f * scale)),
        static_cast<int>(std::lround(15.f * scale)),
        static_cast<int>(std::lround(22.f * scale)),
        {inset, inset, 76.f * scale, 32.f * scale},
        {inset + 86.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset + 200.f * scale, inset, 112.f * scale, 32.f * scale},
        {inset + 322.f * scale, inset, 112.f * scale, 32.f * scale},
        {inset + 444.f * scale, inset, 140.f * scale, 32.f * scale},
        {inset + 594.f * scale, inset, 128.f * scale, 32.f * scale},
        {inset + 732.f * scale, inset, 56.f * scale, 32.f * scale},
        {inset + 798.f * scale, inset, 84.f * scale, 32.f * scale},
        {inset + 892.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset + 1006.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset, 56.f * scale, 230.f * scale, 22.f * scale},
        {inset, 80.f * scale, status_width, 44.f * scale},
        panel,
        {panel.x + 12.f * scale, panel.y + 20.f * scale,
         panel.width - 24.f * scale, 34.f * scale},
        row(0.f),
        row(1.f),
        row(2.f),
        row(3.f),
        row(4.f),
        developer ? row(4.f) : UiRect{},
        developer ? row(5.f) : UiRect{},
        row(4.f + tail),
        row(5.f + tail),
        row(6.f + tail),
        row(7.f + tail),
        row(8.f + tail),
        {panel.x + 12.f * scale,
         first_y + (button_height + gap) * (8.f + tail) + button_height +
             12.f * scale,
         panel.width - 24.f * scale, 22.f * scale},
        developer};
  }

  [[nodiscard]] UiAction hit(Point point, bool menu_open) const noexcept {
    if (menu_open) {
      if (continue_button.contains(point)) return UiAction::Continue;
      if (save_button.contains(point)) return UiAction::Save;
      if (load_button.contains(point)) return UiAction::Load;
      if (new_game_button.contains(point)) return UiAction::NewGame;
      if (developer_menu) {
        if (dev_tools_button.contains(point)) return UiAction::DevTools;
        if (player_button.contains(point)) return UiAction::PlayerMode;
      } else if (developer_button.contains(point)) {
        return UiAction::Developer;
      }
      if (audio_button.contains(point)) return UiAction::Audio;
      if (voice_button.contains(point)) return UiAction::Voice;
      if (video_button.contains(point)) return UiAction::Video;
      if (support_button.contains(point)) return UiAction::Support;
      if (exit_button.contains(point)) return UiAction::Exit;
      return UiAction::None;
    }
    if (pause.contains(point)) return UiAction::Pause;
    if (speed.contains(point)) return UiAction::Speed;
    if (research.contains(point)) return UiAction::Research;
    if (shipyard.contains(point)) return UiAction::Shipyard;
    if (construction.contains(point)) return UiAction::Construction;
    if (diplomacy.contains(point)) return UiAction::Diplomacy;
    if (notifications.contains(point)) return UiAction::Notifications;
    if (logistics.contains(point)) return UiAction::Logistics;
    if (missions.contains(point)) return UiAction::Missions;
    if (economy.contains(point)) return UiAction::Economy;
    return UiAction::None;
  }
};

} // namespace stellar::native_map
