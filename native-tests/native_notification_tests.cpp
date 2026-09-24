#include "native_notifications.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace stellar::native_map;
using namespace stellar::native_notifications;

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
Point center(UiRect bounds) { return {bounds.x + bounds.width * .5f, bounds.y + bounds.height * .5f}; }
bool intersects(UiRect a, UiRect b) {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}
bool overlaps(UiRect a, UiRect b) { return intersects(a, b); }
TextExtent measured(const Text& text) {
  const int columns = text.wrap_width > 0.f
      ? std::max(1, static_cast<int>(text.wrap_width / 7.f))
      : std::max(1, static_cast<int>(text.value.size()));
  return {std::min(columns, static_cast<int>(text.value.size())) * 7,
          std::max(text.font_pixel_size + 2, ((static_cast<int>(text.value.size()) + columns - 1) / columns) * (text.font_pixel_size + 3))};
}
NativeNotificationFeed feed(std::size_t count = 1, bool contacts = false) {
  NativeNotificationFeed result;
  for (std::size_t i = 0; i < count; ++i)
    result.publish("Research", "Day " + std::to_string(i), "Detailed report " + std::to_string(i) + " " + std::string(120, 'x'),
                   contacts ? std::optional<int>{static_cast<int>(100 + i)} : std::nullopt);
  return result;
}

void bounded_feed_and_reachable_scroll() {
  auto notifications = feed(40);
  require(notifications.items().size() == NativeNotificationFeed::maximum_items, "feed did not retain exactly 32 events");
  NativeNotificationView view;
  view.set_text_measurer(measured);
  view.open(notifications.latest_sequence());
  require(view.last_read() == notifications.latest_sequence(), "opening did not acknowledge browseable retained list");
  auto layout = notification_layout_for(notifications.items(), 720, 720, measured, 0.f);
  require(layout.max_scroll > 0.f && layout.entries.size() == 32, "all retained events were not represented in scroll layout");
  const auto last_before = layout.cards.back();
  require(!intersects(last_before, layout.list_viewport), "long list unexpectedly fits without scrolling");
  for (int i = 0; i < 200; ++i) (void)view.handle({InputEventType::Wheel, center(layout.panel), {}, -1.f}, notifications.items(), 720, 720);
  require(std::abs(view.scroll_offset() - layout.max_scroll) < .1f, "scroll did not clamp at lower bound");
  auto end = notification_layout_for(notifications.items(), 720, 720, measured, view.scroll_offset());
  require(intersects(end.cards.back(), end.list_viewport), "oldest retained event is inaccessible at end scroll");
  for (int i = 0; i < 200; ++i) (void)view.handle({InputEventType::Wheel, center(layout.panel), {}, 1.f}, notifications.items(), 720, 720);
  require(view.scroll_offset() == 0.f, "reverse overscroll did not clamp at zero");
}

void measured_wrapping_and_narrow_geometry() {
  auto notifications = feed();
  const auto layout = notification_layout_for(notifications.items(), 720, 720, measured);
  require(layout.panel.x >= 0 && layout.panel.y >= 0 && layout.panel.x + layout.panel.width <= 720 && layout.panel.y + layout.panel.height <= 720,
          "720p panel escaped viewport");
  require(layout.entries.front().message_bounds.height > 30.f, "actual text measurement did not determine dynamic message height");
  require(!overlaps(layout.entries.front().metadata_bounds, layout.entries.front().message_bounds),
          "720p metadata overlaps measured message bounds");
  NativeNotificationView view; view.set_text_measurer(measured); view.open(notifications.latest_sequence());
  DrawList draw; view.render(draw, notifications.items(), 720, 720);
  bool wrapped_message = false;
  for (const auto& command : draw.overlay) if (const auto* text = std::get_if<Text>(&command); text && text->value == notifications.items().back().message) {
    wrapped_message = text->wrap_width > 0 && text->clip && text->clip->x >= layout.list_viewport.x && text->clip->y >= layout.list_viewport.y;
  }
  require(wrapped_message, "long notification text was not clipped and wrapped in its card viewport");
  for (const auto& command : draw.overlay) if (const auto* text = std::get_if<Text>(&command); text &&
      (text->value == "RECENT EVENTS" || text->value == "X")) {
    require(text->clip && text->at.y >= text->clip->y &&
                text->at.y + measured(*text).height <= text->clip->y + text->clip->height,
            "header or close label was not vertically centered inside its clip");
  }
  const auto narrow = notification_layout_for(notifications.items(), 480, 720, measured);
  require(narrow.panel.width > 0 && narrow.panel.x >= 0 && narrow.panel.x + narrow.panel.width <= 480, "narrow panel geometry is invalid");
  const auto layout1080 = notification_layout_for(notifications.items(), 1920, 1080, measured);
  require(!overlaps(layout1080.entries.front().metadata_bounds, layout1080.entries.front().message_bounds),
          "1080p metadata overlaps measured message bounds");
}

void activation_owns_full_press_release_gesture() {
  auto notifications = feed(1, true);
  NativeNotificationView view; view.set_text_measurer(measured); view.open(notifications.latest_sequence());
  auto layout = notification_layout_for(notifications.items(), 1280, 720, measured);
  const Point contact = center(*layout.contact_buttons.front());
  auto command = view.handle({InputEventType::LeftReleased, contact}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::None && command.captured,
          "orphan release inside a contact leaked to underlying interaction");
  command = view.handle({InputEventType::LeftPressed, contact}, notifications.items(), 1280, 720);
  require(command.captured, "press inside contact was not captured");
  command = view.handle({InputEventType::PointerMove, {0, 0}}, notifications.items(), 1280, 720);
  require(command.captured, "captured press was lost after leaving panel");
  command = view.handle({InputEventType::LeftReleased, {0, 0}}, notifications.items(), 1280, 720);
  require(command.captured && command.kind == NotificationViewCommandKind::None, "dragged contact press activated on release");
  command = view.handle({InputEventType::LeftPressed, contact}, notifications.items(), 1280, 720);
  require(command.captured, "second contact press was not captured");
  command = view.handle({InputEventType::PointerCancelled, {}}, notifications.items(), 1280, 720);
  require(command.captured, "focus loss did not cancel an owned press");
  command = view.handle({InputEventType::LeftReleased, contact}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::None, "focus loss left contact activation armed");
  command = view.handle({InputEventType::LeftPressed, contact}, notifications.items(), 1280, 720);
  command = view.handle({InputEventType::LeftReleased, contact}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::OpenDiplomaticContact && command.civilization_id == 100,
          "matching press and release did not activate optional contact");
  view.open(notifications.latest_sequence());
  command = view.handle({InputEventType::LeftPressed, {10, 10}}, notifications.items(), 1280, 720);
  require(!command.captured, "outside press did not fall through");
  command = view.handle({InputEventType::LeftReleased, center(layout.panel)}, notifications.items(), 1280, 720);
  require(command.captured && command.kind == NotificationViewCommandKind::None,
          "orphan release inside panel leaked to underlying interaction");
  command = view.handle({InputEventType::RightPressed, center(layout.panel)}, notifications.items(), 1280, 720);
  require(command.captured, "right click inside panel leaked to underlying interaction");
  command = view.handle({InputEventType::LeftPressed, contact}, notifications.items(), 1280, 720);
  command = view.handle({InputEventType::Wheel, center(layout.panel), {}, -1.f}, notifications.items(), 1280, 720);
  require(command.captured, "wheel did not remain panel-owned after press");
  command = view.handle({InputEventType::LeftReleased, {0, 0}}, notifications.items(), 1280, 720);
  require(command.captured && command.kind == NotificationViewCommandKind::None,
          "wheel cancellation released ownership to the world");
  layout = notification_layout_for(notifications.items(), 1280, 720, measured);
  command = view.handle({InputEventType::LeftPressed, center(*layout.contact_buttons.front())}, notifications.items(), 1280, 720);
  notifications.publish("Research", "New day", "The feed changed while the pointer was held.", 321);
  command = view.handle({InputEventType::LeftReleased, center(*layout.contact_buttons.front())}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::None,
          "contact activation survived a changed feed/layout");
  view.close(); view.open(notifications.latest_sequence());
  layout = notification_layout_for(notifications.items(), 1280, 720, measured);
  command = view.handle({InputEventType::LeftReleased, center(layout.close_button)}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::None && view.visible(), "orphan close release dismissed panel");
  command = view.handle({InputEventType::LeftPressed, center(layout.close_button)}, notifications.items(), 1280, 720);
  require(command.captured, "close press was not captured");
  command = view.handle({InputEventType::LeftReleased, center(layout.close_button)}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::Close && !view.visible(), "matching close press and release did not dismiss panel");
}
void system_navigation_command() {
  NativeNotificationFeed notifications;
  notifications.publish("Combat", "Day 10", "Fleet engaged over Halcyon",
                        std::nullopt, 9);
  notifications.publish("Research", "Day 11", "Discovery completed");
  NativeNotificationView view;
  view.set_text_measurer(measured);
  view.open(notifications.latest_sequence());
  const auto layout = notification_layout_for(notifications.items(), 1280, 720, measured);
  // Newest first: entry 0 is the locationless report, entry 1 located.
  require(!layout.entries[0].system_button &&
              layout.entries[1].system_button &&
              !layout.entries[0].contact_button,
          "system action row missing or leaked onto locationless report");
  const Point at = center(*layout.entries[1].system_button);
  auto command = view.handle({InputEventType::LeftReleased, at}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::None && command.captured,
          "orphan release on system action leaked");
  command = view.handle({InputEventType::LeftPressed, at}, notifications.items(), 1280, 720);
  require(command.captured, "system action press not captured");
  command = view.handle({InputEventType::LeftReleased, at}, notifications.items(), 1280, 720);
  require(command.kind == NotificationViewCommandKind::OpenSystem &&
              command.system_id == 9 && !view.visible(),
          "system action did not emit OpenSystem");
}

void keyboard_focus() {
  // Keyboard-focus contract: Tab/arrows ring every actionable rect in
  // (y,x) order — header controls, then each card's action buttons —
  // Home/End jump to the ends, Return/Space replay the press/release
  // dispatch, and pointer presses reset the ring. Inert card bodies and
  // partially-clipped action buttons never gain focus.
  NativeNotificationFeed notifications;
  notifications.publish("Research", "Day 10", "Discovery completed", 42);
  notifications.publish("Combat", "Day 11", "Fleet engaged over Halcyon",
                        std::nullopt, 9);
  // Newest first: entry 0 is the located combat card, entry 1 the
  // contact card — focus order is chronicle, close, system, contact.
  NativeNotificationView view;
  view.set_text_measurer(measured);
  view.open(notifications.latest_sequence());
  const auto layout = notification_layout_for(notifications.items(), 1280, 720, measured);
  const auto key = [&](std::uint32_t code, bool shift = false) {
    InputEvent event{};
    event.type = InputEventType::KeyPressed;
    event.key = code;
    event.shift = shift;
    return view.handle(event, notifications.items(), 1280, 720);
  };
  constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
  constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                          kDown = 0x40000051u, kUp = 0x40000052u;
  constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
  constexpr std::uint32_t kF5 = 0x4000003fu;
  require(view.focus() < 0, "focus ring present before any key");
  require(key(kTab).captured && view.focus() == 0, "Tab did not focus the first control");
  require(key(kDown).captured && view.focus() == 1, "Down did not advance the ring");
  require(key(kLeft).captured && view.focus() == 0, "Left did not walk back");
  require(key(kEnd).captured && view.focus() == 3, "End did not land on the last action");
  require(key(kTab, true).captured && view.focus() == 2, "Shift+Tab did not step backwards");
  require(key(kRight).captured && view.focus() == 3, "Right did not walk forward");
  require(key(kHome).captured && view.focus() == 0, "Home did not return to the head");
  require(!key(kF5).captured, "unrelated key was captured");
  require(key(kTab).captured && view.focus() == 1, "Tab did not resume cycling after Home");
  // Activation replays the real press/release dispatch through handle().
  key(kEnd); // entry 1's contact button
  auto command = key(kReturn);
  require(command.kind == NotificationViewCommandKind::OpenDiplomaticContact &&
              command.civilization_id == 42 && command.captured && !view.visible(),
          "Return on a contact action did not dispatch OpenDiplomaticContact");
  view.open(notifications.latest_sequence());
  key(kEnd);
  command = key(kUp); // entry 0's system button
  require(command.captured && view.focus() == 2, "Up did not step back to the located card");
  command = key(kSpace);
  require(command.kind == NotificationViewCommandKind::OpenSystem &&
              command.system_id == 9 && !view.visible(),
          "Space on a located action did not dispatch OpenSystem");
  view.open(notifications.latest_sequence());
  // Chronicle activation keeps the panel open and preserves the ring.
  require(key(kTab).captured && view.focus() == 0, "reopen did not reset the ring");
  command = key(kReturn);
  require(command.kind == NotificationViewCommandKind::OpenChronicle &&
              view.visible() && view.focus() == 0,
          "chronicle activation closed the panel or lost the ring");
  // The ring renders as the last stroke over the focused control.
  DrawList draw;
  view.render(draw, notifications.items(), 1280, 720);
  const auto* ring = std::get_if<StrokedRectangle>(&draw.overlay.back());
  require(ring && std::abs(ring->bounds.x - layout.chronicle_button.x) < .01f &&
              std::abs(ring->bounds.y - layout.chronicle_button.y) < .01f,
          "focus ring was not drawn over the focused control");
  // A pointer press hands ownership back to the pointer.
  require(view.handle({InputEventType::LeftPressed, center(layout.panel)},
                      notifications.items(), 1280, 720).captured &&
              view.focus() < 0,
          "pointer press did not clear the ring");
  // The empty feed still rings its header controls.
  NativeNotificationFeed empty;
  view.open(empty.latest_sequence());
  require(key(kTab).captured && view.focus() == 0, "empty feed did not focus a header control");
  const auto empty_layout = notification_layout_for(empty.items(), 1280, 720, measured);
  DrawList empty_draw;
  view.render(empty_draw, empty.items(), 1280, 720);
  ring = std::get_if<StrokedRectangle>(&empty_draw.overlay.back());
  require(ring && std::abs(ring->bounds.x - empty_layout.chronicle_button.x) < .01f,
          "empty feed rendered no header focus ring");
}

} // namespace

int main() {
  try {
    bounded_feed_and_reachable_scroll();
    measured_wrapping_and_narrow_geometry();
    activation_owns_full_press_release_gesture();
    system_navigation_command();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
