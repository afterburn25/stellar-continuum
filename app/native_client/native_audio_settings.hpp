#pragma once

// Modal audio settings view ported from the reference MainMenuLayer audio
// panel: three live-applied volume sliders (master/music/SFX, 0-100), a Done
// button and Restore defaults. The view owns no mixer; the host applies each
// emitted settings payload through NativeAudioMixer::set_volumes.

#include "native_audio.hpp"

#include <stellar/engine/native_map_platform.hpp>

#include <vector>

namespace stellar::native_audio_settings {

using stellar::native_audio::NativeAudioSettings;

struct AudioSettingsLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect panel;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect hint;
  std::vector<stellar::native_map::UiRect> labels;
  std::vector<stellar::native_map::UiRect> tracks;
  std::vector<stellar::native_map::UiRect> values;
  stellar::native_map::UiRect done;
  stellar::native_map::UiRect defaults;

  [[nodiscard]] static AudioSettingsLayout for_viewport(int width,
                                                        int height);
};

enum class AudioSettingsCommand { None, Apply, Close };

struct AudioSettingsResult {
  AudioSettingsCommand command{AudioSettingsCommand::None};
  bool captured{};
  NativeAudioSettings values{};
};

class NativeAudioSettingsView final {
public:
  void open(NativeAudioSettings current) noexcept;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] const NativeAudioSettings &values() const noexcept {
    return values_;
  }
  // Index of the slider currently dragged (0 master, 1 music, 2 SFX); -1 idle.
  [[nodiscard]] int dragging() const noexcept { return dragging_; }

  [[nodiscard]] AudioSettingsResult
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width,
              int height) const;

private:
  [[nodiscard]] float *slider(int index) noexcept;

  bool visible_{};
  int dragging_{-1};
  stellar::native_map::Point pointer_{};
  NativeAudioSettings values_{};
};

} // namespace stellar::native_audio_settings
