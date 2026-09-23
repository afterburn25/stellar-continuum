#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stellar::engine {

// Uniform 2D spatial hash grid for gameplay/presentation queries. Keys are
// opaque owner handles (EntityId value, system id, formation pointer bits).
// Iteration is deterministic: occupants are stored in insertion order and
// cell-range queries visit cells in lexicographic order, matching the combat
// broadphase contract.
template <class Key>
class SpatialGrid {
public:
    explicit SpatialGrid(float cell_size) : cell_size_(cell_size) {
        if (!(cell_size > 0.0f) || !std::isfinite(cell_size))
            throw std::invalid_argument("SpatialGrid cell size must be positive");
    }

    void insert(Key key, float x, float y) {
        const auto c = cell_of(x, y);
        auto& members = cells_[c];
        members.push_back(Entry{key, x, y});
        locations_[key] = c;
    }

    bool remove(Key key) {
        const auto it = locations_.find(key);
        if (it == locations_.end()) return false;
        auto& members = cells_[it->second];
        std::erase_if(members, [&](const Entry& e) { return e.key == key; });
        if (members.empty()) cells_.erase(it->second);
        locations_.erase(it);
        return true;
    }

    bool move(Key key, float x, float y) {
        const auto it = locations_.find(key);
        if (it == locations_.end()) return false;
        const auto target = cell_of(x, y);
        if (target == it->second) {
            for (auto& e : cells_[it->second])
                if (e.key == key) { e.x = x; e.y = y; return true; }
            return false;
        }
        if (!remove(key)) return false;
        insert(key, x, y);
        return true;
    }

    void clear() {
        cells_.clear();
        locations_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return locations_.size(); }
    [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }
    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }

    // Occupants of every cell within cell_radius cells of the query cell —
    // the combat broadphase contract (cell-sorted, insertion order in-cell).
    [[nodiscard]] std::vector<Key> cells_in_box(float x, float y, int cell_radius) const {
        std::vector<Key> out;
        const auto origin = cell_of(x, y);
        for (int cy = origin.y - cell_radius; cy <= origin.y + cell_radius; ++cy)
            for (int cx = origin.x - cell_radius; cx <= origin.x + cell_radius; ++cx)
                append_cell({cx, cy}, out);
        return out;
    }

    // Exact circle test.
    [[nodiscard]] std::vector<Key> within_radius(float x, float y, float radius) const {
        std::vector<Key> out;
        const int r = static_cast<int>(std::ceil(radius / cell_size_));
        const auto origin = cell_of(x, y);
        const float rr = radius * radius;
        for (int cy = origin.y - r; cy <= origin.y + r; ++cy)
            for (int cx = origin.x - r; cx <= origin.x + r; ++cx) {
                const auto it = cells_.find({cx, cy});
                if (it == cells_.end()) continue;
                for (const auto& e : it->second) {
                    const float dx = e.x - x, dy = e.y - y;
                    if (dx * dx + dy * dy <= rr) out.push_back(e.key);
                }
            }
        return out;
    }

    [[nodiscard]] std::vector<Key> within_rect(float min_x, float min_y,
                                               float max_x, float max_y) const {
        std::vector<Key> out;
        const auto lo = cell_of(min_x, min_y);
        const auto hi = cell_of(max_x, max_y);
        for (int cy = lo.y; cy <= hi.y; ++cy)
            for (int cx = lo.x; cx <= hi.x; ++cx) {
                const auto it = cells_.find({cx, cy});
                if (it == cells_.end()) continue;
                for (const auto& e : it->second)
                    if (e.x >= min_x && e.x <= max_x && e.y >= min_y && e.y <= max_y)
                        out.push_back(e.key);
            }
        return out;
    }

    // Nearest occupant within max_radius, or nullopt. Expanding ring search —
    // O(cells touched), not O(n).
    [[nodiscard]] std::optional<std::pair<Key, float>>
    nearest(float x, float y, float max_radius) const {
        const auto origin = cell_of(x, y);
        const int max_ring = static_cast<int>(std::ceil(max_radius / cell_size_));
        float best = max_radius;
        std::optional<Key> best_key;
        for (int ring = 0; ring <= max_ring; ++ring) {
            // A ring farther than `best` cannot improve the answer.
            if (static_cast<float>(ring - 1) * cell_size_ > best) break;
            for (int cy = origin.y - ring; cy <= origin.y + ring; ++cy)
                for (int cx = origin.x - ring; cx <= origin.x + ring; ++cx) {
                    if (std::max(std::abs(cx - origin.x), std::abs(cy - origin.y)) != ring)
                        continue;
                    const auto it = cells_.find({cx, cy});
                    if (it == cells_.end()) continue;
                    for (const auto& e : it->second) {
                        const float dx = e.x - x, dy = e.y - y;
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d <= best) {
                            best = d;
                            best_key = e.key;
                        }
                    }
                }
        }
        if (!best_key) return std::nullopt;
        return std::pair{*best_key, best};
    }

    // Cells traversed by a ray (DDA), near-to-far, up to max_distance.
    // Returns occupant keys in traversal order — callers needing exact
    // intersection should test shapes themselves.
    [[nodiscard]] std::vector<Key> raycast(float x, float y, float dx, float dy,
                                           float max_distance) const {
        std::vector<Key> out;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0f) || !(max_distance > 0.0f)) return out;
        dx /= length;
        dy /= length;

        auto c = cell_of(x, y);
        const int step_x = dx > 0 ? 1 : -1;
        const int step_y = dy > 0 ? 1 : -1;
        const float t_delta_x = dx != 0.0f ? cell_size_ / std::abs(dx)
                                           : std::numeric_limits<float>::infinity();
        const float t_delta_y = dy != 0.0f ? cell_size_ / std::abs(dy)
                                           : std::numeric_limits<float>::infinity();
        const auto boundary = [&](int c_i, float origin, float d, int step) {
            const float next = static_cast<float>(step > 0 ? c_i + 1 : c_i) * cell_size_;
            return d != 0.0f ? (next - origin) / d
                             : std::numeric_limits<float>::infinity();
        };
        float t_max_x = boundary(c.x, x, dx, step_x);
        float t_max_y = boundary(c.y, y, dy, step_y);

        append_cell(c, out);
        float t = 0.0f;
        while (t <= max_distance) {
            if (t_max_x < t_max_y) {
                c.x += step_x;
                t = t_max_x;
                t_max_x += t_delta_x;
            } else {
                c.y += step_y;
                t = t_max_y;
                t_max_y += t_delta_y;
            }
            if (t > max_distance) break;
            append_cell(c, out);
        }
        return out;
    }

private:
    struct Cell {
        int x{}, y{};
        bool operator==(const Cell&) const = default;
    };
    struct CellHash {
        std::size_t operator()(const Cell& c) const noexcept {
            return (static_cast<std::size_t>(static_cast<std::uint32_t>(c.x)) << 20) ^
                   static_cast<std::uint32_t>(c.y);
        }
    };
    struct Entry {
        Key key;
        float x{}, y{};
    };

    [[nodiscard]] Cell cell_of(float x, float y) const {
        return {static_cast<int>(std::floor(x / cell_size_)),
                static_cast<int>(std::floor(y / cell_size_))};
    }
    void append_cell(const Cell& c, std::vector<Key>& out) const {
        const auto it = cells_.find(c);
        if (it == cells_.end()) return;
        for (const auto& e : it->second) out.push_back(e.key);
    }

    float cell_size_;
    std::unordered_map<Cell, std::vector<Entry>, CellHash> cells_;
    std::unordered_map<Key, Cell> locations_;
};

} // namespace stellar::engine
