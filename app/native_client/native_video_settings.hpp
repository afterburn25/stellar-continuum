#pragma once
#include "native_menu_hover.hpp"
#include "native_dropdown.hpp"

// Modal video settings view ported from the reference MainMenuLayer.cs VIDEO
// panel (VideoSettingsService): DISPLAY (borderless / exclusive fullscreen),
// RESOLUTION (exclusive fullscreen only), V-SYNC (Off / On / Adaptive) and FRAME CAP (Automatic / 60 / 120 / 144 /
// Unlimited) dropdown rows with the reference Apply → 15s CONFIRM DISPLAY →
// Keep/Revert rollback flow, persisted as video-settings.json next to the
// campaign save directory. The view owns no window
// state; the host applies each emitted payload through the Window setters
// and owns the rollback deadline.

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_video_settings {

enum class VideoDisplayMode { Borderless, Exclusive, Windowed };
enum class VideoVsync { Off, On, Adaptive };
enum class VideoFrameCap { Automatic, Fps60, Fps120, Fps144, Unlimited };

struct NativeVideoSettings {
  VideoDisplayMode display{VideoDisplayMode::Borderless};
  int width{}, height{};
  float refresh_hz{};
  VideoVsync vsync{VideoVsync::On};
  VideoFrameCap frame_cap{VideoFrameCap::Automatic};
  int scene_resolution_percent{100}, scene_samples{1};
  int starfield_quality{2}, starfield_density{1};
  [[nodiscard]] NativeVideoSettings sanitized() const noexcept;
  // Startup never changes desktop geometry. Saved exclusive resolution/refresh
  // choices remain available when explicitly selecting Exclusive in Settings.
  [[nodiscard]] NativeVideoSettings for_startup() const noexcept {
    auto value=sanitized();
    if(value.display==VideoDisplayMode::Exclusive)value.display=VideoDisplayMode::Borderless;
    return value;
  }
  [[nodiscard]] bool operator==(const NativeVideoSettings &) const = default;
  [[nodiscard]] static NativeVideoSettings load(const std::filesystem::path &);
  void save(const std::filesystem::path &) const;
};

struct VideoDisplayChoice {
  int width{}, height{};
  float refresh_hz{};
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool operator==(const VideoDisplayChoice &) const = default;
};

struct VideoSettingsLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect panel;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect hint;
  stellar::native_map::UiRect adapter;
  // Display, resolution, V-Sync, frame cap, smoothing and scene resolution.
  std::vector<stellar::native_map::UiRect> choice_labels;
  std::vector<stellar::native_map::UiRect> choice_buttons;
  stellar::native_map::UiRect error, quality_hint, nvidia;
  stellar::native_map::UiRect apply;
  stellar::native_map::UiRect cancel;
  // CONFIRM DISPLAY rollback overlay.
  stellar::native_map::UiRect confirm_panel;
  stellar::native_map::UiRect confirm_title;
  stellar::native_map::UiRect confirm_text;
  stellar::native_map::UiRect keep;
  stellar::native_map::UiRect revert;

  [[nodiscard]] static VideoSettingsLayout for_viewport(int width, int height);
};

enum class VideoSettingsCommand { None, Apply, Cancel, Keep, Revert };

struct VideoSettingsResult {
  VideoSettingsCommand command{VideoSettingsCommand::None};
  bool captured{};
  NativeVideoSettings values{};
};

class NativeVideoSettingsView final {
public:
  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void open(NativeVideoSettings current) noexcept;
  void set_adapter(std::string value, std::function<void()> open_panel) {adapter_label_=std::move(value);open_panel_=std::move(open_panel);}
  void close() noexcept;
  void set_display_choices(std::vector<VideoDisplayChoice>,
                           std::string actual_display_label);
  void set_windowed_choices(std::vector<VideoDisplayChoice> choices);
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const NativeVideoSettings &values() const noexcept {
    return values_;
  }
  // Whether the CONFIRM DISPLAY overlay is up; the host raises it after
  // applying an Apply payload and lowers it on Keep/Revert.
  [[nodiscard]] bool confirming() const noexcept { return confirming_; }
  void set_confirming(bool confirming) noexcept { confirming_ = confirming; dropdown_.close(); }
  void set_error(std::string message);
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

  [[nodiscard]] VideoSettingsResult
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height,
              double rollback_remaining_seconds = 0.) const;

private:
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  void open_choice(int index);
  void select_choice(int index, int option) noexcept;
  void reconcile_resolution() noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string trf(std::string_view key, std::string_view arg,
                                std::string_view fallback) const;

  bool visible_{};
  const stellar::engine::LocalizationTable *locale_{};
  stellar::native_ui::Dropdown dropdown_;
  bool confirming_{};
  stellar::native_map::Point pointer_{};
  NativeVideoSettings values_{};
  std::vector<VideoDisplayChoice> display_choices_;
  std::vector<VideoDisplayChoice> windowed_choices_;
  std::string actual_display_label_{"Desktop default"};
  std::string error_;
  std::string adapter_label_;
  std::function<void()> open_panel_;
};

} // namespace stellar::native_video_settings
