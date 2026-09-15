#include <stellar/core/adaptive_research_snapshot.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

namespace {
using Json = nlohmann::ordered_json;
using namespace stellar::core;
using Writer = detail::AdaptiveResearchStateWriter;

[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void require(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
double real(const Json &value) {
  if (value.is_string()) {
    const auto text = value.get<std::string>();
    if (text == "NaN")
      return std::numeric_limits<double>::quiet_NaN();
    if (text == "Infinity")
      return std::numeric_limits<double>::infinity();
    if (text == "-Infinity")
      return -std::numeric_limits<double>::infinity();
  }
  return value.get<double>();
}
Json real_json(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
std::optional<std::string> optional_string(const Json &object,
                                           const char *name) {
  const auto &value = object.at(name);
  return value.is_null() ? std::nullopt
                         : std::optional(value.get<std::string>());
}
ResearchMaturity maturity(const Json &value) {
  return static_cast<ResearchMaturity>(value.get<int>());
}

AdaptiveResearchStateSnapshot snapshot_from_fixture(const Json &j) {
  AdaptiveResearchStateSnapshot s;
  s.schema_version = j.at("SchemaVersion").get<int>();
  s.catalog_id = j.at("CatalogId").get<std::string>();
  s.civilization_id = j.at("CivilizationId").get<std::string>();
  s.directed_program_stage_id =
      j.at("DirectedProgramStageId").get<std::string>();
  s.total_effective_research_labs = real(j.at("TotalEffectiveResearchLabs"));
  for (const auto &v : j.at("Nodes"))
    s.nodes.push_back(
        {v.at("NodeId").get<std::string>(), maturity(v.at("Maturity")),
         optional_string(v, "Resolution"), real(v.at("StageResearchPoints")),
         real(v.at("TotalResearchPoints"))});
  for (auto it = j.at("Pressures").begin(); it != j.at("Pressures").end(); ++it)
    s.pressures.push_back({it.key(), real(it.value())});
  for (const auto &v : j.at("Evidence"))
    s.evidence.push_back({v.at("EvidenceInstanceId").get<std::string>(),
                          v.at("EvidenceTypeId").get<std::string>(),
                          v.at("Provenance").get<std::string>(),
                          real(v.at("Quality")), real(v.at("Confidence")),
                          optional_string(v, "ContextId")});
  s.civilization_traits =
      j.at("CivilizationTraits").get<std::vector<std::string>>();
  for (auto it = j.at("ApplicabilityContexts").begin();
       it != j.at("ApplicabilityContexts").end(); ++it)
    s.applicability_contexts.push_back(
        {it.key(), it.value().get<std::vector<std::string>>()});
  for (const auto &v : j.at("Capabilities"))
    s.capabilities.push_back({v.at("CapabilityId").get<std::string>(),
                              optional_string(v, "ContextId")});
  s.facility_capabilities =
      j.at("FacilityCapabilities").get<std::vector<std::string>>();
  s.enabled_deployment_event_ids =
      j.at("EnabledDeploymentEventIds").get<std::vector<std::string>>();
  for (const auto &v : j.at("ActiveProjects"))
    s.active_projects.push_back(
        {v.at("NodeId").get<std::string>(), maturity(v.at("Stage")),
         optional_string(v, "TargetApplicabilityContextId"),
         real(v.at("AssignedEffectiveLabs")), real(v.at("ReadinessEfficiency")),
         v.at("Paused").get<bool>(), optional_string(v, "PauseReason"),
         real(v.at("StageResearchPoints")), real(v.at("TotalResearchPoints"))});
  return s;
}

Json snapshot_json(const AdaptiveResearchStateSnapshot &s) {
  Json j = {{"SchemaVersion", s.schema_version},
            {"CatalogId", s.catalog_id},
            {"CivilizationId", s.civilization_id},
            {"DirectedProgramStageId", s.directed_program_stage_id},
            {"TotalEffectiveResearchLabs",
             real_json(s.total_effective_research_labs)}};
  j["Nodes"] = Json::array();
  for (const auto &v : s.nodes)
    j["Nodes"].push_back(
        {{"NodeId", v.node_id},
         {"Maturity", static_cast<int>(v.maturity)},
         {"Resolution", v.resolution ? Json(*v.resolution) : Json(nullptr)},
         {"StageResearchPoints", real_json(v.stage_research_points)},
         {"TotalResearchPoints", real_json(v.total_research_points)}});
  j["Pressures"] = Json::object();
  for (const auto &v : s.pressures)
    j["Pressures"][v.pressure_id] = real_json(v.value);
  j["Evidence"] = Json::array();
  for (const auto &v : s.evidence)
    j["Evidence"].push_back(
        {{"EvidenceInstanceId", v.evidence_instance_id},
         {"EvidenceTypeId", v.evidence_type_id},
         {"Provenance", v.provenance},
         {"Quality", real_json(v.quality)},
         {"Confidence", real_json(v.confidence)},
         {"ContextId", v.context_id ? Json(*v.context_id) : Json(nullptr)}});
  j["CivilizationTraits"] = s.civilization_traits;
  j["ApplicabilityContexts"] = Json::object();
  for (const auto &v : s.applicability_contexts)
    j["ApplicabilityContexts"][v.context_id] = v.traits;
  j["Capabilities"] = Json::array();
  for (const auto &v : s.capabilities)
    j["Capabilities"].push_back(
        {{"CapabilityId", v.capability_id},
         {"ContextId", v.context_id ? Json(*v.context_id) : Json(nullptr)}});
  j["FacilityCapabilities"] = s.facility_capabilities;
  j["EnabledDeploymentEventIds"] = s.enabled_deployment_event_ids;
  j["ActiveProjects"] = Json::array();
  for (const auto &v : s.active_projects)
    j["ActiveProjects"].push_back(
        {{"NodeId", v.node_id},
         {"Stage", static_cast<int>(v.stage)},
         {"TargetApplicabilityContextId",
          v.target_applicability_context_id
              ? Json(*v.target_applicability_context_id)
              : Json(nullptr)},
         {"AssignedEffectiveLabs", real_json(v.assigned_effective_labs)},
         {"ReadinessEfficiency", real_json(v.readiness_efficiency)},
         {"Paused", v.paused},
         {"PauseReason",
          v.pause_reason ? Json(*v.pause_reason) : Json(nullptr)},
         {"StageResearchPoints", real_json(v.stage_research_points)},
         {"TotalResearchPoints", real_json(v.total_research_points)}});
  return j;
}

Json state_json(const AdaptiveResearchCivilizationState &s) {
  Json j = {{"CivilizationId", s.civilization_id()},
            {"Revision", s.revision()},
            {"MaterializedViewRevision", s.materialized_view_revision()},
            {"DirectedProgramStageId", s.directed_program_stage_id()},
            {"TotalEffectiveResearchLabs",
             real_json(s.total_effective_research_labs())},
            {"AssignedEffectiveLabs", real_json(s.assigned_effective_labs())},
            {"FreeEffectiveLabs", real_json(s.free_effective_labs())}};
  j["Nodes"] = Json::array();
  for (const auto &v : s.node_states())
    j["Nodes"].push_back(
        {{"NodeId", v.node_id},
         {"Maturity", static_cast<int>(v.maturity)},
         {"Resolution", v.resolution ? Json(*v.resolution) : Json(nullptr)},
         {"StageResearchPoints", real_json(v.stage_research_points)},
         {"TotalResearchPoints", real_json(v.total_research_points)},
         {"Revision", v.revision},
         {"CountsAsEstablishedKnowledge",
          v.counts_as_established_knowledge()}});
  j["Pressures"] = Json::array();
  for (const auto &v : s.pressures())
    j["Pressures"].push_back(
        {{"Key", v.pressure_id}, {"Value", real_json(v.value)}});
  j["Evidence"] = Json::array();
  for (const auto &v : s.evidence_instances())
    j["Evidence"].push_back(
        {{"EvidenceInstanceId", v.evidence_instance_id},
         {"EvidenceTypeId", v.evidence_type_id},
         {"Provenance", v.provenance},
         {"Quality", real_json(v.quality)},
         {"Confidence", real_json(v.confidence)},
         {"ContextId", v.context_id ? Json(*v.context_id) : Json(nullptr)},
         {"Revision", v.revision}});
  j["CivilizationTraits"] = s.civilization_traits();
  j["ApplicabilityContexts"] = Json::array();
  for (const auto &v : s.applicability_contexts())
    j["ApplicabilityContexts"].push_back(
        {{"Key", v.context_id}, {"Traits", v.sorted_traits}});
  j["Capabilities"] = Json::array();
  for (const auto &v : s.capabilities())
    j["Capabilities"].push_back(
        {{"CapabilityId", v.capability_id},
         {"ContextId", v.context_id ? Json(*v.context_id) : Json(nullptr)}});
  j["FacilityCapabilities"] = s.facility_capabilities();
  j["DeploymentEvents"] = s.enabled_deployment_event_ids();
  j["Projects"] = Json::array();
  for (const auto &v : s.active_projects())
    j["Projects"].push_back(
        {{"NodeId", v.node_id},
         {"Stage", static_cast<int>(v.stage)},
         {"TargetApplicabilityContextId",
          v.target_applicability_context_id
              ? Json(*v.target_applicability_context_id)
              : Json(nullptr)},
         {"AssignedEffectiveLabs", real_json(v.assigned_effective_labs)},
         {"ReadinessEfficiency", real_json(v.readiness_efficiency)},
         {"Paused", v.paused},
         {"PauseReason",
          v.pause_reason ? Json(*v.pause_reason) : Json(nullptr)},
         {"StageResearchPoints", real_json(v.stage_research_points)},
         {"TotalResearchPoints", real_json(v.total_research_points)},
         {"Revision", v.revision}});
  j["Expertise"] = "pending-separate-gate";
  return j;
}

AdaptiveResearchCivilizationState
build_state(const AdaptiveResearchStateSnapshot &s) {
  AdaptiveResearchCivilizationState state(s.civilization_id,
                                          s.directed_program_stage_id);
  Writer::set_total_effective_research_labs(state,
                                            s.total_effective_research_labs);
  for (const auto &v : s.pressures)
    Writer::set_pressure(state, v.pressure_id, v.value);
  for (const auto &v : s.evidence)
    Writer::add_evidence(state, {v.evidence_instance_id, v.evidence_type_id,
                                 v.provenance, v.quality, v.confidence,
                                 v.context_id, state.revision() + 1});
  for (const auto &v : s.civilization_traits)
    Writer::add_civilization_trait(state, v);
  for (const auto &v : s.applicability_contexts)
    Writer::set_applicability_context_traits(state, v.context_id, v.traits);
  for (const auto &v : s.capabilities)
    Writer::add_capability(state, {v.capability_id, v.context_id});
  for (const auto &v : s.facility_capabilities)
    Writer::add_facility_capability(state, v);
  for (const auto &v : s.enabled_deployment_event_ids)
    Writer::add_enabled_deployment_event(state, v);
  for (const auto &v : s.nodes)
    Writer::set_node_state(
        state, {v.node_id, v.maturity, v.resolution, v.stage_research_points,
                v.total_research_points, state.revision() + 1});
  for (const auto &v : s.active_projects)
    Writer::set_project(state,
                        {v.node_id, v.stage, v.target_applicability_context_id,
                         v.assigned_effective_labs, v.readiness_efficiency,
                         v.paused, v.pause_reason, v.stage_research_points,
                         v.total_research_points, state.revision() + 1});
  return state;
}

struct Error {
  std::string type;
  std::string message;
};
Error classify(const std::exception &e, std::string_view operation) {
  if (dynamic_cast<const AdaptiveResearchSnapshotError *>(&e))
    return {"InvalidDataException", e.what()};
  if (dynamic_cast<const AdaptiveResearchSnapshotJsonError *>(&e)) {
    const std::string_view message = e.what();
    const bool null_collection =
        message.starts_with("Adaptive Research snapshot collection '") &&
        message.ends_with(" is null.");
    return {operation == "Serialize" ? "ArgumentException"
            : null_collection        ? "NullReferenceBoundary"
                                     : "JsonBoundary",
            e.what()};
  }
  if (dynamic_cast<const std::invalid_argument *>(&e))
    return {"ArgumentException", e.what()};
  if (dynamic_cast<const std::out_of_range *>(&e))
    return {std::string_view(e.what()).starts_with("Unknown ")
                ? "KeyNotFoundException"
                : "ArgumentOutOfRangeException",
            e.what()};
  return {"UnexpectedNativeException", e.what()};
}
bool json_boundary_source(std::string_view type) {
  return type == "JsonException";
}
void compare_state(const AdaptiveResearchCivilizationState &actual,
                   const Json &expected, const std::string &name) {
  const auto a = state_json(actual);
  if (a != expected)
    fail(name + " state mismatch\nexpected=" + expected.dump() +
         "\nactual=" + a.dump());
}

void check_error(const Error &actual, const Json &expected,
                 std::string_view operation, const std::string &name) {
  const auto source_type = expected.at("Type").get<std::string>();
  const auto source_message = expected.at("Message").get<std::string>();
  if (operation == "Deserialize" && json_boundary_source(source_type)) {
    require(actual.type == "JsonBoundary",
            name + " JSON boundary category mismatch: " + actual.type);
    return;
  }
  if (operation == "Deserialize" && source_type == "NullReferenceException") {
    require(actual.type == "NullReferenceBoundary",
            name +
                " null-collection boundary category mismatch: " + actual.type);
    return;
  }
  require(actual.type == source_type,
          name + " error category mismatch: expected " + source_type +
              ", got " + actual.type);
  if (operation != "Serialize")
    require(actual.message == source_message,
            name + " error message mismatch: expected " + source_message +
                ", got " + actual.message);
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "usage: snapshot_tests <research-root> <oracle.json>");
    const std::filesystem::path root = argv[1];
    std::ifstream input(argv[2], std::ios::binary);
    require(bool(input), "cannot open oracle fixture");
    std::ostringstream bytes;
    bytes << input.rdbuf();
    const Json fixture = Json::parse(bytes.str());
    require(fixture.at("SourceOnlyCases").size() == 1,
            "source-only boundary evidence count");
    const auto &source_only = fixture.at("SourceOnlyCases").at(0);
    require(source_only.at("Error").is_null() && source_only.at("Result")
                                                     .at("Evidence")
                                                     .at(0)
                                                     .at("Provenance")
                                                     .is_null(),
            "source-only null non-nullable string evidence");
    auto catalog = load_adaptive_research_catalog(root);
    auto applicability =
        load_adaptive_research_applicability_catalog(root, catalog);
    auto facilities = load_adaptive_research_facility_catalog(root, catalog);
    AdaptiveResearchSnapshotCodec codec(catalog, applicability, facilities);
    static_assert(!std::is_copy_constructible_v<AdaptiveResearchSnapshotCodec>);
    static_assert(
        std::is_nothrow_move_constructible_v<AdaptiveResearchSnapshotCodec>);
    static_assert(
        !std::is_constructible_v<AdaptiveResearchSnapshotCodec,
                                 AdaptiveResearchCatalog &&,
                                 const AdaptiveResearchApplicabilityCatalog &,
                                 const AdaptiveResearchFacilityCatalog &>);
    static_assert(
        !std::is_constructible_v<AdaptiveResearchSnapshotCodec,
                                 const AdaptiveResearchCatalog &,
                                 AdaptiveResearchApplicabilityCatalog &&,
                                 const AdaptiveResearchFacilityCatalog &>);
    static_assert(
        !std::is_constructible_v<AdaptiveResearchSnapshotCodec,
                                 const AdaptiveResearchCatalog &,
                                 const AdaptiveResearchApplicabilityCatalog &,
                                 AdaptiveResearchFacilityCatalog &&>);
    const auto source_only_input = source_only.at("Input").get<std::string>();
    std::optional<Error> source_only_native_error;
    try {
      (void)codec.deserialize(source_only_input);
    } catch (const std::exception &error) {
      source_only_native_error = classify(error, "Deserialize");
    }
    require(source_only_native_error.has_value() &&
                source_only_native_error->type == "JsonBoundary" &&
                source_only_native_error->message ==
                    "Adaptive Research snapshot record string 'provenance' is "
                    "null.",
            "native null non-nullable string rejection proof");
    const auto seed = snapshot_from_fixture(fixture.at("Seed"));
    auto base_state = build_state(seed);
    std::size_t replayed = 0;
    for (const auto &record : fixture.at("Records")) {
      const auto name = record.at("Name").get<std::string>();
      const auto kind = record.at("Kind").get<std::string>();
      const bool expects_error = !record.at("Error").is_null();
      std::optional<Error> error;
      if (kind == "Restore") {
        auto snapshot = snapshot_from_fixture(record.at("Input"));
        const auto before = snapshot_json(snapshot);
        std::optional<AdaptiveResearchCivilizationState> result;
        try {
          result.emplace(codec.restore(snapshot));
        } catch (const std::exception &e) {
          error = classify(e, kind);
        }
        const auto after = snapshot_json(snapshot);
        require(before == after, name + " mutated its input DTO");
        require(after == record.at("InputAfter"),
                name + " native DTO fingerprint differs from source input");
        if (result)
          compare_state(*result, record.at("Result"), name);
      } else if (kind == "Capture") {
        require(
            state_json(base_state) == record.at("Input"),
            name + " source setup differs from writer-equivalent native setup");
        const auto before = state_json(base_state);
        std::optional<AdaptiveResearchStateSnapshot> result;
        try {
          result.emplace(codec.capture(base_state));
        } catch (const std::exception &e) {
          error = classify(e, kind);
        }
        require(before == state_json(base_state), name + " mutated state");
        require(state_json(base_state) == record.at("InputAfter"),
                name + " state fingerprint differs after call");
        if (result)
          require(snapshot_json(*result) == record.at("Result"),
                  name + " snapshot mismatch");
      } else if (kind == "Serialize") {
        AdaptiveResearchCivilizationState state =
            record.contains("Setup")
                ? build_state(snapshot_from_fixture(record.at("Setup")))
                : base_state;
        // Special Serialize inputs use the writer seam, not Restore.
        require(state_json(state) == record.at("Input"),
                name + " writer-equivalent setup mismatch");
        const auto before = state_json(state);
        std::optional<std::string> result;
        try {
          result = codec.serialize(state);
        } catch (const std::exception &e) {
          error = classify(e, kind);
        }
        require(before == state_json(state), name + " mutated state");
        require(state_json(state) == record.at("InputAfter"),
                name + " state fingerprint differs after call");
        if (result) {
          const auto expected_text = record.at("Result").get<std::string>();
          require(*result == expected_text,
                  name + " deterministic JSON bytes mismatch\nexpected=" +
                      expected_text + "\nactual=" + *result);
          require(Json::parse(*result) == Json::parse(expected_text),
                  name + " semantic JSON mismatch");
        }
      } else if (kind == "Deserialize") {
        const auto text = record.at("Input").get<std::string>();
        std::optional<AdaptiveResearchCivilizationState> result;
        try {
          result.emplace(codec.deserialize(text));
        } catch (const std::exception &e) {
          error = classify(e, kind);
        }
        if (result)
          compare_state(*result, record.at("Result"), name);
      } else
        fail(name + " unknown operation " + kind);
      require(error.has_value() == expects_error,
              name + " success/error mismatch");
      if (error)
        check_error(*error, record.at("Error"), kind, name);
      ++replayed;
    }
    require(replayed == fixture.at("Records").size(),
            "not all fixture rows replayed");
    std::cout << "snapshot parity rows: " << replayed << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "snapshot fixture consumer failed: type=" << typeid(e).name()
              << " message=" << e.what()
              << " cwd=" << std::filesystem::current_path().string();
    if (argc >= 2)
      std::cerr << " research-root=" << argv[1];
    if (argc >= 3)
      std::cerr << " fixture=" << argv[2];
    std::cerr << '\n';
    return 1;
  }
}
