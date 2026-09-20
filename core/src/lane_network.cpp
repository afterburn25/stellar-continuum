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

  // Policy-aware Dijkstra: blocked systems are never entered, leg cost is
  // length * traversal_cost_scale(destination-of-leg). Deterministic
  // min-id tie-breaking mirrors build_route_tree.
  RouteTree build_policy_tree(int origin, int destination,
                              const InterstellarLaneNetwork::RoutePolicy &policy)
      const {
    RouteTree tree;
    std::unordered_set<int> remaining;
    for (const auto &[id, ignored] : by_id) {
      (void)ignored;
      if (id != origin && id != destination) {
        if (policy.permitted_system_ids != nullptr &&
            !policy.permitted_system_ids->contains(id))
          continue;
        if (policy.blocked_system_ids != nullptr &&
            policy.blocked_system_ids->contains(id))
          continue;
      }
      tree.distance.emplace(id, std::numeric_limits<double>::infinity());
      remaining.insert(id);
    }
    if (!remaining.contains(origin) || !remaining.contains(destination))
      return tree;
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
      if (current == destination)
        break;
      remaining.erase(current);
      for (const auto &lane : adjacency.at(current)) {
        if (lane.length_light_years >
            policy.maximum_leg_range_light_years + tie_tolerance)
          continue;
        const int next = lane.other(current);
        if (!remaining.contains(next))
          continue;
        double scale = 1.0;
        if (policy.traversal_cost_scale)
          scale = policy.traversal_cost_scale(next);
        if (!std::isfinite(scale) || scale <= 0.0)
          continue;
        const auto candidate =
            current_distance + lane.length_light_years * scale;
        if (candidate < tree.distance.at(next) - tie_tolerance) {
          tree.distance[next] = candidate;
          tree.prior[next] = current;
        }
      }
    }
    return tree;
  }

  double lane_length_between(int first, int second) const {
    if (first == second)
      return 0.0;
    const auto found = adjacency.find(first);
    if (found == adjacency.end())
      return std::numeric_limits<double>::infinity();
    for (const auto &lane : found->second)
      if (lane.other(first) == second)
        return lane.length_light_years;
    return std::numeric_limits<double>::infinity();
  }

  double route_leg_distance(std::span<const int> route_ids) const {
    double total = 0.0;
    for (std::size_t i = 1; i < route_ids.size(); ++i) {
      const auto leg = lane_length_between(route_ids[i - 1], route_ids[i]);
      if (!std::isfinite(leg))
        return std::numeric_limits<double>::infinity();
      total += leg;
    }
    return total;
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

std::vector<int> InterstellarLaneNetwork::find_shortest_route(
    int origin_system_id, int destination_system_id,
    const RoutePolicy &policy) {
  impl_->require_owner();
  if (policy.maximum_leg_range_light_years <= 0 ||
      std::isnan(policy.maximum_leg_range_light_years))
    return {};
  impl_->ensure_built();
  if (!impl_->by_id.contains(origin_system_id) ||
      !impl_->by_id.contains(destination_system_id))
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'destinationSystemId')");
  if (origin_system_id == destination_system_id)
    return {origin_system_id};
  if (policy.permitted_system_ids != nullptr &&
      (!policy.permitted_system_ids->contains(origin_system_id) ||
       !policy.permitted_system_ids->contains(destination_system_id)))
    return {};

  const auto tree = impl_->build_policy_tree(origin_system_id,
                                             destination_system_id, policy);
  const auto distance = tree.distance.find(destination_system_id);
  if (distance == tree.distance.end() || !std::isfinite(distance->second))
    return {};
  std::vector<int> result{destination_system_id};
  while (result.back() != origin_system_id)
    result.push_back(tree.prior.at(result.back()));
  std::reverse(result.begin(), result.end());
  return result;
}

InterstellarLaneNetwork::FuelRouteResult
InterstellarLaneNetwork::find_fuel_feasible_route(
    const FuelRouteRequest &request) {
  impl_->require_owner();
  FuelRouteResult result;
  if (request.fuel_capacity_light_years <= 0 ||
      request.max_waypoint_insertions < 0)
    return result;
  impl_->ensure_built();
  if (!impl_->by_id.contains(request.origin_system_id) ||
      !impl_->by_id.contains(request.destination_system_id))
    return result;

  const auto capacity = request.fuel_capacity_light_years;
  const double initial_fuel =
      std::min(capacity, request.initial_fuel_light_years <= 0
                             ? capacity
                             : request.initial_fuel_light_years);
  auto refuel_amount = [&](int system_id) {
    if (!request.refuel_light_years)
      return 0.0;
    const auto amount = request.refuel_light_years(system_id);
    return std::isfinite(amount) ? std::max(0.0, amount) : 0.0;
  };
  // Walks `route` from the start with `fuel`, returning fuel remaining at the
  // first system that cannot be reached, or a positive value on success.
  // `stranded_index` receives the index of the unreachable system on failure.
  auto walk = [&](std::span<const int> route_ids, double fuel,
                  std::size_t *stranded_index, std::vector<int> *refuels) {
    if (refuels != nullptr)
      refuels->clear();
    for (std::size_t i = 0; i < route_ids.size(); ++i) {
      if (i > 0) {
        const auto leg =
            impl_->lane_length_between(route_ids[i - 1], route_ids[i]);
        if (!std::isfinite(leg) || leg > fuel + tie_tolerance) {
          if (stranded_index != nullptr)
            *stranded_index = i;
          return -1.0;
        }
        fuel -= leg;
      }
      const auto refill = refuel_amount(route_ids[i]);
      if (refill > 0.0) {
        fuel = std::min(capacity, fuel + refill);
        if (refuels != nullptr)
          refuels->push_back(route_ids[i]);
      }
    }
    return fuel;
  };

  auto route = find_shortest_route(request.origin_system_id,
                                   request.destination_system_id,
                                   request.policy);
  if (route.empty())
    return result;

  for (int iteration = 0; iteration <= request.max_waypoint_insertions;
       ++iteration) {
    std::size_t stranded_index = route.size();
    std::vector<int> refuels;
    const auto fuel = initial_fuel;
    if (walk(route, fuel, &stranded_index, &refuels) >= 0.0) {
      result.feasible = true;
      result.route_system_ids = std::move(route);
      result.refuel_system_ids = std::move(refuels);
      return result;
    }
    if (iteration == request.max_waypoint_insertions)
      break;

    // Stranded before route[stranded_index]: find the best waypoint `w`
    // reachable from route[stranded_index - 1] under current fuel, then
    // continue from `w` to the destination. Score candidates by added
    // distance, prefer refuel stops, break ties by lowest system id.
    const int stranded_from = route[stranded_index - 1];
    double fuel_at_stranded_from = initial_fuel;
    {
      std::size_t ignored{};
      fuel_at_stranded_from =
          walk(std::span<const int>(route.data(), stranded_index),
               initial_fuel, &ignored, nullptr);
      if (fuel_at_stranded_from < 0.0)
        break; // should not happen: prefix walked successfully above
      // walk() above returns fuel AFTER stopping at stranded_index-1.
    }
    const auto direct_tail_distance =
        impl_->route_leg_distance(std::span<const int>(
            route.data() + stranded_index - 1,
            route.size() - stranded_index + 1));

    struct Candidate {
      double detour{};
      bool refuels{};
      int system_id{};
      std::vector<int> to_waypoint;
      std::vector<int> to_destination;
      bool operator<(const Candidate &other) const {
        if (detour != other.detour)
          return detour < other.detour;
        if (refuels != other.refuels)
          return refuels > other.refuels;
        return system_id < other.system_id;
      }
    };
    std::vector<Candidate> candidates;
    for (const auto &[candidate_id, ignored_system] : impl_->by_id) {
      (void)ignored_system;
      if (candidate_id == stranded_from ||
          candidate_id == request.destination_system_id)
        continue;
      if (std::find(route.begin(), route.end(), candidate_id) != route.end())
        continue;
      auto to_waypoint = find_shortest_route(stranded_from, candidate_id,
                                             request.policy);
      if (to_waypoint.size() < 2)
        continue;
      const auto fuel_after =
          walk(to_waypoint, fuel_at_stranded_from, nullptr, nullptr);
      if (fuel_after < 0.0)
        continue;
      auto to_destination = find_shortest_route(candidate_id,
                                                request.destination_system_id,
                                                request.policy);
      if (to_destination.empty())
        continue;
      const auto detour =
          impl_->route_leg_distance(to_waypoint) +
          impl_->route_leg_distance(to_destination) - direct_tail_distance;
      candidates.push_back(Candidate{detour,
                                     refuel_amount(candidate_id) > 0.0,
                                     candidate_id, std::move(to_waypoint),
                                     std::move(to_destination)});
    }
    if (candidates.empty())
      break;
    std::sort(candidates.begin(), candidates.end());
    auto &chosen = candidates.front();
    std::vector<int> next_route(route.begin(), route.begin() +
                                                  stranded_index - 1);
    next_route.insert(next_route.end(), chosen.to_waypoint.begin(),
                      chosen.to_waypoint.end());
    next_route.insert(next_route.end(), chosen.to_destination.begin() + 1,
                      chosen.to_destination.end());
    route = std::move(next_route);
  }
  return result;
}

std::size_t InterstellarLaneNetwork::cached_route_tree_count() const {
  impl_->require_owner();
  return impl_->routes.size();
}

} // namespace stellar::core
