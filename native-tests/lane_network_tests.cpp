#include <stellar/core/lane_network.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <variant>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("Unsupported named number: " + text);
}
Json encoded_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
template <typename T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()};
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (expected.is_number() ||
      (expected.is_string() &&
       (expected == "NaN" || expected == "Infinity" ||
        expected == "-Infinity"))) {
    const auto a = number(actual);
    const auto e = number(expected);
    if (std::isnan(e)) {
      check(std::isnan(a), field + ": expected NaN");
      return;
    }
    if (std::isinf(e)) {
      check(a == e, field + ": infinity mismatch");
      return;
    }
    const auto scale = std::max({1.0, std::abs(a), std::abs(e)});
    check(std::isfinite(a) && std::abs(a - e) <= 1e-7 * scale,
          field + ": number mismatch (actual " + std::to_string(a) +
              ", expected " + std::to_string(e) + ")");
    return;
  }
  if (expected.is_array()) {
    check(actual.is_array() && actual.size() == expected.size(),
          field + ": array shape mismatch");
    for (std::size_t index = 0; index < expected.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (expected.is_object()) {
    check(actual.is_object() && actual.size() == expected.size(),
          field + ": object shape mismatch");
    for (const auto &[key, value] : expected.items()) {
      check(actual.contains(key), field + ": missing " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  check(actual == expected, field + ": value mismatch");
}

std::optional<StellarClass> parse_stellar_class(const Json &value) {
  if (value.is_null())
    return std::nullopt;
  const auto name = value.get<std::string>();
  static const std::vector<std::string> names{
      "MRedDwarf", "KOrangeDwarf", "GYellowDwarf", "FYellowWhiteDwarf",
      "AWhiteStar", "HotBlueStar", "Giant", "WhiteDwarf", "NeutronStar",
      "BlackHole", "Protostar", "Pulsar"};
  const auto found = std::find(names.begin(), names.end(), name);
  check(found != names.end(), "Unknown stellar class " + name);
  return static_cast<StellarClass>(std::distance(names.begin(), found));
}
std::string stellar_class_name(StellarClass value) {
  static const std::vector<std::string> names{
      "MRedDwarf", "KOrangeDwarf", "GYellowDwarf", "FYellowWhiteDwarf",
      "AWhiteStar", "HotBlueStar", "Giant", "WhiteDwarf", "NeutronStar",
      "BlackHole", "Protostar", "Pulsar"};
  return names.at(static_cast<std::size_t>(value));
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.position = {static_cast<float>(number(value.at("X"))),
                     static_cast<float>(number(value.at("Y"))),
                     optional<double>(value.at("GalacticDepthLightYears"))};
  check(value.at("Archetype") == "Standard", "Unknown oracle archetype");
  result.archetype = StarArchetype::Standard;
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.catalog_preset_id = optional<std::string>(value.at("CatalogPresetId"));
  result.primary = parse_stellar_class(value.at("StellarClass"));
  result.secondary = parse_stellar_class(value.at("SecondaryStellarClass"));
  result.tertiary = parse_stellar_class(value.at("TertiaryStellarClass"));
  result.stellar_catalog_id =
      optional<std::string>(value.at("StellarCatalogId"));
  return result;
}
std::optional<std::vector<StellarSystem>> parse_systems(const Json &value) {
  if (value.at("IsNull").get<bool>())
    return std::nullopt;
  std::vector<StellarSystem> result;
  for (const auto &system : value.at("Systems"))
    result.push_back(parse_system(system));
  return result;
}
Json encode_system(const StellarSystem &value) {
  const auto class_value = [](const std::optional<StellarClass> &item) {
    return item ? Json(stellar_class_name(*item)) : Json(nullptr);
  };
  return {{"Id", value.id},
          {"Name", value.name},
          {"X", encoded_number(value.position.x)},
          {"Y", encoded_number(value.position.y)},
          {"Archetype", "Standard"},
          {"HasHabitableWorld", value.has_habitable_world},
          {"HasAnomaly", value.has_anomaly},
          {"HasRareResource", value.has_rare_resource},
          {"HasPreWarpCivilization", value.has_pre_warp_civilization},
          {"CatalogPresetId", value.catalog_preset_id},
          {"StellarClass", class_value(value.primary)},
          {"SecondaryStellarClass", class_value(value.secondary)},
          {"TertiaryStellarClass", class_value(value.tertiary)},
          {"GalacticDepthLightYears", value.position.depth_light_years},
          {"StellarCatalogId", value.stellar_catalog_id}};
}
Json encode_systems(const std::optional<std::vector<StellarSystem>> &values) {
  Json systems = Json::array();
  if (values)
    for (const auto &system : *values)
      systems.push_back(encode_system(system));
  return {{"IsNull", !values}, {"Systems", systems}};
}

FleetState parse_fleet(const Json &value) {
  FleetState result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.name = value.at("Name");
  check(value.at("Role") == "Science", "Unknown oracle fleet role");
  result.role = FleetRole::Science;
  result.design_id = optional<std::string>(value.at("DesignId"));
  result.position = {static_cast<float>(number(value.at("PositionX"))),
                     static_cast<float>(number(value.at("PositionY")))};
  result.current_system_id = optional<int>(value.at("CurrentSystemId"));
  result.destination_system_id = optional<int>(value.at("DestinationSystemId"));
  const auto phase = value.at("TransitPhase").get<std::string>();
  if (phase == "InterstellarWarp")
    result.transit_phase = FleetTransitPhase::InterstellarWarp;
  else if (phase == "LocalDeparture")
    result.transit_phase = FleetTransitPhase::LocalDeparture;
  else if (phase == "LocalArrival")
    result.transit_phase = FleetTransitPhase::LocalArrival;
  else
    check(phase == "None", "Unknown oracle transit phase");
  result.transit_origin_system_id = optional<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id = optional<int>(value.at("TransitTargetSystemId"));
  result.transit_progress = number(value.at("TransitProgress"));
  result.local_transit_start = {value.at("LocalTransitStartX"), value.at("LocalTransitStartY")};
  result.local_transit_position = {value.at("LocalTransitPositionX"), value.at("LocalTransitPositionY")};
  result.local_transit_target = {value.at("LocalTransitTargetX"), value.at("LocalTransitTargetY")};
  result.planned_route_system_ids = value.at("PlannedRouteSystemIds").get<std::vector<int>>();
  result.hold_requested = value.at("HoldRequested");
  result.return_to_base_requested = value.at("ReturnToBaseRequested");
  result.return_to_base_failure_reason = optional<std::string>(value.at("ReturnToBaseFailureReason"));
  result.mission_order_revision = value.at("MissionOrderRevision");
  result.destination_planetary_body_id = optional<int>(value.at("DestinationPlanetaryBodyId"));
  result.prevent_automatic_settlement = value.at("PreventAutomaticSettlement");
  result.settlement_body_id = optional<int>(value.at("SettlementBodyId"));
  result.settlement_days_completed = number(value.at("SettlementDaysCompleted"));
  result.reconnaissance_system_id = optional<int>(value.at("ReconnaissanceSystemId"));
  result.reconnaissance_days_completed = number(value.at("ReconnaissanceDaysCompleted"));
  result.freight_target_outpost_id = optional<int>(value.at("FreightTargetOutpostId"));
  result.freight_home_colony_id = optional<int>(value.at("FreightHomeColonyId"));
  result.cargo_material_capacity = number(value.at("CargoMaterialCapacity"));
  result.cargo_materials = number(value.at("CargoMaterials"));
  result.strategic_speed = number(value.at("StrategicSpeed"));
  result.maximum_leg_range_light_years = number(value.at("MaximumLegRangeLightYears"));
  result.fuel_capacity_light_years = number(value.at("FuelCapacityLightYears"));
  result.fuel_remaining_light_years = number(value.at("FuelRemainingLightYears"));
  result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  result.is_active = value.at("IsActive");
  result.embarked_population_millions = number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id = optional<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  check(value.at("Combat").is_null() && value.at("TacticalLoadout").is_null() &&
            value.at("TacticalVessel").is_null(),
        "Oracle fleet unexpectedly contains tactical state");
  return result;
}
std::string phase_name(FleetTransitPhase value) {
  switch (value) {
  case FleetTransitPhase::LocalDeparture: return "LocalDeparture";
  case FleetTransitPhase::InterstellarWarp: return "InterstellarWarp";
  case FleetTransitPhase::LocalArrival: return "LocalArrival";
  default: return "None";
  }
}
Json encode_fleet(const FleetState &value) {
  return {{"IsNull", false}, {"Id", value.id}, {"CivilizationId", value.civilization_id},
          {"Name", value.name}, {"Role", "Science"}, {"DesignId", value.design_id},
          {"PositionX", value.position.x}, {"PositionY", value.position.y},
          {"CurrentSystemId", value.current_system_id}, {"DestinationSystemId", value.destination_system_id},
          {"TransitPhase", phase_name(value.transit_phase)}, {"TransitOriginSystemId", value.transit_origin_system_id},
          {"TransitTargetSystemId", value.transit_target_system_id}, {"TransitProgress", value.transit_progress},
          {"LocalTransitStartX", value.local_transit_start.x}, {"LocalTransitStartY", value.local_transit_start.y},
          {"LocalTransitPositionX", value.local_transit_position.x},
          {"LocalTransitPositionY", value.local_transit_position.y},
          {"LocalTransitTargetX", value.local_transit_target.x}, {"LocalTransitTargetY", value.local_transit_target.y},
          {"PlannedRouteSystemIds", value.planned_route_system_ids}, {"HoldRequested", value.hold_requested},
          {"ReturnToBaseRequested", value.return_to_base_requested},
          {"ReturnToBaseFailureReason", value.return_to_base_failure_reason},
          {"MissionOrderRevision", value.mission_order_revision},
          {"DestinationPlanetaryBodyId", value.destination_planetary_body_id},
          {"PreventAutomaticSettlement", value.prevent_automatic_settlement},
          {"SettlementBodyId", value.settlement_body_id},
          {"SettlementDaysCompleted", value.settlement_days_completed},
          {"ReconnaissanceSystemId", value.reconnaissance_system_id},
          {"ReconnaissanceDaysCompleted", value.reconnaissance_days_completed},
          {"FreightTargetOutpostId", value.freight_target_outpost_id},
          {"FreightHomeColonyId", value.freight_home_colony_id},
          {"CargoMaterialCapacity", value.cargo_material_capacity},
          {"CargoMaterials", value.cargo_materials}, {"StrategicSpeed", value.strategic_speed},
          {"MaximumLegRangeLightYears", value.maximum_leg_range_light_years},
          {"FuelCapacityLightYears", value.fuel_capacity_light_years},
          {"FuelRemainingLightYears", value.fuel_remaining_light_years}, {"SensorRange", value.sensor_range},
          {"IsActive", value.is_active}, {"EmbarkedPopulationMillions", value.embarked_population_millions},
          {"EmbarkedPopulationSpeciesId", value.embarked_population_species_id}, {"Combat", nullptr},
          {"TacticalLoadout", nullptr}, {"TacticalVessel", nullptr}};
}
Json world_view(const std::optional<std::vector<StellarSystem>> &systems,
                const std::optional<FleetState> &fleet, bool galaxy_null) {
  return {{"GalaxyIsNull", galaxy_null},
          {"Systems", galaxy_null ? Json(nullptr) : encode_systems(systems)},
          {"Fleet", fleet ? encode_fleet(*fleet) : Json{{"IsNull", true}}}};
}
Json encode_lanes(std::span<const InterstellarLane> lanes) {
  Json result = Json::array();
  for (const auto &lane : lanes)
    result.push_back({{"FirstSystemId", lane.first_system_id},
                      {"SecondSystemId", lane.second_system_id},
                      {"LengthLightYears", encoded_number(lane.length_light_years)}});
  return result;
}

struct Error { std::string type, message; };
Error classify(const std::exception_ptr &error) {
  if (!error)
    return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::out_of_range &value) {
    return {"ArgumentOutOfRangeException", value.what()};
  } catch (const std::invalid_argument &value) {
    const std::string message = value.what();
    return {message.starts_with("Value cannot be null.") ?
                "ArgumentNullException" : "ArgumentException", message};
  } catch (const std::exception &value) {
    return {"InvalidOperationException", value.what()};
  }
}
void check_error(const Error &actual, const Json &expected,
                 const std::string &name) {
  if (expected.is_null()) {
    check(actual.type.empty(), name + ": unexpected " + actual.type + ": " + actual.message);
  } else {
    check(actual.type == expected.at("Type").get<std::string>(), name + ": error type");
    check(actual.message == expected.at("Message").get<std::string>(), name + ": error message");
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  const auto arguments = test.at("Arguments");
  const auto expected_result = test.at("Result");
  const auto source_error = test.at("Error");
  const auto expected_error =
      test.contains("NativeBoundary") && !test.at("NativeBoundary").is_null()
          ? test.at("NativeBoundary")
          : source_error;
  const auto expected_before = test.at("Before");
  const auto expected_after = test.at("After");

  if (kind == "LightYearsToParsecs" || kind == "AuToKilometres") {
    const auto value = number(arguments.at("Value"));
    equal_json(Json{{"Value", encoded_number(value)}}, expected_before, name + ".Before");
    double result{}; std::exception_ptr error;
    try { result = kind == "LightYearsToParsecs" ? light_years_to_parsecs(value) : au_to_kilometres(value); }
    catch (...) { error = std::current_exception(); }
    check_error(classify(error), expected_error, name);
    if (!error) equal_json(Json{{"Value", encoded_number(result)}}, expected_result, name + ".Result");
    equal_json(Json{{"Value", encoded_number(value)}}, expected_after, name + ".After");
    return;
  }
  if (kind == "LaneConnects" || kind == "LaneOther") {
    const auto lane_json = arguments.at("Lane");
    const InterstellarLane lane{lane_json.at("FirstSystemId"),
                                lane_json.at("SecondSystemId"),
                                number(lane_json.at("LengthLightYears"))};
    const int system_id = arguments.at("SystemId");
    equal_json(encode_lanes(std::span{&lane, std::size_t{1}}).front(), expected_before, name + ".Before");
    bool bool_result{}; int int_result{}; std::exception_ptr error;
    try { if (kind == "LaneConnects") bool_result = lane.connects(system_id); else int_result = lane.other(system_id); }
    catch (...) { error = std::current_exception(); }
    check_error(classify(error), expected_error, name);
    if (!error)
      equal_json(kind == "LaneConnects" ? Json(bool_result) : Json(int_result),
                 expected_result, name + ".Result");
    equal_json(encode_lanes(std::span{&lane, std::size_t{1}}).front(), expected_after, name + ".After");
    return;
  }
  if (kind == "Build") {
    auto systems = parse_systems(arguments.at("Systems"));
    equal_json(encode_systems(systems), expected_before, name + ".Before");
    std::vector<InterstellarLane> result; std::exception_ptr error;
    try {
      InterstellarLaneNetwork network(systems ? &*systems : nullptr);
      const auto lanes = network.build();
      result.assign(lanes.begin(), lanes.end());
    } catch (...) {
      error = std::current_exception();
    }
    check_error(classify(error), expected_error, name);
    if (!error) equal_json(encode_lanes(result), expected_result, name + ".Result");
    equal_json(encode_systems(systems), expected_after, name + ".After");
    return;
  }
  if (kind == "BuildCacheMutation") {
    auto systems = *parse_systems(arguments.at("Systems"));
    const auto replacement_index = arguments.at("ReplacementIndex").get<std::size_t>();
    const auto replacement = parse_system(arguments.at("Replacement"));
    equal_json(encode_systems(systems), expected_before, name + ".Before");
    std::vector<InterstellarLane> first, repeat, changed; bool same_before{}, same_after{}; std::exception_ptr error;
    try {
      InterstellarLaneNetwork network(&systems);
      const auto first_view = network.build(); first.assign(first_view.begin(), first_view.end());
      const auto repeat_view = network.build(); repeat.assign(repeat_view.begin(), repeat_view.end());
      same_before = first_view.data() == repeat_view.data();
      systems.at(replacement_index) = replacement;
      network = InterstellarLaneNetwork(&systems);
      const auto changed_view = network.build(); changed.assign(changed_view.begin(), changed_view.end());
      same_after = false;
    } catch (...) { error = std::current_exception(); }
    check_error(classify(error), expected_error, name);
    if (!error) equal_json(Json{{"First", encode_lanes(first)}, {"Repeat", encode_lanes(repeat)},
      {"SameReferenceBeforeReplacement", same_before}, {"Changed", encode_lanes(changed)},
      {"SameReferenceAfterReplacement", same_after}}, expected_result, name + ".Result");
    equal_json(encode_systems(systems), expected_after, name + ".After");
    return;
  }
  if (kind == "FindShortestRoute") {
    auto systems = parse_systems(arguments.at("Systems"));
    const int origin = arguments.at("OriginSystemId"), destination = arguments.at("DestinationSystemId");
    const auto range = number(arguments.at("MaximumLegRangeLightYears"));
    std::optional<std::unordered_set<int>> permitted;
    if (!arguments.at("PermittedSystemIds").is_null()) {
      const auto ids =
          arguments.at("PermittedSystemIds").get<std::vector<int>>();
      permitted.emplace(ids.begin(), ids.end());
    }
    equal_json(encode_systems(systems), expected_before, name + ".Before");
    std::vector<int> result; std::exception_ptr error;
    try {
      InterstellarLaneNetwork network(systems ? &*systems : nullptr);
      result = network.find_shortest_route(origin, destination, range,
                                           permitted ? &*permitted : nullptr);
    } catch (...) { error = std::current_exception(); }
    check_error(classify(error), expected_error, name);
    if (!error) equal_json(result, expected_result, name + ".Result");
    equal_json(encode_systems(systems), expected_after, name + ".After");
    return;
  }
  if (kind == "FindShortestRoutePermissionMutation" || kind == "FindShortestRouteRangeSequence") {
    auto systems = *parse_systems(arguments.at("Systems"));
    const int origin = arguments.at("OriginSystemId"), destination = arguments.at("DestinationSystemId");
    std::exception_ptr error; Json actual;
    if (kind == "FindShortestRoutePermissionMutation") {
      auto permitted_values = arguments.at("InitialPermittedSystemIds").get<std::vector<int>>();
      std::unordered_set<int> permitted(permitted_values.begin(), permitted_values.end());
      const auto range = number(arguments.at("MaximumLegRangeLightYears"));
      const auto removed_ids =
          arguments.at("RemovedSystemIds").get<std::vector<int>>();
      equal_json(Json{{"Systems", encode_systems(systems)},
                      {"PermittedSystemIds", permitted_values}},
                 expected_before, name + ".Before");
      std::vector<int> first, second;
      std::size_t cached_route_count{};
      try { InterstellarLaneNetwork network(&systems);
        first = network.find_shortest_route(origin, destination, range, &permitted);
        for (int id : removed_ids) permitted.erase(id);
        second = network.find_shortest_route(origin, destination, range, &permitted);
        cached_route_count = network.cached_route_tree_count();
      } catch (...) { error = std::current_exception(); }
      check_error(classify(error), expected_error, name);
      if (!error) {
        check(cached_route_count == 0,
              name + ": permission-specific query polluted unrestricted cache");
        equal_json(Json{{"First", first}, {"Second", second}},
                   expected_result, name + ".Result");
      }
      auto after_ids = std::vector<int>(permitted.begin(), permitted.end());
      std::sort(after_ids.begin(), after_ids.end());
      equal_json(Json{{"Systems", encode_systems(systems)},
                      {"PermittedSystemIds", after_ids}},
                 expected_after, name + ".After");
    } else {
      std::vector<double> ranges;
      for (const auto &range_json :
           arguments.at("MaximumLegRangesLightYears"))
        ranges.push_back(number(range_json));
      equal_json(encode_systems(systems), expected_before, name + ".Before");
      struct RangeResult { double range; std::vector<int> path; }; std::vector<RangeResult> results;
      std::size_t cached_route_count{};
      try { InterstellarLaneNetwork network(&systems); for (const auto range : ranges) {
        results.push_back({range, network.find_shortest_route(origin, destination, range)}); }
        cached_route_count = network.cached_route_tree_count();
      } catch (...) { error = std::current_exception(); }
      check_error(classify(error), expected_error, name);
      if (!error) {
        check(cached_route_count == 3,
              name + ": exact range cache did not reuse repeated key");
        actual = Json::array();
        for (const auto &item : results)
          actual.push_back({{"Range", encoded_number(item.range)},
                            {"Path", item.path}});
        equal_json(actual, expected_result, name + ".Result");
      }
      equal_json(encode_systems(systems), expected_after, name + ".After");
    }
    return;
  }
  if (kind == "FleetRouteMetricsMeasure") {
    const auto world = arguments.at("World");
    const bool galaxy_null = world.at("GalaxyIsNull");
    std::optional<std::vector<StellarSystem>> systems;
    if (!galaxy_null) systems = parse_systems(world.at("Systems"));
    std::optional<FleetState> fleet;
    if (!world.at("Fleet").at("IsNull").get<bool>()) fleet = parse_fleet(world.at("Fleet"));
    equal_json(world_view(systems, fleet, galaxy_null), expected_before, name + ".Before");
    RemainingFleetRoute result; std::exception_ptr error;
    try { result = measure_remaining_fleet_route(galaxy_null ? nullptr : &*systems, fleet ? &*fleet : nullptr); }
    catch (...) { error = std::current_exception(); }
    check_error(classify(error), expected_error, name);
    if (!error)
      equal_json(Json{{"RemainingLegs", result.remaining_legs},
                      {"DistanceLightYears",
                       encoded_number(result.distance_light_years)}},
                 expected_result, name + ".Result");
    equal_json(world_view(systems, fleet, galaxy_null), expected_after, name + ".After");
    return;
  }
  fail(name + ": unknown kind " + kind);
}

void smoke() {
  constexpr int count = 2500;
  std::vector<StellarSystem> systems;
  systems.reserve(count);
  for (int id = 0; id < count; ++id) {
    const auto angle = static_cast<float>(id) * 0.137f;
    StellarSystem system; system.id = id; system.name = "Smoke " + std::to_string(id);
    system.position = {std::cos(angle) * (20.0f + id * .03f),
                       std::sin(angle) * (20.0f + id * .03f),
                       static_cast<double>(id % 17)};
    systems.push_back(std::move(system));
  }
  const auto start = std::chrono::steady_clock::now();
  InterstellarLaneNetwork network(&systems);
  const auto lanes = network.build();
  check(lanes.size() >= static_cast<std::size_t>(count - 1), "smoke: graph lacks connected backbone");
  std::vector<std::vector<int>> adjacency(count);
  for (const auto &lane : lanes) {
    adjacency.at(static_cast<std::size_t>(lane.first_system_id))
        .push_back(lane.second_system_id);
    adjacency.at(static_cast<std::size_t>(lane.second_system_id))
        .push_back(lane.first_system_id);
  }
  std::vector<bool> visited(count);
  std::vector<int> pending{0};
  visited.front() = true;
  while (!pending.empty()) {
    const int current = pending.back();
    pending.pop_back();
    for (const int next : adjacency.at(static_cast<std::size_t>(current)))
      if (!visited.at(static_cast<std::size_t>(next))) {
        visited.at(static_cast<std::size_t>(next)) = true;
        pending.push_back(next);
      }
  }
  check(std::ranges::all_of(visited, [](bool value) { return value; }),
        "smoke: emitted graph is disconnected");
  std::vector<StellarSystem> small(systems.begin(), systems.begin() + 70);
  InterstellarLaneNetwork cache_network(&small);
  for (int origin = 0; origin < 65; ++origin)
    (void)cache_network.find_shortest_route(origin, 69, 1000.0 + origin);
  check(cache_network.cached_route_tree_count() == 1, "smoke: 64-tree clear-before-add policy mismatch");
  std::vector<StellarSystem> negative_ids{systems[0], systems[1]};
  negative_ids[0].id = -1;
  negative_ids[1].id = -7;
  InterstellarLaneNetwork negative_network(&negative_ids);
  check(negative_network.build().size() == 1,
        "smoke: negative ID collided with graph bookkeeping");
  std::vector<StellarSystem> invalid_geometry{systems[0], systems[1]};
  invalid_geometry[1].position.x = std::numeric_limits<float>::quiet_NaN();
  InterstellarLaneNetwork invalid_network(&invalid_geometry);
  bool invalid_rejected = false;
  try {
    (void)invalid_network.build();
  } catch (const std::invalid_argument &error) {
    invalid_rejected = std::string(error.what()) ==
                       "Star position must contain finite light-year coordinates";
  }
  check(invalid_rejected,
        "smoke: nonfinite imported geometry was not rejected cleanly");
  std::vector<StellarSystem> duplicate_ids{systems[0], systems[1]};
  duplicate_ids[1].id = duplicate_ids[0].id;
  InterstellarLaneNetwork duplicate_network(&duplicate_ids);
  check(duplicate_network.find_shortest_route(99, 100, 0).empty(),
        "smoke: invalid range did not precede lazy geometry validation");
  bool duplicate_rejected = false;
  try {
    (void)duplicate_network.build();
  } catch (const std::invalid_argument &) {
    duplicate_rejected = true;
  }
  check(duplicate_rejected,
        "smoke: duplicate geometry survived a later valid build request");
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  std::cout << "lane_network_tests: 2500-node connectedness/cache smoke " << elapsed.count() << " ms\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected lane-network fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open lane-network fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format") == "stellar-lane-network-oracle-v1", "Unknown lane-network fixture format");
    for (const auto &test : fixture.at("Cases")) run_case(test);
    smoke();
    std::cout << "lane_network_tests: passed " << fixture.at("Cases").size() << " actual-C# cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "lane_network_tests failed: " << error.what() << '\n';
    return 1;
  }
}
