#include <stellar/core/fleet_seeding.hpp>

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
void equal_number(double actual, double expected, const std::string &field) {
  if (std::isnan(expected)) {
    check(std::isnan(actual), field + ": expected NaN");
    return;
  }
  if (std::isinf(expected)) {
    check(actual == expected, field + ": infinity mismatch");
    return;
  }
  const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
  check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-6 * scale,
        field + ": number mismatch");
}
template <typename T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
template <typename T>
void equal_optional(const std::optional<T> &actual, const Json &expected,
                    const std::string &field) {
  check(actual.has_value() != expected.is_null(), field + ": presence");
  if (actual)
    check(*actual == expected.get<T>(), field + ": value");
}
Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
void equal_vec2(Vec2 actual, const Json &expected, const std::string &field) {
  equal_number(actual.x, number(expected.at("X")), field + ".X");
  equal_number(actual.y, number(expected.at("Y")), field + ".Y");
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
void equal_combat(const FleetCombatState &actual, const Json &expected,
                  const std::string &field) {
  check(actual.profile_id == expected.at("ProfileId").get<std::string>(),
        field + ".ProfileId");
  equal_number(actual.shields, number(expected.at("Shields")),
               field + ".Shields");
  equal_number(actual.armor, number(expected.at("Armor")), field + ".Armor");
  equal_number(actual.hull, number(expected.at("Hull")), field + ".Hull");
  equal_number(actual.weapon_cooldown_remaining_days,
               number(expected.at("WeaponCooldownRemainingDays")),
               field + ".Cooldown");
  check(static_cast<int>(actual.order) == expected.at("Order").get<int>(),
        field + ".Order");
  equal_optional(actual.target_fleet_id, expected.at("TargetFleetId"),
                 field + ".TargetFleetId");
  equal_optional(actual.defend_system_id, expected.at("DefendSystemId"),
                 field + ".DefendSystemId");
  equal_number(actual.retreat_progress_days,
               number(expected.at("RetreatProgressDays")),
               field + ".RetreatProgressDays");
  check(actual.retreat_started == expected.at("RetreatStarted").get<bool>() &&
            actual.is_disengaged == expected.at("IsDisengaged").get<bool>(),
        field + ": combat flags");
  equal_optional(actual.disengaged_system_id, expected.at("DisengagedSystemId"),
                 field + ".DisengagedSystemId");
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
void equal_weapon(const MassiveWeaponGroup &actual, const Json &expected,
                  const std::string &field) {
  check(actual.id == expected.at("Id").get<std::string>() &&
            static_cast<int>(actual.kind) == expected.at("Kind").get<int>() &&
            actual.mounts_per_ship == expected.at("MountsPerShip").get<int>(),
        field + ": identity");
  equal_number(actual.damage_per_shot, number(expected.at("DamagePerShot")),
               field + ".DamagePerShot");
  equal_number(actual.shots_per_second, number(expected.at("ShotsPerSecond")),
               field + ".ShotsPerSecond");
  equal_number(actual.range, number(expected.at("Range")), field + ".Range");
  equal_number(actual.accuracy, number(expected.at("Accuracy")),
               field + ".Accuracy");
  equal_number(actual.power_per_second, number(expected.at("PowerPerSecond")),
               field + ".Power");
  equal_number(actual.heat_per_second, number(expected.at("HeatPerSecond")),
               field + ".Heat");
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
void equal_module(const MassiveModuleState &actual, const Json &expected,
                  const std::string &field) {
  check(actual.id == expected.at("Id").get<std::string>() &&
            static_cast<int>(actual.kind) == expected.at("Kind").get<int>() &&
            actual.installed_count ==
                expected.at("InstalledCount").get<int>() &&
            actual.enabled == expected.at("Enabled").get<bool>() &&
            actual.slots == expected.at("Slots").get<int>(),
        field + ": identity and flags");
  equal_number(actual.mass_each, number(expected.at("MassEach")),
               field + ".MassEach");
  equal_number(actual.power_per_second_each,
               number(expected.at("PowerPerSecondEach")), field + ".Power");
  equal_number(actual.heat_per_second_each,
               number(expected.at("HeatPerSecondEach")), field + ".Heat");
  equal_number(actual.condition, number(expected.at("Condition")),
               field + ".Condition");
  equal_number(actual.effective_range, number(expected.at("EffectiveRange")),
               field + ".Range");
  equal_number(actual.field_strength, number(expected.at("FieldStrength")),
               field + ".Strength");
  equal_number(actual.detection_signature,
               number(expected.at("DetectionSignature")), field + ".Signature");
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
void equal_loadout(const MassiveCombatLoadout &actual, const Json &expected,
                   const std::string &field) {
  const auto scalar = [&](float value, const char *name) {
    equal_number(value, number(expected.at(name)), field + "." + name);
  };
  scalar(actual.mass_per_ship, "MassPerShip");
  scalar(actual.acceleration, "Acceleration");
  scalar(actual.maximum_speed, "MaximumSpeed");
  scalar(actual.shield_per_ship, "ShieldPerShip");
  scalar(actual.armor_per_ship, "ArmorPerShip");
  scalar(actual.hull_per_ship, "HullPerShip");
  scalar(actual.reactor_output_per_ship, "ReactorOutputPerShip");
  scalar(actual.cooling_per_ship, "CoolingPerShip");
  scalar(actual.warp_stabilization, "WarpStabilization");
  scalar(actual.warp_spool_seconds, "WarpSpoolSeconds");
  scalar(actual.maximum_module_mass, "MaximumModuleMass");
  check(actual.module_slot_capacity ==
            expected.at("ModuleSlotCapacity").get<int>(),
        field + ".ModuleSlotCapacity");
  check(actual.weapons.size() == expected.at("Weapons").size() &&
            actual.modules.size() == expected.at("Modules").size(),
        field + ": equipment counts");
  for (std::size_t index = 0; index < actual.weapons.size(); ++index)
    equal_weapon(actual.weapons[index], expected.at("Weapons")[index],
                 field + ".Weapons[" + std::to_string(index) + "]");
  for (std::size_t index = 0; index < actual.modules.size(); ++index)
    equal_module(actual.modules[index], expected.at("Modules")[index],
                 field + ".Modules[" + std::to_string(index) + "]");
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
void equal_vessel(const MassiveVesselState &actual, const Json &expected,
                  const std::string &field) {
  check(actual.id == expected.at("Id").get<std::int64_t>() &&
            actual.name == expected.at("Name").get<std::string>() &&
            actual.design_id == expected.at("DesignId").get<std::string>() &&
            actual.is_flagship == expected.at("IsFlagship").get<bool>() &&
            actual.is_carrier == expected.at("IsCarrier").get<bool>() &&
            actual.is_interdictor == expected.at("IsInterdictor").get<bool>() &&
            actual.is_story_ship == expected.at("IsStoryShip").get<bool>() &&
            actual.battles_fought == expected.at("BattlesFought").get<int>() &&
            actual.confirmed_kills ==
                expected.at("ConfirmedKills").get<int>() &&
            actual.destroyed == expected.at("Destroyed").get<bool>() &&
            actual.escaped == expected.at("Escaped").get<bool>(),
        field + ": identity, flags, or history");
  equal_number(actual.hull_fraction, number(expected.at("HullFraction")),
               field + ".HullFraction");
  equal_number(actual.engine_fraction, number(expected.at("EngineFraction")),
               field + ".EngineFraction");
  equal_number(actual.sensor_fraction, number(expected.at("SensorFraction")),
               field + ".SensorFraction");
  equal_number(actual.warp_drive_fraction,
               number(expected.at("WarpDriveFraction")),
               field + ".WarpDriveFraction");
  equal_number(actual.reactor_fraction, number(expected.at("ReactorFraction")),
               field + ".ReactorFraction");
  equal_number(actual.interdictor_fraction,
               number(expected.at("InterdictorFraction")),
               field + ".InterdictorFraction");
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

void equal_fleet(const FleetState &actual, const Json &expected,
                 const std::string &field) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.civilization_id ==
                expected.at("CivilizationId").get<int>() &&
            actual.name == expected.at("Name").get<std::string>() &&
            static_cast<int>(actual.role) == expected.at("Role").get<int>(),
        field + ": identity");
  equal_optional(actual.design_id, expected.at("DesignId"),
                 field + ".DesignId");
  equal_vec2(actual.position, expected.at("Position"), field + ".Position");
  equal_optional(actual.current_system_id, expected.at("CurrentSystemId"),
                 field + ".CurrentSystemId");
  equal_optional(actual.destination_system_id,
                 expected.at("DestinationSystemId"),
                 field + ".DestinationSystemId");
  check(static_cast<int>(actual.transit_phase) ==
            expected.at("TransitPhase").get<int>(),
        field + ".TransitPhase");
  equal_optional(actual.transit_origin_system_id,
                 expected.at("TransitOriginSystemId"),
                 field + ".TransitOriginSystemId");
  equal_optional(actual.transit_target_system_id,
                 expected.at("TransitTargetSystemId"),
                 field + ".TransitTargetSystemId");
  equal_number(actual.transit_progress, number(expected.at("TransitProgress")),
               field + ".TransitProgress");
  equal_vec2(actual.local_transit_start, expected.at("LocalTransitStart"),
             field + ".LocalTransitStart");
  equal_vec2(actual.local_transit_position, expected.at("LocalTransitPosition"),
             field + ".LocalTransitPosition");
  equal_vec2(actual.local_transit_target, expected.at("LocalTransitTarget"),
             field + ".LocalTransitTarget");
  check(actual.planned_route_system_ids ==
            expected.at("PlannedRouteSystemIds").get<std::vector<int>>(),
        field + ".Route");
  check(actual.hold_requested == expected.at("HoldRequested").get<bool>() &&
            actual.return_to_base_requested ==
                expected.at("ReturnToBaseRequested").get<bool>() &&
            actual.mission_order_revision ==
                expected.at("MissionOrderRevision").get<int>() &&
            actual.prevent_automatic_settlement ==
                expected.at("PreventAutomaticSettlement").get<bool>(),
        field + ": command fields");
  equal_optional(actual.return_to_base_failure_reason,
                 expected.at("ReturnToBaseFailureReason"),
                 field + ".ReturnFailure");
  equal_optional(actual.destination_planetary_body_id,
                 expected.at("DestinationPlanetaryBodyId"),
                 field + ".DestinationBody");
  equal_optional(actual.settlement_body_id, expected.at("SettlementBodyId"),
                 field + ".SettlementBody");
  equal_number(actual.settlement_days_completed,
               number(expected.at("SettlementDaysCompleted")),
               field + ".SettlementDays");
  equal_optional(actual.reconnaissance_system_id,
                 expected.at("ReconnaissanceSystemId"), field + ".ReconSystem");
  equal_number(actual.reconnaissance_days_completed,
               number(expected.at("ReconnaissanceDaysCompleted")),
               field + ".ReconDays");
  equal_optional(actual.freight_target_outpost_id,
                 expected.at("FreightTargetOutpostId"),
                 field + ".FreightTarget");
  equal_optional(actual.freight_home_colony_id,
                 expected.at("FreightHomeColonyId"), field + ".FreightHome");
  equal_number(actual.cargo_material_capacity,
               number(expected.at("CargoMaterialCapacity")),
               field + ".CargoCapacity");
  equal_number(actual.cargo_materials, number(expected.at("CargoMaterials")),
               field + ".Cargo");
  equal_number(actual.strategic_speed, number(expected.at("StrategicSpeed")),
               field + ".Speed");
  equal_number(actual.maximum_leg_range_light_years,
               number(expected.at("MaximumLegRangeLightYears")),
               field + ".Range");
  equal_number(actual.fuel_capacity_light_years,
               number(expected.at("FuelCapacityLightYears")),
               field + ".FuelCapacity");
  equal_number(actual.fuel_remaining_light_years,
               number(expected.at("FuelRemainingLightYears")), field + ".Fuel");
  equal_number(actual.sensor_range, number(expected.at("SensorRange")),
               field + ".SensorRange");
  check(actual.is_active == expected.at("IsActive").get<bool>(),
        field + ".IsActive");
  equal_number(actual.embarked_population_millions,
               number(expected.at("EmbarkedPopulationMillions")),
               field + ".EmbarkedPopulation");
  equal_optional(actual.embarked_population_species_id,
                 expected.at("EmbarkedPopulationSpeciesId"),
                 field + ".EmbarkedSpecies");
  check(actual.combat.has_value() != expected.at("Combat").is_null(),
        field + ".Combat presence");
  if (actual.combat)
    equal_combat(*actual.combat, expected.at("Combat"), field + ".Combat");
  check(actual.tactical_loadout.has_value() !=
            expected.at("TacticalLoadout").is_null(),
        field + ".Loadout presence");
  if (actual.tactical_loadout)
    equal_loadout(*actual.tactical_loadout, expected.at("TacticalLoadout"),
                  field + ".Loadout");
  check(actual.tactical_vessel.has_value() !=
            expected.at("TacticalVessel").is_null(),
        field + ".Vessel presence");
  if (actual.tactical_vessel)
    equal_vessel(*actual.tactical_vessel, expected.at("TacticalVessel"),
                 field + ".Vessel");
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = {
      static_cast<float>(number(value.at("Position").at("X"))),
      static_cast<float>(number(value.at("Position").at("Y"))),
      optional_value<double>(value.at("GalacticDepthLightYears"))};
  if (!value.at("StellarClass").is_null())
    result.primary =
        static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary =
        static_cast<StellarClass>(value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary =
        static_cast<StellarClass>(value.at("TertiaryStellarClass").get<int>());
  result.catalog_preset_id =
      optional_value<std::string>(value.at("CatalogPresetId"));
  result.stellar_catalog_id =
      optional_value<std::string>(value.at("StellarCatalogId"));
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  return result;
}
void equal_system(const StellarSystem &actual, const Json &expected,
                  const std::string &field) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.name == expected.at("Name").get<std::string>() &&
            static_cast<int>(actual.archetype) ==
                expected.at("Archetype").get<int>() &&
            actual.has_habitable_world ==
                expected.at("HasHabitableWorld").get<bool>() &&
            actual.has_anomaly == expected.at("HasAnomaly").get<bool>() &&
            actual.has_rare_resource ==
                expected.at("HasRareResource").get<bool>() &&
            actual.has_pre_warp_civilization ==
                expected.at("HasPreWarpCivilization").get<bool>(),
        field + ": identity and flags");
  equal_number(actual.position.x, number(expected.at("Position").at("X")),
               field + ".Position.X");
  equal_number(actual.position.y, number(expected.at("Position").at("Y")),
               field + ".Position.Y");
  equal_optional(actual.position.depth_light_years,
                 expected.at("GalacticDepthLightYears"), field + ".Depth");
  const auto equal_class = [&](const std::optional<StellarClass> &value,
                               const Json &json, const std::string &name) {
    check(value.has_value() != json.is_null(), field + name + ": presence");
    if (value)
      check(static_cast<int>(*value) == json.get<int>(),
            field + name + ": value");
  };
  equal_class(actual.primary, expected.at("StellarClass"), ".Primary");
  equal_class(actual.secondary, expected.at("SecondaryStellarClass"),
              ".Secondary");
  equal_class(actual.tertiary, expected.at("TertiaryStellarClass"),
              ".Tertiary");
  equal_optional(actual.catalog_preset_id, expected.at("CatalogPresetId"),
                 field + ".CatalogPresetId");
  equal_optional(actual.stellar_catalog_id, expected.at("StellarCatalogId"),
                 field + ".StellarCatalogId");
}

Civilization parse_civilization(const Json &value, const std::string &field) {
  Civilization result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.home_system_id = value.at("HomeSystemId").get<int>();
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound").get<bool>()};
  result.is_player = value.at("IsPlayer").get<bool>();
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient").get<bool>();
  result.expansion_allowed = value.at("ExpansionAllowed").get<bool>();
  result.neutral_unless_provoked =
      value.at("NeutralUnlessProvoked").get<bool>();
  result.species_id = value.at("SpeciesId").get<std::string>();
  check(value.at("Leadership").at("Offices").empty(),
        field + ": nonempty leadership unsupported by fixture adapter");
  return result;
}
void equal_civilization(const Civilization &actual, const Json &expected,
                        const std::string &field) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.name == expected.at("Name").get<std::string>() &&
            actual.home_system_id == expected.at("HomeSystemId").get<int>() &&
            static_cast<int>(actual.archetype) ==
                expected.at("Archetype").get<int>() &&
            actual.is_player == expected.at("IsPlayer").get<bool>() &&
            static_cast<int>(actual.development_stage) ==
                expected.at("DevelopmentStage").get<int>() &&
            actual.is_seeded_ancient ==
                expected.at("IsSeededAncient").get<bool>() &&
            actual.expansion_allowed ==
                expected.at("ExpansionAllowed").get<bool>() &&
            actual.neutral_unless_provoked ==
                expected.at("NeutralUnlessProvoked").get<bool>() &&
            actual.species_id == expected.at("SpeciesId").get<std::string>() &&
            actual.leadership.empty(),
        field + ": fields");
  const auto &traits = expected.at("Traits");
  equal_number(actual.traits.aggression, number(traits.at("Aggression")),
               field + ".Aggression");
  equal_number(actual.traits.territoriality,
               number(traits.at("Territoriality")), field + ".Territoriality");
  equal_number(actual.traits.greed, number(traits.at("Greed")),
               field + ".Greed");
  equal_number(actual.traits.scientific_curiosity,
               number(traits.at("ScientificCuriosity")),
               field + ".ScientificCuriosity");
  equal_number(actual.traits.risk_tolerance, number(traits.at("RiskTolerance")),
               field + ".RiskTolerance");
  equal_number(actual.traits.survival_priority,
               number(traits.at("SurvivalPriority")),
               field + ".SurvivalPriority");
  check(actual.traits.honor_bound == traits.at("HonorBound").get<bool>(),
        field + ".HonorBound");
}

SurfaceBuilding parse_building(const Json &value) {
  SurfaceBuilding result;
  result.id = value.at("Id").get<int>();
  result.type_id = value.at("TypeId").get<std::string>();
  result.x = static_cast<float>(number(value.at("X")));
  result.z = static_cast<float>(number(value.at("Z")));
  result.rotation_degrees =
      static_cast<float>(number(value.at("RotationDegrees")));
  result.industry_progress = number(value.at("IndustryProgress"));
  result.is_complete = value.at("IsComplete").get<bool>();
  result.is_enabled = value.at("IsEnabled").get<bool>();
  result.pending_upgrade_type_id =
      optional_value<std::string>(value.at("PendingUpgradeTypeId"));
  result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
  result.operating_priority = value.at("OperatingPriority").get<int>();
  result.condition = number(value.at("Condition"));
  result.stored_power_days = number(value.at("StoredPowerDays"));
  return result;
}
Colony parse_colony(const Json &value) {
  Colony result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.planetary_body_id = optional_value<int>(value.at("PlanetaryBodyId"));
  result.name = value.at("Name").get<std::string>();
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id =
      value.at("PopulationSpeciesId").get<std::string>();
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials =
      number(value.at("StoredExtractedMaterials"));
  result.remaining_extractable_materials =
      value.at("RemainingExtractableMaterials").is_null()
          ? std::nullopt
          : std::optional<double>(
                number(value.at("RemainingExtractableMaterials")));
  result.surface_hub_level = value.at("SurfaceHubLevel").get<int>();
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &building : value.at("SurfaceBuildings"))
    result.surface_buildings.push_back(parse_building(building));
  return result;
}
void equal_colony(const Colony &actual, const Json &expected,
                  const std::string &field) {
  check(
      actual.id == expected.at("Id").get<int>() &&
          actual.civilization_id == expected.at("CivilizationId").get<int>() &&
          actual.system_id == expected.at("SystemId").get<int>() &&
          actual.name == expected.at("Name").get<std::string>() &&
          static_cast<int>(actual.kind) == expected.at("Kind").get<int>() &&
          actual.population_species_id ==
              expected.at("PopulationSpeciesId").get<std::string>() &&
          actual.surface_hub_level == expected.at("SurfaceHubLevel").get<int>(),
      field + ": identity");
  equal_optional(actual.planetary_body_id, expected.at("PlanetaryBodyId"),
                 field + ".Body");
  equal_number(actual.population_millions,
               number(expected.at("PopulationMillions")),
               field + ".Population");
  equal_number(actual.infrastructure, number(expected.at("Infrastructure")),
               field + ".Infrastructure");
  equal_number(actual.stability, number(expected.at("Stability")),
               field + ".Stability");
  equal_number(actual.stored_food_population_days_millions,
               number(expected.at("StoredFoodPopulationDaysMillions")),
               field + ".Food");
  equal_number(actual.stored_water_population_days_millions,
               number(expected.at("StoredWaterPopulationDaysMillions")),
               field + ".Water");
  equal_number(actual.stored_extracted_materials,
               number(expected.at("StoredExtractedMaterials")),
               field + ".Materials");
  check(actual.remaining_extractable_materials.has_value() !=
            expected.at("RemainingExtractableMaterials").is_null(),
        field + ".Remaining presence");
  if (actual.remaining_extractable_materials)
    equal_number(*actual.remaining_extractable_materials,
                 number(expected.at("RemainingExtractableMaterials")),
                 field + ".Remaining");
  equal_number(actual.surface_hub_upgrade_days_remaining,
               number(expected.at("SurfaceHubUpgradeDaysRemaining")),
               field + ".HubUpgrade");
  const auto &buildings = expected.at("SurfaceBuildings");
  check(actual.surface_buildings.size() == buildings.size(),
        field + ".Buildings count");
  for (std::size_t index = 0; index < actual.surface_buildings.size();
       ++index) {
    const auto &a = actual.surface_buildings[index];
    const auto &e = buildings[index];
    check(a.id == e.at("Id").get<int>() &&
              a.type_id == e.at("TypeId").get<std::string>() &&
              a.is_complete == e.at("IsComplete").get<bool>() &&
              a.is_enabled == e.at("IsEnabled").get<bool>() &&
              a.operating_priority == e.at("OperatingPriority").get<int>(),
          field + ".Building identity");
    equal_number(a.x, number(e.at("X")), field + ".Building.X");
    equal_number(a.z, number(e.at("Z")), field + ".Building.Z");
    equal_number(a.rotation_degrees, number(e.at("RotationDegrees")),
                 field + ".Building.Rotation");
    equal_number(a.industry_progress, number(e.at("IndustryProgress")),
                 field + ".Building.Progress");
    equal_optional(a.pending_upgrade_type_id, e.at("PendingUpgradeTypeId"),
                   field + ".Building.PendingUpgrade");
    equal_number(a.upgrade_days_remaining, number(e.at("UpgradeDaysRemaining")),
                 field + ".Building.UpgradeDays");
    equal_number(a.condition, number(e.at("Condition")),
                 field + ".Building.Condition");
    equal_number(a.stored_power_days, number(e.at("StoredPowerDays")),
                 field + ".Building.Power");
  }
}

struct World {
  std::vector<StellarSystem> systems;
  std::vector<Civilization> civilizations;
  std::optional<std::vector<Colony>> colonies;
  std::vector<FleetState> fleets;
};
World parse_world(const Json &value, bool galaxy, const std::string &field) {
  World result;
  for (const auto &item : value.at("Systems"))
    result.systems.push_back(parse_system(item));
  for (std::size_t index = 0; index < value.at("Civilizations").size(); ++index)
    result.civilizations.push_back(parse_civilization(
        value.at("Civilizations")[index],
        field + ".Civilizations[" + std::to_string(index) + "]"));
  if (!value.at("Colonies").is_null()) {
    result.colonies.emplace();
    for (const auto &item : value.at("Colonies"))
      result.colonies->push_back(parse_colony(item));
  }
  if (galaxy)
    for (const auto &item : value.at("Fleets"))
      result.fleets.push_back(parse_fleet(item));
  return result;
}
void equal_world(const World &actual, const Json &expected, bool galaxy,
                 const std::string &field) {
  check(actual.systems.size() == expected.at("Systems").size(),
        field + ".Systems count");
  for (std::size_t index = 0; index < actual.systems.size(); ++index)
    equal_system(actual.systems[index], expected.at("Systems")[index],
                 field + ".Systems[" + std::to_string(index) + "]");
  check(actual.civilizations.size() == expected.at("Civilizations").size(),
        field + ".Civilizations count");
  for (std::size_t index = 0; index < actual.civilizations.size(); ++index)
    equal_civilization(actual.civilizations[index],
                       expected.at("Civilizations")[index],
                       field + ".Civilizations[" + std::to_string(index) + "]");
  check(actual.colonies.has_value() != expected.at("Colonies").is_null(),
        field + ".Colonies presence");
  if (actual.colonies) {
    check(actual.colonies->size() == expected.at("Colonies").size(),
          field + ".Colonies count");
    for (std::size_t index = 0; index < actual.colonies->size(); ++index)
      equal_colony((*actual.colonies)[index], expected.at("Colonies")[index],
                   field + ".Colonies[" + std::to_string(index) + "]");
  }
  if (galaxy) {
    check(actual.fleets.size() == expected.at("Fleets").size(),
          field + ".Fleets count");
    for (std::size_t index = 0; index < actual.fleets.size(); ++index)
      equal_fleet(actual.fleets[index], expected.at("Fleets")[index],
                  field + ".Fleets[" + std::to_string(index) + "]");
  }
}

std::vector<FleetState> parse_fleets(const Json &value) {
  std::vector<FleetState> result;
  check(value.is_array(), "Fleet result must be an array");
  for (const auto &fleet : value)
    result.push_back(parse_fleet(fleet));
  return result;
}
void equal_fleets(const std::vector<FleetState> &actual, const Json &expected,
                  const std::string &field) {
  check(actual.size() == expected.size(), field + ": count");
  for (std::size_t index = 0; index < actual.size(); ++index)
    equal_fleet(actual[index], expected[index],
                field + "[" + std::to_string(index) + "]");
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
  } catch (const std::overflow_error &actual) {
    return {"NativeIdentityExhaustion", actual.what()};
  } catch (const std::invalid_argument &actual) {
    return {"InvalidOperationException", actual.what()};
  } catch (const std::exception &actual) {
    return {"UnexpectedNativeException", actual.what()};
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Seed" || kind == "Ensure", name + ": unknown kind");
  const auto &arguments = test.at("Arguments");
  const auto &input = kind == "Seed" ? arguments : arguments.at("Galaxy");
  World world = parse_world(input, kind == "Ensure", name + ".Arguments");
  const auto civilization_id =
      kind == "Ensure" ? arguments.at("CivilizationId").get<int>() : 0;
  const auto native_identity_boundary =
      kind == "Ensure"
          ? arguments.at("NativeIdentityBoundary").get<std::string>()
          : std::string{};
  check(native_identity_boundary.empty() ||
            native_identity_boundary == "before-scout" ||
            native_identity_boundary == "after-scout",
        name + ": unknown native identity boundary");
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "InvalidOperationException",
        name + ": unsupported expected error type");
  std::optional<std::vector<FleetState>> source_result;
  if (!test.at("Result").is_null())
    source_result = parse_fleets(test.at("Result"));
  std::optional<World> source_boundary_after;
  if (!native_identity_boundary.empty())
    source_boundary_after =
        parse_world(test.at("After"), true, name + ".SourceAfter");
  if (kind == "Ensure")
    check(world.colonies.has_value(), name + ": galaxy colonies missing");
  equal_world(world, test.at("Before"), kind == "Ensure", name + ".Before");

  std::optional<std::vector<FleetState>> result;
  std::exception_ptr operation_error;
  try {
    if (kind == "Seed") {
      const auto population_sources =
          world.colonies ? std::optional<std::span<Colony>>(*world.colonies)
                         : std::nullopt;
      result =
          seed_fleets(world.systems, world.civilizations, population_sources);
    } else {
      ensure_starter_fleets(world.systems, world.civilizations, *world.colonies,
                            world.fleets, civilization_id);
      result = world.fleets;
    }
  } catch (...) {
    operation_error = std::current_exception();
  }

  const auto actual_error = classify(operation_error);
  if (!native_identity_boundary.empty()) {
    check(actual_error.type == "NativeIdentityExhaustion" &&
              actual_error.message == "Fleet identity space is exhausted.",
          name + ": native identity boundary");
    check(expected_error_type.empty() && source_result.has_value(),
          name + ": source boundary evidence");
    const auto original_fleet_count = test.at("Before").at("Fleets").size();
    const auto source_added_count =
        native_identity_boundary == "before-scout" ? 1u : 2u;
    check(source_result->size() == original_fleet_count + source_added_count &&
              source_result->back().id == std::numeric_limits<int>::min(),
          name + ": source unchecked-wrap evidence");
    equal_fleets(source_boundary_after->fleets, test.at("Result"),
                 name + ".SourceAfter.Fleets");
    World unchanged_native = world;
    if (native_identity_boundary == "after-scout") {
      check(world.fleets.size() == original_fleet_count + 1,
            name + ": native partial scout count");
      equal_fleet(world.fleets.back(), test.at("Result")[original_fleet_count],
                  name + ".NativeScout");
      unchanged_native.fleets.resize(original_fleet_count);
    }
    equal_world(unchanged_native, test.at("Before"), true,
                name + ".NativeUnchangedInputs");
  } else if (!expected_error_type.empty()) {
    check(actual_error.type == expected_error_type, name + ": error category");
    check(actual_error.message ==
              test.at("Error").at("Message").get<std::string>(),
          name + ": error message");
    check(test.at("Result").is_null(), name + ": errored result");
    equal_world(world, test.at("After"), kind == "Ensure", name + ".After");
  } else {
    check(actual_error.type.empty(), name + ": unexpected native error");
    check(result.has_value() && source_result.has_value(),
          name + ": result presence");
    equal_fleets(*result, test.at("Result"), name + ".Result");
    equal_world(world, test.at("After"), kind == "Ensure", name + ".After");
  }

  // Fields outside this adapter's systems/civilizations/colonies/fleets surface
  // are immutable source context and must also remain unchanged in the oracle.
  if (kind == "Ensure") {
    static const std::vector<std::string> ancillary{
        "DeveloperSession",   "GenerationMetadata",
        "GalacticCore",       "ActiveCombatEncounter",
        "CombatIntelligence", "Seed",
        "PlanetaryBodies",    "Economies",
        "Technologies",       "ConstructionStates",
        "ShipyardStates",     "PlayerCivilizationId",
        "Knowledge"};
    for (const auto &field : ancillary)
      check(test.at("Before").at(field) == test.at("After").at(field),
            name + ": source mutated ancillary field " + field);
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected fleet-seeding fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open fleet-seeding fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-fleet-seeding-oracle-v1",
          "Unknown fleet-seeding fixture format");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "fleet_seeding_tests: passed " << fixture.at("Cases").size()
              << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fleet_seeding_tests failed: " << error.what() << '\n';
    return 1;
  }
}
