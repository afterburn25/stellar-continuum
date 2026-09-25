#include <stellar/engine/flow_network.hpp>

#include <algorithm>
#include <numeric>
#include <set>
#include <stdexcept>

namespace stellar::engine {

namespace {

template <typename T>
std::vector<std::uint64_t> sorted_keys(
    const std::unordered_map<std::uint64_t, T>& m) {
    std::vector<std::uint64_t> keys;
    keys.reserve(m.size());
    for (const auto& [k, v] : m) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace

FlowNetwork::FlowNetwork(std::string resource_id)
    : resource_(std::move(resource_id)) {
    if (resource_.empty()) {
        throw std::invalid_argument("flow network resource id empty");
    }
}

bool FlowNetwork::add_node(std::uint64_t id, double supply_per_day,
                           double demand_per_day, double storage_capacity) {
    if (nodes_.count(id)) return false;
    FlowNodeState n;
    n.supply_per_day = supply_per_day;
    n.demand_per_day = demand_per_day;
    n.storage_capacity = storage_capacity;
    nodes_.emplace(id, n);
    topology_dirty_ = true;
    return true;
}

bool FlowNetwork::remove_node(std::uint64_t id) {
    if (!nodes_.erase(id)) return false;
    for (auto it = edges_.begin(); it != edges_.end();) {
        if (it->second.from == id || it->second.to == id) {
            it = edges_.erase(it);
        } else {
            ++it;
        }
    }
    topology_dirty_ = true;
    return true;
}

bool FlowNetwork::add_edge(std::uint64_t id, std::uint64_t from,
                           std::uint64_t to, double capacity_per_day) {
    if (edges_.count(id) || !nodes_.count(from) || !nodes_.count(to) ||
        from == to || capacity_per_day < 0.0) {
        return false;
    }
    for (const auto& [eid, e] : edges_) {
        if (e.from == from && e.to == to) return false; // duplicate direction
    }
    FlowEdgeState e;
    e.from = from;
    e.to = to;
    e.capacity_per_day = capacity_per_day;
    edges_.emplace(id, e);
    topology_dirty_ = true;
    return true;
}

bool FlowNetwork::remove_edge(std::uint64_t id) {
    if (!edges_.erase(id)) return false;
    topology_dirty_ = true;
    return true;
}

bool FlowNetwork::set_node_enabled(std::uint64_t id, bool enabled) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    if (it->second.enabled != enabled) {
        it->second.enabled = enabled;
        topology_dirty_ = true;
    }
    return true;
}

bool FlowNetwork::set_edge_enabled(std::uint64_t id, bool enabled) {
    auto it = edges_.find(id);
    if (it == edges_.end()) return false;
    if (it->second.enabled != enabled) {
        it->second.enabled = enabled;
        topology_dirty_ = true;
    }
    return true;
}

bool FlowNetwork::set_node_rates(std::uint64_t id, double supply_per_day,
                                 double demand_per_day) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    it->second.supply_per_day = supply_per_day;
    it->second.demand_per_day = demand_per_day;
    return true;
}

double FlowNetwork::deposit_storage(std::uint64_t id, double quantity) {
    auto it = nodes_.find(id);
    if (it == nodes_.end() || quantity <= 0.0) return 0.0;
    const double room = it->second.storage_capacity - it->second.storage;
    const double put = std::min(room, quantity);
    it->second.storage += put;
    return put;
}

double FlowNetwork::withdraw_storage(std::uint64_t id, double quantity) {
    auto it = nodes_.find(id);
    if (it == nodes_.end() || quantity <= 0.0) return 0.0;
    const double took = std::min(it->second.storage, quantity);
    it->second.storage -= took;
    return took;
}

const FlowNodeState* FlowNetwork::node(std::uint64_t id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const FlowEdgeState* FlowNetwork::edge(std::uint64_t id) const {
    auto it = edges_.find(id);
    return it == edges_.end() ? nullptr : &it->second;
}

std::vector<std::uint64_t> FlowNetwork::node_ids() const {
    return sorted_keys(nodes_);
}

std::vector<std::uint64_t> FlowNetwork::edge_ids() const {
    return sorted_keys(edges_);
}

const std::vector<std::vector<std::uint64_t>>& FlowNetwork::components() {
    if (topology_dirty_) rebuild_components();
    return components_;
}

void FlowNetwork::rebuild_components() {
    components_.clear();
    const auto ids = node_ids();
    if (ids.empty()) {
        topology_dirty_ = false;
        return;
    }
    // Union-find over enabled nodes + enabled edges.
    std::unordered_map<std::uint64_t, std::uint64_t> parent;
    parent.reserve(ids.size());
    for (const auto id : ids) {
        if (nodes_[id].enabled) parent[id] = id;
    }
    const auto find = [&parent](std::uint64_t x) {
        std::uint64_t r = x;
        while (parent[r] != r) r = parent[r];
        while (parent[x] != r) {
            const std::uint64_t next = parent[x];
            parent[x] = r;
            x = next;
        }
        return r;
    };
    for (const auto eid : edge_ids()) {
        const auto& e = edges_[eid];
        if (!e.enabled) continue;
        if (!parent.count(e.from) || !parent.count(e.to)) continue;
        const std::uint64_t ra = find(e.from), rb = find(e.to);
        if (ra != rb) {
            // Deterministic merge: smaller root id wins.
            if (ra < rb) {
                parent[rb] = ra;
            } else {
                parent[ra] = rb;
            }
        }
    }
    std::unordered_map<std::uint64_t, std::size_t> comp_index;
    for (const auto id : ids) {
        if (!parent.count(id)) continue;
        const std::uint64_t root = find(id);
        auto [it, fresh] =
            comp_index.emplace(root, components_.size());
        if (fresh) components_.push_back({});
        components_[it->second].push_back(id);
    }
    // Node ids arrive ascending; components ordered by smallest member.
    std::sort(components_.begin(), components_.end(),
              [](const auto& a, const auto& b) { return a.front() < b.front(); });
    topology_dirty_ = false;
}

FlowAdvanceResult FlowNetwork::advance(double elapsed_days) {
    FlowAdvanceResult result;
    result.elapsed_days = elapsed_days;
    const auto ids = node_ids();
    if (elapsed_days <= 0.0 || ids.empty()) return result;

    std::unordered_map<std::uint64_t, double> export_pool, import_need;

    // Pass A — local serve, then own storage release into deficit.
    for (const auto id : ids) {
        auto& n = nodes_[id];
        n.last_demand = n.last_served = n.last_unmet = 0.0;
        n.last_exported = n.last_imported = 0.0;
        if (!n.enabled) continue;

        const double demand = n.demand_per_day * elapsed_days;
        const double supply = n.supply_per_day * elapsed_days;
        const double served = std::min(demand, supply);
        double deficit = demand - served;
        double surplus = supply - served;

        if (deficit > 0.0 && n.storage > 0.0) {
            const double release = std::min(n.storage, deficit);
            n.storage -= release;
            deficit -= release;
            result.storage_released += release;
        }
        n.last_demand = demand;
        n.last_served = demand - deficit; // own supply + own storage release
        n.last_unmet = deficit;
        result.total_supply += supply;
        result.total_demand += demand;
        result.total_served += n.last_served;
        if (deficit > 0.0) import_need[id] = deficit;
        if (surplus > 0.0) export_pool[id] = surplus;
    }

    // Pass B — greedy edge rebalancing, ascending edge id.
    for (const auto eid : edge_ids()) {
        auto& e = edges_[eid];
        e.last_flow = 0.0;
        if (!e.enabled) continue;
        auto fi = export_pool.find(e.from);
        if (fi == export_pool.end() || fi->second <= 0.0) continue;
        auto ti = import_need.find(e.to);
        if (ti == import_need.end() || ti->second <= 0.0) continue;
        const double cap = e.capacity_per_day * elapsed_days;
        const double qty = std::min({cap, fi->second, ti->second});
        if (qty <= 0.0) continue;
        fi->second -= qty;
        ti->second -= qty;
        e.last_flow = qty;
        nodes_[e.from].last_exported += qty;
        nodes_[e.to].last_imported += qty;
        nodes_[e.to].last_served += qty;
        nodes_[e.to].last_unmet -= qty;
        result.total_served += qty;
    }

    // Pass C — absorb leftover surplus into storage; report unmet.
    for (const auto id : ids) {
        auto& n = nodes_[id];
        if (!n.enabled) continue;
        auto ei = export_pool.find(id);
        if (ei != export_pool.end() && ei->second > 0.0) {
            const double room = n.storage_capacity - n.storage;
            const double put = std::min(room, ei->second);
            n.storage += put;
            ei->second -= put;
            result.storage_absorbed += put;
        }
        if (n.last_unmet > 0.0) {
            result.unmet_by_node.emplace_back(id, n.last_unmet);
        }
    }
    result.total_unmet =
        result.total_demand - result.total_served;
    for (const auto eid : edge_ids()) {
        if (edges_[eid].last_flow > 0.0) {
            result.flow_by_edge.emplace_back(eid, edges_[eid].last_flow);
        }
    }
    return result;
}

double FlowNetwork::served_fraction(std::uint64_t id) const {
    const auto* n = node(id);
    if (!n || n->last_demand <= 0.0) return 1.0;
    return n->last_served / n->last_demand;
}

FlowNetwork::State FlowNetwork::capture_state() const {
    State state;
    state.resource = resource_;
    for (const std::uint64_t id : node_ids()) {
        const FlowNodeState& n = nodes_.at(id);
        state.nodes.push_back({id, n.supply_per_day, n.demand_per_day,
                               n.storage_capacity, n.storage, n.enabled});
    }
    for (const std::uint64_t id : edge_ids()) {
        const FlowEdgeState& e = edges_.at(id);
        state.edges.push_back({id, e.from, e.to, e.capacity_per_day,
                               e.enabled});
    }
    return state;
}

void FlowNetwork::restore_state(const State& state) {
    if (state.resource != resource_)
        throw std::invalid_argument(
            "FlowNetwork snapshot resource mismatch");
    nodes_.clear();
    edges_.clear();
    components_.clear();
    for (const NodeState& n : state.nodes)
        nodes_[n.id] = {n.supply_per_day,
                        n.demand_per_day,
                        n.storage_capacity,
                        n.storage,
                        n.enabled,
                        0.0, 0.0, 0.0, 0.0, 0.0};
    std::set<std::pair<std::uint64_t, std::uint64_t>> seen_pairs;
    for (const EdgeState& e : state.edges) {
        if (!nodes_.count(e.from) || !nodes_.count(e.to))
            throw std::invalid_argument(
                "FlowNetwork snapshot edge references missing node");
        if (!seen_pairs.emplace(e.from, e.to).second)
            throw std::invalid_argument(
                "FlowNetwork snapshot duplicate directed edge pair");
        edges_[e.id] = {e.from, e.to, e.capacity_per_day, e.enabled, 0.0};
    }
    topology_dirty_ = true;
}

} // namespace stellar::engine
