#include <stellar/core/adaptive_research_runtime.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>
#include <variant>
#include <vector>

using json = nlohmann::json;
using namespace stellar::core;
using Writer = stellar::core::detail::AdaptiveResearchStateWriter;

namespace {

std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string sha256(std::span<const unsigned char> value) {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  DWORD object_size{}, ignored{};
  std::vector<unsigned char> object;
  std::vector<unsigned char> digest(32);
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size),
                        sizeof(object_size), &ignored, 0) < 0)
    throw std::runtime_error("Could not initialize SHA-256.");
  object.resize(object_size);
  if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0,
                       0) < 0 ||
      BCryptHashData(hash, const_cast<PUCHAR>(value.data()),
                     static_cast<ULONG>(value.size()), 0) < 0 ||
      BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()),
                       0) < 0)
    throw std::runtime_error("Could not compute SHA-256.");
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  constexpr char hex[] = "0123456789ABCDEF";
  std::string text;
  text.reserve(64);
  for (const auto byte : digest) {
    text.push_back(hex[byte >> 4]);
    text.push_back(hex[byte & 15]);
  }
  return text;
}

std::string fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(root))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      files.push_back(entry.path());
  std::sort(files.begin(), files.end(), [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::string combined;
  for (const auto &path : files) {
    combined += path.filename().string();
    combined += bytes(path);
  }
  return sha256({reinterpret_cast<const unsigned char *>(combined.data()),
                 combined.size()});
}

json optional(const std::optional<std::string> &value) {
  return value ? json(*value) : json(nullptr);
}

json numeric(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

json blocker(const ResearchBlocker &value) {
  return {{"Code", static_cast<int>(value.code)},
          {"SubjectId", optional(value.subject_id)},
          {"RequiredValue",
           value.required_value ? numeric(*value.required_value) : json(nullptr)},
          {"ActualValue",
           value.actual_value ? numeric(*value.actual_value) : json(nullptr)},
          {"Message", value.message}};
}

json runtime_event(const AdaptiveResearchRuntimeEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"CivilizationId", value.civilization_id},
          {"NodeId", optional(value.node_id)},
          {"SubjectId", optional(value.subject_id)},
          {"Message", value.message}};
}

json events(std::span<const AdaptiveResearchRuntimeEvent> values) {
  auto result = json::array();
  for (const auto &value : values)
    result.push_back(runtime_event(value));
  return result;
}

json command_result(const AdaptiveResearchCommandResult &value) {
  auto blockers = json::array();
  for (const auto &entry : value.blockers)
    blockers.push_back(blocker(entry));
  return {{"Accepted", value.accepted},
          {"Message", value.message},
          {"Events", events(value.events)},
          {"Blockers", std::move(blockers)}};
}

json state_json(const AdaptiveResearchCivilizationState &state) {
  auto nodes = json::array();
  for (const auto &node : state.node_states())
    nodes.push_back({{"NodeId", node.node_id},
                     {"Maturity", static_cast<int>(node.maturity)},
                     {"Resolution", optional(node.resolution)},
                     {"StageResearchPoints", numeric(node.stage_research_points)},
                     {"TotalResearchPoints", numeric(node.total_research_points)},
                     {"Revision", node.revision},
                     {"CountsAsEstablishedKnowledge",
                      node.counts_as_established_knowledge()}});
  auto pressures = json::array();
  for (const auto &pressure : state.pressures())
    pressures.push_back({{"Id", pressure.pressure_id},
                         {"Value", numeric(pressure.value)}});
  auto evidence = json::array();
  for (const auto &item : state.evidence_instances())
    evidence.push_back({{"EvidenceInstanceId", item.evidence_instance_id},
                        {"EvidenceTypeId", item.evidence_type_id},
                        {"Provenance", item.provenance},
                        {"Quality", numeric(item.quality)},
                        {"Confidence", numeric(item.confidence)},
                        {"ContextId", optional(item.context_id)},
                        {"Revision", item.revision}});
  auto traits = json::array();
  for (const auto &trait : state.civilization_traits())
    traits.push_back(trait);
  auto contexts = json::array();
  for (const auto &context : state.applicability_contexts())
    contexts.push_back({{"Id", context.context_id},
                        {"Traits", context.sorted_traits}});
  auto capabilities = json::array();
  for (const auto &capability : state.capabilities())
    capabilities.push_back({{"CapabilityId", capability.capability_id},
                            {"ContextId", optional(capability.context_id)}});
  auto facilities = json::array();
  for (const auto &item : state.facility_capabilities())
    facilities.push_back(item);
  auto deployments = json::array();
  for (const auto &item : state.enabled_deployment_event_ids())
    deployments.push_back(item);
  auto projects = json::array();
  for (const auto &project : state.active_projects())
    projects.push_back(
        {{"NodeId", project.node_id},
         {"Stage", static_cast<int>(project.stage)},
         {"TargetApplicabilityContextId",
          optional(project.target_applicability_context_id)},
         {"AssignedEffectiveLabs", numeric(project.assigned_effective_labs)},
         {"ReadinessEfficiency", numeric(project.readiness_efficiency)},
         {"Paused", project.paused},
         {"PauseReason", optional(project.pause_reason)},
         {"StageResearchPoints", numeric(project.stage_research_points)},
         {"TotalResearchPoints", numeric(project.total_research_points)},
         {"Revision", project.revision}});
  return {{"CivilizationId", state.civilization_id()},
          {"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"DirectedProgramStageId", state.directed_program_stage_id()},
          {"TotalEffectiveResearchLabs",
           numeric(state.total_effective_research_labs())},
          {"AssignedEffectiveLabs", numeric(state.assigned_effective_labs())},
          {"FreeEffectiveLabs", numeric(state.free_effective_labs())},
          {"Nodes", std::move(nodes)},
          {"Pressures", std::move(pressures)},
          {"Evidence", std::move(evidence)},
          {"CivilizationTraits", std::move(traits)},
          {"Contexts", std::move(contexts)},
          {"Capabilities", std::move(capabilities)},
          {"FacilityCapabilities", std::move(facilities)},
          {"DeploymentEvents", std::move(deployments)},
          {"Projects", std::move(projects)}};
}

double number(const json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::invalid_argument("Unsupported fixture number.");
}

std::optional<std::string_view> context_arg(const json &value) {
  if (value.is_null())
    return std::nullopt;
  return value.get_ref<const std::string &>();
}

void setup(AdaptiveResearchRuntime &runtime,
           AdaptiveResearchCivilizationState &state, const json &steps) {
  for (const auto &step : steps) {
    const auto op = step.at("Op").get<std::string>();
    if (op == "Labs") {
      (void)runtime.set_total_effective_research_labs(
          state, step.at("Value").get<double>());
    } else if (op == "Node") {
      Writer::set_node_state(
          state,
          {step.at("NodeId").get<std::string>(),
           static_cast<ResearchMaturity>(step.at("Maturity").get<int>()),
           step.at("Resolution").is_null()
               ? std::nullopt
               : std::optional<std::string>(
                     step.at("Resolution").get<std::string>()),
           number(step.at("StageRp")), number(step.at("TotalRp")), 0});
    } else if (op == "Project") {
      Writer::set_project(
          state,
          {step.at("NodeId").get<std::string>(),
           static_cast<ResearchMaturity>(step.at("Stage").get<int>()),
           step.at("Context").is_null()
               ? std::nullopt
               : std::optional<std::string>(
                     step.at("Context").get<std::string>()),
           number(step.at("Labs")), number(step.at("Readiness")),
           step.at("Paused").get<bool>(),
           step.at("Reason").is_null()
               ? std::nullopt
               : std::optional<std::string>(step.at("Reason").get<std::string>()),
           number(step.at("StageRp")), number(step.at("TotalRp")),
           0});
    } else if (op == "AllFacilities") {
      for (const auto &id : runtime.facilities().facility_capability_ids())
        runtime.add_facility_capability(state, id);
    } else if (op == "ReadyEngineering") {
      const auto node_id = step.at("NodeId").get<std::string>();
      const std::string context = "context:grant";
      (void)runtime.set_total_effective_research_labs(state, 100.0);
      for (const auto &id : runtime.facilities().facility_capability_ids())
        runtime.add_facility_capability(state, id);
      std::unordered_set<std::string> established;
      const auto establish = [&](const auto &self, const std::string &id) -> void {
        if (!established.insert(id).second)
          return;
        const auto &definition = runtime.catalog().get_node(id);
        for (const auto &prerequisite : definition.prerequisites.all_of)
          self(self, prerequisite);
        if (!definition.prerequisites.any_of.empty())
          self(self, definition.prerequisites.any_of.front());
        Writer::set_node_state(
            state, {id, ResearchMaturity::mature, std::nullopt, 0.0,
                    definition.project_requirements.base_research_points, 0});
      };
      const auto &node = runtime.catalog().get_node(node_id);
      for (const auto &prerequisite : node.prerequisites.all_of)
        establish(establish, prerequisite);
      if (!node.prerequisites.any_of.empty())
        establish(establish, node.prerequisites.any_of.front());
      for (const auto &trait_id : node.applicability.traits) {
        const auto &trait = runtime.applicability().get_trait(trait_id);
        if (trait.scope == ResearchApplicabilityTraitScope::civilization)
          Writer::add_civilization_trait(state, trait_id);
        else
          Writer::add_applicability_trait(state, context, trait_id);
      }
      std::unordered_set<std::string> evidence_seen;
      for (const auto &evidence_id : node.applicability.evidence_types)
        if (evidence_seen.insert(evidence_id).second)
          Writer::add_evidence(
              state, {"fixture:" + evidence_id, evidence_id,
                      "actual-source setup", 1.0, 1.0, context,
                      detail::checked_next_research_state_revision(
                          state.revision())});
      for (const auto &evidence_id :
           node.project_requirements.required_evidence)
        if (evidence_seen.insert(evidence_id).second)
          Writer::add_evidence(
              state, {"fixture:" + evidence_id, evidence_id,
                      "actual-source setup", 1.0, 1.0, context,
                      detail::checked_next_research_state_revision(
                          state.revision())});
      for (const auto &pressure : node.project_requirements.required_pressure)
        Writer::set_pressure(state, pressure.id, pressure.value);
      if (!node.project_requirements.required_pressure_any.empty()) {
        const auto &pressure =
            node.project_requirements.required_pressure_any.front();
        Writer::set_pressure(state, pressure.id, pressure.value);
      }
      std::vector<std::string> required_capabilities =
          node.capability_requirements.all_of;
      if (!node.capability_requirements.any_of.empty())
        required_capabilities.push_back(
            node.capability_requirements.any_of.front());
      for (const auto &capability_id : required_capabilities) {
        const auto *capability =
            runtime.catalog().find_capability(capability_id);
        Writer::add_capability(
            state,
            {capability_id,
             capability->scope == ResearchCapabilityScope::civilization
                 ? std::nullopt
                 : std::optional<std::string>(context)});
      }
      const double stage_work = runtime.progress_policy().get_stage_work(
          node, ResearchMaturity::engineering);
      const double stage_progress = std::max(0.0, stage_work - 1.0);
      const double total_progress =
          node.project_requirements.base_research_points - 1.0;
      Writer::set_node_state(
          state, {node_id, ResearchMaturity::engineering, std::nullopt,
                  stage_progress, total_progress, 0});
      Writer::set_project(
          state,
          {node_id, ResearchMaturity::engineering, context,
           static_cast<double>(std::max(
               node.project_requirements.minimum_labs,
               node.project_requirements.recommended_labs)),
           1.0, false, std::nullopt, stage_progress, total_progress, 0});
    } else {
      throw std::runtime_error("Unknown setup operation " + op);
    }
  }
}

struct PreparedCommand {
  std::string op;
  std::vector<std::string> text;
  std::vector<double> numbers;
  std::vector<std::string> ids;
  std::optional<std::string> context;
  bool flag{};
};

PreparedCommand prepare(std::string op, const json &args) {
  PreparedCommand command{std::move(op)};
  if (command.op == "Start") {
    command.text = {args.at(0).get<std::string>()};
    command.numbers = {number(args.at(1)), number(args.at(2))};
    if (!args.at(3).is_null()) command.context = args.at(3).get<std::string>();
  } else if (command.op == "Pause" || command.op == "AddFacility" ||
             command.op == "RemoveFacility" ||
             command.op == "CivilizationTrait") {
    command.text = {args.at(0).get<std::string>()};
  } else if (command.op == "Resume") {
    command.text = {args.at(0).get<std::string>()};
    command.numbers = {number(args.at(1)), number(args.at(2))};
  } else if (command.op == "Reallocate" || command.op == "Readiness") {
    command.text = {args.at(0).get<std::string>()};
    command.numbers = {number(args.at(1))};
  } else if (command.op == "Advance") {
    command.numbers = {number(args.at(0))};
  } else if (command.op == "Pressure") {
    command.text = {args.at(0).get<std::string>()};
    command.numbers = {number(args.at(1))};
    if (!args.at(2).is_null()) command.context = args.at(2).get<std::string>();
  } else if (command.op == "Evidence") {
    command.text = {args.at(0).get<std::string>(),
                    args.at(1).get<std::string>(),
                    args.at(2).get<std::string>()};
    command.numbers = {number(args.at(3)), number(args.at(4))};
    if (!args.at(5).is_null()) command.context = args.at(5).get<std::string>();
  } else if (command.op == "ContextTraits") {
    command.text = {args.at(0).get<std::string>()};
    command.ids = args.at(1).get<std::vector<std::string>>();
  } else if (command.op == "Capability") {
    command.text = {args.at(0).get<std::string>()};
    if (!args.at(1).is_null()) command.context = args.at(1).get<std::string>();
  } else if (command.op == "Resolve") {
    command.text = {args.at(0).get<std::string>()};
    command.flag = args.at(1).get<bool>();
  } else if (command.op == "Review") {
    command.ids = args.at(0).get<std::vector<std::string>>();
    if (!args.at(1).is_null()) command.context = args.at(1).get<std::string>();
  } else {
    throw std::runtime_error("Unknown invocation operation " + command.op);
  }
  return command;
}

using NativeResult = std::variant<std::vector<AdaptiveResearchRuntimeEvent>,
                                  AdaptiveResearchCommandResult, bool>;

NativeResult invoke(AdaptiveResearchRuntime &runtime,
                    AdaptiveResearchCivilizationState &state,
                    const PreparedCommand &command) {
  const auto context = command.context
                           ? std::optional<std::string_view>(*command.context)
                           : std::nullopt;
  if (command.op == "Start")
    return runtime.start_directed_research(state, command.text[0],
                                            command.numbers[0],
                                            command.numbers[1], context);
  if (command.op == "Pause")
    return runtime.pause_directed_research(state, command.text[0]);
  if (command.op == "Resume")
    return runtime.resume_directed_research(
        state, command.text[0], command.numbers[0], command.numbers[1]);
  if (command.op == "Reallocate")
    return runtime.reallocate_research_labs(state, command.text[0],
                                             command.numbers[0]);
  if (command.op == "Readiness")
    return runtime.set_project_readiness(state, command.text[0],
                                          command.numbers[0]);
  if (command.op == "Advance")
    return runtime.advance_projects(state, command.numbers[0]);
  if (command.op == "Pressure")
    return runtime.set_pressure(state, command.text[0], command.numbers[0],
                                context);
  if (command.op == "Evidence")
    return runtime.add_evidence(state, command.text[0], command.text[1],
                                command.text[2], command.numbers[0],
                                command.numbers[1], context);
  if (command.op == "CivilizationTrait")
    return runtime.add_civilization_trait(state, command.text[0]);
  if (command.op == "ContextTraits")
    return runtime.set_applicability_context_traits(state, command.text[0],
                                                     command.ids);
  if (command.op == "Capability")
    return runtime.add_capability(state, command.text[0], context);
  if (command.op == "AddFacility") {
    runtime.add_facility_capability(state, command.text[0]);
    return true;
  }
  if (command.op == "RemoveFacility") {
    runtime.remove_facility_capability(state, command.text[0]);
    return true;
  }
  if (command.op == "Resolve")
    return runtime.resolve_hypothesis(state, command.text[0], command.flag);
  return runtime.review_basic_science_candidates(state, command.ids, context);
}

json project_result(const NativeResult &result) {
  if (const auto *value =
          std::get_if<std::vector<AdaptiveResearchRuntimeEvent>>(&result))
    return events(*value);
  if (const auto *value = std::get_if<AdaptiveResearchCommandResult>(&result))
    return command_result(*value);
  return {{"Void", std::get<bool>(result)}};
}

std::string source_exception_name(const std::exception &error) {
  if (dynamic_cast<const std::out_of_range *>(&error) &&
      std::string_view(error.what()).starts_with("Unknown "))
    return "KeyNotFoundException";
  if (dynamic_cast<const std::out_of_range *>(&error))
    return "ArgumentOutOfRangeException";
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return "ArgumentException";
  return "RuntimeException";
}

void require_equal(const json &actual, const json &expected,
                   const std::string &where) {
  if (actual != expected)
    throw std::runtime_error(where + " mismatch\nexpected: " + expected.dump() +
                             "\nactual: " + actual.dump());
}

} // namespace

int main(int argc, char **argv) {
  try {
    static_assert(!std::is_copy_constructible_v<AdaptiveResearchRuntimeContent>);
    static_assert(std::is_move_constructible_v<AdaptiveResearchRuntimeContent>);
    static_assert(!std::is_copy_constructible_v<AdaptiveResearchRuntime>);
    static_assert(std::is_move_constructible_v<AdaptiveResearchRuntime>);
    static_assert(std::is_constructible_v<
                  AdaptiveResearchRuntime,
                  std::shared_ptr<const AdaptiveResearchRuntimeContent>>);
    if (argc != 3)
      throw std::invalid_argument(
          "Expected fixture path and canonical research directory.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root_path = std::filesystem::absolute(argv[2]);
    const auto fixture = json::parse(bytes(fixture_path));
    const auto load_fingerprint_before = fingerprint(root_path);
    auto runtime = load_adaptive_research_runtime(root_path);
    const auto load_fingerprint_after = fingerprint(root_path);
    if (load_fingerprint_before != load_fingerprint_after ||
        load_fingerprint_before !=
            fixture.at("Metadata").at("LoadFingerprintBefore").get<std::string>() ||
        load_fingerprint_after !=
            fixture.at("Metadata").at("LoadFingerprintAfter").get<std::string>())
      throw std::runtime_error("Runtime loading changed canonical input bytes.");
    auto shared_runtime = AdaptiveResearchRuntime(runtime.shared_content());
    if (std::addressof(shared_runtime.catalog()) !=
        std::addressof(runtime.catalog()))
      throw std::runtime_error("Runtime content was copied instead of shared.");
    try {
      AdaptiveResearchRuntime rejected(
          std::shared_ptr<const AdaptiveResearchRuntimeContent>{});
      (void)rejected;
      throw std::runtime_error("Null shared content was accepted.");
    } catch (const std::invalid_argument &error) {
      if (std::string_view(error.what()) !=
          "Adaptive Research runtime content cannot be null.")
        throw;
    }
    {
      auto shared = runtime.shared_content();
      auto survivor = std::make_unique<AdaptiveResearchRuntime>(shared);
      {
        AdaptiveResearchRuntime first(shared);
        auto first_state = first.create_civilization_state("civ:first-owner");
        (void)first.set_total_effective_research_labs(first_state, 1.0);
      }
      auto survivor_state =
          survivor->create_civilization_state("civ:surviving-owner");
      (void)survivor->set_total_effective_research_labs(survivor_state, 2.0);
      const auto survivor_view = survivor->build_view(survivor_state);
      if (survivor_view.civilization_id != "civ:surviving-owner")
        throw std::runtime_error(
            "Shared runtime helper failed after another owner was destroyed.");

      AdaptiveResearchRuntime move_source(shared);
      AdaptiveResearchRuntime move_constructed(std::move(move_source));
      AdaptiveResearchRuntime move_assigned(shared);
      move_assigned = std::move(move_constructed);
      auto moved_state =
          move_assigned.create_civilization_state("civ:moved-runtime");
      (void)move_assigned.set_total_effective_research_labs(moved_state, 3.0);
      const auto moved_view = move_assigned.build_view(moved_state);
      if (moved_state.total_effective_research_labs() != 3.0 ||
          moved_view.civilization_id != "civ:moved-runtime")
        throw std::runtime_error("Moved runtime helpers were invalidated.");
    }
    if (runtime.catalog().metadata().catalog_id !=
        fixture.at("CatalogId").get<std::string>())
      throw std::runtime_error("Fixture catalog ID mismatch.");

    // Public string views may point into state caches that a writer rebuilds.
    // Exercise those aliases directly before replaying the source rows.
    {
      const auto &ids = fixture.at("Ids");
      const auto simple_id = ids.at("SimpleNode").get<std::string>();
      auto alias_state = runtime.create_civilization_state("civ:alias-inputs");
      (void)runtime.set_total_effective_research_labs(alias_state, 100.0);
      for (const auto &facility_id :
           runtime.facilities().facility_capability_ids())
        runtime.add_facility_capability(alias_state, facility_id);
      Writer::set_node_state(
          alias_state,
          {simple_id, ResearchMaturity::investigable, std::nullopt, 0.0, 0.0,
           0});
      const std::string_view node_alias =
          alias_state.node_states().front().node_id;
      auto started = runtime.start_directed_research(
          alias_state, node_alias,
          runtime.catalog().get_node(simple_id).project_requirements.minimum_labs,
          60.0, "context:alias");
      if (!started.accepted || started.events.front().node_id != simple_id)
        throw std::runtime_error("Node-cache alias did not survive start.");
      std::string_view project_alias =
          alias_state.active_projects().front().node_id;
      auto paused = runtime.pause_directed_research(alias_state, project_alias);
      if (!paused.accepted || paused.events.front().node_id != simple_id)
        throw std::runtime_error("Project-cache alias did not survive pause.");
      project_alias = alias_state.active_projects().front().node_id;
      auto resumed = runtime.resume_directed_research(
          alias_state, project_alias,
          runtime.catalog().get_node(simple_id).project_requirements.minimum_labs,
          60.0);
      project_alias = alias_state.active_projects().front().node_id;
      auto reallocated = runtime.reallocate_research_labs(
          alias_state, project_alias,
          runtime.catalog().get_node(simple_id).project_requirements.minimum_labs);
      project_alias = alias_state.active_projects().front().node_id;
      auto readiness =
          runtime.set_project_readiness(alias_state, project_alias, 80.0);
      if (!resumed.accepted || !reallocated.accepted || !readiness.accepted)
        throw std::runtime_error("Project-cache alias command failed.");

      const auto pressure_id = ids.at("Pressure").get<std::string>();
      (void)runtime.set_pressure(alias_state, pressure_id, 1.0);
      const std::string_view pressure_alias =
          alias_state.pressures().front().pressure_id;
      const std::string_view context_alias = *alias_state.active_projects()
                                                  .front()
                                                  .target_applicability_context_id;
      (void)runtime.set_pressure(alias_state, pressure_alias, 2.0,
                                 context_alias);
      const auto evidence_type = ids.at("EvidenceType").get<std::string>();
      (void)runtime.add_evidence(alias_state, "alias:e1", evidence_type,
                                 "alias", 1.0, 1.0);
      const std::string_view evidence_alias =
          alias_state.evidence_instances().front().evidence_type_id;
      (void)runtime.add_evidence(alias_state, "alias:e2", evidence_alias,
                                 "alias", 1.0, 1.0);
      const auto trait_id =
          ids.at("CivilizationTrait").get<std::string>();
      (void)runtime.add_civilization_trait(alias_state, trait_id);
      const std::string_view trait_alias =
          alias_state.civilization_traits().front();
      (void)runtime.add_civilization_trait(alias_state, trait_alias);
      const std::string_view facility_alias =
          alias_state.facility_capabilities().front();
      runtime.remove_facility_capability(alias_state, facility_alias);

      const auto hypothesis_id =
          ids.at("HypothesisNode").get<std::string>();
      auto resolution_state =
          runtime.create_civilization_state("civ:alias-resolution");
      Writer::set_node_state(
          resolution_state,
          {hypothesis_id, ResearchMaturity::experimental, std::nullopt, 1.0,
           1.0, 0});
      Writer::set_project(
          resolution_state,
          {hypothesis_id, ResearchMaturity::experimental, std::nullopt, 1.0,
           1.0, true, "hypothesis_resolution_required", 1.0, 1.0, 0});
      const std::string_view resolution_alias =
          resolution_state.active_projects().front().node_id;
      auto resolved = runtime.resolve_hypothesis(resolution_state,
                                                 resolution_alias, false);
      if (!resolved.accepted || resolved.events.front().node_id != hypothesis_id)
        throw std::runtime_error(
            "Project-cache alias did not survive hypothesis resolution.");
    }

    std::size_t count = 0;
    for (const auto &record : fixture.at("Records")) {
      auto state = runtime.create_civilization_state(
          "civ:" + record.at("Name").get<std::string>());
      setup(runtime, state, record.at("Setup"));
      for (const auto &outcome : record.at("Outcomes")) {
        const auto where = record.at("Name").get<std::string>() + "/" +
                           outcome.at("Op").get<std::string>() + "/" +
                           std::to_string(count);
        require_equal(state_json(state), outcome.at("Before"), where + " before");
        const auto before_fingerprint = fingerprint(root_path);
        const auto prepared = prepare(
            outcome.at("Op").get<std::string>(), outcome.at("Args"));
        std::optional<NativeResult> owned_result;
        std::optional<std::pair<std::string, std::string>> caught_error;
        try {
          owned_result.emplace(invoke(runtime, state, prepared));
        } catch (const std::exception &failure) {
          caught_error.emplace(source_exception_name(failure), failure.what());
        }
        const json error = caught_error
                               ? json{{"Type", caught_error->first},
                                      {"Message", caught_error->second}}
                               : json(nullptr);
        const auto after_fingerprint = fingerprint(root_path);
        if (before_fingerprint != after_fingerprint ||
            before_fingerprint !=
                outcome.at("FingerprintBefore").get<std::string>() ||
            after_fingerprint !=
                outcome.at("FingerprintAfter").get<std::string>())
          throw std::runtime_error(where + " changed canonical input bytes.");
        require_equal(state_json(state), outcome.at("After"), where + " after");
        require_equal(error, outcome.at("Error"), where + " error");
        if (error.is_null()) {
          const auto result = project_result(*owned_result);
          require_equal(result, outcome.at("Result"), where + " result");
        }
        ++count;
      }
      require_equal(state_json(state), record.at("FinalState"),
                    record.at("Name").get<std::string>() + " final state");
    }
    std::cout << "Adaptive Research runtime parity passed " << count
              << " actual-source command outcomes.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Adaptive Research runtime parity failed: " << error.what()
              << "\ntype=" << typeid(error).name()
              << " kind=terminal cwd=" << std::filesystem::current_path()
              << " fixture=" << (argc > 1 ? argv[1] : "<missing>")
              << " research_root=" << (argc > 2 ? argv[2] : "<missing>")
              << "\n";
    return 1;
  }
}
