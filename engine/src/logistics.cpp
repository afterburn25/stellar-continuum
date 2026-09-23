#include <stellar/engine/logistics.hpp>

#include <algorithm>
#include <numeric>

namespace stellar::engine {

namespace {

template <typename T>
std::vector<const T*> sorted_ptrs(
    const std::unordered_map<std::uint64_t, T>& m) {
    std::vector<const T*> out;
    out.reserve(m.size());
    for (const auto& [k, v] : m) out.push_back(&v);
    std::sort(out.begin(), out.end(),
              [](const T* a, const T* b) { return a->id < b->id; });
    return out;
}

} // namespace

bool LogisticsNetwork::add_node(std::uint64_t id) {
    return nodes_.emplace(id, true).second;
}

bool LogisticsNetwork::remove_node(std::uint64_t id) {
    if (!nodes_.count(id)) return false;
    for (const auto& [rid, r] : routes_) {
        if (std::find(r.path.begin(), r.path.end(), id) != r.path.end()) {
            return false; // referenced by a route
        }
    }
    nodes_.erase(id);
    return true;
}

bool LogisticsNetwork::has_node(std::uint64_t id) const {
    return nodes_.count(id) != 0;
}

std::vector<std::uint64_t> LogisticsNetwork::node_ids() const {
    std::vector<std::uint64_t> ids;
    ids.reserve(nodes_.size());
    for (const auto& [id, _] : nodes_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool LogisticsNetwork::add_route(std::uint64_t id,
                                 std::vector<std::uint64_t> path,
                                 std::vector<double> leg_days,
                                 double capacity) {
    if (routes_.count(id) || path.size() < 2 ||
        leg_days.size() != path.size() - 1 || capacity < 0.0) {
        return false;
    }
    for (const auto n : path) {
        if (!nodes_.count(n)) return false;
    }
    double total = 0.0;
    for (const double d : leg_days) {
        if (d < 0.0) return false;
        total += d;
    }
    FreightRoute r;
    r.id = id;
    r.path = std::move(path);
    r.leg_days = std::move(leg_days);
    r.capacity = capacity;
    r.total_days = total;
    routes_.emplace(id, std::move(r));
    return true;
}

bool LogisticsNetwork::remove_route(std::uint64_t id) {
    auto it = routes_.find(id);
    if (it == routes_.end()) return false;
    // Drop queued shipments on the route; in-flight cargo is lost with
    // the route (callers inspect in_transit() first if they care).
    for (auto q = queue_.begin(); q != queue_.end();) {
        if (q->second.route == id) {
            q = queue_.erase(q);
        } else {
            ++q;
        }
    }
    for (auto t = transit_.begin(); t != transit_.end();) {
        if (t->second.route == id) {
            t = transit_.erase(t);
        } else {
            ++t;
        }
    }
    routes_.erase(it);
    return true;
}

bool LogisticsNetwork::set_route_enabled(std::uint64_t id, bool enabled) {
    auto it = routes_.find(id);
    if (it == routes_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

const FreightRoute* LogisticsNetwork::route(std::uint64_t id) const {
    auto it = routes_.find(id);
    return it == routes_.end() ? nullptr : &it->second;
}

std::vector<const FreightRoute*> LogisticsNetwork::routes() const {
    return sorted_ptrs(routes_);
}

bool LogisticsNetwork::dispatch(std::uint64_t shipment_id,
                                std::uint64_t route_id,
                                std::string resource, double quantity) {
    if (queue_.count(shipment_id) || transit_.count(shipment_id) ||
        quantity <= 0.0 || resource.empty()) {
        return false;
    }
    if (!routes_.count(route_id)) return false;
    Shipment s;
    s.id = shipment_id;
    s.route = route_id;
    s.resource = std::move(resource);
    s.quantity = quantity;
    s.departed = -1.0;
    queue_.emplace(s.id, std::move(s));
    return true;
}

bool LogisticsNetwork::cancel(std::uint64_t shipment_id) {
    return queue_.erase(shipment_id) != 0;
}

const Shipment* LogisticsNetwork::shipment(std::uint64_t id) const {
    if (auto it = queue_.find(id); it != queue_.end()) return &it->second;
    if (auto it = transit_.find(id); it != transit_.end()) return &it->second;
    return nullptr;
}

bool LogisticsNetwork::is_queued(std::uint64_t id) const {
    return queue_.count(id) != 0;
}

std::vector<const Shipment*> LogisticsNetwork::queued() const {
    return sorted_ptrs(queue_);
}

std::vector<const Shipment*> LogisticsNetwork::in_transit() const {
    return sorted_ptrs(transit_);
}

std::optional<double>
LogisticsNetwork::shipment_progress(std::uint64_t id) const {
    const auto it = transit_.find(id);
    if (it == transit_.end()) return std::nullopt;
    const auto* r = route(it->second.route);
    if (!r || r->total_days <= 0.0) return 1.0;
    return std::clamp((now_ - it->second.departed) / r->total_days, 0.0, 1.0);
}

LogisticsAdvance LogisticsNetwork::advance(double elapsed_days) {
    LogisticsAdvance result;
    result.elapsed_days = elapsed_days;
    if (elapsed_days <= 0.0) return result;

    // Departures first (ascending shipment id), stamped at the START of
    // the step — a dispatched shipment travels during this advance, and
    // a zero-length route completes within it.
    for (const auto* qs : queued()) {
        const auto rid = qs->route;
        auto& r = routes_[rid];
        if (!r.enabled) continue;
        if (r.capacity > 0.0 && r.in_flight + qs->quantity > r.capacity) {
            continue; // route full — stays queued
        }
        Shipment s = *qs;
        s.departed = now_;
        s.eta = now_ + r.total_days;
        r.in_flight += s.quantity;
        queue_.erase(s.id);
        transit_.emplace(s.id, s);
        result.departed.push_back(s.id);
    }

    now_ += elapsed_days;

    // Arrivals: eta <= now_, delivered in (eta, id) order.
    std::vector<const Shipment*> arrived;
    for (const auto* s : in_transit()) {
        if (s->eta <= now_) arrived.push_back(s);
    }
    std::sort(arrived.begin(), arrived.end(), [](const Shipment* a,
                                                 const Shipment* b) {
        if (a->eta != b->eta) return a->eta < b->eta;
        return a->id < b->id;
    });
    for (const auto* s : arrived) {
        auto& r = routes_[s->route];
        r.in_flight -= s->quantity;
        FreightDelivery d;
        d.node = r.path.back();
        d.resource = s->resource;
        d.quantity = s->quantity;
        d.shipment = s->id;
        result.deliveries.push_back(std::move(d));
        transit_.erase(s->id);
    }
    return result;
}

std::vector<std::pair<std::uint64_t, double>>
LogisticsNetwork::route_utilization() const {
    std::vector<std::pair<std::uint64_t, double>> out;
    for (const auto* r : routes()) {
        const double u =
            r->capacity > 0.0 ? r->in_flight / r->capacity : 0.0;
        out.emplace_back(r->id, u);
    }
    return out;
}

} // namespace stellar::engine
