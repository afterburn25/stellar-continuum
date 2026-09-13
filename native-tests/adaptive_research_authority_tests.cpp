#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_catalog.hpp>
#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_expertise_service.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>
#include <stellar/core/adaptive_research_readiness.hpp>
#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>
#include <vector>

using json = nlohmann::json;
using namespace stellar::core;
using Writer = stellar::core::detail::AdaptiveResearchStateWriter;
using ExpertiseWriter =
    stellar::core::detail::AdaptiveResearchExpertiseStateWriter;

static_assert(!std::is_constructible_v<AdaptiveResearchReadinessCalculator,
                                       AdaptiveResearchCatalog &&,
                                       const AdaptiveResearchFacilityCatalog &,
                                       const AdaptiveResearchExpertiseCatalog &,
                                       const AdaptiveResearchProgressPolicy &>);
static_assert(!std::is_constructible_v<AdaptiveResearchReadinessCalculator,
                                       const AdaptiveResearchCatalog &,
                                       AdaptiveResearchFacilityCatalog &&,
                                       const AdaptiveResearchExpertiseCatalog &,
                                       const AdaptiveResearchProgressPolicy &>);
static_assert(!std::is_constructible_v<AdaptiveResearchReadinessCalculator,
                                       const AdaptiveResearchCatalog &,
                                       const AdaptiveResearchFacilityCatalog &,
                                       AdaptiveResearchExpertiseCatalog &&,
                                       const AdaptiveResearchProgressPolicy &>);
static_assert(!std::is_constructible_v<AdaptiveResearchReadinessCalculator,
                                       const AdaptiveResearchCatalog &,
                                       const AdaptiveResearchFacilityCatalog &,
                                       const AdaptiveResearchExpertiseCatalog &,
                                       AdaptiveResearchProgressPolicy &&>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchExpertiseService, AdaptiveResearchCatalog &&,
              const AdaptiveResearchExpertiseCatalog &,
              const AdaptiveResearchReadinessCalculator &>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchExpertiseService, const AdaptiveResearchCatalog &,
              AdaptiveResearchExpertiseCatalog &&,
              const AdaptiveResearchReadinessCalculator &>);
static_assert(!std::is_constructible_v<AdaptiveResearchExpertiseService,
                                       const AdaptiveResearchCatalog &,
                                       const AdaptiveResearchExpertiseCatalog &,
                                       AdaptiveResearchReadinessCalculator &&>);
static_assert(std::is_move_constructible_v<AdaptiveResearchAuthority>);
static_assert(std::is_move_assignable_v<AdaptiveResearchAuthority>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchAuthority>);
static_assert(!std::is_copy_assignable_v<AdaptiveResearchAuthority>);

namespace {
std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}
std::vector<std::pair<std::string, std::string>>
research_bytes(const std::filesystem::path &root) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      result.emplace_back(entry.path().filename().string(),
                          bytes(entry.path()));
    }
  }
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return left.first < right.first;
  });
  return result;
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
std::optional<std::string> optional_string(const json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<std::string>(value.get<std::string>());
}
std::optional<std::string_view>
optional_view(const std::optional<std::string> &value) {
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}
json competence(const ResearchCompetenceVector &value) {
  return {{"Theoretical", numeric(value.theoretical)},
          {"Experimental", numeric(value.experimental)},
          {"Engineering", numeric(value.engineering)}};
}
json expertise_json(const AdaptiveResearchExpertiseState &state) {
  auto fields = json::array();
  for (const auto &field : state.field_competence())
    fields.push_back({{"FieldId", field.field_id},
                      {"Current", competence(field.current)},
                      {"HistoricalPeak", competence(field.historical_peak)},
                      {"LastTheoreticalActivityYear",
                       numeric(field.last_theoretical_activity_year)},
                      {"LastExperimentalActivityYear",
                       numeric(field.last_experimental_activity_year)},
                      {"LastEngineeringActivityYear",
                       numeric(field.last_engineering_activity_year)},
                      {"Revision", field.revision}});
  auto institutions = json::array();
  for (const auto &institution : state.institutions())
    institutions.push_back(
        {{"InstitutionInstanceId", institution.institution_instance_id},
         {"InstitutionArchetypeId", institution.institution_archetype_id},
         {"ContextId", optional(institution.context_id)},
         {"TotalCount", institution.total_count},
         {"ActiveCount", institution.active_count},
         {"Revision", institution.revision},
         {"IsActive", institution.is_active()}});
  auto tacit = json::array();
  for (const auto &asset : state.tacit_assets())
    tacit.push_back(
        {{"AssetId", asset.asset_id},
         {"AssetTypeId", asset.asset_type_id},
         {"ScopeKind", static_cast<int>(asset.scope_kind)},
         {"ScopeRef", asset.scope_ref},
         {"AssimilationStage", static_cast<int>(asset.assimilation_stage)},
         {"Depth", numeric(asset.depth)},
         {"Availability", numeric(asset.availability)},
         {"TranslationContextQuality",
          numeric(asset.translation_context_quality)},
         {"TrainingContinuity", numeric(asset.training_continuity)},
         {"Provenance", asset.provenance},
         {"ContextId", optional(asset.context_id)},
         {"Revision", asset.revision}});
  return {{"Revision", state.revision()},
          {"FieldCompetence", std::move(fields)},
          {"Institutions", std::move(institutions)},
          {"TacitAssets", std::move(tacit)}};
}
json state_json(const AdaptiveResearchCivilizationState &state) {
  auto nodes = json::array();
  for (const auto &node : state.node_states())
    nodes.push_back(
        {{"NodeId", node.node_id},
         {"Maturity", static_cast<int>(node.maturity)},
         {"Resolution", optional(node.resolution)},
         {"StageResearchPoints", numeric(node.stage_research_points)},
         {"TotalResearchPoints", numeric(node.total_research_points)},
         {"Revision", node.revision},
         {"CountsAsEstablishedKnowledge",
          node.counts_as_established_knowledge()}});
  auto pressures = json::array();
  for (const auto &pressure : state.pressures())
    pressures.push_back(
        {{"Id", pressure.pressure_id}, {"Value", numeric(pressure.value)}});
  auto evidence = json::array();
  for (const auto &item : state.evidence_instances())
    evidence.push_back({{"EvidenceInstanceId", item.evidence_instance_id},
                        {"EvidenceTypeId", item.evidence_type_id},
                        {"Provenance", item.provenance},
                        {"Quality", numeric(item.quality)},
                        {"Confidence", numeric(item.confidence)},
                        {"ContextId", optional(item.context_id)},
                        {"Revision", item.revision}});
  auto contexts = json::array();
  for (const auto &context : state.applicability_contexts())
    contexts.push_back(
        {{"Id", context.context_id}, {"Traits", context.sorted_traits}});
  auto capabilities = json::array();
  for (const auto &item : state.capabilities())
    capabilities.push_back({{"CapabilityId", item.capability_id},
                            {"ContextId", optional(item.context_id)}});
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
          {"NodeStates", std::move(nodes)},
          {"Pressures", std::move(pressures)},
          {"EvidenceInstances", std::move(evidence)},
          {"CivilizationTraits", state.civilization_traits()},
          {"Capabilities", std::move(capabilities)},
          {"FacilityCapabilities", state.facility_capabilities()},
          {"EnabledDeploymentEventIds", state.enabled_deployment_event_ids()},
          {"ApplicabilityContexts", std::move(contexts)},
          {"ActiveProjects", std::move(projects)},
          {"Expertise", expertise_json(state.expertise())}};
}
json readiness_json(const ResearchReadinessBreakdown &value) {
  return {
      {"NodeId", value.node_id},
      {"Stage", static_cast<int>(value.stage)},
      {"TargetApplicabilityContextId",
       optional(value.target_applicability_context_id)},
      {"FieldCompetenceScore", numeric(value.field_competence_score)},
      {"FacilityReadinessScore", numeric(value.facility_readiness_score)},
      {"EvidenceReadinessScore", value.evidence_readiness_score
                                     ? numeric(*value.evidence_readiness_score)
                                     : json(nullptr)},
      {"TacitExpertiseScore", value.tacit_expertise_score
                                  ? numeric(*value.tacit_expertise_score)
                                  : json(nullptr)},
      {"OverallReadinessScore", numeric(value.overall_readiness_score)},
      {"RpEfficiency", numeric(value.rp_efficiency)},
      {"Explanations", value.explanations}};
}
bool float_field(std::string_view key) {
  static constexpr std::string_view names[] = {"TotalEffectiveResearchLabs",
                                               "AssignedEffectiveLabs",
                                               "FreeEffectiveLabs",
                                               "Value",
                                               "Quality",
                                               "Confidence",
                                               "StageResearchPoints",
                                               "TotalResearchPoints",
                                               "Theoretical",
                                               "Experimental",
                                               "Engineering",
                                               "LastTheoreticalActivityYear",
                                               "LastExperimentalActivityYear",
                                               "LastEngineeringActivityYear",
                                               "Depth",
                                               "Availability",
                                               "TranslationContextQuality",
                                               "TrainingContinuity",
                                               "FieldCompetenceScore",
                                               "FacilityReadinessScore",
                                               "EvidenceReadinessScore",
                                               "TacitExpertiseScore",
                                               "OverallReadinessScore",
                                               "RpEfficiency"};
  return std::ranges::find(names, key) != std::end(names);
}
void require_equal(const json &actual, const json &expected,
                   const std::string &where, std::string_view key = {}) {
  if (actual.type() != expected.type()) {
    if (!(actual.is_number() && expected.is_number()))
      throw std::runtime_error(where + " type differs: " + actual.dump() +
                               " != " + expected.dump());
  }
  if (actual.is_object()) {
    if (actual.size() != expected.size())
      throw std::runtime_error(where + " object size differs");
    for (const auto &[name, value] : expected.items()) {
      if (!actual.contains(name))
        throw std::runtime_error(where + " missing " + name);
      require_equal(actual.at(name), value, where + "." + name, name);
    }
    return;
  }
  if (actual.is_array()) {
    if (actual.size() != expected.size())
      throw std::runtime_error(where + " array size differs");
    for (std::size_t index = 0; index < actual.size(); ++index)
      require_equal(actual[index], expected[index],
                    where + "[" + std::to_string(index) + "]", key);
    return;
  }
  if (actual.is_number_float() && expected.is_number() && float_field(key)) {
    const auto left = actual.get<double>(), right = expected.get<double>();
    const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
    if (std::abs(left - right) <= 1e-10 * scale)
      return;
  } else if (actual == expected) {
    return;
  }
  throw std::runtime_error(where + " differs: " + actual.dump() +
                           " != " + expected.dump());
}
std::pair<std::string, std::string> source_error(const std::exception &error) {
  const std::string message = error.what();
  if (dynamic_cast<const std::overflow_error *>(&error))
    return {"OverflowException", message};
  if (dynamic_cast<const std::out_of_range *>(&error)) {
    if (message.starts_with("Specified argument") ||
        message.starts_with("Invalid institution counts") ||
        message.starts_with("Value must be in"))
      return {"ArgumentOutOfRangeException", message};
    return {"KeyNotFoundException", message};
  }
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return {"ArgumentException", message};
  return {"UnexpectedNativeException", message};
}

json blocker_json(const ResearchBlocker &value) {
  return {{"Code", static_cast<int>(value.code)},
          {"SubjectId", optional(value.subject_id)},
          {"RequiredValue", value.required_value
                                ? numeric(*value.required_value)
                                : json(nullptr)},
          {"ActualValue",
           value.actual_value ? numeric(*value.actual_value) : json(nullptr)},
          {"Message", value.message}};
}
json event_json(const AdaptiveResearchRuntimeEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"CivilizationId", value.civilization_id},
          {"NodeId", optional(value.node_id)},
          {"SubjectId", optional(value.subject_id)},
          {"Message", value.message}};
}
json events_json(std::span<const AdaptiveResearchRuntimeEvent> values) {
  auto result = json::array();
  for (const auto &value : values)
    result.push_back(event_json(value));
  return result;
}
json command_json(const AdaptiveResearchCommandResult &value) {
  auto blockers = json::array();
  for (const auto &entry : value.blockers)
    blockers.push_back(blocker_json(entry));
  return {{"Accepted", value.accepted},
          {"Message", value.message},
          {"Events", events_json(value.events)},
          {"Blockers", std::move(blockers)}};
}
json blockers_json(const std::vector<ResearchBlocker> &values) {
  auto result = json::array();
  for (const auto &value : values)
    result.push_back(blocker_json(value));
  return result;
}
json view_json(const AdaptiveResearchView &value) {
  auto nodes = json::array();
  for (const auto &node : value.visible_nodes)
    nodes.push_back(
        {{"NodeId", node.node_id},
         {"DisplayName", node.display_name},
         {"DomainId", node.domain_id},
         {"SolutionFamily", node.solution_family},
         {"State", static_cast<int>(node.state)},
         {"IsHypothesis", node.is_hypothesis},
         {"KnownCapabilities", node.known_capabilities},
         {"Blockers", blockers_json(node.blockers)},
         {"MinimumLabs", node.minimum_labs},
         {"RecommendedLabs", node.recommended_labs},
         {"AssignedLabs",
          node.assigned_labs ? numeric(*node.assigned_labs) : json(nullptr)},
         {"TargetApplicabilityContextId",
          optional(node.target_applicability_context_id)}});
  auto edges = json::array();
  for (const auto &edge : value.visible_edges)
    edges.push_back({{"FromVisibleNodeId", edge.from_visible_node_id},
                     {"ToVisibleNodeId", edge.to_visible_node_id},
                     {"Relationship", edge.relationship}});
  auto projects = json::array();
  for (const auto &project : value.active_projects)
    projects.push_back(
        {{"NodeId", project.node_id},
         {"Stage", static_cast<int>(project.stage)},
         {"StageProgress", numeric(project.stage_progress)},
         {"TotalProgress", numeric(project.total_progress)},
         {"AssignedEffectiveLabs", numeric(project.assigned_effective_labs)},
         {"ReadinessBand", project.readiness_band},
         {"Paused", project.paused},
         {"PauseReason", optional(project.pause_reason)},
         {"CurrentBlockers", blockers_json(project.current_blockers)},
         {"TargetApplicabilityContextId",
          optional(project.target_applicability_context_id)}});
  auto pressures = json::array();
  for (const auto &pressure : value.recognized_pressures)
    pressures.push_back({{"PressureId", pressure.pressure_id},
                         {"Value", numeric(pressure.value)},
                         {"VisibleHardGateTargetNodeIds",
                          pressure.visible_hard_gate_target_node_ids}});
  return {
      {"Revision", value.revision},
      {"CivilizationId", value.civilization_id},
      {"DirectedProgramCapacity",
       {{"StageId", value.directed_program_capacity.stage_id},
        {"MaximumDirectedPrograms",
         value.directed_program_capacity.maximum_directed_programs},
        {"LabCapacityOnly", value.directed_program_capacity.lab_capacity_only},
        {"ActiveProgramCount",
         value.directed_program_capacity.active_program_count},
        {"FreeEffectiveLabs",
         numeric(value.directed_program_capacity.free_effective_labs)}}},
      {"VisibleNodes", std::move(nodes)},
      {"VisibleEdges", std::move(edges)},
      {"ActiveProjects", std::move(projects)},
      {"RecognizedPressures", std::move(pressures)}};
}
json composition_json(const AdaptiveResearchStartingCompositionResult &value) {
  auto fields = json::array();
  for (const auto &[id, item] : value.deferred.field_competence)
    fields.push_back({{"Id", id},
                      {"Theoretical", numeric(item.theoretical)},
                      {"Experimental", numeric(item.experimental)},
                      {"Engineering", numeric(item.engineering)}});
  auto institutions = json::array();
  for (const auto &item : value.deferred.research_institutions)
    institutions.push_back(
        {{"InstitutionArchetypeId", item.institution_archetype_id},
         {"Count", item.count}});
  auto tacit = json::array();
  for (const auto &item : value.deferred.tacit_assets)
    tacit.push_back({{"AssetTypeId", item.asset_type_id},
                     {"Provenance", item.provenance},
                     {"ScopeRef", optional(item.scope_ref)}});
  return {{"State", state_json(value.state)},
          {"Deferred",
           {{"FieldCompetence", std::move(fields)},
            {"ResearchInstitutions", std::move(institutions)},
            {"TacitAssets", std::move(tacit)},
            {"SelectedFragmentIds", value.deferred.selected_fragment_ids},
            {"ReferenceProfileId", value.deferred.reference_profile_id},
            {"HistoricalNotes", optional(value.deferred.historical_notes)}}},
          {"InitialHorizonEvents", events_json(value.initial_horizon_events)}};
}
using OperationResult =
    std::variant<std::monostate, bool, ResearchReadinessBreakdown,
                 AdaptiveResearchCommandResult,
                 std::vector<AdaptiveResearchRuntimeEvent>,
                 AdaptiveResearchView>;
json operation_json(const OperationResult &result) {
  if (std::holds_alternative<std::monostate>(result))
    return nullptr;
  if (const auto *value = std::get_if<bool>(&result))
    return *value;
  if (const auto *value = std::get_if<ResearchReadinessBreakdown>(&result))
    return readiness_json(*value);
  if (const auto *value = std::get_if<AdaptiveResearchCommandResult>(&result))
    return command_json(*value);
  if (const auto *value =
          std::get_if<std::vector<AdaptiveResearchRuntimeEvent>>(&result))
    return events_json(*value);
  return view_json(std::get<AdaptiveResearchView>(result));
}
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}
void apply_setup(const json &setup, AdaptiveResearchAuthority &authority,
                 AdaptiveResearchCivilizationState &state) {
  const auto kind = setup.at("Kind").get<std::string>();
  if (kind == "ExistingProjectReadiness") {
    const auto node = setup.at("NodeId").get<std::string>();
    const auto projects = state.active_projects();
    const auto found = std::ranges::find_if(
        projects, [&](const auto &project) { return project.node_id == node; });
    if (found == projects.end())
      throw std::invalid_argument("Missing project for readiness setup.");
    auto updated = *found;
    updated.readiness_efficiency = number(setup.at("Readiness"));
    Writer::set_project(state, std::move(updated));
    return;
  }
  if (kind == "Compose") {
    state = authority
                .compose_reference_profile(
                    setup.at("CivilizationId").get<std::string>(),
                    setup.at("ProfileId").get<std::string>(),
                    setup.at("Context").get<std::string>(),
                    number(setup.at("ActivityYear")))
                .state;
    return;
  }
  if (kind == "ControlledEvidence") {
    state = authority.create_civilization_state(
        setup.at("CivilizationId").get<std::string>());
    Writer::set_total_effective_research_labs(state,
                                              number(setup.at("TotalLabs")));
    const auto node = setup.at("NodeId").get<std::string>();
    Writer::set_node_state(
        state, {node, ResearchMaturity::investigable, std::nullopt, 0, 0, 0});
    Writer::set_project(
        state,
        {node,
         static_cast<ResearchMaturity>(setup.at("ProjectStage").get<int>()),
         std::nullopt, number(setup.at("AssignedLabs")),
         number(setup.at("Readiness")), false, std::nullopt, 0, 0, 0});
    return;
  }
  if (kind == "PartialEvidenceFailure") {
    state = authority.create_civilization_state(
        setup.at("CivilizationId").get<std::string>());
    Writer::set_total_effective_research_labs(state,
                                              number(setup.at("TotalLabs")));
    Writer::set_project(state, {setup.at("ProjectNodeId").get<std::string>(),
                                ResearchMaturity::experimental, std::nullopt, 1,
                                .5, false, std::nullopt, 0, 0, 0});
    return;
  }
  if (kind == "PausedHypothesis") {
    state = authority.create_civilization_state(
        setup.at("CivilizationId").get<std::string>());
    Writer::set_total_effective_research_labs(state,
                                              number(setup.at("TotalLabs")));
    const auto hypothesis = setup.at("HypothesisNodeId").get<std::string>();
    const auto running = setup.at("RunningNodeId").get<std::string>();
    Writer::set_node_state(state, {hypothesis, ResearchMaturity::investigable,
                                   std::nullopt, 0, 0, 0});
    Writer::set_node_state(state, {running, ResearchMaturity::investigable,
                                   std::nullopt, 0, 0, 0});
    Writer::set_project(
        state, {hypothesis, ResearchMaturity::experimental, std::nullopt, 1, .5,
                true, std::string("hypothesis_resolution_required"), 0, 0, 0});
    Writer::set_project(state,
                        {running, ResearchMaturity::experimental, std::nullopt,
                         1, 1, false, std::nullopt,
                         number(setup.at("RunningWork")) - .00001, 0, 0});
    return;
  }
  throw std::invalid_argument("Unknown setup kind: " + kind);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: adaptive_research_authority_tests <fixture> <research-data>");
    std::ifstream fixture_input(argv[1]);
    if (!fixture_input)
      throw std::runtime_error("Could not open fixture.");
    json fixture;
    fixture_input >> fixture;
    const std::filesystem::path research_root(argv[2]);
    const auto source_before = research_bytes(research_root);
    auto authority = load_adaptive_research_authority(research_root);
    std::size_t profiles = 0, commands = 0;
    for (const auto &row : fixture.at("Profiles")) {
      const auto &input = row.at("Input");
      const auto civ = input.at("CivilizationId").get<std::string>();
      const auto profile = input.at("ProfileId").get<std::string>();
      const auto context = input.at("Context").get<std::string>();
      const auto year = number(input.at("ActivityYear"));
      std::optional<AdaptiveResearchStartingCompositionResult> actual;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        actual =
            authority.compose_reference_profile(civ, profile, context, year);
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      const json projected = actual ? composition_json(*actual) : json(nullptr);
      const json projected_error =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(projected, row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(projected_error, row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      ++profiles;
    }
    auto state = authority
                     .compose_reference_profile(
                         "fixture:sequence", "reference_humanlike_solar_2050",
                         "fixture:primary", 2050)
                     .state;
    for (const auto &row : fixture.at("Sequence")) {
      const auto &input = row.at("Input");
      if (input.contains("Setup"))
        apply_setup(input.at("Setup"), authority, state);
      const auto before = state_json(state);
      require_equal(before, row.at("Before"),
                    row.at("Name").get<std::string>() + ".Before");
      const auto op = input.at("Op").get<std::string>();
      const auto node = input.value("NodeId", std::string{});
      const auto id = input.value("Id", std::string{});
      const auto context = input.contains("Context")
                               ? optional_string(input.at("Context"))
                               : std::nullopt;
      const auto labs = input.contains("Labs") ? number(input.at("Labs")) : 0.0;
      const auto elapsed =
          input.contains("Elapsed") ? number(input.at("Elapsed")) : 0.0;
      const auto current =
          input.contains("Current") ? number(input.at("Current")) : 0.0;
      const auto value =
          input.contains("Value") ? number(input.at("Value")) : 0.0;
      const auto instance = input.value("InstanceId", std::string{});
      const auto archetype = input.value("ArchetypeId", std::string{});
      const auto total_count = input.value("Total", 0);
      const auto active_count = input.value("Active", 0);
      const auto type = input.value("TypeId", std::string{});
      const auto provenance = input.value("Provenance", std::string{});
      const auto quality =
          input.contains("Quality") ? number(input.at("Quality")) : 0.0;
      const auto confidence =
          input.contains("Confidence") ? number(input.at("Confidence")) : 0.0;
      const auto asset = input.value("AssetId", std::string{});
      const auto scope_ref = input.value("ScopeRef", std::string{});
      const auto scope = input.contains("Scope")
                             ? static_cast<ResearchTacitScopeKind>(
                                   input.at("Scope").get<int>())
                             : ResearchTacitScopeKind::technology_node;
      const auto assimilation =
          input.contains("Assimilation")
              ? static_cast<ResearchTacitAssimilationStage>(
                    input.at("Assimilation").get<int>())
              : ResearchTacitAssimilationStage::access;
      const auto depth =
          input.contains("Depth") ? number(input.at("Depth")) : 0.0;
      const auto availability = input.contains("Availability")
                                    ? number(input.at("Availability"))
                                    : 0.0;
      const auto translation =
          input.contains("Translation") ? number(input.at("Translation")) : 0.0;
      const auto training =
          input.contains("Training") ? number(input.at("Training")) : 0.0;
      const auto supported = input.value("Supported", false);
      std::vector<std::string> strings;
      if (input.contains("Traits"))
        strings = input.at("Traits").get<std::vector<std::string>>();
      if (input.contains("Ids"))
        strings = input.at("Ids").get<std::vector<std::string>>();
      const auto stage =
          input.contains("Stage")
              ? static_cast<ResearchMaturity>(input.at("Stage").get<int>())
              : ResearchMaturity::investigable;
      OperationResult result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        if (op == "BuildView")
          result = authority.build_view(state);
        else if (op == "Readiness")
          result = authority.get_project_readiness(state, node, stage, labs,
                                                   optional_view(context));
        else if (op == "Start")
          result = authority.start_directed_research(state, node, labs,
                                                     optional_view(context));
        else if (op == "Pause")
          result = authority.pause_directed_research(state, node);
        else if (op == "Resume")
          result = authority.resume_directed_research(state, node, labs);
        else if (op == "Reallocate")
          result = authority.reallocate_research_labs(state, node, labs);
        else if (op == "Resolve")
          result = authority.resolve_hypothesis(state, node, supported);
        else if (op == "Pressure")
          result =
              authority.set_pressure(state, id, value, optional_view(context));
        else if (op == "Trait")
          result = authority.add_civilization_trait(state, id);
        else if (op == "ContextTraits")
          result =
              authority.set_applicability_context_traits(state, id, strings);
        else if (op == "Capability")
          result = authority.add_capability(state, id, optional_view(context));
        else if (op == "Review")
          result = authority.review_basic_science_candidates(
              state, strings, optional_view(context));
        else if (op == "Advance")
          result = authority.advance_projects(state, elapsed, current);
        else if (op == "Institution") {
          authority.set_research_institution(state, instance, archetype,
                                             total_count, active_count,
                                             optional_view(context));
          result = true;
        } else if (op == "Evidence")
          result =
              authority.add_evidence(state, instance, type, provenance, quality,
                                     confidence, optional_view(context));
        else if (op == "Tacit") {
          authority.set_tacit_asset(state, asset, type, scope, scope_ref,
                                    assimilation, depth, availability,
                                    translation, training, provenance,
                                    optional_view(context));
          result = true;
        } else if (op == "Atrophy") {
          authority.apply_competence_atrophy(state, current, elapsed);
          result = true;
        } else
          throw std::invalid_argument("Unknown operation: " + op);
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      const auto projected = operation_json(result);
      const json projected_error =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(projected, row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(projected_error, row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      require_equal(state_json(state), row.at("After"),
                    row.at("Name").get<std::string>() + ".After");
      ++commands;
    }
    // Stable-storage move proof composes and invokes through both move
    // operations.
    auto move_source =
        load_adaptive_research_authority(std::filesystem::path(argv[2]));
    const auto *borrowed_catalog = &move_source.catalog();
    auto moved = std::move(move_source);
    require(&moved.catalog() == borrowed_catalog,
            "move construction changed stable catalog address");
    auto moved_state = moved
                           .compose_reference_profile(
                               "fixture:move", "reference_humanlike_solar_2050",
                               "fixture:move-context", 2050)
                           .state;
    auto readiness = moved.get_project_readiness(moved_state, "fusion_power",
                                                 ResearchMaturity::experimental,
                                                 4, "fixture:move-context");
    require(std::isfinite(readiness.overall_readiness_score),
            "move readiness failed");
    auto assigned =
        load_adaptive_research_authority(std::filesystem::path(argv[2]));
    assigned = std::move(moved);
    require(&assigned.catalog() == borrowed_catalog,
            "move assignment changed stable catalog address");
    auto started = assigned.start_directed_research(moved_state, "fusion_power",
                                                    4, "fixture:move-context");
    require(started.accepted, "move-assigned authority start failed");
    (void)assigned.advance_projects(moved_state, .01, 2050.01);
    // Every borrowed input is owned before the first mutation. These views all
    // point into records that the invoked operation replaces or erases.
    auto alias_state = assigned.create_civilization_state("fixture:alias");
    Writer::set_total_effective_research_labs(alias_state, 10);
    const auto &alias_node = assigned.catalog().nodes().front();
    Writer::set_node_state(
        alias_state,
        {alias_node.id, ResearchMaturity::investigable, std::nullopt, 0, 0, 0});
    Writer::set_project(alias_state,
                        {alias_node.id, ResearchMaturity::experimental,
                         std::nullopt, 1, .1, false, std::nullopt, 0, 0, 0});
    const auto alias_evidence_type =
        fixture.at("EvidenceControls").at("EvidenceTypeId").get<std::string>();
    Writer::add_evidence(alias_state, {"alias:evidence", alias_evidence_type,
                                       "alias:provenance", .2, .3,
                                       std::string("alias:context"), 0});
    const auto &evidence_alias = alias_state.evidence_instances().front();
    const auto evidence_events = assigned.add_evidence(
        alias_state, evidence_alias.evidence_instance_id,
        evidence_alias.evidence_type_id, evidence_alias.provenance, .8, .9,
        std::optional<std::string_view>(*evidence_alias.context_id));
    (void)evidence_events;
    assigned.set_research_institution(alias_state, "alias:institution",
                                      "general_research_laboratory", 1, 1,
                                      "alias:context");
    const auto &institution_alias =
        alias_state.expertise().institutions().front();
    assigned.set_research_institution(
        alias_state, institution_alias.institution_instance_id,
        institution_alias.institution_archetype_id, 0, 0,
        std::optional<std::string_view>(*institution_alias.context_id));
    require(research_bytes(research_root) == source_before,
            "native authority replay changed canonical research data");
    std::cout << "adaptive_research_authority_tests: " << profiles
              << " profiles and " << commands << " commands passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive_research_authority_tests: " << typeid(error).name()
              << ": " << error.what()
              << "\nCWD: " << std::filesystem::current_path()
              << "\nFixture: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nResearch root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
