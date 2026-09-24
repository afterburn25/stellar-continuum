#pragma once

#include <stellar/engine/native_map_platform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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
  Map,
  Home,
  Inspect,
  ZoomIn,
  ZoomOut,
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
  Colonies,
  Explore,
  Menu,
  Missions,
  SwitchView
};

struct NativeUiLayout {
  float scale{};
  int control_font_pixels{};
  int metric_font_pixels{};
  int heading_font_pixels{};
  UiRect pause;
  UiRect speed;
  UiRect map;
  UiRect home;
  UiRect inspect;
  UiRect zoom_in;
  UiRect zoom_out;
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
  UiRect explore;
  UiRect menu;
  UiRect zoom_text;
  UiRect navigation_bar,brand;
  // Rail utility slot below Explore — assigned via secondary(5), not the
  // positional initializer, so it must trail every listed member.
  UiRect missions;

  // Accessibility user multiplier (interface-scale preference). Main-thread
  // presentation only — applied on top of the viewport-derived scale.
  static inline float user_scale_factor = 1.f;
  static void set_user_scale(float factor) noexcept {
    user_scale_factor = std::clamp(factor, .75f, 2.f);
  }
  [[nodiscard]] static float user_scale() noexcept { return user_scale_factor; }

  // Accessibility text multiplier (AccessibilitySettings::text_scale) —
  // enlarges the shared font metrics without growing chrome geometry, so
  // larger text does not shrink the world viewport.
  static inline float text_scale_factor = 1.f;
  static void set_text_scale(float factor) noexcept {
    text_scale_factor = std::clamp(factor, .75f, 2.f);
  }
  [[nodiscard]] static float text_scale() noexcept { return text_scale_factor; }

  [[nodiscard]] static NativeUiLayout for_viewport(int width,
                                                    int height) noexcept {
    const auto screen_width = static_cast<float>(width);
    const auto screen_height = static_cast<float>(height);
    const auto requested_scale =
        std::max(1.f, screen_height / 1080.f) * user_scale_factor;
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
    const auto rail_gap = std::max(2.f, 4.f * scale);
    const auto rail_size = std::min(38.f * scale,
        std::max(12.f, (screen_height - 2.f * inset - 60.f * scale - 13.f * rail_gap) / 14.f));
    const auto rail_y = inset + 60.f * scale;

    const auto text = text_scale_factor;
    auto result = NativeUiLayout{
        scale,
        static_cast<int>(std::lround(17.f * scale * text)),
        static_cast<int>(std::lround(15.f * scale * text)),
        static_cast<int>(std::lround(22.f * scale * text)),
        {inset, inset, 76.f * scale, 32.f * scale},
        {inset + 86.f * scale, inset, 104.f * scale, 32.f * scale},
        {inset, rail_y, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap), rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 2.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 3.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 4.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 6.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 8.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 7.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 12.f, rail_size, rail_size},
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
        {inset, rail_y + (rail_size + rail_gap) * 11.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 5.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 10.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 9.f, rail_size, rail_size},
        {inset, rail_y + (rail_size + rail_gap) * 13.f, rail_size, rail_size},
        {inset + 242.f * scale, 52.f * scale, 210.f * scale, 20.f * scale}};
    // A thin strategic strip leaves the viewport to the world. All controls
    // share these bounds for drawing and mouse interaction.
    result.pause={screen_width-146.f*scale,3.f*scale,40.f*scale,30.f*scale};
    result.speed={screen_width-102.f*scale,3.f*scale,90.f*scale,30.f*scale};
    result.notifications={screen_width-216.f*scale,3.f*scale,64.f*scale,30.f*scale};
    result.day_text={screen_width-340.f*scale,2.f*scale,116.f*scale,32.f*scale};
    result.status_text={80.f*scale,screen_height-30.f*scale,std::max(0.f,screen_width*.5f-310.f*scale),20.f*scale};
    result.navigation_bar={0,36.f*scale,screen_width,66.f*scale};
    result.brand={18.f*scale,44.f*scale,190.f*scale,46.f*scale};
    const float start=226.f*scale,nav_width=std::min(116.f*scale,std::max(64.f*scale,(screen_width-start-76.f*scale)/8.f));
    const auto nav=[&](int i){return UiRect{start+i*nav_width,40.f*scale,nav_width-4.f*scale,58.f*scale};};
    result.map=nav(0);result.home=nav(1);result.colonies=nav(2);result.economy=nav(3);
    result.research=nav(4);result.diplomacy=nav(5);result.supply=nav(6);result.shipyard=nav(7);
    result.menu={screen_width-52.f*scale,49.f*scale,34.f*scale,34.f*scale};
    const auto secondary=[&](int i){return UiRect{inset,120.f*scale+i*(rail_size+rail_gap),rail_size,rail_size};};
    result.inspect=secondary(0);result.zoom_in=secondary(1);result.zoom_out=secondary(2);result.construction=secondary(3);result.explore=secondary(4);
    result.missions=secondary(5);
    result.zoom_text={80.f*scale,110.f*scale,210.f*scale,20.f*scale};
    return result;
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
    if (map.contains(point)) return UiAction::Map;
    if (home.contains(point)) return UiAction::Home;
    if (inspect.contains(point)) return UiAction::Inspect;
    if (zoom_in.contains(point)) return UiAction::ZoomIn;
    if (zoom_out.contains(point)) return UiAction::ZoomOut;
    if (notifications.contains(point)) return UiAction::Notifications;
    if (research.contains(point)) return UiAction::Research;
    if (shipyard.contains(point)) return UiAction::Shipyard;
    if (construction.contains(point)) return UiAction::Construction;
    if (diplomacy.contains(point)) return UiAction::Diplomacy;
    if (supply.contains(point)) return UiAction::Supply;
    if (economy.contains(point)) return UiAction::Economy;
    if (colonies.contains(point)) return UiAction::Colonies;
    if (explore.contains(point)) return UiAction::Explore;
    if (missions.contains(point)) return UiAction::Missions;
    if (menu.contains(point)) return UiAction::Menu;
    return UiAction::None;
  }

  // Always-on chrome in keyboard focus order — top strip left-to-right, then
  // the navigation bar, then the left rail. `notifications` is only clickable
  // when the feed is available; callers drop it from the ring in that case.
  [[nodiscard]] std::array<std::pair<UiRect, UiAction>, 18> hud_actions()
      const noexcept {
    return {{{notifications, UiAction::Notifications},
             {pause, UiAction::Pause},
             {speed, UiAction::Speed},
             {map, UiAction::Map},
             {home, UiAction::Home},
             {colonies, UiAction::Colonies},
             {economy, UiAction::Economy},
             {research, UiAction::Research},
             {diplomacy, UiAction::Diplomacy},
             {supply, UiAction::Supply},
             {shipyard, UiAction::Shipyard},
             {menu, UiAction::Menu},
             {inspect, UiAction::Inspect},
             {zoom_in, UiAction::ZoomIn},
             {zoom_out, UiAction::ZoomOut},
             {construction, UiAction::Construction},
             {explore, UiAction::Explore},
             {missions, UiAction::Missions}}};
  }
};

inline float native_workspace_top(int width,int height)noexcept{
  const auto layout=NativeUiLayout::for_viewport(width,height);
  return layout.navigation_bar.y+layout.navigation_bar.height+10.f*layout.scale;
}

// Shared by presentation and hit testing, in drawable pixels (never desktop DPI).
struct CommandHudLayout {
  float scale{};
  UiRect resource_strip, context, crest, switch_view;
  static CommandHudLayout make(int width, int height) {
    const float s = NativeUiLayout::for_viewport(width,height).scale;
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const float plate = std::min(430.f*s,w*.48f);
    const UiRect context{(w-plate)*.5f,h-66.f*s,plate,58.f*s};
    return {s,{0,0,w,36.f*s},context,
        {context.x+8.f*s,context.y+9.f*s,40.f*s,40.f*s},
        {context.x+context.width-52.f*s,context.y+7.f*s,44.f*s,44.f*s}};
  }
};

} // namespace stellar::native_map
