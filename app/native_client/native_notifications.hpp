#pragma once

// Native port of the reference PlayerNotificationFeed + NotificationCenter:
// a bounded player-visible session history with a toggleable recent-events
// panel. Publishers must filter observer-sensitive events before calling
// publish(); the feed itself never inspects simulation state.

#include <stellar/engine/native_map_platform.hpp>

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace stellar::native_notifications {

struct NativePlayerNotification {
  std::int64_t sequence{};
  std::string category, date, message;
  std::optional<int> diplomatic_contact_id;
};

class NativeNotificationFeed final {
 public:
  static constexpr std::size_t maximum_items = 32;

  void publish(std::string category, std::string date, std::string message,
               std::optional<int> diplomatic_contact_id = std::nullopt);
  [[nodiscard]] const std::deque<NativePlayerNotification> &items()
      const noexcept {
    return items_;
  }
  // Sequences continue across clear() so a refilled feed cannot alias read
  // markers from a previous campaign.
  [[nodiscard]] std::int64_t latest_sequence() const noexcept {
    return next_sequence_ - 1;
  }
  [[nodiscard]] int unread_count(std::int64_t last_read) const noexcept;
  void clear() noexcept { items_.clear(); }

 private:
  std::deque<NativePlayerNotification> items_;
  std::int64_t next_sequence_{1};
};

// Panel geometry, exposed for tests and graphical smoke drivers. Mirrors the
// reference NotificationCenter.UpdateBounds placement.
struct NotificationLayout {
  native_map::UiRect panel, header, close_button, empty_hint;
  std::vector<native_map::UiRect> cards;
  std::vector<std::optional<native_map::UiRect>> contact_buttons;
  float scale{};
};

[[nodiscard]] NotificationLayout
notification_layout_for(const std::deque<NativePlayerNotification> &items,
                        int width, int height);

enum class NotificationViewCommandKind { None, Close, OpenDiplomaticContact };

struct NotificationViewCommand {
  NotificationViewCommandKind kind{NotificationViewCommandKind::None};
  bool captured{};
  int civilization_id{-1};
};

// Toggleable recent-events panel. Shows the newest 16 items in reverse
// chronological order with the reference category colors; items carrying a
// diplomatic contact expose an OPEN RELATIONS shortcut.
class NativeNotificationView final {
 public:
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  // Opening marks the feed read at its latest sequence.
  void open(std::int64_t latest_sequence) noexcept {
    visible_ = true;
    last_read_ = latest_sequence;
  }
  void close() noexcept { visible_ = false; }
  void toggle(std::int64_t latest_sequence) noexcept {
    if (visible_)
      close();
    else
      open(latest_sequence);
  }
  [[nodiscard]] std::int64_t last_read() const noexcept { return last_read_; }

  [[nodiscard]] NotificationViewCommand
  handle(const native_map::InputEvent &event,
         const std::deque<NativePlayerNotification> &items, int width,
         int height);
  void render(native_map::DrawList &out,
              const std::deque<NativePlayerNotification> &items, int width,
              int height) const;

 private:
  bool visible_{};
  std::int64_t last_read_{};
  native_map::Point pointer_{};
};

}  // namespace stellar::native_notifications
