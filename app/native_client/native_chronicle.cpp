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

struct TagChip {
  std::string tag;
  UiRect bounds;
};

struct CardLayout {
  UiRect bounds, metadata_bounds, message_bounds;
  bool navigable{}; // the card body activates only when it navigates
  std::optional<UiRect> contact_button;
  std::vector<TagChip> tag_chips;
};

// Same geometry contract as the notification panel: cards stacked
// top-down (entries already newest-first), translated by scroll.
struct ChronicleLayout {
  UiRect panel, header, close_button, refresh_button, search_box,
      domain_button, significance_button, actor_button, time_button,
      list_viewport, empty_hint;
  std::optional<UiRect> focus_button; // clears the active tag focus
  // Window paging — only exist while a bounded recency window is
  // active (paging "all history" is meaningless).
  std::optional<UiRect> page_older_button, page_newer_button;
  std::vector<CardLayout> entries;
  float scale{};
  stellar::engine::ScrollView scroll{};
};

ChronicleLayout chronicle_layout_for(const ChronicleSnapshot &snap,
                                     int width, int height,
                                     const TextMeasurer &measurer,
                                     float requested_scroll,
                                     std::string_view tag_filter,
                                     bool paged_window) {
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
  // Free-text search sits in the header between the title and REFRESH.
  layout.search_box = {layout.header.x + 104.f * s,
                       layout.header.y + 2.f * s,
                       std::max(24.f * s, layout.refresh_button.x -
                                             layout.header.x -
                                             110.f * s),
                       24.f * s};
  // Domain filter sits in the intro row, right-aligned beside the
  // subtitle — the header row has no room for a third control.
  layout.domain_button = {
      layout.panel.x + layout.panel.width - pad - 120.f * s,
      layout.header.y + layout.header.height + 4.f * s, 120.f * s,
      24.f * s};
  layout.significance_button = {layout.domain_button.x - 76.f * s,
                                layout.domain_button.y, 70.f * s,
                                layout.domain_button.height};
  layout.actor_button = {layout.significance_button.x - 76.f * s,
                         layout.domain_button.y, 70.f * s,
                         layout.domain_button.height};
  layout.time_button = {layout.actor_button.x - 70.f * s,
                        layout.domain_button.y, 64.f * s,
                        layout.domain_button.height};
  if (paged_window) {
    layout.page_newer_button = {layout.time_button.x - 24.f * s,
                                layout.time_button.y, 20.f * s,
                                layout.time_button.height};
    layout.page_older_button = {layout.page_newer_button->x - 24.f * s,
                                layout.time_button.y, 20.f * s,
                                layout.time_button.height};
  }
  if (!tag_filter.empty())
    layout.focus_button = {layout.panel.x + pad, layout.domain_button.y,
                           104.f * s, layout.domain_button.height};
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
    // A single foreign actor reserves a right-side strip for the DIP
    // action so the button never underlays message text.
    const float contact_reserve = entry.contact_id != 0 ? 54.f * s : 0.f;
    const Text message{{}, entry.summary, message_color, body_pixels,
                       std::max(1.f, measure_clip.width - contact_reserve),
                       std::nullopt, TextAlign::Left, FontFace::Interface};
    const float message_height =
        static_cast<float>(measure(measurer, message).height);
    const float chip_height =
        entry.tags.empty() ? 0.f : 3.f * s + 16.f * s;
    const float card_height = card_pad + metadata_height + 3.f * s +
                              message_height + chip_height + card_pad;
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
    card.navigable = entry.system_id != 0;
    if (entry.contact_id != 0) {
      const float bw = 44.f * s, bh = 20.f * s;
      card.contact_button = UiRect{
          card.bounds.x + card.bounds.width - card_pad - bw,
          card.message_bounds.y +
              std::max(0.f, (message_height - bh) * .5f),
          bw, bh};
    }
    // Reference tags render as clickable chips — clicking one focuses
    // the browser on that exact entity reference (HistoryQuery::tag
    // semantics). Flow left-to-right; chips that would overflow the
    // card are simply not shown (the tag still exists on the record).
    float chip_x = card.bounds.x + card_pad;
    const float chip_y =
        card.message_bounds.y + message_height + 3.f * s;
    const float chip_right = card.bounds.x + card.bounds.width - card_pad;
    for (const auto &tag : entry.tags) {
      const Text probe{{}, upper(tag), muted_color, small_pixels, 0.f,
                       std::nullopt, TextAlign::Left, FontFace::Interface};
      const float chip_width =
          std::clamp(static_cast<float>(measure(measurer, probe).width) +
                         10.f * s,
                     28.f * s, 96.f * s);
      if (chip_x + chip_width > chip_right) break;
      card.tag_chips.push_back({tag, {chip_x, chip_y, chip_width, 16.f * s}});
      chip_x += chip_width + 5.f * s;
    }
    layout.entries.push_back(card);
    cursor += card_height + 7.f * s;
  }
  layout.scroll.sync(
      std::max(0.f, cursor - (snap.entries.empty() ? 0.f : 7.f * s)),
      layout.list_viewport.height);
  layout.scroll.scroll_to(requested_scroll);
  for (auto &card : layout.entries) {
    card.bounds.y -= layout.scroll.scroll_offset;
    card.metadata_bounds.y -= layout.scroll.scroll_offset;
    card.message_bounds.y -= layout.scroll.scroll_offset;
    if (card.contact_button)
      card.contact_button->y -= layout.scroll.scroll_offset;
    for (auto &chip : card.tag_chips)
      chip.bounds.y -= layout.scroll.scroll_offset;
  }
  return layout;
}

std::optional<UiRect> clipped(UiRect a, const UiRect &b) {
  const float x1 = std::min(a.x + a.width, b.x + b.width),
              y1 = std::min(a.y + a.height, b.y + b.height);
  a.x = std::max(a.x, b.x);
  a.y = std::max(a.y, b.y);
  a.width = x1 - a.x;
  a.height = y1 - a.y;
  if (a.width <= 0.f || a.height <= 0.f) return std::nullopt;
  return a;
}

// Keyboard-focus contract: every actionable control in visual (y,x)
// order — the header row, the intro-row cyclers, then each visible
// card's body (located entries only — an unlocated card activates to
// nothing), DIP action and tag chips.
struct FocusTarget {
  UiRect bounds;
  std::string label;
};

std::vector<FocusTarget> focusables(
    const ChronicleLayout &layout, const ChronicleSnapshot &snap,
    const stellar::engine::LocalizationTable *locale) {
  std::vector<FocusTarget> out;
  out.push_back({layout.search_box,
                 resolve(locale, "CHRONICLE_SEARCH", "Search chronicle")});
  out.push_back({layout.refresh_button,
                 resolve(locale, "CHRONICLE_REFRESH", "Refresh")});
  out.push_back({layout.close_button,
                 resolve(locale, "CHRONICLE_CLOSE", "Close chronicle")});
  if (layout.focus_button)
    out.push_back({*layout.focus_button,
                   resolve(locale, "CHRONICLE_CLEAR_TAG", "Clear tag focus")});
  if (layout.page_older_button)
    out.push_back({*layout.page_older_button,
                   resolve(locale, "CHRONICLE_PAGE_OLDER", "Older page")});
  if (layout.page_newer_button)
    out.push_back({*layout.page_newer_button,
                   resolve(locale, "CHRONICLE_PAGE_NEWER", "Newer page")});
  out.push_back({layout.time_button,
                 resolve(locale, "CHRONICLE_TIME_FILTER", "Time filter")});
  out.push_back({layout.actor_button,
                 resolve(locale, "CHRONICLE_ACTOR_FILTER", "Actor filter")});
  out.push_back({layout.significance_button,
                 resolve(locale, "CHRONICLE_SIGNIFICANCE_FILTER",
                         "Significance filter")});
  out.push_back({layout.domain_button,
                 resolve(locale, "CHRONICLE_DOMAIN_FILTER", "Domain filter")});
  for (std::size_t i = 0; i < layout.entries.size(); ++i) {
    const auto &card = layout.entries[i];
    const std::string card_label =
        i < snap.entries.size()
            ? entry_label(snap.entries[i].category, locale) + "  " +
                  snap.entries[i].date
            : std::string{};
    if (card.navigable)
      if (const auto clip = clipped(card.bounds, layout.list_viewport))
        out.push_back({*clip, card_label});
    if (card.contact_button)
      if (const auto clip =
              clipped(*card.contact_button, layout.list_viewport))
        out.push_back(
            {*clip, resolve(locale, "CHRONICLE_CONTACT", "Contact")});
    for (const auto &chip : card.tag_chips)
      if (const auto clip = clipped(chip.bounds, layout.list_viewport))
        out.push_back(
            {*clip, resolve(locale, "CHRONICLE_TAG", "Tag") + " " + chip.tag});
  }
  std::ranges::sort(out, [](const FocusTarget &a, const FocusTarget &b) {
    return a.bounds.y != b.bounds.y ? a.bounds.y < b.bounds.y
                                    : a.bounds.x < b.bounds.x;
  });
  return out;
}

} // namespace

const char *category_label(std::string_view category) noexcept {
  if (category.starts_with("construction.")) return "Construction";
  if (category.starts_with("shipbuilding.")) return "Ships";
  if (category.starts_with("research.")) return "Research";
  if (category.starts_with("exploration.")) return "Exploration";
  if (category.starts_with("colony.")) return "Colony";
  if (category.starts_with("war.")) return "Combat";
  if (category.starts_with("diplomacy.")) return "Diplomacy";
  return nullptr;
}

ChronicleSnapshot snapshot(const engine::EventHistory &history,
                           int observer_civilization_id,
                           const ChronicleFilter &filter,
                           std::size_t max_entries) {
  const auto observer =
      static_cast<std::uint64_t>(observer_civilization_id);
  // feed() == query() with these three fields — query() is the same
  // observer-safe projection plus the before_day axis feed() lacks.
  engine::HistoryQuery q;
  q.observer = observer;
  q.after_day = filter.since_day;
  q.min_significance = filter.min_significance;
  q.before_day = filter.before_day;
  const auto events = history.query(q);
  const std::string needle = upper(std::string(filter.search));
  ChronicleSnapshot snap;
  snap.entries.reserve(std::min(max_entries, events.size()));
  for (auto it = events.end(); it != events.begin();) {
    --it;
    const auto *event = *it;
    if (!filter.category_prefix.empty() &&
        !event->category.starts_with(filter.category_prefix))
      continue;
    if (filter.actor != 0 &&
        std::ranges::find(event->actors, filter.actor) ==
            event->actors.end())
      continue;
    if (!filter.tag.empty() &&
        std::ranges::find(event->tags, filter.tag) == event->tags.end())
      continue;
    if (!needle.empty() &&
        upper(event->summary).find(needle) == std::string::npos &&
        upper(event->category).find(needle) == std::string::npos &&
        std::ranges::none_of(event->tags, [&](const auto &t) {
          return upper(t).find(needle) != std::string::npos;
        }))
      continue;
    ++snap.total;
    if (snap.entries.size() >= max_entries) continue;
    const char *label = category_label(event->category);
    // Exactly one foreign actor → the entry can offer a diplomatic
    // jump (first contacts, battles, treaties). Multiple or zero
    // foreign actors carry no unambiguous contact.
    std::uint64_t contact = 0;
    for (const auto id : event->actors)
      if (id != observer) {
        if (contact != 0 && contact != id) { contact = 0; break; }
        contact = id;
      }
    snap.entries.push_back({event->id, event->location, contact,
                            label ? std::string(label) : event->category,
                            native_campaign::format_campaign_date(
                                event->at_day),
                            event->summary, event->tags});
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
  scroll_ = {};
  history_ = &history;
  observer_ = observer_civilization_id;
  domain_filter_.clear();
  significance_floor_ = 0.0;
  actor_filter_ = 0;
  tag_filter_.clear();
  recency_window_ = 0.0;
  window_page_ = 0;
  search_.clear();
  search_focused_ = false;
  focus_ = -1;
  snapshot_ = snapshot(history, observer_civilization_id);
  cancel_press();
}

void NativeChronicleView::close() noexcept {
  visible_ = false;
  scroll_ = {};
  search_focused_ = false;
  focus_ = -1;
  cancel_press();
}

void NativeChronicleView::refresh() {
  if (!history_) return;
  // Recency window rides the feed's own since_day bound; without a
  // campaign-day source the filter is inert (all history).
  ChronicleFilter filter;
  filter.category_prefix = domain_filter_;
  filter.min_significance = significance_floor_;
  filter.actor = actor_filter_;
  filter.tag = tag_filter_;
  if (recency_window_ > 0.0 && campaign_day_source_) {
    // Window [end - width, end]. Both HistoryQuery bounds are inclusive,
    // so an entry at exactly `end` would repeat on the next-older page —
    // make older pages half-open on their upper edge (the newest page
    // keeps `end` inclusive: nothing newer exists to claim it).
    const double end =
        campaign_day_source_() - recency_window_ * window_page_;
    filter.since_day = end - recency_window_;
    filter.before_day =
        window_page_ == 0
            ? end
            : std::nextafter(end, -std::numeric_limits<double>::infinity());
  } else {
    filter.since_day = -std::numeric_limits<double>::infinity();
  }
  filter.search = search_;
  snapshot_ = snapshot(*history_, observer_, filter);
  scroll_ = {};
}

void NativeChronicleView::cycle_recency() {
  static constexpr std::array<double, 3> windows{30.0, 365.0, 3650.0};
  const auto it = std::ranges::find_if(
      windows, [&](double w) { return recency_window_ < w - 1e-9; });
  recency_window_ = it == windows.end() ? 0.0 : *it;
  window_page_ = 0; // a changed window restarts at the present edge
  refresh();
}

void NativeChronicleView::page_older() {
  if (recency_window_ <= 0.0) return;
  ++window_page_;
  refresh();
}

void NativeChronicleView::page_newer() {
  if (window_page_ == 0) return;
  --window_page_;
  refresh();
}

void NativeChronicleView::cycle_actor() {
  if (!history_) return;
  // Distinct actors across the observer's visible feed — ALL first,
  // then the observer (MINE), then every other involved civilization
  // in ascending id order. The observer's own feed is the authorized
  // projection, so actor ids gathered here stay privacy-safe.
  const auto observer = static_cast<std::uint64_t>(observer_);
  const auto events = history_->feed(
      observer, -std::numeric_limits<double>::infinity(), 0.0);
  std::vector<std::uint64_t> actors;
  for (const auto *event : events)
    for (const auto id : event->actors)
      if (id != observer &&
          std::ranges::find(actors, id) == actors.end())
        actors.push_back(id);
  std::ranges::sort(actors);
  actors.insert(actors.begin(), {0, observer});
  const auto it = std::ranges::find(actors, actor_filter_);
  actor_filter_ =
      (it == actors.end() || it + 1 == actors.end()) ? 0 : *(it + 1);
  refresh();
}

void NativeChronicleView::cycle_significance() {
  static constexpr std::array<double, 4> floors{0.0, 0.3, 0.5, 0.7};
  const auto it = std::ranges::find_if(
      floors, [&](double f) { return significance_floor_ < f - 1e-9; });
  significance_floor_ = it == floors.end() ? 0.0 : *it;
  refresh();
}

void NativeChronicleView::cycle_domain() {
  static constexpr std::array<std::string_view, 7> domains{
      "construction.", "shipbuilding.", "research.",   "exploration.",
      "colony.",       "war.",          "diplomacy."};
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

std::string NativeChronicleView::focused_label(int width,
                                               int height) const {
  if (focus_ < 0 || !visible_) return {};
  const auto layout = chronicle_layout_for(
      snapshot_, width, height, measure_, scroll_.scroll_offset, tag_filter_,
      recency_window_ > 0.0);
  const auto items = focusables(layout, snapshot_, locale_);
  return focus_ < static_cast<int>(items.size())
             ? items[static_cast<std::size_t>(focus_)].label
             : std::string{};
}

bool NativeChronicleView::handle(const native_map::InputEvent &event,
                                 int width, int height) {
  if (!visible_) return false;
  pointer_ = event.position;
  const auto layout = chronicle_layout_for(
      snapshot_, width, height, measure_, scroll_.scroll_offset,
      tag_filter_, recency_window_ > 0.0);
  scroll_ = layout.scroll;
  if (event.type == native_map::InputEventType::EscapePressed) {
    // Escape unfocuses the search field first, then closes the view.
    if (search_focused_) {
      search_focused_ = false;
      return true;
    }
    close();
    return true;
  }
  if (event.type == native_map::InputEventType::TextEntered ||
      event.type == native_map::InputEventType::BackspacePressed) {
    // The overlay is modal — text events never fall through while it
    // is open; only the focused search field consumes them.
    if (!search_focused_) return true;
    if (event.type == native_map::InputEventType::TextEntered) {
      if (search_.size() + event.text.size() <= 120)
        search_ += event.text;
    } else if (!search_.empty()) {
      // UTF-8-safe pop (same rule as the assets search field).
      auto n = search_.size() - 1;
      while (n > 0 &&
             (static_cast<unsigned char>(search_[n]) & 0xc0) == 0x80)
        --n;
      search_.resize(n);
    }
    refresh();
    return true;
  }
  if (event.type == native_map::InputEventType::KeyPressed &&
      event.key) {
    if (search_focused_) {
      // While editing, the search field owns the keyboard — Tab or
      // Return commit out of it; every other key stays captured.
      if (event.key == 9u || event.key == 13u) search_focused_ = false;
      return true;
    }
    // SDL_Keycode: Tab/arrows walk the (y,x)-ordered focusables,
    // Home/End jump to the ends, and Return/Space replay the
    // press/release pair through the same dispatch a click takes.
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    const auto items = focusables(layout, snapshot_, locale_);
    const int count = static_cast<int>(items.size());
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      return true;
    }
    if (count > 0 && (fwd || bwd)) {
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      return true;
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &rect = items[static_cast<std::size_t>(focus_)].bounds;
      native_map::InputEvent press{native_map::InputEventType::LeftPressed};
      press.position = {rect.x + rect.width * .5f,
                        rect.y + rect.height * .5f};
      native_map::InputEvent release = press;
      release.type = native_map::InputEventType::LeftReleased;
      const int keep = focus_;
      (void)handle(press, width, height);
      (void)handle(release, width, height);
      if (visible_) focus_ = keep;
      return true;
    }
    // Unhandled keys keep falling through to global shortcuts.
  }
  if (event.type == native_map::InputEventType::PointerCancelled) {
    const bool captured = pointer_captured_;
    focus_ = -1;
    cancel_press();
    return captured;
  }
  if (event.type == native_map::InputEventType::Wheel) {
    if (!layout.panel.contains(event.position)) return false;
    press_target_ = PressTarget::None;
    scroll_.scroll_by(-event.wheel_y * 42.f * layout.scale);
    return true;
  }
  if (event.type == native_map::InputEventType::LeftPressed) {
    if (!layout.panel.contains(event.position)) {
      search_focused_ = false;
      return false;
    }
    focus_ = -1;
    pointer_captured_ = true;
    press_origin_ = event.position;
    press_target_ = PressTarget::None;
    search_focused_ = layout.search_box.contains(event.position);
    if (search_focused_)
      press_target_ = PressTarget::Search;
    else if (layout.close_button.contains(event.position))
      press_target_ = PressTarget::Close;
    else if (layout.refresh_button.contains(event.position))
      press_target_ = PressTarget::Refresh;
    else if (layout.domain_button.contains(event.position))
      press_target_ = PressTarget::Domain;
    else if (layout.significance_button.contains(event.position))
      press_target_ = PressTarget::Significance;
    else if (layout.actor_button.contains(event.position))
      press_target_ = PressTarget::Actor;
    else if (layout.time_button.contains(event.position))
      press_target_ = PressTarget::Time;
    else if (layout.page_older_button &&
             layout.page_older_button->contains(event.position))
      press_target_ = PressTarget::PageOlder;
    else if (layout.page_newer_button &&
             layout.page_newer_button->contains(event.position))
      press_target_ = PressTarget::PageNewer;
    else if (layout.focus_button &&
             layout.focus_button->contains(event.position))
      press_target_ = PressTarget::FocusClear;
    else if (layout.list_viewport.contains(event.position)) {
      for (std::size_t i = 0; i < layout.entries.size(); ++i)
        if (layout.entries[i].bounds.contains(event.position)) {
          press_target_ = PressTarget::Entry;
          press_entry_ = i;
          for (std::size_t c = 0;
               c < layout.entries[i].tag_chips.size(); ++c)
            if (layout.entries[i].tag_chips[c].bounds.contains(
                    event.position)) {
              press_target_ = PressTarget::Tag;
              press_tag_ = c;
              break;
            }
          if (press_target_ == PressTarget::Entry &&
              layout.entries[i].contact_button &&
              layout.entries[i].contact_button->contains(event.position))
            press_target_ = PressTarget::Contact;
          break;
        }
    }
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
  else if (target == PressTarget::Actor &&
           layout.actor_button.contains(event.position))
    cycle_actor();
  else if (target == PressTarget::Time &&
           layout.time_button.contains(event.position))
    cycle_recency();
  else if (target == PressTarget::PageOlder &&
           layout.page_older_button &&
           layout.page_older_button->contains(event.position))
    page_older();
  else if (target == PressTarget::PageNewer &&
           layout.page_newer_button &&
           layout.page_newer_button->contains(event.position) &&
           window_page_ > 0)
    page_newer();
  else if (target == PressTarget::Entry &&
           press_entry_ < layout.entries.size() &&
           layout.entries[press_entry_].bounds.contains(event.position) &&
           snapshot_.entries[press_entry_].system_id != 0)
    navigation_ = snapshot_.entries[press_entry_].system_id;
  else if (target == PressTarget::Contact &&
           press_entry_ < layout.entries.size() &&
           layout.entries[press_entry_].contact_button &&
           layout.entries[press_entry_].contact_button->contains(
               event.position))
    contact_navigation_ = snapshot_.entries[press_entry_].contact_id;
  else if (target == PressTarget::Tag &&
           press_entry_ < layout.entries.size() &&
           press_tag_ < layout.entries[press_entry_].tag_chips.size() &&
           layout.entries[press_entry_]
               .tag_chips[press_tag_]
               .bounds.contains(event.position)) {
    // Toggle: re-clicking the focused chip clears the focus.
    const auto &tag =
        layout.entries[press_entry_].tag_chips[press_tag_].tag;
    tag_filter_ = (tag_filter_ == tag) ? std::string{} : tag;
    refresh();
  } else if (target == PressTarget::FocusClear && layout.focus_button &&
             layout.focus_button->contains(event.position)) {
    tag_filter_.clear();
    refresh();
  }
  return true;
}

void NativeChronicleView::render(DrawList &out, int width, int height) const {
  if (!visible_) return;
  const auto layout = chronicle_layout_for(
      snapshot_, width, height, measure_, scroll_.scroll_offset,
      tag_filter_, recency_window_ > 0.0);
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
  // Search field — focused state gets the accent border; empty shows a
  // dim placeholder.
  fill(out, layout.search_box, {3, 13, 22, 245});
  out.overlay.emplace_back(native_map::StrokedRectangle{
      layout.search_box,
      search_focused_ ? Color{80, 200, 230, 255}
                      : Color{54, 111, 140, 255}});
  const int search_pixels =
      std::max(9, static_cast<int>(std::lround(10.f * s)));
  const std::string search_text =
      search_.empty()
          ? resolve(locale_, "CHRONICLE_SEARCH_HINT", "SEARCH")
          : upper(search_);
  const Text search_probe{{}, search_text,
                          search_.empty() ? muted_color : title_color,
                          search_pixels,
                          layout.search_box.width - 8.f * s, std::nullopt,
                          TextAlign::Left, FontFace::Interface};
  const auto search_extent = measure(measure_, search_probe);
  clipped_text(
      out,
      {layout.search_box.x + 4.f * s,
       layout.search_box.y +
           (layout.search_box.height - search_extent.height) * .5f},
      search_text, search_.empty() ? muted_color : title_color,
      search_pixels, layout.search_box.width - 8.f * s,
      layout.search_box);
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
  stellar::engine::ui_skin::control(out, layout.actor_button,
                                    layout.actor_button.contains(pointer_),
                                    actor_filter_ != 0, true, s);
  std::string actor_text;
  if (actor_filter_ == 0) {
    actor_text = resolve(locale_, "CHRONICLE_FILTER_ALL", "ALL");
  } else if (actor_filter_ ==
             static_cast<std::uint64_t>(observer_)) {
    actor_text = resolve(locale_, "CHRONICLE_SCOPE_MINE", "MINE");
  } else {
    if (actor_name_resolver_)
      actor_text = actor_name_resolver_(actor_filter_);
    if (actor_text.empty()) {
      char civ_buf[24];
      std::snprintf(civ_buf, sizeof(civ_buf), "CIV %llu",
                    static_cast<unsigned long long>(actor_filter_));
      actor_text = civ_buf;
    }
  }
  actor_text = upper(std::move(actor_text));
  const Text actor_probe{{}, actor_text, muted_color, domain_pixels,
                         layout.actor_button.width - 4.f * s, std::nullopt,
                         TextAlign::Center, FontFace::Interface};
  const auto actor_extent = measure(measure_, actor_probe);
  clipped_text(
      out,
      {layout.actor_button.x + layout.actor_button.width * .5f,
       layout.actor_button.y +
           (layout.actor_button.height - actor_extent.height) * .5f},
      actor_text, muted_color, domain_pixels,
      layout.actor_button.width - 4.f * s, layout.actor_button,
      TextAlign::Center);
  stellar::engine::ui_skin::control(out, layout.time_button,
                                    layout.time_button.contains(pointer_),
                                    recency_window_ > 0.0, true, s);
  std::string time_text;
  if (recency_window_ <= 0.0) {
    time_text = resolve(locale_, "CHRONICLE_FILTER_ALL", "ALL");
  } else if (recency_window_ < 365.0) {
    time_text = resolve(locale_, "CHRONICLE_TIME_30D", "30D");
  } else if (recency_window_ < 3650.0) {
    time_text = resolve(locale_, "CHRONICLE_TIME_1Y", "1Y");
  } else {
    time_text = resolve(locale_, "CHRONICLE_TIME_10Y", "10Y");
  }
  const Text time_probe{{}, time_text, muted_color, domain_pixels,
                        layout.time_button.width - 4.f * s, std::nullopt,
                        TextAlign::Center, FontFace::Interface};
  const auto time_extent = measure(measure_, time_probe);
  clipped_text(
      out,
      {layout.time_button.x + layout.time_button.width * .5f,
       layout.time_button.y +
           (layout.time_button.height - time_extent.height) * .5f},
      time_text, muted_color, domain_pixels,
      layout.time_button.width - 4.f * s, layout.time_button,
      TextAlign::Center);
  if (layout.page_older_button) {
    stellar::engine::ui_skin::control(
        out, *layout.page_older_button,
        layout.page_older_button->contains(pointer_), false, true, s);
    clipped_text(out,
                 {layout.page_older_button->x +
                      layout.page_older_button->width * .5f - 3.f * s,
                  layout.page_older_button->y + 4.f * s},
                 "<", muted_color, domain_pixels,
                 layout.page_older_button->width - 4.f * s,
                 *layout.page_older_button, TextAlign::Center);
  }
  if (layout.page_newer_button) {
    stellar::engine::ui_skin::control(
        out, *layout.page_newer_button,
        layout.page_newer_button->contains(pointer_), false,
        window_page_ > 0, s);
    clipped_text(out,
                 {layout.page_newer_button->x +
                      layout.page_newer_button->width * .5f - 3.f * s,
                  layout.page_newer_button->y + 4.f * s},
                 ">", muted_color, domain_pixels,
                 layout.page_newer_button->width - 4.f * s,
                 *layout.page_newer_button, TextAlign::Center);
  }
  if (layout.focus_button) {
    stellar::engine::ui_skin::control(
        out, *layout.focus_button,
        layout.focus_button->contains(pointer_), true, true, s);
    const std::string focus_text = "X " + upper(tag_filter_);
    const Text focus_probe{{}, focus_text, muted_color, domain_pixels,
                           layout.focus_button->width - 4.f * s,
                           std::nullopt, TextAlign::Center,
                           FontFace::Interface};
    const auto focus_extent = measure(measure_, focus_probe);
    clipped_text(
        out,
        {layout.focus_button->x + layout.focus_button->width * .5f,
         layout.focus_button->y +
             (layout.focus_button->height - focus_extent.height) * .5f},
        focus_text, muted_color, domain_pixels,
        layout.focus_button->width - 4.f * s, *layout.focus_button,
        TextAlign::Center);
  }
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
  const float subtitle_x =
      layout.focus_button ? layout.focus_button->x +
                                layout.focus_button->width + 8.f * s
                          : layout.list_viewport.x;
  // Clamp to the leftmost occupied button so the subtitle never
  // underlays controls (time button sits left of the actor button,
  // and page arrows left of time when a window is bounded).
  const float first_button_x =
      layout.page_older_button ? layout.page_older_button->x
                               : layout.time_button.x;
  clipped_text(out,
               {subtitle_x,
                layout.header.y + layout.header.height + 13.f * s},
               subtitle, muted_color,
               std::max(9, static_cast<int>(std::lround(11.f * s))),
               std::max(1.f, first_button_x - subtitle_x - 8.f * s),
               layout.panel);
  if (snapshot_.entries.empty()) {
    clipped_text(out, {layout.empty_hint.x, layout.empty_hint.y},
                 resolve(locale_, "CHRONICLE_EMPTY",
                         "No recorded events yet."),
                 muted_color, std::max(11, static_cast<int>(std::lround(13.f * s))),
                 layout.empty_hint.width, layout.empty_hint);
  }
  for (std::size_t i = 0; i < layout.entries.size(); ++i) {
    const auto &card = layout.entries[i];
    if (!intersects(card.bounds, layout.list_viewport)) continue;
    const auto &entry = snapshot_.entries[i];
    stellar::engine::ui_skin::surface(
        out, card.bounds, s,
        entry.system_id != 0 && card.bounds.contains(pointer_),
        layout.list_viewport);
    clipped_text(out, {card.metadata_bounds.x, card.metadata_bounds.y},
                 entry_label(entry.category, locale_) + "  " + entry.date,
                 label_color(entry.category),
                 std::max(9, static_cast<int>(std::lround(11.f * s))),
                 card.metadata_bounds.width, layout.list_viewport);
    if (entry.system_id != 0)
      clipped_text(
          out,
          {card.bounds.x + card.bounds.width - 16.f * s,
           card.metadata_bounds.y},
          "->", muted_color,
          std::max(9, static_cast<int>(std::lround(11.f * s))), 0.f,
          layout.list_viewport);
    if (card.contact_button &&
        intersects(*card.contact_button, layout.list_viewport)) {
      stellar::engine::ui_skin::control(
          out, *card.contact_button,
          card.contact_button->contains(pointer_), false, true, s);
      const auto dip_text =
          resolve(locale_, "CHRONICLE_CONTACT", "DIP");
      const int dip_pixels =
          std::max(9, static_cast<int>(std::lround(10.f * s)));
      const Text dip_probe{{}, dip_text, title_color, dip_pixels,
                           card.contact_button->width - 4.f * s,
                           std::nullopt, TextAlign::Center,
                           FontFace::Interface};
      const auto dip_extent = measure(measure_, dip_probe);
      clipped_text(
          out,
          {card.contact_button->x + card.contact_button->width * .5f,
           card.contact_button->y +
               (card.contact_button->height - dip_extent.height) * .5f},
          dip_text, title_color, dip_pixels,
          card.contact_button->width - 4.f * s, *card.contact_button,
          TextAlign::Center);
    }
    for (const auto &chip : card.tag_chips) {
      if (!intersects(chip.bounds, layout.list_viewport)) continue;
      const bool focused = chip.tag == tag_filter_;
      stellar::engine::ui_skin::control(
          out, chip.bounds, chip.bounds.contains(pointer_), focused,
          true, s);
      const std::string chip_text = upper(chip.tag);
      const int chip_pixels =
          std::max(9, static_cast<int>(std::lround(10.f * s)));
      const Text chip_probe{{}, chip_text, muted_color, chip_pixels,
                            chip.bounds.width - 4.f * s, std::nullopt,
                            TextAlign::Center, FontFace::Interface};
      const auto chip_extent = measure(measure_, chip_probe);
      clipped_text(
          out,
          {chip.bounds.x + chip.bounds.width * .5f,
           chip.bounds.y +
               (chip.bounds.height - chip_extent.height) * .5f},
          chip_text, muted_color, chip_pixels,
          chip.bounds.width - 4.f * s, chip.bounds, TextAlign::Center);
    }
    clipped_text(out, {card.message_bounds.x, card.message_bounds.y},
                 entry.summary, message_color,
                 std::max(11, static_cast<int>(std::lround(13.f * s))),
                 card.message_bounds.width, layout.list_viewport);
  }
  if (const auto thumb = layout.scroll.thumb(layout.list_viewport.height,
                                             16.f * s);
      thumb.size > 0.f) {
    fill(out,
         {layout.list_viewport.x + layout.list_viewport.width - 3.f * s,
          layout.list_viewport.y + thumb.offset, 2.f * s, thumb.size},
         muted_color);
  }
  if (focus_ >= 0) {
    const auto items = focusables(layout, snapshot_, locale_);
    if (focus_ < static_cast<int>(items.size()))
      out.overlay.emplace_back(StrokedRectangle{
          items[static_cast<std::size_t>(focus_)].bounds,
          {160, 210, 255, 255}});
  }
}

} // namespace stellar::native_chronicle
