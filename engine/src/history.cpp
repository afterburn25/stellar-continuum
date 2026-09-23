#include <stellar/engine/history.hpp>

#include <algorithm>

namespace stellar::engine {

namespace {

bool visible_to_observer(const HistoryEvent& e,
                         const std::optional<std::uint64_t>& observer) {
    if (!observer) return true;           // omniscient view
    if (e.visible_to.empty()) return true; // public knowledge
    return std::find(e.visible_to.begin(), e.visible_to.end(), *observer) !=
           e.visible_to.end();
}

bool matches(const HistoryEvent& e, const HistoryQuery& q) {
    if (!q.category.empty() && e.category != q.category) return false;
    if (!q.tag.empty() &&
        std::find(e.tags.begin(), e.tags.end(), q.tag) == e.tags.end()) {
        return false;
    }
    if (q.actor != 0 &&
        std::find(e.actors.begin(), e.actors.end(), q.actor) ==
            e.actors.end()) {
        return false;
    }
    if (q.after_day && e.at_day < *q.after_day) return false;
    if (q.before_day && e.at_day > *q.before_day) return false;
    if (e.significance < q.min_significance) return false;
    return visible_to_observer(e, q.observer);
}

bool by_time(const HistoryEvent* a, const HistoryEvent* b) {
    if (a->at_day != b->at_day) return a->at_day < b->at_day;
    return a->id < b->id;
}

} // namespace

std::uint64_t EventHistory::record(HistoryEvent event) {
    event.id = next_id_++;
    if (events_.size() >= capacity_) {
        events_.pop_front(); // bounded: drop oldest record
    }
    events_.push_back(std::move(event));
    return events_.back().id;
}

const HistoryEvent* EventHistory::event(std::uint64_t id) const {
    // Ids are monotonic and storage stays ordered under both
    // capacity-pop and prune_before — binary search.
    const auto it =
        std::lower_bound(events_.begin(), events_.end(), id,
                         [](const HistoryEvent& e, std::uint64_t v) {
                             return e.id < v;
                         });
    return (it != events_.end() && it->id == id) ? &*it : nullptr;
}

std::vector<const HistoryEvent*>
EventHistory::query(const HistoryQuery& q) const {
    std::vector<const HistoryEvent*> out;
    for (const auto& e : events_) {
        if (matches(e, q)) out.push_back(&e);
    }
    std::sort(out.begin(), out.end(), by_time);
    if (q.limit > 0 && out.size() > q.limit) {
        // Most recent `limit` events, still time-ordered.
        out.erase(out.begin(), out.end() - q.limit);
    }
    return out;
}

std::vector<const HistoryEvent*>
EventHistory::feed(std::optional<std::uint64_t> observer, double since_day,
                   double min_significance) const {
    HistoryQuery q;
    q.observer = observer;
    q.after_day = since_day;
    q.min_significance = min_significance;
    return query(q);
}

std::size_t EventHistory::prune_before(double day, double keep_significance) {
    const auto before = events_.size();
    events_.erase(std::remove_if(events_.begin(), events_.end(),
                                 [day, keep_significance](
                                     const HistoryEvent& e) {
                                     return e.at_day < day &&
                                            e.significance < keep_significance;
                                 }),
                  events_.end());
    return before - events_.size();
}

void EventHistory::clear() {
    events_.clear();
    next_id_ = 1;
}

} // namespace stellar::engine
