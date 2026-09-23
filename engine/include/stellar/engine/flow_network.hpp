#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Infrastructure flow network — ONE resource's distribution graph
// (power grid, water mains, data backbone, freight web). Games compose
// several networks; a settlement's utility pool (colony.hpp) is the
// degenerate single-component case.
//
// Nodes supply, demand and optionally store the resource. Directed
// edges carry capacity per day (bidirectional links are two edges).
// advance() is a deterministic three-pass transport: local
// serve/storage, then greedy edge rebalancing in ascending edge-id
// order, then surplus storage absorption. It is NOT optimal max-flow —
// it is cheap, stable and reproducible; diagnostics report the
// resulting unmet demand and per-edge utilization so owners can act on
// congestion rather than hide it.
//
// Topology changes (add/remove node or edge, enable/disable) mark the
// component cache dirty; components() rebuilds lazily on next query.

struct FlowNodeState {
    double supply_per_day{0.0};
    double demand_per_day{0.0};
    double storage_capacity{0.0};
    double storage{0.0};
    bool enabled{true};
    // Last advance() accounting (elapsed-quantities, not rates):
    double last_demand{0.0};
    double last_served{0.0};
    double last_unmet{0.0};
    double last_exported{0.0};
    double last_imported{0.0};
};

struct FlowEdgeState {
    std::uint64_t from{0};
    std::uint64_t to{0};
    double capacity_per_day{0.0};
    bool enabled{true};
    double last_flow{0.0}; // quantity moved by last advance()
};

struct FlowAdvanceResult {
    double elapsed_days{0.0};
    double total_supply{0.0};
    double total_demand{0.0};
    double total_served{0.0};
    double total_unmet{0.0};
    double storage_released{0.0};
    double storage_absorbed{0.0};
    // Nonzero entries only, ascending id.
    std::vector<std::pair<std::uint64_t, double>> unmet_by_node;
    std::vector<std::pair<std::uint64_t, double>> flow_by_edge;
};

class FlowNetwork {
public:
    explicit FlowNetwork(std::string resource_id);
    [[nodiscard]] const std::string& resource() const { return resource_; }

    // Topology mutators — mark the component cache dirty. Node ids and
    // edge ids are caller-supplied (save identity). add_edge rejects
    // duplicate ids and duplicate (from,to) direction pairs.
    bool add_node(std::uint64_t id, double supply_per_day,
                  double demand_per_day, double storage_capacity = 0.0);
    bool remove_node(std::uint64_t id); // drops incident edges too
    bool add_edge(std::uint64_t id, std::uint64_t from, std::uint64_t to,
                  double capacity_per_day);
    bool remove_edge(std::uint64_t id);
    bool set_node_enabled(std::uint64_t id, bool enabled);
    bool set_edge_enabled(std::uint64_t id, bool enabled);

    // Rate/storage updates do NOT dirty topology.
    bool set_node_rates(std::uint64_t id, double supply_per_day,
                        double demand_per_day);
    // Returns amount actually deposited/withdrawn (<= request).
    double deposit_storage(std::uint64_t id, double quantity);
    double withdraw_storage(std::uint64_t id, double quantity);

    [[nodiscard]] const FlowNodeState* node(std::uint64_t id) const;
    [[nodiscard]] const FlowEdgeState* edge(std::uint64_t id) const;
    [[nodiscard]] std::vector<std::uint64_t> node_ids() const; // ascending
    [[nodiscard]] std::vector<std::uint64_t> edge_ids() const; // ascending
    [[nodiscard]] std::size_t node_count() const { return nodes_.size(); }
    [[nodiscard]] std::size_t edge_count() const { return edges_.size(); }

    // Connected components over enabled nodes+edges. Rebuilt lazily;
    // each component's node ids are ascending, components ordered by
    // their smallest id. Singletons (isolated nodes) included.
    [[nodiscard]] bool topology_dirty() const { return topology_dirty_; }
    [[nodiscard]] const std::vector<std::vector<std::uint64_t>>& components();

    // Deterministic transport step. Demand/supply rates integrate over
    // elapsed_days. See header comment for the pass order.
    FlowAdvanceResult advance(double elapsed_days);

    // Convenience: 0..1 served fraction for a node over its last demand.
    [[nodiscard]] double served_fraction(std::uint64_t id) const;

private:
    void rebuild_components();

    std::string resource_;
    std::unordered_map<std::uint64_t, FlowNodeState> nodes_;
    std::unordered_map<std::uint64_t, FlowEdgeState> edges_;
    std::vector<std::vector<std::uint64_t>> components_;
    bool topology_dirty_{true};
};

} // namespace stellar::engine
