#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stellar/engine/spatial_index.hpp>

namespace stellar::engine {

// Gameplay physics: collision primitives, grid broadphase, raycasts, overlap
// queries, trigger volumes and swept projectile tests. Astronomical orbit
// propagation is deliberately out of scope — this is for proximity, docking,
// debris and projectile interactions at tactical scale.
using PhysicsBodyId = std::uint64_t;

enum class PhysicsShapeKind { Circle, Aabb };

struct PhysicsShape {
    PhysicsShapeKind kind{PhysicsShapeKind::Circle};
    float radius{};                // Circle
    float half_width{}, half_height{}; // Aabb
};

struct PhysicsBody {
    PhysicsBodyId id{};
    PhysicsShape shape;
    float x{}, y{};
    float velocity_x{}, velocity_y{};
    std::uint32_t layer{1};        // bitmask — queries filter with it
    bool trigger{};                // overlaps reported, no blocking semantics
};

struct PhysicsRayHit {
    PhysicsBodyId body{};
    float distance{};              // t along the ray
    float x{}, y{};                // hit point
};

struct PhysicsTriggerEvent {
    PhysicsBodyId trigger{};
    PhysicsBodyId other{};
    bool entered{};                // false => exited
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(float broadphase_cell_size = 256.0f);

    PhysicsBodyId add_body(PhysicsShape shape, float x, float y,
                           std::uint32_t layer = 1, bool trigger = false);
    bool remove_body(PhysicsBodyId id);
    bool teleport(PhysicsBodyId id, float x, float y);
    bool set_velocity(PhysicsBodyId id, float vx, float vy);
    [[nodiscard]] const PhysicsBody* body(PhysicsBodyId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return bodies_.size(); }

    // Point/area queries (broadphase-filtered, then exact shape tests).
    [[nodiscard]] std::vector<PhysicsBodyId> overlap_circle(float x, float y, float radius,
                                                            std::uint32_t mask = ~0u) const;
    [[nodiscard]] std::vector<PhysicsBodyId> overlap_aabb(float min_x, float min_y,
                                                          float max_x, float max_y,
                                                          std::uint32_t mask = ~0u) const;
    [[nodiscard]] std::vector<PhysicsBodyId> bodies_at(float x, float y,
                                                       std::uint32_t mask = ~0u) const;

    // Nearest hit along a ray, and all hits in traversal order.
    [[nodiscard]] std::optional<PhysicsRayHit> raycast(float x, float y, float dx, float dy,
                                                       float max_distance,
                                                       std::uint32_t mask = ~0u) const;
    [[nodiscard]] std::vector<PhysicsRayHit> raycast_all(float x, float y, float dx, float dy,
                                                         float max_distance,
                                                         std::uint32_t mask = ~0u) const;
    // Swept circle (projectile) test: earliest body the disc touches while
    // travelling the segment, or nullopt.
    [[nodiscard]] std::optional<PhysicsRayHit> sweep_circle(float x, float y, float radius,
                                                            float dx, float dy, float distance,
                                                            std::uint32_t mask = ~0u) const;

    // Integrates velocities by dt seconds and diffs trigger overlaps,
    // returning enter/exit events since the previous advance.
    [[nodiscard]] std::vector<PhysicsTriggerEvent> advance(float dt);

    // Exact shape-pair test, exposed for callers with their own broadphase.
    static bool intersects(const PhysicsBody& a, const PhysicsBody& b) noexcept;
    static bool point_inside(const PhysicsBody& body, float x, float y) noexcept;

    // Persistence: bodies, id counter, broadphase cell size and the live
    // trigger-overlap set round-trip so a restored world continues without
    // re-firing ENTER events for pairs that were already overlapping. The
    // broadphase grid itself is derived state and rebuilds on restore.
    struct State {
        std::uint32_t version{1};
        float broadphase_cell_size{256.0f};
        PhysicsBodyId next_id{1};
        std::vector<PhysicsBody> bodies;                       // sorted by id
        std::vector<std::pair<PhysicsBodyId, PhysicsBodyId>> overlapping;
    };
    [[nodiscard]] State capture_state() const;
    // Replaces all bodies and overlap state. Throws invalid_argument on
    // duplicate ids, overlapping pairs that reference missing bodies, or a
    // non-finite/non-positive cell size.
    void restore_state(const State& state);

private:
    struct BodyPairHash {
        std::size_t operator()(const std::pair<PhysicsBodyId, PhysicsBodyId>& p) const noexcept {
            return p.first * 0x9E3779B97F4A7C15ULL ^ p.second;
        }
    };

    [[nodiscard]] std::optional<PhysicsRayHit>
    ray_hit(const PhysicsBody& body, float x, float y, float dx, float dy,
            float max_distance) const;
    [[nodiscard]] float body_radius_for_broadphase(const PhysicsBody& body) const noexcept;
    void update_broadphase(PhysicsBodyId id);

    float cell_size_;
    SpatialGrid<PhysicsBodyId> grid_;
    std::unordered_map<PhysicsBodyId, PhysicsBody> bodies_;
    std::set<std::pair<PhysicsBodyId, PhysicsBodyId>> overlapping_;
    PhysicsBodyId next_id_{1};
};

} // namespace stellar::engine
