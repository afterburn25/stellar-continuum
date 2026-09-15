#include <stellar/core/massive_combat_persistence.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
namespace fs = std::filesystem;

namespace {

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
  throw std::runtime_error("Unknown named floating-point value.");
}

float single(const Json &value) { return static_cast<float>(number(value)); }

template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
}

Json floating(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

MassivePoint point(const Json &value) {
  return {single(value.at("X")), single(value.at("Y"))};
}

Json point_json(const MassivePoint &value) {
  return {{"X", floating(value.x)}, {"Y", floating(value.y)},
          {"Vector", Json::object()}, {"IsFinite", value.is_finite()}};
}

std::array<std::uint8_t, 16> guid_bytes(std::string text) {
  std::string digits;
  for (const char ch : text)
    if (ch != '-')
      digits.push_back(ch);
  if (digits.size() != 32)
    throw std::runtime_error("Invalid GUID input.");
  std::array<std::uint8_t, 16> canonical{};
  for (std::size_t index = 0; index < canonical.size(); ++index)
    canonical[index] = static_cast<std::uint8_t>(
        std::stoi(digits.substr(index * 2, 2), nullptr, 16));
  return {canonical[3], canonical[2], canonical[1], canonical[0],
          canonical[5], canonical[4], canonical[7], canonical[6],
          canonical[8], canonical[9], canonical[10], canonical[11],
          canonical[12], canonical[13], canonical[14], canonical[15]};
}

std::string guid_text(const std::array<std::uint8_t, 16> &bytes) {
  const std::array<std::uint8_t, 16> canonical = {
      bytes[3], bytes[2], bytes[1], bytes[0], bytes[5], bytes[4], bytes[7],
      bytes[6], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]};
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < canonical.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10)
      output << '-';
    output << std::setw(2) << static_cast<unsigned>(canonical[index]);
  }
  return output.str();
}

MassiveWeaponGroup weapon(const Json &value) {
  MassiveWeaponGroup result;
  result.id = value.at("Id").get<std::string>();
  result.kind = static_cast<MassiveWeaponKind>(value.at("Kind").get<int>());
  result.mounts_per_ship = value.at("MountsPerShip").get<int>();
  result.damage_per_shot = single(value.at("DamagePerShot"));
  result.shots_per_second = single(value.at("ShotsPerSecond"));
  result.range = single(value.at("Range"));
  result.accuracy = single(value.at("Accuracy"));
  result.power_per_second = single(value.at("PowerPerSecond"));
  result.heat_per_second = single(value.at("HeatPerSecond"));
  return result;
}

Json weapon_json(const MassiveWeaponGroup &value) {
  return {{"Id", value.id}, {"Kind", static_cast<int>(value.kind)},
          {"MountsPerShip", value.mounts_per_ship},
          {"DamagePerShot", floating(value.damage_per_shot)},
          {"ShotsPerSecond", floating(value.shots_per_second)},
          {"Range", floating(value.range)},
          {"Accuracy", floating(value.accuracy)},
          {"PowerPerSecond", floating(value.power_per_second)},
          {"HeatPerSecond", floating(value.heat_per_second)}};
}

MassiveModuleState module(const Json &value) {
  MassiveModuleState result;
  result.id = value.at("Id").get<std::string>();
  result.kind = static_cast<MassiveModuleKind>(value.at("Kind").get<int>());
  result.installed_count = value.at("InstalledCount").get<int>();
  result.mass_each = single(value.at("MassEach"));
  result.power_per_second_each = single(value.at("PowerPerSecondEach"));
  result.heat_per_second_each = single(value.at("HeatPerSecondEach"));
  result.condition = single(value.at("Condition"));
  result.enabled = value.at("Enabled").get<bool>();
  result.effective_range = single(value.at("EffectiveRange"));
  result.field_strength = single(value.at("FieldStrength"));
  result.detection_signature = single(value.at("DetectionSignature"));
  result.slots = value.at("Slots").get<int>();
  return result;
}

Json module_json(const MassiveModuleState &value) {
  return {{"Id", value.id}, {"Kind", static_cast<int>(value.kind)},
          {"InstalledCount", value.installed_count},
          {"MassEach", floating(value.mass_each)},
          {"PowerPerSecondEach", floating(value.power_per_second_each)},
          {"HeatPerSecondEach", floating(value.heat_per_second_each)},
          {"Condition", floating(value.condition)}, {"Enabled", value.enabled},
          {"EffectiveRange", floating(value.effective_range)},
          {"FieldStrength", floating(value.field_strength)},
          {"DetectionSignature", floating(value.detection_signature)},
          {"Slots", value.slots}};
}

MassiveCombatLoadout loadout(const Json &value) {
  MassiveCombatLoadout result;
  result.mass_per_ship = single(value.at("MassPerShip"));
  result.acceleration = single(value.at("Acceleration"));
  result.maximum_speed = single(value.at("MaximumSpeed"));
  result.shield_per_ship = single(value.at("ShieldPerShip"));
  result.armor_per_ship = single(value.at("ArmorPerShip"));
  result.hull_per_ship = single(value.at("HullPerShip"));
  result.reactor_output_per_ship = single(value.at("ReactorOutputPerShip"));
  result.cooling_per_ship = single(value.at("CoolingPerShip"));
  result.warp_stabilization = single(value.at("WarpStabilization"));
  result.warp_spool_seconds = single(value.at("WarpSpoolSeconds"));
  result.module_slot_capacity = value.at("ModuleSlotCapacity").get<int>();
  result.maximum_module_mass = single(value.at("MaximumModuleMass"));
  for (const auto &item : value.at("Weapons"))
    result.weapons.push_back(weapon(item));
  for (const auto &item : value.at("Modules"))
    result.modules.push_back(module(item));
  return result;
}

Json loadout_json(const MassiveCombatLoadout &value) {
  Json weapons = Json::array();
  for (const auto &item : value.weapons)
    weapons.push_back(weapon_json(item));
  Json modules = Json::array();
  for (const auto &item : value.modules)
    modules.push_back(module_json(item));
  return {{"MassPerShip", floating(value.mass_per_ship)},
          {"Acceleration", floating(value.acceleration)},
          {"MaximumSpeed", floating(value.maximum_speed)},
          {"ShieldPerShip", floating(value.shield_per_ship)},
          {"ArmorPerShip", floating(value.armor_per_ship)},
          {"HullPerShip", floating(value.hull_per_ship)},
          {"ReactorOutputPerShip", floating(value.reactor_output_per_ship)},
          {"CoolingPerShip", floating(value.cooling_per_ship)},
          {"WarpStabilization", floating(value.warp_stabilization)},
          {"WarpSpoolSeconds", floating(value.warp_spool_seconds)},
          {"ModuleSlotCapacity", value.module_slot_capacity},
          {"MaximumModuleMass", floating(value.maximum_module_mass)},
          {"Weapons", std::move(weapons)}, {"Modules", std::move(modules)}};
}

MassiveVesselState vessel(const Json &value) {
  MassiveVesselState result;
  result.id = value.at("Id").get<std::int64_t>();
  result.name = value.at("Name").get<std::string>();
  result.design_id = value.at("DesignId").get<std::string>();
  result.is_flagship = value.at("IsFlagship").get<bool>();
  result.is_carrier = value.at("IsCarrier").get<bool>();
  result.is_interdictor = value.at("IsInterdictor").get<bool>();
  result.is_story_ship = value.at("IsStoryShip").get<bool>();
  result.hull_fraction = single(value.at("HullFraction"));
  result.engine_fraction = single(value.at("EngineFraction"));
  result.sensor_fraction = single(value.at("SensorFraction"));
  result.warp_drive_fraction = single(value.at("WarpDriveFraction"));
  result.reactor_fraction = single(value.at("ReactorFraction"));
  result.interdictor_fraction = single(value.at("InterdictorFraction"));
  result.battles_fought = value.at("BattlesFought").get<int>();
  result.confirmed_kills = value.at("ConfirmedKills").get<int>();
  result.destroyed = value.at("Destroyed").get<bool>();
  result.escaped = value.at("Escaped").get<bool>();
  return result;
}

Json vessel_json(const MassiveVesselState &value) {
  return {{"Id", value.id}, {"Name", value.name}, {"DesignId", value.design_id},
          {"IsFlagship", value.is_flagship}, {"IsCarrier", value.is_carrier},
          {"IsInterdictor", value.is_interdictor},
          {"IsStoryShip", value.is_story_ship},
          {"HullFraction", floating(value.hull_fraction)},
          {"EngineFraction", floating(value.engine_fraction)},
          {"SensorFraction", floating(value.sensor_fraction)},
          {"WarpDriveFraction", floating(value.warp_drive_fraction)},
          {"ReactorFraction", floating(value.reactor_fraction)},
          {"InterdictorFraction", floating(value.interdictor_fraction)},
          {"BattlesFought", value.battles_fought},
          {"ConfirmedKills", value.confirmed_kills},
          {"Destroyed", value.destroyed}, {"Escaped", value.escaped}};
}

MassiveCohortState cohort(const Json &value) {
  return {value.at("Id").get<std::int64_t>(),
          value.at("DesignId").get<std::string>(),
          value.at("InitialCount").get<int>(),
          value.at("ActiveCount").get<int>(), single(value.at("Experience"))};
}

Json cohort_json(const MassiveCohortState &value) {
  return {{"Id", value.id}, {"DesignId", value.design_id},
          {"InitialCount", value.initial_count}, {"ActiveCount", value.active_count},
          {"Experience", floating(value.experience)}};
}

MassiveFormationState formation(const Json &value) {
  MassiveFormationState result;
  result.id = value.at("Id").get<std::int64_t>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.fleet_id = value.at("FleetId").get<int>();
  result.task_force_id = value.at("TaskForceId").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = point(value.at("Position"));
  result.velocity = point(value.at("Velocity"));
  result.heading = point(value.at("Heading"));
  result.objective = point(value.at("Objective"));
  result.shape = static_cast<MassiveFormationShape>(value.at("Shape").get<int>());
  result.order = static_cast<MassiveCombatOrderType>(value.at("Order").get<int>());
  result.target_formation_id = optional<std::int64_t>(value.at("TargetFormationId"));
  result.protected_formation_id = optional<std::int64_t>(value.at("ProtectedFormationId"));
  result.interdictor_protection = static_cast<InterdictorProtectionPolicy>(
      value.at("InterdictorProtection").get<int>());
  result.cohesion = single(value.at("Cohesion"));
  result.morale = single(value.at("Morale"));
  result.shield_pool = single(value.at("ShieldPool"));
  result.armor_pool = single(value.at("ArmorPool"));
  result.hull_pool = single(value.at("HullPool"));
  result.hull_loss_threshold_per_ship = single(value.at("HullLossThresholdPerShip"));
  result.heat = single(value.at("Heat"));
  result.power_reserve = single(value.at("PowerReserve"));
  result.warp_spool_progress = single(value.at("WarpSpoolProgress"));
  result.warp_blocked = value.at("WarpBlocked").get<bool>();
  result.escaped = value.at("Escaped").get<bool>();
  result.surrendered = value.at("Surrendered").get<bool>();
  result.initial_ship_count = value.at("InitialShipCount").get<int>();
  result.destroyed_ships = value.at("DestroyedShips").get<int>();
  result.hull_damage_remainder = single(value.at("HullDamageRemainder"));
  result.loadout = loadout(value.at("Loadout"));
  for (const auto &item : value.at("Cohorts"))
    result.cohorts.push_back(cohort(item));
  for (const auto &item : value.at("ImportantVessels"))
    result.important_vessels.push_back(vessel(item));
  return result;
}

Json optional_integer(const std::optional<std::int64_t> &value) {
  return value ? Json(*value) : Json(nullptr);
}

Json formation_json(const MassiveFormationState &value) {
  Json cohorts = Json::array();
  for (const auto &item : value.cohorts)
    cohorts.push_back(cohort_json(item));
  Json vessels = Json::array();
  for (const auto &item : value.important_vessels)
    vessels.push_back(vessel_json(item));
  return {{"Id", value.id}, {"CivilizationId", value.civilization_id},
          {"FleetId", value.fleet_id}, {"TaskForceId", value.task_force_id},
          {"Name", value.name}, {"Position", point_json(value.position)},
          {"Velocity", point_json(value.velocity)}, {"Heading", point_json(value.heading)},
          {"Objective", point_json(value.objective)},
          {"Shape", static_cast<int>(value.shape)},
          {"Order", static_cast<int>(value.order)},
          {"TargetFormationId", optional_integer(value.target_formation_id)},
          {"ProtectedFormationId", optional_integer(value.protected_formation_id)},
          {"InterdictorProtection", static_cast<int>(value.interdictor_protection)},
          {"Cohesion", floating(value.cohesion)}, {"Morale", floating(value.morale)},
          {"ShieldPool", floating(value.shield_pool)},
          {"ArmorPool", floating(value.armor_pool)}, {"HullPool", floating(value.hull_pool)},
          {"HullLossThresholdPerShip", floating(value.hull_loss_threshold_per_ship)},
          {"Heat", floating(value.heat)}, {"PowerReserve", floating(value.power_reserve)},
          {"WarpSpoolProgress", floating(value.warp_spool_progress)},
          {"WarpBlocked", value.warp_blocked}, {"Escaped", value.escaped},
          {"Surrendered", value.surrendered},
          {"InitialShipCount", value.initial_ship_count},
          {"DestroyedShips", value.destroyed_ships},
          {"HullDamageRemainder", floating(value.hull_damage_remainder)},
          {"Loadout", loadout_json(value.loadout)}, {"Cohorts", std::move(cohorts)},
          {"ImportantVessels", std::move(vessels)}};
}

MassiveCombatEvent event(const Json &value) {
  return {value.at("Sequence").get<std::int64_t>(),
          value.at("Tick").get<std::int64_t>(),
          static_cast<MassiveCombatEventType>(value.at("Type").get<int>()),
          value.at("ActorCivilizationId").get<int>(),
          value.at("ActorFormationId").get<std::int64_t>(),
          optional<int>(value.at("TargetCivilizationId")),
          optional<std::int64_t>(value.at("TargetFormationId")),
          value.at("Magnitude").get<int>(), point(value.at("Position")),
          value.at("Message").get<std::string>()};
}

Json event_json(const MassiveCombatEvent &value) {
  return {{"Sequence", value.sequence}, {"Tick", value.tick},
          {"Type", static_cast<int>(value.type)},
          {"ActorCivilizationId", value.actor_civilization_id},
          {"ActorFormationId", value.actor_formation_id},
          {"TargetCivilizationId", value.target_civilization_id ? Json(*value.target_civilization_id) : Json(nullptr)},
          {"TargetFormationId", optional_integer(value.target_formation_id)},
          {"Magnitude", value.magnitude}, {"Position", point_json(value.position)},
          {"Message", value.message}};
}

MassiveMissileSalvoState salvo(const Json &value) {
  MassiveMissileSalvoState result;
  result.id = value.at("Id").get<std::int64_t>();
  result.source_formation_id = value.at("SourceFormationId").get<std::int64_t>();
  result.target_formation_id = value.at("TargetFormationId").get<std::int64_t>();
  result.missile_count = value.at("MissileCount").get<int>();
  result.damage = single(value.at("Damage"));
  result.remaining_seconds = single(value.at("RemainingSeconds"));
  if (!value.at("LaunchPosition").is_null())
    result.launch_position = point(value.at("LaunchPosition"));
  result.initial_flight_seconds = single(value.at("InitialFlightSeconds"));
  return result;
}

Json salvo_json(const MassiveMissileSalvoState &value) {
  return {{"Id", value.id}, {"SourceFormationId", value.source_formation_id},
          {"TargetFormationId", value.target_formation_id},
          {"MissileCount", value.missile_count}, {"Damage", floating(value.damage)},
          {"RemainingSeconds", floating(value.remaining_seconds)},
          {"LaunchPosition", value.launch_position ? point_json(*value.launch_position) : Json(nullptr)},
          {"InitialFlightSeconds", floating(value.initial_flight_seconds)}};
}

MassiveCombatBattleState battle(const Json &value) {
  MassiveCombatBattleState result;
  result.battle_id = guid_bytes(value.at("BattleId").get<std::string>());
  result.seed = value.at("Seed").get<std::uint64_t>();
  result.tick = value.at("Tick").get<std::int64_t>();
  result.simulated_seconds = number(value.at("SimulatedSeconds"));
  result.pending_seconds = number(value.at("PendingSeconds"));
  result.next_event_sequence = value.at("NextEventSequence").get<std::int64_t>();
  result.next_salvo_id = value.at("NextSalvoId").get<std::int64_t>();
  for (const auto &item : value.at("Formations"))
    result.formations.push_back(formation(item));
  for (const auto &item : value.at("Events"))
    result.events.push_back(event(item));
  for (const auto &item : value.at("ActiveSalvos"))
    result.active_salvos.push_back(salvo(item));
  return result;
}

Json battle_json(const MassiveCombatBattleState &value) {
  Json formations = Json::array();
  for (const auto &item : value.formations)
    formations.push_back(formation_json(item));
  Json events = Json::array();
  for (const auto &item : value.events)
    events.push_back(event_json(item));
  Json salvos = Json::array();
  for (const auto &item : value.active_salvos)
    salvos.push_back(salvo_json(item));
  return {{"BattleId", guid_text(value.battle_id)}, {"Seed", value.seed},
          {"Tick", value.tick}, {"SimulatedSeconds", floating(value.simulated_seconds)},
          {"PendingSeconds", floating(value.pending_seconds)},
          {"NextEventSequence", value.next_event_sequence},
          {"NextSalvoId", value.next_salvo_id}, {"Formations", std::move(formations)},
          {"Events", std::move(events)}, {"ActiveSalvos", std::move(salvos)}};
}

CampaignMassiveEncounter encounter(const Json &value) {
  CampaignMassiveEncounter result;
  result.system_id = value.at("SystemId").get<int>();
  result.started_day = number(value.at("StartedDay"));
  result.battle = battle(value.at("Battle"));
  for (const auto &item : value.at("Vessels"))
    result.vessels.push_back({item.at("FleetId").get<int>(),
                              item.at("FormationId").get<std::int64_t>()});
  for (const auto &item : value.at("EngagedFormationPairs"))
    result.engaged_formation_pairs.push_back(
        {item.at("FirstFormationId").get<std::int64_t>(),
         item.at("SecondFormationId").get<std::int64_t>()});
  result.last_observed_event_sequence = value.at("LastObservedEventSequence").get<std::int64_t>();
  result.reconciled = value.at("Reconciled").get<bool>();
  return result;
}

Json encounter_json(const CampaignMassiveEncounter &value) {
  Json vessels = Json::array();
  for (const auto &item : value.vessels)
    vessels.push_back({{"FleetId", item.fleet_id}, {"FormationId", item.formation_id}});
  Json engagements = Json::array();
  for (const auto &item : value.engaged_formation_pairs)
    engagements.push_back({{"FirstFormationId", item.first_formation_id},
                           {"SecondFormationId", item.second_formation_id}});
  return {{"SystemId", value.system_id}, {"StartedDay", floating(value.started_day)},
          {"Battle", battle_json(value.battle)}, {"Vessels", std::move(vessels)},
          {"EngagedFormationPairs", std::move(engagements)},
          {"LastObservedEventSequence", value.last_observed_event_sequence},
          {"Reconciled", value.reconciled}};
}

struct Prepared {
  std::vector<StellarSystem> systems;
  std::vector<FleetState> fleets;
  CampaignMassiveEncounter encounter;
};

Prepared prepare(const Json &input) {
  Prepared result;
  for (const auto &item : input.at("Systems")) {
    StellarSystem system;
    system.id = item.at("Id").get<int>();
    result.systems.push_back(std::move(system));
  }
  for (const auto &item : input.at("Fleets")) {
    FleetState fleet;
    fleet.id = item.at("Id").get<int>();
    fleet.civilization_id = item.at("CivilizationId").get<int>();
    if (!item.at("DesignId").is_null())
      fleet.design_id = item.at("DesignId").get<std::string>();
    if (!item.at("CombatProfileId").is_null()) {
      FleetCombatState combat;
      combat.profile_id = item.at("CombatProfileId").get<std::string>();
      fleet.combat = std::move(combat);
    }
    result.fleets.push_back(std::move(fleet));
  }
  result.encounter = encounter(input.at("Encounter"));
  return result;
}

Json input_json(const Prepared &value) {
  Json systems = Json::array();
  for (const auto &system : value.systems)
    systems.push_back({{"Id", system.id}});
  Json fleets = Json::array();
  for (const auto &fleet : value.fleets)
    fleets.push_back({{"Id", fleet.id}, {"CivilizationId", fleet.civilization_id},
                      {"DesignId", fleet.design_id ? Json(*fleet.design_id) : Json(nullptr)},
                      {"CombatProfileId", fleet.combat ? Json(fleet.combat->profile_id) : Json(nullptr)}});
  return {{"Systems", std::move(systems)}, {"Fleets", std::move(fleets)},
          {"Encounter", encounter_json(value.encounter)}};
}

bool equal_json(const Json &actual, const Json &expected, std::string &path) {
  const auto float_field = [&] {
    const auto slash = path.find_last_of('/');
    const auto field = path.substr(slash == std::string::npos ? 0 : slash + 1);
    constexpr std::array fields = {
        "X", "Y", "DamagePerShot", "ShotsPerSecond", "Range",
        "Accuracy", "PowerPerSecond", "HeatPerSecond", "MassEach",
        "PowerPerSecondEach", "HeatPerSecondEach", "Condition",
        "EffectiveRange", "FieldStrength", "DetectionSignature",
        "MassPerShip", "Acceleration", "MaximumSpeed", "ShieldPerShip",
        "ArmorPerShip", "HullPerShip", "ReactorOutputPerShip",
        "CoolingPerShip", "WarpStabilization", "WarpSpoolSeconds",
        "MaximumModuleMass", "HullFraction", "EngineFraction",
        "SensorFraction", "WarpDriveFraction", "ReactorFraction",
        "InterdictorFraction", "Experience", "Cohesion", "Morale",
        "ShieldPool", "ArmorPool", "HullPool",
        "HullLossThresholdPerShip", "Heat", "PowerReserve",
        "WarpSpoolProgress", "HullDamageRemainder", "Damage",
        "RemainingSeconds", "InitialFlightSeconds", "StartedDay",
        "SimulatedSeconds", "PendingSeconds"};
    return std::ranges::find(fields, field) != fields.end();
  }();
  if (actual.type() != expected.type()) {
    if (!actual.is_number() || !expected.is_number())
      return false;
    const bool ai = actual.is_number_integer() || actual.is_number_unsigned();
    const bool ei = expected.is_number_integer() || expected.is_number_unsigned();
    if (ai && ei) {
      if (actual.is_number_unsigned() && expected.is_number_unsigned())
        return actual.get<std::uint64_t>() == expected.get<std::uint64_t>();
      if (actual.is_number_integer() && expected.is_number_integer())
        return actual.get<std::int64_t>() == expected.get<std::int64_t>();
      const Json &signed_value = actual.is_number_integer() ? actual : expected;
      const Json &unsigned_value = actual.is_number_unsigned() ? actual : expected;
      const auto signed_number = signed_value.get<std::int64_t>();
      return signed_number >= 0 && static_cast<std::uint64_t>(signed_number) ==
                                       unsigned_value.get<std::uint64_t>();
    }
    const auto left = actual.get<double>();
    const auto right = expected.get<double>();
    return float_field &&
           (left == right || std::abs(left - right) <= 1e-6);
  }
  if (actual.is_primitive()) {
    if (actual.is_number_float()) {
      const auto left = actual.get<double>();
      const auto right = expected.get<double>();
      return left == right ||
             (float_field && std::abs(left - right) <= 1e-6);
    }
    return actual == expected;
  }
  if (actual.size() != expected.size())
    return false;
  if (actual.is_array()) {
    for (std::size_t index = 0; index < actual.size(); ++index) {
      const auto prior = path;
      path += "/" + std::to_string(index);
      if (!equal_json(actual[index], expected[index], path))
        return false;
      path = prior;
    }
    return true;
  }
  for (auto item = actual.begin(); item != actual.end(); ++item) {
    const auto prior = path;
    path += "/" + item.key();
    if (!expected.contains(item.key()) ||
        !equal_json(item.value(), expected.at(item.key()), path))
      return false;
    path = prior;
  }
  return true;
}

Json error_json(std::exception_ptr error) {
  if (!error)
    return nullptr;
  try {
    std::rethrow_exception(error);
  } catch (const MassiveCombatStateError &value) {
    return {{"Type", "InvalidOperationException"}, {"Message", value.what()}};
  } catch (const CampaignMassiveEncounterDataError &value) {
    return {{"Type", "InvalidDataException"}, {"Message", value.what()}};
  } catch (const CampaignMassiveEncounterArgumentError &value) {
    return {{"Type", "ArgumentException"}, {"Message", value.what()}};
  } catch (const std::overflow_error &value) {
    return {{"Type", "OverflowException"}, {"Message", value.what()}};
  } catch (const std::exception &value) {
    return {{"Type", "UnexpectedNativeException"}, {"Message", value.what()}};
  }
}

std::string read_bytes(const fs::path &path, std::string_view description) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("Cannot open " + std::string(description) + ": " +
                             path.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::string sha256(std::string_view value) {
  const auto digest = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto byte : digest)
    result << std::setw(2) << static_cast<unsigned>(byte);
  auto text = result.str();
  std::ranges::transform(text, text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return text;
}

int replay(const fs::path &fixture_path, const fs::path &source_root) {
  const auto fixture_bytes = read_bytes(fixture_path, "retained fixture");
  if (sha256(fixture_bytes) !=
      "64140AC12DD0642C64102AE6C7BC0484B878B69B05603C1EB2BFB1095C36DEB7")
    throw std::runtime_error("Retained fixture fingerprint changed.");
  const auto fixture = Json::parse(fixture_bytes);
  if (fixture.at("SchemaVersion") != 1 || fixture.at("SourceFiles").size() != 5)
    throw std::runtime_error("Fixture schema or source inventory is invalid.");
  constexpr std::array expected_source_paths = {
      "Simulation/Combat/Massive/MassiveCombatState.cs",
      "Simulation/Combat/Massive/MassiveCombatContracts.cs",
      "Simulation/Combat/Massive/MassiveCombatEquipment.cs",
      "Simulation/Combat/CampaignMassiveCombat.cs",
      "Persistence/CampaignSaveService.cs"};
  for (std::size_t index = 0; index < expected_source_paths.size(); ++index)
    if (fixture.at("SourceFiles")[index].at("Path") !=
        expected_source_paths[index])
      throw std::runtime_error("Fixture source inventory order changed.");
  if (fixture.at("RowCount") != fixture.at("Rows").size())
    throw std::runtime_error("Fixture row count is inconsistent.");
  std::vector<std::pair<fs::path, std::string>> source_fingerprints;
  for (const auto &source : fixture.at("SourceFiles")) {
    const auto path = source_root / source.at("Path").get<std::string>();
    const auto expected = source.at("Sha256").get<std::string>();
    if (sha256(read_bytes(path, "retained source")) != expected)
      throw std::runtime_error("Retained source fingerprint changed: " +
                               path.string());
    source_fingerprints.emplace_back(path, expected);
  }
  std::size_t native_count = 0;
  std::size_t source_only_count = 0;
  for (const auto &row : fixture.at("Rows")) {
    if (row.at("SourceOnly").get<bool>()) {
      if (row.at("Name") != "null-enumerable-element" ||
          row.at("Operation") != "Validate" ||
          row.at("ErrorType") != "NullReferenceException" ||
          row.at("SourceOnlyReason") !=
              "Source permits null reference elements; native public spans contain valid typed values only." ||
          row.at("Result") != nullptr)
        throw std::runtime_error("Invalid source-only row metadata.");
      ++source_only_count;
      continue;
    }
    const auto operation = row.at("Operation").get<std::string>();
    if (operation != "Validate" && operation != "Clone" &&
        operation != "Capture")
      throw std::runtime_error("Unknown fixture operation.");
    auto prepared = prepare(row.at("Input"));
    std::string mismatch;
    if (!equal_json(input_json(prepared), row.at("Input"), mismatch))
      throw std::runtime_error(row.at("Name").get<std::string>() +
                               " decoded input mismatch at " + mismatch);
    const auto input_before = input_json(prepared);
    std::exception_ptr error;
    Json result;
    if (operation == "Validate") {
      try {
        validate_campaign_massive_encounter(
            prepared.encounter, {prepared.systems, prepared.fleets});
      } catch (...) {
        error = std::current_exception();
      }
      if (!error) {
        Json active = Json::array();
        Json surviving = Json::array();
        for (const auto &formation : prepared.encounter.battle.formations) {
          active.push_back(formation.active_ship_count());
          surviving.push_back(formation.surviving_ship_count());
        }
        result = {{"IsComplete", prepared.encounter.battle.is_complete()},
                  {"ActiveShipCounts", std::move(active)},
                  {"SurvivingShipCounts", std::move(surviving)}};
      }
    } else if (operation == "Clone") {
      CampaignMassiveEncounter cloned;
      try {
        cloned = clone_campaign_massive_encounter(prepared.encounter);
      } catch (...) {
        error = std::current_exception();
      }
      if (!error) {
        const auto clone_before = encounter_json(cloned);
        prepared.encounter.battle.formations[0].name = "mutated-live";
        prepared.encounter.battle.formations[0].loadout.weapons[0].damage_per_shot = 999;
        prepared.encounter.battle.formations[0].important_vessels[0].name = "mutated-vessel";
        prepared.encounter.battle.events.clear();
        prepared.encounter.battle.active_salvos.clear();
        prepared.encounter.vessels.clear();
        prepared.encounter.engaged_formation_pairs.clear();
        const auto clone_after_live = encounter_json(cloned);
        cloned.battle.formations[1].name = "mutated-clone";
        const auto live_after_clone = encounter_json(prepared.encounter);
        std::string ignored;
        result = {{"CloneBeforeMutation", clone_before},
                  {"CloneAfterLiveMutation", clone_after_live},
                  {"CloneStable", equal_json(clone_before, clone_after_live, ignored)},
                  {"LiveUnaffectedByClone", prepared.encounter.battle.formations[1].name != "mutated-clone"},
                  {"Provenance", "actual private CampaignSaveService.CloneEncounter used by CaptureDetachedEnvelope"}};
      }
    } else {
      CampaignMassiveEncounter captured;
      try {
        captured = clone_campaign_massive_encounter(prepared.encounter);
      } catch (...) {
        error = std::current_exception();
      }
      if (!error) {
        const auto captured_before = encounter_json(captured);
        prepared.encounter.battle.formations[0].name =
            "mutated-after-public-save";
        prepared.encounter.battle.formations[0].loadout.weapons.clear();
        prepared.encounter.vessels.clear();
        const auto captured_after = encounter_json(captured);
        std::string ignored;
        result = {
            {"Captured", captured_before},
            {"CapturedAfterLiveMutation", captured_after},
            {"Stable",
             equal_json(captured_before, captured_after, ignored)},
            {"Provenance",
             "actual public CampaignSaveService.Save detached galaxy capture"}};
      }
    }
    const auto actual_error = error_json(error);
    const Json expected_error = row.at("ErrorType").is_null()
                                    ? Json(nullptr)
                                    : Json{{"Type", row.at("ErrorType")},
                                           {"Message", row.at("ErrorMessage")}};
    mismatch.clear();
    if (!equal_json(actual_error, expected_error, mismatch))
      throw std::runtime_error(row.at("Name").get<std::string>() +
                               " error mismatch at " + mismatch + ": " + actual_error.dump());
    mismatch.clear();
    if (!equal_json(encounter_json(prepared.encounter), row.at("After"), mismatch))
      throw std::runtime_error(row.at("Name").get<std::string>() +
                               " state mismatch at " + mismatch);
    if (!error) {
      mismatch.clear();
      if (!equal_json(result, row.at("Result"), mismatch))
        throw std::runtime_error(row.at("Name").get<std::string>() +
                                 " result mismatch at " + mismatch);
    }
    // World and caller-owned fleet/system inputs remain unchanged. Encounter mutation is
    // intentionally compared above because Validate materializes legacy counts.
    auto after_input = input_json(prepared);
    after_input["Encounter"] = input_before.at("Encounter");
    mismatch.clear();
    if (!equal_json(after_input, input_before, mismatch))
      throw std::runtime_error(row.at("Name").get<std::string>() +
                               " borrowed world input changed at " + mismatch);
    ++native_count;
  }
  if (native_count != 39 || source_only_count != 1 ||
      fixture.at("SourceOnlyCount") != 1)
    throw std::runtime_error("Expected exactly 39 native and one source-only row.");
  for (const auto &[path, expected] : source_fingerprints)
    if (sha256(read_bytes(path, "retained source")) != expected)
      throw std::runtime_error("Retained source changed during replay: " +
                               path.string());
  std::cout << "Massive encounter native replay: " << native_count << "/"
            << native_count << " native rows, " << source_only_count
            << " explicit source-only boundary passed.\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: massive_combat_persistence_tests <fixture.json> <Game source root>");
    return replay(fs::absolute(argv[1]).lexically_normal(),
                  fs::absolute(argv[2]).lexically_normal());
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n'
              << "Working directory: " << fs::current_path().string() << '\n'
              << "Fixture path: " << (argc > 1 ? fs::absolute(argv[1]).string() : "<missing>") << '\n';
    if (argc > 2)
      std::cerr << "Source root: " << fs::absolute(argv[2]).string() << '\n';
    return 1;
  }
}
