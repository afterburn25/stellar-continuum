#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <bcrypt.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <stellar/core/adaptive_research_authority.hpp>
#include <stellar/core/adaptive_research_foreign_discovery.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <variant>
#include <vector>
using json = nlohmann::json;
using namespace stellar::core;
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

json assessment_json(const ForeignTechnologyAssessmentRuntimeState &value) {
  return {{"ForeignTechnologyReference", value.foreign_technology_reference},
          {"SourceLineageReference", value.source_lineage_reference},
          {"Understanding", static_cast<int>(value.understanding)},
          {"Operability", static_cast<int>(value.operability)},
          {"Reproduction", static_cast<int>(value.reproduction)},
          {"Adaptation", static_cast<int>(value.adaptation)},
          {"KnownConstraintIds", value.known_constraint_ids},
          {"EvidenceRefs", value.evidence_refs},
          {"TacitAssetRefs", value.tacit_asset_refs},
          {"LastAssessmentYear", numeric(value.last_assessment_year)},
          {"Confidence", numeric(value.confidence)},
          {"Revision", value.revision}};
}
json package_json(const ForeignTechnologyPackageRuntimeState &value) {
  return {{"PackageId", value.package_id},
          {"ForeignTechnologyReference", value.foreign_technology_reference},
          {"SourceLineageReference", value.source_lineage_reference},
          {"ComponentIds", value.component_ids},
          {"RightIds", value.right_ids},
          {"EvidenceRefs", value.evidence_refs},
          {"TacitAssetRefs", value.tacit_asset_refs},
          {"KnowledgeFieldIds", value.knowledge_field_ids},
          {"Provenance", value.provenance},
          {"Integrity", numeric(value.integrity)},
          {"Revision", value.revision}};
}
json foreign_json(const AdaptiveResearchForeignTechnologyState &state) {
  auto assessments = json::array(), packages = json::array();
  for (const auto &value : state.assessments())
    assessments.push_back(assessment_json(value));
  for (const auto &value : state.packages())
    packages.push_back(package_json(value));
  return {{"Revision", state.revision()},
          {"Assessments", std::move(assessments)},
          {"Packages", std::move(packages)}};
}
ForeignTechnologyAssessmentRuntimeState assessment_input(const json &value) {
  return {
      value.at("ForeignTechnologyReference").get<std::string>(),
      value.at("SourceLineageReference").get<std::string>(),
      static_cast<ForeignUnderstandingState>(value.at("Understanding").get<int>()),
      static_cast<ForeignOperabilityState>(value.at("Operability").get<int>()),
      static_cast<ForeignReproductionState>(value.at("Reproduction").get<int>()),
      static_cast<ForeignAdaptationState>(value.at("Adaptation").get<int>()),
      value.at("KnownConstraintIds").get<std::vector<std::string>>(),
      value.at("EvidenceRefs").get<std::vector<std::string>>(),
      value.at("TacitAssetRefs").get<std::vector<std::string>>(),
      number(value.at("LastAssessmentYear")), number(value.at("Confidence")),
      value.at("Revision").get<std::int64_t>()};
}
ForeignTechnologyPackageRuntimeState package_state_input(const json &value) {
  return {
      value.at("PackageId").get<std::string>(),
      value.at("ForeignTechnologyReference").get<std::string>(),
      value.at("SourceLineageReference").get<std::string>(),
      value.at("ComponentIds").get<std::vector<std::string>>(),
      value.at("RightIds").get<std::vector<std::string>>(),
      value.at("EvidenceRefs").get<std::vector<std::string>>(),
      value.at("TacitAssetRefs").get<std::vector<std::string>>(),
      value.at("KnowledgeFieldIds").get<std::vector<std::string>>(),
      value.at("Provenance").get<std::string>(), number(value.at("Integrity")),
      value.at("Revision").get<std::int64_t>()};
}
json catalog_json(const AdaptiveResearchForeignTechnologyCatalog &catalog) {
  auto components = json::array();
  for (const auto &value : catalog.components())
    components.push_back({{"Id", value.id},
                          {"TacitAssetTypeIds", value.tacit_asset_type_ids},
                          {"CreatesOrReferencesEvidence",
                           value.creates_or_references_evidence}});
  json minimum = json::object();
  for (const auto &value :
       catalog.runtime_policy().minimum_understanding_by_component)
    minimum[value.component_id] = static_cast<int>(value.understanding);
  const auto &policy = catalog.runtime_policy();
  auto parsed = json::array();
  for (const auto &id : {"unknown", "observed", "characterized",
                         "principle_understood", "engineering_understood"})
    parsed.push_back(
        {{"Id", id},
         {"Value",
          static_cast<int>(
              AdaptiveResearchForeignTechnologyCatalog::parse_understanding(
                  id))}});
  return {{"ConstraintIds", catalog.constraint_ids()},
          {"Components", std::move(components)},
          {"Rights", catalog.rights()},
          {"RuntimePolicy",
           {{"MinimumUnderstandingByComponent", std::move(minimum)},
            {"ConceptualInspirationIfCharacterized",
             policy.conceptual_inspiration_if_characterized},
            {"MinimumTrainingContinuityForTrained",
             numeric(policy.minimum_training_continuity_for_trained)},
            {"MinimumTranslationQualityBeyondAccess",
             numeric(policy.minimum_translation_quality_beyond_access)},
            {"NewObservationConfidence",
             numeric(policy.new_observation_confidence)},
            {"NewCharacterizedConfidence",
             numeric(policy.new_characterized_confidence)},
            {"ControlledAnalysisConfidenceMinimum",
             numeric(policy.controlled_analysis_confidence_minimum)},
            {"OperationalFactConfidenceMinimum",
             numeric(policy.operational_fact_confidence_minimum)},
            {"EngineeringUnderstoodConfidenceMinimum",
             numeric(policy.engineering_understood_confidence_minimum)}}},
          {"ParseUnderstanding", std::move(parsed)}};
}
ForeignTechnologyPackageInput package_input(const json &value) {
  ForeignTechnologyPackageInput result;
  result.package_id = value.at("PackageId").get<std::string>();
  result.foreign_technology_reference =
      value.at("ForeignTechnologyReference").get<std::string>();
  result.source_lineage_reference =
      value.at("SourceLineageReference").get<std::string>();
  result.component_ids =
      value.at("ComponentIds").get<std::vector<std::string>>();
  result.right_ids = value.at("RightIds").get<std::vector<std::string>>();
  result.knowledge_field_ids =
      value.at("KnowledgeFieldIds").get<std::vector<std::string>>();
  for (const auto &evidence : value.at("Evidence"))
    result.evidence.push_back(
        {evidence.at("EvidenceInstanceId").get<std::string>(),
         evidence.at("EvidenceTypeId").get<std::string>(),
         evidence.at("Provenance").get<std::string>(),
         number(evidence.at("Quality")), number(evidence.at("Confidence")),
         optional_string(evidence.at("ContextId"))});
  result.known_constraint_ids =
      value.at("KnownConstraintIds").get<std::vector<std::string>>();
  result.provenance = value.at("Provenance").get<std::string>();
  result.integrity = number(value.at("Integrity"));
  result.translation_context_quality =
      number(value.at("TranslationContextQuality"));
  result.training_continuity = number(value.at("TrainingContinuity"));
  result.acquired_year = number(value.at("AcquiredYear"));
  result.target_applicability_context_id =
      optional_string(value.at("TargetApplicabilityContextId"));
  return result;
}
ForeignTechnologyRecipientValueContext value_context(const json &value) {
  return {number(value.at("CapabilityNovelty")),
          number(value.at("StrategicNeed")),
          number(value.at("ExpectedNativeWorkSaved")),
          number(value.at("RecipientReadiness")),
          number(value.at("RecipientOperabilityFit")),
          number(value.at("DependencySafety")),
          number(value.at("KnownThirdPartyDemand")),
          number(value.at("PackageTransferability")),
          number(value.at("ScarcityOrExclusivity")),
          number(value.at("DependencyRisk")),
          number(value.at("HazardRisk"))};
}
json recipient_json(const ForeignTechnologyRecipientValueAssessment &value) {
  return {
      {"ResearchUtility", numeric(value.research_utility)},
      {"OperationalUtility", numeric(value.operational_utility)},
      {"ResaleOrBrokerageUtility", numeric(value.resale_or_brokerage_utility)},
      {"DependencyRisk", numeric(value.dependency_risk)},
      {"HazardRisk", numeric(value.hazard_risk)},
      {"HolderIncompatibilityCanStillBroker",
       value.holder_incompatibility_can_still_broker},
      {"Explanation", value.explanation}};
}
std::pair<std::string, std::string> source_error(const std::exception &error) {
  const std::string message(error.what());
  if (dynamic_cast<const AdaptiveResearchForeignArgumentOutOfRange *>(&error))
    return {"ArgumentOutOfRangeException", message};
  if (dynamic_cast<const AdaptiveResearchForeignMissingRecord *>(&error))
    return {"KeyNotFoundException", message};
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return {"ArgumentException", message};
  if (dynamic_cast<const std::overflow_error *>(&error))
    return {"OverflowException", message};
  if (dynamic_cast<const AdaptiveResearchForeignCatalogDataError *>(&error))
    return {"InvalidDataException", message};
  if (dynamic_cast<const AdaptiveResearchForeignJsonOperationError *>(&error))
    return {"InvalidOperationException", message};
  if (dynamic_cast<const std::runtime_error *>(&error))
    return {"InvalidOperationException", message};
  return {"UnexpectedNativeException", message};
}
using Result =
    std::variant<std::monostate, bool, ForeignTechnologyAssessmentRuntimeState,
                 ForeignTechnologyRecipientValueAssessment>;
json result_json(const Result &value) {
  if (std::holds_alternative<std::monostate>(value))
    return nullptr;
  if (const auto *boolean = std::get_if<bool>(&value))
    return *boolean;
  if (const auto *assessment =
          std::get_if<ForeignTechnologyAssessmentRuntimeState>(&value))
    return assessment_json(*assessment);
  return recipient_json(
      std::get<ForeignTechnologyRecipientValueAssessment>(value));
}

void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string(message));
}

class OwnedScratch final {
public:
  explicit OwnedScratch(const std::filesystem::path &canonical_root) {
    parent_ = std::filesystem::absolute(std::filesystem::temp_directory_path() /
                                        "stellar-gate059-owned");
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
          "Could not establish exclusive foreign-technology scratch ownership.");
    for (const auto *name :
         {"foreign_research_materialization_policy.json"})
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

std::string decode_base64(std::string_view encoded) {
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
  require(encoded.size() % 4 == 0, "Fixture base64 length is invalid.");
  std::string output;
  output.reserve(encoded.size() / 4 * 3);
  for (std::size_t index = 0; index < encoded.size(); index += 4) {
    const bool last = index + 4 == encoded.size();
    const bool pad_two = encoded[index + 2] == '=';
    const bool pad_three = encoded[index + 3] == '=';
    require(encoded[index] != '=' && encoded[index + 1] != '=' &&
                (!pad_two || pad_three) &&
                ((!pad_two && !pad_three) || last),
            "Fixture base64 padding is invalid.");
    const auto first = value(static_cast<unsigned char>(encoded[index]));
    const auto second = value(static_cast<unsigned char>(encoded[index + 1]));
    const auto third =
        pad_two ? 0 : value(static_cast<unsigned char>(encoded[index + 2]));
    const auto fourth =
        pad_three ? 0 : value(static_cast<unsigned char>(encoded[index + 3]));
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

std::string sha256(std::string_view value) {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  auto require_status = [](NTSTATUS status, std::string_view operation) {
    if (status < 0)
      throw std::runtime_error(std::string(operation) + " failed.");
  };
  require_status(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                              nullptr, 0),
                 "BCryptOpenAlgorithmProvider");
  struct AlgorithmCloser {
    BCRYPT_ALG_HANDLE value;
    ~AlgorithmCloser() { BCryptCloseAlgorithmProvider(value, 0); }
  } algorithm_closer{algorithm};
  DWORD object_length{}, hash_length{}, returned{};
  require_status(BCryptGetProperty(
                     algorithm, BCRYPT_OBJECT_LENGTH,
                     reinterpret_cast<PUCHAR>(&object_length),
                     sizeof(object_length), &returned, 0),
                 "BCryptGetProperty(object length)");
  require_status(BCryptGetProperty(
                     algorithm, BCRYPT_HASH_LENGTH,
                     reinterpret_cast<PUCHAR>(&hash_length),
                     sizeof(hash_length), &returned, 0),
                 "BCryptGetProperty(hash length)");
  std::vector<unsigned char> object(object_length), digest(hash_length);
  require_status(BCryptCreateHash(algorithm, &hash, object.data(),
                                  object_length, nullptr, 0, 0),
                 "BCryptCreateHash");
  struct HashCloser {
    BCRYPT_HASH_HANDLE value;
    ~HashCloser() { BCryptDestroyHash(value); }
  } hash_closer{hash};
  require_status(BCryptHashData(
                     hash,
                     reinterpret_cast<PUCHAR>(const_cast<char *>(value.data())),
                     static_cast<ULONG>(value.size()), 0),
                 "BCryptHashData");
  require_status(BCryptFinishHash(hash, digest.data(), hash_length, 0),
                 "BCryptFinishHash");
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(digest.size() * 2);
  for (const auto byte : digest) {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

std::string directory_fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> paths;
  for (const auto &entry : std::filesystem::directory_iterator(root))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      paths.push_back(entry.path());
  std::ranges::sort(paths, [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::string retained;
  for (const auto &path : paths) {
    retained += path.filename().string();
    retained += bytes(path);
  }
  return sha256(retained);
}

json discovery_catalog_json(
    const AdaptiveResearchForeignDiscoveryCatalog &catalog) {
  auto rules = json::array();
  for (const auto &rule : catalog.method_rules())
    rules.push_back({{"TriggerAxis", static_cast<int>(rule.trigger_axis)},
                     {"MinimumAxisRank", rule.minimum_axis_rank},
                     {"NodeIds", rule.node_ids},
                     {"AwarenessState",
                      static_cast<int>(rule.awareness_state)}});
  auto parsed = json::array();
  for (const auto *id : {"none", "conceptual_inspiration",
                         "interface_adaptation", "native_derivative",
                         "hybrid_lineage"})
    parsed.push_back(
        {{"Id", id},
         {"Value", static_cast<int>(
                       AdaptiveResearchForeignDiscoveryCatalog::
                           parse_adaptation(id))}});
  return {{"ObservedEvidenceState",
           static_cast<int>(catalog.observed_evidence_state())},
          {"CharacterizedEvidenceState",
           static_cast<int>(catalog.characterized_evidence_state())},
          {"MethodRules", std::move(rules)},
          {"ParseAdaptation", std::move(parsed)}};
}

json awareness_event_json(const ForeignResearchAwarenessEvent &event) {
  return {{"ForeignTechnologyReference", event.foreign_technology_reference},
          {"NodeId", event.node_id},
          {"PreviousState",
           event.previous_state ? json(static_cast<int>(*event.previous_state))
                                : json(nullptr)},
          {"NewState", static_cast<int>(event.new_state)},
          {"Reason", event.reason}};
}

json awareness_events_json(
    std::span<const ForeignResearchAwarenessEvent> events) {
  auto result = json::array();
  for (const auto &event : events)
    result.push_back(awareness_event_json(event));
  return result;
}

using DiscoveryResult =
    std::variant<std::monostate, bool, ForeignTechnologyAssessmentRuntimeState,
                 ForeignResearchDiscoveryResult,
                 std::vector<ForeignResearchAwarenessEvent>>;

json discovery_result_json(const DiscoveryResult &result) {
  if (std::holds_alternative<std::monostate>(result))
    return nullptr;
  if (const auto *value = std::get_if<bool>(&result))
    return *value;
  if (const auto *value =
          std::get_if<ForeignTechnologyAssessmentRuntimeState>(&result))
    return assessment_json(*value);
  if (const auto *value = std::get_if<ForeignResearchDiscoveryResult>(&result))
    return {{"Assessment", assessment_json(value->assessment)},
            {"AwarenessEvents",
             awareness_events_json(value->awareness_events)}};
  return awareness_events_json(
      std::get<std::vector<ForeignResearchAwarenessEvent>>(result));
}

ResearchNodeRuntimeState node_input(const json &value) {
  return {value.at("NodeId").get<std::string>(),
          static_cast<ResearchMaturity>(value.at("Maturity").get<int>()),
          optional_string(value.at("Resolution")),
          number(value.at("StageResearchPoints")),
          number(value.at("TotalResearchPoints")),
          value.at("Revision").get<std::int64_t>()};
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: adaptive_research_foreign_discovery_tests <fixture> "
          "<research-data>");
    std::ifstream input(argv[1]);
    if (!input)
      throw std::runtime_error("Could not open fixture.");
    json fixture;
    input >> fixture;
    const std::filesystem::path root(argv[2]);
    const auto canonical_fingerprint = directory_fingerprint(root);
    require(canonical_fingerprint ==
                fixture.at("CanonicalFingerprint").get<std::string>(),
            "Canonical research-data fingerprint differs.");

    auto authority = load_adaptive_research_authority(root);
    auto foreign_catalog =
        load_adaptive_research_foreign_technology_catalog(
            root, authority.catalog(), authority.expertise_catalog());
    AdaptiveResearchForeignTechnologyRuntime foreign(authority,
                                                      foreign_catalog);
    auto catalog = load_adaptive_research_foreign_discovery_catalog(
        root, authority.catalog());
    AdaptiveResearchForeignDiscoveryRuntime runtime(authority, foreign,
                                                     catalog);
    require_equal(discovery_catalog_json(catalog), fixture.at("Catalog"),
                  "Catalog");

    std::size_t catalog_count{};
    for (const auto &row : fixture.at("CatalogCases")) {
      const auto &retained = row.at("Input");
      const auto changed_bytes =
          decode_base64(retained.at("BytesBase64").get<std::string>());
      require(sha256(changed_bytes) ==
                  retained.at("BytesSha256").get<std::string>(),
              "Retained catalog bytes digest differs.");
      OwnedScratch scratch(root);
      const auto changed_path =
          scratch.path() / "foreign_research_materialization_policy.json";
      {
        std::ofstream changed(changed_path,
                              std::ios::binary | std::ios::trunc);
        require(changed.is_open(), "Could not open changed catalog file.");
        changed.write(changed_bytes.data(),
                      static_cast<std::streamsize>(changed_bytes.size()));
        changed.flush();
        require(changed.good(), "Could not write changed catalog file.");
      }
      require(directory_fingerprint(scratch.path()) ==
                  retained.at("BeforeFingerprint").get<std::string>(),
              "Catalog scratch before-fingerprint differs.");
      std::optional<AdaptiveResearchForeignDiscoveryCatalog> loaded;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        loaded.emplace(load_adaptive_research_foreign_discovery_catalog(
            scratch.path(), authority.catalog()));
      } catch (const AdaptiveResearchForeignCatalogDataError &caught) {
        error = {"InvalidDataException", caught.what()};
      } catch (const AdaptiveResearchForeignJsonOperationError &caught) {
        error = {"InvalidOperationException", caught.what()};
      } catch (const std::exception &caught) {
        error = {"UnexpectedNativeException", caught.what()};
      }
      require(directory_fingerprint(scratch.path()) ==
                  retained.at("AfterFingerprint").get<std::string>(),
              "Catalog load changed retained input.");
      require_equal(loaded ? discovery_catalog_json(*loaded) : json(nullptr),
                    row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(error ? json{{"Type", error->first},
                                 {"Message", error->second}}
                          : json(nullptr),
                    row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      ++catalog_count;
    }
    require(directory_fingerprint(root) == canonical_fingerprint,
            "Catalog cases changed canonical research data.");

    auto state =
        authority.create_civilization_state("fixture:foreign-discovery");
    std::size_t command_count{};
    for (const auto &row : fixture.at("Sequence")) {
      const auto &retained = row.at("Input");
      const auto op = retained.at("Op").get<std::string>();
      static constexpr std::string_view operations[] = {
          "Reevaluate", "Observe",       "Analysis",
          "Adaptation", "Acquire",       "SetNode",
          "ForeignObserve", "ForeignAnalysis"};
      require(std::ranges::find(operations, op) != std::end(operations),
              "Unknown discovery fixture operation.");
      const auto reference = retained.value("Reference", std::string{});
      const auto lineage = retained.value("Lineage", std::string{});
      const auto confidence = retained.contains("Confidence")
                                  ? number(retained.at("Confidence"))
                                  : 0.0;
      const auto year =
          retained.contains("Year") ? number(retained.at("Year")) : 0.0;
      const auto constraints =
          retained.contains("Constraints")
              ? retained.at("Constraints").get<std::vector<std::string>>()
              : std::vector<std::string>{};
      const auto context =
          retained.contains("Context")
              ? optional_string(retained.at("Context"))
              : std::nullopt;
      const auto understanding = static_cast<ForeignUnderstandingState>(
          retained.value("Understanding", 0));
      const auto adaptation = static_cast<ForeignAdaptationState>(
          retained.value("Adaptation", 0));
      const auto package = retained.contains("Package")
                               ? std::optional<ForeignTechnologyPackageInput>(
                                     package_input(retained.at("Package")))
                               : std::nullopt;
      const auto node = retained.contains("Value")
                            ? std::optional<ResearchNodeRuntimeState>(
                                  node_input(retained.at("Value")))
                            : std::nullopt;
      require_equal(state_json(state), row.at("CoreBefore"),
                    row.at("Name").get<std::string>() + ".CoreBefore");
      require_equal(foreign_json(foreign.state(state)),
                    row.at("ForeignBefore"),
                    row.at("Name").get<std::string>() + ".ForeignBefore");

      DiscoveryResult result;
      std::optional<std::pair<std::string, std::string>> error;
      try {
        if (op == "Reevaluate")
          result = runtime.reevaluate(
              state, reference,
              context ? std::optional<std::string_view>(*context)
                      : std::nullopt);
        else if (op == "Observe")
          result = runtime.observe(
              state, reference, lineage, confidence, year, constraints,
              context ? std::optional<std::string_view>(*context)
                      : std::nullopt);
        else if (op == "Analysis")
          result = runtime.record_analysis_result(
              state, reference, understanding, confidence, year, constraints,
              context ? std::optional<std::string_view>(*context)
                      : std::nullopt);
        else if (op == "Adaptation")
          result = runtime.record_adaptation_result(
              state, reference, adaptation, confidence, year,
              context ? std::optional<std::string_view>(*context)
                      : std::nullopt);
        else if (op == "Acquire")
          result = runtime.acquire_package(state, *package);
        else if (op == "SetNode") {
          detail::AdaptiveResearchStateWriter::set_node_state(state, *node);
          result = true;
        } else if (op == "ForeignObserve")
          result = foreign.observe(state, reference, lineage, confidence, year,
                                   constraints);
        else if (op == "ForeignAnalysis")
          result = foreign.record_analysis_result(
              state, reference, understanding, confidence, year, constraints);
      } catch (const std::exception &caught) {
        error = source_error(caught);
      }
      require_equal(discovery_result_json(result), row.at("Result"),
                    row.at("Name").get<std::string>() + ".Result");
      require_equal(error ? json{{"Type", error->first},
                                 {"Message", error->second}}
                          : json(nullptr),
                    row.at("Error"),
                    row.at("Name").get<std::string>() + ".Error");
      require_equal(state_json(state), row.at("CoreAfter"),
                    row.at("Name").get<std::string>() + ".CoreAfter");
      require_equal(foreign_json(foreign.state(state)), row.at("ForeignAfter"),
                    row.at("Name").get<std::string>() + ".ForeignAfter");
      ++command_count;
    }

    const auto &partial = fixture.at("PartialDelegation");
    const auto &partial_input = partial.at("Input");
    const auto partial_reference =
        partial_input.at("Reference").get<std::string>();
    const auto &partial_setup = partial_input.at("Setup");
    const auto partial_lineage =
        partial_setup.at("Lineage").get<std::string>();
    const auto partial_setup_confidence =
        number(partial_setup.at("Confidence"));
    const auto partial_setup_year = number(partial_setup.at("Year"));
    const auto partial_understanding = static_cast<ForeignUnderstandingState>(
        partial_input.at("Understanding").get<int>());
    const auto partial_confidence = number(partial_input.at("Confidence"));
    const auto partial_year = number(partial_input.at("Year"));
    const auto &retained_rule = partial_input.at("Rule");
    const auto partial_axis = static_cast<ForeignDiscoveryTriggerAxis>(
        retained_rule.at("TriggerAxis").get<int>());
    const auto partial_minimum =
        retained_rule.at("MinimumAxisRank").get<int>();
    const auto partial_nodes =
        retained_rule.at("NodeIds").get<std::vector<std::string>>();
    const auto partial_awareness = static_cast<ResearchMaturity>(
        retained_rule.at("AwarenessState").get<int>());
    auto partial_catalog = load_adaptive_research_foreign_discovery_catalog(
        root, authority.catalog());
    auto partial_rules = partial_catalog.method_rules();
    require(!partial_rules.empty(), "Partial fixture requires a method rule.");
    auto *mutable_rules = const_cast<ForeignResearchMethodCandidateRule *>(
        partial_rules.data());
    mutable_rules[0].trigger_axis = partial_axis;
    mutable_rules[0].minimum_axis_rank = partial_minimum;
    mutable_rules[0].node_ids = partial_nodes;
    mutable_rules[0].awareness_state = partial_awareness;
    for (std::size_t index = 1; index < partial_rules.size(); ++index)
      mutable_rules[index].minimum_axis_rank =
          std::numeric_limits<int>::max();
    AdaptiveResearchForeignDiscoveryRuntime partial_runtime(
        authority, foreign, partial_catalog);
    auto partial_state =
        authority.create_civilization_state("fixture:discovery-partial");
    (void)foreign.observe(partial_state, partial_reference, partial_lineage,
                          partial_setup_confidence, partial_setup_year);
    require_equal(state_json(partial_state), partial.at("CoreBefore"),
                  "PartialDelegation.CoreBefore");
    require_equal(foreign_json(foreign.state(partial_state)),
                  partial.at("ForeignBefore"),
                  "PartialDelegation.ForeignBefore");
    std::optional<ForeignResearchDiscoveryResult> partial_result;
    std::optional<std::pair<std::string, std::string>> partial_error;
    try {
      partial_result = partial_runtime.record_analysis_result(
          partial_state, partial_reference, partial_understanding,
          partial_confidence, partial_year);
    } catch (const std::exception &caught) {
      partial_error = source_error(caught);
    }
    require_equal(partial_result
                      ? discovery_result_json(DiscoveryResult(*partial_result))
                      : json(nullptr),
                  partial.at("Result"), "PartialDelegation.Result");
    require_equal(partial_error ? json{{"Type", partial_error->first},
                                       {"Message", partial_error->second}}
                                : json(nullptr),
                  partial.at("Error"), "PartialDelegation.Error");
    require_equal(state_json(partial_state), partial.at("CoreAfter"),
                  "PartialDelegation.CoreAfter");
    require_equal(foreign_json(foreign.state(partial_state)),
                  partial.at("ForeignAfter"),
                  "PartialDelegation.ForeignAfter");

    static_assert(!std::is_constructible_v<
                  AdaptiveResearchForeignDiscoveryRuntime,
                  AdaptiveResearchAuthority &&,
                  const AdaptiveResearchForeignTechnologyRuntime &,
                  const AdaptiveResearchForeignDiscoveryCatalog &>);
    static_assert(!std::is_constructible_v<
                  AdaptiveResearchForeignDiscoveryRuntime,
                  const AdaptiveResearchAuthority &,
                  AdaptiveResearchForeignTechnologyRuntime &&,
                  const AdaptiveResearchForeignDiscoveryCatalog &>);
    static_assert(!std::is_constructible_v<
                  AdaptiveResearchForeignDiscoveryRuntime,
                  const AdaptiveResearchAuthority &,
                  const AdaptiveResearchForeignTechnologyRuntime &,
                  AdaptiveResearchForeignDiscoveryCatalog &&>);
    auto alias_state =
        authority.create_civilization_state("fixture:discovery-alias");
    const std::vector<std::string> initial_constraints{
        foreign_catalog.constraint_ids().front()};
    (void)runtime.observe(alias_state, "foreign:alias", "lineage:alias", .7,
                          1, initial_constraints);
    const auto &aliased = foreign.state(alias_state).assessments()[0];
    const std::string_view aliased_reference =
        aliased.foreign_technology_reference;
    const std::span<const std::string> aliased_constraints =
        aliased.known_constraint_ids;
    const std::optional<std::string_view> aliased_context =
        aliased.source_lineage_reference;
    const auto alias_result = runtime.record_analysis_result(
        alias_state, aliased_reference,
        ForeignUnderstandingState::characterized, .8, 2,
        aliased_constraints, aliased_context);
    require(alias_result.assessment.foreign_technology_reference ==
                "foreign:alias" &&
                alias_result.assessment.known_constraint_ids ==
                    initial_constraints,
            "Discovery wrapper did not own aliased inputs before mutation.");
    auto moved_runtime = std::move(runtime);
    require(moved_runtime.reevaluate(alias_state, "foreign:alias").empty(),
            "Moved discovery coordinator changed retained dependencies.");

    std::cout << "adaptive_research_foreign_discovery_tests: "
              << command_count << " source commands and " << catalog_count
              << " catalog cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive_research_foreign_discovery_tests: "
              << typeid(error).name() << ": " << error.what()
              << "\nCWD: " << std::filesystem::current_path()
              << "\nFixture: " << (argc > 1 ? argv[1] : "<missing>")
              << "\nResearch root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
