#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace stellar::engine {

// Engine-level accessibility settings. The UI and presentation layers read
// these rather than scattering per-screen toggles. All values validate on
// load; unknown enum spellings fall back to defaults rather than failing.

enum class ColorBlindMode {
  None,
  Protanopia,
  Deuteranopia,
  Tritanopia,
};

struct AccessibilitySettings {
  float ui_scale{1.0f};        // clamped 0.75..2.0
  float text_scale{1.0f};      // clamped 0.75..2.0
  bool high_contrast{false};
  ColorBlindMode color_blind{ColorBlindMode::None};
  bool reduce_motion{false};
  bool reduce_flashing{false};
  bool subtitles_enabled{true};
  float subtitle_scale{1.0f};  // clamped 0.75..2.0

  // Clamps every field into its supported range.
  void sanitize();

  std::string to_json() const;
  static AccessibilitySettings from_json(std::string_view document);
};

// Effective pixel scale combining UI and text preferences — the single
// number layout code should multiply font metrics by.
inline float effective_text_scale(const AccessibilitySettings &s) noexcept {
  return s.ui_scale * s.text_scale;
}

} // namespace stellar::engine
