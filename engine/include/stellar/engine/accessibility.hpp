#pragma once

#include <cstdint>
#include <deque>
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

  bool operator==(const AccessibilitySettings &) const = default;

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

// Screen-reader live-region substrate. Surfaces announce semantic text
// (focused-control labels, arriving events, mode changes); a platform
// bridge — or the client's caption channel — drains the queue. The queue
// is bounded and deterministic: assertive announcements preempt queued
// polite ones, consecutive duplicate text is dropped, and overflow
// evicts the oldest polite entries first.

enum class AnnouncementPriority {
  Polite,
  Assertive,
};

struct AccessibilityAnnouncement {
  std::string text;
  AnnouncementPriority priority{AnnouncementPriority::Polite};
  std::uint64_t sequence{};
};

class AccessibilityAnnouncer {
public:
  explicit AccessibilityAnnouncer(std::size_t capacity = 16) noexcept
      : capacity_(capacity ? capacity : 1) {}

  void announce(std::string text,
                AnnouncementPriority priority = AnnouncementPriority::Polite);
  // Oldest pending announcement, or nullopt when drained.
  [[nodiscard]] std::optional<AccessibilityAnnouncement> take();
  // Newest pending announcement without consuming it.
  [[nodiscard]] const AccessibilityAnnouncement *latest() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return pending_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return pending_.size(); }
  void clear() noexcept { pending_.clear(); }

private:
  std::deque<AccessibilityAnnouncement> pending_;
  std::size_t capacity_;
  std::uint64_t sequence_{};
};

} // namespace stellar::engine
