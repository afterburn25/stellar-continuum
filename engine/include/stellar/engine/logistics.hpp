#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::engine {

// Strategic logistics — freight moving between settlements/stations
// over multi-leg routes with transit time and route capacity. The
// network is abstract: node ids are caller-owned waypoints (colonies,
// stations, depots); routes are explicit paths with per-leg transit
// days (pathfinding is the caller's — e.g. the Core lanes/reach
// planner). Cargo accounting at endpoints is the caller's: dispatch()
// assumes the origin already paid the cargo; advance() returns
// deliveries the owner deposits.
//
// Deterministic: dispatch queue and deliveries order by (time, id);
// expected-value transit — a shipment's eta is set at departure and
// does not drift. Caller-supplied ids throughout.

struct FreightRoute {
    std::uint64_t id{};
    std::vector<std::uint64_t> path;      // waypoint node ids, size >= 2
    std::vector<double> leg_days;         // size == path.size() - 1
    double capacity{0.0};                 // max in-flight quantity
    bool enabled{true};
    double in_flight{0.0};                // current committed tonnage
    double total_days{0.0};               // sum(leg_days)
};

struct Shipment {
    std::uint64_t id{};
    std::uint64_t route{0};
    std::string resource;
    double quantity{0.0};
    double departed{0.0};  // network clock (days)
    double eta{0.0};       // departed + route total_days
};

struct FreightDelivery {
    std::uint64_t node{0};       // route destination
    std::string resource;
    double quantity{0.0};
    std::uint64_t shipment{0};
};

struct LogisticsAdvance {
    double elapsed_days{0.0};
    std::vector<FreightDelivery> deliveries; // sorted by (eta, shipment id)
    std::vector<std::uint64_t> departed;     // shipment ids that left queue
};

class LogisticsNetwork {
public:
    // Nodes are pure waypoints — registered so routes validate.
    bool add_node(std::uint64_t id);
    bool remove_node(std::uint64_t id); // fails while a route references it
    [[nodiscard]] bool has_node(std::uint64_t id) const;
    [[nodiscard]] std::vector<std::uint64_t> node_ids() const;

    // path.size() >= 2, all nodes known, leg_days.size() == path-1,
    // every leg >= 0, capacity >= 0 (0 = unlimited).
    bool add_route(std::uint64_t id, std::vector<std::uint64_t> path,
                   std::vector<double> leg_days, double capacity);
    bool remove_route(std::uint64_t id); // cancels its queued shipments;
                                         // in-flight cargo is LOST
                                         // (report via in_flight list)
    bool set_route_enabled(std::uint64_t id, bool enabled);
    [[nodiscard]] const FreightRoute* route(std::uint64_t id) const;
    [[nodiscard]] std::vector<const FreightRoute*> routes() const;

    // Enqueue a shipment. The route must exist; queued shipments depart
    // in ascending shipment-id order as capacity frees. Disabling a
    // route stops new departures; in-flight shipments still arrive.
    bool dispatch(std::uint64_t shipment_id, std::uint64_t route_id,
                  std::string resource, double quantity);
    // Cancels a QUEUED shipment (in-flight shipments cannot be recalled).
    bool cancel(std::uint64_t shipment_id);
    // Finds a shipment in the queue or in transit.
    [[nodiscard]] const Shipment* shipment(std::uint64_t id) const;
    [[nodiscard]] bool is_queued(std::uint64_t id) const;
    [[nodiscard]] std::vector<const Shipment*> queued() const;    // by id
    [[nodiscard]] std::vector<const Shipment*> in_transit() const; // by id
    // 0..1 transit progress for an in-flight shipment.
    [[nodiscard]] std::optional<double> shipment_progress(std::uint64_t id) const;

    // Advance the network clock: depart queued shipments (stamped at
    // step start, so a dispatched shipment travels during this step and
    // a zero-length route completes within it), then complete arrivals.
    // Deterministic delivery order: eta then shipment id.
    LogisticsAdvance advance(double elapsed_days);

    [[nodiscard]] double now() const { return now_; }
    // Diagnostics: route id -> in-flight / capacity ratio (0 if no cap).
    [[nodiscard]] std::vector<std::pair<std::uint64_t, double>>
    route_utilization() const;

    // --- persistence -------------------------------------------------
    // Serializable logistics state: waypoint set, routes with in-flight
    // tonnage, queued and in-transit shipments, and the network clock.
    struct RouteState {
        std::uint64_t id{};
        std::vector<std::uint64_t> path;
        std::vector<double> leg_days;
        double capacity{0.0};
        bool enabled{true};
        double in_flight{0.0};
    };
    struct ShipmentState {
        std::uint64_t id{};
        std::uint64_t route{0};
        std::string resource;
        double quantity{0.0};
        double departed{0.0};
        double eta{0.0};
    };
    struct State {
        std::uint32_t version{1};
        double now{0.0};
        std::vector<std::uint64_t> nodes;      // sorted
        std::vector<RouteState> routes;        // sorted by id
        std::vector<ShipmentState> queued;     // sorted by id
        std::vector<ShipmentState> in_transit; // sorted by id
    };
    [[nodiscard]] State capture_state() const;
    // Replaces the whole network with the snapshot. Throws
    // invalid_argument on a route referencing an unknown node, a
    // malformed route (bad path/leg lengths), or a shipment referencing
    // an unknown route.
    void restore_state(const State& state);

private:
    std::unordered_map<std::uint64_t, bool> nodes_; // id set
    std::unordered_map<std::uint64_t, FreightRoute> routes_;
    std::unordered_map<std::uint64_t, Shipment> queue_;
    std::unordered_map<std::uint64_t, Shipment> transit_;
    double now_{0.0};
};

} // namespace stellar::engine
