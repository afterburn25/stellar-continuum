#include <stellar/core/fleet_transit.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace stellar::core {
namespace {
constexpr double base_chart_units_per_day = 0.9;
constexpr double reference_strategic_speed = 22.0;

double max_preserving_nan(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(left, right);
}
double min_preserving_nan(double left, double right) {
  return std::isnan(left) || std::isnan(right)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(left, right);
}
double clamp_preserving_nan(double value, double minimum, double maximum) {
  if (std::isnan(value))
    return value;
  return std::clamp(value, minimum, maximum);
}
float clamp_preserving_nan(float value, float minimum, float maximum) {
  if (std::isnan(value))
    return value;
  return std::clamp(value, minimum, maximum);
}
float length_squared(Vec2 value) {
  return value.x * value.x + value.y * value.y;
}
float distance_squared(Vec2 first, Vec2 second) {
  const Vec2 difference{first.x - second.x, first.y - second.y};
  return length_squared(difference);
}
float distance(Vec2 first, Vec2 second) {
  return std::sqrt(distance_squared(first, second));
}
Vec2 lerp(Vec2 first, Vec2 second, float amount) {
  // System.Numerics.Vector2.Lerp evaluates these two products before adding.
  const auto remaining = 1.0F - amount;
  return {first.x * remaining + second.x * amount,
          first.y * remaining + second.y * amount};
}
Vec2 position(const StellarSystem &system) {
  return {system.position.x, system.position.y};
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 std::optional<int> id) {
  if (!id)
    return nullptr;
  const auto found =
      std::find_if(systems.begin(), systems.end(),
                   [=](const auto &system) { return system.id == *id; });
  return found == systems.end() ? nullptr : &*found;
}
} // namespace

bool finite_fleet_chart_position(Vec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

double fleet_local_transit_rate(const FleetState &fleet) {
  return max_preserving_nan(0.1,
                            base_chart_units_per_day *
                                max_preserving_nan(0.1, fleet.strategic_speed) /
                                reference_strategic_speed);
}

Vec2 fleet_gate_towards(Vec2 source_system_position,
                        Vec2 current_system_position) {
  const Vec2 direction{source_system_position.x - current_system_position.x,
                       source_system_position.y - current_system_position.y};
  if (length_squared(direction) <= 0.000001F)
    return {fleet_local_gate_radius, 0};
  const auto length = std::sqrt(length_squared(direction));
  return {direction.x / length * fleet_local_gate_radius,
          direction.y / length * fleet_local_gate_radius};
}

void begin_fleet_local_transit(FleetState &fleet, FleetTransitPhase phase,
                               Vec2 start, Vec2 target) {
  fleet.transit_phase = phase;
  fleet.local_transit_start =
      finite_fleet_chart_position(start) ? start : Vec2{};
  fleet.local_transit_position = fleet.local_transit_start;
  fleet.local_transit_target =
      finite_fleet_chart_position(target) ? target : Vec2{};
  fleet.transit_progress = 0;
}

double advance_fleet_local_transit(FleetState &fleet, double available_days) {
  if (available_days <= 0 || !std::isfinite(available_days))
    return 0;
  const auto start = finite_fleet_chart_position(fleet.local_transit_start)
                         ? fleet.local_transit_start
                         : Vec2{};
  const auto current = finite_fleet_chart_position(fleet.local_transit_position)
                           ? fleet.local_transit_position
                           : start;
  const auto target = finite_fleet_chart_position(fleet.local_transit_target)
                          ? fleet.local_transit_target
                          : Vec2{};
  const auto total = distance(start, target);
  const auto remaining = distance(current, target);
  if (remaining <= 0.00001F) {
    fleet.local_transit_position = target;
    fleet.transit_progress = 1;
    return 0;
  }

  const auto rate = fleet_local_transit_rate(fleet);
  const auto completed =
      total <= 0.00001F
          ? 1.0F
          : clamp_preserving_nan(distance(start, current) / total, 0.0F, 1.0F);
  const auto progress = min_preserving_nan(
      1.0, completed + available_days * rate / static_cast<double>(total));
  const auto spent = (progress - completed) * total / rate;
  fleet.local_transit_position =
      lerp(start, target, static_cast<float>(progress));
  fleet.transit_progress = progress;
  return spent;
}

bool fleet_local_transit_complete(const FleetState &fleet) {
  return distance_squared(fleet.local_transit_position,
                          fleet.local_transit_target) <= 0.00000001F;
}

double fleet_local_transit_remaining_days(const FleetState &fleet) {
  if (fleet.transit_phase != FleetTransitPhase::LocalDeparture &&
      fleet.transit_phase != FleetTransitPhase::LocalArrival)
    return 0;
  return distance(fleet.local_transit_position, fleet.local_transit_target) /
         fleet_local_transit_rate(fleet);
}

double fleet_remaining_chart_distance(std::span<const StellarSystem> systems,
                                      const FleetState &fleet) {
  auto route = fleet.planned_route_system_ids;
  if (route.empty() && fleet.destination_system_id)
    route.push_back(*fleet.destination_system_id);
  if (route.empty())
    return fleet_local_transit_remaining_days(fleet) *
           fleet_local_transit_rate(fleet);

  std::unordered_map<int, const StellarSystem *> systems_by_id;
  systems_by_id.reserve(systems.size());
  for (const auto &system : systems)
    if (!systems_by_id.emplace(system.id, &system).second)
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " +
          std::to_string(system.id));
  const auto lookup = [&](int id) -> const StellarSystem * {
    const auto found = systems_by_id.find(id);
    return found == systems_by_id.end() ? nullptr : found->second;
  };
  const auto required = [&](int id) -> const StellarSystem & {
    if (const auto *system = lookup(id))
      return *system;
    throw std::out_of_range("The given key '" + std::to_string(id) +
                            "' was not present in the dictionary.");
  };
  float result =
      fleet.transit_phase == FleetTransitPhase::LocalDeparture ||
              fleet.transit_phase == FleetTransitPhase::LocalArrival
          ? distance(fleet.local_transit_position, fleet.local_transit_target)
          : 0;
  std::size_t start_index = 0;
  auto previous_id = fleet.transit_origin_system_id
                         ? fleet.transit_origin_system_id
                         : fleet.current_system_id;
  if (fleet.transit_phase == FleetTransitPhase::LocalArrival &&
      fleet.current_system_id) {
    previous_id = fleet.current_system_id;
    start_index = route.front() == *fleet.current_system_id ? 1 : 0;
  }
  if (fleet.transit_phase == FleetTransitPhase::None &&
      fleet.current_system_id) {
    const auto *current = lookup(*fleet.current_system_id);
    const auto *next = lookup(route.front());
    if (current && next) {
      result +=
          distance(fleet.local_transit_position,
                   fleet_gate_towards(position(*next), position(*current)));
      previous_id = fleet.current_system_id;
    }
  }

  for (auto index = start_index; index < route.size(); ++index) {
    const auto *origin = previous_id ? lookup(*previous_id) : nullptr;
    const auto *target = lookup(route[index]);
    if (!origin || !target)
      continue;
    const auto inbound =
        fleet_gate_towards(position(*origin), position(*target));
    const auto endpoint =
        index == route.size() - 1
            ? Vec2{}
            : fleet_gate_towards(position(required(route[index + 1])),
                                 position(*target));
    result += distance(inbound, endpoint);
    previous_id = target->id;
  }
  return result;
}

Vec2 interpolate_chart_position(const StellarSystem &origin,
                                const StellarSystem &target, double progress) {
  return lerp(position(origin), position(target),
              static_cast<float>(clamp_preserving_nan(progress, 0.0, 1.0)));
}

double interstellar_distance_from_fleet(std::span<const StellarSystem> systems,
                                        const FleetState &fleet,
                                        const StellarSystem &target) {
  const auto origin_id =
      fleet.transit_phase == FleetTransitPhase::InterstellarWarp
          ? fleet.transit_origin_system_id
          : fleet.current_system_id;
  const auto *origin = find_system(systems, origin_id);
  if ((!origin || !origin->position.depth_light_years) &&
      !target.position.depth_light_years)
    return distance(fleet.position, position(target));

  auto depth = origin ? origin->position.depth_light_years.value_or(0) : 0;
  if (fleet.transit_phase == FleetTransitPhase::InterstellarWarp) {
    const auto waypoint_id =
        !fleet.planned_route_system_ids.empty()
            ? std::optional<int>(fleet.planned_route_system_ids.front())
            : fleet.destination_system_id;
    if (const auto *waypoint = find_system(systems, waypoint_id))
      depth += (waypoint->position.depth_light_years.value_or(0) - depth) *
               clamp_preserving_nan(fleet.transit_progress, 0.0, 1.0);
  }
  const auto dx = static_cast<double>(fleet.position.x) - target.position.x;
  const auto dy = static_cast<double>(fleet.position.y) - target.position.y;
  const auto dz = depth - target.position.depth_light_years.value_or(0);
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace stellar::core
