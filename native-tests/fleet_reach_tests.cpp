#include <stellar/core/fleet_reach.hpp>

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
  if (text == "-0")
    return -0.0;
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
  check(actual == expected, field + ": value mismatch; actual " +
                                actual.dump() + ", expected " +
                                expected.dump());
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

Json encode_system(const StellarSystem &value) {
  const auto encoded_class = [](const auto &stellar_class) {
    return stellar_class ? Json(static_cast<int>(*stellar_class))
                         : Json(nullptr);
  };
  return Json{{"Id", value.id},
              {"Name", value.name},
              {"Position",
               {{"X", encoded_number(value.position.x)},
                {"Y", encoded_number(value.position.y)}}},
              {"Archetype", static_cast<int>(value.archetype)},
              {"HasHabitableWorld", value.has_habitable_world},
              {"HasAnomaly", value.has_anomaly},
              {"HasRareResource", value.has_rare_resource},
              {"HasPreWarpCivilization", value.has_pre_warp_civilization},
              {"CatalogPresetId", encoded_optional(value.catalog_preset_id)},
              {"StellarClass", encoded_class(value.primary)},
              {"SecondaryStellarClass", encoded_class(value.secondary)},
              {"TertiaryStellarClass", encoded_class(value.tertiary)},
              {"GalacticDepthLightYears",
               value.position.depth_light_years
                   ? encoded_number(*value.position.depth_light_years)
                   : Json(nullptr)},
              {"StellarCatalogId", encoded_optional(value.stellar_catalog_id)}};
}
Json encode_systems(std::span<const StellarSystem> values) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(encode_system(value));
  return result;
}

std::vector<Colony> parse_colonies(const Json &value) {
  check(value.is_array(), "Colonies must be an array");
  std::vector<Colony> result;
  for (const auto &entry : value) {
    Colony colony;
    colony.id = entry.at("Id").get<int>();
    colony.civilization_id = entry.at("CivilizationId").get<int>();
    colony.system_id = entry.at("SystemId").get<int>();
    colony.planetary_body_id = optional_value<int>(entry.at("PlanetaryBodyId"));
    colony.name = entry.at("Name").get<std::string>();
    colony.kind = static_cast<SettlementKind>(entry.at("Kind").get<int>());
    colony.population_species_id =
        entry.at("PopulationSpeciesId").get<std::string>();
    colony.population_millions = number(entry.at("PopulationMillions"));
    colony.infrastructure = number(entry.at("Infrastructure"));
    colony.stability = number(entry.at("Stability"));
    colony.stored_food_population_days_millions =
        number(entry.at("StoredFoodPopulationDaysMillions"));
    colony.stored_water_population_days_millions =
        number(entry.at("StoredWaterPopulationDaysMillions"));
    colony.stored_extracted_materials =
        number(entry.at("StoredExtractedMaterials"));
    if (!entry.at("RemainingExtractableMaterials").is_null())
      colony.remaining_extractable_materials =
          number(entry.at("RemainingExtractableMaterials"));
    colony.surface_hub_level = entry.at("SurfaceHubLevel").get<int>();
    colony.surface_hub_upgrade_days_remaining =
        number(entry.at("SurfaceHubUpgradeDaysRemaining"));
    for (const auto &building_value : entry.at("SurfaceBuildings")) {
      SurfaceBuilding building;
      building.id = building_value.at("Id").get<int>();
      building.type_id = building_value.at("TypeId").get<std::string>();
      building.x = static_cast<float>(number(building_value.at("X")));
      building.z = static_cast<float>(number(building_value.at("Z")));
      building.rotation_degrees =
          static_cast<float>(number(building_value.at("RotationDegrees")));
      building.industry_progress =
          number(building_value.at("IndustryProgress"));
      building.is_complete = building_value.at("IsComplete").get<bool>();
      building.is_enabled = building_value.at("IsEnabled").get<bool>();
      building.pending_upgrade_type_id = optional_value<std::string>(
          building_value.at("PendingUpgradeTypeId"));
      building.upgrade_days_remaining =
          number(building_value.at("UpgradeDaysRemaining"));
      building.operating_priority =
          building_value.at("OperatingPriority").get<int>();
      building.condition = number(building_value.at("Condition"));
      building.stored_power_days = number(building_value.at("StoredPowerDays"));
      colony.surface_buildings.push_back(std::move(building));
    }
    result.push_back(std::move(colony));
  }
  return result;
}
Json encode_colonies(std::span<const Colony> values) {
  auto result = Json::array();
  for (const auto &value : values) {
    auto buildings = Json::array();
    for (const auto &building : value.surface_buildings)
      buildings.push_back(
          {{"Id", building.id},
           {"TypeId", building.type_id},
           {"X", encoded_number(building.x)},
           {"Z", encoded_number(building.z)},
           {"RotationDegrees", encoded_number(building.rotation_degrees)},
           {"IndustryProgress", encoded_number(building.industry_progress)},
           {"IsComplete", building.is_complete},
           {"IsEnabled", building.is_enabled},
           {"PendingUpgradeTypeId",
            encoded_optional(building.pending_upgrade_type_id)},
           {"UpgradeDaysRemaining",
            encoded_number(building.upgrade_days_remaining)},
           {"OperatingPriority", building.operating_priority},
           {"Condition", encoded_number(building.condition)},
           {"StoredPowerDays", encoded_number(building.stored_power_days)}});
    result.push_back(
        {{"Id", value.id},
         {"CivilizationId", value.civilization_id},
         {"SystemId", value.system_id},
         {"PlanetaryBodyId", encoded_optional(value.planetary_body_id)},
         {"Name", value.name},
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
         {"RemainingExtractableMaterials",
          value.remaining_extractable_materials
              ? encoded_number(*value.remaining_extractable_materials)
              : Json(nullptr)},
         {"SurfaceHubLevel", value.surface_hub_level},
         {"SurfaceHubUpgradeDaysRemaining",
          encoded_number(value.surface_hub_upgrade_days_remaining)},
         {"SurfaceBuildings", std::move(buildings)}});
  }
  return result;
}
MissionReachAssessment parse_assessment(const Json &value) {
  MissionReachAssessment result;
  result.is_supported = value.at("IsSupported").get<bool>();
  result.is_authoritative = value.at("IsAuthoritative").get<bool>();
  result.reason = value.at("Reason").get<std::string>();
  if (!value.at("RouteSystemIds").is_null())
    result.route_system_ids =
        value.at("RouteSystemIds").get<std::vector<int>>();
  result.route_distance_light_years =
      number(value.at("RouteDistanceLightYears"));
  return result;
}
Json encode_assessment(const MissionReachAssessment &value) {
  return Json{{"IsSupported", value.is_supported},
              {"IsAuthoritative", value.is_authoritative},
              {"Reason", value.reason},
              {"RouteSystemIds", value.route_system_ids
                                     ? Json(*value.route_system_ids)
                                     : Json(nullptr)},
              {"RouteDistanceLightYears",
               encoded_number(value.route_distance_light_years)}};
}
struct ErrorInfo {
  std::string type, message;
};
ErrorInfo classify(const std::exception_ptr &error) {
  if (!error)
    return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::overflow_error &e) {
    return {"OverflowException", e.what()};
  } catch (const std::invalid_argument &e) {
    return {"ArgumentException", e.what()};
  } catch (const std::out_of_range &e) {
    return {"ArgumentOutOfRangeException", e.what()};
  } catch (const std::runtime_error &e) {
    return {"InvalidOperationException", e.what()};
  } catch (const std::exception &e) {
    return {"UnexpectedNativeException", e.what()};
  }
}
void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "FactorySupported" || kind == "FactoryUnsupported" ||
            kind == "FactoryProvisional" || kind == "FormatPrimary" ||
            kind == "FormatSpeed" || kind == "Assess" || kind == "Assign" ||
            kind == "Clear",
        name + ": unknown kind");
  const auto &arguments = test.at("Arguments");
  const auto boundary = test.at("NativeBoundary").get<std::string>();
  check(boundary.empty() || boundary == "conversion-overflow" ||
            boundary == "revision-exhaustion",
        name + ": unknown native boundary");
  const auto expected_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  const auto expected_message =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Message").get<std::string>();
  check(expected_type.empty() || expected_type == "ArgumentException" ||
            expected_type == "ArgumentOutOfRangeException" ||
            expected_type == "InvalidOperationException" ||
            expected_type == "OverflowException",
        name + ": unsupported expected error");

  std::optional<std::string> reason;
  double value{};
  std::vector<StellarSystem> systems;
  std::vector<Colony> colonies;
  std::optional<FleetState> fleet;
  int civilization_id{}, target_id{}, mission_kind{}, destination{};
  std::optional<MissionReachAssessment> reach;
  if (kind.starts_with("Factory"))
    reason = optional_value<std::string>(arguments.at("Reason"));
  if (kind == "FormatPrimary" || kind == "FormatSpeed")
    value = number(arguments.at("Value"));
  if (kind == "Assess") {
    systems = parse_systems(arguments.at("Systems"));
    colonies = parse_colonies(arguments.at("Colonies"));
    fleet = parse_fleet(arguments.at("Fleet"));
    civilization_id = arguments.at("CivilizationId").get<int>();
    target_id = arguments.at("TargetSystemId").get<int>();
    mission_kind = arguments.at("MissionKind").get<int>();
    check(mission_kind >= 0 && mission_kind <= 4,
          name + ": invalid mission kind");
    equal_json(arguments.at("Systems"), test.at("BeforeSystems"),
               name + ": source systems snapshot");
    equal_json(test.at("AfterSystems"), test.at("BeforeSystems"),
               name + ": C# systems mutation");
    equal_json(arguments.at("Colonies"), test.at("BeforeColonies"),
               name + ": source colonies snapshot");
    equal_json(test.at("AfterColonies"), test.at("BeforeColonies"),
               name + ": C# colonies mutation");
  }
  if (kind == "Assign") {
    systems = parse_systems(arguments.at("Systems"));
    fleet = parse_fleet(test.at("Before"));
    destination = arguments.at("FinalDestinationSystemId").get<int>();
    reach = parse_assessment(arguments.at("Reach"));
    equal_json(arguments.at("Systems"), test.at("BeforeSystems"),
               name + ": source systems snapshot");
    equal_json(test.at("AfterSystems"), test.at("BeforeSystems"),
               name + ": C# systems mutation");
  }
  if (kind == "Clear")
    fleet = parse_fleet(test.at("Before"));
  if (boundary == "revision-exhaustion") {
    check(fleet &&
              fleet->mission_order_revision == std::numeric_limits<int>::max(),
          name + ": malformed revision boundary");
    check(test.at("Error").is_null() &&
              test.at("After").at("MissionOrderRevision").get<int>() ==
                  std::numeric_limits<int>::min(),
          name + ": C# unchecked wrap not captured");
  }
  if (boundary == "conversion-overflow")
    check(expected_type == "OverflowException" && test.at("Result").is_null(),
          name + ": C# conversion boundary not captured");

  std::optional<MissionReachAssessment> actual_assessment;
  std::optional<std::string> actual_string;
  bool actual_void_result = false;
  std::exception_ptr operation_error;
  try {
    if (kind == "FactorySupported")
      actual_assessment =
          reason ? supported_mission_reach(*reason) : supported_mission_reach();
    else if (kind == "FactoryUnsupported")
      actual_assessment = unsupported_mission_reach(reason.value_or(""));
    else if (kind == "FactoryProvisional")
      actual_assessment =
          provisional_supported_mission_reach(reason.value_or(""));
    else if (kind == "FormatPrimary")
      actual_string = format_interstellar_metric_primary(value);
    else if (kind == "FormatSpeed")
      actual_string = format_interstellar_metric_speed(value);
    else if (kind == "Assess") {
      InterstellarLaneNetwork lanes(systems);
      actual_assessment = assess_operational_reach(
          {systems, colonies, lanes}, civilization_id, *fleet, target_id,
          static_cast<InterstellarMissionKind>(mission_kind));
    } else if (kind == "Assign") {
      InterstellarLaneNetwork lanes(systems);
      assign_fleet_route({systems, colonies, lanes}, *fleet, destination,
                         *reach);
      actual_void_result = true;
    } else {
      clear_fleet_route(*fleet);
      actual_void_result = true;
    }
  } catch (...) {
    operation_error = std::current_exception();
  }

  std::optional<Json> actual_result;
  if (!operation_error) {
    if (actual_assessment)
      actual_result = encode_assessment(*actual_assessment);
    else if (actual_string)
      actual_result = *actual_string;
    else if (actual_void_result)
      actual_result = Json(nullptr);
  }

  const auto actual_error = classify(operation_error);
  if (boundary == "conversion-overflow") {
    check(actual_error.type == "ArgumentException" &&
              actual_error.message ==
                  "Metric conversion exceeds the native formatting range.",
          name + ": native conversion boundary");
    check(!actual_result, name + ": boundary result");
  } else if (boundary == "revision-exhaustion") {
    check(actual_error.type == "OverflowException" &&
              actual_error.message ==
                  "Fleet mission revision space is exhausted.",
          name + ": native revision boundary");
    check(!actual_result, name + ": boundary result");
    equal_json(encode_fleet(*fleet), test.at("Before"),
               name + ": boundary state");
  } else if (!expected_type.empty()) {
    check(actual_error.type == expected_type,
          name + ": error category got " + actual_error.type);
    check(actual_error.message == expected_message,
          name + ": error message got " + actual_error.message);
    check(!actual_result, name + ": errored result");
  } else {
    check(actual_error.type.empty(), name + ": unexpected " +
                                         actual_error.type + ": " +
                                         actual_error.message);
    check(actual_result.has_value(), name + ": missing result");
    equal_json(*actual_result, test.at("Result"), name + ": result");
  }
  if (kind == "Assess")
    equal_json(encode_fleet(*fleet), test.at("After"), name + ": after");
  if (kind == "Assess") {
    equal_json(encode_systems(systems), test.at("AfterSystems"),
               name + ": native systems after");
    equal_json(encode_colonies(colonies), test.at("AfterColonies"),
               name + ": native colonies after");
  }
  if ((kind == "Assign" || kind == "Clear") && boundary.empty())
    equal_json(encode_fleet(*fleet), test.at("After"), name + ": after");
  if (kind == "Assign")
    equal_json(encode_systems(systems), test.at("AfterSystems"),
               name + ": native systems after");
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: fleet_reach_tests <fixture.json>\n";
    return 2;
  }
  try {
    std::ifstream input(argv[1]);
    check(input.good(), "fixture could not be opened");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format") == "stellar-operational-reach-oracle-v1",
          "fixture format");
    const auto &cases = fixture.at("Cases");
    check(cases.is_array() && !cases.empty(), "fixture cases");
    for (const auto &test : cases)
      run_case(test);
    std::cout << "fleet_reach_tests: passed " << cases.size() << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fleet_reach_tests: " << error.what() << '\n';
    return 1;
  }
}
