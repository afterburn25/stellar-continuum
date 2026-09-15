#include <stellar/core/adaptive_research_catalog.hpp>
#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/adaptive_research_expertise_service.hpp>
#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_pressure.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>
#include <stellar/core/adaptive_research_readiness.hpp>
#include <stellar/core/adaptive_research_state.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_pressure_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
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
    if (message.starts_with("Pressure metric signal") ||
        message.starts_with("Metric signal") ||
        message.starts_with("Event severity") ||
        message.starts_with("Specified argument") ||
        message.starts_with("Invalid institution counts") ||
        message.starts_with("Value must be in"))
      return {"ArgumentOutOfRangeException", message};
    if (message.starts_with("Unknown Research Pressure"))
      return {"ArgumentException", message};
    return {"KeyNotFoundException", message};
  }
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return {"ArgumentException", message};
  if (message.starts_with("Pressure dynamics defines unknown pressure") ||
      message.starts_with("Pressure dynamics rules do not cover") ||
      message.starts_with("pressure_dynamics.json catalog_id") ||
      message.starts_with("research_pressure_runtime_policy.json catalog_id") ||
      (message.starts_with("Pressure '") &&
       message.ends_with("has invalid decay/memory-floor values.")) ||
      message == "Invalid Research Pressure runtime policy.")
    return {"InvalidDataException", message};
  return {"UnexpectedNativeException", message};
}

json support_json(const AdaptiveResearchPressureState &state) {
  auto metrics = json::array();
  for (const auto &entry : state.metric_signals())
    metrics.push_back(
        {{"Id", entry.signal_id}, {"Value", numeric(entry.value)}});
  return {{"Revision", state.revision()},
          {"MetricSignals", std::move(metrics)},
          {"ActivePressureIds", state.active_pressure_ids()}};
}
void require(bool condition, std::string_view message);
json catalog_json(const AdaptiveResearchPressureCatalog &catalog) {
  auto rules = json::array();
  for (const auto &rule : catalog.rules())
    rules.push_back({{"Id", rule.id},
                     {"MetricSignalIds", rule.metric_signal_ids},
                     {"EventSignalIds", rule.event_signal_ids},
                     {"DecayPerYear", numeric(rule.decay_per_year)},
                     {"MemoryFloor", numeric(rule.memory_floor)}});
  auto metric = json::array(), event = json::array();
  std::vector<std::string> seen;
  for (const auto &rule : catalog.rules())
    for (const auto &id : rule.metric_signal_ids)
      if (std::ranges::find(seen, id) == seen.end()) {
        seen.push_back(id);
        metric.push_back(
            {{"Id", id},
             {"PressureIds", catalog.pressures_for_metric_signal(id)}});
      }
  seen.clear();
  for (const auto &rule : catalog.rules())
    for (const auto &id : rule.event_signal_ids)
      if (std::ranges::find(seen, id) == seen.end()) {
        seen.push_back(id);
        event.push_back(
            {{"Id", id},
             {"PressureIds", catalog.pressures_for_event_signal(id)}});
      }
  const auto &p = catalog.runtime_policy();
  return {{"Rules", std::move(rules)},
          {"MetricIndex", std::move(metric)},
          {"EventIndex", std::move(event)},
          {"RuntimePolicy",
           {{"StrongestSignalWeight", numeric(p.strongest_signal_weight)},
            {"MeanSignalWeight", numeric(p.mean_signal_weight)},
            {"RiseTowardTargetPerYear", numeric(p.rise_toward_target_per_year)},
            {"EventPulseBasePoints", numeric(p.event_pulse_base_points)},
            {"IntendedReviewIntervalYears",
             numeric(p.intended_review_interval_years)},
            {"DormantReviewIntervalYears",
             numeric(p.dormant_review_interval_years)}}}};
}
json catalog_case_json(const AdaptiveResearchPressureCatalog &catalog,
                       std::string_view rule_id) {
  const auto &rule = catalog.get_rule(rule_id);
  std::vector<std::string> duplicate_index;
  const auto duplicate =
      catalog.pressures_for_metric_signal("fixture:duplicate-signal");
  duplicate_index.assign(duplicate.begin(), duplicate.end());
  return {{"StrongestSignalWeight",
           numeric(catalog.runtime_policy().strongest_signal_weight)},
          {"MeanSignalWeight",
           numeric(catalog.runtime_policy().mean_signal_weight)},
          {"Rule",
           {{"Id", rule.id},
            {"MetricSignalIds", rule.metric_signal_ids},
            {"EventSignalIds", rule.event_signal_ids},
            {"DecayPerYear", numeric(rule.decay_per_year)},
            {"MemoryFloor", numeric(rule.memory_floor)}}},
          {"DuplicateMetricIndex", duplicate_index}};
}

class OwnedScratch final {
public:
  explicit OwnedScratch(const std::filesystem::path &canonical_root) {
    parent_ = std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                        "stellar-gate055-owned");
    std::filesystem::create_directories(parent_);
    parent_ = std::filesystem::weakly_canonical(parent_);
    std::random_device random;
    for (int attempt = 0; attempt < 32; ++attempt) {
      path_ = parent_ / ("native-" + std::to_string(random()) + "-" +
                         std::to_string(random()));
      if (std::filesystem::create_directory(path_)) {
        path_ = std::filesystem::weakly_canonical(path_);
        break;
      }
      path_.clear();
    }
    if (path_.empty() || path_.parent_path() != parent_)
      throw std::runtime_error(
          "Could not establish exclusive pressure scratch ownership.");
    for (const auto *name :
         {"pressure_dynamics.json", "research_pressure_runtime_policy.json"})
      std::filesystem::copy_file(canonical_root / name, path_ / name);
  }

  ~OwnedScratch() {
    std::error_code ignored;
    if (!path_.empty() && path_.is_absolute() &&
        std::filesystem::weakly_canonical(path_, ignored).parent_path() ==
            parent_ &&
        !ignored)
      std::filesystem::remove_all(path_, ignored);
  }

  OwnedScratch(const OwnedScratch &) = delete;
  OwnedScratch &operator=(const OwnedScratch &) = delete;
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path parent_;
  std::filesystem::path path_;
};

std::string decode_base64(std::string_view text) {
  const auto value = [](unsigned char character) {
    if (character >= 'A' && character <= 'Z')
      return static_cast<int>(character - 'A');
    if (character >= 'a' && character <= 'z')
      return static_cast<int>(character - 'a' + 26);
    if (character >= '0' && character <= '9')
      return static_cast<int>(character - '0' + 52);
    if (character == '+')
      return 62;
    if (character == '/')
      return 63;
    return -1;
  };
  require(text.size() % 4 == 0, "Fixture base64 length is invalid.");
  std::string output;
  output.reserve(text.size() / 4 * 3);
  for (std::size_t index = 0; index < text.size(); index += 4) {
    const bool last = index + 4 == text.size();
    const bool pad_two = text[index + 2] == '=';
    const bool pad_three = text[index + 3] == '=';
    require(text[index] != '=' && text[index + 1] != '=' &&
                (!pad_two || pad_three) && ((!pad_two && !pad_three) || last),
            "Fixture base64 padding is invalid.");
    const auto first = value(static_cast<unsigned char>(text[index]));
    const auto second = value(static_cast<unsigned char>(text[index + 1]));
    const auto third =
        pad_two ? 0 : value(static_cast<unsigned char>(text[index + 2]));
    const auto fourth =
        pad_three ? 0 : value(static_cast<unsigned char>(text[index + 3]));
    require(first >= 0 && second >= 0 && third >= 0 && fourth >= 0,
            "Fixture base64 alphabet is invalid.");
    require(!pad_two || (second & 15) == 0,
            "Fixture base64 trailing bits are invalid.");
    require(pad_two || !pad_three || (third & 3) == 0,
            "Fixture base64 trailing bits are invalid.");
    const auto bits = (static_cast<std::uint32_t>(first) << 18U) |
                      (static_cast<std::uint32_t>(second) << 12U) |
                      (static_cast<std::uint32_t>(third) << 6U) |
                      static_cast<std::uint32_t>(fourth);
    output.push_back(static_cast<char>((bits >> 16U) & 255U));
    if (!pad_two)
      output.push_back(static_cast<char>((bits >> 8U) & 255U));
    if (!pad_three)
      output.push_back(static_cast<char>(bits & 255U));
  }
  return output;
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
  for (const auto &v : values)
    result.push_back(event_json(v));
  return result;
}
using Result = std::variant<std::monostate, bool, double,
                            std::vector<AdaptiveResearchRuntimeEvent>>;
json result_json(const Result &result) {
  if (std::holds_alternative<std::monostate>(result))
    return nullptr;
  if (const auto *v = std::get_if<bool>(&result))
    return *v;
  if (const auto *v = std::get_if<double>(&result))
    return numeric(*v);
  return events_json(
      std::get<std::vector<AdaptiveResearchRuntimeEvent>>(result));
}
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: adaptive_research_pressure_tests <fixture> <research-data>");
    std::ifstream input(argv[1]);
    if (!input)
      throw std::runtime_error("Could not open fixture.");
    json fixture;
    input >> fixture;
    const std::filesystem::path root(argv[2]);
    auto base = load_adaptive_research_catalog(root);
    auto kernel = load_adaptive_research_runtime(root);
    auto catalog = load_adaptive_research_pressure_catalog(root, base);
    require_equal(catalog_json(catalog), fixture.at("Catalog"), "Catalog");
    std::size_t catalog_case_count = 0, source_only_catalog_count = 0;
    for (const auto &row : fixture.at("CatalogCases")) {
      const auto &retained = row.at("Input");
      const auto file_name = retained.at("File").get<std::string>();
      require(file_name == "pressure_dynamics.json" ||
                  file_name == "research_pressure_runtime_policy.json",
              "Catalog fixture names an unsupported changed file.");
      const auto changed_bytes =
          decode_base64(retained.at("BytesBase64").get<std::string>());
      const auto rule_id =
          row.at("Result").is_null()
              ? fixture.at("Catalog")
                    .at("Rules")
                    .at(0)
                    .at("Id")
                    .get<std::string>()
              : row.at("Result").at("Rule").at("Id").get<std::string>();
      const auto source_only = retained.at("SourceOnly").get<bool>();
      OwnedScratch scratch(root);
      {
        std::ofstream changed(scratch.path() / file_name,
                              std::ios::binary | std::ios::trunc);
        require(changed.is_open(), "Could not open changed catalog file.");
        changed.write(changed_bytes.data(),
                      static_cast<std::streamsize>(changed_bytes.size()));
        changed.flush();
        require(changed.good(), "Could not write changed catalog file.");
      }
      std::optional<AdaptiveResearchPressureCatalog> loaded;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        loaded.emplace(
            load_adaptive_research_pressure_catalog(scratch.path(), base));
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      if (source_only) {
        require(
            !row.at("Result").is_null() && row.at("Error").is_null(),
            "Source-only parser boundary lacks a successful source result.");
        require(!loaded && error.has_value(),
                "Native parser boundary unexpectedly accepted overflow JSON.");
        ++source_only_catalog_count;
        continue;
      }
      const json projected_result =
          loaded ? catalog_case_json(*loaded, rule_id) : json(nullptr);
      const json projected_error =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(projected_result, row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(projected_error, row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      ++catalog_case_count;
    }
    AdaptiveResearchPressureState low;
    std::size_t low_count = 0, command_count = 0;
    for (const auto &row : fixture.at("LowLevel")) {
      const auto &args = row.at("Input");
      const auto op = args.at("Op").get<std::string>();
      const auto id = args.at("Id").get<std::string>();
      const auto value =
          args.contains("Value") ? number(args.at("Value")) : 0.0;
      require_equal(support_json(low), row.at("Before"),
                    row.at("Name").get<std::string>() + ".Before");
      Result result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        if (op == "Get")
          result = low.get_metric_signal(id);
        else if (op == "Set")
          result =
              detail::AdaptiveResearchPressureStateWriter::set_metric_signal(
                  low, id, value);
        else if (op == "Activate")
          result =
              detail::AdaptiveResearchPressureStateWriter::activate_pressure(
                  low, id);
        else if (op == "Deactivate")
          result =
              detail::AdaptiveResearchPressureStateWriter::deactivate_pressure(
                  low, id);
        else
          throw std::invalid_argument("Unknown low-level operation.");
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      const json projected_error =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(result_json(result), row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(projected_error, row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      require_equal(support_json(low), row.at("After"),
                    row.at("Name").get<std::string>() + ".After");
      ++low_count;
    }
    AdaptiveResearchPressureRuntime runtime(kernel, catalog);
    auto state = kernel.create_civilization_state("fixture:pressure");
    for (const auto &row : fixture.at("Sequence")) {
      const auto &args = row.at("Input");
      const auto op = args.at("Op").get<std::string>();
      const auto id = args.value("Id", std::string{});
      const auto value =
          args.contains("Value") ? number(args.at("Value")) : 0.0;
      const auto years =
          args.contains("Years") ? number(args.at("Years")) : 0.0;
      const auto context = args.contains("Context")
                               ? optional_string(args.at("Context"))
                               : std::nullopt;
      std::vector<ResearchPressureMetricSignalEntry> values;
      if (args.contains("Values"))
        for (const auto &entry : args.at("Values"))
          values.push_back(
              {entry.at("Id").get<std::string>(), number(entry.at("Value"))});
      require_equal(state_json(state), row.at("StateBefore"),
                    row.at("Name").get<std::string>() + ".StateBefore");
      require_equal(support_json(runtime.get_support_state(state)),
                    row.at("SupportBefore"),
                    row.at("Name").get<std::string>() + ".SupportBefore");
      Result result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        if (op == "Metric") {
          runtime.report_metric_signal(state, id, value);
          result = true;
        } else if (op == "Metrics") {
          runtime.report_metric_signals(state, values);
          result = true;
        } else if (op == "Target")
          result = runtime.get_metric_target(state, id);
        else if (op == "Event")
          result = runtime.report_event_signal(state, id, value,
                                               optional_view(context));
        else if (op == "Advance")
          result = runtime.advance(state, years, optional_view(context));
        else
          throw std::invalid_argument("Unknown runtime operation.");
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      const json projected_error =
          error ? json{{"Type", error->first}, {"Message", error->second}}
                : json(nullptr);
      require_equal(result_json(result), row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(projected_error, row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      require_equal(state_json(state), row.at("StateAfter"),
                    row.at("Name").get<std::string>() + ".StateAfter");
      require_equal(support_json(runtime.get_support_state(state)),
                    row.at("SupportAfter"),
                    row.at("Name").get<std::string>() + ".SupportAfter");
      ++command_count;
    }

    const auto metric_id =
        fixture.at("Controls").at("MetricSignals").at(0).get<std::string>();
    AdaptiveResearchPressureRuntime first_runtime(kernel, catalog);
    AdaptiveResearchPressureRuntime second_runtime(kernel, catalog);
    auto first_state = kernel.create_civilization_state("fixture:identity");
    auto equal_id_state = kernel.create_civilization_state("fixture:identity");
    first_runtime.report_metric_signal(first_state, metric_id, 0.9);
    require_equal(support_json(first_runtime.get_support_state(first_state)),
                  fixture.at("Independence").at("RuntimeOneStateOne"),
                  "Independence.RuntimeOneStateOne");
    require_equal(support_json(second_runtime.get_support_state(first_state)),
                  fixture.at("Independence").at("RuntimeTwoStateOne"),
                  "Independence.RuntimeTwoStateOne");
    require_equal(support_json(first_runtime.get_support_state(equal_id_state)),
                  fixture.at("Independence").at("RuntimeOneEqualIdStateTwo"),
                  "Independence.RuntimeOneEqualIdStateTwo");

    const auto *support_before_state_move =
        &first_runtime.get_support_state(first_state);
    auto moved_state = std::move(first_state);
    require(&first_runtime.get_support_state(moved_state) ==
                support_before_state_move,
            "State move did not preserve pressure support identity.");

    ExpertiseWriter::set_field(
        Writer::expertise(moved_state),
        {"field:identity", {1, 2, 3}, {4, 5, 6}, 7, 8, 9, 1});
    const auto moved_state_projection = state_json(moved_state);
    auto copied_state = moved_state;
    require_equal(state_json(copied_state), moved_state_projection,
                  "Identity.CopyState");
    require(first_runtime.get_support_state(copied_state).revision() == 0,
            "State copy reused the source pressure identity.");
    ExpertiseWriter::set_field(
        Writer::expertise(copied_state),
        {"field:identity", {9, 8, 7}, {9, 8, 7}, 6, 5, 4, 2});
    require(state_json(copied_state) != state_json(moved_state),
            "State copy did not own an independent expertise sidecar.");

    auto assigned_state = kernel.create_civilization_state("fixture:assigned");
    first_runtime.report_metric_signal(assigned_state, metric_id, 0.4);
    require(first_runtime.get_support_state(assigned_state).revision() > 0,
            "Copy-assignment target did not begin with pressure support.");
    assigned_state = moved_state;
    require_equal(state_json(assigned_state), moved_state_projection,
                  "Identity.CopyAssignedState");
    require(first_runtime.get_support_state(assigned_state).revision() == 0,
            "State copy assignment reused the source pressure identity.");
    const auto *assigned_support =
        &first_runtime.get_support_state(assigned_state);
    const auto &assigned_alias = assigned_state;
    assigned_state = assigned_alias;
    require(&first_runtime.get_support_state(assigned_state) ==
                assigned_support,
            "State self-assignment replaced its pressure identity.");

    auto move_source =
        kernel.create_civilization_state("fixture:move-assignment-source");
    auto move_target =
        kernel.create_civilization_state("fixture:move-assignment-target");
    first_runtime.report_metric_signal(move_source, metric_id, 0.7);
    first_runtime.report_metric_signal(move_target, metric_id, 0.2);
    const auto *incoming_move_support =
        &first_runtime.get_support_state(move_source);
    require(first_runtime.get_support_state(move_target).revision() > 0,
            "Move-assignment target did not begin with pressure support.");
    move_target = std::move(move_source);
    require(&first_runtime.get_support_state(move_target) ==
                incoming_move_support,
            "State move assignment did not preserve the incoming identity.");

    const auto *support_before_runtime_move =
        &first_runtime.get_support_state(moved_state);
    AdaptiveResearchPressureRuntime moved_runtime(std::move(first_runtime));
    require(&moved_runtime.get_support_state(moved_state) ==
                support_before_runtime_move,
            "Runtime move construction invalidated pressure support.");
    AdaptiveResearchPressureRuntime assigned_runtime(kernel, catalog);
    assigned_runtime = std::move(moved_runtime);
    require(&assigned_runtime.get_support_state(moved_state) ==
                support_before_runtime_move,
            "Runtime move assignment invalidated pressure support.");

    detail::AdaptiveResearchWeakStateTable<int> weak_table;
    std::optional<AdaptiveResearchCivilizationState> reusable_state;
    reusable_state.emplace(
        kernel.create_civilization_state("fixture:expiring"));
    const auto *reused_object_address = &*reusable_state;
    weak_table.get_or_create(*reusable_state) = 73;
    require(weak_table.entry_count_for_testing() == 1,
            "Weak table did not retain its live entry.");
    reusable_state.reset();
    reusable_state.emplace(
        kernel.create_civilization_state("fixture:replacement"));
    require(&*reusable_state == reused_object_address,
            "Optional did not reuse the exact State object address.");
    require(weak_table.try_get(*reusable_state) == nullptr,
            "Same-address replacement recovered expired pressure support.");
    weak_table.maintain(64);
    require(weak_table.entry_count_for_testing() == 0,
            "Weak table did not reclaim its expired entry.");

    detail::AdaptiveResearchWeakStateTable<int> factory_table;
    bool factory_threw = false;
    try {
      (void)factory_table.get_or_create(*reusable_state, []() -> int {
        throw std::runtime_error("fixture factory failure");
      });
    } catch (const std::runtime_error &error) {
      factory_threw =
          std::string_view(error.what()) == "fixture factory failure";
    }
    require(factory_threw,
            "Weak-table factory failure was not propagated exactly.");
    require(factory_table.entry_count_for_testing() == 0,
            "Weak-table factory failure inserted an entry.");
    factory_table.get_or_create(*reusable_state) = 19;
    require(factory_table.try_get(*reusable_state) &&
                *factory_table.try_get(*reusable_state) == 19,
            "Weak table failed after a throwing factory retry.");

    std::cout << "adaptive_research_pressure_tests: " << catalog.rules().size()
              << " rules, " << catalog_case_count << " catalog cases plus "
              << source_only_catalog_count << " source-only parser boundary, "
              << low_count << " state operations, and " << command_count
              << " runtime commands plus identity lifetime probes passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive_research_pressure_tests: " << typeid(error).name()
              << ": " << error.what()
              << "\nCWD: " << std::filesystem::current_path()
              << "\nFixture: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nResearch root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
