#include <stellar/core/fleet_state.hpp>

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
        field + ": numeric mismatch");
}
template <typename T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
void check_vec2(Vec2 actual, const Json &expected, const std::string &field) {
  equal_number(actual.x, number(expected.at("X")), field + ".X");
  equal_number(actual.y, number(expected.at("Y")), field + ".Y");
}

void check_profile(const CombatProfileDefinition &actual, const Json &expected,
                   const std::string &field) {
  check(actual.id == expected.at("Id").get<std::string>(), field + ".Id");
  equal_number(actual.max_shields, number(expected.at("MaxShields")),
               field + ".MaxShields");
  equal_number(actual.max_armor, number(expected.at("MaxArmor")),
               field + ".MaxArmor");
  equal_number(actual.max_hull, number(expected.at("MaxHull")),
               field + ".MaxHull");
  equal_number(actual.weapon_damage, number(expected.at("WeaponDamage")),
               field + ".WeaponDamage");
  equal_number(actual.weapon_interval_days,
               number(expected.at("WeaponIntervalDays")),
               field + ".WeaponIntervalDays");
  equal_number(actual.retreat_delay_days,
               number(expected.at("RetreatDelayDays")),
               field + ".RetreatDelayDays");
  check(actual.has_weapon() == expected.at("HasWeapon").get<bool>(),
        field + ".HasWeapon");
  equal_number(actual.sustained_damage_per_day(),
               number(expected.at("SustainedDamagePerDay")),
               field + ".SustainedDamagePerDay");
}

CombatProfileDefinition parse_profile(const Json &value) {
  return {value.at("Id").get<std::string>(),
          number(value.at("MaxShields")),
          number(value.at("MaxArmor")),
          number(value.at("MaxHull")),
          number(value.at("WeaponDamage")),
          number(value.at("WeaponIntervalDays")),
          number(value.at("RetreatDelayDays"))};
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
  for (const auto &item : value.at("Weapons"))
    result.weapons.push_back(parse_weapon(item));
  for (const auto &item : value.at("Modules"))
    result.modules.push_back(parse_module(item));
  return result;
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

void check_combat(const FleetCombatState &a, const Json &e,
                  const std::string &l) {
  check(a.profile_id == e.at("ProfileId").get<std::string>(), l + ".ProfileId");
  equal_number(a.shields, number(e.at("Shields")), l + ".Shields");
  equal_number(a.armor, number(e.at("Armor")), l + ".Armor");
  equal_number(a.hull, number(e.at("Hull")), l + ".Hull");
  equal_number(a.weapon_cooldown_remaining_days,
               number(e.at("WeaponCooldownRemainingDays")), l + ".Cooldown");
  check(static_cast<int>(a.order) == e.at("Order").get<int>(), l + ".Order");
  check(a.target_fleet_id == optional_value<int>(e.at("TargetFleetId")),
        l + ".Target");
  check(a.defend_system_id == optional_value<int>(e.at("DefendSystemId")),
        l + ".Defend");
  equal_number(a.retreat_progress_days, number(e.at("RetreatProgressDays")),
               l + ".RetreatProgress");
  check(a.retreat_started == e.at("RetreatStarted").get<bool>(),
        l + ".RetreatStarted");
  check(a.is_disengaged == e.at("IsDisengaged").get<bool>(), l + ".Disengaged");
  check(a.disengaged_system_id ==
            optional_value<int>(e.at("DisengagedSystemId")),
        l + ".DisengagedSystem");
}
void check_weapon(const MassiveWeaponGroup &a, const Json &e,
                  const std::string &l) {
  check(a.id == e.at("Id").get<std::string>() &&
            static_cast<int>(a.kind) == e.at("Kind").get<int>() &&
            a.mounts_per_ship == e.at("MountsPerShip").get<int>(),
        l + ".Identity");
  equal_number(a.damage_per_shot, number(e.at("DamagePerShot")), l + ".Damage");
  equal_number(a.shots_per_second, number(e.at("ShotsPerSecond")),
               l + ".Shots");
  equal_number(a.range, number(e.at("Range")), l + ".Range");
  equal_number(a.accuracy, number(e.at("Accuracy")), l + ".Accuracy");
  equal_number(a.power_per_second, number(e.at("PowerPerSecond")),
               l + ".Power");
  equal_number(a.heat_per_second, number(e.at("HeatPerSecond")), l + ".Heat");
}
void check_module(const MassiveModuleState &a, const Json &e,
                  const std::string &l) {
  check(a.id == e.at("Id").get<std::string>() &&
            static_cast<int>(a.kind) == e.at("Kind").get<int>() &&
            a.installed_count == e.at("InstalledCount").get<int>() &&
            a.enabled == e.at("Enabled").get<bool>() &&
            a.slots == e.at("Slots").get<int>(),
        l + ".Identity");
  equal_number(a.mass_each, number(e.at("MassEach")), l + ".Mass");
  equal_number(a.power_per_second_each, number(e.at("PowerPerSecondEach")),
               l + ".Power");
  equal_number(a.heat_per_second_each, number(e.at("HeatPerSecondEach")),
               l + ".Heat");
  equal_number(a.condition, number(e.at("Condition")), l + ".Condition");
  equal_number(a.effective_range, number(e.at("EffectiveRange")), l + ".Range");
  equal_number(a.field_strength, number(e.at("FieldStrength")),
               l + ".Strength");
  equal_number(a.detection_signature, number(e.at("DetectionSignature")),
               l + ".Signature");
}
void check_loadout(const MassiveCombatLoadout &a, const Json &e,
                   const std::string &l) {
  equal_number(a.mass_per_ship, number(e.at("MassPerShip")), l + ".Mass");
  equal_number(a.acceleration, number(e.at("Acceleration")),
               l + ".Acceleration");
  equal_number(a.maximum_speed, number(e.at("MaximumSpeed")), l + ".Speed");
  equal_number(a.shield_per_ship, number(e.at("ShieldPerShip")), l + ".Shield");
  equal_number(a.armor_per_ship, number(e.at("ArmorPerShip")), l + ".Armor");
  equal_number(a.hull_per_ship, number(e.at("HullPerShip")), l + ".Hull");
  equal_number(a.reactor_output_per_ship, number(e.at("ReactorOutputPerShip")),
               l + ".Reactor");
  equal_number(a.cooling_per_ship, number(e.at("CoolingPerShip")),
               l + ".Cooling");
  equal_number(a.warp_stabilization, number(e.at("WarpStabilization")),
               l + ".Stabilization");
  equal_number(a.warp_spool_seconds, number(e.at("WarpSpoolSeconds")),
               l + ".Spool");
  check(a.module_slot_capacity == e.at("ModuleSlotCapacity").get<int>(),
        l + ".Capacity");
  equal_number(a.maximum_module_mass, number(e.at("MaximumModuleMass")),
               l + ".MaxMass");
  check(a.weapons.size() == e.at("Weapons").size() &&
            a.modules.size() == e.at("Modules").size(),
        l + ".Counts");
  for (size_t i = 0; i < a.weapons.size(); ++i)
    check_weapon(a.weapons[i], e.at("Weapons")[i],
                 l + ".Weapon" + std::to_string(i));
  for (size_t i = 0; i < a.modules.size(); ++i)
    check_module(a.modules[i], e.at("Modules")[i],
                 l + ".Module" + std::to_string(i));
}
void check_vessel(const MassiveVesselState &a, const Json &e,
                  const std::string &l) {
  check(a.id == e.at("Id").get<std::int64_t>() &&
            a.name == e.at("Name").get<std::string>() &&
            a.design_id == e.at("DesignId").get<std::string>(),
        l + ".Identity");
  check(a.is_flagship == e.at("IsFlagship").get<bool>() &&
            a.is_carrier == e.at("IsCarrier").get<bool>() &&
            a.is_interdictor == e.at("IsInterdictor").get<bool>() &&
            a.is_story_ship == e.at("IsStoryShip").get<bool>(),
        l + ".Flags");
  equal_number(a.hull_fraction, number(e.at("HullFraction")), l + ".Hull");
  equal_number(a.engine_fraction, number(e.at("EngineFraction")),
               l + ".Engine");
  equal_number(a.sensor_fraction, number(e.at("SensorFraction")),
               l + ".Sensor");
  equal_number(a.warp_drive_fraction, number(e.at("WarpDriveFraction")),
               l + ".Warp");
  equal_number(a.reactor_fraction, number(e.at("ReactorFraction")),
               l + ".Reactor");
  equal_number(a.interdictor_fraction, number(e.at("InterdictorFraction")),
               l + ".Interdictor");
  check(a.battles_fought == e.at("BattlesFought").get<int>() &&
            a.confirmed_kills == e.at("ConfirmedKills").get<int>() &&
            a.destroyed == e.at("Destroyed").get<bool>() &&
            a.escaped == e.at("Escaped").get<bool>(),
        l + ".History");
}
void check_fleet(const FleetState &a, const Json &e, const std::string &l) {
  check(a.id == e.at("Id").get<int>() &&
            a.civilization_id == e.at("CivilizationId").get<int>() &&
            a.name == e.at("Name").get<std::string>() &&
            static_cast<int>(a.role) == e.at("Role").get<int>(),
        l + ".Identity");
  check(a.design_id == optional_value<std::string>(e.at("DesignId")),
        l + ".Design");
  check_vec2(a.position, e.at("Position"), l + ".Position");
  check(a.current_system_id == optional_value<int>(e.at("CurrentSystemId")) &&
            a.destination_system_id ==
                optional_value<int>(e.at("DestinationSystemId")) &&
            static_cast<int>(a.transit_phase) ==
                e.at("TransitPhase").get<int>() &&
            a.transit_origin_system_id ==
                optional_value<int>(e.at("TransitOriginSystemId")) &&
            a.transit_target_system_id ==
                optional_value<int>(e.at("TransitTargetSystemId")),
        l + ".TransitIds");
  equal_number(a.transit_progress, number(e.at("TransitProgress")),
               l + ".TransitProgress");
  check_vec2(a.local_transit_start, e.at("LocalTransitStart"),
             l + ".LocalStart");
  check_vec2(a.local_transit_position, e.at("LocalTransitPosition"),
             l + ".LocalPosition");
  check_vec2(a.local_transit_target, e.at("LocalTransitTarget"),
             l + ".LocalTarget");
  check(a.planned_route_system_ids ==
            e.at("PlannedRouteSystemIds").get<std::vector<int>>(),
        l + ".Route");
  check(
      a.hold_requested == e.at("HoldRequested").get<bool>() &&
          a.return_to_base_requested ==
              e.at("ReturnToBaseRequested").get<bool>() &&
          a.return_to_base_failure_reason ==
              optional_value<std::string>(e.at("ReturnToBaseFailureReason")) &&
          a.mission_order_revision == e.at("MissionOrderRevision").get<int>(),
      l + ".Orders");
  check(a.destination_planetary_body_id ==
                optional_value<int>(e.at("DestinationPlanetaryBodyId")) &&
            a.prevent_automatic_settlement ==
                e.at("PreventAutomaticSettlement").get<bool>() &&
            a.settlement_body_id ==
                optional_value<int>(e.at("SettlementBodyId")),
        l + ".SettlementIds");
  equal_number(a.settlement_days_completed,
               number(e.at("SettlementDaysCompleted")), l + ".SettlementDays");
  check(a.reconnaissance_system_id ==
            optional_value<int>(e.at("ReconnaissanceSystemId")),
        l + ".ReconSystem");
  equal_number(a.reconnaissance_days_completed,
               number(e.at("ReconnaissanceDaysCompleted")), l + ".ReconDays");
  check(a.freight_target_outpost_id ==
                optional_value<int>(e.at("FreightTargetOutpostId")) &&
            a.freight_home_colony_id ==
                optional_value<int>(e.at("FreightHomeColonyId")),
        l + ".Freight");
  equal_number(a.cargo_material_capacity, number(e.at("CargoMaterialCapacity")),
               l + ".CargoCapacity");
  equal_number(a.cargo_materials, number(e.at("CargoMaterials")), l + ".Cargo");
  equal_number(a.strategic_speed, number(e.at("StrategicSpeed")), l + ".Speed");
  equal_number(a.maximum_leg_range_light_years,
               number(e.at("MaximumLegRangeLightYears")), l + ".Range");
  equal_number(a.fuel_capacity_light_years,
               number(e.at("FuelCapacityLightYears")), l + ".FuelCapacity");
  equal_number(a.fuel_remaining_light_years,
               number(e.at("FuelRemainingLightYears")), l + ".Fuel");
  equal_number(a.sensor_range, number(e.at("SensorRange")), l + ".SensorRange");
  check(a.is_active == e.at("IsActive").get<bool>(), l + ".Active");
  equal_number(a.embarked_population_millions,
               number(e.at("EmbarkedPopulationMillions")), l + ".Population");
  check(a.embarked_population_species_id ==
            optional_value<std::string>(e.at("EmbarkedPopulationSpeciesId")),
        l + ".Species");
  check(a.combat.has_value() != e.at("Combat").is_null(),
        l + ".CombatPresence");
  if (a.combat)
    check_combat(*a.combat, e.at("Combat"), l + ".Combat");
  check(a.tactical_loadout.has_value() != e.at("TacticalLoadout").is_null(),
        l + ".LoadoutPresence");
  if (a.tactical_loadout)
    check_loadout(*a.tactical_loadout, e.at("TacticalLoadout"), l + ".Loadout");
  check(a.tactical_vessel.has_value() != e.at("TacticalVessel").is_null(),
        l + ".VesselPresence");
  if (a.tactical_vessel)
    check_vessel(*a.tactical_vessel, e.at("TacticalVessel"), l + ".Vessel");
}

void check_error(const std::exception_ptr &error, const Json &expected,
                 const std::string &name) {
  check(error != nullptr, name + ": expected error");
  const auto type = expected.at("Type").get<std::string>();
  const auto message = expected.at("Message").get<std::string>();
  try {
    std::rethrow_exception(error);
  } catch (const std::overflow_error &actual) {
    check(type == "OverflowException", name + ": expected error category");
    check(actual.what() == message, name + ": overflow message");
  } catch (const std::invalid_argument &actual) {
    check(type == "InvalidOperationException",
          name + ": expected error category");
    check(actual.what() == message, name + ": validation message");
  } catch (const std::out_of_range &actual) {
    check(type == "KeyNotFoundException", name + ": expected error category");
    check(actual.what() == message, name + ": lookup message");
  } catch (const std::exception &) {
    fail(name + ": unexpected native error category");
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Catalog" || kind == "ProfileGet" || kind == "ProfileFind" ||
            kind == "Initial" || kind == "Ensure" || kind == "FleetRoundtrip" ||
            kind == "LegacyLoadout" || kind == "Interdictor" ||
            kind == "Validation" || kind == "Projection",
        name + ": unknown kind");
  if (kind == "Catalog") {
    const auto &expected = test.at("Catalog");
    const auto actual = combat_profile_catalog();
    check(actual.size() == expected.size(), name + ": count");
    for (size_t i = 0; i < actual.size(); ++i) {
      const auto &a = actual[i];
      check_profile(a, expected[i],
                    name + ".Catalog[" + std::to_string(i) + "]");
      check(find_combat_profile(a.id) == &a && &get_combat_profile(a.id) == &a,
            name + ": lookup");
    }
    check(find_combat_profile("missing") == nullptr, name + ": missing lookup");
    return;
  }
  const auto &args = test.at("Arguments");
  const auto role = args.contains("Role")
                        ? static_cast<FleetRole>(args.at("Role").get<int>())
                        : FleetRole::Scout;
  const auto profile_id =
      args.contains("ProfileId")
          ? optional_value<std::string>(args.at("ProfileId"))
          : std::nullopt;
  if (kind == "ProfileGet" || kind == "ProfileFind")
    check(profile_id.has_value(), name + ": missing profile id argument");
  std::optional<FleetState> fleet;
  if (test.contains("Before"))
    fleet = parse_fleet(test.at("Before"));
  const CombatProfileDefinition *profile = nullptr;
  std::optional<CombatProfileDefinition> custom_profile;
  if (kind == "LegacyLoadout") {
    if (args.contains("Profile")) {
      custom_profile = parse_profile(args.at("Profile"));
      profile = &*custom_profile;
    } else {
      check(profile_id.has_value(), name + ": missing legacy profile argument");
      profile = find_combat_profile(*profile_id);
      check(profile != nullptr, name + ": fixture profile");
    }
  }
  if (kind == "Ensure" || kind == "FleetRoundtrip")
    check(fleet.has_value(), name + ": missing fleet snapshot");
  float range = 0, strength = 0;
  if (kind == "Interdictor") {
    range = static_cast<float>(number(args.at("Range")));
    strength = static_cast<float>(number(args.at("Strength")));
  }
  std::optional<MassiveWeaponGroup> weapon;
  std::optional<MassiveModuleState> module;
  std::optional<MassiveCombatLoadout> loadout;
  std::optional<MassiveVesselState> vessel;
  if (kind == "Validation") {
    const auto target = args.at("Target").get<std::string>();
    const auto &value = args.at("Value");
    if (target == "Weapon")
      weapon = parse_weapon(value);
    else if (target == "Module")
      module = parse_module(value);
    else if (target == "Loadout")
      loadout = parse_loadout(value);
    else if (target == "Vessel")
      vessel = parse_vessel(value);
    else
      fail(name + ": unknown validation target");
  }
  std::vector<FleetState> projection_input;
  if (kind == "Projection")
    for (const auto &value : args.at("Fleets"))
      projection_input.push_back(parse_fleet(value));
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "InvalidOperationException" ||
            expected_error_type == "OverflowException" ||
            expected_error_type == "KeyNotFoundException",
        name + ": unsupported expected error");
  if (fleet)
    check_fleet(*fleet, test.at("Before"), name + ".Before");
  std::optional<FleetCombatState> combat_result;
  std::optional<MassiveCombatLoadout> loadout_result;
  std::optional<MassiveModuleState> module_result;
  std::optional<std::vector<EconomyFleetState>> projection_result;
  const CombatProfileDefinition *profile_result = nullptr;
  bool has_profile_result = false;
  std::exception_ptr operation_error;
  try {
    if (kind == "ProfileGet") {
      profile_result = &get_combat_profile(*profile_id);
      has_profile_result = true;
    } else if (kind == "ProfileFind") {
      profile_result = find_combat_profile(*profile_id);
      has_profile_result = true;
    } else if (kind == "Initial")
      combat_result = create_initial_fleet_combat_state(
          profile_id ? std::optional<std::string_view>(*profile_id)
                     : std::nullopt,
          role);
    else if (kind == "Ensure")
      combat_result = ensure_fleet_combat_state(*fleet);
    else if (kind == "LegacyLoadout")
      loadout_result = massive_loadout_from_legacy(*profile);
    else if (kind == "Interdictor")
      module_result = warp_interdictor(range, strength);
    else if (kind == "Validation") {
      if (weapon)
        weapon->validate();
      else if (module)
        module->validate();
      else if (loadout)
        loadout->validate();
      else
        vessel->validate();
    } else if (kind == "Projection")
      projection_result = economic_fleet_projection(projection_input);
  } catch (...) {
    operation_error = std::current_exception();
  }
  if (!test.at("Error").is_null())
    check_error(operation_error, test.at("Error"), name);
  else {
    check(operation_error == nullptr, name + ": unexpected error");
    const auto &result = test.at("Result");
    if (has_profile_result) {
      check((profile_result == nullptr) == result.is_null(),
            name + ": profile result presence");
      if (profile_result)
        check_profile(*profile_result, result, name + ".Result");
    } else if (combat_result)
      check_combat(*combat_result, result, name + ".Result");
    else if (loadout_result)
      check_loadout(*loadout_result, result, name + ".Result");
    else if (module_result)
      check_module(*module_result, result, name + ".Result");
    else if (kind == "FleetRoundtrip")
      check_fleet(*fleet, result, name + ".Result");
    else if (projection_result) {
      check(projection_result->size() == result.size(),
            name + ": projection count");
      for (size_t i = 0; i < projection_result->size(); ++i) {
        check((*projection_result)[i].civilization_id ==
                      result[i].at("CivilizationId").get<int>() &&
                  static_cast<int>((*projection_result)[i].role) ==
                      result[i].at("Role").get<int>() &&
                  (*projection_result)[i].is_active ==
                      result[i].at("IsActive").get<bool>(),
              name + ": projection field/order");
      }
    }
  }
  if (fleet)
    check_fleet(*fleet, test.at("After"), name + ".After");
  if (kind == "Projection")
    for (size_t i = 0; i < projection_input.size(); ++i)
      check_fleet(projection_input[i], args.at("Fleets")[i], name + ".Input");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected fleet fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open fleet fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-fleet-state-oracle-v2",
          "Unknown fleet fixture format");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "fleet_state_tests: passed " << fixture.at("Cases").size()
              << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fleet_state_tests failed: " << error.what() << '\n';
    return 1;
  }
}
