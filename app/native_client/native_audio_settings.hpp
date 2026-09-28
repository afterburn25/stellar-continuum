#pragma once
#include "native_menu_hover.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace stellar::native_audio {

struct AudioPreferences final {
  float master{.78f};
  float music{.64f};
  float effects{.82f};
  bool muted{};
  bool operator==(const AudioPreferences&) const = default;
};

struct AudioSettingsLayout final {
  float scale{};
  int body_font_pixels{};
  int heading_font_pixels{};
  stellar::native_map::UiRect panel, master_track, music_track, effects_track;
  stellar::native_map::UiRect mute, defaults, cancel, save, status, video, general;

  [[nodiscard]] static AudioSettingsLayout for_viewport(int width, int height) noexcept;
};

class NativeAudioSettings final {
 public:
  using Apply = std::function<void(const AudioPreferences&)>;
  using Confirm = std::function<void()>;

  explicit NativeAudioSettings(std::filesystem::path path, Apply apply = {}, Confirm confirm = {});
  NativeAudioSettings(const NativeAudioSettings&) = delete;
  NativeAudioSettings& operator=(const NativeAudioSettings&) = delete;
  NativeAudioSettings(NativeAudioSettings&&) = delete;
  NativeAudioSettings& operator=(NativeAudioSettings&&) = delete;

  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void set_video_navigation(Confirm callback) { require_owner(); video_navigation_ = std::move(callback); }
  void set_general_navigation(Confirm callback) { require_owner(); general_navigation_ = std::move(callback); }
  void set_localization(const stellar::engine::LocalizationTable* table){locale_=table;}
  void open();
  [[nodiscard]] bool visible() const;
  // While visible this consumes every event, including events outside the panel.
  [[nodiscard]] bool handle(const stellar::native_map::InputEvent&, int width, int height);
  void render(stellar::native_map::DrawList&, int width, int height) const;
  void cancel();
  void set_device_status(std::string message);
  [[nodiscard]] AudioPreferences values() const;
  [[nodiscard]] AudioPreferences saved_values() const;
  [[nodiscard]] std::string status() const;

 private:
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  enum class Dragged { None, Master, Music, Effects };
  void require_owner() const;
  void preview();
  void load();
  void save();
  void set_from_track(Dragged, stellar::native_map::Point,
                      const AudioSettingsLayout&);
  [[nodiscard]] std::string tr(std::string_view key, std::string_view fallback) const;

  std::thread::id owner_{std::this_thread::get_id()};
  std::filesystem::path path_;
  Apply apply_;
  Confirm confirm_;
  Confirm video_navigation_;
  Confirm general_navigation_;
  AudioPreferences values_;
  AudioPreferences saved_;
  std::string status_;
  std::string device_status_;
  bool visible_{};
  Dragged dragging_{Dragged::None};
  int viewport_width_{};
  int viewport_height_{};
  bool save_diagnostic_emitted_{};
  const stellar::engine::LocalizationTable* locale_{};
};

} // namespace stellar::native_audio
