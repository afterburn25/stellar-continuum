#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stellar/core/own_combat_fleet_status.hpp>

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
  fail("Unknown named number.");
}
Json encode_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
template <class T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
Json encode_vec2(Vec2 value) {
  return {{"X", encode_number(value.x)}, {"Y", encode_number(value.y)}};
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (actual.is_number_integer() && expected.is_number_integer()) {
    check(actual == expected, field + ": integer value");
    return;
  }
  if (actual.is_number() && expected.is_number()) {
    const double left = actual.get<double>();
    const double right = expected.get<double>();
    check(std::abs(left - right) <=
              1e-7 * std::max({1.0, std::abs(left), std::abs(right)}),
          field + ": numeric value");
    return;
  }
  check(actual.type() == expected.type(), field + ": JSON type");
  if (actual.is_array()) {
    check(actual.size() == expected.size(), field + ": array size");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
  } else if (actual.is_object()) {
    check(actual.size() == expected.size(), field + ": object size");
    for (const auto &[name, value] : expected.items()) {
      check(actual.contains(name), field + ": missing " + name);
      equal_json(actual.at(name), value, field + "." + name);
    }
  } else {
    check(actual == expected, field + ": value");
  }
}

FleetCombatState parse_combat(const Json &value) {
  return {value.at("ProfileId"),
          number(value.at("Shields")),
          number(value.at("Armor")),
          number(value.at("Hull")),
          number(value.at("WeaponCooldownRemainingDays")),
          static_cast<MilitaryOrderType>(value.at("Order").get<int>()),
          optional_value<int>(value.at("TargetFleetId")),
          optional_value<int>(value.at("DefendSystemId")),
          number(value.at("RetreatProgressDays")),
          value.at("RetreatStarted"),
          value.at("IsDisengaged"),
          optional_value<int>(value.at("DisengagedSystemId"))};
}
Json encode_combat(const FleetCombatState &state) {
  return {{"ProfileId", state.profile_id},
          {"Shields", encode_number(state.shields)},
          {"Armor", encode_number(state.armor)},
          {"Hull", encode_number(state.hull)},
          {"WeaponCooldownRemainingDays",
           encode_number(state.weapon_cooldown_remaining_days)},
          {"Order", static_cast<int>(state.order)},
          {"TargetFleetId", state.target_fleet_id},
          {"DefendSystemId", state.defend_system_id},
          {"RetreatProgressDays", encode_number(state.retreat_progress_days)},
          {"RetreatStarted", state.retreat_started},
          {"IsDisengaged", state.is_disengaged},
          {"DisengagedSystemId", state.disengaged_system_id}};
}
MassiveVesselState parse_vessel(const Json &value) {
  MassiveVesselState result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.design_id = value.at("DesignId");
  result.is_flagship = value.at("IsFlagship");
  result.is_carrier = value.at("IsCarrier");
  result.is_interdictor = value.at("IsInterdictor");
  result.is_story_ship = value.at("IsStoryShip");
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
  result.battles_fought = value.at("BattlesFought");
  result.confirmed_kills = value.at("ConfirmedKills");
  result.destroyed = value.at("Destroyed");
  result.escaped = value.at("Escaped");
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
          {"HullFraction", encode_number(value.hull_fraction)},
          {"EngineFraction", encode_number(value.engine_fraction)},
          {"SensorFraction", encode_number(value.sensor_fraction)},
          {"WarpDriveFraction", encode_number(value.warp_drive_fraction)},
          {"ReactorFraction", encode_number(value.reactor_fraction)},
          {"InterdictorFraction", encode_number(value.interdictor_fraction)},
          {"BattlesFought", value.battles_fought},
          {"ConfirmedKills", value.confirmed_kills},
          {"Destroyed", value.destroyed},
          {"Escaped", value.escaped}};
}
FleetState parse_fleet(const Json &value) {
  FleetState fleet;
  fleet.id = value.at("Id");
  fleet.civilization_id = value.at("CivilizationId");
  fleet.name = value.at("Name");
  fleet.role = static_cast<FleetRole>(value.at("Role").get<int>());
  fleet.design_id = optional_value<std::string>(value.at("DesignId"));
  fleet.position = parse_vec2(value.at("Position"));
  fleet.current_system_id = optional_value<int>(value.at("CurrentSystemId"));
  fleet.destination_system_id =
      optional_value<int>(value.at("DestinationSystemId"));
  fleet.transit_phase =
      static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  fleet.transit_origin_system_id =
      optional_value<int>(value.at("TransitOriginSystemId"));
  fleet.transit_target_system_id =
      optional_value<int>(value.at("TransitTargetSystemId"));
  fleet.transit_progress = number(value.at("TransitProgress"));
  fleet.local_transit_start = parse_vec2(value.at("LocalTransitStart"));
  fleet.local_transit_position = parse_vec2(value.at("LocalTransitPosition"));
  fleet.local_transit_target = parse_vec2(value.at("LocalTransitTarget"));
  fleet.planned_route_system_ids =
      value.at("PlannedRouteSystemIds").get<std::vector<int>>();
  fleet.hold_requested = value.at("HoldRequested");
  fleet.return_to_base_requested = value.at("ReturnToBaseRequested");
  fleet.return_to_base_failure_reason =
      optional_value<std::string>(value.at("ReturnToBaseFailureReason"));
  fleet.mission_order_revision = value.at("MissionOrderRevision");
  fleet.destination_planetary_body_id =
      optional_value<int>(value.at("DestinationPlanetaryBodyId"));
  fleet.prevent_automatic_settlement = value.at("PreventAutomaticSettlement");
  fleet.settlement_body_id = optional_value<int>(value.at("SettlementBodyId"));
  fleet.settlement_days_completed = number(value.at("SettlementDaysCompleted"));
  fleet.reconnaissance_system_id =
      optional_value<int>(value.at("ReconnaissanceSystemId"));
  fleet.reconnaissance_days_completed =
      number(value.at("ReconnaissanceDaysCompleted"));
  fleet.freight_target_outpost_id =
      optional_value<int>(value.at("FreightTargetOutpostId"));
  fleet.freight_home_colony_id =
      optional_value<int>(value.at("FreightHomeColonyId"));
  fleet.cargo_material_capacity = number(value.at("CargoMaterialCapacity"));
  fleet.cargo_materials = number(value.at("CargoMaterials"));
  fleet.strategic_speed = number(value.at("StrategicSpeed"));
  fleet.maximum_leg_range_light_years =
      number(value.at("MaximumLegRangeLightYears"));
  fleet.fuel_capacity_light_years = number(value.at("FuelCapacityLightYears"));
  fleet.fuel_remaining_light_years =
      number(value.at("FuelRemainingLightYears"));
  fleet.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  fleet.is_active = value.at("IsActive");
  fleet.embarked_population_millions =
      number(value.at("EmbarkedPopulationMillions"));
  fleet.embarked_population_species_id =
      optional_value<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  if (!value.at("Combat").is_null())
    fleet.combat = parse_combat(value.at("Combat"));
  check(value.at("TacticalLoadout").is_null(),
        "TacticalLoadout is outside the native import boundary.");
  if (!value.at("TacticalVessel").is_null())
    fleet.tactical_vessel = parse_vessel(value.at("TacticalVessel"));
  return fleet;
}
Json encode_fleet(const FleetState &fleet) {
  return {
      {"Id", fleet.id},
      {"CivilizationId", fleet.civilization_id},
      {"Name", fleet.name},
      {"Role", static_cast<int>(fleet.role)},
      {"DesignId", fleet.design_id},
      {"Position", encode_vec2(fleet.position)},
      {"CurrentSystemId", fleet.current_system_id},
      {"DestinationSystemId", fleet.destination_system_id},
      {"TransitPhase", static_cast<int>(fleet.transit_phase)},
      {"TransitOriginSystemId", fleet.transit_origin_system_id},
      {"TransitTargetSystemId", fleet.transit_target_system_id},
      {"TransitProgress", encode_number(fleet.transit_progress)},
      {"LocalTransitStart", encode_vec2(fleet.local_transit_start)},
      {"LocalTransitPosition", encode_vec2(fleet.local_transit_position)},
      {"LocalTransitTarget", encode_vec2(fleet.local_transit_target)},
      {"PlannedRouteSystemIds", fleet.planned_route_system_ids},
      {"HoldRequested", fleet.hold_requested},
      {"ReturnToBaseRequested", fleet.return_to_base_requested},
      {"ReturnToBaseFailureReason", fleet.return_to_base_failure_reason},
      {"MissionOrderRevision", fleet.mission_order_revision},
      {"DestinationPlanetaryBodyId", fleet.destination_planetary_body_id},
      {"PreventAutomaticSettlement", fleet.prevent_automatic_settlement},
      {"SettlementBodyId", fleet.settlement_body_id},
      {"SettlementDaysCompleted",
       encode_number(fleet.settlement_days_completed)},
      {"ReconnaissanceSystemId", fleet.reconnaissance_system_id},
      {"ReconnaissanceDaysCompleted",
       encode_number(fleet.reconnaissance_days_completed)},
      {"FreightTargetOutpostId", fleet.freight_target_outpost_id},
      {"FreightHomeColonyId", fleet.freight_home_colony_id},
      {"CargoMaterialCapacity", encode_number(fleet.cargo_material_capacity)},
      {"CargoMaterials", encode_number(fleet.cargo_materials)},
      {"StrategicSpeed", encode_number(fleet.strategic_speed)},
      {"MaximumLegRangeLightYears",
       encode_number(fleet.maximum_leg_range_light_years)},
      {"FuelCapacityLightYears",
       encode_number(fleet.fuel_capacity_light_years)},
      {"FuelRemainingLightYears",
       encode_number(fleet.fuel_remaining_light_years)},
      {"SensorRange", encode_number(fleet.sensor_range)},
      {"IsActive", fleet.is_active},
      {"EmbarkedPopulationMillions",
       encode_number(fleet.embarked_population_millions)},
      {"EmbarkedPopulationSpeciesId", fleet.embarked_population_species_id},
      {"Combat", fleet.combat ? encode_combat(*fleet.combat) : Json(nullptr)},
      {"TacticalLoadout", nullptr},
      {"TacticalVessel", fleet.tactical_vessel
                             ? encode_vessel(*fleet.tactical_vessel)
                             : Json(nullptr)}};
}

Civilization parse_civilization(const Json &value) {
  Civilization civilization;
  civilization.id = value.at("Id");
  civilization.name = value.at("Name");
  civilization.home_system_id = value.at("HomeSystemId");
  civilization.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  civilization.traits = {number(traits.at("Aggression")),
                         number(traits.at("Territoriality")),
                         number(traits.at("Greed")),
                         number(traits.at("ScientificCuriosity")),
                         number(traits.at("RiskTolerance")),
                         number(traits.at("SurvivalPriority")),
                         traits.at("HonorBound")};
  civilization.is_player = value.at("IsPlayer");
  civilization.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  civilization.is_seeded_ancient = value.at("IsSeededAncient");
  civilization.expansion_allowed = value.at("ExpansionAllowed");
  civilization.neutral_unless_provoked = value.at("NeutralUnlessProvoked");
  civilization.species_id = value.at("SpeciesId");
  for (const auto &[office, character] :
       value.at("Leadership").at("Offices").items())
    civilization.leadership.push_back(
        {office,
         {character.at("Id"), character.at("DisplayName"),
          optional_value<std::string>(character.at("VoiceProfileId")),
          optional_value<std::string>(character.at("Portrait"))}});
  return civilization;
}

Json encode_civilization(const Civilization &civilization) {
  Json offices = Json::object();
  for (const auto &entry : civilization.leadership) {
    offices[entry.office] = {
        {"Id", entry.character.id},
        {"DisplayName", entry.character.display_name},
        {"VoiceProfileId", entry.character.voice_profile_id},
        {"Portrait", entry.character.portrait},
    };
  }
  return {
      {"Id", civilization.id},
      {"Name", civilization.name},
      {"HomeSystemId", civilization.home_system_id},
      {"Archetype", static_cast<int>(civilization.archetype)},
      {"Traits",
       {{"Aggression", encode_number(civilization.traits.aggression)},
        {"Territoriality", encode_number(civilization.traits.territoriality)},
        {"Greed", encode_number(civilization.traits.greed)},
        {"ScientificCuriosity",
         encode_number(civilization.traits.scientific_curiosity)},
        {"RiskTolerance", encode_number(civilization.traits.risk_tolerance)},
        {"SurvivalPriority",
         encode_number(civilization.traits.survival_priority)},
        {"HonorBound", civilization.traits.honor_bound}}},
      {"IsPlayer", civilization.is_player},
      {"DevelopmentStage", static_cast<int>(civilization.development_stage)},
      {"IsSeededAncient", civilization.is_seeded_ancient},
      {"ExpansionAllowed", civilization.expansion_allowed},
      {"NeutralUnlessProvoked", civilization.neutral_unless_provoked},
      {"SpeciesId", civilization.species_id},
      {"Leadership", {{"Offices", offices}}},
  };
}

struct World {
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
};
World parse_world(const Json &value) {
  World world;
  for (const auto &civilization : value.at("Civilizations"))
    world.civilizations.push_back(parse_civilization(civilization));
  for (const auto &fleet : value.at("Fleets"))
    world.fleets.push_back(parse_fleet(fleet));
  return world;
}
Json encode_world(const World &world) {
  Json civilizations = Json::array();
  for (const auto &civilization : world.civilizations)
    civilizations.push_back(encode_civilization(civilization));
  Json fleets = Json::array();
  for (const auto &fleet : world.fleets)
    fleets.push_back(encode_fleet(fleet));
  return {{"Civilizations", civilizations}, {"Fleets", fleets}};
}
Json encode_summary(const CombatReadinessSummary &summary) {
  return {
      {"CivilizationId", summary.civilization_id},
      {"ActiveVessels", summary.active_vessels},
      {"ActiveArmedVessels", summary.active_armed_vessels},
      {"CombatEffectiveArmedVessels", summary.combat_effective_armed_vessels},
      {"DamagedVessels", summary.damaged_vessels},
      {"HullDamagedVessels", summary.hull_damaged_vessels},
      {"RetreatingVessels", summary.retreating_vessels},
      {"DisengagedVessels", summary.disengaged_vessels},
      {"CurrentDurability", encode_number(summary.current_durability)},
      {"MaximumDurability", encode_number(summary.maximum_durability)},
      {"CurrentArmedStrength", encode_number(summary.current_armed_strength)},
      {"MaximumArmedStrength", encode_number(summary.maximum_armed_strength)},
      {"CombatEffectiveArmedStrength",
       encode_number(summary.combat_effective_armed_strength)},
      {"TotalRepairDeficit", encode_number(summary.total_repair_deficit)},
      {"DurabilityRatio", encode_number(summary.durability_ratio())},
      {"ArmedStrengthRatio", encode_number(summary.armed_strength_ratio())}};
}
Json encode_status(const OwnCombatFleetStatus &status) {
  return {
      {"FleetId", status.fleet_id},
      {"FleetName", status.fleet_name},
      {"Role", static_cast<int>(status.role)},
      {"CurrentSystemId", status.current_system_id},
      {"CombatProfileId", status.combat_profile_id},
      {"Shields", encode_number(status.shields)},
      {"MaximumShields", encode_number(status.maximum_shields)},
      {"Armor", encode_number(status.armor)},
      {"MaximumArmor", encode_number(status.maximum_armor)},
      {"Hull", encode_number(status.hull)},
      {"MaximumHull", encode_number(status.maximum_hull)},
      {"WeaponCooldownRemainingDays",
       encode_number(status.weapon_cooldown_remaining_days)},
      {"CurrentOrder", static_cast<int>(status.current_order)},
      {"HasAssignedAttackTarget", status.has_assigned_attack_target},
      {"DefendSystemId", status.defend_system_id},
      {"RetreatProgressDays", encode_number(status.retreat_progress_days)},
      {"RetreatDelayDays", encode_number(status.retreat_delay_days)},
      {"IsArmed", status.is_armed},
      {"IsCombatEffective", status.is_combat_effective},
      {"IsDisengaged", status.is_disengaged},
      {"CurrentStrength", encode_number(status.current_strength)},
      {"MaximumStrength", encode_number(status.maximum_strength)},
      {"RepairDeficit", encode_number(status.repair_deficit)},
      {"CurrentDurability", encode_number(status.current_durability())},
      {"MaximumDurability", encode_number(status.maximum_durability())},
      {"DurabilityRatio", encode_number(status.durability_ratio())},
      {"HullIntegrityRatio", encode_number(status.hull_integrity_ratio())},
      {"IsDamaged", status.is_damaged()},
      {"HasHullDamage", status.has_hull_damage()},
      {"IsRetreating", status.is_retreating()},
      {"CanFireNow", status.can_fire_now()},
      {"RetreatProgressRatio", encode_number(status.retreat_progress_ratio())}};
}
Json encode_result(const OwnCombatFleetStatusView &result) {
  Json fleets = Json::array();
  for (const auto &fleet : result.fleets)
    fleets.push_back(encode_status(fleet));
  return {{"CivilizationId", result.civilization_id},
          {"Summary", encode_summary(result.summary)},
          {"Fleets", fleets}};
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto &arguments = test.at("Arguments");
  World world = parse_world(arguments.at("World"));
  const int civilization_id = arguments.at("CivilizationId").get<int>();
  const Json expected_result = test.at("Result");
  const Json expected_error = test.at("Error");
  const Json expected_after = test.at("After");
  equal_json(encode_world(world), test.at("Before"), name + ".Before");

  std::optional<OwnCombatFleetStatusView> result;
  std::string error_type;
  std::string error_message;
  try {
    result = build_own_combat_fleet_status({world.civilizations, world.fleets},
                                           civilization_id);
  } catch (const std::runtime_error &error) {
    error_type = "InvalidOperationException";
    error_message = error.what();
  } catch (const std::exception &error) {
    error_type = "UnexpectedNativeException";
    error_message = error.what();
  }

  const Json actual_result = result ? encode_result(*result) : Json(nullptr);
  const Json actual_error =
      error_type.empty()
          ? Json(nullptr)
          : Json{{"Type", error_type}, {"Message", error_message}};
  equal_json(actual_result, expected_result, name + ".Result");
  equal_json(actual_error, expected_error, name + ".Error");
  equal_json(encode_world(world), expected_after, name + ".After");
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      fail("Expected fixture path.");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture.");
    Json fixture;
    stream >> fixture;
    check(fixture.at("Schema") == "stellar-own-combat-status-oracle-v1",
          "Unexpected fixture schema.");
    check(fixture.at("SourceOnlyObservations").size() == 1,
          "Expected the documented null-world source observation.");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "own combat status parity: " << fixture.at("Cases").size()
              << " actual C# cases passed; "
              << fixture.at("SourceOnlyObservations").size()
              << " source-only boundary documented\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
