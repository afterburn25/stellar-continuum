#pragma once

// Bounded, player-visible recent-event presentation.  Publishers are
// responsible for observer filtering; this UI never reads simulation state.

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <deque>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stellar::native_notifications {

// Presentation urgency assigned by the publisher — orthogonal to category.
// Renders as a card accent bar (plus a "!" marker on Alert) so combat losses
// cannot hide between completion notices in a flat feed.
enum class NotificationSeverity { Info, Positive, Caution, Alert };

struct NativePlayerNotification {
  std::int64_t sequence{};
  std::string category, date, message;
  // Catalog key/format-arg for fixed publisher messages; empty keeps the
  // composed `message` literal (core-emitted summaries stay English).
  std::string message_key, message_arg;
  std::optional<int> diplomatic_contact_id;
  std::optional<int> system_id; // located events can navigate there
  NotificationSeverity severity{NotificationSeverity::Info};
};

class NativeNotificationFeed final {
 public:
  static constexpr std::size_t maximum_items = 32;

  void publish(std::string category, std::string date, std::string message,
               std::optional<int> diplomatic_contact_id = std::nullopt,
               std::optional<int> system_id = std::nullopt,
               std::string message_key = {}, std::string message_arg = {},
               NotificationSeverity severity = NotificationSeverity::Info);
  [[nodiscard]] const std::deque<NativePlayerNotification>& items() const noexcept { return items_; }
  [[nodiscard]] std::int64_t latest_sequence() const noexcept { return next_sequence_ - 1; }
  [[nodiscard]] int unread_count(std::int64_t last_read) const noexcept;
  void clear() noexcept { items_.clear(); }

 private:
  std::deque<NativePlayerNotification> items_;
  std::int64_t next_sequence_{1};
};

struct NotificationCardLayout {
  std::size_t item_index{}; // Index into the feed, newest first in layout order.
  native_map::UiRect bounds, metadata_bounds, message_bounds;
  std::optional<native_map::UiRect> contact_button, system_button;
};

struct NotificationLayout {
  native_map::UiRect panel, header, close_button, chronicle_button,
      filter_all, filter_important, filter_category, empty_hint,
      list_viewport;
  std::vector<native_map::UiRect> cards;
  std::vector<std::optional<native_map::UiRect>> contact_buttons;
  std::vector<NotificationCardLayout> entries;
  float scale{};
  stellar::engine::ScrollView scroll{};
};

using TextMeasurer = std::function<native_map::TextExtent(const native_map::Text&)>;

[[nodiscard]] NotificationLayout notification_layout_for(
    const std::deque<NativePlayerNotification>& items, int width, int height,
    const TextMeasurer& measure = {}, float scroll = 0.f,
    const stellar::engine::LocalizationTable* locale = nullptr);

enum class NotificationViewCommandKind {
  None,
  Close,
  OpenDiplomaticContact,
  OpenChronicle,
  OpenSystem
};
struct NotificationViewCommand {
  NotificationViewCommandKind kind{NotificationViewCommandKind::None};
  bool captured{};
  int civilization_id{-1};
  int system_id{-1};
};

class NativeNotificationView final {
 public:
  void set_text_measurer(TextMeasurer measure) { measure_ = std::move(measure); }
  void set_localization(
      const stellar::engine::LocalizationTable* table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  // Every retained event is reachable through the panel's bounded scroll, so
  // opening deliberately acknowledges the full retained feed.
  void open(std::int64_t latest_sequence) noexcept;
  void close() noexcept;
  void toggle(std::int64_t latest_sequence) noexcept;
  [[nodiscard]] std::int64_t last_read() const noexcept { return last_read_; }
  [[nodiscard]] float scroll_offset() const noexcept {
    return scroll_.scroll_offset;
  }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Client-local severity filter: when set, the feed only presents Caution
  // and Alert items. The authoritative deque is never modified.
  [[nodiscard]] bool important_only() const noexcept { return important_only_; }
  // Client-local topic filter: empty shows every category, otherwise the
  // feed only presents cards whose category matches. Cycling follows a
  // canonical order intersected with the categories actually published.
  [[nodiscard]] const std::string& category_filter() const noexcept {
    return category_filter_;
  }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(
      const std::deque<NativePlayerNotification>& items, int width,
      int height) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<native_map::UiRect> focused_bounds(
      const std::deque<NativePlayerNotification>& items, int width,
      int height) const;

  [[nodiscard]] NotificationViewCommand handle(
      const native_map::InputEvent& event,
      const std::deque<NativePlayerNotification>& items, int width, int height);
  void render(native_map::DrawList& out,
              const std::deque<NativePlayerNotification>& items, int width,
              int height) const;

 private:
  enum class PressTarget { None, Close, Contact, Chronicle, System, Filter };
  void cancel_press() noexcept;
  // Filtered view of the feed (identity when `important_only_` is off).
  // `filtered_` is a render cache only — callers keep owning the feed.
  const std::deque<NativePlayerNotification>& visible_items(
      const std::deque<NativePlayerNotification>& items) const;

  bool visible_{};
  bool important_only_{};
  std::string category_filter_{};
  mutable std::deque<NativePlayerNotification> filtered_{};
  std::int64_t last_read_{};
  stellar::engine::ScrollView scroll_{};
  int focus_{-1};
  native_map::Point pointer_{};
  native_map::Point press_origin_{};
  bool pointer_captured_{};
  PressTarget press_target_{PressTarget::None};
  std::optional<int> pressed_contact_id_, pressed_system_id_;
  std::optional<native_map::UiRect> pressed_bounds_;
  TextMeasurer measure_;
  const stellar::engine::LocalizationTable* locale_{};
};

} // namespace stellar::native_notifications
