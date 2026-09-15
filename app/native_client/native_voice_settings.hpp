#pragma once

// Modal voice & subtitles settings view ported from the reference
// VoicePlaybackController.Settings.cs panel: four toggles (voices, subtitles,
// speaker labels, no interruptions), three sliders (voice volume, subtitle
// background, communication filter), two cycle rows (subtitle size,
// announcement frequency) and Replay/Stop/Close actions. The developer voice
// lab is developer-mode only in the reference and has no native surface. The
// view owns no playback; the host applies each emitted settings payload
// through NativeVoicePlayback::apply_settings and persists it.

#include "native_voice.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <vector>

namespace stellar::native_voice_settings {

using stellar::native_voice::NativeVoiceSettings;
using stellar::native_voice::VoiceFrequency;

struct VoiceSettingsLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect panel;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect hint;
  // 4 toggles: voices, subtitles, speaker labels, no interruptions.
  std::vector<stellar::native_map::UiRect> toggle_labels;
  std::vector<stellar::native_map::UiRect> toggle_boxes;
  // 3 sliders: voice volume, subtitle background, communication filter.
  std::vector<stellar::native_map::UiRect> slider_labels;
  std::vector<stellar::native_map::UiRect> slider_tracks;
  std::vector<stellar::native_map::UiRect> slider_values;
  // 2 cycle rows: subtitle size, announcement frequency.
  std::vector<stellar::native_map::UiRect> choice_labels;
  std::vector<stellar::native_map::UiRect> choice_buttons;
  stellar::native_map::UiRect replay;
  stellar::native_map::UiRect stop;
  stellar::native_map::UiRect close;

  [[nodiscard]] static VoiceSettingsLayout for_viewport(int width, int height);
};

enum class VoiceSettingsCommand { None, Apply, Close, Replay, Stop };

struct VoiceSettingsResult {
  VoiceSettingsCommand command{VoiceSettingsCommand::None};
  bool captured{};
  NativeVoiceSettings values{};
};

class NativeVoiceSettingsView final {
public:
  void open(NativeVoiceSettings current) noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const NativeVoiceSettings &values() const noexcept {
    return values_;
  }
  // Index of the slider currently dragged (0 volume, 1 opacity, 2 filter);
  // -1 idle.
  [[nodiscard]] int dragging() const noexcept { return dragging_; }

  [[nodiscard]] VoiceSettingsResult
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width,
              int height) const;

private:
  [[nodiscard]] bool *toggle(int index) noexcept;
  [[nodiscard]] float *slider(int index) noexcept;
  void cycle_choice(int index) noexcept;

  bool visible_{};
  int dragging_{-1};
  stellar::native_map::Point pointer_{};
  NativeVoiceSettings values_{};
};

} // namespace stellar::native_voice_settings
