#include <stellar/engine/physics.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace stellar::engine {

namespace {

bool circle_circle(float ax, float ay, float ar, float bx, float by, float br) noexcept {
    const float dx = ax - bx, dy = ay - by, r = ar + br;
    return dx * dx + dy * dy <= r * r;
}

bool circle_aabb(float cx, float cy, float r,
                 float bx, float by, float hw, float hh) noexcept {
    const float dx = std::max(std::abs(cx - bx) - hw, 0.0f);
    const float dy = std::max(std::abs(cy - by) - hh, 0.0f);
    return dx * dx + dy * dy <= r * r;
}

bool aabb_aabb(float ax, float ay, float ahw, float ahh,
               float bx, float by, float bhw, float bhh) noexcept {
    return std::abs(ax - bx) <= ahw + bhw && std::abs(ay - by) <= ahh + bhh;
}

// Ray vs circle: smallest positive t where |p + t d - c| = r.
std::optional<float> ray_circle(float px, float py, float dx, float dy,
                                float cx, float cy, float r, float max_t) {
    const float fx = px - cx, fy = py - cy;
    const float b = fx * dx + fy * dy;
    const float c = fx * fx + fy * fy - r * r;
    const float disc = b * b - c;
    if (disc < 0.0f) return std::nullopt;
    const float root = std::sqrt(disc);
    float t = -b - root;
    if (t < 0.0f) t = -b + root; // origin inside the circle
    if (t < 0.0f || t > max_t) return std::nullopt;
    return t;
}

// Ray vs AABB: slab test.
std::optional<float> ray_aabb(float px, float py, float dx, float dy,
                              float cx, float cy, float hw, float hh, float max_t) {
    float t_min = 0.0f, t_max = max_t;
    const auto slab = [&](float p, float d, float lo, float hi) -> bool {
        if (d == 0.0f) return p >= lo && p <= hi;
        float t1 = (lo - p) / d, t2 = (hi - p) / d;
        if (t1 > t2) std::swap(t1, t2);
        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        return t_min <= t_max;
    };
    if (!slab(px, dx, cx - hw, cx + hw)) return std::nullopt;
    if (!slab(py, dy, cy - hh, cy + hh)) return std::nullopt;
    if (t_min < 0.0f || t_min > max_t) return std::nullopt;
    return t_min;
}

} // namespace

PhysicsWorld::PhysicsWorld(float broadphase_cell_size)
    : cell_size_(broadphase_cell_size), grid_(broadphase_cell_size) {}

PhysicsBodyId PhysicsWorld::add_body(PhysicsShape shape, float x, float y,
                                     std::uint32_t layer, bool trigger) {
    if (shape.kind == PhysicsShapeKind::Circle && !(shape.radius > 0.0f))
        throw std::invalid_argument("PhysicsWorld circle radius must be positive");
    if (shape.kind == PhysicsShapeKind::Aabb &&
        !(shape.half_width > 0.0f && shape.half_height > 0.0f))
        throw std::invalid_argument("PhysicsWorld aabb extents must be positive");
    const PhysicsBodyId id = next_id_++;
    PhysicsBody body;
    body.id = id;
    body.shape = shape;
    body.x = x;
    body.y = y;
    body.layer = layer;
    body.trigger = trigger;
    bodies_.emplace(id, body);
    grid_.insert(id, x, y);
    return id;
}

bool PhysicsWorld::remove_body(PhysicsBodyId id) {
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    grid_.remove(id);
    for (auto pair = overlapping_.begin(); pair != overlapping_.end();)
        if (pair->first == id || pair->second == id) pair = overlapping_.erase(pair);
        else ++pair;
    bodies_.erase(it);
    return true;
}

bool PhysicsWorld::teleport(PhysicsBodyId id, float x, float y) {
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.x = x;
    it->second.y = y;
    grid_.move(id, x, y);
    return true;
}

bool PhysicsWorld::set_velocity(PhysicsBodyId id, float vx, float vy) {
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.velocity_x = vx;
    it->second.velocity_y = vy;
    return true;
}

const PhysicsBody* PhysicsWorld::body(PhysicsBodyId id) const {
    const auto it = bodies_.find(id);
    return it == bodies_.end() ? nullptr : &it->second;
}

float PhysicsWorld::body_radius_for_broadphase(const PhysicsBody& body) const noexcept {
    return body.shape.kind == PhysicsShapeKind::Circle
               ? body.shape.radius
               : std::sqrt(body.shape.half_width * body.shape.half_width +
                           body.shape.half_height * body.shape.half_height);
}

void PhysicsWorld::update_broadphase(PhysicsBodyId id) {
    const auto it = bodies_.find(id);
    if (it != bodies_.end()) grid_.move(id, it->second.x, it->second.y);
}

bool PhysicsWorld::intersects(const PhysicsBody& a, const PhysicsBody& b) noexcept {
    if (a.shape.kind == PhysicsShapeKind::Circle && b.shape.kind == PhysicsShapeKind::Circle)
        return circle_circle(a.x, a.y, a.shape.radius, b.x, b.y, b.shape.radius);
    if (a.shape.kind == PhysicsShapeKind::Aabb && b.shape.kind == PhysicsShapeKind::Aabb)
        return aabb_aabb(a.x, a.y, a.shape.half_width, a.shape.half_height,
                         b.x, b.y, b.shape.half_width, b.shape.half_height);
    const PhysicsBody& circle = a.shape.kind == PhysicsShapeKind::Circle ? a : b;
    const PhysicsBody& box = a.shape.kind == PhysicsShapeKind::Circle ? b : a;
    return circle_aabb(circle.x, circle.y, circle.shape.radius,
                       box.x, box.y, box.shape.half_width, box.shape.half_height);
}

bool PhysicsWorld::point_inside(const PhysicsBody& body, float x, float y) noexcept {
    if (body.shape.kind == PhysicsShapeKind::Circle) {
        const float dx = x - body.x, dy = y - body.y;
        return dx * dx + dy * dy <= body.shape.radius * body.shape.radius;
    }
    return std::abs(x - body.x) <= body.shape.half_width &&
           std::abs(y - body.y) <= body.shape.half_height;
}

std::vector<PhysicsBodyId> PhysicsWorld::overlap_circle(float x, float y, float radius,
                                                        std::uint32_t mask) const {
    // Expand the broadphase query by the largest body extent so centers one
    // cell outside still get shape-tested.
    float max_extent = 0.0f;
    for (const auto& [id, body] : bodies_)
        max_extent = std::max(max_extent, body_radius_for_broadphase(body));
    std::vector<PhysicsBodyId> out;
    for (PhysicsBodyId id : grid_.within_radius(x, y, radius + max_extent)) {
        const auto& candidate = bodies_.at(id);
        if (!(candidate.layer & mask)) continue;
        if (candidate.shape.kind == PhysicsShapeKind::Circle
                ? circle_circle(x, y, radius, candidate.x, candidate.y, candidate.shape.radius)
                : circle_aabb(x, y, radius, candidate.x, candidate.y,
                              candidate.shape.half_width, candidate.shape.half_height))
            out.push_back(id);
    }
    return out;
}

std::vector<PhysicsBodyId> PhysicsWorld::overlap_aabb(float min_x, float min_y,
                                                      float max_x, float max_y,
                                                      std::uint32_t mask) const {
    float max_extent = 0.0f;
    for (const auto& [id, body] : bodies_)
        max_extent = std::max(max_extent, body_radius_for_broadphase(body));
    std::vector<PhysicsBodyId> out;
    for (PhysicsBodyId id :
         grid_.within_rect(min_x - max_extent, min_y - max_extent,
                           max_x + max_extent, max_y + max_extent)) {
        const auto& candidate = bodies_.at(id);
        if (!(candidate.layer & mask)) continue;
        const float cx = (min_x + max_x) * 0.5f, cy = (min_y + max_y) * 0.5f;
        const float hw = (max_x - min_x) * 0.5f, hh = (max_y - min_y) * 0.5f;
        PhysicsBody probe;
        probe.shape = PhysicsShape{PhysicsShapeKind::Aabb, 0.0f, hw, hh};
        probe.x = cx;
        probe.y = cy;
        if (intersects(probe, candidate)) out.push_back(id);
    }
    return out;
}

std::vector<PhysicsBodyId> PhysicsWorld::bodies_at(float x, float y,
                                                   std::uint32_t mask) const {
    float max_extent = 0.0f;
    for (const auto& [id, body] : bodies_)
        max_extent = std::max(max_extent, body_radius_for_broadphase(body));
    std::vector<PhysicsBodyId> out;
    for (PhysicsBodyId id : grid_.within_radius(x, y, max_extent + 1.0f)) {
        const auto& candidate = bodies_.at(id);
        if ((candidate.layer & mask) && point_inside(candidate, x, y)) out.push_back(id);
    }
    return out;
}

std::optional<PhysicsRayHit> PhysicsWorld::ray_hit(const PhysicsBody& body, float x, float y,
                                                   float dx, float dy,
                                                   float max_distance) const {
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.0f)) return std::nullopt;
    const float nx = dx / length, ny = dy / length;
    std::optional<float> t;
    if (body.shape.kind == PhysicsShapeKind::Circle)
        t = ray_circle(x, y, nx, ny, body.x, body.y, body.shape.radius, max_distance);
    else
        t = ray_aabb(x, y, nx, ny, body.x, body.y, body.shape.half_width,
                     body.shape.half_height, max_distance);
    if (!t) return std::nullopt;
    return PhysicsRayHit{body.id, *t, x + nx * *t, y + ny * *t};
}

std::vector<PhysicsRayHit> PhysicsWorld::raycast_all(float x, float y, float dx, float dy,
                                                     float max_distance,
                                                     std::uint32_t mask) const {
    std::vector<PhysicsRayHit> hits;
    for (PhysicsBodyId id : grid_.raycast(x, y, dx, dy, max_distance)) {
        const auto& candidate = bodies_.at(id);
        if (!(candidate.layer & mask)) continue;
        if (auto hit = ray_hit(candidate, x, y, dx, dy, max_distance))
            hits.push_back(*hit);
    }
    std::sort(hits.begin(), hits.end(),
              [](const PhysicsRayHit& a, const PhysicsRayHit& b) {
                  return a.distance < b.distance;
              });
    hits.erase(std::unique(hits.begin(), hits.end(),
                           [](const PhysicsRayHit& a, const PhysicsRayHit& b) {
                               return a.body == b.body;
                           }),
               hits.end());
    return hits;
}

std::optional<PhysicsRayHit> PhysicsWorld::raycast(float x, float y, float dx, float dy,
                                                   float max_distance,
                                                   std::uint32_t mask) const {
    const auto hits = raycast_all(x, y, dx, dy, max_distance, mask);
    return hits.empty() ? std::nullopt : std::optional<PhysicsRayHit>{hits.front()};
}

std::optional<PhysicsRayHit> PhysicsWorld::sweep_circle(float x, float y, float radius,
                                                        float dx, float dy, float distance,
                                                        std::uint32_t mask) const {
    // Equivalent to a ray cast against shapes inflated by the sweep radius.
    std::optional<PhysicsRayHit> best;
    for (PhysicsBodyId id : grid_.raycast(x, y, dx, dy, distance)) {
        const auto& candidate = bodies_.at(id);
        if (!(candidate.layer & mask)) continue;
        PhysicsBody inflated = candidate;
        if (inflated.shape.kind == PhysicsShapeKind::Circle) {
            inflated.shape.radius += radius;
        } else {
            inflated.shape.half_width += radius;
            inflated.shape.half_height += radius;
        }
        if (auto hit = ray_hit(inflated, x, y, dx, dy, distance)) {
            hit->body = id;
            if (!best || hit->distance < best->distance) best = hit;
        }
    }
    return best;
}

std::vector<PhysicsTriggerEvent> PhysicsWorld::advance(float dt) {
    if (dt < 0.0f || !std::isfinite(dt))
        throw std::invalid_argument("PhysicsWorld dt must be finite and non-negative");
    // Integrate.
    for (auto& [id, body] : bodies_) {
        if (body.velocity_x != 0.0f || body.velocity_y != 0.0f) {
            body.x += body.velocity_x * dt;
            body.y += body.velocity_y * dt;
            grid_.move(id, body.x, body.y);
        }
    }
    // Diff trigger overlaps.
    std::set<std::pair<PhysicsBodyId, PhysicsBodyId>> current;
    for (const auto& [id, body] : bodies_) {
        if (!body.trigger) continue;
        const float reach = body_radius_for_broadphase(body);
        float max_extent = reach;
        for (const auto& [other_id, other] : bodies_)
            if (other_id != id && !other.trigger)
                max_extent = std::max(max_extent, body_radius_for_broadphase(other));
        for (PhysicsBodyId other_id :
             grid_.within_radius(body.x, body.y, reach + max_extent)) {
            if (other_id == id) continue;
            const auto& other = bodies_.at(other_id);
            if (other.trigger) continue; // trigger-trigger pairs are not tracked
            if (intersects(body, other)) current.emplace(id, other_id);
        }
    }
    std::vector<PhysicsTriggerEvent> events;
    for (const auto& pair : current)
        if (!overlapping_.count(pair))
            events.push_back({pair.first, pair.second, true});
    for (const auto& pair : overlapping_)
        if (!current.count(pair))
            events.push_back({pair.first, pair.second, false});
    overlapping_ = std::move(current);
    return events;
}

} // namespace stellar::engine
