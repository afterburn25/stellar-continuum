#include <stellar/core/campaign_coordinator.hpp>

#include <stellar/core/strategic_input_support.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
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
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
}
template <typename T> Json optional_json(const std::optional<T> &value) {
  return value ? Json(*value) : Json(nullptr);
}
bool is_float32_field(std::string_view field) {
  constexpr std::array fields{
      ".X", ".Y", ".Z", ".RotationDegrees", ".SensorRange",
      ".HullFraction", ".EngineFraction", ".SensorFraction",
      ".WarpDriveFraction", ".ReactorFraction", ".InterdictorFraction",
      ".ExclusionRadius"};
  return std::ranges::any_of(fields, [field](std::string_view suffix) {
    return field.ends_with(suffix);
  });
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if ((actual.is_number_integer() || actual.is_number_unsigned()) &&
      (expected.is_number_integer() || expected.is_number_unsigned())) {
    require(actual == expected, field + ": integer mismatch");
    return;
  }
  if (actual.is_number() && expected.is_number()) {
    const auto left = actual.get<double>();
    const auto right = expected.get<double>();
    if (field.ends_with(".SimulationDays")) {
      require(left == right, field + ": exact number mismatch");
      return;
    }
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    const auto relative_tolerance = is_float32_field(field) ? 1e-6 : 1e-10;
    require(std::isfinite(left) && std::isfinite(right) &&
                std::abs(left - right) <= relative_tolerance * scale,
            field + ": number mismatch");
    return;
  }
  if (actual.is_array() && expected.is_array()) {
    require(actual.size() == expected.size(), field + ": array count");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (actual.is_object() && expected.is_object()) {
    require(actual.size() == expected.size(), field + ": object count");
    for (const auto &[key, value] : expected.items()) {
      require(actual.contains(key), field + ": missing " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  require(actual == expected,
          field + ": actual=" + actual.dump() +
              " expected=" + expected.dump());
}

Vec2 parse_vec2(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
Json encode_vec2(const Vec2 &value) {
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
  result.target_fleet_id = optional<int>(value.at("TargetFleetId"));
  result.defend_system_id = optional<int>(value.at("DefendSystemId"));
  result.retreat_progress_days = number(value.at("RetreatProgressDays"));
  result.retreat_started = value.at("RetreatStarted").get<bool>();
  result.is_disengaged = value.at("IsDisengaged").get<bool>();
  result.disengaged_system_id =
      optional<int>(value.at("DisengagedSystemId"));
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
          {"TargetFleetId", optional_json(value.target_fleet_id)},
          {"DefendSystemId", optional_json(value.defend_system_id)},
          {"RetreatProgressDays", encoded_number(value.retreat_progress_days)},
          {"RetreatStarted", value.retreat_started},
          {"IsDisengaged", value.is_disengaged},
          {"DisengagedSystemId", optional_json(value.disengaged_system_id)}};
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
  require(value.at("TacticalLoadout").is_null(),
          "Fixture TacticalLoadout must be null");
  FleetState result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional<std::string>(value.at("DesignId"));
  result.position = parse_vec2(value.at("Position"));
  result.current_system_id = optional<int>(value.at("CurrentSystemId"));
  result.destination_system_id = optional<int>(value.at("DestinationSystemId"));
  result.transit_phase =
      static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  result.transit_origin_system_id =
      optional<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id =
      optional<int>(value.at("TransitTargetSystemId"));
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
      optional<std::string>(value.at("ReturnToBaseFailureReason"));
  result.mission_order_revision = value.at("MissionOrderRevision").get<int>();
  result.destination_planetary_body_id =
      optional<int>(value.at("DestinationPlanetaryBodyId"));
  result.prevent_automatic_settlement =
      value.at("PreventAutomaticSettlement").get<bool>();
  result.settlement_body_id = optional<int>(value.at("SettlementBodyId"));
  result.settlement_days_completed =
      number(value.at("SettlementDaysCompleted"));
  result.reconnaissance_system_id =
      optional<int>(value.at("ReconnaissanceSystemId"));
  result.reconnaissance_days_completed =
      number(value.at("ReconnaissanceDaysCompleted"));
  result.freight_target_outpost_id =
      optional<int>(value.at("FreightTargetOutpostId"));
  result.freight_home_colony_id =
      optional<int>(value.at("FreightHomeColonyId"));
  result.cargo_material_capacity = number(value.at("CargoMaterialCapacity"));
  result.cargo_materials = number(value.at("CargoMaterials"));
  result.strategic_speed = number(value.at("StrategicSpeed"));
  result.maximum_leg_range_light_years =
      number(value.at("MaximumLegRangeLightYears"));
  result.fuel_capacity_light_years =
      number(value.at("FuelCapacityLightYears"));
  result.fuel_remaining_light_years =
      number(value.at("FuelRemainingLightYears"));
  result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  result.is_active = value.at("IsActive").get<bool>();
  result.embarked_population_millions =
      number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id =
      optional<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  if (!value.at("Combat").is_null())
    result.combat = parse_combat(value.at("Combat"));
  if (!value.at("TacticalVessel").is_null())
    result.tactical_vessel = parse_vessel(value.at("TacticalVessel"));
  return result;
}
Json encode_fleet(const FleetState &value) {
  return {{"Id", value.id},
          {"CivilizationId", value.civilization_id},
          {"Name", value.name},
          {"Role", static_cast<int>(value.role)},
          {"DesignId", optional_json(value.design_id)},
          {"Position", encode_vec2(value.position)},
          {"CurrentSystemId", optional_json(value.current_system_id)},
          {"DestinationSystemId", optional_json(value.destination_system_id)},
          {"TransitPhase", static_cast<int>(value.transit_phase)},
          {"TransitOriginSystemId", optional_json(value.transit_origin_system_id)},
          {"TransitTargetSystemId", optional_json(value.transit_target_system_id)},
          {"TransitProgress", encoded_number(value.transit_progress)},
          {"LocalTransitStart", encode_vec2(value.local_transit_start)},
          {"LocalTransitPosition", encode_vec2(value.local_transit_position)},
          {"LocalTransitTarget", encode_vec2(value.local_transit_target)},
          {"PlannedRouteSystemIds", value.planned_route_system_ids},
          {"HoldRequested", value.hold_requested},
          {"ReturnToBaseRequested", value.return_to_base_requested},
          {"ReturnToBaseFailureReason", optional_json(value.return_to_base_failure_reason)},
          {"MissionOrderRevision", value.mission_order_revision},
          {"DestinationPlanetaryBodyId", optional_json(value.destination_planetary_body_id)},
          {"PreventAutomaticSettlement", value.prevent_automatic_settlement},
          {"SettlementBodyId", optional_json(value.settlement_body_id)},
          {"SettlementDaysCompleted", encoded_number(value.settlement_days_completed)},
          {"ReconnaissanceSystemId", optional_json(value.reconnaissance_system_id)},
          {"ReconnaissanceDaysCompleted", encoded_number(value.reconnaissance_days_completed)},
          {"FreightTargetOutpostId", optional_json(value.freight_target_outpost_id)},
          {"FreightHomeColonyId", optional_json(value.freight_home_colony_id)},
          {"CargoMaterialCapacity", encoded_number(value.cargo_material_capacity)},
          {"CargoMaterials", encoded_number(value.cargo_materials)},
          {"StrategicSpeed", encoded_number(value.strategic_speed)},
          {"MaximumLegRangeLightYears", encoded_number(value.maximum_leg_range_light_years)},
          {"FuelCapacityLightYears", encoded_number(value.fuel_capacity_light_years)},
          {"FuelRemainingLightYears", encoded_number(value.fuel_remaining_light_years)},
          {"SensorRange", encoded_number(value.sensor_range)},
          {"IsActive", value.is_active},
          {"EmbarkedPopulationMillions", encoded_number(value.embarked_population_millions)},
          {"EmbarkedPopulationSpeciesId", optional_json(value.embarked_population_species_id)},
          {"Combat", value.combat ? encode_combat(*value.combat) : Json(nullptr)},
          {"TacticalLoadout", Json(nullptr)},
          {"TacticalVessel", value.tactical_vessel ? encode_vessel(*value.tactical_vessel) : Json(nullptr)}};
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = {static_cast<float>(number(value.at("Position").at("X"))),
                     static_cast<float>(number(value.at("Position").at("Y"))),
                     optional<double>(value.at("GalacticDepthLightYears"))};
  if (!value.at("StellarClass").is_null())
    result.primary =
        static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary = static_cast<StellarClass>(
        value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary = static_cast<StellarClass>(
        value.at("TertiaryStellarClass").get<int>());
  result.catalog_preset_id =
      optional<std::string>(value.at("CatalogPresetId"));
  result.stellar_catalog_id =
      optional<std::string>(value.at("StellarCatalogId"));
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
  return result;
}
Json encode_system(const StellarSystem &value) {
  return {{"Id", value.id},
          {"Name", value.name},
          {"Position", encode_vec2({value.position.x, value.position.y})},
          {"Archetype", static_cast<int>(value.archetype)},
          {"HasHabitableWorld", value.has_habitable_world},
          {"HasAnomaly", value.has_anomaly},
          {"HasRareResource", value.has_rare_resource},
          {"HasPreWarpCivilization", value.has_pre_warp_civilization},
          {"CatalogPresetId", optional_json(value.catalog_preset_id)},
          {"StellarClass", value.primary ? Json(static_cast<int>(*value.primary)) : Json(nullptr)},
          {"SecondaryStellarClass", value.secondary ? Json(static_cast<int>(*value.secondary)) : Json(nullptr)},
          {"TertiaryStellarClass", value.tertiary ? Json(static_cast<int>(*value.tertiary)) : Json(nullptr)},
          {"GalacticDepthLightYears", optional_json(value.position.depth_light_years)},
          {"StellarCatalogId", optional_json(value.stellar_catalog_id)}};
}

PlanetaryBody parse_body(const Json &value) {
  PlanetaryBody result;
  result.id = value.at("Id").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.parent_body_id = optional<int>(value.at("ParentBodyId"));
  result.orbit_index = value.at("OrbitIndex").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.kind = static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>());
  result.radius_earth = number(value.at("RadiusEarth"));
  result.mass_earth = number(value.at("MassEarth"));
  const auto &environment = value.at("Environment");
  result.environment = {
      number(environment.at("GravityG")),
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
Json encode_body(const PlanetaryBody &value) {
  return {{"Id", value.id},
          {"SystemId", value.system_id},
          {"ParentBodyId", optional_json(value.parent_body_id)},
          {"OrbitIndex", value.orbit_index},
          {"Name", value.name},
          {"Kind", static_cast<int>(value.kind)},
          {"RadiusEarth", encoded_number(value.radius_earth)},
          {"MassEarth", encoded_number(value.mass_earth)},
          {"Environment", {{"GravityG", encoded_number(value.environment.gravity_g)},
                           {"TemperatureKelvin", encoded_number(value.environment.temperature_kelvin)},
                           {"PressureKPa", encoded_number(value.environment.pressure_kpa)},
                           {"Atmosphere", static_cast<int>(value.environment.atmosphere)},
                           {"AvailableSolvent", static_cast<int>(value.environment.available_solvent)},
                           {"RadiationHazard", encoded_number(value.environment.radiation_hazard)},
                           {"IsImmersedEnvironment", value.environment.is_immersed_environment},
                           {"HasSolidSurface", value.environment.has_solid_surface}}},
          {"LegacyColonizationCandidate", value.legacy_colonization_candidate},
          {"HasRareResource", value.has_rare_resource},
          {"HasAnomaly", value.has_anomaly},
          {"HasPreWarpCivilization", value.has_pre_warp_civilization}};
}

Civilization parse_civilization(const Json &value) {
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
  static constexpr std::string_view offices[]{
      "FleetCommander", "ChiefScientist", "Diplomat", "Governor",
      "EconomicAdvisor", "OperationsOfficer", "ExpeditionCommander"};
  const auto &source = value.at("Leadership").at("Offices");
  for (const auto office : offices) {
    if (!source.contains(office))
      continue;
    const auto &character = source.at(office);
    result.leadership.push_back(
        {std::string(office),
         {character.at("Id").get<std::string>(),
          character.at("DisplayName").get<std::string>(),
          optional<std::string>(character.at("VoiceProfileId")),
          optional<std::string>(character.at("Portrait"))}});
  }
  require(result.leadership.size() == source.size(),
          "Unsupported leadership office");
  return result;
}
Json encode_civilization(const Civilization &value) {
  Json offices = Json::object();
  for (const auto &office : value.leadership)
    offices[office.office] = {
        {"Id", office.character.id},
        {"DisplayName", office.character.display_name},
        {"VoiceProfileId", optional_json(office.character.voice_profile_id)},
        {"Portrait", optional_json(office.character.portrait)}};
  return {{"Id", value.id},
          {"Name", value.name},
          {"HomeSystemId", value.home_system_id},
          {"Archetype", static_cast<int>(value.archetype)},
          {"Traits", {{"Aggression", encoded_number(value.traits.aggression)},
                      {"Territoriality", encoded_number(value.traits.territoriality)},
                      {"Greed", encoded_number(value.traits.greed)},
                      {"ScientificCuriosity", encoded_number(value.traits.scientific_curiosity)},
                      {"RiskTolerance", encoded_number(value.traits.risk_tolerance)},
                      {"SurvivalPriority", encoded_number(value.traits.survival_priority)},
                      {"HonorBound", value.traits.honor_bound}}},
          {"IsPlayer", value.is_player},
          {"DevelopmentStage", static_cast<int>(value.development_stage)},
          {"IsSeededAncient", value.is_seeded_ancient},
          {"ExpansionAllowed", value.expansion_allowed},
          {"NeutralUnlessProvoked", value.neutral_unless_provoked},
          {"SpeciesId", value.species_id},
          {"Leadership", {{"Offices", std::move(offices)}}}};
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
      optional<std::string>(value.at("PendingUpgradeTypeId"));
  result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
  result.operating_priority = value.at("OperatingPriority").get<int>();
  result.condition = number(value.at("Condition"));
  result.stored_power_days = number(value.at("StoredPowerDays"));
  return result;
}
Json encode_building(const SurfaceBuilding &value) {
  return {{"Id", value.id},
          {"TypeId", value.type_id},
          {"X", encoded_number(value.x)},
          {"Z", encoded_number(value.z)},
          {"RotationDegrees", encoded_number(value.rotation_degrees)},
          {"IndustryProgress", encoded_number(value.industry_progress)},
          {"IsComplete", value.is_complete},
          {"IsEnabled", value.is_enabled},
          {"PendingUpgradeTypeId", optional_json(value.pending_upgrade_type_id)},
          {"UpgradeDaysRemaining", encoded_number(value.upgrade_days_remaining)},
          {"OperatingPriority", value.operating_priority},
          {"Condition", encoded_number(value.condition)},
          {"StoredPowerDays", encoded_number(value.stored_power_days)}};
}

Colony parse_colony(const Json &value) {
  Colony result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.planetary_body_id = optional<int>(value.at("PlanetaryBodyId"));
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
  if (!value.at("RemainingExtractableMaterials").is_null())
    result.remaining_extractable_materials =
        number(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = value.at("SurfaceHubLevel").get<int>();
  result.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &building : value.at("SurfaceBuildings"))
    result.surface_buildings.push_back(parse_building(building));
  return result;
}
Json encode_colony(const Colony &value) {
  Json buildings = Json::array();
  for (const auto &building : value.surface_buildings)
    buildings.push_back(encode_building(building));
  return {{"Id", value.id},
          {"CivilizationId", value.civilization_id},
          {"SystemId", value.system_id},
          {"PlanetaryBodyId", optional_json(value.planetary_body_id)},
          {"Name", value.name},
          {"Kind", static_cast<int>(value.kind)},
          {"PopulationSpeciesId", value.population_species_id},
          {"PopulationMillions", encoded_number(value.population_millions)},
          {"Infrastructure", encoded_number(value.infrastructure)},
          {"Stability", encoded_number(value.stability)},
          {"StoredFoodPopulationDaysMillions", encoded_number(value.stored_food_population_days_millions)},
          {"StoredWaterPopulationDaysMillions", encoded_number(value.stored_water_population_days_millions)},
          {"StoredExtractedMaterials", encoded_number(value.stored_extracted_materials)},
          {"RemainingExtractableMaterials", value.remaining_extractable_materials ? encoded_number(*value.remaining_extractable_materials) : Json(nullptr)},
          {"SurfaceHubLevel", value.surface_hub_level},
          {"SurfaceHubUpgradeDaysRemaining", encoded_number(value.surface_hub_upgrade_days_remaining)},
          {"SurfaceBuildings", std::move(buildings)}};
}

CivilizationEconomy parse_economy(const Json &value) {
  CivilizationEconomy result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.credits = number(value.at("Credits"));
  result.industry = number(value.at("Industry"));
  result.science = number(value.at("Science"));
  result.last_credits_per_second = number(value.at("LastCreditsPerSecond"));
  result.last_industry_per_second = number(value.at("LastIndustryPerSecond"));
  result.last_science_per_second = number(value.at("LastSciencePerSecond"));
  result.last_research_spending_per_day =
      number(value.at("LastResearchSpendingPerDay"));
  result.last_research_funding_fraction =
      number(value.at("LastResearchFundingFraction"));
  result.operating_arrears = number(value.at("OperatingArrears"));
  result.last_base_operations_funding_fraction =
      number(value.at("LastBaseOperationsFundingFraction"));
  if (!value.at("IndustryPriority").is_null())
    result.industry_priority = static_cast<IndustryPriority>(
        value.at("IndustryPriority").get<int>());
  return result;
}
Json encode_economy(const CivilizationEconomy &value) {
  return {{"CivilizationId", value.civilization_id},
          {"Credits", encoded_number(value.credits)},
          {"Industry", encoded_number(value.industry)},
          {"Science", encoded_number(value.science)},
          {"LastCreditsPerSecond", encoded_number(value.last_credits_per_second)},
          {"LastIndustryPerSecond", encoded_number(value.last_industry_per_second)},
          {"LastSciencePerSecond", encoded_number(value.last_science_per_second)},
          {"LastResearchSpendingPerDay", encoded_number(value.last_research_spending_per_day)},
          {"LastResearchFundingFraction", encoded_number(value.last_research_funding_fraction)},
          {"OperatingArrears", encoded_number(value.operating_arrears)},
          {"LastBaseOperationsFundingFraction", encoded_number(value.last_base_operations_funding_fraction)},
          {"IndustryPriority", value.industry_priority ? Json(static_cast<int>(*value.industry_priority)) : Json(nullptr)}};
}

TechnologyState parse_technology(const Json &value) {
  TechnologyState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  for (const auto &id : value.at("CompletedTechnologyIds"))
    require(result.completed_technology_ids.insert(id.get<std::string>()),
            "Duplicate completed technology ID");
  result.active_research_id =
      optional<std::string>(value.at("ActiveResearchId"));
  result.active_research_progress = number(value.at("ActiveResearchProgress"));
  return result;
}
Json encode_technology(const TechnologyState &value) {
  return {{"CivilizationId", value.civilization_id},
          {"CompletedTechnologyIds", value.completed_technology_ids.values()},
          {"ActiveResearchId", optional_json(value.active_research_id)},
          {"ActiveResearchProgress", encoded_number(value.active_research_progress)}};
}

ConstructionState parse_construction(const Json &value) {
  ConstructionState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.completed_project_ids =
      value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id =
      optional<std::string>(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  for (const auto &order : value.at("QueuedProjects"))
    result.queued_projects.push_back(
        {order.at("ProjectId").get<std::string>(),
         number(order.at("AuthorizationCredits"))});
  return result;
}
Json encode_construction(const ConstructionState &value) {
  Json queued = Json::array();
  for (const auto &order : value.queued_projects)
    queued.push_back({{"ProjectId", order.project_id},
                      {"AuthorizationCredits", encoded_number(order.authorization_credits)}});
  return {{"CivilizationId", value.civilization_id},
          {"CompletedProjectIds", value.completed_project_ids},
          {"ActiveProjectId", optional_json(value.active_project_id)},
          {"ActiveProjectProgress", encoded_number(value.active_project_progress)},
          {"ActiveProjectAuthorizationCredits", encoded_number(value.active_project_authorization_credits)},
          {"QueuedProjects", std::move(queued)}};
}

ShipyardState parse_shipyard(const Json &value) {
  ShipyardState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.next_order_sequence = value.at("NextOrderSequence").get<std::int64_t>();
  result.active_design_id = optional<std::string>(value.at("ActiveDesignId"));
  result.active_order_id = optional<std::string>(value.at("ActiveOrderId"));
  result.active_build_progress = number(value.at("ActiveBuildProgress"));
  result.active_authorization_credits =
      number(value.at("ActiveAuthorizationCredits"));
  result.reserved_population_millions =
      number(value.at("ReservedPopulationMillions"));
  result.reserved_population_species_id =
      optional<std::string>(value.at("ReservedPopulationSpeciesId"));
  result.reserved_population_source_colony_id =
      optional<int>(value.at("ReservedPopulationSourceColonyId"));
  for (const auto &order : value.at("QueuedBuilds"))
    result.queued_builds.push_back(
        {order.at("OrderId").get<std::string>(),
         order.at("DesignId").get<std::string>(),
         number(order.at("AuthorizationCredits")),
         number(order.at("ReservedPopulationMillions")),
         optional<std::string>(order.at("ReservedPopulationSpeciesId")),
         optional<int>(order.at("ReservedPopulationSourceColonyId"))});
  require(result.pending_build_count() == value.at("PendingBuildCount").get<int>(),
          "Shipyard PendingBuildCount mismatch");
  return result;
}
Json encode_shipyard(const ShipyardState &value) {
  Json queued = Json::array();
  for (const auto &order : value.queued_builds)
    queued.push_back({{"OrderId", order.order_id},
                      {"DesignId", order.design_id},
                      {"AuthorizationCredits", encoded_number(order.authorization_credits)},
                      {"ReservedPopulationMillions", encoded_number(order.reserved_population_millions)},
                      {"ReservedPopulationSpeciesId", optional_json(order.reserved_population_species_id)},
                      {"ReservedPopulationSourceColonyId", optional_json(order.reserved_population_source_colony_id)}});
  return {{"CivilizationId", value.civilization_id},
          {"NextOrderSequence", value.next_order_sequence},
          {"ActiveDesignId", optional_json(value.active_design_id)},
          {"ActiveOrderId", optional_json(value.active_order_id)},
          {"ActiveBuildProgress", encoded_number(value.active_build_progress)},
          {"ActiveAuthorizationCredits", encoded_number(value.active_authorization_credits)},
          {"ReservedPopulationMillions", encoded_number(value.reserved_population_millions)},
          {"ReservedPopulationSpeciesId", optional_json(value.reserved_population_species_id)},
          {"ReservedPopulationSourceColonyId", optional_json(value.reserved_population_source_colony_id)},
          {"QueuedBuilds", std::move(queued)},
          {"PendingBuildCount", value.pending_build_count()}};
}

CivilizationKnowledgeState parse_knowledge(const Json &value) {
  CivilizationKnowledgeState result;
  for (const auto &observer : value.at("Observers")) {
    const auto civilization_id = observer.at("CivilizationId").get<int>();
    if (observer.at("HasCoreAccess").get<bool>())
      result.unlock_galactic_core_access(civilization_id);
    if (observer.at("CoreDiscovered").get<bool>())
      (void)result.record_galactic_core_exploration(civilization_id);
    for (const auto system_id : observer.at("KnownSystems").get<std::vector<int>>())
      (void)result.reveal_system(civilization_id, system_id);
    for (const auto target_id :
         observer.at("KnownCivilizations").get<std::vector<int>>())
      (void)result.reveal_civilization(civilization_id, target_id);
    for (const auto &survey : observer.at("Survey")) {
      const auto system_id = survey.at("SystemId").get<int>();
      switch (survey.at("Level").get<int>()) {
      case 1:
        (void)result.reveal_system(civilization_id, system_id);
        break;
      case 2:
        (void)result.advance_system_survey(
            civilization_id, system_id, number(survey.at("Progress")));
        break;
      case 3:
        (void)result.mark_system_fully_surveyed(civilization_id, system_id);
        break;
      default:
        fail("Unsupported survey level");
      }
    }
  }
  require(result.galactic_core_observers() ==
              value.at("CoreObservers").get<std::vector<int>>(),
          "Knowledge CoreObservers mismatch");
  return result;
}
Json encode_knowledge(const CivilizationKnowledgeState &value,
                      std::span<const Civilization> civilizations) {
  Json observers = Json::array();
  for (const auto &civilization : civilizations) {
    Json survey = Json::array();
    for (const auto &entry :
         value.system_survey_knowledge(civilization.id))
      survey.push_back({{"SystemId", entry.system_id},
                        {"Level", static_cast<int>(entry.level)},
                        {"Progress", encoded_number(entry.progress)}});
    observers.push_back(
        {{"CivilizationId", civilization.id},
         {"HasCoreAccess", value.has_galactic_core_access(civilization.id)},
         {"CoreDiscovered", value.is_galactic_core_discovered(civilization.id)},
         {"KnownSystems", value.known_systems(civilization.id)},
         {"KnownCivilizations", value.known_civilizations(civilization.id)},
         {"Survey", std::move(survey)}});
  }
  return {{"CoreObservers", value.galactic_core_observers()},
          {"Observers", std::move(observers)}};
}

template <typename T, typename Parse>
std::vector<T> parse_array(const Json &value, Parse parse) {
  require(value.is_array(), "Expected array");
  std::vector<T> result;
  result.reserve(value.size());
  for (const auto &entry : value)
    result.push_back(parse(entry));
  return result;
}
template <typename T, typename Encode>
Json encode_array(std::span<const T> values, Encode encode) {
  Json result = Json::array();
  for (const auto &value : values)
    result.push_back(encode(value));
  return result;
}

FreshCampaignState parse_campaign(const Json &value,
                                  bool used_constrained_fallback) {
  FreshCampaignState result;
  result.seed = value.at("Seed").get<std::int64_t>();
  result.systems = parse_array<StellarSystem>(value.at("Systems"), parse_system);
  result.bodies = parse_array<PlanetaryBody>(value.at("Bodies"), parse_body);
  result.civilizations = parse_array<Civilization>(
      value.at("Civilizations"), parse_civilization);
  result.fleets = parse_array<FleetState>(value.at("Fleets"), parse_fleet);
  result.colonies = parse_array<Colony>(value.at("Colonies"), parse_colony);
  result.economies =
      parse_array<CivilizationEconomy>(value.at("Economies"), parse_economy);
  result.technologies =
      parse_array<TechnologyState>(value.at("Technologies"), parse_technology);
  result.construction = parse_array<ConstructionState>(
      value.at("Construction"), parse_construction);
  result.shipyards =
      parse_array<ShipyardState>(value.at("Shipyards"), parse_shipyard);
  result.player_civilization_id = value.at("PlayerCivilizationId").get<int>();
  result.knowledge = parse_knowledge(value.at("Knowledge"));
  if (!value.at("Core").is_null()) {
    const auto &core = value.at("Core");
    require(core.at("LandmarkKey") == "galactic-core-smbh-v1",
            "Unsupported GalacticCore landmark");
    result.core = GalacticCore{{static_cast<float>(number(core.at("X"))),
                                static_cast<float>(number(core.at("Y")))},
                               static_cast<float>(
                                   number(core.at("ExclusionRadius")))};
  }
  result.used_constrained_home_fallback = used_constrained_fallback;
  return result;
}
Json encode_campaign(const FreshCampaignState &value) {
  Json core = nullptr;
  if (value.core)
    core = {{"LandmarkKey", "galactic-core-smbh-v1"},
            {"X", encoded_number(value.core->position.x)},
            {"Y", encoded_number(value.core->position.y)},
            {"ExclusionRadius", encoded_number(value.core->exclusion_radius)}};
  return {{"Seed", value.seed},
          {"Systems", encode_array<StellarSystem>(value.systems, encode_system)},
          {"Bodies", encode_array<PlanetaryBody>(value.bodies, encode_body)},
          {"Civilizations", encode_array<Civilization>(value.civilizations, encode_civilization)},
          {"Fleets", encode_array<FleetState>(value.fleets, encode_fleet)},
          {"Colonies", encode_array<Colony>(value.colonies, encode_colony)},
          {"Economies", encode_array<CivilizationEconomy>(value.economies, encode_economy)},
          {"Technologies", encode_array<TechnologyState>(value.technologies, encode_technology)},
          {"Construction", encode_array<ConstructionState>(value.construction, encode_construction)},
          {"Shipyards", encode_array<ShipyardState>(value.shipyards, encode_shipyard)},
          {"PlayerCivilizationId", value.player_civilization_id},
          {"Knowledge", encode_knowledge(value.knowledge, value.civilizations)},
          {"Core", std::move(core)}};
}

CombatEvent parse_combat_event(const Json &value) {
  return {static_cast<CombatEventType>(value.at("Type").get<int>()),
          optional<int>(value.at("SystemId")),
          value.at("ActorCivilizationId").get<int>(),
          value.at("ActorFleetId").get<int>(),
          optional<int>(value.at("TargetCivilizationId")),
          optional<int>(value.at("TargetFleetId")),
          number(value.at("ShieldDamage")),
          number(value.at("ArmorDamage")),
          number(value.at("HullDamage")),
          value.at("Message").get<std::string>(),
          number(value.at("EmbarkedPopulationCasualtiesMillions"))};
}
Json encode_combat_event(const CombatEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"SystemId", optional_json(value.system_id)},
          {"ActorCivilizationId", value.actor_civilization_id},
          {"ActorFleetId", value.actor_fleet_id},
          {"TargetCivilizationId", optional_json(value.target_civilization_id)},
          {"TargetFleetId", optional_json(value.target_fleet_id)},
          {"ShieldDamage", encoded_number(value.shield_damage)},
          {"ArmorDamage", encoded_number(value.armor_damage)},
          {"HullDamage", encoded_number(value.hull_damage)},
          {"Message", value.message},
          {"EmbarkedPopulationCasualtiesMillions", encoded_number(value.embarked_population_casualties_millions)}};
}
Json encode_civilization_outcome(
    const CombatCivilizationOutcomeSummary &value) {
  return {{"CivilizationId", value.civilization_id},
          {"ShieldDamageDealt", encoded_number(value.shield_damage_dealt)},
          {"ArmorDamageDealt", encoded_number(value.armor_damage_dealt)},
          {"HullDamageDealt", encoded_number(value.hull_damage_dealt)},
          {"ShieldDamageTaken", encoded_number(value.shield_damage_taken)},
          {"ArmorDamageTaken", encoded_number(value.armor_damage_taken)},
          {"HullDamageTaken", encoded_number(value.hull_damage_taken)},
          {"EnemyVesselsDestroyed", value.enemy_vessels_destroyed},
          {"OwnVesselsLost", value.own_vessels_lost},
          {"RetreatsInitiated", value.retreats_initiated},
          {"SuccessfulEscapes", value.successful_escapes},
          {"EmbarkedPopulationCasualtiesInflictedMillions", encoded_number(value.embarked_population_casualties_inflicted_millions)},
          {"EmbarkedPopulationCasualtiesSufferedMillions", encoded_number(value.embarked_population_casualties_suffered_millions)},
          {"TotalDamageDealt", encoded_number(value.total_damage_dealt())},
          {"TotalDamageTaken", encoded_number(value.total_damage_taken())}};
}
Json encode_outcome(const CombatOutcomeSummary &value);
Json encode_system_outcome(const CombatSystemOutcomeSummary &value) {
  Json civilizations = Json::array();
  for (const auto &entry : value.civilizations)
    civilizations.push_back(encode_civilization_outcome(entry));
  return {{"SystemId", optional_json(value.system_id)},
          {"EventCount", value.event_count},
          {"EngagementsStarted", value.engagements_started},
          {"EngagementsEnded", value.engagements_ended},
          {"DamageEvents", value.damage_events},
          {"VesselsDestroyed", value.vessels_destroyed},
          {"RetreatsInitiated", value.retreats_initiated},
          {"SuccessfulEscapes", value.successful_escapes},
          {"TotalDamageApplied", encoded_number(value.total_damage_applied)},
          {"EmbarkedPopulationCasualtiesMillions", encoded_number(value.embarked_population_casualties_millions)},
          {"Civilizations", std::move(civilizations)}};
}
Json encode_outcome(const CombatOutcomeSummary &value) {
  Json civilizations = Json::array();
  for (const auto &entry : value.civilizations)
    civilizations.push_back(encode_civilization_outcome(entry));
  Json systems = Json::array();
  for (const auto &entry : value.systems)
    systems.push_back(encode_system_outcome(entry));
  return {{"EventCount", value.event_count},
          {"EngagementsStarted", value.engagements_started},
          {"EngagementsEnded", value.engagements_ended},
          {"DamageEvents", value.damage_events},
          {"VesselsDestroyed", value.vessels_destroyed},
          {"RetreatsInitiated", value.retreats_initiated},
          {"SuccessfulEscapes", value.successful_escapes},
          {"TotalDamageApplied", encoded_number(value.total_damage_applied)},
          {"EmbarkedPopulationCasualtiesMillions", encoded_number(value.embarked_population_casualties_millions)},
          {"Civilizations", std::move(civilizations)},
          {"Systems", std::move(systems)}};
}

Json encode_result(const SimulationStepResult &value) {
  Json allocations = Json::array();
  for (const auto &entry : value.industry_allocations)
    allocations.push_back(
        {{"CivilizationId", entry.civilization_id},
         {"AvailableIndustry", encoded_number(entry.available_industry)},
         {"ConstructionDemand", encoded_number(entry.construction_demand)},
         {"ShipbuildingDemand", encoded_number(entry.shipbuilding_demand)},
         {"ConstructionWeight", encoded_number(entry.construction_weight)},
         {"ShipbuildingWeight", encoded_number(entry.shipbuilding_weight)},
         {"ConstructionAllocated", encoded_number(entry.construction_allocated)},
         {"ShipbuildingAllocated", encoded_number(entry.shipbuilding_allocated)},
         {"TotalAllocated", encoded_number(entry.total_allocated)}});
  Json construction = Json::array();
  for (const auto &entry : value.construction_events)
    construction.push_back({{"CivilizationId", entry.civilization_id},
                            {"ProjectId", entry.project_id},
                            {"Message", entry.message}});
  Json shipbuilding = Json::array();
  for (const auto &entry : value.shipbuilding_events)
    shipbuilding.push_back({{"CivilizationId", entry.civilization_id},
                            {"FleetId", entry.fleet_id},
                            {"DesignId", entry.design_id},
                            {"Message", entry.message}});
  Json research = Json::array();
  for (const auto &entry : value.research_events)
    research.push_back({{"CivilizationId", entry.civilization_id},
                        {"TechnologyId", entry.technology_id},
                        {"Message", entry.message}});
  Json exploration = Json::array();
  for (const auto &entry : value.exploration_events)
    exploration.push_back(
        {{"Type", static_cast<int>(entry.type)},
         {"CivilizationId", entry.civilization_id},
         {"FleetId", entry.fleet_id},
         {"SystemId", entry.system_id},
         {"Message", entry.message},
         {"TargetCivilizationId", optional_json(entry.target_civilization_id)},
         {"PlanetaryBodyId", optional_json(entry.planetary_body_id)}});
  Json combat = Json::array();
  for (const auto &entry : value.combat_events)
    combat.push_back(encode_combat_event(entry));
  Json colonization = Json::array();
  for (const auto &entry : value.colonization_events)
    colonization.push_back({{"CivilizationId", entry.civilization_id},
                            {"FleetId", entry.fleet_id},
                            {"SystemId", entry.system_id},
                            {"ColonyId", entry.colony_id},
                            {"Message", entry.message}});
  return {{"SimulationDays", encoded_number(value.simulation_days)},
          {"IndustryAllocations", std::move(allocations)},
          {"ConstructionEvents", std::move(construction)},
          {"ShipbuildingEvents", std::move(shipbuilding)},
          {"ResearchEvents", std::move(research)},
          {"ExplorationEvents", std::move(exploration)},
          {"CombatEvents", std::move(combat)},
          {"ColonizationEvents", std::move(colonization)},
          {"CombatOutcome", encode_outcome(value.combat_outcome())}};
}

bool contains_call(std::span<const int> calls, int value) {
  return std::find(calls.begin(), calls.end(), value) != calls.end();
}
std::size_t phase_count(const std::vector<Json> &calls,
                        std::string_view phase) {
  return static_cast<std::size_t>(std::count_if(
      calls.begin(), calls.end(), [&](const auto &call) {
        return call.at("Phase").template get<std::string>() == phase;
      }));
}
struct CallbackLog {
  std::vector<Json> calls;
  std::vector<int> capability_throw_calls;
  std::vector<int> hostility_throw_calls;
};

struct ErrorInfo {
  std::string type;
  std::string message;
};
template <typename Error>
ErrorInfo error_info(const Error &error, std::string type) {
  return {std::move(type), error.what()};
}
void equal_error(const std::optional<ErrorInfo> &actual, const Json &expected,
                 const std::string &field) {
  require(actual.has_value() != expected.is_null(), field + ": presence");
  if (!actual)
    return;
  require(actual->type == expected.at("Type").get<std::string>(),
          field + ": type " + actual->type);
  require(actual->message == expected.at("Message").get<std::string>(),
          field + ": message actual=" + actual->message +
              " expected=" + expected.at("Message").get<std::string>());
}

CivilizationStrategicRuntimeCoordinator make_strategic(
    bool injected_knowledge, const std::shared_ptr<CallbackLog> &log) {
  CivilizationStrategicInputBuilder input{
      StrategicLogisticsQuery{}, StrategicShipbuildingCapabilityQuery{},
      StrategicExplorationPlanQuery{}};
  CivilizationStrategicDirector director{std::move(input)};
  StrategicKnowledgeQuery knowledge;
  if (injected_knowledge)
    knowledge = [log](int observer_id, std::int64_t now_tick) {
      log->calls.push_back({{"Phase", "StrategicKnowledge"},
                            {"ObserverCivilizationId", observer_id},
                            {"NowTick", now_tick}});
      return StrategicKnowledgeSnapshot{now_tick, {}};
    };
  return CivilizationStrategicRuntimeCoordinator{
      std::move(director), std::move(knowledge)};
}

std::unique_ptr<GalaxySimulationStepCoordinator> make_coordinator(
    const Json &controls, const std::shared_ptr<CallbackLog> &log) {
  const auto use_default = controls.at("UseDefaultConstructor").get<bool>();
  const auto combat_mode = controls.at("CombatMode").get<std::string>();
  const auto capability_mode =
      controls.at("CapabilityMode").get<std::string>();
  const auto knowledge_mode = controls.at("KnowledgeMode").get<std::string>();
  require(combat_mode == "Matched" || combat_mode == "Raw",
          "Unknown CombatMode");
  require(capability_mode == "DefaultPrototype" ||
              capability_mode == "Injected",
          "Unknown CapabilityMode");
  require(knowledge_mode == "DefaultEmpty" || knowledge_mode == "Injected",
          "Unknown KnowledgeMode");
  require(!use_default ||
              (combat_mode == "Matched" &&
               capability_mode == "DefaultPrototype" &&
               knowledge_mode == "DefaultEmpty" &&
               controls.at("AdvanceLegacyResearch").get<bool>() &&
               controls.at("UseStrategicShipbuildingPreferences").get<bool>()),
          "Default constructor controls are inconsistent");
  if (use_default)
    return std::make_unique<GalaxySimulationStepCoordinator>();

  SourceCompatibleCampaignConfiguration configuration;
  configuration.advance_legacy_research =
      controls.at("AdvanceLegacyResearch").get<bool>();
  configuration.use_strategic_shipbuilding_preferences =
      controls.at("UseStrategicShipbuildingPreferences").get<bool>();
  if (capability_mode == "Injected") {
    configuration.construction_capability =
        [log, own_call_count = 0](
            std::span<const TechnologyState> technologies,
            int civilization_id,
            std::string_view capability_id) mutable {
          ++own_call_count;
          require(static_cast<std::size_t>(own_call_count) ==
                      phase_count(log->calls, "ConstructionCapability") + 1,
                  "Construction capability callable was cloned");
          const auto technology = std::find_if(
              technologies.begin(), technologies.end(),
              [&](const auto &state) {
                return state.civilization_id == civilization_id;
              });
          const bool result =
              technology != technologies.end() &&
              technology->completed_technology_ids.contains(capability_id);
          log->calls.push_back({{"Phase", "ConstructionCapability"},
                                {"CivilizationId", civilization_id},
                                {"CapabilityId", capability_id},
                                {"Result", result}});
          if (contains_call(log->capability_throw_calls,
                            static_cast<int>(log->calls.size())))
            throw std::runtime_error("campaign-step capability failure");
          return result;
        };
    configuration.shipbuilding_capability =
        [log, own_call_count = 0](
            std::span<const TechnologyState> technologies,
            int civilization_id,
            std::string_view capability_id) mutable {
          ++own_call_count;
          require(static_cast<std::size_t>(own_call_count) ==
                      phase_count(log->calls, "ShipbuildingCapability") + 1,
                  "Shipbuilding capability callable was cloned");
          const auto technology = std::find_if(
              technologies.begin(), technologies.end(),
              [&](const auto &state) {
                return state.civilization_id == civilization_id;
              });
          bool result = false;
          if (technology != technologies.end()) {
            if (capability_id == "spacecraft_construction")
              result = technology->completed_technology_ids.contains(
                  "orbital_industry");
            else if (capability_id == "experimental_interstellar_transit")
              result = technology->completed_technology_ids.contains(
                  "prototype_warp_drive");
          }
          log->calls.push_back({{"Phase", "ShipbuildingCapability"},
                                {"CivilizationId", civilization_id},
                                {"CapabilityId", capability_id},
                                {"Result", result}});
          if (contains_call(log->capability_throw_calls,
                            static_cast<int>(log->calls.size())))
            throw std::runtime_error("campaign-step capability failure");
          return result;
        };
  }
  auto hostility = [log, call_index = 0](int first, int second) mutable {
    ++call_index;
    log->calls.push_back({{"Phase", "Hostility"},
                          {"FirstCivilizationId", first},
                          {"SecondCivilizationId", second}});
    if (contains_call(log->hostility_throw_calls, call_index))
      throw std::runtime_error("campaign-step hostility failure");
    return true;
  };
  if (knowledge_mode == "Injected") {
    auto strategic = make_strategic(true, log);
    if (combat_mode == "Raw")
      return std::make_unique<GalaxySimulationStepCoordinator>(
          std::move(configuration), std::move(strategic),
          CombatSimulation{std::move(hostility)});
    return std::make_unique<GalaxySimulationStepCoordinator>(
        std::move(configuration), std::move(strategic),
        CombatCommandRuntime{std::move(hostility)});
  }
  if (combat_mode == "Raw")
    return std::make_unique<GalaxySimulationStepCoordinator>(
        std::move(configuration), CombatSimulation{std::move(hostility)});
  return std::make_unique<GalaxySimulationStepCoordinator>(
      std::move(configuration),
      CombatCommandRuntime{std::move(hostility)});
}

void run_outcome_case(const Json &test_case) {
  const auto events = parse_array<CombatEvent>(
      test_case.at("Arguments").at("Events"), parse_combat_event);
  const auto expected_result = test_case.at("Result");
  const auto expected_error = test_case.at("Error");
  std::optional<CombatOutcomeSummary> result;
  std::optional<ErrorInfo> error;
  try {
    result = summarize_combat_outcome(events);
  } catch (const std::overflow_error &caught) {
    error = error_info(caught, "OverflowException");
  } catch (const std::out_of_range &caught) {
    error = error_info(caught, "ArgumentOutOfRangeException");
  } catch (const std::invalid_argument &caught) {
    error = error_info(caught, "ArgumentException");
  } catch (const std::runtime_error &caught) {
    error = error_info(caught, "InvalidOperationException");
  }
  equal_error(error, expected_error, test_case.at("Name"));
  require(result.has_value() != expected_result.is_null(),
          test_case.at("Name").get<std::string>() + ": result presence");
  if (result)
    equal_json(encode_outcome(*result), expected_result,
               test_case.at("Name").get<std::string>() + ".Result");
}

void run_step_case(const Json &test_case) {
  const auto name = test_case.at("Name").get<std::string>();
  const auto &arguments = test_case.at("Arguments");
  const auto &controls = arguments.at("InjectionControls");
  const auto used_fallback =
      controls.at("UsedConstrainedHomeFallback").get<bool>();
  auto campaign = parse_campaign(arguments.at("InitialState"), used_fallback);
  equal_json(encode_campaign(campaign), test_case.at("Before"),
             name + ".Before");
  auto state = std::make_unique<CampaignSimulationState>(std::move(campaign));
  auto log = std::make_shared<CallbackLog>();
  log->capability_throw_calls =
      controls.at("CapabilityThrowCalls").get<std::vector<int>>();
  log->hostility_throw_calls =
      controls.at("HostilityThrowCalls").get<std::vector<int>>();
  auto coordinator = make_coordinator(controls, log);
  require(coordinator->has_matched_combat_runtime() ==
              (controls.at("CombatMode").get<std::string>() == "Matched"),
          name + ": combat composition");

  const auto &steps = arguments.at("Steps");
  const auto &commands = test_case.at("Commands");
  require(steps.size() == commands.size(), name + ": command count");
  for (std::size_t index = 0; index < commands.size(); ++index) {
    const auto &command = commands[index];
    const auto days = number(steps[index].at("SimulationDays"));
    require(command.at("Input").at("SimulationDays") ==
                steps[index].at("SimulationDays"),
            name + ": decoded step mismatch");
    const auto call_start = log->calls.size();
    const auto expected_result = command.at("Result");
    const auto expected_error = command.at("Error");
    std::optional<SimulationStepResult> result;
    std::optional<ErrorInfo> error;
    try {
      result = coordinator->advance(state.get(), days);
    } catch (const std::overflow_error &caught) {
      error = error_info(caught, "OverflowException");
    } catch (const std::out_of_range &caught) {
      error = error_info(caught, "ArgumentOutOfRangeException");
    } catch (const std::invalid_argument &caught) {
      error = error_info(caught, "ArgumentException");
    } catch (const std::runtime_error &caught) {
      error = error_info(caught, "InvalidOperationException");
    }
    equal_error(error, expected_error,
                name + ".Commands[" + std::to_string(index) + "].Error");
    require(result.has_value() != expected_result.is_null(),
            name + ": result presence");
    if (result)
      equal_json(encode_result(*result), expected_result,
                 name + ".Commands[" + std::to_string(index) + "].Result");
    equal_json(encode_campaign(state->campaign()), command.at("After"),
               name + ".Commands[" + std::to_string(index) + "].After");
    Json calls = Json::array();
    for (auto call = call_start; call < log->calls.size(); ++call)
      calls.push_back(log->calls[call]);
    equal_json(calls, command.at("CallbackCalls"),
               name + ".Commands[" + std::to_string(index) +
                   "].CallbackCalls");
  }
  equal_json(encode_campaign(state->campaign()), test_case.at("After"),
             name + ".After");
  equal_json(log->calls, test_case.at("CallbackCalls"),
             name + ".CallbackCalls");
  require(state->campaign().used_constrained_home_fallback == used_fallback,
          name + ": native fallback metadata mutated");
}

void test_shared_capability_callable_survives_move(const Json &fixture) {
  const auto found = std::find_if(
      fixture.at("Cases").begin(), fixture.at("Cases").end(),
      [](const auto &entry) {
        return entry.at("Name") == "injected-capability-on-demand";
      });
  require(found != fixture.at("Cases").end(),
          "Missing ownership probe source scenario");
  const auto &test_case = *found;
  auto campaign = parse_campaign(
      test_case.at("Arguments").at("InitialState"), false);
  CampaignSimulationState state{std::move(campaign)};
  auto observations =
      std::make_shared<std::vector<std::pair<std::string, int>>>();
  SourceCompatibleCampaignConfiguration configuration;
  configuration.construction_capability =
      [observations, sequence = 0](
          std::span<const TechnologyState> technologies,
          int civilization_id, std::string_view capability_id) mutable {
        observations->emplace_back("ConstructionCapability", ++sequence);
        return prototype_construction_has_capability(
            technologies, civilization_id, capability_id);
      };
  configuration.shipbuilding_capability =
      [observations, sequence = 0](
          std::span<const TechnologyState> technologies,
          int civilization_id, std::string_view capability_id) mutable {
        observations->emplace_back("ShipbuildingCapability", ++sequence);
        return prototype_shipbuilding_has_capability(
            technologies, civilization_id, capability_id);
      };
  GalaxySimulationStepCoordinator coordinator{
      std::move(configuration), CombatCommandRuntime{CombatHostilityView{}}};
  const auto &commands = test_case.at("Commands");
  auto first = coordinator.advance(
      &state, number(commands[0].at("Input").at("SimulationDays")));
  equal_json(encode_result(first), commands[0].at("Result"),
             "native shared callback first result");
  equal_json(encode_campaign(state.campaign()), commands[0].at("After"),
             "native shared callback first state");
  GalaxySimulationStepCoordinator moved{std::move(coordinator)};
  auto second = moved.advance(
      &state, number(commands[1].at("Input").at("SimulationDays")));
  equal_json(encode_result(second), commands[1].at("Result"),
             "native shared callback moved result");
  equal_json(encode_campaign(state.campaign()), commands[1].at("After"),
             "native shared callback moved state");
  require(observations->size() > 2,
          "Shared capability ownership probe made too few calls");
  int next_construction = 1;
  int next_shipbuilding = 1;
  for (const auto &[phase, sequence] : *observations) {
    auto &next = phase == "ConstructionCapability" ? next_construction
                                                    : next_shipbuilding;
    require(sequence == next++,
            "Capability callable copy or move broke its private sequence");
  }
  require(next_construction > 1 && next_shipbuilding > 2,
          "Ownership probe lacks cross-consumer capability calls");
}

void test_lane_ownership_and_invalidation() {
  FreshCampaignState campaign;
  campaign.systems = {{1, "A", {0, 0, 0.0}},
                      {2, "B", {1, 0, 0.0}}};
  CampaignSimulationState state{std::move(campaign)};
  (void)state.lanes().find_shortest_route(1, 2, 10.0);
  require(state.cached_lane_route_tree_count() == 1,
          "Lane route cache was not retained");
  auto identical_astronomy = state.campaign().systems;
  state.campaign().systems.swap(identical_astronomy);
  identical_astronomy.clear();
  identical_astronomy.shrink_to_fit();
  require(state.cached_lane_route_tree_count() == 1,
          "Identical astronomy replacement invalidated the lane cache");
  require(state.lanes().find_shortest_route(1, 2, 10.0) ==
              std::vector<int>({1, 2}),
          "Lane graph borrowed the freed astronomy vector");
  auto moved = std::move(state);
  require(moved.cached_lane_route_tree_count() == 1,
          "Moving campaign state lost the retained lane cache");
  moved.campaign().systems[1].position.x = 3;
  require(moved.cached_lane_route_tree_count() == 0,
          "Changed astronomy did not invalidate the lane cache");
  const auto lanes = moved.lanes().build();
  require(lanes.size() == 1 && std::abs(lanes.front().length_light_years - 3.0) <
                                    1e-12,
          "Changed astronomy rebuilt an incorrect lane graph");
}

void test_time_validation_precedes_lane_graph() {
  FreshCampaignState campaign;
  campaign.systems = {{1, "Duplicate A", {0, 0, 0.0}},
                      {1, "Duplicate B", {1, 0, 0.0}}};
  CampaignSimulationState state{std::move(campaign)};
  GalaxySimulationStepCoordinator coordinator;
  const auto empty = coordinator.advance(&state, 0.0);
  require(empty.simulation_days == 0.0 &&
              empty.industry_allocations.empty(),
          "Zero time did not return before malformed astronomy");
  bool rejected = false;
  try {
    (void)coordinator.advance(
        &state, std::numeric_limits<double>::quiet_NaN());
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  require(rejected, "Invalid time did not precede malformed astronomy");
}

void test_null_advance() {
  GalaxySimulationStepCoordinator coordinator;
  std::optional<ErrorInfo> error;
  try {
    (void)coordinator.advance(nullptr, 1.0);
  } catch (const std::invalid_argument &caught) {
    error = error_info(caught, "ArgumentException");
  }
  require(error && error->message ==
                       "Value cannot be null. (Parameter 'galaxy')",
          "Null campaign parity");
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: campaign_coordinator_tests <fixture>");
    std::ifstream input(argv[1]);
    require(input.good(), "Unable to open campaign-step fixture");
    Json fixture;
    input >> fixture;
    require(fixture.at("Schema") == "stellar-campaign-step-oracle-v1",
            "Unexpected schema");
    require(fixture.at("SourceOnlyObservations").size() == 4,
            "Unexpected source-only observation count");
    int step_cases = 0;
    int outcome_cases = 0;
    for (const auto &test_case : fixture.at("Cases")) {
      const auto kind = test_case.at("Kind").get<std::string>();
      if (kind == "Step") {
        run_step_case(test_case);
        ++step_cases;
      } else if (kind == "CombatOutcome") {
        run_outcome_case(test_case);
        ++outcome_cases;
      } else {
        fail("Unknown case kind: " + kind);
      }
    }
    require(step_cases == 28 && outcome_cases == 8,
            "Unexpected campaign fixture case counts");
    test_shared_capability_callable_survives_move(fixture);
    test_lane_ownership_and_invalidation();
    test_time_validation_precedes_lane_graph();
    test_null_advance();
    std::cout << "campaign coordinator parity: " << step_cases
              << " step cases, " << outcome_cases
              << " combat outcome cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "campaign coordinator parity failed: " << error.what()
              << '\n';
    return 1;
  }
}
