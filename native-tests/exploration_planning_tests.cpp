#include <stellar/core/exploration_planning.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
static_assert(std::is_constructible_v<ExplorationAiMissionCoordinator,
                                      ExplorationMissionPlanner &>);
static_assert(!std::is_constructible_v<ExplorationAiMissionCoordinator,
                                       ExplorationMissionPlanner &&>);
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
  check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-9 * scale,
        field + ": number mismatch");
}
template <typename T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
Vec2 vec(const Json &value) {
  return {static_cast<float>(number(value.at("X"))),
          static_cast<float>(number(value.at("Y")))};
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.position = {vec(value.at("Position")).x, vec(value.at("Position")).y,
                     optional<double>(value.at("GalacticDepthLightYears"))};
  result.archetype =
      static_cast<StarArchetype>(value.at("Archetype").get<int>());
  result.has_habitable_world = value.at("HasHabitableWorld").get<bool>();
  result.has_anomaly = value.at("HasAnomaly").get<bool>();
  result.has_rare_resource = value.at("HasRareResource").get<bool>();
  result.has_pre_warp_civilization =
      value.at("HasPreWarpCivilization").get<bool>();
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
  result.id = value.at("Id").get<int>();
  result.system_id = value.at("SystemId").get<int>();
  result.parent_body_id = optional<int>(value.at("ParentBodyId"));
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
FleetState parse_fleet(const Json &value, const std::string &field) {
  FleetState result;
  result.id = value.at("Id").get<int>();
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional<std::string>(value.at("DesignId"));
  result.position = vec(value.at("Position"));
  result.current_system_id = optional<int>(value.at("CurrentSystemId"));
  result.destination_system_id = optional<int>(value.at("DestinationSystemId"));
  result.transit_phase =
      static_cast<FleetTransitPhase>(value.at("TransitPhase").get<int>());
  result.transit_origin_system_id =
      optional<int>(value.at("TransitOriginSystemId"));
  result.transit_target_system_id =
      optional<int>(value.at("TransitTargetSystemId"));
  result.transit_progress = number(value.at("TransitProgress"));
  result.local_transit_start = vec(value.at("LocalTransitStart"));
  result.local_transit_position = vec(value.at("LocalTransitPosition"));
  result.local_transit_target = vec(value.at("LocalTransitTarget"));
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
  result.fuel_capacity_light_years = number(value.at("FuelCapacityLightYears"));
  result.fuel_remaining_light_years =
      number(value.at("FuelRemainingLightYears"));
  result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  result.is_active = value.at("IsActive").get<bool>();
  result.embarked_population_millions =
      number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id =
      optional<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  check(value.at("Combat").is_null() && value.at("TacticalLoadout").is_null() &&
            value.at("TacticalVessel").is_null(),
        field + ": tactical fixture fields must be null");
  return result;
}
void equal_fleet(const FleetState &a, const FleetState &b,
                 const std::string &field) {
  check(
      a.id == b.id && a.civilization_id == b.civilization_id &&
          a.name == b.name && a.role == b.role && a.design_id == b.design_id &&
          a.current_system_id == b.current_system_id &&
          a.destination_system_id == b.destination_system_id &&
          a.transit_phase == b.transit_phase &&
          a.transit_origin_system_id == b.transit_origin_system_id &&
          a.transit_target_system_id == b.transit_target_system_id &&
          a.planned_route_system_ids == b.planned_route_system_ids &&
          a.hold_requested == b.hold_requested &&
          a.return_to_base_requested == b.return_to_base_requested &&
          a.return_to_base_failure_reason == b.return_to_base_failure_reason &&
          a.mission_order_revision == b.mission_order_revision &&
          a.destination_planetary_body_id == b.destination_planetary_body_id &&
          a.prevent_automatic_settlement == b.prevent_automatic_settlement &&
          a.settlement_body_id == b.settlement_body_id &&
          a.reconnaissance_system_id == b.reconnaissance_system_id &&
          a.freight_target_outpost_id == b.freight_target_outpost_id &&
          a.freight_home_colony_id == b.freight_home_colony_id &&
          a.is_active == b.is_active &&
          a.embarked_population_species_id ==
              b.embarked_population_species_id &&
          !a.combat && !a.tactical_loadout && !a.tactical_vessel,
      field + ": fleet mutation");
#define SAME_NUMBER(member)                                                    \
  equal_number(a.member, b.member, field + "." #member)
  SAME_NUMBER(position.x);
  SAME_NUMBER(position.y);
  SAME_NUMBER(transit_progress);
  SAME_NUMBER(local_transit_start.x);
  SAME_NUMBER(local_transit_start.y);
  SAME_NUMBER(local_transit_position.x);
  SAME_NUMBER(local_transit_position.y);
  SAME_NUMBER(local_transit_target.x);
  SAME_NUMBER(local_transit_target.y);
  SAME_NUMBER(settlement_days_completed);
  SAME_NUMBER(reconnaissance_days_completed);
  SAME_NUMBER(cargo_material_capacity);
  SAME_NUMBER(cargo_materials);
  SAME_NUMBER(strategic_speed);
  SAME_NUMBER(maximum_leg_range_light_years);
  SAME_NUMBER(fuel_capacity_light_years);
  SAME_NUMBER(fuel_remaining_light_years);
  SAME_NUMBER(sensor_range);
  SAME_NUMBER(embarked_population_millions);
#undef SAME_NUMBER
}

MissionReachAssessment parse_reach(const Json &value) {
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
void equal_reach(const MissionReachAssessment &actual, const Json &expected,
                 const std::string &field) {
  check(actual.is_supported == expected.at("IsSupported").get<bool>() &&
            actual.is_authoritative ==
                expected.at("IsAuthoritative").get<bool>() &&
            actual.reason == expected.at("Reason").get<std::string>() &&
            actual.route_system_ids.has_value() !=
                expected.at("RouteSystemIds").is_null(),
        field + ": reach fields");
  if (actual.route_system_ids)
    check(*actual.route_system_ids ==
              expected.at("RouteSystemIds").get<std::vector<int>>(),
          field + ".RouteSystemIds");
  equal_number(actual.route_distance_light_years,
               number(expected.at("RouteDistanceLightYears")),
               field + ".RouteDistanceLightYears");
}
void equal_candidate(const ExplorationMissionCandidate &actual,
                     const Json &expected, const std::string &field) {
  check(actual.system_id == expected.at("SystemId").get<int>(),
        field + ".SystemId");
  check(actual.catalog_name == expected.at("CatalogName").get<std::string>(),
        field + ".CatalogName");
  check(static_cast<int>(actual.survey_level) ==
            expected.at("SurveyLevel").get<int>(),
        field + ".SurveyLevel");
  check(actual.priority_band == expected.at("PriorityBand").get<int>(),
        field + ".PriorityBand");
  check(actual.reason == expected.at("Reason").get<std::string>(),
        field + ".Reason: " + actual.reason);
  equal_number(actual.survey_progress, number(expected.at("SurveyProgress")),
               field + ".SurveyProgress");
  equal_number(actual.distance_from_fleet,
               number(expected.at("DistanceFromFleet")),
               field + ".DistanceFromFleet");
  check(actual.estimated_remaining_science_survey_days.has_value() !=
            expected.at("EstimatedRemainingScienceSurveyDays").is_null(),
        field + ".Remaining presence");
  if (actual.estimated_remaining_science_survey_days)
    equal_number(*actual.estimated_remaining_science_survey_days,
                 number(expected.at("EstimatedRemainingScienceSurveyDays")),
                 field + ".Remaining");
  check(actual.survey_operational_hazard.has_value() !=
            expected.at("SurveyOperationalHazard").is_null(),
        field + ".Hazard presence");
  if (actual.survey_operational_hazard)
    check(static_cast<int>(*actual.survey_operational_hazard) ==
              expected.at("SurveyOperationalHazard").get<int>(),
          field + ".Hazard");
  equal_reach(actual.reach, expected.at("Reach"), field + ".Reach");
}

void equal_plan(const ExplorationMissionPlan &actual, const Json &expected,
                const std::string &field) {
  check(actual.fleet_id == expected.at("FleetId").get<int>() &&
            actual.fleet_name == expected.at("FleetName").get<std::string>() &&
            static_cast<int>(actual.fleet_role) ==
                expected.at("FleetRole").get<int>() &&
            actual.can_receive_orders ==
                expected.at("CanReceiveOrders").get<bool>() &&
            actual.status == expected.at("Status").get<std::string>() &&
            actual.candidates.size() == expected.at("Candidates").size(),
        field + ": plan fields");
  for (std::size_t index = 0; index < actual.candidates.size(); ++index)
    equal_candidate(actual.candidates[index], expected.at("Candidates")[index],
                    field + ".Candidates[" + std::to_string(index) + "]");
}
void equal_assessment(const ExplorationMissionOrderAssessment &actual,
                      const Json &expected, const std::string &field) {
  check(actual.accepted == expected.at("Accepted").get<bool>(),
        field + ".Accepted");
  check(actual.is_local_survey == expected.at("IsLocalSurvey").get<bool>(),
        field + ".IsLocalSurvey");
  check(actual.message == expected.at("Message").get<std::string>(),
        field + ".Message: " + actual.message);
  check(actual.candidate.has_value() != expected.at("Candidate").is_null(),
        field + ".Candidate presence");
  if (actual.candidate)
    equal_candidate(*actual.candidate, expected.at("Candidate"),
                    field + ".Candidate");
}
void equal_selection(const ExplorationAiMissionSelection &actual,
                     const Json &expected, const std::string &field) {
  check(actual.fleet_id == expected.at("FleetId").get<int>() &&
            actual.used_shared_fallback ==
                expected.at("UsedSharedFallback").get<bool>() &&
            actual.reserved_system_ids ==
                expected.at("ReservedSystemIds").get<std::vector<int>>() &&
            actual.reason == expected.at("Reason").get<std::string>() &&
            actual.candidate.has_value() != expected.at("Candidate").is_null(),
        field + ": selection fields");
  if (actual.candidate)
    equal_candidate(*actual.candidate, expected.at("Candidate"),
                    field + ".Candidate");
}
struct ErrorInfo {
  std::string type;
  std::string message;
};
struct ReachCall {
  int civilization_id{};
  int fleet_civilization_id{};
  FleetRole fleet_role{};
  InterstellarMissionKind mission_kind{};
  int target_system_id{};
};
InterstellarMissionKind expected_mission_kind(FleetRole role) {
  if (role == FleetRole::Scout)
    return InterstellarMissionKind::ScoutReconnaissance;
  if (role == FleetRole::Science)
    return InterstellarMissionKind::ScienceSurvey;
  if (role == FleetRole::Military)
    return InterstellarMissionKind::MilitaryDeployment;
  if (role == FleetRole::Logistics)
    return InterstellarMissionKind::Logistics;
  return InterstellarMissionKind::ScoutReconnaissance;
}
void equal_knowledge(const CivilizationKnowledgeState &actual,
                     const Json &expected, const std::string &field) {
  check(expected.is_array(), field + ": expected array");
  for (const auto &observer : expected) {
    const auto civilization = observer.at("CivilizationId").get<int>();
    for (const auto &survey : observer.at("Survey")) {
      const auto system = survey.at("SystemId").get<int>();
      check(static_cast<int>(actual.system_survey_level(
                civilization, system)) == survey.at("Level").get<int>(),
            field + ".Level");
      equal_number(actual.system_survey_progress(civilization, system),
                   number(survey.at("Progress")), field + ".Progress");
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: exploration_planning_tests <fixture.json>");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture");
    const auto fixture = Json::parse(stream);
    check(fixture.at("Format") == "stellar-exploration-planning-oracle-v1",
          "Unsupported fixture format");
    check(fixture.at("NativeBoundary").size() == 3,
          "Native boundary documentation mismatch");
    int passed = 0;
    for (const auto &test : fixture.at("Cases")) {
      const auto name = test.at("Name").get<std::string>();
      const auto kind = test.at("Kind").get<std::string>();
      check(kind == "Plan" || kind == "Assess" || kind == "Select",
            name + ": unknown kind " + kind);
      const auto &arguments = test.at("Arguments");
      const auto fleet_id = arguments.at("FleetId").get<int>();
      const auto destination = arguments.at("DestinationSystemId").get<int>();
      const auto maximum = arguments.at("MaximumCandidates").get<int>();
      const auto require_work = arguments.at("RequireSurveyWork").get<bool>();
      const auto fleet_argument_id = arguments.at("FleetArgumentId").get<int>();
      const auto reach_mode = arguments.at("ReachMode").get<std::string>();
      check(reach_mode == "Injected" || reach_mode == "DefaultLane",
            name + ": unsupported reach mode");
      std::vector<StellarSystem> systems;
      std::vector<PlanetaryBody> bodies;
      std::vector<FleetState> fleets;
      for (const auto &value : arguments.at("Systems"))
        systems.push_back(parse_system(value));
      for (const auto &value : arguments.at("Bodies"))
        bodies.push_back(parse_body(value));
      for (std::size_t index = 0; index < arguments.at("Fleets").size();
           ++index)
        fleets.push_back(
            parse_fleet(arguments.at("Fleets")[index], name + ".Fleets"));
      std::optional<FleetState> fleet_argument;
      if (!arguments.at("FleetArgument").is_null())
        fleet_argument =
            parse_fleet(arguments.at("FleetArgument"), name + ".FleetArgument");
      check((kind == "Select") == fleet_argument.has_value(),
            name + ": fleet argument presence");
      if (fleet_argument)
        check(fleet_argument->id == fleet_argument_id,
              name + ": fleet argument ID mismatch");
      const auto fleets_before = fleets;
      const auto fleet_argument_before = fleet_argument;
      CivilizationKnowledgeState knowledge;
      for (const auto &entry : arguments.at("Knowledge")) {
        const auto civilization = entry.at("CivilizationId").get<int>();
        const auto system = entry.at("SystemId").get<int>();
        const auto level =
            static_cast<SystemSurveyLevel>(entry.at("Level").get<int>());
        const auto progress = number(entry.at("Progress"));
        if (level == SystemSurveyLevel::detected)
          knowledge.reveal_system(civilization, system);
        else if (level == SystemSurveyLevel::partially_surveyed)
          knowledge.record_reconnaissance(civilization, system, progress);
        else if (level == SystemSurveyLevel::fully_surveyed)
          knowledge.mark_system_fully_surveyed(civilization, system);
        else
          check(level == SystemSurveyLevel::unknown,
                name + ": unsupported knowledge level");
      }
      const auto knowledge_before = knowledge;
      std::map<int, MissionReachAssessment> reaches;
      for (const auto &entry : arguments.at("Reach"))
        reaches.emplace(entry.at("SystemId").get<int>(), parse_reach(entry));
      std::vector<ReachCall> reach_calls;
      ExplorationReachAssessment reach =
          [&reaches, &reach_calls](OperationalReachWorldView, int civilization,
                                   const FleetState &fleet, int id,
                                   InterstellarMissionKind kind) {
            reach_calls.push_back(
                {civilization, fleet.civilization_id, fleet.role, kind, id});
            const auto found = reaches.find(id);
            if (found == reaches.end())
              throw std::runtime_error("Missing injected reach input.");
            return found->second;
          };
      InterstellarLaneNetwork lanes(systems);
      ExplorationPlanningWorldView world{systems, bodies,    fleets,
                                         {},      knowledge, lanes};
      ExplorationMissionPlanner planner = reach_mode == "Injected"
                                              ? ExplorationMissionPlanner(reach)
                                              : ExplorationMissionPlanner();
      std::optional<ExplorationMissionPlan> plan;
      std::optional<ExplorationMissionOrderAssessment> assessment;
      std::optional<ExplorationAiMissionSelection> selection;
      const auto expects_error = !test.at("Error").is_null();
      std::optional<ErrorInfo> wanted_error;
      if (expects_error) {
        wanted_error =
            ErrorInfo{test.at("Error").at("Type").get<std::string>(),
                      test.at("Error").at("Message").get<std::string>()};
        check(wanted_error->type == "InvalidOperationException" ||
                  wanted_error->type == "ArgumentException" ||
                  wanted_error->type == "ArgumentOutOfRangeException",
              name + ": unsupported expected error type");
      }
      std::optional<ErrorInfo> actual_error;
      try {
        if (kind == "Plan")
          plan = planner.build_plan(world, fleet_id, maximum);
        else if (kind == "Assess")
          assessment =
              planner.assess_order(world, fleet_id, destination, require_work);
        else {
          ExplorationAiMissionCoordinator coordinator(planner);
          selection = coordinator.select_mission(world, *fleet_argument);
        }
      } catch (const std::out_of_range &error) {
        actual_error = ErrorInfo{"ArgumentOutOfRangeException", error.what()};
      } catch (const std::invalid_argument &error) {
        actual_error = ErrorInfo{"ArgumentException", error.what()};
      } catch (const std::runtime_error &error) {
        actual_error = ErrorInfo{"InvalidOperationException", error.what()};
      }
      for (const auto &call : reach_calls) {
        check(call.civilization_id == call.fleet_civilization_id,
              name + ": reach civilization mismatch");
        check(call.mission_kind == expected_mission_kind(call.fleet_role),
              name + ": reach mission-kind mismatch");
        check(reaches.contains(call.target_system_id),
              name + ": undocumented injected reach target");
      }
      check(expects_error == actual_error.has_value(),
            name + ": error presence mismatch");
      if (wanted_error) {
        check(actual_error->type == wanted_error->type &&
                  actual_error->message == wanted_error->message,
              name + ": error mismatch: " + actual_error->message);
      } else if (kind == "Plan")
        equal_plan(*plan, test.at("Result"), name);
      else if (kind == "Assess")
        equal_assessment(*assessment, test.at("Result"), name);
      else
        equal_selection(*selection, test.at("Result"), name);

      check(test.at("Before") == test.at("After"),
            name + ": C# planning mutated source state");
      check(test.at("Before").at("Fleets") == arguments.at("Fleets"),
            name + ": C# before fleet snapshot mismatch");
      equal_knowledge(knowledge, test.at("Before").at("Knowledge"),
                      name + ".Before.Knowledge");
      equal_knowledge(knowledge, test.at("After").at("Knowledge"),
                      name + ".After.Knowledge");
      check(fleets.size() == fleets_before.size(),
            name + ": fleet count mutation");
      for (std::size_t index = 0; index < fleets.size(); ++index)
        equal_fleet(fleets[index], fleets_before[index],
                    name + ".Fleets[" + std::to_string(index) + "]");
      if (fleet_argument)
        equal_fleet(*fleet_argument, *fleet_argument_before,
                    name + ".FleetArgument");
      for (const auto &fleet : fleets) {
        const auto after =
            knowledge.system_survey_knowledge(fleet.civilization_id);
        const auto before =
            knowledge_before.system_survey_knowledge(fleet.civilization_id);
        check(after.size() == before.size(),
              name + ": knowledge count mutation");
        for (std::size_t index = 0; index < after.size(); ++index) {
          check(after[index].system_id == before[index].system_id &&
                    after[index].level == before[index].level,
                name + ": knowledge identity mutation");
          equal_number(after[index].progress, before[index].progress,
                       name + ": knowledge progress mutation");
        }
      }
      ++passed;
    }
    std::cout << "exploration planning parity passed " << passed << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "exploration planning parity failed: " << error.what() << '\n';
    return 1;
  }
}
