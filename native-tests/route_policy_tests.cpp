#include <stellar/core/lane_network.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace stellar::core;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

StellarSystem system(int id, float x, float y) {
  StellarSystem result;
  result.id = id;
  result.position.x = x;
  result.position.y = y;
  return result;
}

// Complete graph over four systems:
//   1:(0,0)  2:(4,0)  3:(8,0)  4:(4,2.5)
// Leg lengths: 1-2=4, 2-3=4, 2-4=2.5, 1-4=3-4=~4.72, 1-3=8.
std::vector<StellarSystem> diamond() {
  return {system(1, 0, 0), system(2, 4, 0), system(3, 8, 0),
          system(4, 4, 2.5f)};
}

bool route_equals(const std::vector<int> &route,
                  std::initializer_list<int> expected) {
  return route == std::vector<int>(expected);
}
} // namespace

int main() {
  auto systems = diamond();
  InterstellarLaneNetwork network(&systems);
  network.build();

  // Baseline: distance-optimal route through 2.
  const auto plain = network.find_shortest_route(1, 3, 5.0);
  check(route_equals(plain, {1, 2, 3}), "baseline route via 2");

  // Policy: high cost on entering system 2 detours through 4.
  InterstellarLaneNetwork::RoutePolicy hostile;
  hostile.maximum_leg_range_light_years = 5.0;
  hostile.traversal_cost_scale = [](int id) { return id == 2 ? 10.0 : 1.0; };
  const auto avoided = network.find_shortest_route(1, 3, hostile);
  check(route_equals(avoided, {1, 4, 3}),
        "hostile traversal cost reroutes via 4");

  // Policy: blocking system 2 forces the detour even without cost scaling.
  std::unordered_set<int> blocked{2};
  InterstellarLaneNetwork::RoutePolicy blockade;
  blockade.maximum_leg_range_light_years = 5.0;
  blockade.blocked_system_ids = &blocked;
  const auto blockaded = network.find_shortest_route(1, 3, blockade);
  check(route_equals(blockaded, {1, 4, 3}),
        "blocked system routes around it");

  // Blocking every alternative produces no route.
  std::unordered_set<int> sealed{2, 4};
  blockade.blocked_system_ids = &sealed;
  check(network.find_shortest_route(1, 3, blockade).empty(),
        "fully blocked network returns empty route");

  // Fuel-aware: capacity 4 cannot finish two consecutive 4-LY legs without
  // a refuel, and no waypoint helps from fuel 0 -> infeasible.
  InterstellarLaneNetwork::FuelRouteRequest request;
  request.origin_system_id = 1;
  request.destination_system_id = 3;
  request.policy.maximum_leg_range_light_years = 5.0;
  request.fuel_capacity_light_years = 4.0;
  auto result = network.find_fuel_feasible_route(request);
  check(!result.feasible, "capacity 4 cannot strand-hop without refuel");

  // A refuel stop at 2 restores the tank: direct route becomes feasible.
  request.fuel_capacity_light_years = 5.0;
  request.refuel_light_years = [](int id) { return id == 2 ? 5.0 : 0.0; };
  result = network.find_fuel_feasible_route(request);
  check(result.feasible && route_equals(result.route_system_ids, {1, 2, 3}),
        "refuel at 2 makes the direct route feasible");
  check(std::find(result.refuel_system_ids.begin(),
                  result.refuel_system_ids.end(),
                  2) != result.refuel_system_ids.end(),
        "refuel list records system 2");

  // Refuel only at the off-path system 4: search must insert the waypoint.
  request.fuel_capacity_light_years = 7.4;
  request.refuel_light_years = [](int id) { return id == 4 ? 7.4 : 0.0; };
  result = network.find_fuel_feasible_route(request);
  check(result.feasible &&
            route_equals(result.route_system_ids, {1, 2, 4, 3}),
        "fuel-aware search inserts waypoint 4");
  check(std::find(result.refuel_system_ids.begin(),
                  result.refuel_system_ids.end(),
                  4) != result.refuel_system_ids.end(),
        "refuel list records waypoint 4");

  // Same request without any refuel source stays infeasible.
  request.refuel_light_years = nullptr;
  result = network.find_fuel_feasible_route(request);
  check(!result.feasible, "no refuel source leaves route infeasible");

  if (failures == 0)
    std::cout << "RoutePolicy and fuel-aware routing tests passed\n";
  return failures == 0 ? 0 : 1;
}
