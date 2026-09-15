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

namespace {
std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
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
void setup(AdaptiveResearchExpertiseService &service,
           AdaptiveResearchCivilizationState &state, const json &steps) {
  for (const auto &step : steps) {
    const auto op = step.at("Op").get<std::string>();
    if (op == "SeedField") {
      const auto &v = step.at("Value");
      service.seed_field_competence(
          state, step.at("FieldId").get<std::string>(),
          {number(v.at("Theoretical")), number(v.at("Experimental")),
           number(v.at("Engineering"))},
          number(step.at("ActivityYear")));
    } else if (op == "SetInstitution") {
      const auto context = optional_string(step.at("Context"));
      service.set_institution(state, step.at("InstanceId").get<std::string>(),
                              step.at("ArchetypeId").get<std::string>(),
                              step.at("Total").get<int>(),
                              step.at("Active").get<int>(),
                              optional_view(context));
    } else if (op == "AddEvidence") {
      const auto context = optional_string(step.at("Context"));
      Writer::add_evidence(state, {step.at("InstanceId").get<std::string>(),
                                   step.at("TypeId").get<std::string>(),
                                   "fixture", number(step.at("Quality")),
                                   number(step.at("Confidence")), context, 0});
    } else if (op == "SetTacit") {
      const auto context = optional_string(step.at("Context"));
      service.set_tacit_asset(
          state, step.at("AssetId").get<std::string>(),
          step.at("AssetTypeId").get<std::string>(),
          static_cast<ResearchTacitScopeKind>(step.at("Scope").get<int>()),
          step.at("ScopeRef").get<std::string>(),
          static_cast<ResearchTacitAssimilationStage>(
              step.at("Assimilation").get<int>()),
          number(step.at("Depth")), number(step.at("Availability")),
          number(step.at("Translation")), number(step.at("Training")),
          step.at("Provenance").get<std::string>(), optional_view(context));
    } else if (op == "InjectTacit") {
      const auto &value = step.at("Value");
      ExpertiseWriter::set_tacit_asset(
          Writer::expertise(state),
          {value.at("AssetId").get<std::string>(),
           value.at("AssetTypeId").get<std::string>(),
           static_cast<ResearchTacitScopeKind>(
               value.at("ScopeKind").get<int>()),
           value.at("ScopeRef").get<std::string>(),
           static_cast<ResearchTacitAssimilationStage>(
               value.at("AssimilationStage").get<int>()),
           number(value.at("Depth")), number(value.at("Availability")),
           number(value.at("TranslationContextQuality")),
           number(value.at("TrainingContinuity")),
           value.at("Provenance").get<std::string>(),
           optional_string(value.at("ContextId")), 0});
    } else if (op == "InjectInstitution") {
      const auto &value = step.at("Value");
      ExpertiseWriter::set_institution(
          Writer::expertise(state),
          {value.at("InstitutionInstanceId").get<std::string>(),
           value.at("InstitutionArchetypeId").get<std::string>(),
           optional_string(value.at("ContextId")),
           value.at("TotalCount").get<int>(),
           value.at("ActiveCount").get<int>(), 0});
    } else if (op == "SetNode") {
      const auto &value = step.at("Value");
      Writer::set_node_state(
          state,
          {value.at("NodeId").get<std::string>(),
           static_cast<ResearchMaturity>(value.at("Maturity").get<int>()),
           optional_string(value.at("Resolution")),
           number(value.at("StageResearchPoints")),
           number(value.at("TotalResearchPoints")), 0});
    } else {
      throw std::invalid_argument("Unknown setup operation " + op);
    }
  }
}
using Result = std::variant<std::monostate, bool, ResearchReadinessBreakdown>;
json result_json(const Result &result) {
  if (std::holds_alternative<std::monostate>(result))
    return nullptr;
  if (const auto *value = std::get_if<bool>(&result))
    return *value;
  return readiness_json(std::get<ResearchReadinessBreakdown>(result));
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: research_readiness_tests <fixture> <research-data>");
    const json fixture = json::parse(bytes(argv[1]));
    const std::filesystem::path root(argv[2]);
    auto catalog = load_adaptive_research_catalog(root);
    auto facilities = load_adaptive_research_facility_catalog(root, catalog);
    auto progress = load_adaptive_research_progress_policy(root, catalog);
    auto expertise =
        load_adaptive_research_expertise_catalog(root, catalog, facilities);
    AdaptiveResearchReadinessCalculator calculator(catalog, facilities,
                                                   expertise, progress);
    AdaptiveResearchExpertiseService service(catalog, expertise, calculator);
    std::size_t readiness_count = 0, command_count = 0;
    for (const auto &record : fixture.at("ReadinessCases")) {
      const auto &input = record.at("Input");
      const auto node_id = input.at("NodeId").get<std::string>();
      const auto stage =
          static_cast<ResearchMaturity>(input.at("Stage").get<int>());
      const double labs = number(input.at("AssignedEffectiveLabs"));
      const auto context = optional_string(input.at("Context"));
      const auto context_view = optional_view(context);
      AdaptiveResearchCivilizationState state(
          "fixture", catalog.metadata().starting_directed_program_stage_id);
      setup(service, state, input.at("Setup"));
      const auto where = "readiness/" + record.at("Name").get<std::string>();
      require_equal(state_json(state), record.at("Before"), where + ".before");
      std::optional<ResearchReadinessBreakdown> result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        result.emplace(calculator.calculate(state, state.expertise(), node_id,
                                            stage, labs, context_view));
      } catch (const std::exception &caught) {
        error.emplace(source_error(caught));
      }
      require_equal(state_json(state), record.at("After"), where + ".after");
      const json error_json =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(error_json, record.at("Error"), where + ".error");
      if (!error)
        require_equal(readiness_json(*result), record.at("Result"),
                      where + ".result");
      ++readiness_count;
    }
    AdaptiveResearchCivilizationState state(
        "fixture", catalog.metadata().starting_directed_program_stage_id);
    for (const auto &record : fixture.at("Commands")) {
      const auto &input = record.at("Input");
      const auto op = input.at("Op").get<std::string>();
      const auto &args = input.at("Args");
      std::function<Result()> invoke;
      if (op == "SeedFieldCompetence") {
        const auto id = args.at("FieldId").get<std::string>();
        const auto &v = args.at("Value");
        const ResearchCompetenceVector value{number(v.at("Theoretical")),
                                             number(v.at("Experimental")),
                                             number(v.at("Engineering"))};
        const double year = number(args.at("ActivityYear"));
        invoke = [&, id, value, year] {
          service.seed_field_competence(state, id, value, year);
          return Result{};
        };
      } else if (op == "SetInstitution") {
        const auto instance = args.at("InstanceId").get<std::string>(),
                   archetype = args.at("ArchetypeId").get<std::string>();
        const int total = args.at("Total").get<int>(),
                  active = args.at("Active").get<int>();
        const auto context = optional_string(args.at("Context"));
        invoke = [&, instance, archetype, total, active, context] {
          service.set_institution(state, instance, archetype, total, active,
                                  optional_view(context));
          return Result{};
        };
      } else if (op == "RemoveInstitution") {
        const auto id = args.at("InstanceId").get<std::string>();
        invoke = [&, id] {
          return Result(service.remove_institution(state, id));
        };
      } else if (op == "SetTacitAsset") {
        const auto asset = args.at("AssetId").get<std::string>(),
                   type = args.at("AssetTypeId").get<std::string>(),
                   scope_ref = args.at("ScopeRef").get<std::string>(),
                   provenance = args.at("Provenance").get<std::string>();
        const auto scope =
            static_cast<ResearchTacitScopeKind>(args.at("Scope").get<int>());
        const auto assimilation = static_cast<ResearchTacitAssimilationStage>(
            args.at("Assimilation").get<int>());
        const double depth = number(args.at("Depth")),
                     availability = number(args.at("Availability")),
                     translation = number(args.at("Translation")),
                     training = number(args.at("Training"));
        const auto context = optional_string(args.at("Context"));
        invoke = [&, asset, type, scope, scope_ref, assimilation, depth,
                  availability, translation, training, provenance, context] {
          service.set_tacit_asset(state, asset, type, scope, scope_ref,
                                  assimilation, depth, availability,
                                  translation, training, provenance,
                                  optional_view(context));
          return Result{};
        };
      } else if (op == "RemoveTacitAsset") {
        const auto id = args.at("AssetId").get<std::string>();
        invoke = [&, id] {
          return Result(service.remove_tacit_asset(state, id));
        };
      } else if (op == "ApplyCompletedStagePractice") {
        const auto id = args.at("NodeId").get<std::string>();
        const auto stage =
            static_cast<ResearchMaturity>(args.at("Stage").get<int>());
        const double year = number(args.at("ActivityYear"));
        invoke = [&, id, stage, year] {
          service.apply_completed_stage_practice(state, id, stage, year);
          return Result{};
        };
      } else if (op == "ApplyCompetenceAtrophy") {
        const double year = number(args.at("CurrentYear")),
                     elapsed = number(args.at("ElapsedYears"));
        invoke = [&, year, elapsed] {
          service.apply_competence_atrophy(state, year, elapsed);
          return Result{};
        };
      } else if (op == "CalculateProjectReadiness") {
        const auto id = args.at("NodeId").get<std::string>();
        const auto stage =
            static_cast<ResearchMaturity>(args.at("Stage").get<int>());
        const double labs = number(args.at("AssignedEffectiveLabs"));
        const auto context = optional_string(args.at("Context"));
        invoke = [&, id, stage, labs, context] {
          return Result(service.calculate_project_readiness(
              state, id, stage, labs, optional_view(context)));
        };
      } else
        throw std::invalid_argument("Unknown command operation " + op);
      const auto where = "command/" + record.at("Name").get<std::string>();
      require_equal(state_json(state), record.at("Before"), where + ".before");
      std::optional<Result> result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        result.emplace(invoke());
      } catch (const std::exception &caught) {
        error.emplace(source_error(caught));
      }
      require_equal(state_json(state), record.at("After"), where + ".after");
      const json error_json =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(error_json, record.at("Error"), where + ".error");
      if (!error)
        require_equal(result_json(*result), record.at("Result"),
                      where + ".result");
      ++command_count;
    }
    std::size_t focused_count = 0;
    for (const auto &record : fixture.at("ServiceCases")) {
      AdaptiveResearchCivilizationState focused_state(
          "fixture", catalog.metadata().starting_directed_program_stage_id);
      const auto &input = record.at("Input");
      setup(service, focused_state, input.at("Setup"));
      const auto op = input.at("Op").get<std::string>();
      const auto &args = input.at("Args");
      std::function<Result()> invoke;
      if (op == "SetInstitution") {
        const auto instance = args.at("InstanceId").get<std::string>();
        const auto archetype = args.at("ArchetypeId").get<std::string>();
        const int total = args.at("Total").get<int>();
        const int active = args.at("Active").get<int>();
        const auto context = optional_string(args.at("Context"));
        invoke = [&, instance, archetype, total, active, context] {
          service.set_institution(focused_state, instance, archetype, total,
                                  active, optional_view(context));
          return Result{};
        };
      } else if (op == "ApplyCompetenceAtrophy") {
        const double year = number(args.at("CurrentYear"));
        const double elapsed = number(args.at("ElapsedYears"));
        invoke = [&, year, elapsed] {
          service.apply_competence_atrophy(focused_state, year, elapsed);
          return Result{};
        };
      } else if (op == "ApplyCompletedStagePractice") {
        const auto id = args.at("NodeId").get<std::string>();
        const auto stage =
            static_cast<ResearchMaturity>(args.at("Stage").get<int>());
        const double year = number(args.at("ActivityYear"));
        invoke = [&, id, stage, year] {
          service.apply_completed_stage_practice(focused_state, id, stage,
                                                 year);
          return Result{};
        };
      } else {
        throw std::invalid_argument("Unknown focused service operation " + op);
      }
      const auto where = "focused/" + record.at("Name").get<std::string>();
      require_equal(state_json(focused_state), record.at("Before"),
                    where + ".before");
      std::optional<Result> result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        result.emplace(invoke());
      } catch (const std::exception &caught) {
        error.emplace(source_error(caught));
      }
      require_equal(state_json(focused_state), record.at("After"),
                    where + ".after");
      const json error_json =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(error_json, record.at("Error"), where + ".error");
      if (!error)
        require_equal(result_json(*result), record.at("Result"),
                      where + ".result");
      ++focused_count;
    }

    const auto field_id =
        fixture.at("Metadata").at("PlainNode").get<std::string>();
    const auto &plain_node = catalog.get_node(field_id);
    const auto competence_id = plain_node.knowledge_fields.front();
    AdaptiveResearchCivilizationState copy_source(
        "copy-source", catalog.metadata().starting_directed_program_stage_id);
    service.seed_field_competence(copy_source, competence_id, {10, 20, 30},
                                  1.0);
    auto copy = copy_source;
    service.seed_field_competence(copy_source, competence_id, {40, 50, 60},
                                  2.0);
    if (copy.expertise().revision() != 1 ||
        copy.expertise().get_field(competence_id).current.theoretical != 10.0)
      throw std::runtime_error(
          "Attached expertise sidecar copy is not independent.");
    service.seed_field_competence(copy, competence_id, {70, 80, 90}, 3.0);
    if (copy_source.expertise().revision() != 2 ||
        copy_source.expertise().get_field(competence_id).current.theoretical !=
            40.0)
      throw std::runtime_error("Expertise mutation escaped copied state.");

    const std::string_view field_alias =
        copy.expertise().field_competence().front().field_id;
    service.seed_field_competence(copy, field_alias, {75, 85, 95}, 4.0);
    const auto institution_id =
        expertise.institutions().front().institution_archetype_id;
    service.set_institution(copy, "alias:institution", institution_id, 2, 1);
    const auto &stored_institution = copy.expertise().institutions().front();
    const std::string_view instance_alias =
        stored_institution.institution_instance_id;
    const std::string_view archetype_alias =
        stored_institution.institution_archetype_id;
    service.set_institution(copy, instance_alias, archetype_alias, 3, 2);
    const std::string_view remove_institution_alias =
        copy.expertise().institutions().front().institution_instance_id;
    if (!service.remove_institution(copy, remove_institution_alias))
      throw std::runtime_error("Institution alias removal failed.");

    static_cast<void>(Writer::add_facility_capability(copy, "alias:one"));
    static_cast<void>(Writer::add_facility_capability(copy, "alias:two"));
    const auto facility_aliases = copy.facility_capabilities();
    Writer::set_facility_capabilities(copy, facility_aliases);
    if (copy.facility_capabilities().size() != 2)
      throw std::runtime_error("Bulk facility alias snapshot failed.");

    bool core_boundary = false, expertise_boundary = false;
    try {
      static_cast<void>(detail::checked_next_research_state_revision(
          std::numeric_limits<std::int64_t>::max()));
    } catch (const std::overflow_error &) {
      core_boundary = true;
    }
    try {
      static_cast<void>(
          detail::checked_next_adaptive_research_expertise_revision(
              std::numeric_limits<std::int64_t>::max()));
    } catch (const std::overflow_error &) {
      expertise_boundary = true;
    }
    if (!core_boundary || !expertise_boundary)
      throw std::runtime_error("A revision boundary accepted signed overflow.");
    std::cout << "research readiness parity: " << readiness_count
              << " readiness cases, " << command_count
              << " sequential service commands, " << focused_count
              << " focused service cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "research readiness parity failure\nexception_type: "
              << typeid(error).name() << "\nmessage: " << error.what()
              << "\ncwd: " << std::filesystem::current_path()
              << "\nfixture: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nresearch_root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
