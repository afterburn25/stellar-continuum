#include <stellar/core/fleet_reach.hpp>

#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/fleet_transit.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace stellar::core {
namespace {

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}

bool blank_reason(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

std::string group_integer(std::string value) {
  for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(value.size()) - 3;
       index > 0; index -= 3)
    value.insert(static_cast<std::size_t>(index), ",");
  return value;
}

std::string superscript(int value) {
  static constexpr const char *digits[] = {"⁰", "¹", "²", "³", "⁴",
                                           "⁵", "⁶", "⁷", "⁸", "⁹"};
  if (value == 0)
    return digits[0];
  std::string result;
  std::int64_t magnitude = value;
  if (magnitude < 0) {
    result = "⁻";
    magnitude = -magnitude;
  }
  const auto ordinary = std::to_string(magnitude);
  for (const auto digit : ordinary)
    result += digits[digit - '0'];
  return result;
}

std::string scientific(double value) {
  if (!std::isfinite(value))
    throw std::invalid_argument(
        "Metric conversion exceeds the native formatting range.");
  if (value < 1'000'000.0) {
    // Invariant N0 uses midpoint-to-even rounding. This branch is bounded below
    // one million, so the integer conversion is representable.
    const auto integral = std::floor(value);
    const auto fraction = value - integral;
    auto rounded = static_cast<std::uint64_t>(integral);
    if (fraction > 0.5 || (fraction == 0.5 && rounded % 2 != 0))
      ++rounded;
    if (rounded == 0 && std::signbit(value))
      return "-0";
    return group_integer(std::to_string(rounded));
  }
  const auto exponent_value = std::floor(std::log10(value));
  if (!std::isfinite(exponent_value) ||
      exponent_value < std::numeric_limits<int>::min() ||
      exponent_value > std::numeric_limits<int>::max())
    throw std::invalid_argument(
        "Metric conversion exceeds the native formatting range.");
  const auto exponent = static_cast<int>(exponent_value);
  const auto coefficient = value / std::pow(10.0, exponent);
  return detail::legacy_custom_fixed(coefficient, 0, 3) + " × 10" +
         superscript(exponent);
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 int id) {
  const auto found =
      std::find_if(systems.begin(), systems.end(),
                   [id](const auto &system) { return system.id == id; });
  return found == systems.end() ? nullptr : &*found;
}

void ensure_revision_available(const FleetState &fleet) {
  if (fleet.mission_order_revision == std::numeric_limits<int>::max())
    throw std::overflow_error("Fleet mission revision space is exhausted.");
}

} // namespace

MissionReachAssessment supported_mission_reach(std::string reason) {
  return {true, true, std::move(reason), std::nullopt, 0.0};
}

MissionReachAssessment unsupported_mission_reach(std::string reason) {
  if (blank_reason(reason))
    reason = "Mission is beyond current operational reach.";
  return {false, true, std::move(reason), std::nullopt, 0.0};
}

MissionReachAssessment provisional_supported_mission_reach(std::string reason) {
  return {true, false, std::move(reason), std::nullopt, 0.0};
}

std::string format_interstellar_metric_primary(double light_years) {
  if (!std::isfinite(light_years) || light_years < 0.0)
    return "Distance unconfirmed";
  return scientific(light_years * kilometres_per_light_year) + " km · " +
         detail::legacy_custom_fixed(light_years, 0, 1) + " ly";
}

std::string format_interstellar_metric_speed(double light_years_per_day) {
  if (!std::isfinite(light_years_per_day) || light_years_per_day < 0.0)
    return "Speed unconfirmed";
  return scientific(light_years_per_day * kilometres_per_light_year) +
         " km/day · " + detail::legacy_custom_fixed(light_years_per_day, 0, 1) +
         " ly/day";
}

MissionReachAssessment
assess_operational_reach(OperationalReachWorldView world, int civilization_id,
                         const FleetState &fleet, int target_system_id,
                         InterstellarMissionKind /*mission_kind*/) {
  if (fleet.civilization_id != civilization_id)
    return unsupported_mission_reach(
        "That fleet is not controlled by this civilization.");
  if (!find_system(world.systems, target_system_id))
    return unsupported_mission_reach("Unknown mission target.");
  if (!fleet.current_system_id)
    return unsupported_mission_reach(
        "The fleet must finish its current lane leg before receiving a new "
        "interstellar route.");

  auto route = world.lanes.find_shortest_route(
      *fleet.current_system_id, target_system_id,
      fleet.maximum_leg_range_light_years);
  if (route.empty())
    return unsupported_mission_reach(
        "No connected lane route is available within this fleet's " +
        format_interstellar_metric_primary(
            fleet.maximum_leg_range_light_years) +
        " maximum leg range.");

  std::unordered_map<int, const StellarSystem *> systems;
  systems.reserve(world.systems.size());
  for (const auto &system : world.systems)
    if (!systems.emplace(system.id, &system).second)
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " +
          std::to_string(system.id));

  std::unordered_map<int, double> refueling;
  for (const auto &colony : world.colonies) {
    if (colony.civilization_id != civilization_id)
      continue;
    auto &service = refueling[colony.system_id];
    service =
        std::max(service, colony.kind == SettlementKind::Colony ? 1.0 : 0.5);
  }
  auto fuel = fleet.fuel_remaining_light_years;
  if (const auto service = refueling.find(*fleet.current_system_id);
      service != refueling.end())
    fuel = fleet.fuel_capacity_light_years * service->second;

  double distance = 0.0;
  for (std::size_t index = 1; index < route.size(); ++index) {
    const auto first = systems.at(route[index - 1]);
    const auto second = systems.at(route[index]);
    const auto leg = distance_light_years(first->position, second->position);
    if (leg > fuel + 1e-9)
      return unsupported_mission_reach(
          "Insufficient fuel endurance for the lane into " + second->name +
          ": " + format_interstellar_metric_primary(leg) + " required, " +
          format_interstellar_metric_primary(fuel) +
          " available before refueling.");
    fuel -= leg;
    distance += leg;
    if (const auto service = refueling.find(second->id);
        service != refueling.end())
      fuel = fleet.fuel_capacity_light_years * service->second;
  }

  const auto legs = static_cast<int>(route.size() - 1);
  auto reason = legs == 0
                    ? std::string("The fleet is already in the target system.")
                    : "Route: " + std::to_string(legs) + " lane leg" +
                          (legs == 1 ? "" : "s") + ", " +
                          format_interstellar_metric_primary(distance) +
                          " total; maximum leg " +
                          format_interstellar_metric_primary(
                              fleet.maximum_leg_range_light_years) +
                          "; projected fuel reserve " +
                          format_interstellar_metric_primary(fuel) + ".";
  return {true, true, std::move(reason), std::move(route), distance};
}

void assign_fleet_route(OperationalReachWorldView world, FleetState &fleet,
                        int final_destination_system_id,
                        const MissionReachAssessment &reach) {
  if (!reach.is_supported)
    throw std::runtime_error("A fleet route can only be assigned from a "
                             "supported reach assessment.");

  auto route = reach.route_system_ids;
  if (!route && fleet.current_system_id)
    route = world.lanes.find_shortest_route(
        *fleet.current_system_id, final_destination_system_id,
        fleet.maximum_leg_range_light_years);
  if (!route || route->empty())
    route = std::vector<int>{final_destination_system_id};
  ensure_revision_available(fleet);

  fleet.destination_system_id = final_destination_system_id;
  ++fleet.mission_order_revision;
  fleet.hold_requested = false;
  fleet.return_to_base_requested = false;
  fleet.return_to_base_failure_reason.reset();
  fleet.planned_route_system_ids.clear();
  std::copy_if(route->begin(), route->end(),
               std::back_inserter(fleet.planned_route_system_ids), [&](int id) {
                 return !fleet.current_system_id ||
                        id != *fleet.current_system_id;
               });

  if (fleet.current_system_id &&
      (fleet.transit_phase == FleetTransitPhase::LocalDeparture ||
       fleet.transit_phase == FleetTransitPhase::LocalArrival)) {
    const auto current = find_system(world.systems, *fleet.current_system_id);
    if (!current)
      return;
    const auto next_id = fleet.planned_route_system_ids.empty()
                             ? final_destination_system_id
                             : fleet.planned_route_system_ids.front();
    const auto next = find_system(world.systems, next_id);
    if (!next)
      return;
    fleet.transit_origin_system_id = current->id;
    fleet.transit_target_system_id = next->id;
    begin_fleet_local_transit(
        fleet, FleetTransitPhase::LocalDeparture, fleet.local_transit_position,
        fleet_gate_towards(Vec2{next->position.x, next->position.y},
                           Vec2{current->position.x, current->position.y}));
  }
}

void clear_fleet_route(FleetState &fleet) {
  ensure_revision_available(fleet);
  fleet.destination_system_id.reset();
  ++fleet.mission_order_revision;
  fleet.hold_requested = false;
  fleet.return_to_base_requested = false;
  fleet.return_to_base_failure_reason.reset();
  fleet.planned_route_system_ids.clear();
  if (fleet.current_system_id &&
      fleet.transit_phase != FleetTransitPhase::None) {
    begin_fleet_local_transit(fleet, FleetTransitPhase::LocalArrival,
                              fleet.local_transit_position, Vec2{});
    fleet.transit_target_system_id.reset();
  } else if (fleet.transit_phase != FleetTransitPhase::InterstellarWarp) {
    fleet.transit_phase = FleetTransitPhase::None;
    fleet.transit_origin_system_id.reset();
    fleet.transit_target_system_id.reset();
    fleet.transit_progress = 0.0;
  }
}

} // namespace stellar::core
