#include <stellar/core/colonization_runtime.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
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
  const auto &offices = value.at("Leadership").at("Offices");
  check(offices.is_object(), field + ".Leadership.Offices object");
  for (const auto &[office, character] : offices.items()) {
    result.leadership.push_back(
        {office,
         {character.at("Id").get<std::string>(),
          character.at("DisplayName").get<std::string>(),
          optional_value<std::string>(character.at("VoiceProfileId")),
          optional_value<std::string>(character.at("Portrait"))}});
  }
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
            actual.species_id == expected.at("SpeciesId").get<std::string>(),
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
  const auto &offices = expected.at("Leadership").at("Offices");
  check(actual.leadership.size() == offices.size(),
        field + ".Leadership count");
  static constexpr const char *office_order[] = {
      "FleetCommander",
      "ChiefScientist",
      "Diplomat",
      "Governor",
      "EconomicAdvisor",
      "OperationsOfficer",
      "ExpeditionCommander",
  };
  for (std::size_t office_index = 0; office_index < actual.leadership.size();
       ++office_index) {
    const auto &native = actual.leadership[office_index];
    const std::string office = office_order[office_index];
    check(native.office == office, field + ".Leadership office order");
    check(offices.contains(office), field + ".Leadership expected office");
    const auto &character = offices.at(office);
    check(native.character.id == character.at("Id").get<std::string>(),
          field + ".Leadership.Id");
    check(native.character.display_name ==
              character.at("DisplayName").get<std::string>(),
          field + ".Leadership.DisplayName");
    equal_optional(native.character.voice_profile_id,
                   character.at("VoiceProfileId"),
                   field + ".Leadership.VoiceProfileId");
    equal_optional(native.character.portrait, character.at("Portrait"),
                   field + ".Leadership.Portrait");
  }
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

void equal_body(const PlanetaryBody &actual, const Json &expected,
                const std::string &field) {
  check(actual.id == expected.at("Id").get<int>(), field + ".Id");
  check(actual.system_id == expected.at("SystemId").get<int>(),
        field + ".SystemId");
  equal_optional(actual.parent_body_id, expected.at("ParentBodyId"),
                 field + ".ParentBodyId");
  check(actual.orbit_index == expected.at("OrbitIndex").get<int>(),
        field + ".OrbitIndex");
  check(actual.name == expected.at("Name").get<std::string>(), field + ".Name");
  check(static_cast<int>(actual.kind) == expected.at("Kind").get<int>(),
        field + ".Kind");
  equal_number(actual.radius_earth, number(expected.at("RadiusEarth")),
               field + ".RadiusEarth");
  equal_number(actual.mass_earth, number(expected.at("MassEarth")),
               field + ".MassEarth");
  const auto &environment = expected.at("Environment");
  equal_number(actual.environment.gravity_g, number(environment.at("GravityG")),
               field + ".Environment.GravityG");
  equal_number(actual.environment.temperature_kelvin,
               number(environment.at("TemperatureKelvin")),
               field + ".Environment.TemperatureKelvin");
  equal_number(actual.environment.pressure_kpa,
               number(environment.at("PressureKPa")),
               field + ".Environment.PressureKPa");
  check(static_cast<int>(actual.environment.atmosphere) ==
            environment.at("Atmosphere").get<int>(),
        field + ".Environment.Atmosphere");
  check(static_cast<int>(actual.environment.available_solvent) ==
            environment.at("AvailableSolvent").get<int>(),
        field + ".Environment.AvailableSolvent");
  equal_number(actual.environment.radiation_hazard,
               number(environment.at("RadiationHazard")),
               field + ".Environment.RadiationHazard");
  check(actual.environment.is_immersed_environment ==
            environment.at("IsImmersedEnvironment").get<bool>(),
        field + ".Environment.IsImmersedEnvironment");
  check(actual.environment.has_solid_surface ==
            environment.at("HasSolidSurface").get<bool>(),
        field + ".Environment.HasSolidSurface");
  check(actual.legacy_colonization_candidate ==
            expected.at("LegacyColonizationCandidate").get<bool>(),
        field + ".LegacyColonizationCandidate");
  check(actual.has_rare_resource == expected.at("HasRareResource").get<bool>(),
        field + ".HasRareResource");
  check(actual.has_anomaly == expected.at("HasAnomaly").get<bool>(),
        field + ".HasAnomaly");
  check(actual.has_pre_warp_civilization ==
            expected.at("HasPreWarpCivilization").get<bool>(),
        field + ".HasPreWarpCivilization");
  equal_number(actual.orbital_eccentricity,
               expected.contains("OrbitalEccentricity")
                   ? number(expected.at("OrbitalEccentricity"))
                   : 0.0,
               field + ".OrbitalEccentricity");
  equal_number(actual.orbital_inclination_degrees,
               expected.contains("OrbitalInclinationDegrees")
                   ? number(expected.at("OrbitalInclinationDegrees"))
                   : 0.0,
               field + ".OrbitalInclinationDegrees");
}
template <class T, class Equal>
void equal_list(std::span<const T> actual, const Json &expected,
                const std::string &field, Equal equal) {
  check(expected.is_array() && actual.size() == expected.size(),
        field + ": count");
  for (std::size_t index = 0; index < actual.size(); ++index)
    equal(actual[index], expected[index],
          field + "[" + std::to_string(index) + "]");
}
void equal_economy(const CivilizationEconomy &actual, const Json &expected,
                   const std::string &field) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        field + ".CivilizationId");
#define ECONOMY_NUMBER(native_name, json_name)                                 \
  equal_number(actual.native_name, number(expected.at(json_name)),             \
               field + "." json_name)
  ECONOMY_NUMBER(credits, "Credits");
  ECONOMY_NUMBER(industry, "Industry");
  ECONOMY_NUMBER(science, "Science");
  ECONOMY_NUMBER(last_credits_per_second, "LastCreditsPerSecond");
  ECONOMY_NUMBER(last_industry_per_second, "LastIndustryPerSecond");
  ECONOMY_NUMBER(last_science_per_second, "LastSciencePerSecond");
  ECONOMY_NUMBER(last_research_spending_per_day, "LastResearchSpendingPerDay");
  ECONOMY_NUMBER(last_research_funding_fraction, "LastResearchFundingFraction");
  ECONOMY_NUMBER(operating_arrears, "OperatingArrears");
  ECONOMY_NUMBER(last_base_operations_funding_fraction,
                 "LastBaseOperationsFundingFraction");
#undef ECONOMY_NUMBER
  check(actual.industry_priority.has_value() !=
            expected.at("IndustryPriority").is_null(),
        field + ".IndustryPriority presence");
  if (actual.industry_priority)
    check(static_cast<int>(*actual.industry_priority) ==
              expected.at("IndustryPriority").get<int>(),
          field + ".IndustryPriority");
}
void equal_technology(const TechnologyState &actual, const Json &expected,
                      const std::string &field) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        field + ".CivilizationId");
  check(Json(actual.completed_technology_ids.values()) ==
            expected.at("CompletedTechnologyIds"),
        field + ".CompletedTechnologyIds");
  equal_optional(actual.active_research_id, expected.at("ActiveResearchId"),
                 field + ".ActiveResearchId");
  equal_number(actual.active_research_progress,
               number(expected.at("ActiveResearchProgress")),
               field + ".ActiveResearchProgress");
}
void equal_construction(const ConstructionState &actual, const Json &expected,
                        const std::string &field) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        field + ".CivilizationId");
  check(actual.completed_project_ids ==
            expected.at("CompletedProjectIds").get<std::vector<std::string>>(),
        field + ".CompletedProjectIds");
  equal_optional(actual.active_project_id, expected.at("ActiveProjectId"),
                 field + ".ActiveProjectId");
  equal_number(actual.active_project_progress,
               number(expected.at("ActiveProjectProgress")),
               field + ".ActiveProjectProgress");
  equal_number(actual.active_project_authorization_credits,
               number(expected.at("ActiveProjectAuthorizationCredits")),
               field + ".ActiveProjectAuthorizationCredits");
  check(actual.queued_projects.size() == expected.at("QueuedProjects").size(),
        field + ".QueuedProjects count");
  for (std::size_t index = 0; index < actual.queued_projects.size(); ++index) {
    const auto &entry = actual.queued_projects[index];
    const auto &wanted = expected.at("QueuedProjects")[index];
    check(entry.project_id == wanted.at("ProjectId").get<std::string>(),
          field + ".QueuedProjects.ProjectId");
    equal_number(entry.authorization_credits,
                 number(wanted.at("AuthorizationCredits")),
                 field + ".QueuedProjects.AuthorizationCredits");
  }
}
void equal_shipyard(const ShipyardState &actual, const Json &expected,
                    const std::string &field) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        field + ".CivilizationId");
  check(actual.next_order_sequence ==
            expected.at("NextOrderSequence").get<std::int64_t>(),
        field + ".NextOrderSequence");
  equal_optional(actual.active_design_id, expected.at("ActiveDesignId"),
                 field + ".ActiveDesignId");
  equal_optional(actual.active_order_id, expected.at("ActiveOrderId"),
                 field + ".ActiveOrderId");
  equal_number(actual.active_build_progress,
               number(expected.at("ActiveBuildProgress")),
               field + ".ActiveBuildProgress");
  equal_number(actual.active_authorization_credits,
               number(expected.at("ActiveAuthorizationCredits")),
               field + ".ActiveAuthorizationCredits");
  equal_number(actual.reserved_population_millions,
               number(expected.at("ReservedPopulationMillions")),
               field + ".ReservedPopulationMillions");
  equal_optional(actual.reserved_population_species_id,
                 expected.at("ReservedPopulationSpeciesId"),
                 field + ".ReservedPopulationSpeciesId");
  equal_optional(actual.reserved_population_source_colony_id,
                 expected.at("ReservedPopulationSourceColonyId"),
                 field + ".ReservedPopulationSourceColonyId");
  check(actual.queued_builds.size() == expected.at("QueuedBuilds").size(),
        field + ".QueuedBuilds count");
  for (std::size_t index = 0; index < actual.queued_builds.size(); ++index) {
    const auto &entry = actual.queued_builds[index];
    const auto &wanted = expected.at("QueuedBuilds")[index];
    check(entry.order_id == wanted.at("OrderId").get<std::string>(),
          field + ".QueuedBuilds.OrderId");
    check(entry.design_id == wanted.at("DesignId").get<std::string>(),
          field + ".QueuedBuilds.DesignId");
    equal_number(entry.authorization_credits,
                 number(wanted.at("AuthorizationCredits")),
                 field + ".QueuedBuilds.AuthorizationCredits");
    equal_number(entry.reserved_population_millions,
                 number(wanted.at("ReservedPopulationMillions")),
                 field + ".QueuedBuilds.ReservedPopulationMillions");
    equal_optional(entry.reserved_population_species_id,
                   wanted.at("ReservedPopulationSpeciesId"),
                   field + ".QueuedBuilds.ReservedPopulationSpeciesId");
    equal_optional(entry.reserved_population_source_colony_id,
                   wanted.at("ReservedPopulationSourceColonyId"),
                   field + ".QueuedBuilds.ReservedPopulationSourceColonyId");
  }
  check(actual.pending_build_count() ==
            expected.at("PendingBuildCount").get<int>(),
        field + ".PendingBuildCount");
}

void equal_campaign(const FreshCampaignState &actual, const Json &expected,
                    const std::string &field) {
  check(actual.seed == expected.at("Seed").get<std::int64_t>(),
        field + ".Seed");
  equal_list<StellarSystem>(actual.systems, expected.at("Systems"),
                            field + ".Systems", equal_system);
  equal_list<PlanetaryBody>(actual.bodies, expected.at("Bodies"),
                            field + ".Bodies", equal_body);
  equal_list<Civilization>(actual.civilizations, expected.at("Civilizations"),
                           field + ".Civilizations", equal_civilization);
  equal_list<FleetState>(actual.fleets, expected.at("Fleets"),
                         field + ".Fleets", equal_fleet);
  equal_list<Colony>(actual.colonies, expected.at("Colonies"),
                     field + ".Colonies", equal_colony);
  equal_list<CivilizationEconomy>(actual.economies, expected.at("Economies"),
                                  field + ".Economies", equal_economy);
  equal_list<TechnologyState>(actual.technologies, expected.at("Technologies"),
                              field + ".Technologies", equal_technology);
  equal_list<ConstructionState>(actual.construction,
                                expected.at("Construction"),
                                field + ".Construction", equal_construction);
  equal_list<ShipyardState>(actual.shipyards, expected.at("Shipyards"),
                            field + ".Shipyards", equal_shipyard);
  check(actual.player_civilization_id ==
            expected.at("PlayerCivilizationId").get<int>(),
        field + ".PlayerCivilizationId");

  const auto &knowledge = expected.at("Knowledge");
  check(actual.knowledge.galactic_core_observers() ==
            knowledge.at("CoreObservers").get<std::vector<int>>(),
        field + ".Knowledge.CoreObservers");
  check(knowledge.at("Observers").size() == actual.civilizations.size(),
        field + ".Knowledge.Observers count");
  for (std::size_t index = 0; index < actual.civilizations.size(); ++index) {
    const auto civilization_id = actual.civilizations[index].id;
    const auto &observer = knowledge.at("Observers")[index];
    check(observer.at("CivilizationId").get<int>() == civilization_id,
          field + ".Knowledge.CivilizationId");
    check(actual.knowledge.has_galactic_core_access(civilization_id) ==
              observer.at("HasCoreAccess").get<bool>(),
          field + ".Knowledge.HasCoreAccess");
    check(actual.knowledge.is_galactic_core_discovered(civilization_id) ==
              observer.at("CoreDiscovered").get<bool>(),
          field + ".Knowledge.CoreDiscovered");
    check(actual.knowledge.known_systems(civilization_id) ==
              observer.at("KnownSystems").get<std::vector<int>>(),
          field + ".Knowledge.KnownSystems");
    check(actual.knowledge.known_civilizations(civilization_id) ==
              observer.at("KnownCivilizations").get<std::vector<int>>(),
          field + ".Knowledge.KnownCivilizations");
    const auto surveys =
        actual.knowledge.system_survey_knowledge(civilization_id);
    check(surveys.size() == observer.at("Survey").size(),
          field + ".Knowledge.Survey count");
    for (std::size_t survey_index = 0; survey_index < surveys.size();
         ++survey_index) {
      const auto &wanted = observer.at("Survey")[survey_index];
      check(surveys[survey_index].system_id == wanted.at("SystemId").get<int>(),
            field + ".Knowledge.Survey.SystemId");
      check(static_cast<int>(surveys[survey_index].level) ==
                wanted.at("Level").get<int>(),
            field + ".Knowledge.Survey.Level");
      equal_number(surveys[survey_index].progress,
                   number(wanted.at("Progress")),
                   field + ".Knowledge.Survey.Progress");
    }
  }
  check(actual.core.has_value() != expected.at("Core").is_null(),
        field + ".Core presence");
  if (actual.core) {
    check(expected.at("Core").at("LandmarkKey") == "galactic-core-smbh-v1",
          field + ".Core.LandmarkKey");
    equal_number(actual.core->position.x, number(expected.at("Core").at("X")),
                 field + ".Core.X");
    equal_number(actual.core->position.y, number(expected.at("Core").at("Y")),
                 field + ".Core.Y");
    equal_number(actual.core->exclusion_radius,
                 number(expected.at("Core").at("ExclusionRadius")),
                 field + ".Core.ExclusionRadius");
  }
}

struct ErrorInfo {
  std::string type;
  std::string message;
};

ErrorInfo classify_error(const std::exception &error) {
  if (dynamic_cast<const std::out_of_range *>(&error) != nullptr)
    return {"ArgumentOutOfRangeException", error.what()};
  if (dynamic_cast<const std::invalid_argument *>(&error) != nullptr)
    return {"ArgumentException", error.what()};
  return {"InvalidOperationException", error.what()};
}

std::vector<CivilizationOffice> founding_roster(int civilization_id) {
  struct Office {
    const char *id;
    const char *name;
  };
  static constexpr Office offices[] = {
      {"FleetCommander", "Commander Elena Voss"},
      {"ChiefScientist", "Dr. Amara Chen"},
      {"Diplomat", "Ambassador Mara Okafor"},
      {"Governor", "Governor Elias Ward"},
      {"EconomicAdvisor", "Economic Advisor"},
      {"OperationsOfficer", "Operations Officer"},
      {"ExpeditionCommander", "Expedition Commander"},
  };
  std::vector<CivilizationOffice> result;
  for (const auto &office : offices) {
    const auto prefix =
        "civ-" + std::to_string(civilization_id) + ":" + office.id + ":founder";
    result.push_back({office.id, {prefix, office.name, {}, {}}});
  }
  return result;
}

struct WarpResult {
  std::int64_t seed{};
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<Colony> colonies_before;
  std::vector<FleetState> fleets;
  std::vector<Colony> colonies_after;
  double population_before{};
  double population_after{};
  double embarked_population{};
};

WarpResult run_warp_composition(std::int64_t seed) {
  WarpResult result;
  result.seed = seed;
  StellarSystem sol;
  sol.id = 0;
  sol.name = "Sol";
  sol.position = {0.0F, 0.0F, std::nullopt};
  sol.primary = StellarClass::GYellowDwarf;
  sol.catalog_preset_id = std::string(sol_catalog_preset_id);
  sol.has_habitable_world = true;
  result.systems.push_back(sol);
  result.bodies = generate_planetary_catalog(seed, result.systems);
  result.civilizations.push_back({42,
                                  "Warp Reservation",
                                  0,
                                  CivilizationArchetype::Adaptive,
                                  {.1, .2, .3, .4, .5, .6, false},
                                  true,
                                  CivilizationDevelopmentStage::WarpCapable,
                                  false,
                                  true,
                                  false,
                                  "terran_baseline",
                                  founding_roster(42)});
  result.colonies_after = seed_colonies(result.civilizations, result.bodies);
  result.colonies_before = result.colonies_after;
  for (const auto &colony : result.colonies_before)
    result.population_before += colony.population_millions;
  result.fleets = seed_fleets(result.systems, result.civilizations,
                              std::span<Colony>(result.colonies_after));
  for (const auto &colony : result.colonies_after)
    result.population_after += colony.population_millions;
  for (const auto &fleet : result.fleets)
    result.embarked_population += fleet.embarked_population_millions;
  return result;
}

void equal_warp(const WarpResult &actual, const Json &expected,
                const std::string &field) {
  check(actual.seed == expected.at("Seed").get<std::int64_t>(),
        field + ".Seed");
  equal_list<StellarSystem>(actual.systems, expected.at("Systems"),
                            field + ".Systems", equal_system);
  equal_list<PlanetaryBody>(actual.bodies, expected.at("Bodies"),
                            field + ".Bodies", equal_body);
  equal_list<Civilization>(actual.civilizations, expected.at("Civilizations"),
                           field + ".Civilizations", equal_civilization);
  equal_list<Colony>(actual.colonies_before, expected.at("ColoniesBefore"),
                     field + ".ColoniesBefore", equal_colony);
  equal_list<FleetState>(actual.fleets, expected.at("Fleets"),
                         field + ".Fleets", equal_fleet);
  equal_list<Colony>(actual.colonies_after, expected.at("ColoniesAfter"),
                     field + ".ColoniesAfter", equal_colony);
  equal_number(actual.population_before,
               number(expected.at("PopulationBefore")),
               field + ".PopulationBefore");
  equal_number(actual.population_after, number(expected.at("PopulationAfter")),
               field + ".PopulationAfter");
  equal_number(actual.embarked_population,
               number(expected.at("EmbarkedPopulation")),
               field + ".EmbarkedPopulation");
  equal_number(actual.population_before,
               actual.population_after + actual.embarked_population,
               field + ".PopulationConservation");
}
} // namespace

#if 0
int main(int argc, char **argv) {
  try {
    check(argc == 3,
          "usage: fresh_campaign_tests <fixture.json> <catalog.json>");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture");
    const auto fixture = Json::parse(stream);
    check(fixture.at("Format") == "stellar-fresh-campaign-oracle-v1",
          "Unsupported fixture format");
    const auto catalog = load_nearby_catalog(argv[2]);
    check(!catalog.empty(), "Catalog must not be empty");

    int passed = 0;
    for (const auto &test : fixture.at("Cases")) {
      const auto name = test.at("Name").get<std::string>();
      const auto kind = test.at("Kind").get<std::string>();
      const auto &arguments = test.at("Arguments");
      check(kind == "Fresh" || kind == "WarpComposition",
            name + ": unknown kind " + kind);
      check(test.contains("Result") && test.contains("Error"),
            name + ": missing result/error");

      const auto expects_error = !test.at("Error").is_null();
      std::optional<ErrorInfo> wanted_error;
      if (expects_error) {
        wanted_error =
            ErrorInfo{test.at("Error").at("Type").get<std::string>(),
                      test.at("Error").at("Message").get<std::string>()};
        check(wanted_error->type == "ArgumentOutOfRangeException" ||
                  wanted_error->type == "ArgumentException" ||
                  wanted_error->type == "InvalidOperationException",
              name + ": unsupported expected error type");
      }

      std::optional<std::int64_t> seed;
      int system_count = 0;
      int pre_warp_count = 0;
      int ancient_count = 0;
      std::string species;
      if (kind == "Fresh") {
        seed = arguments.at("Seed").get<std::int64_t>();
        system_count = arguments.at("SystemCount").get<int>();
        pre_warp_count = arguments.at("PreWarpCount").get<int>();
        ancient_count = arguments.at("AncientCount").get<int>();
        species = arguments.at("PlayerSpeciesId").get<std::string>();
        check(arguments.at("SettingsProfile") ==
                  "FullGalaxy + GalaxyGenerationSettings.ToSettings defaults",
              name + ": unsupported settings profile");
        check(test.contains("ErrorStage"), name + ": missing error stage");
        if (expects_error) {
          const auto error_stage = test.at("ErrorStage").get<std::string>();
          check(error_stage == "Settings" || error_stage == "Generate",
                name + ": unsupported error stage");
        } else {
          check(test.at("ErrorStage").is_null(),
                name + ": successful case has an error stage");
        }
      } else {
        seed = arguments.at("Seed").get<std::int64_t>();
      }

      std::optional<FreshCampaignState> campaign;
      std::optional<FreshCampaignState> repeated_campaign;
      std::optional<WarpResult> warp;
      std::optional<ErrorInfo> actual_error;
      try {
        if (kind == "Fresh") {
          campaign =
              seed_fresh_campaign(*seed, catalog, system_count, pre_warp_count,
                                  ancient_count, species);
          if (!expects_error)
            repeated_campaign =
                seed_fresh_campaign(*seed, catalog, system_count,
                                    pre_warp_count, ancient_count, species);
        } else {
          warp = run_warp_composition(*seed);
        }
      } catch (const std::exception &error) {
        actual_error = classify_error(error);
      }

      check(actual_error.has_value() == expects_error,
            name + ": error presence mismatch" +
                (actual_error ? " (" + actual_error->type + ": " +
                                    actual_error->message + ")"
                              : ""));
      if (wanted_error) {
        check(actual_error->type == wanted_error->type,
              name + ": error type mismatch: " + actual_error->type);
        check(actual_error->message == wanted_error->message,
              name + ": error message mismatch: " + actual_error->message);
        check(test.at("Result").is_null(),
              name + ": error result must be null");
      } else {
        check(!test.at("Result").is_null(), name + ": result must be present");
        if (kind == "Fresh") {
          check(campaign.has_value(), name + ": campaign result missing");
          equal_campaign(*campaign, test.at("Result"), name);
          check(repeated_campaign.has_value(),
                name + ": repeated campaign result missing");
          equal_campaign(*repeated_campaign, test.at("Result"),
                         name + ".Repeated");
          check(campaign->used_constrained_home_fallback ==
                    repeated_campaign->used_constrained_home_fallback,
                name + ": fallback metadata is not deterministic");
        } else {
          check(warp.has_value(), name + ": warp result missing");
          equal_warp(*warp, test.at("Result"), name);
        }
      }
      ++passed;
    }
    std::cout << "fresh campaign parity passed " << passed << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fresh campaign parity failed: " << error.what() << '\n';
    return 1;
  }
}

#endif

namespace {
Json encoded_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (std::isinf(value))
    return value > 0 ? Json("Infinity") : Json("-Infinity");
  return value;
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if ((actual.is_number_integer() || actual.is_number_unsigned()) &&
      (expected.is_number_integer() || expected.is_number_unsigned())) {
    check(actual == expected, field + ": integer actual=" + actual.dump() +
                                  " expected=" + expected.dump());
    return;
  }
  if (actual.is_number() && expected.is_number()) {
    equal_number(actual.get<double>(), expected.get<double>(), field);
    return;
  }
  if (actual.is_array() && expected.is_array()) {
    check(actual.size() == expected.size(), field + ": array size");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (actual.is_object() && expected.is_object()) {
    check(actual.size() == expected.size(), field + ": object size");
    for (const auto &[key, value] : expected.items()) {
      check(actual.contains(key), field + ": missing " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  check(actual == expected,
        field + ": actual=" + actual.dump() + " expected=" + expected.dump());
}

struct RuntimeWorld {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<Colony> colonies;
  std::vector<FleetState> fleets;
  std::vector<CivilizationEconomy> economies;
  CivilizationKnowledgeState knowledge;
  std::unique_ptr<InterstellarLaneNetwork> lanes;
  ColonizationWorldView view() {
    return {systems, bodies,    civilizations, colonies,
            fleets,  economies, knowledge,     *lanes};
  }
};

PlanetaryBody parse_runtime_body(const Json &value) {
  PlanetaryBody result;
  result.id = value.at("Id").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.parent_body_id = optional_value<int>(value.at("ParentBodyId"));
  result.orbit_index = value.at("OrbitIndex").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.kind = static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>());
  result.radius_earth = number(value.at("RadiusEarth"));
  result.mass_earth = number(value.at("MassEarth"));
  const auto &environment = value.at("Environment");
  result.environment = {number(environment.at("GravityG")),
                        number(environment.at("TemperatureKelvin")),
                        number(environment.at("PressureKPa")),
                        static_cast<PlanetaryAtmosphereRegime>(
                            environment.at("Atmosphere").get<int>()),
                        static_cast<PlanetarySolventRegime>(
                            environment.at("AvailableSolvent").get<int>()),
                        number(environment.at("RadiationHazard")),
                        environment.at("IsImmersedEnvironment").get<bool>(),
                        environment.at("HasSolidSurface").get<bool>()};
  result.legacy_colonization_candidate =
      value.at("LegacyColonizationCandidate").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  return result;
}

CivilizationEconomy parse_runtime_economy(const Json &value) {
  CivilizationEconomy result;
  result.civilization_id = value.at("CivilizationId").get<int>();
#define READ_ECONOMY(native_name, json_name)                                   \
  result.native_name = number(value.at(json_name))
  READ_ECONOMY(credits, "Credits");
  READ_ECONOMY(industry, "Industry");
  READ_ECONOMY(science, "Science");
  READ_ECONOMY(last_credits_per_second, "LastCreditsPerSecond");
  READ_ECONOMY(last_industry_per_second, "LastIndustryPerSecond");
  READ_ECONOMY(last_science_per_second, "LastSciencePerSecond");
  READ_ECONOMY(last_research_spending_per_day, "LastResearchSpendingPerDay");
  READ_ECONOMY(last_research_funding_fraction, "LastResearchFundingFraction");
  READ_ECONOMY(operating_arrears, "OperatingArrears");
  READ_ECONOMY(last_base_operations_funding_fraction,
               "LastBaseOperationsFundingFraction");
#undef READ_ECONOMY
  if (!value.at("IndustryPriority").is_null())
    result.industry_priority =
        static_cast<IndustryPriority>(value.at("IndustryPriority").get<int>());
  return result;
}

RuntimeWorld parse_runtime_world(const Json &value, const std::string &field) {
  RuntimeWorld result;
  for (const auto &item : value.at("Systems"))
    result.systems.push_back(parse_system(item));
  for (const auto &item : value.at("Bodies"))
    result.bodies.push_back(parse_runtime_body(item));
  for (std::size_t index = 0; index < value.at("Civilizations").size(); ++index)
    result.civilizations.push_back(parse_civilization(
        value.at("Civilizations")[index],
        field + ".Civilizations[" + std::to_string(index) + "]"));
  for (const auto &item : value.at("Colonies"))
    result.colonies.push_back(parse_colony(item));
  for (const auto &item : value.at("Fleets"))
    result.fleets.push_back(parse_fleet(item));
  for (const auto &item : value.at("Economies"))
    result.economies.push_back(parse_runtime_economy(item));
  for (const auto &observer : value.at("Knowledge")) {
    const auto civilization_id = observer.at("CivilizationId").get<int>();
    for (const auto &survey : observer.at("Surveys")) {
      const auto system_id = survey.at("SystemId").get<int>();
      const auto level = survey.at("Level").get<int>();
      const auto progress = number(survey.at("Progress"));
      if (level == 1)
        result.knowledge.reveal_system(civilization_id, system_id);
      else if (level == 2)
        result.knowledge.advance_system_survey(civilization_id, system_id,
                                               progress);
      else if (level == 3)
        result.knowledge.mark_system_fully_surveyed(civilization_id, system_id);
      else
        fail(field + ": unsupported survey level");
    }
  }
  result.lanes = std::make_unique<InterstellarLaneNetwork>(
      std::span<const StellarSystem>(result.systems));
  return result;
}

void equal_runtime_world(const RuntimeWorld &actual, const Json &expected,
                         const std::string &field) {
  equal_list<StellarSystem>(actual.systems, expected.at("Systems"),
                            field + ".Systems", equal_system);
  equal_list<PlanetaryBody>(actual.bodies, expected.at("Bodies"),
                            field + ".Bodies", equal_body);
  equal_list<Civilization>(actual.civilizations, expected.at("Civilizations"),
                           field + ".Civilizations", equal_civilization);
  equal_list<Colony>(actual.colonies, expected.at("Colonies"),
                     field + ".Colonies", equal_colony);
  equal_fleets(actual.fleets, expected.at("Fleets"), field + ".Fleets");
  equal_list<CivilizationEconomy>(actual.economies, expected.at("Economies"),
                                  field + ".Economies", equal_economy);
  check(actual.civilizations.size() == expected.at("Knowledge").size(),
        field + ".Knowledge observer count");
  for (const auto &observer : expected.at("Knowledge")) {
    const auto civilization_id = observer.at("CivilizationId").get<int>();
    for (const auto &system : actual.systems) {
      auto expected_level = 0;
      double expected_progress = 0;
      for (const auto &survey : observer.at("Surveys"))
        if (survey.at("SystemId").get<int>() == system.id) {
          expected_level = survey.at("Level").get<int>();
          expected_progress = number(survey.at("Progress"));
        }
      check(static_cast<int>(actual.knowledge.system_survey_level(
                civilization_id, system.id)) == expected_level,
            field + ".Knowledge.Level");
      equal_number(
          actual.knowledge.system_survey_progress(civilization_id, system.id),
          expected_progress, field + ".Knowledge.Progress");
    }
  }
}

Json encode_reach(const MissionReachAssessment &value) {
  return {{"IsSupported", value.is_supported},
          {"IsAuthoritative", value.is_authoritative},
          {"Reason", value.reason},
          {"RouteSystemIds", value.route_system_ids
                                 ? Json(*value.route_system_ids)
                                 : Json(nullptr)},
          {"RouteDistanceLightYears",
           encoded_number(value.route_distance_light_years)}};
}
Json encode_colony_candidate(const ColonizationOpportunityCandidate &value) {
  return {
      {"SystemId", value.system_id},
      {"SystemName", value.system_name},
      {"PlanetaryBodyId", value.planetary_body_id},
      {"PlanetaryBodyName", value.planetary_body_name},
      {"PlanetaryBodyKind", value.planetary_body_kind},
      {"FleetId", value.fleet_id},
      {"PassengerSpeciesId", value.passenger_species_id},
      {"ColonizationViability", value.colonization_viability},
      {"NaturalHabitability", encoded_number(value.natural_habitability)},
      {"UnprotectedOperationalCapacity",
       encoded_number(value.unprotected_operational_capacity)},
      {"LimitingFactor", value.limiting_factor},
      {"RequiresGravityMitigation", value.requires_gravity_mitigation},
      {"RequiresThermalControl", value.requires_thermal_control},
      {"RequiresPressureControl", value.requires_pressure_control},
      {"RequiresSealedHabitat", value.requires_sealed_habitat},
      {"RequiresArtificialBiosphere", value.requires_artificial_biosphere},
      {"RequiresRadiationShielding", value.requires_radiation_shielding},
      {"HasSolidSurface", value.has_solid_surface},
      {"HasNativePreWarpCivilization", value.has_native_pre_warp_civilization},
      {"HasRareResource", value.has_rare_resource},
      {"SystemOccupied", value.system_occupied},
      {"DistanceFromFleet", encoded_number(value.distance_from_fleet)},
      {"Reach", encode_reach(value.reach)},
      {"CanOrder", value.can_order},
      {"Reason", value.reason},
      {"SystemReservedByFriendlyColonyMission",
       value.system_reserved_by_friendly_colony_mission},
      {"ReservedByFleetId", value.reserved_by_fleet_id
                                ? Json(*value.reserved_by_fleet_id)
                                : Json(nullptr)}};
}
Json encode_colony_plan(const ColonizationOpportunityPlan &value) {
  Json candidates = Json::array();
  for (const auto &candidate : value.candidates)
    candidates.push_back(encode_colony_candidate(candidate));
  return {{"FleetId", value.fleet_id},
          {"FleetName", value.fleet_name},
          {"CivilizationId", value.civilization_id},
          {"PassengerSpeciesId", value.passenger_species_id},
          {"PassengerSpeciesName", value.passenger_species_name},
          {"EmbarkedPopulationMillions",
           encoded_number(value.embarked_population_millions)},
          {"CanReceiveOrders", value.can_receive_orders},
          {"Status", value.status},
          {"Candidates", candidates}};
}
Json encode_colony_assessment(const ColonizationOrderAssessment &value) {
  return {{"Accepted", value.accepted},
          {"Message", value.message},
          {"Candidate", value.candidate
                            ? encode_colony_candidate(*value.candidate)
                            : Json(nullptr)}};
}
Json encode_outpost_candidate(
    const ResourceOutpostOpportunityCandidate &value) {
  return {{"SystemId", value.system_id},
          {"SystemName", value.system_name},
          {"PlanetaryBodyId", value.planetary_body_id},
          {"PlanetaryBodyName", value.planetary_body_name},
          {"FleetId", value.fleet_id},
          {"PersonnelSpeciesId", value.personnel_species_id},
          {"NaturalHabitability", encoded_number(value.natural_habitability)},
          {"UnprotectedOperationalCapacity",
           encoded_number(value.unprotected_operational_capacity)},
          {"LimitingFactor", value.limiting_factor},
          {"HasRareResource", value.has_rare_resource},
          {"DepositMaterialName", value.deposit_material_name},
          {"DepositGrade", value.deposit_grade},
          {"DepositAccessibility", encoded_number(value.deposit_accessibility)},
          {"ExtractionYieldMultiplier",
           encoded_number(value.extraction_yield_multiplier)},
          {"InitialDepositMaterials",
           encoded_number(value.initial_deposit_materials)},
          {"SystemOccupied", value.system_occupied},
          {"IsTooHarshForColony", value.is_too_harsh_for_colony},
          {"DistanceFromFleet", encoded_number(value.distance_from_fleet)},
          {"Reach", encode_reach(value.reach)},
          {"CanOrder", value.can_order},
          {"Reason", value.reason}};
}
Json encode_outpost_plan(const ResourceOutpostOpportunityPlan &value) {
  Json candidates = Json::array();
  for (const auto &candidate : value.candidates)
    candidates.push_back(encode_outpost_candidate(candidate));
  return {{"FleetId", value.fleet_id},
          {"FleetName", value.fleet_name},
          {"CivilizationId", value.civilization_id},
          {"PersonnelSpeciesId", value.personnel_species_id},
          {"PersonnelSpeciesName", value.personnel_species_name},
          {"PersonnelMillions", encoded_number(value.personnel_millions)},
          {"CanReceiveOrders", value.can_receive_orders},
          {"Status", value.status},
          {"Candidates", candidates}};
}
Json encode_order(const ColonyOrderResult &value) {
  return {{"Accepted", value.accepted}, {"Message", value.message}};
}
Json encode_event(const ColonizationEvent &value) {
  return {{"CivilizationId", value.civilization_id},
          {"FleetId", value.fleet_id},
          {"SystemId", value.system_id},
          {"ColonyId", value.colony_id},
          {"Message", value.message}};
}
Json encode_body(const PlanetaryBody &value) {
  return {
      {"Id", value.id},
      {"SystemId", value.system_id},
      {"ParentBodyId",
       value.parent_body_id ? Json(*value.parent_body_id) : Json(nullptr)},
      {"OrbitIndex", value.orbit_index},
      {"Name", value.name},
      {"Kind", value.kind},
      {"RadiusEarth", value.radius_earth},
      {"MassEarth", value.mass_earth},
      {"Environment",
       {{"GravityG", value.environment.gravity_g},
        {"TemperatureKelvin", value.environment.temperature_kelvin},
        {"PressureKPa", value.environment.pressure_kpa},
        {"Atmosphere", value.environment.atmosphere},
        {"AvailableSolvent", value.environment.available_solvent},
        {"RadiationHazard", value.environment.radiation_hazard},
        {"IsImmersedEnvironment", value.environment.is_immersed_environment},
        {"HasSolidSurface", value.environment.has_solid_surface}}},
      {"LegacyColonizationCandidate", value.legacy_colonization_candidate},
      {"HasRareResource", value.has_rare_resource},
      {"HasAnomaly", value.has_anomaly},
      {"HasPreWarpCivilization", value.has_pre_warp_civilization}};
}

struct ReachCall {
  int civilization_id{}, fleet_id{}, target_system_id{};
  InterstellarMissionKind mission_kind{};
};
Json encode_calls(const std::vector<ReachCall> &calls) {
  Json result = Json::array();
  for (const auto &call : calls)
    result.push_back({{"CivilizationId", call.civilization_id},
                      {"FleetId", call.fleet_id},
                      {"TargetSystemId", call.target_system_id},
                      {"MissionKind", call.mission_kind}});
  return result;
}
struct CapturedError {
  std::string type, message;
};
template <class Operation>
void capture(Operation operation, std::optional<CapturedError> &error) {
  try {
    operation();
  } catch (const std::invalid_argument &value) {
    error = CapturedError{"ArgumentException", value.what()};
  } catch (const std::out_of_range &value) {
    error = CapturedError{"ArgumentOutOfRangeException", value.what()};
  } catch (const std::overflow_error &value) {
    error = CapturedError{"OverflowException", value.what()};
  } catch (const std::runtime_error &value) {
    error = CapturedError{"InvalidOperationException", value.what()};
  }
}
void compare_error(const std::optional<CapturedError> &actual,
                   const Json &expected, const std::string &field) {
  check(actual.has_value() != expected.is_null(), field + ": presence");
  if (actual) {
    check(actual->type == expected.at("Type").get<std::string>(),
          field + ": type " + actual->type);
    check(actual->message == expected.at("Message").get<std::string>(),
          field + ": message actual=" + actual->message +
              " expected=" + expected.at("Message").get<std::string>());
  }
}

enum class LegacyKind {
  ColonyBuild,
  ColonyAssess,
  ColonyReach,
  OutpostBuild,
  OutpostOrder,
  IsOutpost
};
LegacyKind legacy_kind(std::string_view value) {
  if (value == "ColonyBuild")
    return LegacyKind::ColonyBuild;
  if (value == "ColonyAssess")
    return LegacyKind::ColonyAssess;
  if (value == "ColonyReach")
    return LegacyKind::ColonyReach;
  if (value == "OutpostBuild")
    return LegacyKind::OutpostBuild;
  if (value == "OutpostAssess")
    return LegacyKind::OutpostOrder;
  if (value == "IsOutpost")
    return LegacyKind::IsOutpost;
  fail("Unknown legacy case kind: " + std::string(value));
}

SettlementReachAssessment make_reach(bool supported, bool throws,
                                     std::string reason, double distance,
                                     std::vector<ReachCall> &calls) {
  return [=, &calls](OperationalReachWorldView, int civilization_id,
                     const FleetState &fleet, int target_system_id,
                     InterstellarMissionKind mission_kind) {
    calls.push_back(
        {civilization_id, fleet.id, target_system_id, mission_kind});
    if (throws)
      throw std::runtime_error("fixture reach failure");
    return MissionReachAssessment{
        supported, false, reason,
        std::vector<int>{fleet.current_system_id.value_or(-1),
                         target_system_id},
        distance};
  };
}

void run_legacy(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = legacy_kind(test.at("Kind").get<std::string>());
  const auto &arguments = test.at("Arguments");
  const auto fleet_id = arguments.at("FleetId").get<int>();
  const auto system_id = arguments.at("SystemId").get<int>();
  const auto body_id = arguments.at("BodyId").get<int>();
  const auto maximum = arguments.at("MaximumCandidates").get<int>();
  const auto injected = arguments.at("InjectedReach").get<bool>();
  const auto supported = arguments.at("ReachSupported").get<bool>();
  const auto throws = arguments.at("ReachThrows").get<bool>();
  const auto reason = arguments.at("ReachReason").get<std::string>();
  const auto distance = number(arguments.at("ReachDistance"));
  const auto expected_result = test.at("Result");
  const auto expected_error = test.at("Error");
  const auto expected_after = test.at("After");
  const auto expected_calls = test.at("ReachCalls");
  equal_json(arguments.at("World"), test.at("Before"), name + ".Before");
  auto world = parse_runtime_world(arguments.at("World"), name);
  equal_runtime_world(world, test.at("Before"), name + ".DecodedBefore");
  if (kind == LegacyKind::IsOutpost)
    check(!world.fleets.empty(), name + ": missing IsOutpost fleet input");
  std::vector<ReachCall> calls;
  auto callback = make_reach(supported, throws, reason, distance, calls);
  ColonizationSimulation simulation(injected ? callback
                                             : SettlementReachAssessment{});
  std::optional<ColonizationOpportunityPlan> colony_plan;
  std::optional<ColonizationOrderAssessment> colony_assessment;
  std::optional<MissionReachAssessment> reach_assessment;
  std::optional<ResourceOutpostOpportunityPlan> outpost_plan;
  std::optional<ColonyOrderResult> order_result;
  std::optional<bool> outpost_result;
  std::optional<CapturedError> error;
  capture(
      [&] {
        switch (kind) {
        case LegacyKind::ColonyBuild:
          colony_plan =
              simulation.get_opportunity_plan(world.view(), fleet_id, maximum);
          break;
        case LegacyKind::ColonyAssess:
          colony_assessment = simulation.assess_colony_order(
              world.view(), fleet_id, system_id, body_id);
          break;
        case LegacyKind::ColonyReach:
          reach_assessment = simulation.assess_operational_reach(
              world.view(), fleet_id, system_id);
          break;
        case LegacyKind::OutpostBuild:
          outpost_plan = simulation.get_resource_outpost_opportunity_plan(
              world.view(), fleet_id, maximum);
          break;
        case LegacyKind::OutpostOrder:
          order_result = simulation.issue_resource_outpost_fleet_order(
              world.view(), fleet_id, system_id, body_id);
          break;
        case LegacyKind::IsOutpost:
          outpost_result = ResourceOutpostOpportunityPlanner::is_outpost_fleet(
              world.fleets.front());
          break;
        }
      },
      error);
  std::optional<Json> result;
  if (colony_plan)
    result = encode_colony_plan(*colony_plan);
  else if (colony_assessment)
    result = encode_colony_assessment(*colony_assessment);
  else if (reach_assessment)
    result = encode_reach(*reach_assessment);
  else if (outpost_plan)
    result = encode_outpost_plan(*outpost_plan);
  else if (order_result)
    result = encode_order(*order_result);
  else if (outpost_result)
    result = *outpost_result;
  compare_error(error, expected_error, name + ".Error");
  if (!error) {
    check(result.has_value(), name + ": missing result");
    equal_json(*result, expected_result, name + ".Result");
  } else
    check(!result, name + ": result on error");
  equal_runtime_world(world, expected_after, name + ".After");
  equal_json(encode_calls(calls), expected_calls, name + ".ReachCalls");
}

struct RuntimeCommand {
  std::string kind;
  double days{};
  int fleet_id{}, civilization_id{}, system_id{}, body_id{}, colony_id{};
};
RuntimeCommand parse_command(const Json &value) {
  RuntimeCommand result{value.at("Kind").get<std::string>(),
                        number(value.at("Days")),
                        value.at("FleetId").get<int>(),
                        value.at("CivilizationId").get<int>(),
                        value.at("SystemId").get<int>(),
                        value.at("BodyId").get<int>(),
                        value.at("ColonyId").get<int>()};
  const std::array names{"Advance",      "Transit",           "Abandon",
                         "ColonyOrder",  "PlayerOrder",       "PlayerBodyOrder",
                         "OutpostOrder", "EstablishmentDays", "Resolve"};
  check(std::find(names.begin(), names.end(), result.kind) != names.end(),
        "Unknown runtime command: " + result.kind);
  return result;
}
void run_sequence(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto &arguments = test.at("Arguments");
  std::vector<RuntimeCommand> commands;
  for (const auto &value : arguments.at("Commands"))
    commands.push_back(parse_command(value));
  const auto supported = arguments.at("ReachSupported").get<bool>();
  const auto throws = arguments.at("ReachThrows").get<bool>();
  const auto expected_results = test.at("Result");
  const auto expected_after = test.at("After");
  const auto expected_calls = test.at("ReachCalls");
  check(commands.size() == expected_results.size(), name + ": result count");
  equal_json(arguments.at("World"), test.at("Before"), name + ".Before");
  auto world = parse_runtime_world(arguments.at("World"), name);
  std::vector<ReachCall> calls;
  auto callback = make_reach(supported, throws, "fixture reach", 7, calls);
  ColonizationSimulation simulation(callback);
  for (std::size_t index = 0; index < commands.size(); ++index) {
    const auto &command = commands[index];
    const auto &expected = expected_results[index];
    equal_json(arguments.at("Commands")[index], expected.at("Command"),
               name + ".Command[" + std::to_string(index) + "]");
    const FleetState *fleet_argument = nullptr;
    const Colony *colony_argument = nullptr;
    if (command.kind == "Abandon" || command.kind == "EstablishmentDays") {
      const auto it = std::find_if(
          world.fleets.begin(), world.fleets.end(),
          [&](const auto &fleet) { return fleet.id == command.fleet_id; });
      check(it != world.fleets.end(), name + ": command fleet argument");
      fleet_argument = &*it;
    }
    if (command.kind == "Resolve") {
      const auto it = std::find_if(
          world.colonies.begin(), world.colonies.end(),
          [&](const auto &colony) { return colony.id == command.colony_id; });
      check(it != world.colonies.end(), name + ": command colony argument");
      colony_argument = &*it;
    }
    std::optional<std::vector<ColonizationEvent>> events;
    std::optional<ColonyOrderResult> order_result;
    std::optional<double> scalar_result;
    std::optional<PlanetaryBody> body_result;
    bool resolved_body = false, void_result = false;
    std::optional<CapturedError> error;
    capture(
        [&] {
          if (command.kind == "Advance")
            events = simulation.advance(world.view(), command.days);
          else if (command.kind == "Transit")
            order_result = simulation.issue_transit_order(
                world.view(), command.civilization_id, command.fleet_id,
                command.system_id);
          else if (command.kind == "Abandon") {
            simulation.abandon_mission_for_transit(
                const_cast<FleetState &>(*fleet_argument));
            void_result = true;
          } else if (command.kind == "ColonyOrder")
            order_result = simulation.issue_colony_fleet_order(
                world.view(), command.fleet_id, command.system_id,
                command.body_id);
          else if (command.kind == "PlayerOrder")
            order_result = simulation.issue_player_colony_order(
                world.view(), command.civilization_id, command.system_id);
          else if (command.kind == "PlayerBodyOrder")
            order_result = simulation.issue_player_colony_order(
                world.view(), command.civilization_id, command.system_id,
                command.body_id);
          else if (command.kind == "OutpostOrder")
            order_result = simulation.issue_resource_outpost_fleet_order(
                world.view(), command.fleet_id, command.system_id,
                command.body_id);
          else if (command.kind == "EstablishmentDays")
            scalar_result = simulation.establishment_days(*fleet_argument);
          else if (command.kind == "Resolve") {
            body_result = simulation.resolve_compatibility_colony_world(
                world.view(), *colony_argument);
            resolved_body = true;
          }
        },
        error);
    std::optional<Json> result;
    if (events) {
      Json encoded = Json::array();
      for (const auto &event : *events)
        encoded.push_back(encode_event(event));
      result = std::move(encoded);
    } else if (order_result)
      result = encode_order(*order_result);
    else if (scalar_result)
      result = encoded_number(*scalar_result);
    else if (resolved_body)
      result = body_result ? encode_body(*body_result) : Json(nullptr);
    else if (void_result)
      result = Json(nullptr);
    compare_error(error, expected.at("Error"),
                  name + ".Error[" + std::to_string(index) + "]");
    if (!error) {
      check(result.has_value(), name + ": missing command result");
      equal_json(*result, expected.at("Result"),
                 name + ".Result[" + std::to_string(index) + "]");
    } else
      check(!result, name + ": result on error");
    equal_runtime_world(world, expected.at("After"),
                        name + ".After[" + std::to_string(index) + "]");
    equal_json(encode_calls(calls), expected.at("ReachCalls"),
               name + ".Calls[" + std::to_string(index) + "]");
  }
  equal_runtime_world(world, expected_after, name + ".FinalAfter");
  equal_json(encode_calls(calls), expected_calls, name + ".FinalCalls");
}

const Json &find_case(const Json &cases, std::string_view name) {
  for (const auto &test : cases)
    if (test.at("Name").get<std::string>() == name)
      return test;
  fail("Missing native boundary seed: " + std::string(name));
}
void native_boundaries(const Json &cases) {
  {
    auto expected = find_case(cases, "runtime-transit-then-abandon")
                        .at("Arguments")
                        .at("World");
    expected.at("Fleets")[0]["MissionOrderRevision"] =
        std::numeric_limits<int>::max();
    auto world = parse_runtime_world(expected, "revision boundary");
    std::vector<ReachCall> calls;
    ColonizationSimulation simulation(
        make_reach(true, false, "fixture reach", 7, calls));
    std::string message;
    try {
      static_cast<void>(
          simulation.issue_transit_order(world.view(), 1, 100, 1));
    } catch (const std::overflow_error &error) {
      message = error.what();
    }
    check(message == "Fleet mission revision space is exhausted.",
          "revision boundary message");
    equal_runtime_world(world, expected, "revision boundary full state");
  }
  {
    auto before = find_case(cases, "runtime-player-body-order")
                      .at("Arguments")
                      .at("World");
    before.at("Fleets")[0]["MissionOrderRevision"] =
        std::numeric_limits<int>::max();
    auto expected = before;
    expected.at("Economies")[0]["Credits"] = 380.0;
    auto world = parse_runtime_world(before, "charged revision boundary");
    std::vector<ReachCall> calls;
    ColonizationSimulation simulation(
        make_reach(true, false, "fixture reach", 7, calls));
    std::string message;
    try {
      static_cast<void>(
          simulation.issue_player_colony_order(world.view(), 1, 1, 10));
    } catch (const std::overflow_error &error) {
      message = error.what();
    }
    check(message == "Fleet mission revision space is exhausted.",
          "charged revision boundary message");
    equal_runtime_world(world, expected,
                        "charged revision boundary full partial state");
  }
  {
    auto expected = find_case(cases, "runtime-player-body-order")
                        .at("Arguments")
                        .at("World");
    expected.at("Colonies")[0]["Id"] = std::numeric_limits<int>::max();
    auto &fleet = expected.at("Fleets")[0];
    fleet["CurrentSystemId"] = 1;
    fleet["DestinationSystemId"] = nullptr;
    fleet["DestinationPlanetaryBodyId"] = 10;
    fleet["SettlementBodyId"] = 10;
    fleet["SettlementDaysCompleted"] = 30;
    auto world = parse_runtime_world(expected, "colony ID boundary");
    ColonizationSimulation simulation;
    std::string message;
    try {
      static_cast<void>(simulation.advance(world.view(), 1));
    } catch (const std::overflow_error &error) {
      message = error.what();
    }
    check(message == "Colony ID exceeds the native integer range.",
          "colony ID boundary message");
    equal_runtime_world(world, expected, "colony ID boundary full state");
  }
  {
    const auto &seed = find_case(cases, "runtime-bodyless-natural-arrival");
    auto before = seed.at("Arguments").at("World");
    auto &before_fleet = before.at("Fleets")[0];
    before_fleet["SettlementBodyId"] = 1;
    before_fleet["SettlementDaysCompleted"] = 30.0;
    before_fleet["MissionOrderRevision"] = std::numeric_limits<int>::max();
    auto expected = before;
    expected.at("Colonies")
        .push_back(seed.at("Result")[1].at("After").at("Colonies")[0]);
    auto &expected_fleet = expected.at("Fleets")[0];
    expected_fleet["EmbarkedPopulationMillions"] = 0.0;
    expected_fleet["EmbarkedPopulationSpeciesId"] = nullptr;
    expected_fleet["IsActive"] = false;
    auto world = parse_runtime_world(before, "arrival revision boundary");
    ColonizationSimulation simulation;
    std::string message;
    try {
      static_cast<void>(simulation.advance(world.view(), 1));
    } catch (const std::overflow_error &error) {
      message = error.what();
    }
    check(message == "Fleet mission revision space is exhausted.",
          "arrival revision boundary message");
    equal_runtime_world(world, expected,
                        "arrival revision boundary full partial state");
  }
}

} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: colonization_runtime_tests <fixture>");
    std::ifstream input(argv[1]);
    check(input.good(), "fixture open");
    Json root;
    input >> root;
    check(root.at("Format") == "stellar-colonization-runtime-oracle-v1",
          "fixture format");
    check(root.at("CaseCount").get<std::size_t>() == root.at("Cases").size(),
          "fixture case count");
    for (const auto *group :
         {&root.at("SourceOnlyNullWorld"), &root.at("SourceOnlyNullFleet")})
      for (const auto &observation : *group) {
        check(observation.at("Error").is_object(),
              "source-only null observation error");
        check(!observation.at("Error").at("Type").get<std::string>().empty(),
              "source-only null observation type");
        check(!observation.at("Error").at("Message").get<std::string>().empty(),
              "source-only null observation message");
      }
    for (const auto &test : root.at("Cases")) {
      const auto kind = test.at("Kind").get<std::string>();
      if (kind == "Sequence")
        run_sequence(test);
      else
        run_legacy(test);
    }
    native_boundaries(root.at("Cases"));
    std::cout << "colonization runtime parity: " << root.at("Cases").size()
              << " actual C# cases and 4 native boundaries passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "colonization runtime parity failure: " << error.what()
              << '\n';
    return 1;
  }
}
