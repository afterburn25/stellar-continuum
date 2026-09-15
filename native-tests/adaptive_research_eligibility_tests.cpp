#include <stellar/core/adaptive_research_eligibility.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;
using Writer = detail::AdaptiveResearchStateWriter;

namespace {

void require(const bool value, const std::string &message) {
  if (!value) {
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

Json encoded(const double value) {
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

ResearchEvidenceInstance evidence(const Json &value) {
  return {
      .evidence_instance_id = value.at("EvidenceInstanceId"),
      .evidence_type_id = value.at("EvidenceTypeId"),
      .provenance = value.at("Provenance"),
      .quality = number(value.at("Quality")),
      .confidence = number(value.at("Confidence")),
      .context_id = optional_string(value.at("ContextId")),
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

Json evidence_json(const ResearchEvidenceInstance &value) {
  return {{"EvidenceInstanceId", value.evidence_instance_id},
          {"EvidenceTypeId", value.evidence_type_id},
          {"Provenance", value.provenance},
          {"Quality", encoded(value.quality)},
          {"Confidence", encoded(value.confidence)},
          {"ContextId", value.context_id},
          {"Revision", value.revision}};
}

Json project_json(const ResearchProjectRuntimeState &value) {
  return {
      {"NodeId", value.node_id},
      {"Stage", static_cast<int>(value.stage)},
      {"TargetApplicabilityContextId", value.target_applicability_context_id},
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
  Json evidence_values = Json::array();
  for (const auto &value : state.evidence_instances()) {
    evidence_values.push_back(evidence_json(value));
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
  Json projects = Json::array();
  for (const auto &value : state.active_projects()) {
    projects.push_back(project_json(value));
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
          {"EvidenceInstances", evidence_values},
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
  } else if (op == "AddEvidence") {
    Writer::add_evidence(state, evidence(input));
  } else if (op == "RemoveEvidence") {
    Writer::remove_evidence(state, input.at("Id").get<std::string>());
  } else if (op == "AddTrait") {
    Writer::add_civilization_trait(state, input.at("Id"));
  } else if (op == "RemoveTrait") {
    Writer::remove_civilization_trait(state, input.at("Id").get<std::string>());
  } else if (op == "AddContextTrait") {
    Writer::add_applicability_trait(state, input.at("Context"),
                                    input.at("Trait"));
  } else if (op == "RemoveContextTrait") {
    Writer::remove_applicability_trait(state,
                                       input.at("Context").get<std::string>(),
                                       input.at("Trait").get<std::string>());
  } else if (op == "AddCapability") {
    Writer::add_capability(
        state, {input.at("Id"), optional_string(input.at("Context"))});
  } else if (op == "RemoveCapability") {
    Writer::remove_capability(
        state, {input.at("Id"), optional_string(input.at("Context"))});
  } else if (op == "AddFacility") {
    Writer::add_facility_capability(state, input.at("Id"));
  } else if (op == "RemoveFacility") {
    Writer::remove_facility_capability(state,
                                       input.at("Id").get<std::string>());
  } else if (op == "SetNode") {
    Writer::set_node_state(state, node(input));
  } else if (op == "RemoveNode") {
    Writer::remove_node_state(state, input.at("Id").get<std::string>());
  } else if (op == "SetStage") {
    Writer::set_directed_program_stage(state, input.at("Id"));
  } else if (op == "SetProject") {
    Writer::set_project(state, project(input));
  } else {
    throw std::invalid_argument("Unknown setup operation '" + op + "'.");
  }
}

Json result_json(const ResearchEligibilityResult &result) {
  Json blockers = Json::array();
  for (const auto &blocker : result.blockers) {
    blockers.push_back({
        {"Code", static_cast<int>(blocker.code)},
        {"SubjectId", blocker.subject_id},
        {"RequiredValue", blocker.required_value.has_value()
                              ? encoded(*blocker.required_value)
                              : Json(nullptr)},
        {"ActualValue", blocker.actual_value.has_value()
                            ? encoded(*blocker.actual_value)
                            : Json(nullptr)},
        {"Message", blocker.message},
    });
  }
  return {{"Allowed", result.allowed}, {"Blockers", blockers}};
}

struct CatalogBundle {
  AdaptiveResearchCatalog catalog;
  AdaptiveResearchApplicabilityCatalog applicability;
  AdaptiveResearchFacilityCatalog facilities;
  AdaptiveResearchEligibilityEvaluator evaluator;

  explicit CatalogBundle(const std::filesystem::path &root)
      : catalog(load_adaptive_research_catalog(root)),
        applicability(
            load_adaptive_research_applicability_catalog(root, catalog)),
        facilities(load_adaptive_research_facility_catalog(root, catalog)),
        evaluator(catalog, applicability, facilities) {}
};

std::string stage_name(const ResearchMaturity stage) {
  switch (stage) {
  case ResearchMaturity::rumored:
    return "rumored";
  case ResearchMaturity::hypothesized:
    return "hypothesized";
  case ResearchMaturity::investigable:
    return "investigable";
  case ResearchMaturity::experimental:
    return "experimental";
  case ResearchMaturity::demonstrated:
    return "demonstrated";
  case ResearchMaturity::engineering:
    return "engineering";
  case ResearchMaturity::mature:
    return "mature";
  case ResearchMaturity::archived:
    return "archived";
  }
  throw std::invalid_argument("Invalid maturity.");
}

class ScratchDirectory {
public:
  explicit ScratchDirectory(const std::filesystem::path &source) {
    const auto normalized_source =
        std::filesystem::weakly_canonical(std::filesystem::absolute(source));
    require(std::filesystem::is_directory(normalized_source),
            "Research source directory does not exist.");
    parent_ = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path() /
        "stellar-research-eligibility-scratch");
    std::filesystem::create_directories(parent_);
    const auto stamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path_ = (parent_ / ("case-" + std::to_string(stamp))).lexically_normal();
    require(path_.parent_path() == parent_,
            "Scratch directory escaped its owned parent.");
    std::error_code error;
    const auto created = std::filesystem::create_directory(path_, error);
    require(!error && created,
            "Scratch directory already exists or could not be claimed.");
    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(normalized_source)) {
        std::filesystem::copy(entry.path(), path_ / entry.path().filename(),
                              std::filesystem::copy_options::recursive);
      }
    } catch (...) {
      (void)cleanup();
      throw;
    }
  }
  ~ScratchDirectory() { (void)cleanup(); }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }
  [[nodiscard]] bool cleanup() noexcept {
    if (path_.empty()) {
      return true;
    }
    if (path_.parent_path() != parent_) {
      return false;
    }
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    if (error || std::filesystem::exists(path_, error) || error) {
      return false;
    }
    path_.clear();
    return true;
  }

private:
  std::filesystem::path parent_;
  std::filesystem::path path_;
};

void make_synthetic_catalog(const std::filesystem::path &root,
                            const Json &recipe) {
  bool found_non_public = false;
  bool found_any_capability = false;
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    if (entry.path().extension() != ".json") {
      continue;
    }
    std::ifstream stream(entry.path());
    Json document;
    stream >> document;
    if (!document.contains("nodes") || !document.at("nodes").is_array()) {
      continue;
    }
    bool changed = false;
    for (auto &node_value : document.at("nodes")) {
      const auto id = node_value.at("id").get<std::string>();
      if (id == recipe.at("NonPublicNodeId").get<std::string>()) {
        node_value["public_normal_research"] = false;
        found_non_public = true;
        changed = true;
      }
      if (id == recipe.at("AnyCapabilityNodeId").get<std::string>()) {
        node_value["capability_requirements"] = {
            {"all_of", Json::array()},
            {"any_of", recipe.at("AnyCapabilityIds")},
            {"context", "civilization"}};
        found_any_capability = true;
        changed = true;
      }
    }
    if (changed) {
      std::ofstream output(entry.path());
      output << document;
    }
  }
  require(found_non_public && found_any_capability,
          "Synthetic node mutations were not applied.");

  const auto facility_path = root / "biochemical_research_facilities.json";
  std::ifstream input(facility_path);
  Json facility;
  input >> facility;
  auto &requirement = facility.at("stage_requirements")
                          .at(recipe.at("AnyFacilityNodeId").get<std::string>())
                          .at(stage_name(static_cast<ResearchMaturity>(
                              recipe.at("AnyFacilityStage").get<int>())));
  requirement["any_of"] = recipe.at("AnyFacilityIds");
  std::ofstream output(facility_path);
  output << facility;
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
  } catch (const std::exception &error) {
    return Error{"Exception", error.what()};
  }
}

void check_error(const std::optional<Error> &actual, const Json &expected,
                 const std::string &where) {
  require(actual.has_value() == !expected.is_null(),
          where + " error presence differs");
  if (actual.has_value()) {
    require(actual->type == expected.at("Type").get<std::string>(),
            where + " error type differs");
    require(actual->message == expected.at("Message").get<std::string>(),
            where + " error message differs");
  }
}

void run_case(const Json &row, CatalogBundle &canonical,
              CatalogBundle &synthetic) {
  const auto name = row.at("Name").get<std::string>();
  const auto &input = row.at("Input");
  auto &bundle =
      input.at("CatalogVariant") == "synthetic" ? synthetic : canonical;
  const auto method = input.at("Method").get<std::string>();
  const auto node_id = input.at("NodeId").get<std::string>();
  const auto context =
      input.at("ContextPresent").get<bool>()
          ? std::optional(input.at("Context").get<std::string>())
          : std::nullopt;
  const auto requested_labs = input.at("RequestedAssignedLabs").is_null()
                                  ? std::optional<double>()
                                  : number(input.at("RequestedAssignedLabs"));
  const auto stage =
      input.at("Stage").is_null()
          ? std::optional<ResearchMaturity>()
          : static_cast<ResearchMaturity>(input.at("Stage").get<int>());
  require(method == "Scientific" || method == "ProjectStart" ||
              method == "StageFacility",
          name + " invalid method");
  require((method == "ProjectStart") == requested_labs.has_value(),
          name + " invalid requested labs");
  require((method == "StageFacility") == stage.has_value(),
          name + " invalid stage");

  AdaptiveResearchCivilizationState state(
      "civ:eligibility",
      bundle.catalog.metadata().starting_directed_program_stage_id);
  for (const auto &setup : input.at("Setup")) {
    apply_setup(setup, state);
  }
  equal_json(snapshot(state), input.at("State"), name + " decoded state");
  equal_json(snapshot(state), row.at("Before"), name + " before");

  ResearchEligibilityResult result;
  const auto error = capture([&] {
    if (method == "Scientific") {
      result = bundle.evaluator.evaluate_scientific_eligibility(
          state, node_id,
          context.has_value() ? std::optional<std::string_view>(*context)
                              : std::nullopt);
    } else if (method == "ProjectStart") {
      result = bundle.evaluator.evaluate_project_start(
          state, node_id, *requested_labs,
          context.has_value() ? std::optional<std::string_view>(*context)
                              : std::nullopt);
    } else {
      result = bundle.evaluator.evaluate_stage_facility_eligibility(
          state, node_id, *stage);
    }
  });
  check_error(error, row.at("Error"), name);
  if (!error.has_value()) {
    equal_json(result_json(result), row.at("Result"), name + " result");
  }
  equal_json(snapshot(state), row.at("After"), name + " after");
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3,
            "usage: research_eligibility_tests <fixture> <research-data>");
    std::ifstream fixture_stream(argv[1]);
    Json root;
    fixture_stream >> root;
    require(root.at("Schema") == "stellar-adaptive-research-eligibility-v1",
            "Unexpected fixture schema.");
    require(root.at("SourceOnly").size() == 6,
            "Source-only null cases must remain explicit metadata.");

    CatalogBundle canonical(argv[2]);
    require(canonical.catalog.metadata().catalog_id ==
                root.at("CatalogId").get<std::string>(),
            "Catalog ID differs.");
    require(canonical.catalog.nodes().size() ==
                root.at("CanonicalNodeCount").get<std::size_t>(),
            "Canonical node count differs.");

    ScratchDirectory scratch(argv[2]);
    make_synthetic_catalog(scratch.path(), root.at("Synthetic"));
    CatalogBundle synthetic(scratch.path());
    for (const auto &row : root.at("Cases")) {
      run_case(row, canonical, synthetic);
    }
    require(scratch.cleanup(), "Failed to clean the owned scratch directory.");

    std::cout << "research eligibility parity: " << root.at("Cases").size()
              << " native cases over " << canonical.catalog.nodes().size()
              << " canonical nodes; all 19 blocker codes; "
              << root.at("SourceOnly").size() << " source-only null cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "research eligibility parity failure: " << error.what()
              << '\n';
    return 1;
  }
}
