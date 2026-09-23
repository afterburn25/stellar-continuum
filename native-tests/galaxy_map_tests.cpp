#include <stellar/engine/galaxy_map.hpp>

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

stellar::engine::GalaxySystem sys(std::uint64_t id, double x, double y,
                                  std::string name = {}) {
    stellar::engine::GalaxySystem s;
    s.id = id;
    s.name = std::move(name);
    s.x_light_years = x;
    s.y_light_years = y;
    return s;
}

stellar::engine::GalaxyLane lane(std::uint64_t id, std::uint64_t a,
                                 std::uint64_t b, double length = 1.0) {
    return stellar::engine::GalaxyLane{id, a, b, length, true};
}

using namespace stellar::engine;

} // namespace

int main() {
    // --- Topology and deterministic ordering --------------------------------
    {
        GalaxyMap map;
        check(map.add_system(sys(3, 9.0, 0.0)), "add system 3");
        check(map.add_system(sys(1, 0.0, 0.0)), "add system 1");
        check(map.add_system(sys(2, 4.0, 0.0)), "add system 2");
        check(!map.add_system(sys(2, 5.0, 0.0)), "duplicate system rejected");
        check((map.system_ids() == std::vector<std::uint64_t>{1, 2, 3}),
              "system ids ascending");

        check(!map.add_lane(lane(9, 1, 4)), "lane to unknown system rejected");
        check(!map.add_lane(lane(9, 1, 1)), "self-lane rejected");
        check(map.add_lane(lane(2, 2, 3)), "add lane 2");
        check(map.add_lane(lane(1, 1, 2)), "add lane 1");
        check(!map.add_lane(lane(1, 1, 3)), "duplicate lane id rejected");
        check((map.lane_ids() == std::vector<std::uint64_t>{1, 2}),
              "lane ids ascending");

        check((map.neighbors(1) == std::vector<std::uint64_t>{2}),
              "system 1 neighbors");
        check((map.neighbors(2) == std::vector<std::uint64_t>{1, 3}),
              "system 2 neighbors sorted");
        check((map.lanes_for(2) == std::vector<std::uint64_t>{1, 2}),
              "system 2 lanes sorted");
        check(!map.topology_dirty(), "adjacency rebuilt lazily");

        check(map.set_lane_enabled(1, false), "disable lane 1");
        check(map.topology_dirty(), "disable marks dirty");
        check(map.neighbors(1).empty(), "disabled lane drops adjacency");
        check((map.neighbors(2) == std::vector<std::uint64_t>{3}),
              "remaining adjacency");

        check(map.remove_system(3), "remove system 3");
        check(map.lane_count() == 1 && !map.lane(2),
              "incident lane removed, unrelated lane kept");
        check(!map.remove_system(3), "re-remove fails");
    }

    // --- Spatial queries -----------------------------------------------------
    {
        GalaxyMap map;
        map.add_system(sys(10, 0.0, 0.0));
        map.add_system(sys(11, 3.0, 4.0)); // 5 ly from origin
        map.add_system(sys(12, 30.0, 40.0));
        check((map.systems_in_radius(0, 0, 6.0) ==
               std::vector<std::uint64_t>{10, 11}),
              "radius query");
        check(map.nearest_system(2.9, 3.9).value_or(0) == 11,
              "nearest system");
        check(!GalaxyMap{}.nearest_system(0, 0).has_value(),
              "empty map nearest nullopt");
        check(near(map.distance_light_years(10, 11), 5.0),
              "euclidean distance");
        bool threw = false;
        try {
            (void)map.distance_light_years(10, 99);
        } catch (const std::out_of_range &) {
            threw = true;
        }
        check(threw, "unknown-system distance throws");
    }

    // --- Markers --------------------------------------------------------------
    {
        GalaxyMap map;
        map.add_system(sys(1, 0.0, 0.0));
        map.add_system(sys(2, 8.0, 0.0));
        GalaxyMarker fleet;
        fleet.id = 100;
        fleet.kind = GalaxyMarker::Kind::Fleet;
        fleet.owner_id = 7;
        fleet.system_id = 1;
        fleet.destination_system_id = 2;
        GalaxyMarker colony;
        colony.id = 101;
        colony.kind = GalaxyMarker::Kind::Colony;
        colony.owner_id = 7;
        colony.system_id = 2;
        GalaxyMarker other;
        other.id = 102;
        other.kind = GalaxyMarker::Kind::Fleet;
        other.owner_id = 9;
        other.system_id = 1;
        check(map.add_marker(fleet) && map.add_marker(colony) &&
                  map.add_marker(other),
              "add markers");
        check(!map.add_marker(fleet), "duplicate marker rejected");
        check((map.marker_ids() == std::vector<std::uint64_t>{100, 101, 102}),
              "marker ids ascending");
        check((map.markers_in_system(1) == std::vector<std::uint64_t>{100, 102}),
              "markers in system 1");
        check((map.markers_for_owner(7) == std::vector<std::uint64_t>{100, 101}),
              "markers for owner 7");
        check(map.update_marker_position(100, 4.0, 0.0),
              "marker position update");
        check(near(map.marker(100)->x_light_years, 4.0), "position applied");
        check(map.set_marker_destination(100, std::nullopt),
              "clear destination");
        check(!map.marker(100)->destination_system_id.has_value(),
              "destination cleared");
        check(map.remove_marker(102), "remove marker");
        check(map.marker_count() == 2, "marker count after remove");

        // Removing a system detaches anchored markers without deleting them.
        check(map.remove_system(2), "remove anchored system");
        check(!map.marker(101)->system_id.has_value(),
              "marker detached on system removal");
    }

    // --- Persistence round-trip ----------------------------------------------
    {
        GalaxyMap map;
        map.add_system(sys(5, 1.0, 2.0, "alpha"));
        map.add_system(sys(7, 9.0, 8.0, "beta"));
        map.add_lane(lane(3, 5, 7, 10.0));
        map.set_lane_enabled(3, false);
        GalaxyMarker marker;
        marker.id = 42;
        marker.kind = GalaxyMarker::Kind::Outpost;
        marker.label = "watch";
        marker.owner_id = 4;
        marker.system_id = 7;
        map.add_marker(marker);

        const auto state = map.capture_state();
        GalaxyMap restored;
        restored.restore_state(state);
        check(restored.system_count() == 2 && restored.lane_count() == 1 &&
                  restored.marker_count() == 1,
              "restore counts");
        check(restored.system(5)->name == "alpha", "restore names");
        check(!restored.lane(3)->enabled, "restore lane flags");
        check(restored.marker(42)->kind == GalaxyMarker::Kind::Outpost,
              "restore marker kind");
        check(restored.neighbors(5).empty(),
              "disabled lane adjacency after restore");

        // Continuation after restore behaves identically to the original.
        restored.set_lane_enabled(3, true);
        check((restored.neighbors(5) == std::vector<std::uint64_t>{7}),
              "restored adjacency");

        // Corrupt states are rejected without clobbering existing content.
        auto bad = state;
        bad.version = 99;
        bool threw = false;
        try {
            restored.restore_state(bad);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        check(threw && restored.system_count() == 2,
              "bad version rejected atomically");
        bad = state;
        bad.lanes.push_back(lane(4, 5, 999));
        threw = false;
        try {
            restored.restore_state(bad);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        check(threw && restored.system_count() == 2,
              "dangling lane rejected atomically");
        bad = state;
        bad.markers.push_back(state.markers.front());
        threw = false;
        try {
            restored.restore_state(bad);
        } catch (const std::runtime_error &) {
            threw = true;
        }
        check(threw, "duplicate marker rejected");
    }

    if (failures == 0)
        std::cout << "galaxy_map tests passed\n";
    return failures == 0 ? 0 : 1;
}
