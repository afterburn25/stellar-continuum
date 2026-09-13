#pragma once

#include <stellar/core/fleet_state.hpp>
#include <stellar/core/galaxy_catalog.hpp>

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
  std::size_t cached_route_tree_count() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace stellar::core
