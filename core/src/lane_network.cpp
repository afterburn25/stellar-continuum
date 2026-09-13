#include <stellar/core/lane_network.hpp>

#include <stellar/core/interstellar_distance.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {
constexpr double tie_tolerance = 1e-9;

std::pair<int, int> canonical(int first, int second) {
  return first < second ? std::pair{first, second} : std::pair{second, first};
}

std::string duplicate_message(int id) {
  return "An item with the same key has already been added. Key: " +
         std::to_string(id);
}

double flat_chart_distance(Vec2 first, Vec2 second) {
  return distance_light_years({first.x, first.y, std::nullopt},
                              {second.x, second.y, std::nullopt});
}

struct RouteKey {
  int origin{};
  std::uint64_t range_bits{};
  bool operator==(const RouteKey &) const = default;
};
struct RouteKeyHash {
  std::size_t operator()(const RouteKey &key) const noexcept {
    auto result = static_cast<std::size_t>(static_cast<unsigned>(key.origin));
    result ^= static_cast<std::size_t>(key.range_bits) + 0x9e3779b9U +
              (result << 6U) + (result >> 2U);
    return result;
  }
};
struct RouteTree {
  std::unordered_map<int, double> distance;
  std::unordered_map<int, int> prior;
};
} // namespace

double light_years_to_parsecs(double light_years) noexcept {
  return light_years / light_years_per_parsec;
}
double au_to_kilometres(double au) noexcept { return au * kilometres_per_au; }

bool InterstellarLane::connects(int system_id) const noexcept {
  return first_system_id == system_id || second_system_id == system_id;
}
int InterstellarLane::other(int system_id) const {
  if (first_system_id == system_id)
    return second_system_id;
  if (second_system_id == system_id)
    return first_system_id;
  throw std::out_of_range(
      "Specified argument was out of the range of valid values. (Parameter "
      "'systemId')");
}

RemainingFleetRoute
measure_remaining_fleet_route(const std::vector<StellarSystem> *systems,
                              const FleetState *fleet) {
  if (systems == nullptr)
    throw std::invalid_argument("Value cannot be null. (Parameter 'galaxy')");
  if (fleet == nullptr)
    throw std::invalid_argument("Value cannot be null. (Parameter 'fleet')");
  if (!fleet->destination_system_id)
    return {};

  std::unordered_map<int, const StellarSystem *> by_id;
  for (const auto &system : *systems)
    if (!by_id.emplace(system.id, &system).second)
      throw std::invalid_argument(duplicate_message(system.id));

  const auto &route = fleet->planned_route_system_ids;
  std::vector<int> destination_fallback;
  std::span<const int> route_ids;
  if (route.empty()) {
    destination_fallback.push_back(*fleet->destination_system_id);
    route_ids = destination_fallback;
  } else {
    route_ids = route;
  }

  const StellarSystem *previous = nullptr;
  if (fleet->current_system_id) {
    const auto found = by_id.find(*fleet->current_system_id);
    if (found != by_id.end())
      previous = found->second;
  }
  Vec2 previous_chart_position = fleet->position;
  RemainingFleetRoute result;
  std::size_t start_index = 0;

  if (fleet->transit_phase == FleetTransitPhase::InterstellarWarp &&
      fleet->transit_origin_system_id && !route_ids.empty()) {
    const auto origin = by_id.find(*fleet->transit_origin_system_id);
    const auto target = by_id.find(route_ids.front());
    if (origin != by_id.end() && target != by_id.end()) {
      const auto progress = std::clamp(fleet->transit_progress, 0.0, 1.0);
      result.distance_light_years +=
          distance_light_years(origin->second->position,
                               target->second->position) *
          (1.0 - progress);
      previous = target->second;
      previous_chart_position = {target->second->position.x,
                                 target->second->position.y};
      ++result.remaining_legs;
      start_index = 1;
    }
  }

  for (auto index = start_index; index < route_ids.size(); ++index) {
    const auto found = by_id.find(route_ids[index]);
    if (found == by_id.end())
      continue;
    const auto *waypoint = found->second;
    if (previous == nullptr ||
        (!previous->position.depth_light_years &&
         !waypoint->position.depth_light_years)) {
      result.distance_light_years += flat_chart_distance(
          previous_chart_position, {waypoint->position.x, waypoint->position.y});
    } else {
      result.distance_light_years +=
          distance_light_years(previous->position, waypoint->position);
    }
    previous = waypoint;
    previous_chart_position = {waypoint->position.x, waypoint->position.y};
    ++result.remaining_legs;
  }
  return result;
}

struct InterstellarLaneNetwork::Impl {
  std::vector<StellarSystem> systems;
  std::vector<InterstellarLane> lanes;
  std::unordered_map<int, const StellarSystem *> by_id;
  std::unordered_map<int, std::vector<InterstellarLane>> adjacency;
  std::unordered_map<RouteKey, RouteTree, RouteKeyHash> routes;
  bool built{};
  bool source_is_null{};
  std::thread::id owner{std::this_thread::get_id()};

  explicit Impl(std::span<const StellarSystem> values)
      : systems(values.begin(), values.end()) {}

  void require_owner() const {
    if (std::this_thread::get_id() != owner)
      throw std::runtime_error(
          "Interstellar lane queries must run on the owner thread.");
  }

  void ensure_built() {
    if (built)
      return;
    if (source_is_null)
      throw std::invalid_argument("Value cannot be null. (Parameter 'systems')");
    std::vector<InterstellarLane> next_lanes;
    std::unordered_map<int, const StellarSystem *> next_by_id;
    std::unordered_map<int, std::vector<InterstellarLane>> next_adjacency;

    if (systems.size() >= 2) {
      std::vector<const StellarSystem *> ordered;
      ordered.reserve(systems.size());
      for (const auto &system : systems)
        ordered.push_back(&system);
      std::sort(ordered.begin(), ordered.end(), [](const auto *first,
                                                    const auto *second) {
        return first->id < second->id;
      });
      for (const auto *system : ordered)
        if (!next_by_id.emplace(system->id, system).second)
          throw std::invalid_argument(duplicate_message(system->id));
      for (std::size_t first = 0; first < ordered.size(); ++first)
        for (std::size_t second = first + 1; second < ordered.size(); ++second)
          if (!std::isfinite(distance_light_years(ordered[first]->position,
                                                  ordered[second]->position)))
            throw std::invalid_argument(
                "Interstellar lane geometry produced a nonfinite distance");

      std::vector<std::pair<int, int>> edges;
      const auto add_edge = [&](int first, int second) {
        const auto edge = canonical(first, second);
        if (std::find(edges.begin(), edges.end(), edge) == edges.end())
          edges.push_back(edge);
      };
      std::unordered_set<int> connected{ordered.front()->id};
      struct Nearest {
        int first{};
        double distance{};
      };
      // The source Dictionary was populated from the ascending-ID array and only
      // deletes entries. Preserve that iteration order because the 1e-9 near-tie
      // relation is intentionally approximate and therefore nontransitive.
      std::map<int, Nearest> nearest;
      for (std::size_t index = 1; index < ordered.size(); ++index)
        nearest.emplace(ordered[index]->id,
                        Nearest{ordered.front()->id,
                                distance_light_years(ordered.front()->position,
                                                     ordered[index]->position)});

      while (connected.size() < ordered.size()) {
        int best_id = -1;
        Nearest best{-1, std::numeric_limits<double>::infinity()};
        bool found_best = false;
        for (const auto &[candidate_id, candidate] : nearest) {
          if (!found_best || candidate.distance < best.distance - tie_tolerance ||
              (std::abs(candidate.distance - best.distance) <= tie_tolerance &&
               (candidate.first < best.first ||
                (candidate.first == best.first && candidate_id < best_id)))) {
            best_id = candidate_id;
            best = candidate;
            found_best = true;
          }
        }
        if (!found_best)
          throw std::logic_error("Interstellar lane backbone made no progress.");
        add_edge(best.first, best_id);
        connected.insert(best_id);
        nearest.erase(best_id);
        const auto *added = next_by_id.at(best_id);
        for (auto &[candidate_id, current] : nearest) {
          const auto *candidate = next_by_id.at(candidate_id);
          const auto distance =
              distance_light_years(added->position, candidate->position);
          if (distance < current.distance - tie_tolerance ||
              (std::abs(distance - current.distance) <= tie_tolerance &&
               added->id < current.first))
            current = {added->id, distance};
        }
      }

      for (const auto *system : ordered) {
        std::vector<const StellarSystem *> neighbours;
        for (const auto *candidate : ordered)
          if (candidate->id != system->id)
            neighbours.push_back(candidate);
        std::stable_sort(neighbours.begin(), neighbours.end(),
                         [&](const auto *first, const auto *second) {
          const auto first_distance =
              squared_distance_light_years(system->position, first->position);
          const auto second_distance =
              squared_distance_light_years(system->position, second->position);
          if (first_distance != second_distance)
            return first_distance < second_distance;
          return first->id < second->id;
        });
        const auto count = std::min<std::size_t>(3, neighbours.size());
        for (std::size_t index = 0; index < count; ++index)
          add_edge(system->id, neighbours[index]->id);
      }
      std::sort(edges.begin(), edges.end());
      next_lanes.reserve(edges.size());
      for (const auto &[first, second] : edges)
        next_lanes.push_back(
            {first, second,
             distance_light_years(next_by_id.at(first)->position,
                                  next_by_id.at(second)->position)});
    } else {
      for (const auto &system : systems)
        next_by_id.emplace(system.id, &system);
    }

    for (const auto &[id, ignored] : next_by_id) {
      (void)ignored;
      next_adjacency.emplace(id, std::vector<InterstellarLane>{});
    }
    for (const auto &lane : next_lanes) {
      next_adjacency.at(lane.first_system_id).push_back(lane);
      next_adjacency.at(lane.second_system_id).push_back(lane);
    }
    lanes = std::move(next_lanes);
    by_id = std::move(next_by_id);
    adjacency = std::move(next_adjacency);
    routes.clear();
    built = true;
  }

  RouteTree build_route_tree(int origin, double range,
                             const std::unordered_set<int> *permitted) const {
    RouteTree tree;
    std::unordered_set<int> remaining;
    for (const auto &[id, ignored] : by_id) {
      (void)ignored;
      if (permitted == nullptr || permitted->contains(id)) {
        tree.distance.emplace(id, std::numeric_limits<double>::infinity());
        remaining.insert(id);
      }
    }
    tree.distance.at(origin) = 0;
    while (!remaining.empty()) {
      int current = 0;
      double current_distance = std::numeric_limits<double>::infinity();
      bool found_current = false;
      for (const int candidate : remaining) {
        const auto distance = tree.distance.at(candidate);
        if (!found_current || distance < current_distance ||
            (distance == current_distance && candidate < current)) {
          current = candidate;
          current_distance = distance;
          found_current = true;
        }
      }
      if (!std::isfinite(current_distance))
        break;
      remaining.erase(current);
      for (const auto &lane : adjacency.at(current)) {
        if (lane.length_light_years > range + tie_tolerance)
          continue;
        const int next = lane.other(current);
        if (!remaining.contains(next))
          continue;
        const auto candidate = current_distance + lane.length_light_years;
        if (candidate < tree.distance.at(next) - tie_tolerance) {
          tree.distance[next] = candidate;
          tree.prior[next] = current;
        }
      }
    }
    return tree;
  }
};

InterstellarLaneNetwork::InterstellarLaneNetwork(
    std::span<const StellarSystem> systems)
    : impl_(std::make_unique<Impl>(systems)) {}
InterstellarLaneNetwork::InterstellarLaneNetwork(
    const std::vector<StellarSystem> *systems) {
  impl_ = systems == nullptr ? std::make_unique<Impl>(std::span<const StellarSystem>{})
                             : std::make_unique<Impl>(*systems);
  impl_->source_is_null = systems == nullptr;
}
InterstellarLaneNetwork::~InterstellarLaneNetwork() = default;
InterstellarLaneNetwork::InterstellarLaneNetwork(
    InterstellarLaneNetwork &&) noexcept = default;
InterstellarLaneNetwork &InterstellarLaneNetwork::operator=(
    InterstellarLaneNetwork &&) noexcept = default;

std::span<const InterstellarLane> InterstellarLaneNetwork::build() {
  impl_->require_owner();
  impl_->ensure_built();
  return impl_->lanes;
}

std::vector<int> InterstellarLaneNetwork::find_shortest_route(
    int origin_system_id, int destination_system_id,
    double maximum_leg_range_light_years,
    const std::unordered_set<int> *permitted_system_ids) {
  impl_->require_owner();
  if (maximum_leg_range_light_years <= 0 ||
      std::isnan(maximum_leg_range_light_years))
    return {};
  impl_->ensure_built();
  if (!impl_->by_id.contains(origin_system_id) ||
      !impl_->by_id.contains(destination_system_id))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'destinationSystemId')");
  if (permitted_system_ids != nullptr &&
      (!permitted_system_ids->contains(origin_system_id) ||
       !permitted_system_ids->contains(destination_system_id)))
    return {};

  RouteTree temporary;
  const RouteTree *tree = nullptr;
  if (permitted_system_ids != nullptr) {
    temporary = impl_->build_route_tree(origin_system_id,
                                        maximum_leg_range_light_years,
                                        permitted_system_ids);
    tree = &temporary;
  } else {
    const RouteKey key{origin_system_id,
                       std::bit_cast<std::uint64_t>(maximum_leg_range_light_years)};
    auto found = impl_->routes.find(key);
    if (found == impl_->routes.end()) {
      auto built = impl_->build_route_tree(origin_system_id,
                                           maximum_leg_range_light_years,
                                           nullptr);
      if (impl_->routes.size() >= 64)
        impl_->routes.clear();
      found = impl_->routes.emplace(key, std::move(built)).first;
    }
    tree = &found->second;
  }
  const auto distance = tree->distance.find(destination_system_id);
  if (distance == tree->distance.end() || !std::isfinite(distance->second))
    return {};
  std::vector<int> result{destination_system_id};
  while (result.back() != origin_system_id)
    result.push_back(tree->prior.at(result.back()));
  std::reverse(result.begin(), result.end());
  return result;
}

std::size_t InterstellarLaneNetwork::cached_route_tree_count() const {
  impl_->require_owner();
  return impl_->routes.size();
}

} // namespace stellar::core
