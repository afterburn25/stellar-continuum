#include <stellar/core/adaptive_research_view.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

using Json = nlohmann::json;
using namespace stellar::core;
using Writer = detail::AdaptiveResearchStateWriter;

static_assert(!std::is_constructible_v<
              AdaptiveResearchViewBuilder, AdaptiveResearchCatalog &&,
              const AdaptiveResearchEligibilityEvaluator &,
              const AdaptiveResearchProgressPolicy &>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchViewBuilder, const AdaptiveResearchCatalog &,
              AdaptiveResearchEligibilityEvaluator &&,
              const AdaptiveResearchProgressPolicy &>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchViewBuilder, const AdaptiveResearchCatalog &,
              const AdaptiveResearchEligibilityEvaluator &,
              AdaptiveResearchProgressPolicy &&>);

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

double number(const Json &value) {
  if (value.is_number()) {
    return value.get<double>();
  }
  const auto text = value.get<std::string>();
  if (text == "NaN") {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (text == "Infinity") {
    return std::numeric_limits<double>::infinity();
  }
  if (text == "-Infinity") {
    return -std::numeric_limits<double>::infinity();
  }
  throw std::invalid_argument("Invalid named number.");
}

Json encoded(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0.0 ? Json("Infinity") : Json("-Infinity");
  }
  return value;
}

std::optional<std::string> optional_string(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional(value.get<std::string>());
}

ResearchNodeRuntimeState node(const Json &value) {
  return {
      .node_id = value.at("NodeId"),
      .maturity =
          static_cast<ResearchMaturity>(value.at("Maturity").get<int>()),
      .resolution = optional_string(value.at("Resolution")),
      .stage_research_points = number(value.at("StageResearchPoints")),
      .total_research_points = number(value.at("TotalResearchPoints")),
      .revision = value.at("Revision"),
  };
}

ResearchProjectRuntimeState project(const Json &value) {
  return {
      .node_id = value.at("NodeId"),
      .stage = static_cast<ResearchMaturity>(value.at("Stage").get<int>()),
      .target_applicability_context_id =
          optional_string(value.at("TargetApplicabilityContextId")),
      .assigned_effective_labs = number(value.at("AssignedEffectiveLabs")),
      .readiness_efficiency = number(value.at("ReadinessEfficiency")),
      .paused = value.at("Paused"),
      .pause_reason = optional_string(value.at("PauseReason")),
      .stage_research_points = number(value.at("StageResearchPoints")),
      .total_research_points = number(value.at("TotalResearchPoints")),
      .revision = value.at("Revision"),
  };
}

Json node_json(const ResearchNodeRuntimeState &value) {
  return {{"NodeId", value.node_id},
          {"Maturity", static_cast<int>(value.maturity)},
          {"Resolution", value.resolution},
          {"StageResearchPoints", encoded(value.stage_research_points)},
          {"TotalResearchPoints", encoded(value.total_research_points)},
          {"Revision", value.revision},
          {"CountsAsEstablishedKnowledge",
           value.counts_as_established_knowledge()}};
}

Json project_json(const ResearchProjectRuntimeState &value) {
  return {{"NodeId", value.node_id},
          {"Stage", static_cast<int>(value.stage)},
          {"TargetApplicabilityContextId",
           value.target_applicability_context_id},
          {"AssignedEffectiveLabs", encoded(value.assigned_effective_labs)},
          {"ReadinessEfficiency", encoded(value.readiness_efficiency)},
          {"Paused", value.paused},
          {"PauseReason", value.pause_reason},
          {"StageResearchPoints", encoded(value.stage_research_points)},
          {"TotalResearchPoints", encoded(value.total_research_points)},
          {"Revision", value.revision}};
}

Json snapshot(const AdaptiveResearchCivilizationState &state) {
  Json nodes = Json::array();
  for (const auto &value : state.node_states()) {
    nodes.push_back(node_json(value));
  }
  Json pressures = Json::array();
  for (const auto &value : state.pressures()) {
    pressures.push_back(
        {{"Id", value.pressure_id}, {"Value", encoded(value.value)}});
  }
  Json projects = Json::array();
  for (const auto &value : state.active_projects()) {
    projects.push_back(project_json(value));
  }
  Json evidence = Json::array();
  for (const auto &value : state.evidence_instances()) {
    evidence.push_back({{"EvidenceInstanceId", value.evidence_instance_id},
                        {"EvidenceTypeId", value.evidence_type_id},
                        {"Provenance", value.provenance},
                        {"Quality", encoded(value.quality)},
                        {"Confidence", encoded(value.confidence)},
                        {"ContextId", value.context_id},
                        {"Revision", value.revision}});
  }
  Json capabilities = Json::array();
  for (const auto &value : state.capabilities()) {
    capabilities.push_back({{"CapabilityId", value.capability_id},
                            {"ContextId", value.context_id}});
  }
  Json contexts = Json::array();
  for (const auto &value : state.applicability_contexts()) {
    contexts.push_back(
        {{"ContextId", value.context_id}, {"Traits", value.sorted_traits}});
  }
  return {{"CivilizationId", state.civilization_id()},
          {"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"DirectedProgramStageId", state.directed_program_stage_id()},
          {"TotalEffectiveResearchLabs",
           encoded(state.total_effective_research_labs())},
          {"AssignedEffectiveLabs", encoded(state.assigned_effective_labs())},
          {"FreeEffectiveLabs", encoded(state.free_effective_labs())},
          {"NodeStates", nodes},
          {"Pressures", pressures},
          {"EvidenceInstances", evidence},
          {"CivilizationTraits", state.civilization_traits()},
          {"Capabilities", capabilities},
          {"FacilityCapabilities", state.facility_capabilities()},
          {"EnabledDeploymentEventIds", state.enabled_deployment_event_ids()},
          {"ApplicabilityContexts", contexts},
          {"ActiveProjects", projects}};
}

void equal_json(const Json &actual, const Json &expected,
                const std::string &where) {
  if (actual.is_number() && expected.is_number()) {
    if (actual.is_number_integer() && expected.is_number_integer()) {
      require(actual.get<std::int64_t>() == expected.get<std::int64_t>(),
              where + " integer differs");
    } else {
      const auto left = actual.get<double>();
      const auto right = expected.get<double>();
      const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
      require(std::abs(left - right) <= 1.0e-12 * scale,
              where + " number differs");
    }
    return;
  }
  require(actual.type() == expected.type(), where + " type differs");
  if (actual.is_array()) {
    require(actual.size() == expected.size(), where + " size differs");
    for (std::size_t index = 0; index < actual.size(); ++index) {
      equal_json(actual[index], expected[index],
                 where + "[" + std::to_string(index) + "]");
    }
    return;
  }
  if (actual.is_object()) {
    require(actual.size() == expected.size(), where + " keys differ");
    for (const auto &[key, value] : expected.items()) {
      require(actual.contains(key), where + " missing " + key);
      equal_json(actual.at(key), value, where + "." + key);
    }
    return;
  }
  require(actual == expected, where + " value differs");
}

void apply_setup(const Json &row, AdaptiveResearchCivilizationState &state) {
  const auto op = row.at("Op").get<std::string>();
  const auto &input = row.at("Input");
  if (op == "SetLabs") {
    Writer::set_total_effective_research_labs(state, number(input.at("Value")));
  } else if (op == "SetPressure") {
    Writer::set_pressure(state, input.at("Id"), number(input.at("Value")));
  } else if (op == "SetNode") {
    Writer::set_node_state(state, node(input));
  } else if (op == "SetStage") {
    Writer::set_directed_program_stage(state, input.at("Id"));
  } else if (op == "SetProject") {
    Writer::set_project(state, project(input));
  } else {
    throw std::invalid_argument("Unknown setup operation '" + op + "'.");
  }
}

Json blocker_json(const ResearchBlocker &value) {
  return {{"Code", static_cast<int>(value.code)},
          {"SubjectId", value.subject_id},
          {"RequiredValue", value.required_value.has_value()
                                ? encoded(*value.required_value)
                                : Json(nullptr)},
          {"ActualValue", value.actual_value.has_value()
                              ? encoded(*value.actual_value)
                              : Json(nullptr)},
          {"Message", value.message}};
}

Json blockers_json(const std::vector<ResearchBlocker> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(blocker_json(value));
  }
  return result;
}

Json result_json(const AdaptiveResearchView &value) {
  Json nodes = Json::array();
  for (const auto &node : value.visible_nodes) {
    nodes.push_back({{"NodeId", node.node_id},
                     {"DisplayName", node.display_name},
                     {"DomainId", node.domain_id},
                     {"SolutionFamily", node.solution_family},
                     {"State", static_cast<int>(node.state)},
                     {"IsHypothesis", node.is_hypothesis},
                     {"KnownCapabilities", node.known_capabilities},
                     {"Blockers", blockers_json(node.blockers)},
                     {"MinimumLabs", node.minimum_labs},
                     {"RecommendedLabs", node.recommended_labs},
                     {"AssignedLabs", node.assigned_labs.has_value()
                                          ? encoded(*node.assigned_labs)
                                          : Json(nullptr)},
                     {"TargetApplicabilityContextId",
                      node.target_applicability_context_id}});
  }
  Json edges = Json::array();
  for (const auto &edge : value.visible_edges) {
    edges.push_back({{"FromVisibleNodeId", edge.from_visible_node_id},
                     {"ToVisibleNodeId", edge.to_visible_node_id},
                     {"Relationship", edge.relationship}});
  }
  Json projects = Json::array();
  for (const auto &project : value.active_projects) {
    projects.push_back(
        {{"NodeId", project.node_id},
         {"Stage", static_cast<int>(project.stage)},
         {"StageProgress", encoded(project.stage_progress)},
         {"TotalProgress", encoded(project.total_progress)},
         {"AssignedEffectiveLabs", encoded(project.assigned_effective_labs)},
         {"ReadinessBand", project.readiness_band},
         {"Paused", project.paused},
         {"PauseReason", project.pause_reason},
         {"CurrentBlockers", blockers_json(project.current_blockers)},
         {"TargetApplicabilityContextId",
          project.target_applicability_context_id}});
  }
  Json pressures = Json::array();
  for (const auto &pressure : value.recognized_pressures) {
    pressures.push_back(
        {{"PressureId", pressure.pressure_id},
         {"Value", encoded(pressure.value)},
         {"VisibleHardGateTargetNodeIds",
          pressure.visible_hard_gate_target_node_ids}});
  }
  return {
      {"Revision", value.revision},
      {"CivilizationId", value.civilization_id},
      {"DirectedProgramCapacity",
       {{"StageId", value.directed_program_capacity.stage_id},
        {"MaximumDirectedPrograms",
         value.directed_program_capacity.maximum_directed_programs},
        {"LabCapacityOnly",
         value.directed_program_capacity.lab_capacity_only},
        {"ActiveProgramCount",
         value.directed_program_capacity.active_program_count},
        {"FreeEffectiveLabs",
         encoded(value.directed_program_capacity.free_effective_labs)}}},
      {"VisibleNodes", nodes},
      {"VisibleEdges", edges},
      {"ActiveProjects", projects},
      {"RecognizedPressures", pressures},
  };
}

struct Error {
  std::string type;
  std::string message;
};

template <class Function> std::optional<Error> capture(Function &&function) {
  try {
    function();
    return std::nullopt;
  } catch (const std::out_of_range &error) {
    return Error{"KeyNotFoundException", error.what()};
  } catch (const std::invalid_argument &error) {
    return Error{"ArgumentException", error.what()};
  } catch (const AdaptiveResearchProgressPolicyError &error) {
    return Error{"InvalidOperationException", error.what()};
  } catch (const std::exception &error) {
    return Error{"UnexpectedNativeException", error.what()};
  }
}

void check_error(const std::optional<Error> &actual, const Json &expected,
                 const std::string &where) {
  require(actual.has_value() == !expected.is_null(),
          where + " error presence differs");
  if (actual) {
    require(actual->type == expected.at("Type").get<std::string>(),
            where + " error type differs");
    require(actual->message == expected.at("Message").get<std::string>(),
            where + " error message differs");
  }
}

void run_case(const Json &row, const AdaptiveResearchCatalog &catalog,
              const AdaptiveResearchViewBuilder &builder) {
  const auto name = row.at("Name").get<std::string>();
  const auto &input = row.at("Input");
  const auto civilization_id =
      input.at("State").at("CivilizationId").get<std::string>();
  const auto default_context =
      input.at("DefaultContextPresent").get<bool>()
          ? std::optional(input.at("DefaultContext").get<std::string>())
          : std::nullopt;
  AdaptiveResearchCivilizationState state(
      civilization_id, catalog.metadata().starting_directed_program_stage_id);
  for (const auto &setup : input.at("Setup")) {
    apply_setup(setup, state);
  }
  equal_json(snapshot(state), input.at("State"), name + " decoded state");
  equal_json(snapshot(state), row.at("Before"), name + " before");

  AdaptiveResearchView result;
  const auto error = capture([&] {
    result = builder.build(
        state, default_context
                   ? std::optional<std::string_view>(*default_context)
                   : std::nullopt);
  });
  check_error(error, row.at("Error"), name);
  if (!error) {
    equal_json(result_json(result), row.at("Result"), name + " result");
  }
  equal_json(snapshot(state), row.at("After"), name + " after");
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: research_view_tests <fixture> <research-data>");
    std::ifstream stream(argv[1]);
    Json root;
    stream >> root;
    require(root.at("Schema") == "stellar-adaptive-research-view-v1",
            "Unexpected fixture schema.");
    require(root.at("SourceOnly").size() == 1,
            "Source-only null case must remain explicit metadata.");
    require(root.at("Cases").size() == 20,
            "Expected the complete source case matrix.");

    auto catalog = load_adaptive_research_catalog(argv[2]);
    auto applicability =
        load_adaptive_research_applicability_catalog(argv[2], catalog);
    auto facilities = load_adaptive_research_facility_catalog(argv[2], catalog);
    auto progress = load_adaptive_research_progress_policy(argv[2], catalog);
    AdaptiveResearchEligibilityEvaluator eligibility(catalog, applicability,
                                                      facilities);
    AdaptiveResearchViewBuilder builder(catalog, eligibility, progress);
    require(catalog.metadata().catalog_id ==
                root.at("CatalogId").get<std::string>(),
            "Catalog ID differs.");
    require(catalog.nodes().size() ==
                root.at("CanonicalNodeCount").get<std::size_t>(),
            "Canonical node count differs.");
    for (const auto &row : root.at("Cases")) {
      run_case(row, catalog, builder);
    }
    std::cout << "research view parity: " << root.at("Cases").size()
              << " native cases; full view and state before/after; "
              << root.at("SourceOnly").size() << " source-only null case\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "research view parity failure: " << error.what() << '\n';
    return 1;
  }
}
