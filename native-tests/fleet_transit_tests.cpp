#include <stellar/core/fleet_transit.hpp>

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
template <typename T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
template <typename T> Json encoded_optional(const std::optional<T> &value) {
  return value ? Json(*value) : Json(nullptr);
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (expected.is_number() ||
      (expected.is_string() && (expected == "NaN" || expected == "Infinity" ||
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
    check(std::isfinite(a) && std::abs(a - e) <= 1e-6 * scale,
          field + ": number mismatch");
    return;
  }
  if (expected.is_array()) {
    check(actual.is_array() && actual.size() == expected.size(),
          field + ": array mismatch");
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

Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
Json encode_vec2(Vec2 value) {
  return {{"X", encoded_number(value.x)}, {"Y", encoded_number(value.y)}};
}

FleetCombatState parse_combat(const Json &value) {
  FleetCombatState result;
  result.profile_id = value.at("ProfileId").get<std::string>();
  result.shields = number(value.at("Shields"));
  result.armor = number(value.at("Armor"));
  result.hull = number(value.at("Hull"));
  result.weapon_cooldown_remaining_days =
      number(value.at("WeaponCooldownRemainingDays"));
  result.order = static_cast<MilitaryOrderType>(value.at("Order").get<int>());
  result.target_fleet_id = optional_value<int>(value.at("TargetFleetId"));
  result.defend_system_id = optional_value<int>(value.at("DefendSystemId"));
  result.retreat_progress_days = number(value.at("RetreatProgressDays"));
  result.retreat_started = value.at("RetreatStarted").get<bool>();
  result.is_disengaged = value.at("IsDisengaged").get<bool>();
  result.disengaged_system_id =
      optional_value<int>(value.at("DisengagedSystemId"));
  return result;
}
Json encode_combat(const FleetCombatState &value) {
  return {{"ProfileId", value.profile_id},
          {"Shields", encoded_number(value.shields)},
          {"Armor", encoded_number(value.armor)},
          {"Hull", encoded_number(value.hull)},
          {"WeaponCooldownRemainingDays",
           encoded_number(value.weapon_cooldown_remaining_days)},
          {"Order", static_cast<int>(value.order)},
          {"TargetFleetId", encoded_optional(value.target_fleet_id)},
          {"DefendSystemId", encoded_optional(value.defend_system_id)},
          {"RetreatProgressDays", encoded_number(value.retreat_progress_days)},
          {"RetreatStarted", value.retreat_started},
          {"IsDisengaged", value.is_disengaged},
          {"DisengagedSystemId", encoded_optional(value.disengaged_system_id)}};
}

MassiveWeaponGroup parse_weapon(const Json &value) {
  MassiveWeaponGroup result;
  result.id = value.at("Id").get<std::string>();
  result.kind = static_cast<MassiveWeaponKind>(value.at("Kind").get<int>());
  result.mounts_per_ship = value.at("MountsPerShip").get<int>();
  result.damage_per_shot =
      static_cast<float>(number(value.at("DamagePerShot")));
  result.shots_per_second =
      static_cast<float>(number(value.at("ShotsPerSecond")));
  result.range = static_cast<float>(number(value.at("Range")));
  result.accuracy = static_cast<float>(number(value.at("Accuracy")));
  result.power_per_second =
      static_cast<float>(number(value.at("PowerPerSecond")));
  result.heat_per_second =
      static_cast<float>(number(value.at("HeatPerSecond")));
  return result;
}
Json encode_weapon(const MassiveWeaponGroup &value) {
  return {{"Id", value.id},
          {"Kind", static_cast<int>(value.kind)},
          {"MountsPerShip", value.mounts_per_ship},
          {"DamagePerShot", encoded_number(value.damage_per_shot)},
          {"ShotsPerSecond", encoded_number(value.shots_per_second)},
          {"Range", encoded_number(value.range)},
          {"Accuracy", encoded_number(value.accuracy)},
          {"PowerPerSecond", encoded_number(value.power_per_second)},
          {"HeatPerSecond", encoded_number(value.heat_per_second)}};
}

MassiveModuleState parse_module(const Json &value) {
  MassiveModuleState result;
  result.id = value.at("Id").get<std::string>();
  result.kind = static_cast<MassiveModuleKind>(value.at("Kind").get<int>());
  result.installed_count = value.at("InstalledCount").get<int>();
  result.mass_each = static_cast<float>(number(value.at("MassEach")));
  result.power_per_second_each =
      static_cast<float>(number(value.at("PowerPerSecondEach")));
  result.heat_per_second_each =
      static_cast<float>(number(value.at("HeatPerSecondEach")));
  result.condition = static_cast<float>(number(value.at("Condition")));
  result.enabled = value.at("Enabled").get<bool>();
  result.effective_range =
      static_cast<float>(number(value.at("EffectiveRange")));
  result.field_strength = static_cast<float>(number(value.at("FieldStrength")));
  result.detection_signature =
      static_cast<float>(number(value.at("DetectionSignature")));
  result.slots = value.at("Slots").get<int>();
  return result;
}
Json encode_module(const MassiveModuleState &value) {
  return {{"Id", value.id},
          {"Kind", static_cast<int>(value.kind)},
          {"InstalledCount", value.installed_count},
          {"MassEach", encoded_number(value.mass_each)},
          {"PowerPerSecondEach", encoded_number(value.power_per_second_each)},
          {"HeatPerSecondEach", encoded_number(value.heat_per_second_each)},
          {"Condition", encoded_number(value.condition)},
          {"Enabled", value.enabled},
          {"EffectiveRange", encoded_number(value.effective_range)},
          {"FieldStrength", encoded_number(value.field_strength)},
          {"DetectionSignature", encoded_number(value.detection_signature)},
          {"Slots", value.slots}};
}

MassiveCombatLoadout parse_loadout(const Json &value) {
  MassiveCombatLoadout result;
  result.mass_per_ship = static_cast<float>(number(value.at("MassPerShip")));
  result.acceleration = static_cast<float>(number(value.at("Acceleration")));
  result.maximum_speed = static_cast<float>(number(value.at("MaximumSpeed")));
  result.shield_per_ship =
      static_cast<float>(number(value.at("ShieldPerShip")));
  result.armor_per_ship = static_cast<float>(number(value.at("ArmorPerShip")));
  result.hull_per_ship = static_cast<float>(number(value.at("HullPerShip")));
  result.reactor_output_per_ship =
      static_cast<float>(number(value.at("ReactorOutputPerShip")));
  result.cooling_per_ship =
      static_cast<float>(number(value.at("CoolingPerShip")));
  result.warp_stabilization =
      static_cast<float>(number(value.at("WarpStabilization")));
  result.warp_spool_seconds =
      static_cast<float>(number(value.at("WarpSpoolSeconds")));
  result.module_slot_capacity = value.at("ModuleSlotCapacity").get<int>();
  result.maximum_module_mass =
      static_cast<float>(number(value.at("MaximumModuleMass")));
  for (const auto &weapon : value.at("Weapons"))
    result.weapons.push_back(parse_weapon(weapon));
  for (const auto &module : value.at("Modules"))
    result.modules.push_back(parse_module(module));
  return result;
}
Json encode_loadout(const MassiveCombatLoadout &value) {
  Json weapons = Json::array();
  for (const auto &weapon : value.weapons)
    weapons.push_back(encode_weapon(weapon));
  Json modules = Json::array();
  for (const auto &module : value.modules)
    modules.push_back(encode_module(module));
  return {
      {"MassPerShip", encoded_number(value.mass_per_ship)},
      {"Acceleration", encoded_number(value.acceleration)},
      {"MaximumSpeed", encoded_number(value.maximum_speed)},
      {"ShieldPerShip", encoded_number(value.shield_per_ship)},
      {"ArmorPerShip", encoded_number(value.armor_per_ship)},
      {"HullPerShip", encoded_number(value.hull_per_ship)},
      {"ReactorOutputPerShip", encoded_number(value.reactor_output_per_ship)},
      {"CoolingPerShip", encoded_number(value.cooling_per_ship)},
      {"WarpStabilization", encoded_number(value.warp_stabilization)},
      {"WarpSpoolSeconds", encoded_number(value.warp_spool_seconds)},
      {"ModuleSlotCapacity", value.module_slot_capacity},
      {"MaximumModuleMass", encoded_number(value.maximum_module_mass)},
      {"Weapons", std::move(weapons)},
      {"Modules", std::move(modules)}};
}

MassiveVesselState parse_vessel(const Json &value) {
  MassiveVesselState result;
  result.id = value.at("Id").get<std::int64_t>();
  result.name = value.at("Name").get<std::string>();
  result.design_id = value.at("DesignId").get<std::string>();
  result.is_flagship = value.at("IsFlagship").get<bool>();
  result.is_carrier = value.at("IsCarrier").get<bool>();
  result.is_interdictor = value.at("IsInterdictor").get<bool>();
  result.is_story_ship = value.at("IsStoryShip").get<bool>();
  result.hull_fraction = static_cast<float>(number(value.at("HullFraction")));
  result.engine_fraction =
      static_cast<float>(number(value.at("EngineFraction")));
  result.sensor_fraction =
      static_cast<float>(number(value.at("SensorFraction")));
  result.warp_drive_fraction =
      static_cast<float>(number(value.at("WarpDriveFraction")));
  result.reactor_fraction =
      static_cast<float>(number(value.at("ReactorFraction")));
  result.interdictor_fraction =
      static_cast<float>(number(value.at("InterdictorFraction")));
  result.battles_fought = value.at("BattlesFought").get<int>();
  result.confirmed_kills = value.at("ConfirmedKills").get<int>();
  result.destroyed = value.at("Destroyed").get<bool>();
  result.escaped = value.at("Escaped").get<bool>();
  return result;
}
Json encode_vessel(const MassiveVesselState &value) {
  return {{"Id", value.id},
          {"Name", value.name},
          {"DesignId", value.design_id},
          {"IsFlagship", value.is_flagship},
          {"IsCarrier", value.is_carrier},
          {"IsInterdictor", value.is_interdictor},
          {"IsStoryShip", value.is_story_ship},
          {"HullFraction", encoded_number(value.hull_fraction)},
          {"EngineFraction", encoded_number(value.engine_fraction)},
          {"SensorFraction", encoded_number(value.sensor_fraction)},
          {"WarpDriveFraction", encoded_number(value.warp_drive_fraction)},
          {"ReactorFraction", encoded_number(value.reactor_fraction)},
          {"InterdictorFraction", encoded_number(value.interdictor_fraction)},
          {"BattlesFought", value.battles_fought},
          {"ConfirmedKills", value.confirmed_kills},
          {"Destroyed", value.destroyed},
          {"Escaped", value.escaped}};
}

FleetState parse_fleet(const Json &value) {
  FleetState result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional_value<std::string>(value.at("DesignId"));
  result.position = parse_vec2(value.at("Position"));
  result.current_system_id = optional_value<int>(value.at("CurrentSystemId"));
  result.destination_system_id =
      optional_value<int>(value.at("DestinationSystemId"));
  result.transit_phase =
      static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  result.transit_origin_system_id =
      optional_value<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id =
      optional_value<int>(value.at("TransitTargetSystemId"));
  result.transit_progress = number(value.at("TransitProgress"));
  result.local_transit_start = parse_vec2(value.at("LocalTransitStart"));
  result.local_transit_position = parse_vec2(value.at("LocalTransitPosition"));
  result.local_transit_target = parse_vec2(value.at("LocalTransitTarget"));
  result.planned_route_system_ids =
      value.at("PlannedRouteSystemIds").get<std::vector<int>>();
  result.hold_requested = value.at("HoldRequested").get<bool>();
  result.return_to_base_requested =
      value.at("ReturnToBaseRequested").get<bool>();
  result.return_to_base_failure_reason =
      optional_value<std::string>(value.at("ReturnToBaseFailureReason"));
  result.mission_order_revision = value.at("MissionOrderRevision").get<int>();
  result.destination_planetary_body_id =
      optional_value<int>(value.at("DestinationPlanetaryBodyId"));
  result.prevent_automatic_settlement =
      value.at("PreventAutomaticSettlement").get<bool>();
  result.settlement_body_id = optional_value<int>(value.at("SettlementBodyId"));
  result.settlement_days_completed =
      number(value.at("SettlementDaysCompleted"));
  result.reconnaissance_system_id =
      optional_value<int>(value.at("ReconnaissanceSystemId"));
  result.reconnaissance_days_completed =
      number(value.at("ReconnaissanceDaysCompleted"));
  result.freight_target_outpost_id =
      optional_value<int>(value.at("FreightTargetOutpostId"));
  result.freight_home_colony_id =
      optional_value<int>(value.at("FreightHomeColonyId"));
  result.cargo_material_capacity = number(value.at("CargoMaterialCapacity"));
  result.cargo_materials = number(value.at("CargoMaterials"));
  result.strategic_speed = number(value.at("StrategicSpeed"));
  result.maximum_leg_range_light_years =
      number(value.at("MaximumLegRangeLightYears"));
  result.fuel_capacity_light_years = number(value.at("FuelCapacityLightYears"));
  result.fuel_remaining_light_years =
      number(value.at("FuelRemainingLightYears"));
  result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  result.is_active = value.at("IsActive").get<bool>();
  result.embarked_population_millions =
      number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id =
      optional_value<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  if (!value.at("Combat").is_null())
    result.combat = parse_combat(value.at("Combat"));
  if (!value.at("TacticalLoadout").is_null())
    result.tactical_loadout = parse_loadout(value.at("TacticalLoadout"));
  if (!value.at("TacticalVessel").is_null())
    result.tactical_vessel = parse_vessel(value.at("TacticalVessel"));
  return result;
}

Json encode_fleet(const FleetState &value) {
  Json route = Json::array();
  for (const auto id : value.planned_route_system_ids)
    route.push_back(id);
  return {
      {"Id", value.id},
      {"CivilizationId", value.civilization_id},
      {"Name", value.name},
      {"Role", static_cast<int>(value.role)},
      {"DesignId", encoded_optional(value.design_id)},
      {"Position", encode_vec2(value.position)},
      {"CurrentSystemId", encoded_optional(value.current_system_id)},
      {"DestinationSystemId", encoded_optional(value.destination_system_id)},
      {"TransitPhase", static_cast<int>(value.transit_phase)},
      {"TransitOriginSystemId",
       encoded_optional(value.transit_origin_system_id)},
      {"TransitTargetSystemId",
       encoded_optional(value.transit_target_system_id)},
      {"TransitProgress", encoded_number(value.transit_progress)},
      {"LocalTransitStart", encode_vec2(value.local_transit_start)},
      {"LocalTransitPosition", encode_vec2(value.local_transit_position)},
      {"LocalTransitTarget", encode_vec2(value.local_transit_target)},
      {"PlannedRouteSystemIds", std::move(route)},
      {"HoldRequested", value.hold_requested},
      {"ReturnToBaseRequested", value.return_to_base_requested},
      {"ReturnToBaseFailureReason",
       encoded_optional(value.return_to_base_failure_reason)},
      {"MissionOrderRevision", value.mission_order_revision},
      {"DestinationPlanetaryBodyId",
       encoded_optional(value.destination_planetary_body_id)},
      {"PreventAutomaticSettlement", value.prevent_automatic_settlement},
      {"SettlementBodyId", encoded_optional(value.settlement_body_id)},
      {"SettlementDaysCompleted",
       encoded_number(value.settlement_days_completed)},
      {"ReconnaissanceSystemId",
       encoded_optional(value.reconnaissance_system_id)},
      {"ReconnaissanceDaysCompleted",
       encoded_number(value.reconnaissance_days_completed)},
      {"FreightTargetOutpostId",
       encoded_optional(value.freight_target_outpost_id)},
      {"FreightHomeColonyId", encoded_optional(value.freight_home_colony_id)},
      {"CargoMaterialCapacity", encoded_number(value.cargo_material_capacity)},
      {"CargoMaterials", encoded_number(value.cargo_materials)},
      {"StrategicSpeed", encoded_number(value.strategic_speed)},
      {"MaximumLegRangeLightYears",
       encoded_number(value.maximum_leg_range_light_years)},
      {"FuelCapacityLightYears",
       encoded_number(value.fuel_capacity_light_years)},
      {"FuelRemainingLightYears",
       encoded_number(value.fuel_remaining_light_years)},
      {"SensorRange", encoded_number(value.sensor_range)},
      {"IsActive", value.is_active},
      {"EmbarkedPopulationMillions",
       encoded_number(value.embarked_population_millions)},
      {"EmbarkedPopulationSpeciesId",
       encoded_optional(value.embarked_population_species_id)},
      {"Combat", value.combat ? encode_combat(*value.combat) : Json(nullptr)},
      {"TacticalLoadout", value.tactical_loadout
                              ? encode_loadout(*value.tactical_loadout)
                              : Json(nullptr)},
      {"TacticalVessel", value.tactical_vessel
                             ? encode_vessel(*value.tactical_vessel)
                             : Json(nullptr)}};
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = {
      static_cast<float>(number(value.at("Position").at("X"))),
      static_cast<float>(number(value.at("Position").at("Y"))),
      value.at("GalacticDepthLightYears").is_null()
          ? std::nullopt
          : std::optional<double>(number(value.at("GalacticDepthLightYears")))};
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  result.catalog_preset_id =
      optional_value<std::string>(value.at("CatalogPresetId"));
  result.stellar_catalog_id =
      optional_value<std::string>(value.at("StellarCatalogId"));
  if (!value.at("StellarClass").is_null())
    result.primary =
        static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary =
        static_cast<StellarClass>(value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary =
        static_cast<StellarClass>(value.at("TertiaryStellarClass").get<int>());
  return result;
}
std::vector<StellarSystem> parse_systems(const Json &value) {
  std::vector<StellarSystem> result;
  check(value.is_array(), "Systems must be an array");
  for (const auto &system : value)
    result.push_back(parse_system(system));
  return result;
}

struct ErrorInfo {
  std::string type;
  std::string message;
};
ErrorInfo classify(const std::exception_ptr &error) {
  if (!error)
    return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::invalid_argument &actual) {
    return {"ArgumentException", actual.what()};
  } catch (const std::out_of_range &actual) {
    return {"KeyNotFoundException", actual.what()};
  } catch (const std::exception &actual) {
    return {"UnexpectedNativeException", actual.what()};
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Rate" || kind == "GateTowards" || kind == "Begin" ||
            kind == "Advance" || kind == "Complete" ||
            kind == "RemainingDays" || kind == "RemainingChartDistance" ||
            kind == "InterpolateChartPosition" || kind == "FromFleet",
        name + ": unknown kind");
  const auto &arguments = test.at("Arguments");
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  const auto expected_error_message =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Message").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "ArgumentException" ||
            expected_error_type == "KeyNotFoundException",
        name + ": unsupported expected error");

  std::optional<FleetState> fleet;
  if (kind == "Begin" || kind == "Advance")
    fleet = parse_fleet(test.at("Before"));
  else if (kind == "Complete" || kind == "RemainingDays")
    fleet = parse_fleet(arguments);
  else if (kind == "RemainingChartDistance" || kind == "FromFleet")
    fleet = parse_fleet(arguments.at("Fleet"));
  std::vector<StellarSystem> systems;
  if (kind == "RemainingChartDistance" || kind == "FromFleet")
    systems = parse_systems(arguments.at("Systems"));
  std::optional<StellarSystem> origin;
  std::optional<StellarSystem> target_system;
  if (kind == "InterpolateChartPosition") {
    origin = parse_system(arguments.at("Origin"));
    target_system = parse_system(arguments.at("Target"));
  } else if (kind == "FromFleet") {
    target_system = parse_system(arguments.at("Target"));
  }
  const auto source =
      kind == "GateTowards" ? parse_vec2(arguments.at("Source")) : Vec2{};
  const auto current =
      kind == "GateTowards" ? parse_vec2(arguments.at("Current")) : Vec2{};
  const auto start =
      kind == "Begin" ? parse_vec2(arguments.at("Start")) : Vec2{};
  const auto target =
      kind == "Begin" ? parse_vec2(arguments.at("Target")) : Vec2{};
  const auto phase =
      kind == "Begin"
          ? static_cast<FleetTransitPhase>(arguments.at("Phase").get<int>())
          : FleetTransitPhase::None;
  const auto days = kind == "Advance" ? number(arguments.at("Days")) : 0;
  const auto progress =
      kind == "InterpolateChartPosition" ? number(arguments.at("Progress")) : 0;
  const auto speed =
      kind == "Rate" ? number(arguments.at("StrategicSpeed")) : 0;
  if (fleet && test.contains("Before"))
    equal_json(encode_fleet(*fleet), test.at("Before"), name + ".Before");

  std::optional<double> number_result;
  std::optional<bool> bool_result;
  std::optional<Vec2> vector_result;
  std::exception_ptr operation_error;
  try {
    if (kind == "Rate") {
      FleetState value;
      value.strategic_speed = speed;
      number_result = fleet_local_transit_rate(value);
    } else if (kind == "GateTowards")
      vector_result = fleet_gate_towards(source, current);
    else if (kind == "Begin")
      begin_fleet_local_transit(*fleet, phase, start, target);
    else if (kind == "Advance")
      number_result = advance_fleet_local_transit(*fleet, days);
    else if (kind == "Complete")
      bool_result = fleet_local_transit_complete(*fleet);
    else if (kind == "RemainingDays")
      number_result = fleet_local_transit_remaining_days(*fleet);
    else if (kind == "RemainingChartDistance")
      number_result = fleet_remaining_chart_distance(systems, *fleet);
    else if (kind == "InterpolateChartPosition")
      vector_result =
          interpolate_chart_position(*origin, *target_system, progress);
    else if (kind == "FromFleet")
      number_result =
          interstellar_distance_from_fleet(systems, *fleet, *target_system);
  } catch (...) {
    operation_error = std::current_exception();
  }

  const auto actual_error = classify(operation_error);
  if (!expected_error_type.empty()) {
    check(actual_error.type == expected_error_type, name + ": error category");
    check(actual_error.message == expected_error_message,
          name + ": error message");
    check(test.at("Result").is_null(), name + ": errored result");
  } else {
    check(actual_error.type.empty(), name + ": unexpected error");
    if (number_result)
      equal_json(encoded_number(*number_result), test.at("Result"),
                 name + ".Result");
    else if (bool_result)
      check(*bool_result == test.at("Result").get<bool>(),
            name + ": bool result");
    else if (vector_result)
      equal_json(encode_vec2(*vector_result), test.at("Result"),
                 name + ".Result");
    else
      check(test.at("Result").is_null(), name + ": void result");
  }
  if (fleet && test.contains("After"))
    equal_json(encode_fleet(*fleet), test.at("After"), name + ".After");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected fleet-transit fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open fleet-transit fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-fleet-transit-oracle-v1",
          "Unknown fleet-transit fixture format");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "fleet_transit_tests: passed " << fixture.at("Cases").size()
              << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fleet_transit_tests failed: " << error.what() << '\n';
    return 1;
  }
}
