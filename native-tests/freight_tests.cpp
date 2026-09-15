#include <stellar/core/freight.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
void check(bool value, const std::string &message) {
  if (!value)
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
  fail("Unknown named number " + text);
}
Json encoded(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if ((actual.is_number() || actual.is_string()) &&
      (expected.is_number() || expected.is_string())) {
    const bool named = actual.is_string() || expected.is_string();
    if (named) {
      check(actual == expected, field + ": named number mismatch");
      return;
    }
    const auto a = actual.get<double>(), e = expected.get<double>();
    const auto scale = std::max({1.0, std::abs(a), std::abs(e)});
    check(std::abs(a - e) <= 1e-7 * scale, field + ": number mismatch");
    return;
  }
  check(actual.type() == expected.type(), field + ": JSON type mismatch");
  if (actual.is_array()) {
    check(actual.size() == expected.size(), field + ": array size");
    for (std::size_t i = 0; i < actual.size(); ++i)
      equal_json(actual[i], expected[i], field + "[" + std::to_string(i) + "]");
  } else if (actual.is_object()) {
    check(actual.size() == expected.size(), field + ": object size");
    for (auto it = expected.begin(); it != expected.end(); ++it) {
      check(actual.contains(it.key()), field + ": missing " + it.key());
      equal_json(actual.at(it.key()), it.value(), field + "." + it.key());
    }
  } else
    check(actual == expected, field + ": value mismatch");
}
template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
Vec2 parse_vec(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}
Json encode_vec(Vec2 value) {
  return {{"X", encoded(value.x)}, {"Y", encoded(value.y)}};
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  const auto position = parse_vec(value.at("Position"));
  result.position = {position.x, position.y,
                     optional<double>(value.at("GalacticDepthLightYears"))};
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.catalog_preset_id = optional<std::string>(value.at("CatalogPresetId"));
  if (!value.at("StellarClass").is_null())
    result.primary =
        static_cast<StellarClass>(value.at("StellarClass").get<int>());
  if (!value.at("SecondaryStellarClass").is_null())
    result.secondary =
        static_cast<StellarClass>(value.at("SecondaryStellarClass").get<int>());
  if (!value.at("TertiaryStellarClass").is_null())
    result.tertiary =
        static_cast<StellarClass>(value.at("TertiaryStellarClass").get<int>());
  result.stellar_catalog_id =
      optional<std::string>(value.at("StellarCatalogId"));
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
  result.environment = {number(environment.at("GravityG")),
                        number(environment.at("TemperatureKelvin")),
                        number(environment.at("PressureKPa")),
                        static_cast<PlanetaryAtmosphereRegime>(
                            environment.at("Atmosphere").get<int>()),
                        static_cast<PlanetarySolventRegime>(
                            environment.at("AvailableSolvent").get<int>()),
                        number(environment.at("RadiationHazard")),
                        environment.at("IsImmersedEnvironment"),
                        environment.at("HasSolidSurface")};
  result.legacy_colonization_candidate =
      value.at("LegacyColonizationCandidate");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  result.orbital_eccentricity = value.value("OrbitalEccentricity", 0.0);
  result.orbital_inclination_degrees =
      value.value("OrbitalInclinationDegrees", 0.0);
  return result;
}
Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.home_system_id = value.at("HomeSystemId");
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
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
SurfaceBuilding parse_building(const Json &value) {
  SurfaceBuilding b;
  b.id = value.at("Id");
  b.type_id = value.at("TypeId");
  b.x = static_cast<float>(number(value.at("X")));
  b.z = static_cast<float>(number(value.at("Z")));
  b.rotation_degrees = static_cast<float>(number(value.at("RotationDegrees")));
  b.industry_progress = number(value.at("IndustryProgress"));
  b.is_complete = value.at("IsComplete");
  b.is_enabled = value.at("IsEnabled");
  b.pending_upgrade_type_id =
      optional<std::string>(value.at("PendingUpgradeTypeId"));
  b.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining"));
  b.operating_priority = value.at("OperatingPriority");
  b.condition = number(value.at("Condition"));
  b.stored_power_days = number(value.at("StoredPowerDays"));
  return b;
}
Json encode_building(const SurfaceBuilding &b) {
  return {{"Id", b.id},
          {"TypeId", b.type_id},
          {"X", encoded(b.x)},
          {"Z", encoded(b.z)},
          {"RotationDegrees", encoded(b.rotation_degrees)},
          {"IndustryProgress", encoded(b.industry_progress)},
          {"IsComplete", b.is_complete},
          {"IsEnabled", b.is_enabled},
          {"PendingUpgradeTypeId", b.pending_upgrade_type_id},
          {"UpgradeDaysRemaining", encoded(b.upgrade_days_remaining)},
          {"OperatingPriority", b.operating_priority},
          {"Condition", encoded(b.condition)},
          {"StoredPowerDays", encoded(b.stored_power_days)}};
}
Colony parse_colony(const Json &value) {
  Colony c;
  c.id = value.at("Id");
  c.civilization_id = value.at("CivilizationId");
  c.system_id = value.at("SystemId");
  c.planetary_body_id = optional<int>(value.at("PlanetaryBodyId"));
  c.name = value.at("Name");
  c.kind = static_cast<SettlementKind>(value.at("Kind").get<int>());
  c.population_species_id = value.at("PopulationSpeciesId");
  c.population_millions = number(value.at("PopulationMillions"));
  c.infrastructure = number(value.at("Infrastructure"));
  c.stability = number(value.at("Stability"));
  c.stored_food_population_days_millions =
      number(value.at("StoredFoodPopulationDaysMillions"));
  c.stored_water_population_days_millions =
      number(value.at("StoredWaterPopulationDaysMillions"));
  c.stored_extracted_materials = number(value.at("StoredExtractedMaterials"));
  c.remaining_extractable_materials =
      optional<double>(value.at("RemainingExtractableMaterials"));
  c.surface_hub_level = value.at("SurfaceHubLevel");
  c.surface_hub_upgrade_days_remaining =
      number(value.at("SurfaceHubUpgradeDaysRemaining"));
  for (const auto &b : value.at("SurfaceBuildings"))
    c.surface_buildings.push_back(parse_building(b));
  return c;
}
Json encode_colony(const Colony &c) {
  Json buildings = Json::array();
  for (const auto &b : c.surface_buildings)
    buildings.push_back(encode_building(b));
  return {{"Id", c.id},
          {"CivilizationId", c.civilization_id},
          {"SystemId", c.system_id},
          {"PlanetaryBodyId", c.planetary_body_id},
          {"Name", c.name},
          {"Kind", static_cast<int>(c.kind)},
          {"PopulationSpeciesId", c.population_species_id},
          {"PopulationMillions", encoded(c.population_millions)},
          {"Infrastructure", encoded(c.infrastructure)},
          {"Stability", encoded(c.stability)},
          {"StoredFoodPopulationDaysMillions",
           encoded(c.stored_food_population_days_millions)},
          {"StoredWaterPopulationDaysMillions",
           encoded(c.stored_water_population_days_millions)},
          {"StoredExtractedMaterials", encoded(c.stored_extracted_materials)},
          {"RemainingExtractableMaterials", c.remaining_extractable_materials},
          {"SurfaceHubLevel", c.surface_hub_level},
          {"SurfaceHubUpgradeDaysRemaining",
           encoded(c.surface_hub_upgrade_days_remaining)},
          {"SurfaceBuildings", buildings}};
}
FleetState parse_fleet(const Json &v) {
  FleetState f;
  f.id = v.at("Id");
  f.civilization_id = v.at("CivilizationId");
  f.name = v.at("Name");
  f.role = static_cast<FleetRole>(v.at("Role").get<int>());
  f.design_id = optional<std::string>(v.at("DesignId"));
  f.position = parse_vec(v.at("Position"));
  f.current_system_id = optional<int>(v.at("CurrentSystemId"));
  f.destination_system_id = optional<int>(v.at("DestinationSystemId"));
  f.transit_phase =
      static_cast<FleetTransitPhase>(v.at("TransitPhase").get<int>());
  f.transit_origin_system_id = optional<int>(v.at("TransitOriginSystemId"));
  f.transit_target_system_id = optional<int>(v.at("TransitTargetSystemId"));
  f.transit_progress = number(v.at("TransitProgress"));
  f.local_transit_start = parse_vec(v.at("LocalTransitStart"));
  f.local_transit_position = parse_vec(v.at("LocalTransitPosition"));
  f.local_transit_target = parse_vec(v.at("LocalTransitTarget"));
  f.planned_route_system_ids =
      v.at("PlannedRouteSystemIds").get<std::vector<int>>();
  f.hold_requested = v.at("HoldRequested");
  f.return_to_base_requested = v.at("ReturnToBaseRequested");
  f.return_to_base_failure_reason =
      optional<std::string>(v.at("ReturnToBaseFailureReason"));
  f.mission_order_revision = v.at("MissionOrderRevision");
  f.destination_planetary_body_id =
      optional<int>(v.at("DestinationPlanetaryBodyId"));
  f.prevent_automatic_settlement = v.at("PreventAutomaticSettlement");
  f.settlement_body_id = optional<int>(v.at("SettlementBodyId"));
  f.settlement_days_completed = number(v.at("SettlementDaysCompleted"));
  f.reconnaissance_system_id = optional<int>(v.at("ReconnaissanceSystemId"));
  f.reconnaissance_days_completed = number(v.at("ReconnaissanceDaysCompleted"));
  f.freight_target_outpost_id = optional<int>(v.at("FreightTargetOutpostId"));
  f.freight_home_colony_id = optional<int>(v.at("FreightHomeColonyId"));
  f.cargo_material_capacity = number(v.at("CargoMaterialCapacity"));
  f.cargo_materials = number(v.at("CargoMaterials"));
  f.strategic_speed = number(v.at("StrategicSpeed"));
  f.maximum_leg_range_light_years = number(v.at("MaximumLegRangeLightYears"));
  f.fuel_capacity_light_years = number(v.at("FuelCapacityLightYears"));
  f.fuel_remaining_light_years = number(v.at("FuelRemainingLightYears"));
  f.sensor_range = static_cast<float>(number(v.at("SensorRange")));
  f.is_active = v.at("IsActive");
  f.embarked_population_millions = number(v.at("EmbarkedPopulationMillions"));
  f.embarked_population_species_id =
      optional<std::string>(v.at("EmbarkedPopulationSpeciesId"));
  check(v.at("Combat").is_null() && v.at("TacticalLoadout").is_null() &&
            v.at("TacticalVessel").is_null(),
        "Fixture tactical fields must be null");
  return f;
}
Json encode_fleet(const FleetState &f) {
  return {
      {"Id", f.id},
      {"CivilizationId", f.civilization_id},
      {"Name", f.name},
      {"Role", static_cast<int>(f.role)},
      {"DesignId", f.design_id},
      {"Position", encode_vec(f.position)},
      {"CurrentSystemId", f.current_system_id},
      {"DestinationSystemId", f.destination_system_id},
      {"TransitPhase", static_cast<int>(f.transit_phase)},
      {"TransitOriginSystemId", f.transit_origin_system_id},
      {"TransitTargetSystemId", f.transit_target_system_id},
      {"TransitProgress", encoded(f.transit_progress)},
      {"LocalTransitStart", encode_vec(f.local_transit_start)},
      {"LocalTransitPosition", encode_vec(f.local_transit_position)},
      {"LocalTransitTarget", encode_vec(f.local_transit_target)},
      {"PlannedRouteSystemIds", f.planned_route_system_ids},
      {"HoldRequested", f.hold_requested},
      {"ReturnToBaseRequested", f.return_to_base_requested},
      {"ReturnToBaseFailureReason", f.return_to_base_failure_reason},
      {"MissionOrderRevision", f.mission_order_revision},
      {"DestinationPlanetaryBodyId", f.destination_planetary_body_id},
      {"PreventAutomaticSettlement", f.prevent_automatic_settlement},
      {"SettlementBodyId", f.settlement_body_id},
      {"SettlementDaysCompleted", encoded(f.settlement_days_completed)},
      {"ReconnaissanceSystemId", f.reconnaissance_system_id},
      {"ReconnaissanceDaysCompleted", encoded(f.reconnaissance_days_completed)},
      {"FreightTargetOutpostId", f.freight_target_outpost_id},
      {"FreightHomeColonyId", f.freight_home_colony_id},
      {"CargoMaterialCapacity", encoded(f.cargo_material_capacity)},
      {"CargoMaterials", encoded(f.cargo_materials)},
      {"StrategicSpeed", encoded(f.strategic_speed)},
      {"MaximumLegRangeLightYears", encoded(f.maximum_leg_range_light_years)},
      {"FuelCapacityLightYears", encoded(f.fuel_capacity_light_years)},
      {"FuelRemainingLightYears", encoded(f.fuel_remaining_light_years)},
      {"SensorRange", encoded(f.sensor_range)},
      {"IsActive", f.is_active},
      {"EmbarkedPopulationMillions", encoded(f.embarked_population_millions)},
      {"EmbarkedPopulationSpeciesId", f.embarked_population_species_id},
      {"Combat", nullptr},
      {"TacticalLoadout", nullptr},
      {"TacticalVessel", nullptr}};
}
CivilizationEconomy parse_economy(const Json &v) {
  CivilizationEconomy e;
  e.civilization_id = v.at("CivilizationId");
  e.credits = number(v.at("Credits"));
  e.industry = number(v.at("Industry"));
  e.science = number(v.at("Science"));
  e.last_credits_per_second = number(v.at("LastCreditsPerSecond"));
  e.last_industry_per_second = number(v.at("LastIndustryPerSecond"));
  e.last_science_per_second = number(v.at("LastSciencePerSecond"));
  e.last_research_spending_per_day = number(v.at("LastResearchSpendingPerDay"));
  e.last_research_funding_fraction =
      number(v.at("LastResearchFundingFraction"));
  e.operating_arrears = number(v.at("OperatingArrears"));
  e.last_base_operations_funding_fraction =
      number(v.at("LastBaseOperationsFundingFraction"));
  if (!v.at("IndustryPriority").is_null())
    e.industry_priority =
        static_cast<IndustryPriority>(v.at("IndustryPriority").get<int>());
  return e;
}
Json encode_economy(const CivilizationEconomy &e) {
  return {
      {"CivilizationId", e.civilization_id},
      {"Credits", encoded(e.credits)},
      {"Industry", encoded(e.industry)},
      {"Science", encoded(e.science)},
      {"LastCreditsPerSecond", encoded(e.last_credits_per_second)},
      {"LastIndustryPerSecond", encoded(e.last_industry_per_second)},
      {"LastSciencePerSecond", encoded(e.last_science_per_second)},
      {"LastResearchSpendingPerDay", encoded(e.last_research_spending_per_day)},
      {"LastResearchFundingFraction",
       encoded(e.last_research_funding_fraction)},
      {"OperatingArrears", encoded(e.operating_arrears)},
      {"LastBaseOperationsFundingFraction",
       encoded(e.last_base_operations_funding_fraction)},
      {"IndustryPriority", e.industry_priority
                               ? Json(static_cast<int>(*e.industry_priority))
                               : Json(nullptr)}};
}
ConstructionState parse_construction(const Json &v) {
  ConstructionState s;
  s.civilization_id = v.at("CivilizationId");
  s.completed_project_ids =
      v.at("CompletedProjectIds").get<std::vector<std::string>>();
  s.active_project_id = optional<std::string>(v.at("ActiveProjectId"));
  s.active_project_progress = number(v.at("ActiveProjectProgress"));
  s.active_project_authorization_credits =
      number(v.at("ActiveProjectAuthorizationCredits"));
  for (const auto &q : v.at("QueuedProjects"))
    s.queued_projects.push_back(
        {q.at("ProjectId"), number(q.at("AuthorizationCredits"))});
  return s;
}
Json encode_construction(const ConstructionState &s) {
  Json q = Json::array();
  for (const auto &v : s.queued_projects)
    q.push_back({{"ProjectId", v.project_id},
                 {"AuthorizationCredits", encoded(v.authorization_credits)}});
  return {{"CivilizationId", s.civilization_id},
          {"CompletedProjectIds", s.completed_project_ids},
          {"ActiveProjectId", s.active_project_id},
          {"ActiveProjectProgress", encoded(s.active_project_progress)},
          {"ActiveProjectAuthorizationCredits",
           encoded(s.active_project_authorization_credits)},
          {"QueuedProjects", q}};
}

struct World {
  Json raw_systems, raw_civilizations, raw_bodies;
  std::vector<StellarSystem> systems;
  std::vector<PlanetaryBody> bodies;
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
  std::vector<Colony> colonies;
  std::vector<CivilizationEconomy> economies;
  std::vector<ConstructionState> construction;
};
World parse_world(const Json &v) {
  World w;
  w.raw_systems = v.at("Systems");
  w.raw_civilizations = v.at("Civilizations");
  w.raw_bodies = v.at("PlanetaryBodies");
  for (const auto &x : v.at("Systems"))
    w.systems.push_back(parse_system(x));
  for (const auto &x : v.at("Civilizations"))
    w.civilizations.push_back(parse_civilization(x));
  for (const auto &x : v.at("PlanetaryBodies"))
    w.bodies.push_back(parse_body(x));
  for (const auto &x : v.at("Fleets"))
    w.fleets.push_back(parse_fleet(x));
  for (const auto &x : v.at("Colonies"))
    w.colonies.push_back(parse_colony(x));
  for (const auto &x : v.at("Economies"))
    w.economies.push_back(parse_economy(x));
  for (const auto &x : v.at("ConstructionStates"))
    w.construction.push_back(parse_construction(x));
  return w;
}
Json encode_world(const World &w) {
  Json fleets = Json::array(), colonies = Json::array(),
       economies = Json::array(), construction = Json::array();
  for (const auto &v : w.fleets)
    fleets.push_back(encode_fleet(v));
  for (const auto &v : w.colonies)
    colonies.push_back(encode_colony(v));
  for (const auto &v : w.economies)
    economies.push_back(encode_economy(v));
  for (const auto &v : w.construction)
    construction.push_back(encode_construction(v));
  return {{"Systems", w.raw_systems},
          {"Civilizations", w.raw_civilizations},
          {"PlanetaryBodies", w.raw_bodies},
          {"Fleets", fleets},
          {"Colonies", colonies},
          {"Economies", economies},
          {"ConstructionStates", construction}};
}

std::string classify(const std::exception &e) {
  if (dynamic_cast<const std::out_of_range *>(&e))
    return "ArgumentOutOfRangeException";
  if (dynamic_cast<const std::invalid_argument *>(&e))
    return "ArgumentException";
  if (dynamic_cast<const std::overflow_error *>(&e))
    return "OverflowException";
  if (dynamic_cast<const std::runtime_error *>(&e))
    return "InvalidOperationException";
  return "UnexpectedNativeException";
}
struct ReachCall {
  int civilization_id{}, fleet_id{}, target_system_id{};
  InterstellarMissionKind mission{};
};
void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Transit" || kind == "Collection" || kind == "Advance" ||
            kind == "CargoRate" || kind == "PortRate" ||
            kind == "EffectiveRate",
        name + ": unknown kind " + kind);
  const auto &arguments = test.at("Arguments");
  World world = parse_world(arguments.at("World"));
  const auto operation = arguments.at("Operation");
  const bool supported = arguments.at("Reach").at("supported");
  const auto reason = arguments.at("Reach").at("reason").get<std::string>();
  const auto expected_error = test.at("Error");
  const auto expected_result = test.at("Result");
  const auto expected_after = test.at("After");
  const auto expected_reach_calls = test.at("ReachCalls");
  std::string expected_error_type, expected_error_message;
  if (!expected_error.is_null()) {
    expected_error_type = expected_error.at("Type").get<std::string>();
    expected_error_message = expected_error.at("Message").get<std::string>();
    check(expected_error_type == "ArgumentOutOfRangeException" ||
              expected_error_type == "InvalidOperationException",
          name + ": unsupported expected error " + expected_error_type);
  }
  equal_json(encode_world(world), test.at("Before"), name + ".Before");
  int civilization_id{}, fleet_id{}, target_id{};
  double simulation_days{};
  const FleetState *helper_fleet = nullptr;
  const Colony *helper_colony = nullptr;
  if (kind == "Transit") {
    civilization_id = operation.at("CivilizationId").get<int>();
    fleet_id = operation.at("FleetId").get<int>();
    target_id = operation.at("TargetSystemId").get<int>();
  } else if (kind == "Collection") {
    civilization_id = operation.at("CivilizationId").get<int>();
    fleet_id = operation.at("FleetId").get<int>();
    target_id = operation.at("OutpostId").get<int>();
  } else if (kind == "Advance") {
    simulation_days = number(operation.at("SimulationDays"));
  } else if (kind == "CargoRate") {
    helper_fleet = &world.fleets.at(0);
  } else if (kind == "PortRate") {
    helper_colony = &world.colonies.at(0);
  } else {
    helper_fleet = &world.fleets.at(0);
    helper_colony = &world.colonies.at(0);
  }
  std::vector<ReachCall> calls;
  MissionReachAssessment assessment{supported, true, reason, std::nullopt, 10};
  FreightReachAssessor assessor = [&](OperationalReachWorldView, int civ,
                                      const FleetState &fleet, int target,
                                      InterstellarMissionKind mission) {
    calls.push_back({civ, fleet.id, target, mission});
    assessment.route_system_ids =
        std::vector<int>{fleet.current_system_id.value_or(target), target};
    return assessment;
  };
  InterstellarLaneNetwork lanes(world.systems);
  FreightWorldView view{
      world.systems, world.civilizations, world.bodies,    world.construction,
      world.fleets,  world.colonies,      world.economies, lanes};
  FreightSimulation freight(assessor);
  std::optional<FreightOrderResult> order;
  std::optional<double> scalar;
  std::string error_type, error_message;
  try {
    if (kind == "Transit")
      order = freight.issue_transit_order(view, civilization_id, fleet_id,
                                          target_id);
    else if (kind == "Collection")
      order = freight.issue_collection_order(view, civilization_id, fleet_id,
                                             target_id);
    else if (kind == "Advance")
      freight.advance(view, simulation_days);
    else if (kind == "CargoRate")
      scalar = FreightSimulation::cargo_transfer_rate_per_day(*helper_fleet);
    else if (kind == "PortRate")
      scalar =
          FreightSimulation::port_transfer_capacity_per_day(*helper_colony);
    else if (kind == "EffectiveRate")
      scalar = FreightSimulation::effective_transfer_rate_per_day(
          *helper_fleet, *helper_colony);
  } catch (const std::exception &e) {
    error_type = classify(e);
    error_message = e.what();
  }
  Json actual_result = nullptr;
  if (order)
    actual_result = {{"Accepted", order->accepted},
                     {"Message", order->message}};
  else if (scalar)
    actual_result = encoded(*scalar);
  if (expected_error.is_null())
    check(error_type.empty(),
          name + ": unexpected " + error_type + " " + error_message);
  else {
    check(error_type == expected_error_type,
          name + ": error type " + error_type);
    check(error_message == expected_error_message,
          name + ": error message " + error_message);
  }
  check(actual_result == expected_result, name + ": result mismatch\n" +
                                              actual_result.dump() + "\n" +
                                              expected_result.dump());
  Json encoded_calls = Json::array();
  for (const auto &call : calls)
    encoded_calls.push_back({{"CivilizationId", call.civilization_id},
                             {"FleetId", call.fleet_id},
                             {"TargetSystemId", call.target_system_id},
                             {"MissionKind", static_cast<int>(call.mission)}});
  check(encoded_calls == expected_reach_calls, name + ": reach calls mismatch");
  const auto actual_after = encode_world(world);
  equal_json(actual_after, expected_after, name + ".After");
}
void run_native_boundaries(const Json &cases) {
  const auto find_case = [&](const std::string &name) -> const Json & {
    for (const auto &test : cases)
      if (test.at("Name") == name)
        return test;
    fail("Missing boundary seed case");
  };
  {
    World world = parse_world(
        find_case("collection-supported").at("Arguments").at("World"));
    world.fleets[0].mission_order_revision = std::numeric_limits<int>::max();
    World expected = world;
    expected.fleets[0].freight_home_colony_id = 20;
    expected.fleets[0].freight_target_outpost_id = 30;
    InterstellarLaneNetwork lanes(world.systems);
    FreightWorldView view{
        world.systems, world.civilizations, world.bodies,    world.construction,
        world.fleets,  world.colonies,      world.economies, lanes};
    FreightSimulation freight([](OperationalReachWorldView, int,
                                 const FleetState &fleet, int target,
                                 InterstellarMissionKind) {
      return MissionReachAssessment{
          true, true, "Boundary.",
          std::vector<int>{fleet.current_system_id.value_or(target), target},
          10};
    });
    bool threw = false;
    std::string message;
    try {
      static_cast<void>(freight.issue_collection_order(view, 1, 7, 30));
    } catch (const std::overflow_error &error) {
      threw = true;
      message = error.what();
    }
    check(threw, "collection revision exhaustion did not throw");
    check(message == "Fleet mission revision space is exhausted.",
          "collection revision exhaustion message");
    equal_json(encode_world(world), encode_world(expected),
               "collection revision exhaustion full state");
  }
  {
    World world = parse_world(
        find_case("advance-load-return-supported").at("Arguments").at("World"));
    world.fleets[0].mission_order_revision = std::numeric_limits<int>::max();
    World expected = world;
    expected.fleets[0].cargo_materials = 100;
    expected.colonies[1].stored_extracted_materials = 49;
    expected.fleets[0].freight_target_outpost_id.reset();
    InterstellarLaneNetwork lanes(world.systems);
    FreightWorldView view{
        world.systems, world.civilizations, world.bodies,    world.construction,
        world.fleets,  world.colonies,      world.economies, lanes};
    FreightSimulation freight([](OperationalReachWorldView, int,
                                 const FleetState &fleet, int target,
                                 InterstellarMissionKind) {
      return MissionReachAssessment{
          true, true, "Boundary.",
          std::vector<int>{fleet.current_system_id.value_or(target), target},
          10};
    });
    bool threw = false;
    std::string message;
    try {
      freight.advance(view, 1);
    } catch (const std::overflow_error &error) {
      threw = true;
      message = error.what();
    }
    check(threw, "advance revision exhaustion did not throw");
    check(message == "Fleet mission revision space is exhausted.",
          "advance revision exhaustion message");
    equal_json(encode_world(world), encode_world(expected),
               "advance revision exhaustion full state");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      fail("Expected fixture path.");
    std::ifstream input(argv[1]);
    Json root;
    input >> root;
    check(root.at("Format") == "stellar-freight-oracle-v1", "format");
    for (const auto &test : root.at("Cases"))
      run_case(test);
    run_native_boundaries(root.at("Cases"));
    std::cout << "freight parity: " << root.at("Cases").size()
              << " actual C# cases and 2 native boundaries passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
