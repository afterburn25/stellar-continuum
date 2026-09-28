#pragma once

#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace stellar::core {

inline constexpr double light_years_per_parsec = 3.26156;
inline constexpr double kilometres_per_au = 149597870.7;

double light_years_to_parsecs(double light_years) noexcept;
double au_to_kilometres(double au) noexcept;

struct InterstellarLane {
  int first_system_id{}, second_system_id{};
  double length_light_years{};

  bool connects(int system_id) const noexcept;
  int other(int system_id) const;
};

struct RemainingFleetRoute {
  int remaining_legs{};
  double distance_light_years{};
};

RemainingFleetRoute
measure_remaining_fleet_route(const std::vector<StellarSystem> *systems,
                              const FleetState *fleet);

// Owns an immutable copy of campaign geometry. Owner-thread queries only.
class InterstellarLaneNetwork {
public:
  explicit InterstellarLaneNetwork(std::span<const StellarSystem> systems);
  explicit InterstellarLaneNetwork(const std::vector<StellarSystem> *systems);
  ~InterstellarLaneNetwork();
  InterstellarLaneNetwork(InterstellarLaneNetwork &&) noexcept;
  InterstellarLaneNetwork &operator=(InterstellarLaneNetwork &&) noexcept;
  InterstellarLaneNetwork(const InterstellarLaneNetwork &) = delete;
  InterstellarLaneNetwork &operator=(const InterstellarLaneNetwork &) = delete;

  std::span<const InterstellarLane> build();
  std::vector<int>
  find_shortest_route(int origin_system_id, int destination_system_id,
                      double maximum_leg_range_light_years,
                      const std::unordered_set<int> *permitted_system_ids =
                          nullptr);

  // Extended routing policy for mission-aware search. `permitted_system_ids`
  // is a whitelist (origin/destination must be members); `blocked_system_ids`
  // is a blacklist whose members are never entered (origin/destination
  // themselves are always allowed). `traversal_cost_scale(system_id)`
  // multiplies the effective length of a leg that ENTERS that system — values
  // above 1.0 penalize hostile/dangerous territory without removing the
  // option, exactly what avoidance rules need. Routes found under a policy
  // are not shared with the plain distance cache.
  struct RoutePolicy {
    double maximum_leg_range_light_years{};
    const std::unordered_set<int> *permitted_system_ids = nullptr;
    const std::unordered_set<int> *blocked_system_ids = nullptr;
    std::function<double(int system_id)> traversal_cost_scale;
  };
  std::vector<int> find_shortest_route(int origin_system_id,
                                       int destination_system_id,
                                       const RoutePolicy &policy);

  // Fuel-aware routing: searches for a route that stays within fuel limits,
  // inserting waypoint detours (preferring systems where `refuel_light_years`
  // is positive) when the distance-optimal route would strand the fleet.
  // `refuel_light_years(system_id)` reports how much fuel stopping at that
  // system restores (e.g. colonies vs. outposts). Returns an empty route when
  // no insertion within `max_waypoint_insertions` makes the trip feasible.
  struct FuelRouteRequest {
    int origin_system_id{};
    int destination_system_id{};
    RoutePolicy policy;
    double fuel_capacity_light_years{};
    double initial_fuel_light_years{};
    std::function<double(int system_id)> refuel_light_years;
    int max_waypoint_insertions{6};
  };
  struct FuelRouteResult {
    bool feasible{};
    std::vector<int> route_system_ids;
    // System ids where the walk refuels (subset of route_system_ids).
    std::vector<int> refuel_system_ids;
  };
  FuelRouteResult find_fuel_feasible_route(const FuelRouteRequest &request);

  std::size_t cached_route_tree_count() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace stellar::core
