#include "native_notifications.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stellar::native_notifications {
namespace {
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::UiRect;

constexpr Color title_color = native_ui::color::text_primary;
constexpr Color muted_color = native_ui::color::text_secondary;
constexpr Color message_color = native_ui::color::text_primary;

// Reference NotificationCenter.CategoryColor palette.
Color category_color(const std::string &category) {
  std::string lowered(category);
  for (auto &c : lowered)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lowered == "research") return native_ui::color::science;
  if (lowered == "industry" || lowered == "construction")
    return native_ui::color::construction;
  if (lowered == "economy") return native_ui::color::economy;
  if (lowered == "ships") return native_ui::color::selected;
  if (lowered == "exploration") return native_ui::color::focus;
  if (lowered == "colony") return native_ui::color::success;
  if (lowered == "combat") return native_ui::color::danger;
  if (lowered == "diplomacy") return native_ui::color::diplomacy;
  return native_ui::color::text_secondary;
}

std::string upper(std::string value) {
  for (auto &c : value)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, Point at, std::string value, Color color, int pixels,
          float wrap = 0.f, TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, std::nullopt, align});
}
void clipped_text(DrawList &out, Point at, std::string value, Color color,
                  int pixels, float wrap, const UiRect &clip,
                  TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(
      Text{at, std::move(value), color, pixels, wrap, clip, align});
}

}  // namespace

// Reference geometry: Position {max(112, w-450), 78}, Size {min(430, w-128),
// min(470, h-210)} scaled by the shared UI scale.
NotificationLayout
notification_layout_for(const std::deque<NativePlayerNotification> &items,
                        int width, int height) {
  const auto sw = static_cast<float>(width), sh = static_cast<float>(height);
  const auto scale = std::max(1.f, sh / 900.f);
  NotificationLayout layout;
  layout.scale = scale;
  layout.panel = {std::max(112.f * scale, sw - 450.f * scale), 78.f * scale,
                  std::min(430.f * scale, sw - 128.f * scale),
                  std::min(470.f * scale, sh - 210.f * scale)};
  const auto pad = 12.f * scale;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - pad * 2.f, 30.f * scale};
  layout.close_button = {layout.panel.x + layout.panel.width - pad -
                             26.f * scale,
                         layout.header.y, 26.f * scale, 26.f * scale};
  const auto list_y = layout.header.y + layout.header.height + 40.f * scale;
  const auto list_bottom = layout.panel.y + layout.panel.height - pad;
  layout.empty_hint = {layout.panel.x + pad, list_y,
                       layout.panel.width - pad * 2.f, 60.f * scale};
  const auto card_width = layout.panel.width - pad * 2.f;
  float cursor = list_y;
  // Newest first, up to 16 cards — each 3 lines plus a contact shortcut row.
  const auto count = std::min<std::size_t>(16, items.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto &item = items[items.size() - 1 - i];
    const auto card_height =
        (item.diplomatic_contact_id ? 96.f : 74.f) * scale;
    native_map::UiRect card{layout.panel.x + pad, cursor, card_width,
                            card_height};
    if (card.y + card.height > list_bottom) break;
    layout.cards.push_back(card);
    layout.contact_buttons.push_back(
        item.diplomatic_contact_id
            ? std::optional<native_map::UiRect>(
                  native_map::UiRect{card.x + 8.f * scale,
                                           card.y + 64.f * scale,
                                           140.f * scale, 24.f * scale})
            : std::nullopt);
    cursor += card_height + 7.f * scale;
  }
  return layout;
}

void NativeNotificationFeed::publish(std::string category, std::string date,
                                     std::string message,
                                     std::optional<int> contact) {
  if (category.empty() || date.empty() || message.empty()) return;
  items_.push_back({next_sequence_++, std::move(category), std::move(date),
                    std::move(message), contact});
  while (items_.size() > maximum_items) items_.pop_front();
}

int NativeNotificationFeed::unread_count(std::int64_t last_read) const
    noexcept {
  int count = 0;
  for (const auto &item : items_)
    if (item.sequence > last_read) ++count;
  return count;
}

NotificationViewCommand
NativeNotificationView::handle(const native_map::InputEvent &event,
                               const std::deque<NativePlayerNotification> &items,
                               int width, int height) {
  pointer_ = event.position;
  NotificationViewCommand command{NotificationViewCommandKind::None, true, -1};
  const auto layout = notification_layout_for(items, width, height);
  if (event.type == native_map::InputEventType::EscapePressed) {
    close();
    command.kind = NotificationViewCommandKind::Close;
    return command;
  }
  // Reference ContainPointerInput: only input landing on the panel is
  // captured. Everything else falls through to the workspaces and top bar
  // underneath, so the bell toggle and other controls stay live while the
  // panel is open. The panel itself dismisses via its close button or Escape.
  if (!layout.panel.contains(event.position)) {
    command.captured = false;
    return command;
  }
  if (event.type != native_map::InputEventType::LeftReleased) return command;
  if (layout.close_button.contains(event.position)) {
    close();
    command.kind = NotificationViewCommandKind::Close;
    return command;
  }
  const auto count = std::min<std::size_t>(16, items.size());
  for (std::size_t i = 0; i < layout.contact_buttons.size() && i < count; ++i) {
    if (!layout.contact_buttons[i] ||
        !layout.contact_buttons[i]->contains(event.position))
      continue;
    const auto &item = items[items.size() - 1 - i];
    if (!item.diplomatic_contact_id) continue;
    close();
    command.kind = NotificationViewCommandKind::OpenDiplomaticContact;
    command.civilization_id = *item.diplomatic_contact_id;
    return command;
  }
  return command;
}

void NativeNotificationView::render(
    DrawList &out, const std::deque<NativePlayerNotification> &items,
    int width, int height) const {
  if (!visible_) return;
  const auto layout = notification_layout_for(items, width, height);
  const auto scale = layout.scale;
  const auto pad = 12.f * scale;
  native_ui::panel(out, layout.panel, native_ui::Tone::Selected);
  text(out, {layout.header.x, layout.header.y + 20.f * scale},
       "RECENT EVENTS", title_color, static_cast<int>(18.f * scale));
  text(out, {layout.close_button.x - 10.f * scale,
             layout.header.y + 20.f * scale},
       std::to_string(items.size()) + " RETAINED", muted_color,
       static_cast<int>(10.f * scale), 0.f, TextAlign::Right);
  native_ui::button(out, layout.close_button, "X", pointer_,
                    static_cast<int>(12.f * scale));
  text(out,
       {layout.panel.x + pad,
        layout.header.y + layout.header.height + 14.f * scale},
       "Important outcomes remain here until the campaign or mode changes.",
       muted_color, static_cast<int>(11.f * scale),
       layout.panel.width - pad * 2.f);
  if (items.empty()) {
    text(out, {layout.empty_hint.x, layout.empty_hint.y},
         "No major events yet. Research, construction, missions, colonies and "
         "combat will appear here.",
         muted_color, static_cast<int>(13.f * scale), layout.empty_hint.width);
    return;
  }
  const auto count = std::min<std::size_t>(16, items.size());
  const auto list_bottom = layout.panel.y + layout.panel.height - pad;
  const UiRect list_clip{layout.panel.x, layout.header.y + 30.f * scale,
                         layout.panel.width, layout.panel.height - 42.f * scale};
  for (std::size_t i = 0; i < layout.cards.size() && i < count; ++i) {
    const auto &card = layout.cards[i];
    const auto &item = items[items.size() - 1 - i];
    const auto tone = category_color(item.category);
    fill(out, card, native_ui::color::surface_secondary);
    stroke(out, card, native_ui::color::keyline);
    fill(out, {card.x, card.y, 3.f, card.height}, tone);
    clipped_text(out, {card.x + 10.f * scale, card.y + 14.f * scale},
                 upper(item.category), tone, static_cast<int>(10.f * scale),
                 card.width - 90.f * scale, card);
    clipped_text(out,
                 {card.x + card.width - 8.f * scale, card.y + 14.f * scale},
                 item.date, muted_color, static_cast<int>(10.f * scale), 0.f,
                 card, TextAlign::Right);
    clipped_text(out, {card.x + 8.f * scale, card.y + 30.f * scale},
                 item.message, message_color, static_cast<int>(12.f * scale),
                 card.width - 16.f * scale, card);
    if (item.diplomatic_contact_id && i < layout.contact_buttons.size() &&
        layout.contact_buttons[i]) {
      const auto &button = *layout.contact_buttons[i];
      native_ui::button(out, button, "OPEN RELATIONS", pointer_,
                        static_cast<int>(11.f * scale),
                        native_ui::Tone::Diplomacy);
    }
  }
  (void)list_bottom;
}

}  // namespace stellar::native_notifications
