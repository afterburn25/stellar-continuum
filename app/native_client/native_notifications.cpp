#include <stellar/engine/native_ui_skin.hpp>
#include "native_notifications.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace stellar::native_notifications {
namespace {
using native_map::Color;
using native_map::DrawList;
using native_map::FilledRectangle;
using native_map::FontFace;
using native_map::Point;
using native_map::StrokedRectangle;
using native_map::Text;
using native_map::TextAlign;
using native_map::TextExtent;
using native_map::UiRect;

constexpr Color panel_color{10, 22, 36, 255};
constexpr Color border_color{116, 174, 225, 255};
constexpr Color title_color{154, 225, 255, 255};
constexpr Color muted_color{154, 181, 211, 235};
constexpr Color message_color{238, 244, 255, 255};
constexpr Color button_color{14, 30, 48, 255};
constexpr Color button_hover{24, 46, 70, 255};

Color category_color(const std::string& category) {
  std::string lowered(category);
  for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lowered == "research") return {180, 160, 228, 255};
  if (lowered == "industry" || lowered == "construction" || lowered == "economy") return {240, 197, 106, 255};
  if (lowered == "ships") return {154, 225, 255, 255};
  if (lowered == "exploration") return {143, 215, 176, 255};
  if (lowered == "colony") return {143, 229, 177, 255};
  if (lowered == "combat") return {238, 154, 145, 255};
  return title_color;
}

std::string upper(std::string value) {
  for (auto& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

std::string resolve(const stellar::engine::LocalizationTable* locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

// Categories are stable publisher IDs (they also drive category_color), so the
// catalog key derives from the ID and only the displayed label is translated.
std::string category_label(const std::string& category,
                           const stellar::engine::LocalizationTable* locale) {
  return upper(resolve(locale, "NOTIFY_CATEGORY_" + upper(category), category));
}

bool intersects(UiRect a, UiRect b) noexcept {
  return a.x < b.x + b.width && a.x + a.width > b.x &&
         a.y < b.y + b.height && a.y + a.height > b.y;
}

UiRect intersection(UiRect a, UiRect b) noexcept {
  const float left = std::max(a.x, b.x), top = std::max(a.y, b.y);
  const float right = std::min(a.x + a.width, b.x + b.width);
  const float bottom = std::min(a.y + a.height, b.y + b.height);
  return {left, top, std::max(0.f, right - left), std::max(0.f, bottom - top)};
}

bool contains_rect(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

bool same_rect(UiRect left, UiRect right) noexcept {
  return std::abs(left.x - right.x) < .01f &&
         std::abs(left.y - right.y) < .01f &&
         std::abs(left.width - right.width) < .01f &&
         std::abs(left.height - right.height) < .01f;
}

TextExtent fallback_measure(const Text& text) {
  const auto chars = static_cast<int>(text.value.size());
  const auto line_width = text.wrap_width > 0.f
      ? std::max(1, static_cast<int>(std::floor(text.wrap_width / std::max(1, text.font_pixel_size) * 1.75f)))
      : std::max(1, chars);
  const auto lines = std::max(1, (chars + line_width - 1) / line_width);
  return {std::min(chars, line_width) * std::max(1, text.font_pixel_size / 2),
          lines * (text.font_pixel_size + 3)};
}

TextExtent measure(const TextMeasurer& callback, const Text& text) {
  const auto result = callback ? callback(text) : fallback_measure(text);
  return {std::max(0, result.width), std::max(text.font_pixel_size, result.height)};
}

void fill(DrawList& out, UiRect bounds, Color color) { out.overlay.emplace_back(FilledRectangle{bounds, color}); }
void stroke(DrawList& out, UiRect bounds, Color color) { out.overlay.emplace_back(StrokedRectangle{bounds, color}); }
void clipped_text(DrawList& out, Point at, std::string value, Color color, int pixels,
                  float wrap, const UiRect& clip, TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(Text{at, std::move(value), color, pixels, wrap, clip, align, FontFace::Interface});
}

} // namespace

NotificationLayout notification_layout_for(const std::deque<NativePlayerNotification>& items,
                                           int width, int height,
                                           const TextMeasurer& text_measurer,
                                           float requested_scroll,
                                           const stellar::engine::LocalizationTable* locale) {
  NotificationLayout layout;
  if (width <= 0 || height <= 0) return layout;
  const float sw = static_cast<float>(width), sh = static_cast<float>(height);
  layout.scale = std::clamp(std::min(sw / 1280.f, sh / 900.f), .72f, 1.65f);
  const float s = layout.scale;
  const float inset = 12.f * s;
  const float panel_width = std::clamp(sw - inset * 2.f, 270.f * s, 430.f * s);
  const float panel_height = std::clamp(sh - 116.f * s, 300.f * s, 560.f * s);
  layout.panel = {std::max(inset, sw - panel_width - 20.f * s), 68.f * s,
                  panel_width, std::min(panel_height, sh - 2.f * inset)};
  const float pad = 12.f * s;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad, layout.panel.width - 2.f * pad, 28.f * s};
  layout.close_button = {layout.header.x + layout.header.width - 26.f * s, layout.header.y, 26.f * s, 26.f * s};
  layout.chronicle_button = {layout.close_button.x - 88.f * s, layout.header.y,
                             82.f * s, 26.f * s};
  const float intro_height = 32.f * s;
  layout.list_viewport = {layout.panel.x + pad, layout.header.y + layout.header.height + intro_height,
                          layout.panel.width - 2.f * pad,
                          std::max(1.f, layout.panel.y + layout.panel.height - pad -
                                            (layout.header.y + layout.header.height + intro_height))};
  layout.empty_hint = layout.list_viewport;
  const int body_pixels = std::max(11, static_cast<int>(std::lround(13.f * s)));
  const int small_pixels = std::max(10, static_cast<int>(std::lround(11.f * s)));
  const float card_pad = 8.f * s;
  float cursor = 0.f;
  for (std::size_t reverse = 0; reverse < items.size(); ++reverse) {
    const std::size_t index = items.size() - 1 - reverse;
    const auto& item = items[index];
    const UiRect measure_clip{0, 0, std::max(1.f, layout.list_viewport.width - card_pad * 2.f), 1.f};
    const Text metadata{{}, category_label(item.category, locale) + "  " + item.date,
                        category_color(item.category), small_pixels,
                        measure_clip.width, std::nullopt, TextAlign::Left,
                        FontFace::Interface};
    const auto metadata_height = static_cast<float>(measure(text_measurer, metadata).height);
    const Text message{{}, item.message, message_color, body_pixels, measure_clip.width, std::nullopt, TextAlign::Left, FontFace::Interface};
    const float message_height = static_cast<float>(measure(text_measurer, message).height);
    const float action_height = item.diplomatic_contact_id ? 27.f * s : 0.f;
    const float card_height = card_pad + metadata_height + 3.f * s + message_height +
                              (action_height ? 6.f * s + action_height : 0.f) + card_pad;
    NotificationCardLayout entry;
    entry.item_index = index;
    entry.bounds = {layout.list_viewport.x, layout.list_viewport.y + cursor,
                    layout.list_viewport.width, card_height};
    entry.metadata_bounds = {entry.bounds.x + card_pad, entry.bounds.y + card_pad,
                             entry.bounds.width - card_pad * 2.f, metadata_height};
    entry.message_bounds = {entry.bounds.x + card_pad,
                            entry.metadata_bounds.y + metadata_height + 3.f * s,
                            entry.bounds.width - card_pad * 2.f, message_height};
    if (item.diplomatic_contact_id) {
      entry.contact_button = UiRect{entry.bounds.x + card_pad,
                                    entry.message_bounds.y + message_height + 6.f * s,
                                    std::min(150.f * s, entry.bounds.width - card_pad * 2.f), action_height};
    }
    layout.entries.push_back(entry);
    cursor += card_height + 7.f * s;
  }
  layout.content_height = std::max(0.f, cursor - (items.empty() ? 0.f : 7.f * s));
  layout.max_scroll = std::max(0.f, layout.content_height - layout.list_viewport.height);
  layout.scroll = std::clamp(std::isfinite(requested_scroll) ? requested_scroll : 0.f, 0.f, layout.max_scroll);
  for (auto& entry : layout.entries) {
    entry.bounds.y -= layout.scroll;
    entry.metadata_bounds.y -= layout.scroll;
    entry.message_bounds.y -= layout.scroll;
    if (entry.contact_button) entry.contact_button->y -= layout.scroll;
    layout.cards.push_back(entry.bounds);
    layout.contact_buttons.push_back(entry.contact_button);
  }
  return layout;
}

void NativeNotificationFeed::publish(std::string category, std::string date, std::string message,
                                     std::optional<int> contact) {
  if (category.empty() || date.empty() || message.empty()) return;
  items_.push_back({next_sequence_++, std::move(category), std::move(date), std::move(message), contact});
  while (items_.size() > maximum_items) items_.pop_front();
}

int NativeNotificationFeed::unread_count(std::int64_t last_read) const noexcept {
  return static_cast<int>(std::count_if(items_.begin(), items_.end(),
      [last_read](const auto& item) { return item.sequence > last_read; }));
}

void NativeNotificationView::cancel_press() noexcept {
  pointer_captured_ = false;
  press_target_ = PressTarget::None;
  pressed_contact_id_.reset();
  pressed_bounds_.reset();
}
void NativeNotificationView::open(std::int64_t latest_sequence) noexcept {
  visible_ = true; scroll_ = 0.f; last_read_ = latest_sequence; cancel_press();
}
void NativeNotificationView::close() noexcept { visible_ = false; scroll_ = 0.f; cancel_press(); }
void NativeNotificationView::toggle(std::int64_t latest_sequence) noexcept { if (visible_) close(); else open(latest_sequence); }

NotificationViewCommand NativeNotificationView::handle(const native_map::InputEvent& event,
    const std::deque<NativePlayerNotification>& items, int width, int height) {
  NotificationViewCommand command{};
  if (!visible_) return command;
  pointer_ = event.position;
  auto layout = notification_layout_for(items, width, height, measure_, scroll_, locale_);
  scroll_ = layout.scroll;
  if (event.type == native_map::InputEventType::EscapePressed) { close(); command.kind = NotificationViewCommandKind::Close; command.captured = true; return command; }
  if (event.type == native_map::InputEventType::PointerCancelled) { const bool captured = pointer_captured_; cancel_press(); command.captured = captured; return command; }
  if (event.type == native_map::InputEventType::Wheel) {
    if (!layout.panel.contains(event.position)) return command;
    // Scrolling cancels an activation but a preceding panel press remains
    // owned until its matching release or focus cancellation.
    press_target_ = PressTarget::None;
    pressed_contact_id_.reset();
    pressed_bounds_.reset();
    scroll_ = std::clamp(scroll_ - event.wheel_y * 42.f * layout.scale, 0.f, layout.max_scroll);
    command.captured = true; return command;
  }
  if (event.type == native_map::InputEventType::LeftPressed) {
    if (!layout.panel.contains(event.position)) return command;
    pointer_captured_ = true; press_origin_ = event.position; press_target_ = PressTarget::None; pressed_contact_id_.reset(); pressed_bounds_.reset();
    if (layout.close_button.contains(event.position)) press_target_ = PressTarget::Close;
    else if (layout.chronicle_button.contains(event.position)) press_target_ = PressTarget::Chronicle;
    else for (std::size_t i = 0; i < layout.entries.size(); ++i) {
      const auto& entry = layout.entries[i];
      if (!entry.contact_button || !entry.contact_button->contains(event.position) ||
          !contains_rect(layout.list_viewport, *entry.contact_button)) continue;
      const auto& item = items[entry.item_index];
      if (item.diplomatic_contact_id) { press_target_ = PressTarget::Contact; pressed_contact_id_ = item.diplomatic_contact_id; pressed_bounds_ = entry.contact_button; }
      break;
    }
    command.captured = true; return command;
  }
  if (event.type == native_map::InputEventType::PointerMove && pointer_captured_) {
    const float dx = event.position.x - press_origin_.x, dy = event.position.y - press_origin_.y;
    if (dx * dx + dy * dy > 25.f) { press_target_ = PressTarget::None; pressed_contact_id_.reset(); pressed_bounds_.reset(); }
    command.captured = true; return command;
  }
  if (event.type != native_map::InputEventType::LeftReleased || !pointer_captured_) {
    // A press that began outside the panel stays owned by its original
    // destination, even if its drag crosses this panel. Standalone releases
    // and right clicks inside the panel must not reach the world underneath.
    if (layout.panel.contains(event.position) &&
        (event.type == native_map::InputEventType::LeftReleased ||
         event.type == native_map::InputEventType::RightPressed ||
         event.type == native_map::InputEventType::RightReleased))
      command.captured = true;
    return command;
  }
  command.captured = true;
  const auto target = press_target_;
  const auto contact = pressed_contact_id_;
  const auto pressed_bounds = pressed_bounds_;
  cancel_press();
  if (target == PressTarget::Close && layout.close_button.contains(event.position)) { close(); command.kind = NotificationViewCommandKind::Close; }
  else if (target == PressTarget::Chronicle && layout.chronicle_button.contains(event.position)) command.kind = NotificationViewCommandKind::OpenChronicle;
  else if (target == PressTarget::Contact && contact && pressed_bounds &&
           pressed_bounds->contains(event.position)) {
    for (const auto& entry : layout.entries) {
      if (!entry.contact_button || !same_rect(*entry.contact_button, *pressed_bounds) ||
          !contains_rect(layout.list_viewport, *entry.contact_button))
        continue;
      const auto& current = items[entry.item_index];
      if (!current.diplomatic_contact_id || *current.diplomatic_contact_id != *contact)
        continue;
      close(); command.kind = NotificationViewCommandKind::OpenDiplomaticContact;
      command.civilization_id = *contact;
      break;
    }
  }
  return command;
}

void NativeNotificationView::render(DrawList& out, const std::deque<NativePlayerNotification>& items,
                                    int width, int height) const {
  if (!visible_) return;
  const auto layout = notification_layout_for(items, width, height, measure_, scroll_, locale_);
  const float s = layout.scale;
  stellar::engine::ui_skin::surface(out,layout.panel,s);
  const int title_pixels = std::max(13, static_cast<int>(std::lround(18.f * s)));
  const auto title_text = resolve(locale_, "NOTIFY_TITLE", "RECENT EVENTS");
  const Text title_probe{{}, title_text, title_color, title_pixels, 0.f,
                         std::nullopt, TextAlign::Left, FontFace::Interface};
  const auto title_extent = measure(measure_, title_probe);
  clipped_text(out, {layout.header.x,
                     layout.header.y + (layout.header.height - title_extent.height) * .5f},
               title_text, title_color, title_pixels, 0.f, layout.header);
  stellar::engine::ui_skin::control(out,layout.chronicle_button,layout.chronicle_button.contains(pointer_),false,true,s);
  const auto chronicle_text=resolve(locale_,"NOTIFY_OPEN_CHRONICLE","CHRONICLE");
  const int chronicle_pixels=std::max(9,static_cast<int>(std::lround(10.f*s)));
  const Text chronicle_probe{{},chronicle_text,muted_color,chronicle_pixels,
      layout.chronicle_button.width-4.f*s,std::nullopt,TextAlign::Center,FontFace::Interface};
  const auto chronicle_extent=measure(measure_,chronicle_probe);
  clipped_text(out,{layout.chronicle_button.x+layout.chronicle_button.width*.5f,
                    layout.chronicle_button.y+(layout.chronicle_button.height-chronicle_extent.height)*.5f},
      chronicle_text,muted_color,chronicle_pixels,layout.chronicle_button.width-4.f*s,
      layout.chronicle_button,TextAlign::Center);
  stellar::engine::ui_skin::control(out,layout.close_button,layout.close_button.contains(pointer_),false,true,s);
  const int close_pixels = std::max(10, static_cast<int>(std::lround(12.f * s)));
  const Text close_probe{{}, "X", muted_color, close_pixels, 0.f,
                         std::nullopt, TextAlign::Center, FontFace::Interface};
  const auto close_extent = measure(measure_, close_probe);
  clipped_text(out, {layout.close_button.x + layout.close_button.width * .5f,
                     layout.close_button.y + (layout.close_button.height - close_extent.height) * .5f},
               "X", muted_color, close_pixels, 0.f, layout.close_button, TextAlign::Center);
  clipped_text(out, {layout.list_viewport.x, layout.header.y + layout.header.height + 13.f * s},
               resolve(locale_, "NOTIFY_SUBTITLE", "Recent reports from your empire."), muted_color,
               std::max(9, static_cast<int>(std::lround(11.f * s))), layout.list_viewport.width, layout.panel);
  if (items.empty()) { clipped_text(out, {layout.empty_hint.x, layout.empty_hint.y}, resolve(locale_, "NOTIFY_EMPTY", "No major events yet."), muted_color,
      std::max(11, static_cast<int>(std::lround(13.f * s))), layout.empty_hint.width, layout.empty_hint); return; }
  for (std::size_t i = 0; i < layout.entries.size(); ++i) {
    const auto& entry = layout.entries[i]; if (!intersects(entry.bounds, layout.list_viewport)) continue;
    const auto& item = items[entry.item_index];
    stellar::engine::ui_skin::surface(out,entry.bounds,s,false,layout.list_viewport);
    clipped_text(out, {entry.metadata_bounds.x, entry.metadata_bounds.y}, category_label(item.category, locale_) + "  " + item.date,
                 category_color(item.category), std::max(9, static_cast<int>(std::lround(11.f * s))), entry.metadata_bounds.width, layout.list_viewport);
    clipped_text(out, {entry.message_bounds.x, entry.message_bounds.y}, item.message, message_color,
                 std::max(11, static_cast<int>(std::lround(13.f * s))), entry.message_bounds.width, layout.list_viewport);
    if (entry.contact_button && contains_rect(layout.list_viewport, *entry.contact_button)) { stellar::engine::ui_skin::control(out,*entry.contact_button,entry.contact_button->contains(pointer_),false,true,s);
      const int action_pixels = std::max(9, static_cast<int>(std::lround(10.f * s)));
      const auto action_text = resolve(locale_, "NOTIFY_OPEN_RELATIONS", "OPEN RELATIONS");
      const Text action_probe{{}, action_text, title_color, action_pixels,
                              entry.contact_button->width - 4.f * s,
                              std::nullopt, TextAlign::Center, FontFace::Interface};
      const auto action_extent = measure(measure_, action_probe);
      clipped_text(out, {entry.contact_button->x + entry.contact_button->width * .5f,
                         entry.contact_button->y + (entry.contact_button->height - action_extent.height) * .5f},
        action_text, title_color, action_pixels, entry.contact_button->width - 4.f * s,
        *entry.contact_button, TextAlign::Center); }
  }
  if (layout.max_scroll > 0.f) { const float thumb_h = std::max(16.f * s, layout.list_viewport.height * layout.list_viewport.height / layout.content_height);
    const float y = layout.list_viewport.y + (layout.list_viewport.height - thumb_h) * (layout.scroll / layout.max_scroll);
    fill(out, {layout.list_viewport.x + layout.list_viewport.width - 3.f * s, y, 2.f * s, thumb_h}, muted_color); }
}

} // namespace stellar::native_notifications
