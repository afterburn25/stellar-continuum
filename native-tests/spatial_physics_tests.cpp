#include <stellar/engine/physics.hpp>
#include <stellar/engine/spatial_index.hpp>

#include <algorithm>
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

} // namespace

int main() {
    using namespace stellar::engine;

    // Grid basics: insert/remove/move, radius and rect queries, nearest.
    {
        SpatialGrid<std::uint64_t> grid{100.0f};
        for (std::uint64_t i = 0; i < 1000; ++i)
            grid.insert(i, static_cast<float>(i % 50) * 37.0f,
                        static_cast<float>(i / 50) * 113.0f);
        check(grid.size() == 1000, "grid size");

        const auto near = grid.within_radius(50.0f, 50.0f, 60.0f);
        check(!near.empty(), "radius query finds points");
        for (auto key : near) {
            // All returned points must be inside the circle.
            const float x = static_cast<float>(key % 50) * 37.0f;
            const float y = static_cast<float>(key / 50) * 113.0f;
            check((x - 50.0f) * (x - 50.0f) + (y - 50.0f) * (y - 50.0f) <= 60.0f * 60.0f + 1.0f,
                  "radius query point inside circle");
        }

        const auto in_rect = grid.within_rect(0.0f, 0.0f, 100.0f, 200.0f);
        for (auto key : in_rect) {
            const float x = static_cast<float>(key % 50) * 37.0f;
            const float y = static_cast<float>(key / 50) * 113.0f;
            check(x <= 100.0f && y <= 200.0f, "rect query bounds");
        }

        const auto closest = grid.nearest(10.0f, 10.0f, 500.0f);
        check(closest.has_value(), "nearest found");
        check(closest->first == 0, "nearest is the origin point");

        check(grid.move(5, -5000.0f, -5000.0f), "move reassigns cell");
        check(grid.remove(5), "remove works");
        check(!grid.remove(5), "double remove returns false");
        check(!grid.nearest(10.0f, 10.0f, 0.5f).has_value(), "empty ring finds nothing");
    }

    // Cell-box query matches combat broadphase semantics.
    {
        SpatialGrid<int> grid{500.0f};
        grid.insert(1, 0.0f, 0.0f);
        grid.insert(2, 600.0f, 0.0f);   // adjacent cell
        grid.insert(3, 1600.0f, 0.0f);  // outside ±1 box
        const auto box = grid.cells_in_box(0.0f, 0.0f, 1);
        check(std::find(box.begin(), box.end(), 1) != box.end(), "box contains center");
        check(std::find(box.begin(), box.end(), 2) != box.end(), "box contains neighbor");
        check(std::find(box.begin(), box.end(), 3) == box.end(), "box excludes distant cell");
    }

    // Ray traversal visits cells in order.
    {
        SpatialGrid<int> grid{10.0f};
        grid.insert(1, 5.0f, 5.0f);
        grid.insert(2, 55.0f, 5.0f);
        grid.insert(3, 105.0f, 5.0f);
        const auto hits = grid.raycast(0.0f, 5.0f, 1.0f, 0.0f, 200.0f);
        auto p1 = std::find(hits.begin(), hits.end(), 1);
        auto p2 = std::find(hits.begin(), hits.end(), 2);
        auto p3 = std::find(hits.begin(), hits.end(), 3);
        check(p1 != hits.end() && p2 != hits.end() && p3 != hits.end(), "ray finds all");
        check(p1 < p2 && p2 < p3, "ray order is near-to-far");
    }

    // Physics: overlap, point, raycast, sweep, triggers, layers.
    {
        PhysicsWorld world{64.0f};
        const auto big = world.add_body(
            PhysicsShape{PhysicsShapeKind::Circle, 20.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
            /*layer*/1);
        const auto small = world.add_body(
            PhysicsShape{PhysicsShapeKind::Circle, 5.0f, 0.0f, 0.0f}, 30.0f, 0.0f,
            /*layer*/8);
        const auto box = world.add_body(
            PhysicsShape{PhysicsShapeKind::Aabb, 0.0f, 10.0f, 10.0f}, 0.0f, 100.0f,
            /*layer*/2);
        const auto sensor = world.add_body(
            PhysicsShape{PhysicsShapeKind::Circle, 15.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
            /*layer*/4, /*trigger*/true);

        const auto overlapped = world.overlap_circle(0.0f, 0.0f, 10.0f);
        check(std::find(overlapped.begin(), overlapped.end(), big) != overlapped.end(),
              "overlap finds big circle");
        check(std::find(overlapped.begin(), overlapped.end(), sensor) != overlapped.end(),
              "overlap finds trigger");
        check(std::find(overlapped.begin(), overlapped.end(), box) == overlapped.end(),
              "overlap excludes distant aabb");

        const auto only_layer2 = world.overlap_circle(0.0f, 100.0f, 50.0f, 2u);
        check(only_layer2.size() == 1 && only_layer2[0] == box, "layer mask filters");

        const auto at_origin = world.bodies_at(0.0f, 0.0f);
        check(at_origin.size() >= 2, "point query finds overlapping bodies");

        // Raycast along +x from x=-60, mask=8 isolates `small`: leading edge
        // at x=25 gives t=85.
        const auto hit = world.raycast(-60.0f, 0.0f, 1.0f, 0.0f, 100.0f, 8u);
        check(hit.has_value() && hit->body == small, "raycast hits small circle");
        check(std::abs(hit->distance - 85.0f) < 0.01f, "raycast distance exact");

        // Unmasked ray from -60 hits `big` first (edge at -20, t=40).
        const auto first = world.raycast(-60.0f, 0.0f, 1.0f, 0.0f, 100.0f);
        check(first.has_value() && first->body == big && std::abs(first->distance - 40.0f) < 0.01f,
              "unmasked raycast returns nearest hit");

        // Sweep: a 5-radius projectile from x=-60 reaches small's inflated
        // edge (r=10, face at x=20) at t=80.
        const auto swept = world.sweep_circle(-60.0f, 0.0f, 5.0f, 1.0f, 0.0f, 100.0f, 8u);
        check(swept.has_value() && swept->body == small, "swept circle hits");
        check(std::abs(swept->distance - 80.0f) < 0.01f, "sweep accounts for radius");

        // Triggers: `big` already overlaps the sensor at start, so the first
        // advance reports exactly its enter event; moving small in adds one.
        auto events = world.advance(0.0f);
        check(events.size() == 1 && events[0].trigger == sensor &&
                  events[0].other == big && events[0].entered,
              "initial overlap reports a single enter event");
        world.teleport(small, 10.0f, 0.0f);
        events = world.advance(0.0f);
        check(std::any_of(events.begin(), events.end(),
                          [&](const PhysicsTriggerEvent& e) {
                              return e.trigger == sensor && e.other == small && e.entered;
                          }),
              "trigger enter event");
        world.teleport(small, 100.0f, 0.0f);
        events = world.advance(0.0f);
        check(std::any_of(events.begin(), events.end(),
                          [&](const PhysicsTriggerEvent& e) {
                              return e.trigger == sensor && e.other == small && !e.entered;
                          }),
              "trigger exit event");

        // Velocity integration.
        world.set_velocity(small, 10.0f, 0.0f);
        events = world.advance(1.0f);
        check(std::abs(world.body(small)->x - 110.0f) < 0.001f, "velocity integrates");
    }

    if (failures != 0) {
        std::cerr << failures << " spatial/physics checks failed\n";
        return 1;
    }
    std::cout << "SpatialGrid and PhysicsWorld tests passed\n";
    return 0;
}
