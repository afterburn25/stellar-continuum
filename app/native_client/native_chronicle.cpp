#include "native_chronicle.hpp"
#include "native_campaign_calendar.hpp"

#include <stellar/engine/native_ui_skin.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace stellar::native_chronicle {
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
using native_notifications::TextMeasurer;

constexpr Color title_color{154, 225, 255, 255};
constexpr Color muted_color{154, 181, 211, 235};
constexpr Color message_color{238, 244, 255, 255};

Color label_color(const std::string &label) {
  std::string lowered(label);
  for (auto &c : lowered)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lowered == "research") return {180, 160, 228, 255};
  if (lowered == "construction" || lowered == "economy")
    return {240, 197, 106, 255};
  if (lowered == "ships") return {154, 225, 255, 255};
  if (lowered == "exploration") return {143, 215, 176, 255};
  if (lowered == "colony") return {143, 229, 177, 255};
  if (lowered == "combat") return {238, 154, 145, 255};
  return title_color;
}

std::string upper(std::string value) {
  for (auto &c : value)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

std::string resolve(const stellar::engine::LocalizationTable *locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string entry_label(const std::string &category,
                        const stellar::engine::LocalizationTable *locale) {
  const char *mapped = category_label(category);
  const std::string id = mapped ? std::string(mapped) : category;
  return upper(resolve(locale, "NOTIFY_CATEGORY_" + upper(id), id));
}

bool intersects(UiRect a, UiRect b) noexcept {
  return a.x < b.x + b.width && a.x + a.width > b.x &&
         a.y < b.y + b.height && a.y + a.height > b.y;
}

TextExtent fallback_measure(const Text &text) {
  const auto chars = static_cast<int>(text.value.size());
  const auto line_width =
      text.wrap_width > 0.f
          ? std::max(1, static_cast<int>(std::floor(
                            text.wrap_width /
                            std::max(1, text.font_pixel_size) * 1.75f)))
          : std::max(1, chars);
  const auto lines = std::max(1, (chars + line_width - 1) / line_width);
  return {std::min(chars, line_width) *
              std::max(1, text.font_pixel_size / 2),
          lines * (text.font_pixel_size + 3)};
}

TextExtent measure(const TextMeasurer &callback, const Text &text) {
  const auto result = callback ? callback(text) : fallback_measure(text);
  return {std::max(0, result.width),
          std::max(text.font_pixel_size, result.height)};
}

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void clipped_text(DrawList &out, Point at, std::string value, Color color,
                  int pixels, float wrap, const UiRect &clip,
                  TextAlign align = TextAlign::Left) {
  out.overlay.emplace_back(Text{at, std::move(value), color, pixels, wrap,
                                clip, align, FontFace::Interface});
}

struct CardLayout {
  UiRect bounds, metadata_bounds, message_bounds;
};

// Same geometry contract as the notification panel: cards stacked
// top-down (entries already newest-first), translated by scroll.
struct ChronicleLayout {
  UiRect panel, header, close_button, refresh_button, domain_button,
      significance_button, list_viewport, empty_hint;
  std::vector<CardLayout> entries;
  float scale{}, content_height{}, max_scroll{}, scroll{};
};

ChronicleLayout chronicle_layout_for(const ChronicleSnapshot &snap,
                                     int width, int height,
                                     const TextMeasurer &measurer,
                                     float requested_scroll) {
  ChronicleLayout layout;
  if (width <= 0 || height <= 0) return layout;
  const float sw = static_cast<float>(width), sh = static_cast<float>(height);
  layout.scale = std::clamp(std::min(sw / 1280.f, sh / 900.f), .72f, 1.65f);
  const float s = layout.scale;
  const float inset = 12.f * s;
  const float panel_width =
      std::clamp(sw - inset * 2.f, 320.f * s, 520.f * s);
  const float panel_height =
      std::clamp(sh - 96.f * s, 320.f * s, 640.f * s);
  layout.panel = {std::max(inset, (sw - panel_width) * .5f), 68.f * s,
                  panel_width, std::min(panel_height, sh - 2.f * inset)};
  const float pad = 12.f * s;
  layout.header = {layout.panel.x + pad, layout.panel.y + pad,
                   layout.panel.width - 2.f * pad, 28.f * s};
  layout.close_button = {layout.header.x + layout.header.width - 26.f * s,
                         layout.header.y, 26.f * s, 26.f * s};
  layout.refresh_button = {layout.close_button.x - 86.f * s,
                           layout.header.y, 80.f * s, 26.f * s};
  // Domain filter sits in the intro row, right-aligned beside the
  // subtitle — the header row has no room for a third control.
  layout.domain_button = {
      layout.panel.x + layout.panel.width - pad - 120.f * s,
      layout.header.y + layout.header.height + 4.f * s, 120.f * s,
      24.f * s};
  layout.significance_button = {layout.domain_button.x - 76.f * s,
                                layout.domain_button.y, 70.f * s,
                                layout.domain_button.height};
  const float intro_height = 32.f * s;
  layout.list_viewport = {
      layout.panel.x + pad, layout.header.y + layout.header.height + intro_height,
      layout.panel.width - 2.f * pad,
      std::max(1.f, layout.panel.y + layout.panel.height - pad -
                        (layout.header.y + layout.header.height + intro_height))};
  layout.empty_hint = layout.list_viewport;
  const int body_pixels = std::max(11, static_cast<int>(std::lround(13.f * s)));
  const int small_pixels = std::max(10, static_cast<int>(std::lround(11.f * s)));
  const float card_pad = 8.f * s;
  float cursor = 0.f;
  for (const auto &entry : snap.entries) {
    const UiRect measure_clip{
        0, 0, std::max(1.f, layout.list_viewport.width - card_pad * 2.f), 1.f};
    const Text metadata{{},
                        upper(entry.category) + "  " + entry.date,
                        muted_color, small_pixels, measure_clip.width,
                        std::nullopt, TextAlign::Left, FontFace::Interface};
    const auto metadata_height =
        static_cast<float>(measure(measurer, metadata).height);
    const Text message{{}, entry.summary, message_color, body_pixels,
                       measure_clip.width, std::nullopt, TextAlign::Left,
                       FontFace::Interface};
    const float message_height =
        static_cast<float>(measure(measurer, message).height);
    const float card_height =
        card_pad + metadata_height + 3.f * s + message_height + card_pad;
    CardLayout card;
    card.bounds = {layout.list_viewport.x, layout.list_viewport.y + cursor,
                   layout.list_viewport.width, card_height};
    card.metadata_bounds = {card.bounds.x + card_pad,
                            card.bounds.y + card_pad,
                            card.bounds.width - card_pad * 2.f,
                            metadata_height};
    card.message_bounds = {card.bounds.x + card_pad,
                           card.metadata_bounds.y + metadata_height + 3.f * s,
                           card.bounds.width - card_pad * 2.f,
                           message_height};
    layout.entries.push_back(card);
    cursor += card_height + 7.f * s;
  }
  layout.content_height =
      std::max(0.f, cursor - (snap.entries.empty() ? 0.f : 7.f * s));
  layout.max_scroll =
      std::max(0.f, layout.content_height - layout.list_viewport.height);
  layout.scroll = std::clamp(
      std::isfinite(requested_scroll) ? requested_scroll : 0.f, 0.f,
      layout.max_scroll);
  for (auto &card : layout.entries) {
    card.bounds.y -= layout.scroll;
    card.metadata_bounds.y -= layout.scroll;
    card.message_bounds.y -= layout.scroll;
  }
  return layout;
}

} // namespace

const char *category_label(std::string_view category) noexcept {
  if (category.starts_with("construction.")) return "Construction";
  if (category.starts_with("shipbuilding.")) return "Ships";
  if (category.starts_with("research.")) return "Research";
  if (category.starts_with("exploration.")) return "Exploration";
  if (category.starts_with("colony.")) return "Colony";
  if (category.starts_with("war.")) return "Combat";
  return nullptr;
}

ChronicleSnapshot snapshot(const engine::EventHistory &history,
                           int observer_civilization_id,
                           std::size_t max_entries,
                           std::string_view category_prefix,
                           double min_significance) {
  const auto events = history.feed(
      static_cast<std::uint64_t>(observer_civilization_id),
      -std::numeric_limits<double>::infinity(), min_significance);
  ChronicleSnapshot snap;
  snap.entries.reserve(std::min(max_entries, events.size()));
  for (auto it = events.end(); it != events.begin();) {
    --it;
    const auto *event = *it;
    if (!category_prefix.empty() &&
        !event->category.starts_with(category_prefix))
      continue;
    ++snap.total;
    if (snap.entries.size() >= max_entries) continue;
    const char *label = category_label(event->category);
    snap.entries.push_back({event->id,
                            label ? std::string(label) : event->category,
                            native_campaign::format_campaign_date(
                                event->at_day),
                            event->summary});
  }
  return snap;
}

void NativeChronicleView::cancel_press() noexcept {
  pointer_captured_ = false;
  press_target_ = PressTarget::None;
}

void NativeChronicleView::open(const engine::EventHistory &history,
                               int observer_civilization_id) {
  visible_ = true;
  scroll_ = 0.f;
  history_ = &history;
  observer_ = observer_civilization_id;
  domain_filter_.clear();
  significance_floor_ = 0.0;
  snapshot_ = snapshot(history, observer_civilization_id);
  cancel_press();
}

void NativeChronicleView::close() noexcept {
  visible_ = false;
  scroll_ = 0.f;
  cancel_press();
}

void NativeChronicleView::refresh() {
  if (!history_) return;
  snapshot_ = snapshot(*history_, observer_, 4000, domain_filter_,
                       significance_floor_);
  scroll_ = 0.f;
}

void NativeChronicleView::cycle_significance() {
  static constexpr std::array<double, 4> floors{0.0, 0.3, 0.5, 0.7};
  const auto it = std::ranges::find_if(
      floors, [&](double f) { return significance_floor_ < f - 1e-9; });
  significance_floor_ = it == floors.end() ? 0.0 : *it;
  refresh();
}

void NativeChronicleView::cycle_domain() {
  static constexpr std::array<std::string_view, 6> domains{
      "construction.", "shipbuilding.", "research.",
      "exploration.",   "colony.",       "war."};
  if (domain_filter_.empty()) {
    domain_filter_ = std::string(domains.front());
  } else {
    const auto it = std::ranges::find(domains, domain_filter_);
    domain_filter_ = (it == domains.end() || it + 1 == domains.end())
                         ? std::string{}
                         : std::string(*(it + 1));
  }
  refresh();
}

bool NativeChronicleView::handle(const native_map::InputEvent &event,
                                 int width, int height) {
  if (!visible_) return false;
  pointer_ = event.position;
  const auto layout =
      chronicle_layout_for(snapshot_, width, height, measure_, scroll_);
  scroll_ = layout.scroll;
  if (event.type == native_map::InputEventType::EscapePressed) {
    close();
    return true;
  }
  if (event.type == native_map::InputEventType::PointerCancelled) {
    const bool captured = pointer_captured_;
    cancel_press();
    return captured;
  }
  if (event.type == native_map::InputEventType::Wheel) {
    if (!layout.panel.contains(event.position)) return false;
    press_target_ = PressTarget::None;
    scroll_ = std::clamp(scroll_ - event.wheel_y * 42.f * layout.scale, 0.f,
                         layout.max_scroll);
    return true;
  }
  if (event.type == native_map::InputEventType::LeftPressed) {
    if (!layout.panel.contains(event.position)) return false;
    pointer_captured_ = true;
    press_origin_ = event.position;
    press_target_ = PressTarget::None;
    if (layout.close_button.contains(event.position))
      press_target_ = PressTarget::Close;
    else if (layout.refresh_button.contains(event.position))
      press_target_ = PressTarget::Refresh;
    else if (layout.domain_button.contains(event.position))
      press_target_ = PressTarget::Domain;
    else if (layout.significance_button.contains(event.position))
      press_target_ = PressTarget::Significance;
    return true;
  }
  if (event.type == native_map::InputEventType::PointerMove &&
      pointer_captured_) {
    const float dx = event.position.x - press_origin_.x,
                dy = event.position.y - press_origin_.y;
    if (dx * dx + dy * dy > 25.f) press_target_ = PressTarget::None;
    return true;
  }
  if (event.type != native_map::InputEventType::LeftReleased ||
      !pointer_captured_) {
    return layout.panel.contains(event.position) &&
           (event.type == native_map::InputEventType::LeftReleased ||
            event.type == native_map::InputEventType::RightPressed ||
            event.type == native_map::InputEventType::RightReleased);
  }
  const auto target = press_target_;
  cancel_press();
  if (target == PressTarget::Close &&
      layout.close_button.contains(event.position))
    close();
  else if (target == PressTarget::Refresh &&
           layout.refresh_button.contains(event.position))
    refresh();
  else if (target == PressTarget::Domain &&
           layout.domain_button.contains(event.position))
    cycle_domain();
  else if (target == PressTarget::Significance &&
           layout.significance_button.contains(event.position))
    cycle_significance();
  return true;
}

void NativeChronicleView::render(DrawList &out, int width, int height) const {
  if (!visible_) return;
  const auto layout =
      chronicle_layout_for(snapshot_, width, height, measure_, scroll_);
  const float s = layout.scale;
  stellar::engine::ui_skin::surface(out, layout.panel, s);
  const int title_pixels = std::max(13, static_cast<int>(std::lround(18.f * s)));
  const auto title_text = resolve(locale_, "CHRONICLE_TITLE", "CHRONICLE");
  const Text title_probe{{}, title_text, title_color, title_pixels, 0.f,
                         std::nullopt, TextAlign::Left, FontFace::Interface};
  const auto title_extent = measure(measure_, title_probe);
  clipped_text(out,
               {layout.header.x,
                layout.header.y +
                    (layout.header.height - title_extent.height) * .5f},
               title_text, title_color, title_pixels, 0.f, layout.header);
  stellar::engine::ui_skin::control(out, layout.refresh_button,
                                    layout.refresh_button.contains(pointer_),
                                    false, true, s);
  const auto refresh_text = resolve(locale_, "CHRONICLE_REFRESH", "REFRESH");
  const int refresh_pixels = std::max(9, static_cast<int>(std::lround(10.f * s)));
  const Text refresh_probe{{}, refresh_text, muted_color, refresh_pixels,
                           layout.refresh_button.width - 4.f * s, std::nullopt,
                           TextAlign::Center, FontFace::Interface};
  const auto refresh_extent = measure(measure_, refresh_probe);
  clipped_text(out,
               {layout.refresh_button.x + layout.refresh_button.width * .5f,
                layout.refresh_button.y +
                    (layout.refresh_button.height - refresh_extent.height) * .5f},
               refresh_text, muted_color, refresh_pixels,
               layout.refresh_button.width - 4.f * s, layout.refresh_button,
               TextAlign::Center);
  stellar::engine::ui_skin::control(out, layout.close_button,
                                    layout.close_button.contains(pointer_),
                                    false, true, s);
  const int close_pixels = std::max(10, static_cast<int>(std::lround(12.f * s)));
  const Text close_probe{{}, "X", muted_color, close_pixels, 0.f,
                         std::nullopt, TextAlign::Center, FontFace::Interface};
  const auto close_extent = measure(measure_, close_probe);
  clipped_text(out,
               {layout.close_button.x + layout.close_button.width * .5f,
                layout.close_button.y +
                    (layout.close_button.height - close_extent.height) * .5f},
               "X", muted_color, close_pixels, 0.f, layout.close_button,
               TextAlign::Center);
  stellar::engine::ui_skin::control(out, layout.domain_button,
                                    layout.domain_button.contains(pointer_),
                                    !domain_filter_.empty(), true, s);
  const std::string domain_text =
      domain_filter_.empty()
          ? resolve(locale_, "CHRONICLE_FILTER_ALL", "ALL")
          : upper(std::string(category_label(domain_filter_)));
  const int domain_pixels = std::max(9, static_cast<int>(std::lround(10.f * s)));
  const Text domain_probe{{}, domain_text, muted_color, domain_pixels,
                          layout.domain_button.width - 4.f * s, std::nullopt,
                          TextAlign::Center, FontFace::Interface};
  const auto domain_extent = measure(measure_, domain_probe);
  clipped_text(out,
               {layout.domain_button.x + layout.domain_button.width * .5f,
                layout.domain_button.y +
                    (layout.domain_button.height - domain_extent.height) * .5f},
               domain_text, muted_color, domain_pixels,
               layout.domain_button.width - 4.f * s, layout.domain_button,
               TextAlign::Center);
  stellar::engine::ui_skin::control(
      out, layout.significance_button,
      layout.significance_button.contains(pointer_),
      significance_floor_ > 0.0, true, s);
  char sig_buf[16];
  if (significance_floor_ > 0.0)
    std::snprintf(sig_buf, sizeof(sig_buf), ">=%.1f", significance_floor_);
  const std::string sig_text =
      significance_floor_ > 0.0
          ? std::string(sig_buf)
          : resolve(locale_, "CHRONICLE_FILTER_ALL", "ALL");
  const Text sig_probe{{}, sig_text, muted_color, domain_pixels,
                       layout.significance_button.width - 4.f * s,
                       std::nullopt, TextAlign::Center, FontFace::Interface};
  const auto sig_extent = measure(measure_, sig_probe);
  clipped_text(
      out,
      {layout.significance_button.x + layout.significance_button.width * .5f,
       layout.significance_button.y +
           (layout.significance_button.height - sig_extent.height) * .5f},
      sig_text, muted_color, domain_pixels,
      layout.significance_button.width - 4.f * s, layout.significance_button,
      TextAlign::Center);
  const std::array<std::string, 2> args{
      std::to_string(snapshot_.entries.size()),
      std::to_string(snapshot_.total)};
  std::string subtitle;
  if (snapshot_.total > snapshot_.entries.size()) {
    subtitle = locale_ ? locale_->format("CHRONICLE_SUBTITLE_CAPPED",
                                       std::span<const std::string>(args))
                       : "Recorded history — newest " + args[0] + " of " +
                             args[1] + " events.";
  } else {
    subtitle = locale_ ? locale_->format("CHRONICLE_SUBTITLE",
                                       std::span<const std::string>(args)
                                            .subspan(1))
                       : "Recorded history — " + args[1] + " events.";
  }
  clipped_text(out,
               {layout.list_viewport.x,
                layout.header.y + layout.header.height + 13.f * s},
               subtitle, muted_color,
               std::max(9, static_cast<int>(std::lround(11.f * s))),
               std::max(1.f, layout.significance_button.x -
                                 layout.list_viewport.x - 8.f * s),
               layout.panel);
  if (snapshot_.entries.empty()) {
    clipped_text(out, {layout.empty_hint.x, layout.empty_hint.y},
                 resolve(locale_, "CHRONICLE_EMPTY",
                         "No recorded events yet."),
                 muted_color, std::max(11, static_cast<int>(std::lround(13.f * s))),
                 layout.empty_hint.width, layout.empty_hint);
    return;
  }
  for (std::size_t i = 0; i < layout.entries.size(); ++i) {
    const auto &card = layout.entries[i];
    if (!intersects(card.bounds, layout.list_viewport)) continue;
    const auto &entry = snapshot_.entries[i];
    stellar::engine::ui_skin::surface(out, card.bounds, s, false,
                                      layout.list_viewport);
    clipped_text(out, {card.metadata_bounds.x, card.metadata_bounds.y},
                 entry_label(entry.category, locale_) + "  " + entry.date,
                 label_color(entry.category),
                 std::max(9, static_cast<int>(std::lround(11.f * s))),
                 card.metadata_bounds.width, layout.list_viewport);
    clipped_text(out, {card.message_bounds.x, card.message_bounds.y},
                 entry.summary, message_color,
                 std::max(11, static_cast<int>(std::lround(13.f * s))),
                 card.message_bounds.width, layout.list_viewport);
  }
  if (layout.max_scroll > 0.f) {
    const float thumb_h =
        std::max(16.f * s, layout.list_viewport.height *
                              layout.list_viewport.height /
                              layout.content_height);
    const float y = layout.list_viewport.y +
                    (layout.list_viewport.height - thumb_h) *
                        (layout.scroll / layout.max_scroll);
    fill(out,
         {layout.list_viewport.x + layout.list_viewport.width - 3.f * s, y,
          2.f * s, thumb_h},
         muted_color);
  }
}

} // namespace stellar::native_chronicle
