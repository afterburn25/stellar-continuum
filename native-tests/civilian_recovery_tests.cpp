#include <stellar/core/civilian_recovery.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

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
  if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity") return std::numeric_limits<double>::infinity();
  if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
  fail("Unsupported named number " + text);
}
Json encoded_number(double value) {
  if (std::isnan(value)) return "NaN";
  if (value == std::numeric_limits<double>::infinity()) return "Infinity";
  if (value == -std::numeric_limits<double>::infinity()) return "-Infinity";
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
    const auto a = number(actual), e = number(expected);
    if (std::isnan(e)) { check(std::isnan(a), field + ": expected NaN"); return; }
    if (std::isinf(e)) { check(a == e, field + ": infinity mismatch"); return; }
    const auto scale = std::max({1.0, std::abs(a), std::abs(e)});
    check(std::isfinite(a) && std::abs(a - e) <= 1e-7 * scale,
          field + ": number mismatch");
    return;
  }
  if (expected.is_array()) {
    check(actual.is_array() && actual.size() == expected.size(),
          field + ": array shape mismatch");
    for (std::size_t i = 0; i < expected.size(); ++i)
      equal_json(actual[i], expected[i], field + "[" + std::to_string(i) + "]");
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

Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
Json encode_vec2(Vec2 value) {
  return {{"X", encoded_number(value.x)}, {"Y", encoded_number(value.y)}};
}
StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id"); result.name = value.at("Name");
  const auto position = parse_vec2(value.at("Position"));
  result.position = {position.x, position.y,
                     optional<double>(value.at("GalacticDepthLightYears"))};
  result.archetype = static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.catalog_preset_id = optional<std::string>(value.at("CatalogPresetId"));
  result.stellar_catalog_id = optional<std::string>(value.at("StellarCatalogId"));
  check(value.at("StellarClass").is_null() &&
            value.at("SecondaryStellarClass").is_null() &&
            value.at("TertiaryStellarClass").is_null(),
        "Fixture stellar classes are outside this recovery matrix");
  return result;
}
Json encode_system(const StellarSystem &value) {
  return {{"Id", value.id}, {"Name", value.name},
          {"Position", encode_vec2({value.position.x, value.position.y})},
          {"Archetype", static_cast<int>(value.archetype)},
          {"HasHabitableWorld", value.has_habitable_world},
          {"HasAnomaly", value.has_anomaly},
          {"HasRareResource", value.has_rare_resource},
          {"HasPreWarpCivilization", value.has_pre_warp_civilization},
          {"CatalogPresetId", value.catalog_preset_id},
          {"StellarClass", nullptr}, {"SecondaryStellarClass", nullptr},
          {"TertiaryStellarClass", nullptr},
          {"GalacticDepthLightYears", value.position.depth_light_years},
          {"StellarCatalogId", value.stellar_catalog_id}};
}
Colony parse_colony(const Json &value) {
  Colony result;
  result.id = value.at("Id"); result.civilization_id = value.at("CivilizationId");
  result.system_id = value.at("SystemId");
  result.planetary_body_id = optional<int>(value.at("PlanetaryBodyId"));
  result.name = value.at("Name");
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id = value.at("PopulationSpeciesId");
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials = number(value.at("StoredExtractedMaterials"));
  result.remaining_extractable_materials =
      optional<double>(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = value.at("SurfaceHubLevel");
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  check(value.at("SurfaceBuildings").empty(), "Surface buildings outside fixture boundary");
  return result;
}
Json encode_colony(const Colony &value) {
  return {{"Id", value.id}, {"CivilizationId", value.civilization_id},
          {"SystemId", value.system_id},
          {"PlanetaryBodyId", value.planetary_body_id}, {"Name", value.name},
          {"Kind", static_cast<int>(value.kind)},
          {"PopulationSpeciesId", value.population_species_id},
          {"PopulationMillions", encoded_number(value.population_millions)},
          {"Infrastructure", encoded_number(value.infrastructure)},
          {"Stability", encoded_number(value.stability)},
          {"StoredFoodPopulationDaysMillions",
           encoded_number(value.stored_food_population_days_millions)},
          {"StoredWaterPopulationDaysMillions",
           encoded_number(value.stored_water_population_days_millions)},
          {"StoredExtractedMaterials",
           encoded_number(value.stored_extracted_materials)},
          {"RemainingExtractableMaterials", value.remaining_extractable_materials},
          {"SurfaceHubLevel", value.surface_hub_level},
          {"SurfaceHubUpgradeDaysRemaining",
           encoded_number(value.surface_hub_upgrade_days_remaining)},
          {"SurfaceBuildings", Json::array()}};
}
FleetState parse_fleet(const Json &value) {
  FleetState result;
  result.id = value.at("Id"); result.civilization_id = value.at("CivilizationId");
  result.name = value.at("Name");
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional<std::string>(value.at("DesignId"));
  result.position = parse_vec2(value.at("Position"));
  result.current_system_id = optional<int>(value.at("CurrentSystemId"));
  result.destination_system_id = optional<int>(value.at("DestinationSystemId"));
  result.transit_phase = static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  result.transit_origin_system_id = optional<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id = optional<int>(value.at("TransitTargetSystemId"));
  result.transit_progress = number(value.at("TransitProgress"));
  result.local_transit_start = parse_vec2(value.at("LocalTransitStart"));
  result.local_transit_position = parse_vec2(value.at("LocalTransitPosition"));
  result.local_transit_target = parse_vec2(value.at("LocalTransitTarget"));
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
        "Tactical fixture state is unsupported");
  return result;
}
Json encode_fleet(const FleetState &value) {
  return {{"Id", value.id}, {"CivilizationId", value.civilization_id},
          {"Name", value.name}, {"Role", static_cast<int>(value.role)},
          {"DesignId", value.design_id}, {"Position", encode_vec2(value.position)},
          {"CurrentSystemId", value.current_system_id},
          {"DestinationSystemId", value.destination_system_id},
          {"TransitPhase", static_cast<int>(value.transit_phase)},
          {"TransitOriginSystemId", value.transit_origin_system_id},
          {"TransitTargetSystemId", value.transit_target_system_id},
          {"TransitProgress", encoded_number(value.transit_progress)},
          {"LocalTransitStart", encode_vec2(value.local_transit_start)},
          {"LocalTransitPosition", encode_vec2(value.local_transit_position)},
          {"LocalTransitTarget", encode_vec2(value.local_transit_target)},
          {"PlannedRouteSystemIds", value.planned_route_system_ids},
          {"HoldRequested", value.hold_requested},
          {"ReturnToBaseRequested", value.return_to_base_requested},
          {"ReturnToBaseFailureReason", value.return_to_base_failure_reason},
          {"MissionOrderRevision", value.mission_order_revision},
          {"DestinationPlanetaryBodyId", value.destination_planetary_body_id},
          {"PreventAutomaticSettlement", value.prevent_automatic_settlement},
          {"SettlementBodyId", value.settlement_body_id},
          {"SettlementDaysCompleted", encoded_number(value.settlement_days_completed)},
          {"ReconnaissanceSystemId", value.reconnaissance_system_id},
          {"ReconnaissanceDaysCompleted", encoded_number(value.reconnaissance_days_completed)},
          {"FreightTargetOutpostId", value.freight_target_outpost_id},
          {"FreightHomeColonyId", value.freight_home_colony_id},
          {"CargoMaterialCapacity", encoded_number(value.cargo_material_capacity)},
          {"CargoMaterials", encoded_number(value.cargo_materials)},
          {"StrategicSpeed", encoded_number(value.strategic_speed)},
          {"MaximumLegRangeLightYears", encoded_number(value.maximum_leg_range_light_years)},
          {"FuelCapacityLightYears", encoded_number(value.fuel_capacity_light_years)},
          {"FuelRemainingLightYears", encoded_number(value.fuel_remaining_light_years)},
          {"SensorRange", encoded_number(value.sensor_range)},
          {"IsActive", value.is_active},
          {"EmbarkedPopulationMillions", encoded_number(value.embarked_population_millions)},
          {"EmbarkedPopulationSpeciesId", value.embarked_population_species_id},
          {"Combat", nullptr}, {"TacticalLoadout", nullptr},
          {"TacticalVessel", nullptr}};
}

struct World {
  bool is_null{};
  std::vector<StellarSystem> systems;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
};
World parse_world(const Json &value) {
  World result;
  result.is_null = value.at("IsNull");
  if (result.is_null) return result;
  for (const auto &system : value.at("Systems"))
    result.systems.push_back(parse_system(system));
  for (const auto &colony : value.at("Colonies"))
    result.colonies.push_back(parse_colony(colony));
  for (const auto &fleet : value.at("Fleets"))
    result.fleets.push_back(parse_fleet(fleet));
  return result;
}
Json encode_world(const World &world) {
  if (world.is_null) return {{"IsNull", true}};
  Json systems = Json::array(), colonies = Json::array(), fleets = Json::array();
  for (const auto &system : world.systems) systems.push_back(encode_system(system));
  for (const auto &colony : world.colonies) colonies.push_back(encode_colony(colony));
  for (const auto &fleet : world.fleets) fleets.push_back(encode_fleet(fleet));
  return {{"IsNull", false}, {"Systems", systems},
          {"Colonies", colonies}, {"Fleets", fleets}};
}

enum class Operation { hold, resume, preview, request, activate, abandon };
struct Command {
  Operation operation{};
  Json source;
  int civilization_id{}, fleet_id{}, fleet_index{};
  bool confirm{}, null_galaxy{}, null_fleet{};
};
Command parse_command(const Json &value) {
  Command result;
  result.source = value;
  const auto operation = value.at("Operation").get<std::string>();
  if (operation == "Hold") result.operation = Operation::hold;
  else if (operation == "Resume") result.operation = Operation::resume;
  else if (operation == "PreviewReturn") result.operation = Operation::preview;
  else if (operation == "RequestReturn") result.operation = Operation::request;
  else if (operation == "ActivateQueuedReturnAtSystem") result.operation = Operation::activate;
  else if (operation == "AbandonMissionForTransit") result.operation = Operation::abandon;
  else fail("Unknown recovery command " + operation);
  result.civilization_id = value.at("CivilizationId");
  result.fleet_id = value.at("FleetId");
  result.confirm = value.at("Confirm");
  result.fleet_index = value.at("FleetIndex");
  result.null_galaxy = value.at("NullGalaxy");
  result.null_fleet = value.at("NullFleet");
  return result;
}

struct Error { std::string type, message; };
Error classify(const std::exception_ptr &error) {
  if (!error) return {};
  try { std::rethrow_exception(error); }
  catch (const std::overflow_error &value) { return {"OverflowError", value.what()}; }
  catch (const std::out_of_range &value) { return {"ArgumentOutOfRangeException", value.what()}; }
  catch (const std::invalid_argument &value) {
    const std::string message = value.what();
    return {message.starts_with("Value cannot be null.") ? "ArgumentNullException" : "ArgumentException", message};
  }
  catch (const std::exception &value) { return {"InvalidOperationException", value.what()}; }
}
using Outcome = std::variant<std::monostate, CivilianFleetHoldOrderResult,
                             CivilianFleetReturnOrderResult>;
Json encode_outcome(const Outcome &outcome) {
  if (std::holds_alternative<std::monostate>(outcome)) return nullptr;
  if (const auto *hold = std::get_if<CivilianFleetHoldOrderResult>(&outcome))
    return {{"Accepted", hold->accepted}, {"Message", hold->message}};
  const auto &value = std::get<CivilianFleetReturnOrderResult>(outcome);
  return {{"Accepted", value.accepted},
          {"RequiresConfirmation", value.requires_confirmation},
          {"Message", value.message}};
}

struct Execution { Outcome outcome; std::exception_ptr error; };
Execution invoke(World &world, InterstellarLaneNetwork &lanes,
                 const Command &command) {
  Execution result;
  try {
    CivilianRecoveryWorldView view{world.systems, world.colonies, world.fleets,
                                    lanes};
    switch (command.operation) {
    case Operation::hold:
      result.outcome = hold_civilian_fleet(view, command.civilization_id,
                                           command.fleet_id); break;
    case Operation::resume:
      result.outcome = resume_civilian_fleet(view, command.civilization_id,
                                             command.fleet_id); break;
    case Operation::preview:
      result.outcome = preview_civilian_fleet_return(
          view, command.civilization_id, command.fleet_id); break;
    case Operation::request:
      result.outcome = request_civilian_fleet_return(
          view, command.civilization_id, command.fleet_id, command.confirm); break;
    case Operation::activate:
      result.outcome = activate_queued_civilian_return_at_system(
          view, world.fleets.at(static_cast<std::size_t>(command.fleet_index))); break;
    case Operation::abandon:
      abandon_colony_mission_for_transit(
          command.null_fleet ? nullptr :
              &world.fleets.at(static_cast<std::size_t>(command.fleet_index)));
      result.outcome = std::monostate{}; break;
    }
  } catch (...) { result.error = std::current_exception(); }
  return result;
}
Json freeze_execution(const Command &command, const Execution &execution) {
  Json result{{"Command", command.source}, {"Result", encode_outcome(execution.outcome)}};
  const auto error = classify(execution.error);
  result["Error"] = error.type.empty()
      ? Json(nullptr) : Json{{"Type", error.type}, {"Message", error.message}};
  return result;
}

bool is_source_only_null_boundary(const Command &command) {
  return command.null_galaxy ||
         (command.null_fleet && command.operation == Operation::activate);
}

bool run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  auto world = parse_world(test.at("Arguments").at("World"));
  std::vector<Command> commands;
  for (const auto &value : test.at("Arguments").at("Commands"))
    commands.push_back(parse_command(value));
  const auto source_expected_results = test.at("Result");
  const auto expected_before = test.at("Before");
  const auto source_expected_after = test.at("After");
  const auto boundary = test.at("NativeBoundary");
  const bool has_boundary = !boundary.is_null();
  const auto expected_after = has_boundary ? boundary.at("After") : source_expected_after;
  check(commands.size() == source_expected_results.size(), name + ": result count");
  static const std::vector<std::string> source_error_types{
      "ArgumentNullException", "NullReferenceException", "ArgumentException",
      "InvalidOperationException"};
  for (const auto &expected : source_expected_results)
    if (!expected.at("Error").is_null())
      check(std::ranges::find(source_error_types,
                              expected.at("Error").at("Type").get<std::string>()) !=
                source_error_types.end(),
            name + ": unsupported expected source error category");
  if (has_boundary)
    check(boundary.at("Error").at("Type") == "OverflowError",
          name + ": unsupported native boundary category");
  const bool source_only = std::ranges::any_of(
      commands, is_source_only_null_boundary);
  if (source_only)
    check(std::ranges::all_of(commands, is_source_only_null_boundary),
          name + ": source-only and executable commands cannot share a case");
  for (const auto &command : commands) {
    if (command.null_galaxy)
      continue;
    check(!world.is_null, name + ": non-null command has null world input");
    if ((command.operation == Operation::activate ||
         command.operation == Operation::abandon) &&
        !command.null_fleet)
      check(command.fleet_index >= 0 &&
                static_cast<std::size_t>(command.fleet_index) <
                    world.fleets.size(),
            name + ": FleetIndex is outside the imported fleet array");
  }
  equal_json(encode_world(world), expected_before, name + ".Before");

  if (source_only) {
    check(!has_boundary, name + ": source-only case has a native boundary");
    for (std::size_t index = 0; index < commands.size(); ++index) {
      const auto &expected = source_expected_results[index];
      equal_json(commands[index].source, expected.at("Command"),
                 name + ".SourceOnlyCommand");
      check(expected.at("Result").is_null(),
            name + ": source-only result must be null");
      check(!expected.at("Error").is_null(),
            name + ": source-only observation must contain the C# error");
    }
    equal_json(encode_world(world), source_expected_after,
               name + ".SourceOnlyAfter");
    return false;
  }

  InterstellarLaneNetwork lanes(world.systems);
  Json actual_results = Json::array();
  for (std::size_t index = 0; index < commands.size(); ++index) {
    const auto &command = commands[index];
    Execution execution;
    execution = invoke(world, lanes, command);
    auto frozen = freeze_execution(command, execution);
    if (has_boundary) {
      frozen["Result"] = nullptr;
      const auto &expected_error = boundary.at("Error");
      equal_json(frozen.at("Command"),
                 source_expected_results[index].at("Command"),
                 name + ".NativeCommand");
      equal_json(frozen.at("Error"), expected_error, name + ".NativeError");
    } else {
      equal_json(frozen, source_expected_results[index],
                 name + ".Result[" + std::to_string(index) + "]");
    }
    actual_results.push_back(std::move(frozen));
  }
  equal_json(encode_world(world), expected_after, name + ".After");
  return true;
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected civilian recovery fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open civilian recovery fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format") == "stellar-civilian-recovery-oracle-v1",
          "Unknown civilian recovery fixture format");
    std::size_t native_cases = 0, source_only_cases = 0;
    for (const auto &test : fixture.at("Cases")) {
      if (run_case(test)) ++native_cases;
      else ++source_only_cases;
    }
    std::cout << "civilian_recovery_tests: passed " << native_cases
              << " native actual-C# cases; retained " << source_only_cases
              << " source-only C# null-boundary observations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "civilian_recovery_tests failed: " << error.what() << '\n';
    return 1;
  }
}
