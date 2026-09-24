#pragma once
#include "native_menu_hover.hpp"
#include "native_dropdown.hpp"

#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace stellar::native_audio {

enum class VoiceFrequency { Minimal, Normal, Frequent };

struct VoicePreferences final {
  bool enabled{true};
  float volume{1.f};
  bool subtitles{true};
  int subtitle_size{18};
  float subtitle_background_opacity{1.f};
  bool speaker_labels{true};
  float communication_filter{};
  VoiceFrequency frequency{VoiceFrequency::Normal};
  bool no_interruptions{true};
  bool interface_announcements{};
  bool operator==(const VoicePreferences&) const = default;
};

struct VoiceSettingsLayout final {
  float scale{};
  int body_font_pixels{};
  int heading_font_pixels{};
  stellar::native_map::UiRect panel, title, introduction;
  stellar::native_map::UiRect enable_voices, volume_track, subtitles, subtitle_size;
  stellar::native_map::UiRect background_track, speaker_labels, filter_track, frequency, no_interruptions;
  stellar::native_map::UiRect interface_announcements;
  stellar::native_map::UiRect replay, stop, defaults, cancel, save, status;

  [[nodiscard]] static VoiceSettingsLayout for_viewport(int width, int height) noexcept;
};

class NativeVoiceSettings final {
 public:
  using Apply = std::function<void(const VoicePreferences&)>;
  using Action = std::function<void()>;

  explicit NativeVoiceSettings(std::filesystem::path, Apply = {}, Action replay = {}, Action stop = {});
  NativeVoiceSettings(const NativeVoiceSettings&) = delete;
  NativeVoiceSettings& operator=(const NativeVoiceSettings&) = delete;
  NativeVoiceSettings(NativeVoiceSettings&&) = delete;
  NativeVoiceSettings& operator=(NativeVoiceSettings&&) = delete;

  void set_hover_callback(std::function<void()> callback){hover_feedback_.set_callback(std::move(callback));}
  void set_localization(const stellar::engine::LocalizationTable* table){locale_=table;}
  void open();
  [[nodiscard]] bool visible() const;
  [[nodiscard]] int focused() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers; toggles/choices/sliders include their current value.
  // Empty when nothing is focused.
  [[nodiscard]] std::string focused_label() const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(int width, int height) const;
  // Normalized range of the ringed slider — null for non-slider controls.
  [[nodiscard]] std::optional<stellar::engine::AnnouncementRange>
  focused_range() const;
  [[nodiscard]] bool handle(const stellar::native_map::InputEvent&, int width, int height);
  void render(stellar::native_map::DrawList&, int width, int height) const;
  void cancel();
  [[nodiscard]] VoicePreferences values() const;
  [[nodiscard]] VoicePreferences saved_values() const;
  [[nodiscard]] std::string status() const;

 private:
  stellar::native_menu_audio::HoverFeedback hover_feedback_;
  stellar::native_ui::Dropdown dropdown_;
  enum class Dragged { None, Volume, Background, Filter };
  void require_owner() const;
  void load();
  void save();
  void preview();
  void set_from_track(Dragged, stellar::native_map::Point, const VoiceSettingsLayout&);
  void activate_at(const VoiceSettingsLayout&, stellar::native_map::Point);
  [[nodiscard]] std::string tr(std::string_view key, std::string_view fallback) const;

  std::thread::id owner_{std::this_thread::get_id()};
  std::filesystem::path path_;
  Apply apply_;
  Action replay_;
  Action stop_;
  VoicePreferences values_{};
  VoicePreferences saved_{};
  bool visible_{};
  Dragged dragging_{Dragged::None};
  int viewport_width_{};
  int viewport_height_{};
  std::string status_;
  bool save_diagnostic_emitted_{};
  int focus_{-1};
  const stellar::engine::LocalizationTable* locale_{};
};

} // namespace stellar::native_audio
