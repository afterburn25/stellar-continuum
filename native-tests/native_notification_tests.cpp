#include "native_notifications.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace stellar::native_notifications;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value) throw std::runtime_error(std::string(message));
}

InputEvent release(Point at) {
  InputEvent event{};
  event.type = InputEventType::LeftReleased;
  event.position = at;
  return event;
}

void feed_semantics() {
  NativeNotificationFeed feed;
  require(feed.items().empty() && feed.latest_sequence() == 0,
          "feed must start empty");
  require(feed.unread_count(0) == 0, "empty feed unread count wrong");

  feed.publish("Research", "2050-01-02", "Project completed.");
  feed.publish("Combat", "2050-01-03", "Engagement started.");
  require(feed.items().size() == 2 && feed.latest_sequence() == 2,
          "publish must append ordered items");
  require(feed.items()[0].category == "Research" &&
              feed.items()[0].sequence == 1,
          "first item payload wrong");
  require(feed.unread_count(0) == 2 && feed.unread_count(1) == 1 &&
              feed.unread_count(2) == 0,
          "unread counting wrong");

  // Empty fields are rejected (reference throws; the feed drops silently).
  const auto size = feed.items().size();
  feed.publish("", "2050-01-03", "x");
  feed.publish("Combat", "", "x");
  feed.publish("Combat", "2050-01-03", "");
  require(feed.items().size() == size, "empty fields must not publish");

  // Bounded to 32 items; sequences keep increasing past the bound.
  for (int i = 0; i < 40; ++i)
    feed.publish("Colony", "2050-01-04", "event");
  require(feed.items().size() == NativeNotificationFeed::maximum_items,
          "feed must stay bounded");
  require(feed.items().front().sequence == 11,
          "oldest items must evict in order");

  // Clearing keeps the sequence so read markers cannot alias.
  const auto before = feed.latest_sequence();
  feed.clear();
  require(feed.items().empty(), "clear must drop items");
  feed.publish("Economy", "2050-01-05", "Funding restored.");
  require(feed.items().front().sequence == before + 1,
          "sequence must continue across clear");
}

void view_semantics() {
  constexpr int width = 1280, height = 720;
  NativeNotificationFeed feed;
  NativeNotificationView view;
  require(!view.visible(), "view must start hidden");

  feed.publish("Research", "2050-01-02", "Project completed.");
  feed.publish("Diplomacy", "2050-01-03", "Channel opened.", 7);
  feed.publish("Combat", "2050-01-04", "Engagement started.");

  view.open(feed.latest_sequence());
  require(view.visible() && view.last_read() == 3,
          "open must mark the feed read");
  require(feed.unread_count(view.last_read()) == 0, "unread after open");

  // Renders the panel + three cards into the draw list.
  DrawList out;
  view.render(out, feed.items(), width, height);
  require(!out.overlay.empty(), "visible view must draw");
  const auto baseline = out.overlay.size();

  // A release inside the panel without hitting controls is captured.
  // {1000,500} sits below the last card (which ends at y=418) but inside the
  // panel (bottom edge 548).
  auto command = view.handle(release({1000.f, 500.f}), feed.items(), width,
                             height);
  require(command.captured && command.kind == NotificationViewCommandKind::None,
          "in-panel release must be captured without a command");
  require(view.visible(), "in-panel release must not close the view");

  // The diplomacy card's OPEN RELATIONS button dispatches the contact id and
  // closes the panel. Deterministic layout at 1280x720: scale=1, panel
  // {830,78,430,470}, pad 12, header 30, list starts at y=160; combat card
  // (74px, no contact) then the diplomacy card at y=241 with its contact
  // button at {850,305,140,24}.
  command = view.handle(release({920.f, 317.f}), feed.items(), width, height);
  require(command.kind == NotificationViewCommandKind::OpenDiplomaticContact &&
              command.civilization_id == 7,
          "contact shortcut must carry the civilization id");
  require(!view.visible(), "contact shortcut must close the panel");

  // Outside input falls through to the UI underneath (reference
  // ContainPointerInput) without closing the panel.
  view.open(feed.latest_sequence());
  command = view.handle(release({100.f, 600.f}), feed.items(), width, height);
  require(!command.captured && command.kind == NotificationViewCommandKind::None,
          "outside release must fall through uncaptured");
  require(view.visible(), "outside release must leave the view open");

  // The close button dismisses.
  command = view.handle(release({1240.f, 103.f}), feed.items(), width, height);
  require(command.kind == NotificationViewCommandKind::Close &&
              !view.visible(),
          "close button must dismiss the view");

  // Escape closes too.
  view.open(feed.latest_sequence());
  InputEvent escape{};
  escape.type = InputEventType::EscapePressed;
  command = view.handle(escape, feed.items(), width, height);
  require(command.kind == NotificationViewCommandKind::Close &&
              !view.visible(),
          "escape must close the view");

  // Toggle round-trips and re-marks read.
  feed.publish("Colony", "2050-01-06", "Colony founded.");
  require(feed.unread_count(view.last_read()) == 1,
          "new items must count unread after close");
  view.toggle(feed.latest_sequence());
  require(view.visible() && view.last_read() == 4, "toggle must reopen+read");
  view.toggle(feed.latest_sequence());
  require(!view.visible(), "toggle must close");

  // Hidden view draws nothing.
  DrawList empty;
  view.render(empty, feed.items(), width, height);
  require(empty.overlay.empty(), "hidden view must draw nothing");
  (void)baseline;
}
}  // namespace

int main() {
  try {
    feed_semantics();
    view_semantics();
  } catch (const std::exception &error) {
    std::cerr << "native notification tests failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "native notification tests passed\n";
  return 0;
}
