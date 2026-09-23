#include <stellar/engine/galaxy_map.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace stellar::engine {
namespace {

template <class Map>
std::vector<std::uint64_t> sorted_keys(const Map &map) {
    std::vector<std::uint64_t> ids;
    ids.reserve(map.size());
    for (const auto &[id, value] : map)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace

bool GalaxyMap::add_system(GalaxySystem system) {
    return systems_.emplace(system.id, std::move(system)).second;
}

bool GalaxyMap::remove_system(std::uint64_t id) {
    if (systems_.erase(id) == 0)
        return false;
    for (auto it = lanes_.begin(); it != lanes_.end();) {
        if (it->second.connects(id))
            it = lanes_.erase(it);
        else
            ++it;
    }
    for (auto &[mid, m] : markers_) {
        if (m.system_id == id)
            m.system_id.reset();
        if (m.destination_system_id == id)
            m.destination_system_id.reset();
    }
    topology_dirty_ = true;
    return true;
}

bool GalaxyMap::add_lane(GalaxyLane lane) {
    if (!systems_.contains(lane.first_system_id) ||
        !systems_.contains(lane.second_system_id) ||
        lane.first_system_id == lane.second_system_id)
        return false;
    if (lanes_.emplace(lane.id, std::move(lane)).second) {
        topology_dirty_ = true;
        return true;
    }
    return false;
}

bool GalaxyMap::remove_lane(std::uint64_t id) {
    if (lanes_.erase(id) == 0)
        return false;
    topology_dirty_ = true;
    return true;
}

bool GalaxyMap::set_lane_enabled(std::uint64_t id, bool enabled) {
    auto it = lanes_.find(id);
    if (it == lanes_.end())
        return false;
    it->second.enabled = enabled;
    topology_dirty_ = true;
    return true;
}

bool GalaxyMap::add_marker(GalaxyMarker marker) {
    return markers_.emplace(marker.id, std::move(marker)).second;
}

bool GalaxyMap::remove_marker(std::uint64_t id) {
    return markers_.erase(id) != 0;
}

bool GalaxyMap::update_marker_position(std::uint64_t id, double x,
                                       double y, double depth) {
    auto it = markers_.find(id);
    if (it == markers_.end())
        return false;
    it->second.x_light_years = x;
    it->second.y_light_years = y;
    it->second.depth_light_years = depth;
    return true;
}

bool GalaxyMap::set_marker_destination(std::uint64_t id,
                                       std::optional<std::uint64_t> system) {
    auto it = markers_.find(id);
    if (it == markers_.end())
        return false;
    it->second.destination_system_id = system;
    return true;
}

bool GalaxyMap::set_marker_system(std::uint64_t id,
                                  std::optional<std::uint64_t> system) {
    auto it = markers_.find(id);
    if (it == markers_.end())
        return false;
    it->second.system_id = system;
    return true;
}

const GalaxySystem *GalaxyMap::system(std::uint64_t id) const {
    const auto it = systems_.find(id);
    return it == systems_.end() ? nullptr : &it->second;
}

const GalaxyLane *GalaxyMap::lane(std::uint64_t id) const {
    const auto it = lanes_.find(id);
    return it == lanes_.end() ? nullptr : &it->second;
}

const GalaxyMarker *GalaxyMap::marker(std::uint64_t id) const {
    const auto it = markers_.find(id);
    return it == markers_.end() ? nullptr : &it->second;
}

std::vector<std::uint64_t> GalaxyMap::system_ids() const {
    return sorted_keys(systems_);
}

std::vector<std::uint64_t> GalaxyMap::lane_ids() const {
    return sorted_keys(lanes_);
}

std::vector<std::uint64_t> GalaxyMap::marker_ids() const {
    return sorted_keys(markers_);
}

void GalaxyMap::rebuild_adjacency() const {
    adjacency_systems_.clear();
    adjacency_lanes_.clear();
    for (const auto &[id, system] : systems_) {
        adjacency_systems_[id];
        adjacency_lanes_[id];
    }
    for (const auto &[id, lane] : lanes_) {
        if (!lane.enabled)
            continue;
        adjacency_systems_[lane.first_system_id].push_back(lane.second_system_id);
        adjacency_systems_[lane.second_system_id].push_back(lane.first_system_id);
        adjacency_lanes_[lane.first_system_id].push_back(id);
        adjacency_lanes_[lane.second_system_id].push_back(id);
    }
    for (auto &[id, ids] : adjacency_systems_)
        std::sort(ids.begin(), ids.end());
    for (auto &[id, ids] : adjacency_lanes_)
        std::sort(ids.begin(), ids.end());
    topology_dirty_ = false;
}

std::vector<std::uint64_t> GalaxyMap::neighbors(std::uint64_t system_id) const {
    if (topology_dirty_)
        rebuild_adjacency();
    const auto it = adjacency_systems_.find(system_id);
    return it == adjacency_systems_.end() ? std::vector<std::uint64_t>{}
                                          : it->second;
}

std::vector<std::uint64_t> GalaxyMap::lanes_for(std::uint64_t system_id) const {
    if (topology_dirty_)
        rebuild_adjacency();
    const auto it = adjacency_lanes_.find(system_id);
    return it == adjacency_lanes_.end() ? std::vector<std::uint64_t>{}
                                        : it->second;
}

std::vector<std::uint64_t>
GalaxyMap::systems_in_radius(double x, double y, double radius) const {
    std::vector<std::uint64_t> result;
    const double limit = radius * radius;
    for (const auto &[id, s] : systems_) {
        const double dx = s.x_light_years - x, dy = s.y_light_years - y;
        if (dx * dx + dy * dy <= limit)
            result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::uint64_t> GalaxyMap::nearest_system(double x,
                                                     double y) const {
    std::optional<std::uint64_t> best;
    double best_squared = 0.0;
    for (const auto &[id, s] : systems_) {
        const double dx = s.x_light_years - x, dy = s.y_light_years - y;
        const double squared = dx * dx + dy * dy;
        // Tie-break by id so results never depend on hash order.
        if (!best || squared < best_squared ||
            (squared == best_squared && id < *best)) {
            best = id;
            best_squared = squared;
        }
    }
    return best;
}

std::vector<std::uint64_t>
GalaxyMap::markers_in_system(std::uint64_t system_id) const {
    std::vector<std::uint64_t> result;
    for (const auto &[id, m] : markers_)
        if (m.system_id == system_id)
            result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::uint64_t>
GalaxyMap::markers_for_owner(std::uint64_t owner_id) const {
    std::vector<std::uint64_t> result;
    for (const auto &[id, m] : markers_)
        if (m.owner_id == owner_id)
            result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

double GalaxyMap::distance_light_years(std::uint64_t first,
                                       std::uint64_t second) const {
    const auto *a = system(first), *b = system(second);
    if (!a || !b)
        throw std::out_of_range("GalaxyMap::distance_light_years: unknown system");
    const double dx = a->x_light_years - b->x_light_years;
    const double dy = a->y_light_years - b->y_light_years;
    const double dz = a->depth_light_years - b->depth_light_years;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

GalaxyMap::State GalaxyMap::capture_state() const {
    State state;
    for (const auto id : system_ids())
        state.systems.push_back(*system(id));
    for (const auto id : lane_ids())
        state.lanes.push_back(*lane(id));
    for (const auto id : marker_ids())
        state.markers.push_back(*marker(id));
    return state;
}

void GalaxyMap::restore_state(const State &state) {
    if (state.version != 1)
        throw std::runtime_error("GalaxyMap state version unsupported");
    std::unordered_set<std::uint64_t> seen_systems, seen_lanes, seen_markers;
    for (const auto &s : state.systems)
        if (!seen_systems.insert(s.id).second)
            throw std::runtime_error("GalaxyMap state has duplicate system id");
    for (const auto &l : state.lanes) {
        if (!seen_lanes.insert(l.id).second)
            throw std::runtime_error("GalaxyMap state has duplicate lane id");
        if (!seen_systems.contains(l.first_system_id) ||
            !seen_systems.contains(l.second_system_id))
            throw std::runtime_error("GalaxyMap state lane references unknown system");
    }
    for (const auto &m : state.markers)
        if (!seen_markers.insert(m.id).second)
            throw std::runtime_error("GalaxyMap state has duplicate marker id");

    systems_.clear();
    lanes_.clear();
    markers_.clear();
    for (const auto &s : state.systems)
        systems_.emplace(s.id, s);
    for (const auto &l : state.lanes)
        lanes_.emplace(l.id, l);
    for (const auto &m : state.markers)
        markers_.emplace(m.id, m);
    topology_dirty_ = true;
}

void GalaxyMap::clear() {
    systems_.clear();
    lanes_.clear();
    markers_.clear();
    topology_dirty_ = true;
}

} // namespace stellar::engine
