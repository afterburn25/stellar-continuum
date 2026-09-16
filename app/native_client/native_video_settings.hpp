#pragma once

// Modal video settings view ported from the reference MainMenuLayer.cs VIDEO
// panel (VideoSettingsService): DISPLAY (borderless / exclusive fullscreen),
// V-SYNC (Off / On / Adaptive) and FRAME CAP (Automatic / 60 / 120 / 144 /
// Unlimited) cycle rows with the reference Apply → 15s CONFIRM DISPLAY →
// Keep/Revert rollback flow, persisted as video-settings.json next to the
// campaign save directory. The reference's RESOLUTION row stays intentionally
// absent: it is disabled upstream (fullscreen always follows the desktop
// mode) and its MSAA / 3D-resolution rows only affect the reference's 3D
// pipeline, which this 2D renderer does not have. The view owns no window
// state; the host applies each emitted payload through the Window setters
// and owns the rollback deadline.

#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <vector>

namespace stellar::native_video_settings {

enum class VideoDisplayMode { Borderless, Exclusive };
enum class VideoVsync { Off, On, Adaptive };
enum class VideoFrameCap { Automatic, Fps60, Fps120, Fps144, Unlimited };

struct NativeVideoSettings {
  VideoDisplayMode display{VideoDisplayMode::Borderless};
  VideoVsync vsync{VideoVsync::On};
  VideoFrameCap frame_cap{VideoFrameCap::Automatic};
  [[nodiscard]] NativeVideoSettings sanitized() const noexcept;
  [[nodiscard]] bool operator==(const NativeVideoSettings &) const = default;
  [[nodiscard]] static NativeVideoSettings load(const std::filesystem::path &);
  void save(const std::filesystem::path &) const;
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
  // 3 cycle rows: display, vsync, frame cap.
  std::vector<stellar::native_map::UiRect> choice_labels;
  std::vector<stellar::native_map::UiRect> choice_buttons;
  stellar::native_map::UiRect error;
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
  void open(NativeVideoSettings current) noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const NativeVideoSettings &values() const noexcept {
    return values_;
  }
  // Whether the CONFIRM DISPLAY overlay is up; the host raises it after
  // applying an Apply payload and lowers it on Keep/Revert.
  [[nodiscard]] bool confirming() const noexcept { return confirming_; }
  void set_confirming(bool confirming) noexcept { confirming_ = confirming; }
  void set_error(std::string message);

  [[nodiscard]] VideoSettingsResult
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height,
              double rollback_remaining_seconds = 0.) const;

private:
  void cycle_choice(int index) noexcept;

  bool visible_{};
  bool confirming_{};
  stellar::native_map::Point pointer_{};
  NativeVideoSettings values_{};
  std::string error_;
};

} // namespace stellar::native_video_settings
