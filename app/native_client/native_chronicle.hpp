#pragma once

// Scrollable chronicle browser — the player-facing surface over
// engine::EventHistory (the persisted strategic record). The bounded
// notification feed shows only recent reports; this view renders the
// full observer-filtered chronicle so history older than the transient
// window — and everything restored from a save — stays reachable.
//
// Privacy is the chronicle's own projection: the view snapshots
// EventHistory::feed(observer, -inf) and never re-derives visibility.

#include "native_notifications.hpp"

#include <stellar/engine/history.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_chronicle {

// Maps a history category ("construction.project", "war.battle", ...)
// onto the notification feed's display vocabulary so both surfaces use
// one label set. Returns nullptr for unmapped categories — callers
// fall back to the stable raw id rather than dropping the record.
[[nodiscard]] const char *category_label(std::string_view category) noexcept;

// Display-ready snapshot row. Strings are pre-formatted at projection
// time so the view never touches simulation state.
struct ChronicleEntry {
  std::uint64_t id{};
  std::uint64_t system_id{};  // event location; 0 = not located
  std::uint64_t contact_id{}; // single non-observer actor; 0 = none
  std::string category, date, summary;
  std::vector<std::string> tags; // recorded entity references
};

struct ChronicleSnapshot {
  std::vector<ChronicleEntry> entries; // newest first
  std::size_t total{};                 // visible count before the cap
};

// Presentation-side filter bundle over the observer's authorized
// feed. Every axis applies before the entry cap, so a filtered view
// still reaches deep history and `total` reports the filtered visible
// count. All axes compose; an empty/zero/-inf axis is inert.
struct ChronicleFilter {
  // Category prefix restricting to one domain ("war.", "exploration.").
  std::string_view category_prefix{};
  // The feed's own significance floor — trivia drops without the
  // presentation re-scoring anything.
  double min_significance{0.0};
  // 0 = all; otherwise only events listing this civilization in
  // `actors` — "all intel" (events merely witnessed via known
  // systems) vs "what a given empire actually did".
  std::uint64_t actor{0};
  // HistoryQuery::tag semantics — exact match on a recorded reference
  // tag ("fleet:12"): "everything this entity did that we can see".
  std::string_view tag{};
  // The feed's own recency bound — only events at or after this
  // campaign day appear.
  double since_day{-std::numeric_limits<double>::infinity()};
  // HistoryQuery::before_day — inclusive upper bound; combined with
  // since_day it forms a closed campaign-day window (time paging).
  std::optional<double> before_day{};
  // Case-insensitive substring over the recorded summary, category id
  // or reference tags — free-text lookup over the authorized feed
  // (typing "fleet:12" finds the same records the tag chip focuses).
  std::string_view search{};
};

// Projects the observer-filtered chronicle into display entries,
// newest first, capped at `max_entries` (the tail end of history —
// a chronicle can hold 100k records; the view shows the newest slice
// and reports the true total). Visibility is delegated entirely to
// `EventHistory::query`'s observer projection (the same one `feed()`
// wraps, plus the `before_day` axis) — never re-derived here.
[[nodiscard]] ChronicleSnapshot
snapshot(const engine::EventHistory &history, int observer_civilization_id,
         const ChronicleFilter &filter = {}, std::size_t max_entries = 4000);

class NativeChronicleView final {
public:
  void set_text_measurer(native_notifications::TextMeasurer measure) {
    measure_ = std::move(measure);
  }
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }

  // Materializes the observer's snapshot. `history` is borrowed for
  // later refreshes — callers close the view before the runtime dies.
  void open(const engine::EventHistory &history,
            int observer_civilization_id);
  void close() noexcept;
  void toggle(const engine::EventHistory &history,
              int observer_civilization_id) {
    if (visible_)
      close();
    else
      open(history, observer_civilization_id);
  }
  // Re-pulls the snapshot (history may have grown while open).
  void refresh();
  // Cycles the category-domain filter (All → construction → ships →
  // research → exploration → colony → combat → All) and re-pulls.
  void cycle_domain();
  [[nodiscard]] std::string_view domain_filter() const noexcept {
    return domain_filter_;
  }
  // Cycles the significance floor (0.0 → 0.3 → 0.5 → 0.7 → 0.0) —
  // the feed's own axis, so "major events only" drops trivia without
  // the presentation re-scoring anything.
  void cycle_significance();
  [[nodiscard]] double significance_floor() const noexcept {
    return significance_floor_;
  }
  // Cycles the actor filter (all intel → the observer → each other
  // civilization appearing in the visible feed, ascending) and
  // re-pulls. `actor_name_resolver` supplies display names for the
  // button label (falls back to "CIV <id>"; the observer shows MINE).
  void cycle_actor();
  [[nodiscard]] std::uint64_t actor_filter() const noexcept {
    return actor_filter_;
  }
  void set_actor_name_resolver(
      std::function<std::string(std::uint64_t)> resolver) {
    actor_name_resolver_ = std::move(resolver);
  }
  // Live campaign-day source for the recency filter — the day advances
  // while the browser stays open, so it is queried per refresh rather
  // than captured at open.
  void set_campaign_day_source(std::function<double()> source) {
    campaign_day_source_ = std::move(source);
  }
  // Cycles the recency window (all → last 30d → last year → last
  // decade → all) and re-pulls — the feed's own `since_day` axis, so a
  // bounded window still reaches deep history before the cap.
  void cycle_recency();
  [[nodiscard]] double recency_window() const noexcept {
    return recency_window_;
  }
  // Time paging: while a bounded recency window is active, shifts it
  // backward/forward by its own width — "the 30 days before these".
  // since_day + before_day together form the closed window; inert
  // when the window is ALL. Cycling the window resets to page 0.
  void page_older();
  void page_newer();
  [[nodiscard]] int window_page() const noexcept { return window_page_; }
  // Active entity-focus tag (set by clicking a card's tag chip, or
  // cleared via the focus button / re-clicking the focused chip).
  [[nodiscard]] std::string_view tag_filter() const noexcept {
    return tag_filter_;
  }
  // Free-text search over summary/category (header field; click to
  // focus, Escape or outside press unfocuses). While focused the view
  // wants SDL text input — callers gate global shortcuts on this.
  [[nodiscard]] std::string_view search() const noexcept {
    return search_;
  }
  [[nodiscard]] bool wants_text_input() const noexcept {
    return visible_ && search_focused_;
  }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] float scroll_offset() const noexcept { return scroll_; }
  [[nodiscard]] const ChronicleSnapshot &current() const noexcept {
    return snapshot_;
  }
  // Takes a pending system-navigation request (click on a located
  // entry), clearing it — the client drains it like the debug
  // background's navigation hook. Visibility is never re-derived:
  // feed() already restricted entries to the observer, and the system
  // workspace applies its own observation check on entry.
  [[nodiscard]] std::optional<std::uint64_t> navigation() noexcept {
    const auto pending = navigation_;
    navigation_.reset();
    return pending;
  }
  // Same drain contract for the diplomatic-contact action on cards
  // with exactly one foreign actor (first contacts, battles, treaties).
  [[nodiscard]] std::optional<std::uint64_t> contact_navigation() noexcept {
    const auto pending = contact_navigation_;
    contact_navigation_.reset();
    return pending;
  }

  // Returns true when the event was consumed by the view.
  [[nodiscard]] bool handle(const native_map::InputEvent &event, int width,
                            int height);
  void render(native_map::DrawList &out, int width, int height) const;

private:
  enum class PressTarget { None, Close, Refresh, Domain, Significance,
                           Actor, Time, Search, Entry, Contact, Tag,
                           FocusClear, PageOlder, PageNewer };
  void cancel_press() noexcept;

  bool visible_{};
  float scroll_{};
  const engine::EventHistory *history_{};
  int observer_{-1};
  std::string domain_filter_;
  double significance_floor_{};
  std::uint64_t actor_filter_{};
  std::string tag_filter_;
  double recency_window_{};
  int window_page_{};
  std::string search_;
  bool search_focused_{};
  std::size_t press_entry_{}, press_tag_{};
  std::optional<std::uint64_t> navigation_{}, contact_navigation_{};
  std::function<std::string(std::uint64_t)> actor_name_resolver_;
  std::function<double()> campaign_day_source_;
  ChronicleSnapshot snapshot_;
  native_map::Point pointer_{}, press_origin_{};
  bool pointer_captured_{};
  PressTarget press_target_{PressTarget::None};
  native_notifications::TextMeasurer measure_;
  const stellar::engine::LocalizationTable *locale_{};
};

} // namespace stellar::native_chronicle
