#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {

// Event history — the strategic chronicle. Systems record significant
// happenings (colony founded, battle resolved, discovery made,
// terraforming stage completed); the history keeps them queryable by
// time, category, actor and significance, and filters them per
// observer for fog-of-war-correct chronicles and news feeds.
//
// This is the record layer, not the dispatch layer: transient
// notification delivery is EventBus; missions are MissionRuntime. The
// history is authoritative data — it serializes, and observer filtering
// is a query-time projection (the underlying record keeps full truth).
//
// Deterministic: append-only ordered storage, ids assigned by a
// monotonic counter, queries return stable (at_day, id) order.

struct HistoryEvent {
    std::uint64_t id{0};             // assigned by record()
    double at_day{0.0};              // campaign clock
    std::string category;            // "colony.founded", "war.battle", ...
    std::string summary;             // display text or localization key
    std::vector<std::uint64_t> actors;    // faction/entity ids involved
    std::uint64_t location{0};            // optional body/node id, 0 = none
    double significance{0.5};        // 0..1 — major-event thresholding
    // Observer privacy: EMPTY = public knowledge; otherwise only these
    // faction ids may see the event in filtered queries.
    std::vector<std::uint64_t> visible_to;
    std::vector<std::string> tags;   // free-form query tags
};

struct HistoryQuery {
    std::string_view category{};     // "" = all
    std::string_view tag{};          // "" = all
    std::uint64_t actor{0};          // 0 = all; matches actors list
    std::optional<double> after_day;
    std::optional<double> before_day;
    double min_significance{0.0};
    // Observer filter: std::nullopt = omniscient (developer/full
    // truth); a faction id returns only events public or visible to it.
    std::optional<std::uint64_t> observer;
    std::size_t limit{0};            // 0 = all; counts from most recent
};

class EventHistory {
public:
    explicit EventHistory(std::size_t capacity = 100000)
        : capacity_(capacity) {}

    // Assigns the id, appends, returns it. `at_day` is caller-supplied
    // (the campaign clock) — out-of-order days are allowed and kept in
    // record order; queries sort by (at_day, id).
    std::uint64_t record(HistoryEvent event);

    [[nodiscard]] const HistoryEvent* event(std::uint64_t id) const;
    [[nodiscard]] std::vector<const HistoryEvent*> query(
        const HistoryQuery& q) const;
    [[nodiscard]] std::size_t size() const { return events_.size(); }
    [[nodiscard]] std::uint64_t next_id() const { return next_id_; }

    // Observer-filtered significance feed — the M15 news substrate:
    // events at/after `since_day`, at or above `min_significance`,
    // visible to `observer` (nullopt = omniscient).
    [[nodiscard]] std::vector<const HistoryEvent*> feed(
        std::optional<std::uint64_t> observer, double since_day,
        double min_significance = 0.0) const;

    // Explicit pruning (the caller decides retention policy).
    std::size_t prune_before(double day, double keep_significance = 1.0);
    void clear();

    // --- persistence -------------------------------------------------
    // Serializable chronicle: every retained HistoryEvent plus the id
    // counter. Capacity is constructor policy, not state.
    struct State {
        std::uint32_t version{1};
        std::uint64_t next_id{1};
        std::deque<HistoryEvent> events; // record order (id ascending)
    };
    [[nodiscard]] State capture_state() const;
    // Replaces all records. Throws invalid_argument if event ids are not
    // strictly ascending (record order and binary lookup depend on it)
    // or next_id does not exceed every retained id.
    void restore_state(const State& state);

private:
    std::deque<HistoryEvent> events_;   // record order (id ascending)
    std::size_t capacity_;
    std::uint64_t next_id_{1};
};

} // namespace stellar::engine
