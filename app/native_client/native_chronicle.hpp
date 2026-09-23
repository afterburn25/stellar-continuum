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
  std::string category, date, summary;
};

struct ChronicleSnapshot {
  std::vector<ChronicleEntry> entries; // newest first
  std::size_t total{};                 // visible count before the cap
};

// Projects the observer-filtered chronicle into display entries,
// newest first, capped at `max_entries` (the tail end of history —
// a chronicle can hold 100k records; the view shows the newest slice
// and reports the true total). `category_prefix` restricts to one
// domain ("war.", "exploration.", ...) and `min_significance` is the
// feed's own floor — both apply before the cap so a filtered view
// still reaches deep history. `involved_only` further restricts to
// events that list the observer in `actors` — the difference between
// "all intel" (events merely visible via known systems) and "my
// empire's doings". `total` reports the filtered visible count.
[[nodiscard]] ChronicleSnapshot
snapshot(const engine::EventHistory &history, int observer_civilization_id,
         std::size_t max_entries = 4000,
         std::string_view category_prefix = {},
         double min_significance = 0.0, bool involved_only = false);

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
  // Toggles the actor scope (all visible intel ↔ events involving the
  // observer) and re-pulls.
  void toggle_scope();
  [[nodiscard]] bool involved_only() const noexcept {
    return involved_only_;
  }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] float scroll_offset() const noexcept { return scroll_; }
  [[nodiscard]] const ChronicleSnapshot &current() const noexcept {
    return snapshot_;
  }

  // Returns true when the event was consumed by the view.
  [[nodiscard]] bool handle(const native_map::InputEvent &event, int width,
                            int height);
  void render(native_map::DrawList &out, int width, int height) const;

private:
  enum class PressTarget { None, Close, Refresh, Domain, Significance,
                           Scope };
  void cancel_press() noexcept;

  bool visible_{};
  float scroll_{};
  const engine::EventHistory *history_{};
  int observer_{-1};
  std::string domain_filter_;
  double significance_floor_{};
  bool involved_only_{};
  ChronicleSnapshot snapshot_;
  native_map::Point pointer_{}, press_origin_{};
  bool pointer_captured_{};
  PressTarget press_target_{PressTarget::None};
  native_notifications::TextMeasurer measure_;
  const stellar::engine::LocalizationTable *locale_{};
};

} // namespace stellar::native_chronicle
