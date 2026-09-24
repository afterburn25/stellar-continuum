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

// Focus marks labels raised because the keyboard/AT focus moved — platform
// bridges map it to a real focus-change event rather than a live-region note.
enum class AnnouncementKind {
  Status,
  Focus,
};

// Pixel-space bounds of the focused control a Focus announcement names —
// lets a platform bridge project real fragment geometry (magnifier tracking,
// highlight rectangles). Absent when a surface cannot project bounds.
struct AnnouncementBounds {
  float x{}, y{}, width{}, height{};
};

// Normalized range for focused sliders — platform bridges expose it as a
// value pattern so AT reports position within the control's range, not
// just the label text.
struct AnnouncementRange {
  double minimum{}, maximum{1.}, value{};
};

// Semantic role of the control a Focus announcement names — platform
// bridges map it to the OS control type (UIA ControlType, AT-SPI role) so
// AT announces "button" rather than a generic custom control. Custom is
// the honest fallback for composite or unclassified widgets.
enum class AnnouncementControl {
  Custom,
  Button,
  CheckBox,
  Edit,
  Slider,
  Group,
};

struct AccessibilityAnnouncement {
  std::string text;
  AnnouncementPriority priority{AnnouncementPriority::Polite};
  AnnouncementKind kind{AnnouncementKind::Status};
  std::optional<AnnouncementBounds> bounds;
  std::optional<AnnouncementRange> range;
  AnnouncementControl control{AnnouncementControl::Custom};
  std::uint64_t sequence{};
};

class AccessibilityAnnouncer {
public:
  explicit AccessibilityAnnouncer(std::size_t capacity = 16) noexcept
      : capacity_(capacity ? capacity : 1) {}

  void announce(std::string text,
                AnnouncementPriority priority = AnnouncementPriority::Polite);
  // A focused-control label — routed as a focus change by platform bridges.
  // bounds carries the control's pixel rect when the surface can project it;
  // control names the widget's semantic role for platform control typing.
  void announce_focus(std::string text,
                      std::optional<AnnouncementBounds> bounds = std::nullopt,
                      std::optional<AnnouncementRange> range = std::nullopt,
                      AnnouncementControl control = AnnouncementControl::Custom,
                      AnnouncementPriority priority = AnnouncementPriority::Polite) {
    announce(std::move(text), priority, AnnouncementKind::Focus,
             std::move(bounds), std::move(range), control);
  }
  // Oldest pending announcement, or nullopt when drained.
  [[nodiscard]] std::optional<AccessibilityAnnouncement> take();
  // Newest pending announcement without consuming it.
  [[nodiscard]] const AccessibilityAnnouncement *latest() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return pending_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return pending_.size(); }
  void clear() noexcept { pending_.clear(); }

private:
  void announce(std::string text, AnnouncementPriority priority,
                AnnouncementKind kind,
                std::optional<AnnouncementBounds> bounds,
                std::optional<AnnouncementRange> range,
                AnnouncementControl control);
  std::deque<AccessibilityAnnouncement> pending_;
  std::size_t capacity_;
  std::uint64_t sequence_{};
};

} // namespace stellar::engine
