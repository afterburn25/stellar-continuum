#include <stellar/core/exploration_advance.hpp>

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
  if (!condition) fail(message);
}
double number(const Json &value) {
  if (value.is_number()) return value.get<double>();
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
template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (actual.is_number() && expected.is_number()) {
    const auto first = actual.get<double>();
    const auto second = expected.get<double>();
    const auto scale = std::max({1.0, std::abs(first), std::abs(second)});
    check(std::abs(first - second) <= 1e-7 * scale,
          field + ": number mismatch");
    return;
  }
  if (actual.is_array() && expected.is_array()) {
    check(actual.size() == expected.size(), field + ": array size mismatch");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (actual.is_object() && expected.is_object()) {
    check(actual.size() == expected.size(), field + ": object size mismatch");
    for (const auto &[key, value] : expected.items()) {
      check(actual.contains(key), field + ": missing key " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  check(actual == expected, field + ": JSON mismatch; actual=" + actual.dump() +
                                ", expected=" + expected.dump());
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.position = {
      static_cast<float>(number(value.at("Position").at("X"))),
      static_cast<float>(number(value.at("Position").at("Y"))),
      optional<double>(value.at("GalacticDepthLightYears"))};
  result.archetype = static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.catalog_preset_id = optional<std::string>(value.at("CatalogPresetId"));
  if (!value.at("StellarClass").is_null())
    result.primary = static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary = static_cast<StellarClass>(value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary = static_cast<StellarClass>(value.at("TertiaryStellarClass").get<int>());
  result.stellar_catalog_id = optional<std::string>(value.at("StellarCatalogId"));
  return result;
}

PlanetaryBody parse_body(const Json &value) {
  PlanetaryBody result;
  result.id = value.at("Id");
  result.system_id = value.at("SystemId");
  result.parent_body_id = optional<int>(value.at("ParentBodyId"));
  result.orbit_index = value.at("OrbitIndex");
  result.name = value.at("Name");
  result.kind = static_cast<PlanetaryBodyKind>(value.at("Kind").get<int>());
  result.radius_earth = number(value.at("RadiusEarth"));
  result.mass_earth = number(value.at("MassEarth"));
  const auto &environment = value.at("Environment");
  result.environment = {
      number(environment.at("GravityG")),
      number(environment.at("TemperatureKelvin")),
      number(environment.at("PressureKPa")),
      static_cast<PlanetaryAtmosphereRegime>(environment.at("Atmosphere").get<int>()),
      static_cast<PlanetarySolventRegime>(environment.at("AvailableSolvent").get<int>()),
      number(environment.at("RadiationHazard")),
      environment.at("IsImmersedEnvironment"),
      environment.at("HasSolidSurface")};
  result.legacy_colonization_candidate = value.at("LegacyColonizationCandidate");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.orbital_eccentricity = number(value.at("OrbitalEccentricity"));
  result.orbital_inclination_degrees = number(value.at("OrbitalInclinationDegrees"));
  return result;
}

Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.home_system_id = value.at("HomeSystemId");
  result.archetype = static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound")};
  result.is_player = value.at("IsPlayer");
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient");
  result.expansion_allowed = value.at("ExpansionAllowed");
  result.neutral_unless_provoked = value.at("NeutralUnlessProvoked");
  result.species_id = value.at("SpeciesId");
  return result;
}

Colony parse_colony(const Json &value) {
  Colony result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.system_id = value.at("SystemId");
  result.planetary_body_id = optional<int>(value.at("PlanetaryBodyId"));
  result.name = value.at("Name");
  result.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  result.population_species_id = value.at("PopulationSpeciesId");
  result.population_millions = number(value.at("PopulationMillions"));
  result.infrastructure = number(value.at("Infrastructure"));
  result.stability = number(value.at("Stability"));
  result.stored_food_population_days_millions = number(value.at("StoredFoodPopulationDaysMillions"));
  result.stored_water_population_days_millions = number(value.at("StoredWaterPopulationDaysMillions"));
  result.stored_extracted_materials = number(value.at("StoredExtractedMaterials"));
  result.remaining_extractable_materials = optional<double>(value.at("RemainingExtractableMaterials"));
  result.surface_hub_level = value.at("SurfaceHubLevel");
  result.surface_hub_upgrade_days_remaining = number(value.at("SurfaceHubUpgradeDaysRemaining"));
  check(value.at("SurfaceBuildings").empty(), "Fixture surface buildings are unsupported");
  return result;
}

CivilizationEconomy parse_economy(const Json &value) {
  CivilizationEconomy result;
  result.civilization_id = value.at("CivilizationId");
  result.credits = number(value.at("Credits"));
  result.industry = number(value.at("Industry"));
  result.science = number(value.at("Science"));
  result.last_credits_per_second = number(value.at("LastCreditsPerSecond"));
  result.last_industry_per_second = number(value.at("LastIndustryPerSecond"));
  result.last_science_per_second = number(value.at("LastSciencePerSecond"));
  result.last_research_spending_per_day = number(value.at("LastResearchSpendingPerDay"));
  result.last_research_funding_fraction = number(value.at("LastResearchFundingFraction"));
  result.operating_arrears = number(value.at("OperatingArrears"));
  result.last_base_operations_funding_fraction = number(value.at("LastBaseOperationsFundingFraction"));
  if (!value.at("IndustryPriority").is_null())
    result.industry_priority = static_cast<IndustryPriority>(value.at("IndustryPriority").get<int>());
  return result;
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
  result.profile_id = value.at("ProfileId");
  result.shields = number(value.at("Shields"));
  result.armor = number(value.at("Armor"));
  result.hull = number(value.at("Hull"));
  result.weapon_cooldown_remaining_days =
      number(value.at("WeaponCooldownRemainingDays"));
  result.order = static_cast<MilitaryOrderType>(value.at("Order").get<int>());
  result.target_fleet_id = optional<int>(value.at("TargetFleetId"));
  result.defend_system_id = optional<int>(value.at("DefendSystemId"));
  result.retreat_progress_days = number(value.at("RetreatProgressDays"));
  result.retreat_started = value.at("RetreatStarted");
  result.is_disengaged = value.at("IsDisengaged");
  result.disengaged_system_id = optional<int>(value.at("DisengagedSystemId"));
  return result;
}

Json encode_combat(const FleetCombatState &value) {
  return {{"ProfileId", value.profile_id},
          {"Shields", encoded_number(value.shields)},
          {"Armor", encoded_number(value.armor)},
          {"Hull", encoded_number(value.hull)},
          {"WeaponCooldownRemainingDays",
           encoded_number(value.weapon_cooldown_remaining_days)},
          {"Order", value.order},
          {"TargetFleetId", value.target_fleet_id},
          {"DefendSystemId", value.defend_system_id},
          {"RetreatProgressDays", encoded_number(value.retreat_progress_days)},
          {"RetreatStarted", value.retreat_started},
          {"IsDisengaged", value.is_disengaged},
          {"DisengagedSystemId", value.disengaged_system_id}};
}

MassiveCombatLoadout parse_loadout(const Json &value) {
  MassiveCombatLoadout result;
  result.mass_per_ship = static_cast<float>(number(value.at("MassPerShip")));
  result.acceleration = static_cast<float>(number(value.at("Acceleration")));
  result.maximum_speed = static_cast<float>(number(value.at("MaximumSpeed")));
  result.shield_per_ship = static_cast<float>(number(value.at("ShieldPerShip")));
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
  result.module_slot_capacity = value.at("ModuleSlotCapacity");
  result.maximum_module_mass =
      static_cast<float>(number(value.at("MaximumModuleMass")));
  check(value.at("Weapons").empty() && value.at("Modules").empty(),
        "Exploration tactical fixture uses scalar loadout history only");
  return result;
}

Json encode_loadout(const MassiveCombatLoadout &value) {
  check(value.weapons.empty() && value.modules.empty(),
        "Exploration tactical loadout collections changed unexpectedly");
  return {{"MassPerShip", encoded_number(value.mass_per_ship)},
          {"Acceleration", encoded_number(value.acceleration)},
          {"MaximumSpeed", encoded_number(value.maximum_speed)},
          {"ShieldPerShip", encoded_number(value.shield_per_ship)},
          {"ArmorPerShip", encoded_number(value.armor_per_ship)},
          {"HullPerShip", encoded_number(value.hull_per_ship)},
          {"ReactorOutputPerShip",
           encoded_number(value.reactor_output_per_ship)},
          {"CoolingPerShip", encoded_number(value.cooling_per_ship)},
          {"WarpStabilization", encoded_number(value.warp_stabilization)},
          {"WarpSpoolSeconds", encoded_number(value.warp_spool_seconds)},
          {"ModuleSlotCapacity", value.module_slot_capacity},
          {"MaximumModuleMass", encoded_number(value.maximum_module_mass)},
          {"Weapons", Json::array()},
          {"Modules", Json::array()}};
}

MassiveVesselState parse_vessel(const Json &value) {
  MassiveVesselState result;
  result.id = value.at("Id").get<std::int64_t>();
  result.name = value.at("Name");
  result.design_id = value.at("DesignId");
  result.is_flagship = value.at("IsFlagship");
  result.is_carrier = value.at("IsCarrier");
  result.is_interdictor = value.at("IsInterdictor");
  result.is_story_ship = value.at("IsStoryShip");
  result.hull_fraction = static_cast<float>(number(value.at("HullFraction")));
  result.engine_fraction = static_cast<float>(number(value.at("EngineFraction")));
  result.sensor_fraction = static_cast<float>(number(value.at("SensorFraction")));
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
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
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
  if (!value.at("Combat").is_null())
    result.combat = parse_combat(value.at("Combat"));
  if (!value.at("TacticalLoadout").is_null())
    result.tactical_loadout = parse_loadout(value.at("TacticalLoadout"));
  if (!value.at("TacticalVessel").is_null())
    result.tactical_vessel = parse_vessel(value.at("TacticalVessel"));
  return result;
}

Json encode_fleet(const FleetState &value) {
  return {{"Id", value.id},
          {"CivilizationId", value.civilization_id},
          {"Name", value.name},
          {"Role", value.role},
          {"DesignId", value.design_id},
          {"Position", encode_vec2(value.position)},
          {"CurrentSystemId", value.current_system_id},
          {"DestinationSystemId", value.destination_system_id},
          {"TransitPhase", value.transit_phase},
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
          {"Combat", value.combat ? encode_combat(*value.combat) : Json(nullptr)},
          {"TacticalLoadout", value.tactical_loadout
                                  ? encode_loadout(*value.tactical_loadout)
                                  : Json(nullptr)},
          {"TacticalVessel", value.tactical_vessel
                                 ? encode_vessel(*value.tactical_vessel)
                                 : Json(nullptr)}};
}

struct World {
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  CivilizationKnowledgeState knowledge;
  std::vector<int> observer_ids;
};

World parse_world(const Json &value) {
  World result;
  for (const auto &entry : value.at("Systems")) result.systems.push_back(parse_system(entry));
  for (const auto &entry : value.at("Bodies")) result.bodies.push_back(parse_body(entry));
  for (const auto &entry : value.at("Civilizations")) result.civilizations.push_back(parse_civilization(entry));
  for (const auto &entry : value.at("Fleets")) result.fleets.push_back(parse_fleet(entry));
  for (const auto &entry : value.at("Colonies")) result.colonies.push_back(parse_colony(entry));
  for (const auto &entry : value.at("Economies")) result.economies.push_back(parse_economy(entry));
  for (const auto &observer : value.at("Knowledge")) {
    const auto id = observer.at("CivilizationId").get<int>();
    result.observer_ids.push_back(id);
    for (const auto &survey : observer.at("Surveys")) {
      const auto system_id = survey.at("SystemId").get<int>();
      const auto level = survey.at("Level").get<int>();
      const auto progress = number(survey.at("Progress"));
      if (level == static_cast<int>(SystemSurveyLevel::detected))
        result.knowledge.reveal_system(id, system_id);
      else if (level == static_cast<int>(SystemSurveyLevel::partially_surveyed))
        result.knowledge.advance_system_survey(id, system_id, progress);
      else if (level == static_cast<int>(SystemSurveyLevel::fully_surveyed))
        result.knowledge.mark_system_fully_surveyed(id, system_id);
      else fail("Unsupported survey level in fixture");
    }
    for (const auto target : observer.at("KnownCivilizations"))
      result.knowledge.reveal_civilization(id, target.get<int>());
    if (observer.at("CoreAccess")) result.knowledge.unlock_galactic_core_access(id);
    if (observer.at("CoreDiscovered")) {
      result.knowledge.unlock_galactic_core_access(id);
      result.knowledge.record_galactic_core_exploration(id);
    }
    check(result.knowledge.known_systems(id) ==
              observer.at("KnownSystems").get<std::vector<int>>(),
          "Imported knowledge known-system mismatch");
  }
  check(result.knowledge.galactic_core_observers() ==
            value.at("CoreObservers").get<std::vector<int>>(),
        "Imported core observer mismatch");
  return result;
}

Json encode_knowledge(const World &world) {
  Json result = Json::array();
  for (const auto id : world.observer_ids) {
    Json surveys = Json::array();
    for (const auto &survey : world.knowledge.system_survey_knowledge(id))
      surveys.push_back({{"SystemId", survey.system_id},
                         {"Level", survey.level},
                         {"Progress", encoded_number(survey.progress)}});
    result.push_back({{"CivilizationId", id},
                      {"KnownSystems", world.knowledge.known_systems(id)},
                      {"KnownCivilizations", world.knowledge.known_civilizations(id)},
                      {"Surveys", std::move(surveys)},
                      {"CoreAccess", world.knowledge.has_galactic_core_access(id)},
                      {"CoreDiscovered", world.knowledge.is_galactic_core_discovered(id)}});
  }
  return result;
}

Json encode_world(const World &world, const Json &immutable_source) {
  auto result = immutable_source;
  result["Fleets"] = Json::array();
  for (const auto &fleet : world.fleets)
    result["Fleets"].push_back(encode_fleet(fleet));
  result["Knowledge"] = encode_knowledge(world);
  result["CoreObservers"] = world.knowledge.galactic_core_observers();
  return result;
}

Json encode_event(const ExplorationEvent &event) {
  return {{"Type", event.type},
          {"CivilizationId", event.civilization_id},
          {"FleetId", event.fleet_id},
          {"SystemId", event.system_id},
          {"Message", event.message},
          {"TargetCivilizationId", event.target_civilization_id},
          {"PlanetaryBodyId", event.planetary_body_id}};
}

struct ErrorInfo { std::string type, message; };
ErrorInfo classify(const std::exception &error) {
  if (dynamic_cast<const std::out_of_range *>(&error))
    return {"ArgumentOutOfRangeException", error.what()};
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return {"ArgumentException", error.what()};
  if (dynamic_cast<const std::overflow_error *>(&error))
    return {"OverflowError", error.what()};
  if (dynamic_cast<const std::runtime_error *>(&error))
    return {"InvalidOperationException", error.what()};
  return {"UnexpectedNativeException", error.what()};
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  check(test.at("Kind") == "Advance", name + ": unknown kind");
  const auto arguments = test.at("Arguments");
  const auto input_world = arguments.at("World");
  const auto delta = number(arguments.at("SimulationDelta"));
  const auto expected_result = test.at("Result");
  const auto expected_error = test.at("Error");
  const auto expected_before = test.at("Before");
  const auto expected_after = test.at("After");
  check(input_world == expected_before, name + ": Arguments.World differs from Before");
  if (!expected_error.is_null()) {
    const auto type = expected_error.at("Type").get<std::string>();
    check(type == "ArgumentOutOfRangeException" ||
              type == "InvalidOperationException",
          name + ": unsupported expected error type");
    check(expected_result.is_null(), name + ": error result must be null");
  } else {
    check(expected_result.is_array(), name + ": result must be an event array");
  }

  auto world = parse_world(input_world);
  equal_json(encode_world(world, input_world), expected_before,
             name + ".Before");
  InterstellarLaneNetwork lanes(world.systems);
  ExplorationSimulation simulation;
  std::optional<std::vector<ExplorationEvent>> events;
  std::optional<ErrorInfo> error;
  try {
    events = simulation.advance(
        {world.systems, world.bodies, world.civilizations, world.fleets,
         world.colonies, world.economies, world.knowledge, lanes},
        delta);
  } catch (const std::exception &caught) {
    error = classify(caught);
  }

  check(error.has_value() == !expected_error.is_null(),
        name + ": error presence mismatch" +
            (error ? " (" + error->type + ": " + error->message + ")" : ""));
  if (error) {
    check(error->type == expected_error.at("Type").get<std::string>(),
          name + ": error type mismatch");
    check(error->message == expected_error.at("Message").get<std::string>(),
          name + ": error message mismatch: " + error->message);
  } else {
    Json actual_events = Json::array();
    for (const auto &event : *events) actual_events.push_back(encode_event(event));
    equal_json(actual_events, expected_result, name + ".Result");
  }
  equal_json(encode_world(world, input_world), expected_after, name + ".After");
}

void check_source_only_null_boundaries(const Json &values) {
  check(values.size() == 3, "Expected three source-only null-galaxy observations");
  const auto check_entry = [&](std::size_t index, double delta,
                               std::optional<std::string_view> type,
                               std::optional<std::string_view> message) {
    const auto &entry = values[index];
    check(number(entry.at("SimulationDelta")) == delta,
          "Source-only null delta mismatch");
    if (!type) {
      check(entry.at("Error").is_null(),
            "Zero delta must return before dereferencing null galaxy");
      return;
    }
    check(entry.at("Error").at("Type").get<std::string>() == *type,
          "Source-only null error type mismatch");
    check(entry.at("Error").at("Message").get<std::string>() == *message,
          "Source-only null error message mismatch");
  };
  check_entry(0, 1.0, "NullReferenceException",
              "Object reference not set to an instance of an object.");
  check_entry(1, 0.0, {}, {});
  check_entry(2, -1.0, "ArgumentOutOfRangeException",
              "Specified argument was out of the range of valid values. "
              "(Parameter 'simulationDelta')");
}

void check_revision_overflow_boundary(const Json &cases) {
  const Json *source_case = nullptr;
  for (const auto &test : cases) {
    if (test.at("Name") == "queued-return-inbound") {
      source_case = &test;
      break;
    }
  }
  check(source_case != nullptr,
        "Revision boundary requires queued-return-inbound source case");

  auto input_world = source_case->at("Arguments").at("World");
  input_world.at("Fleets").at(0).at("MissionOrderRevision") =
      std::numeric_limits<int>::max();
  const auto simulation_delta =
      number(source_case->at("Arguments").at("SimulationDelta"));
  auto expected_after = input_world;
  auto &expected_fleet = expected_after.at("Fleets").at(0);
  expected_fleet.at("Position") = {{"X", 3.0}, {"Y", 0.0}};
  expected_fleet.at("CurrentSystemId") = 1;
  expected_fleet.at("TransitPhase") =
      static_cast<int>(FleetTransitPhase::LocalArrival);
  expected_fleet.at("TransitProgress") = 0.0;
  expected_fleet.at("LocalTransitStart") = {{"X", -0.82}, {"Y", 0.0}};
  expected_fleet.at("LocalTransitPosition") = {{"X", -0.82}, {"Y", 0.0}};
  expected_fleet.at("LocalTransitTarget") = {{"X", 0.0}, {"Y", 0.0}};
  expected_fleet.at("FuelRemainingLightYears") = 7.0;
  expected_after.at("Knowledge") = source_case->at("After").at("Knowledge");
  expected_after.at("CoreObservers") =
      source_case->at("After").at("CoreObservers");

  auto world = parse_world(input_world);
  InterstellarLaneNetwork lanes(world.systems);
  ExplorationSimulation simulation;
  std::optional<std::vector<ExplorationEvent>> events;
  std::optional<ErrorInfo> error;
  try {
    events = simulation.advance(
        {world.systems, world.bodies, world.civilizations, world.fleets,
         world.colonies, world.economies, world.knowledge, lanes},
        simulation_delta);
  } catch (const std::exception &caught) {
    error = classify(caught);
  }

  check(!events.has_value(),
        "Revision overflow must not expose the method-local event list");
  check(error.has_value(), "Revision overflow must fail");
  check(error->type == "OverflowError",
        "Revision overflow exception category mismatch");
  check(error->message == "Fleet mission revision space is exhausted.",
        "Revision overflow exception message mismatch");
  equal_json(encode_world(world, input_world), expected_after,
             "MissionRevisionOverflow.After");
}

void check_nonfinite_computed_geometry_boundary(const Json &cases) {
  check(!cases.empty(), "Geometry boundary requires a source world");
  const auto input_world = cases.at(0).at("Arguments").at("World");
  auto systems = std::vector<StellarSystem>{
      parse_system(input_world.at("Systems").at(0)),
      parse_system(input_world.at("Systems").at(1))};
  systems[0].position.x = std::numeric_limits<float>::max();
  systems[1].position.x = -std::numeric_limits<float>::max();
  InterstellarLaneNetwork lanes(systems);
  std::optional<ErrorInfo> error;
  try {
    (void)lanes.build();
  } catch (const std::exception &caught) {
    error = classify(caught);
  }

  check(error.has_value(), "Nonfinite computed geometry must be rejected");
  check(error->type == "ArgumentException",
        "Nonfinite geometry exception category mismatch");
  check(error->message ==
            "Interstellar lane geometry produced a nonfinite distance",
        "Nonfinite geometry exception message mismatch");
}

} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: exploration_advance_tests <fixture.json>");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture");
    const auto fixture = Json::parse(stream);
    check(fixture.at("Format") == "stellar-exploration-advance-oracle-v1",
          "Unsupported fixture format");
    const auto &boundary = fixture.at("NativeBoundary");
    check(boundary.at("NullGalaxy") ==
              "Typed native world references cannot represent null." &&
              boundary.contains("NonFiniteGeometry") &&
              boundary.contains("MissionRevisionOverflow"),
          "Native boundary metadata mismatch");
    check_source_only_null_boundaries(fixture.at("SourceOnlyNullGalaxy"));
    check_revision_overflow_boundary(fixture.at("Cases"));
    check_nonfinite_computed_geometry_boundary(fixture.at("Cases"));
    std::size_t passed = 0;
    for (const auto &test : fixture.at("Cases")) {
      run_case(test);
      ++passed;
    }
    std::cout << "exploration advance parity passed " << passed
              << " native actual-C# cases; retained 3 source-only null-galaxy "
                 "observations; verified 2 explicit native boundaries\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "exploration advance parity failed: " << error.what() << '\n';
    return 1;
  }
}
