#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stellar::engine {

// Galaxy map — a reusable, game-agnostic model of a star chart: systems
// positioned in light-year space, lane links between systems, and markers
// (fleets, colonies, outposts, anomalies) that live at positions or attach
// to systems. It deliberately knows nothing about gameplay rules: ids and
// owner ids are opaque, classifications/tags/labels are caller strings,
// and nothing here simulates. Games fill the map from their authoritative
// state (Core does so through core/galaxy_projection.hpp) and consume the
// queries below for rendering, selection and debugger tools.
//
// All externally visible ordering is deterministic: id accessors return
// ascending ids and query results are sorted by id, independent of
// insertion or hash order. Lane adjacency is rebuilt lazily when topology
// mutates.

struct GalaxySystem {
    std::uint64_t id{0};
    std::string name;
    double x_light_years{0.0};
    double y_light_years{0.0};
    double depth_light_years{0.0};
    // Free-form classification label ("G yellow dwarf", "neutron star"),
    // empty when the game does not classify the system.
    std::string classification;
    // Caller-defined flags ("surveyed", "hostile", ...) — opaque to the map.
    std::vector<std::string> tags;
};

struct GalaxyLane {
    std::uint64_t id{0};
    std::uint64_t first_system_id{0};
    std::uint64_t second_system_id{0};
    double length_light_years{0.0};
    bool enabled{true};

    [[nodiscard]] bool connects(std::uint64_t system_id) const noexcept {
        return first_system_id == system_id || second_system_id == system_id;
    }
    [[nodiscard]] std::uint64_t other(std::uint64_t system_id) const {
        return first_system_id == system_id ? second_system_id
                                            : first_system_id;
    }
};

struct GalaxyMarker {
    enum class Kind { Fleet, Colony, Outpost, Anomaly, Custom };
    std::uint64_t id{0};
    Kind kind{Kind::Custom};
    std::string label;
    // Faction/civilization owner — opaque id, 0 = unowned/unknown.
    std::uint64_t owner_id{0};
    double x_light_years{0.0};
    double y_light_years{0.0};
    double depth_light_years{0.0};
    // When set, the marker is anchored to a system (the position should
    // match the system's); free-floating markers leave this empty.
    std::optional<std::uint64_t> system_id;
    // Optional travel target for moving markers (e.g. fleets under way).
    std::optional<std::uint64_t> destination_system_id;
};

class GalaxyMap {
public:
    // Topology mutators — mark the adjacency cache dirty. Ids are
    // caller-supplied (save identity); adds reject duplicates.
    bool add_system(GalaxySystem system);
    bool remove_system(std::uint64_t id); // drops incident lanes too
    bool add_lane(GalaxyLane lane);
    bool remove_lane(std::uint64_t id);
    bool set_lane_enabled(std::uint64_t id, bool enabled);

    // Markers do not dirty lane topology.
    bool add_marker(GalaxyMarker marker);
    bool remove_marker(std::uint64_t id);
    bool update_marker_position(std::uint64_t id, double x_light_years,
                                double y_light_years,
                                double depth_light_years = 0.0);
    bool set_marker_destination(std::uint64_t id,
                                std::optional<std::uint64_t> system_id);
    // Anchors/detaches a marker at a system (nullopt = free-floating).
    bool set_marker_system(std::uint64_t id,
                           std::optional<std::uint64_t> system_id);

    [[nodiscard]] const GalaxySystem *system(std::uint64_t id) const;
    [[nodiscard]] const GalaxyLane *lane(std::uint64_t id) const;
    [[nodiscard]] const GalaxyMarker *marker(std::uint64_t id) const;
    [[nodiscard]] std::vector<std::uint64_t> system_ids() const; // ascending
    [[nodiscard]] std::vector<std::uint64_t> lane_ids() const;   // ascending
    [[nodiscard]] std::vector<std::uint64_t> marker_ids() const; // ascending
    [[nodiscard]] std::size_t system_count() const { return systems_.size(); }
    [[nodiscard]] std::size_t lane_count() const { return lanes_.size(); }
    [[nodiscard]] std::size_t marker_count() const { return markers_.size(); }

    // Enabled-lane adjacency, rebuilt lazily after topology changes.
    // Each neighbor list is ascending; the outer vector is ordered by
    // system id.
    [[nodiscard]] bool topology_dirty() const { return topology_dirty_; }
    [[nodiscard]] std::vector<std::uint64_t>
    neighbors(std::uint64_t system_id) const;
    [[nodiscard]] std::vector<std::uint64_t>
    lanes_for(std::uint64_t system_id) const; // ascending lane ids

    // Spatial queries — results ascending by id.
    [[nodiscard]] std::vector<std::uint64_t>
    systems_in_radius(double x_light_years, double y_light_years,
                      double radius_light_years) const;
    // Nearest enabled-connected or any system to a point; nullopt when the
    // map is empty.
    [[nodiscard]] std::optional<std::uint64_t>
    nearest_system(double x_light_years, double y_light_years) const;
    [[nodiscard]] std::vector<std::uint64_t>
    markers_in_system(std::uint64_t system_id) const;
    [[nodiscard]] std::vector<std::uint64_t>
    markers_for_owner(std::uint64_t owner_id) const;

    [[nodiscard]] double
    distance_light_years(std::uint64_t first_system_id,
                         std::uint64_t second_system_id) const;

    // --- persistence -------------------------------------------------
    // Versioned snapshot: full topology plus markers — enough to rebuild
    // the map without re-running the mutator sequence. The adjacency
    // cache is derived, not persisted.
    struct State {
        std::uint32_t version{1};
        std::vector<GalaxySystem> systems;
        std::vector<GalaxyLane> lanes;
        std::vector<GalaxyMarker> markers;
    };
    [[nodiscard]] State capture_state() const;
    void restore_state(const State &state);

    void clear();

private:
    void rebuild_adjacency() const;

    std::unordered_map<std::uint64_t, GalaxySystem> systems_;
    std::unordered_map<std::uint64_t, GalaxyLane> lanes_;
    std::unordered_map<std::uint64_t, GalaxyMarker> markers_;
    mutable bool topology_dirty_{true};
    // system id -> ascending neighbor/lane ids over ENABLED lanes.
    mutable std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>
        adjacency_systems_, adjacency_lanes_;
};

} // namespace stellar::engine
