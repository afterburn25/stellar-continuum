#include <stellar/core/adaptive_research_outcomes.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
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
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

using Json = nlohmann::json;
using namespace stellar::core;
using StateWriter = detail::AdaptiveResearchStateWriter;
using OutcomeWriter = detail::AdaptiveResearchOutcomeStateWriter;
using RuntimeAccess = detail::AdaptiveResearchOutcomeRuntimeTestAccess;

static_assert(std::is_move_constructible_v<AdaptiveResearchOutcomeCatalog>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchOutcomeCatalog>);
static_assert(std::is_move_constructible_v<AdaptiveResearchOutcomeRuntime>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchOutcomeRuntime>);
static_assert(!std::is_constructible_v<AdaptiveResearchOutcomeRuntime,
                                       AdaptiveResearchAuthority &&,
                                       const AdaptiveResearchOutcomeCatalog &>);
static_assert(!std::is_constructible_v<AdaptiveResearchOutcomeRuntime,
                                       const AdaptiveResearchAuthority &,
                                       AdaptiveResearchOutcomeCatalog &&>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchOutcomeRuntime, AdaptiveResearchAuthority &&,
              const AdaptiveResearchOutcomeCatalog &,
              const AdaptiveResearchPressureRuntime &>);
static_assert(!std::is_constructible_v<
              AdaptiveResearchOutcomeRuntime, const AdaptiveResearchAuthority &,
              AdaptiveResearchOutcomeCatalog &&,
              const AdaptiveResearchPressureRuntime &>);
static_assert(!std::is_constructible_v<AdaptiveResearchOutcomeRuntime,
                                       const AdaptiveResearchAuthority &,
                                       const AdaptiveResearchOutcomeCatalog &,
                                       AdaptiveResearchPressureRuntime &&>);

namespace {

void require(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}

std::string hex(std::span<const std::uint8_t> value) {
  constexpr std::string_view digits = "0123456789ABCDEF";
  std::string result;
  result.reserve(value.size() * 2);
  for (const auto byte : value) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
}

std::string sha256(std::string_view value) {
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()}));
}

std::string fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(root))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      files.push_back(entry.path());
  std::ranges::sort(files, [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::string combined;
  for (const auto &path : files) {
    combined += path.filename().string();
    combined += bytes(path);
  }
  return sha256(combined);
}

class OwnedScratch final {
public:
  explicit OwnedScratch(const std::filesystem::path &root) {
    parent_ = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path() / "stellar-gate058-owned");
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
    require(!path_.empty() && path_.is_absolute() &&
                path_.parent_path() == parent_,
            "Could not establish exclusive Outcome scratch ownership.");
    for (const auto *name : {"research_outcome_runtime_policy.json",
                             "maturation_model.json", "index.json"})
      std::filesystem::copy_file(root / name, path_ / name);
  }
  ~OwnedScratch() {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path_, error);
    if (!error && !path_.empty() && resolved.is_absolute() &&
        resolved.parent_path() == parent_)
      std::filesystem::remove_all(resolved, error);
  }
  const std::filesystem::path &path() const noexcept { return path_; }

private:
  std::filesystem::path parent_;
  std::filesystem::path path_;
};

std::string decode_base64(std::string_view value) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  std::uint32_t buffer{};
  int bits{};
  for (const char item : value) {
    if (item == '=')
      break;
    const auto position = alphabet.find(item);
    require(position != std::string_view::npos, "Invalid retained base64.");
    buffer = (buffer << 6) | static_cast<std::uint32_t>(position);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<char>((buffer >> bits) & 255));
    }
  }
  return result;
}

void apply_changes(const std::filesystem::path &scratch, const Json &row) {
  for (const auto &change : row.at("ChangedFiles")) {
    const auto relative = change.at("RelativePath").get<std::string>();
    const std::filesystem::path path(relative);
    require(!path.is_absolute() && relative != "." && relative != ".." &&
                path.filename() == path,
            "Retained change escaped Outcome scratch.");
    if (change.at("Deleted").get<bool>()) {
      require(std::filesystem::remove(scratch / path),
              "Could not delete retained Outcome file.");
      continue;
    }
    std::ofstream output(scratch / path, std::ios::binary | std::ios::trunc);
    require(output.is_open(), "Could not open retained Outcome file.");
    output << decode_base64(change.at("ContentBase64").get<std::string>());
    output.flush();
    require(output.good(), "Could not write retained Outcome file.");
  }
}

Json number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

Json optional_string(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

Json competence(const ResearchCompetenceVector &value) {
  return {{"Theoretical", number(value.theoretical)},
          {"Experimental", number(value.experimental)},
          {"Engineering", number(value.engineering)}};
}

Json field_json(const ResearchFieldCompetenceRuntimeState &value) {
  return {{"FieldId", value.field_id},
          {"Current", competence(value.current)},
          {"HistoricalPeak", competence(value.historical_peak)},
          {"LastTheoreticalActivityYear",
           number(value.last_theoretical_activity_year)},
          {"LastExperimentalActivityYear",
           number(value.last_experimental_activity_year)},
          {"LastEngineeringActivityYear",
           number(value.last_engineering_activity_year)},
          {"Revision", value.revision}};
}

Json node_json(const ResearchNodeRuntimeState &value) {
  return {{"NodeId", value.node_id},
          {"Maturity", static_cast<int>(value.maturity)},
          {"Resolution", optional_string(value.resolution)},
          {"StageResearchPoints", number(value.stage_research_points)},
          {"TotalResearchPoints", number(value.total_research_points)},
          {"Revision", value.revision},
          {"CountsAsEstablishedKnowledge",
           value.counts_as_established_knowledge()}};
}

Json project_json(const ResearchProjectRuntimeState &value) {
  return {{"NodeId", value.node_id},
          {"Stage", static_cast<int>(value.stage)},
          {"TargetApplicabilityContextId",
           optional_string(value.target_applicability_context_id)},
          {"AssignedEffectiveLabs", number(value.assigned_effective_labs)},
          {"ReadinessEfficiency", number(value.readiness_efficiency)},
          {"Paused", value.paused},
          {"PauseReason", optional_string(value.pause_reason)},
          {"StageResearchPoints", number(value.stage_research_points)},
          {"TotalResearchPoints", number(value.total_research_points)},
          {"Revision", value.revision}};
}

Json state_projection(const AdaptiveResearchCivilizationState &state) {
  Json nodes = Json::array();
  for (const auto &value : state.node_states())
    nodes.push_back(node_json(value));
  Json projects = Json::array();
  for (const auto &value : state.active_projects())
    projects.push_back(project_json(value));
  Json pressures = Json::array();
  for (const auto &value : state.pressures())
    pressures.push_back(
        {{"Key", value.pressure_id}, {"Value", number(value.value)}});
  Json fields = Json::array();
  for (const auto &value : state.expertise().field_competence())
    fields.push_back(field_json(value));
  return {{"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"Nodes", std::move(nodes)},
          {"Projects", std::move(projects)},
          {"Pressures", std::move(pressures)},
          {"ExpertiseRevision", state.expertise().revision()},
          {"Fields", std::move(fields)}};
}

std::string profile_name(ResearchUncertaintyProfile value) {
  switch (value) {
  case ResearchUncertaintyProfile::established_extension:
    return "EstablishedExtension";
  case ResearchUncertaintyProfile::frontier_engineering:
    return "FrontierEngineering";
  case ResearchUncertaintyProfile::scientific_hypothesis:
    return "ScientificHypothesis";
  case ResearchUncertaintyProfile::hazardous_foreign_or_anomalous:
    return "HazardousForeignOrAnomalous";
  }
  throw std::out_of_range("Unknown profile.");
}

std::string outcome_name(ResearchOutcomeKind value) {
  switch (value) {
  case ResearchOutcomeKind::progress:
    return "Progress";
  case ResearchOutcomeKind::setback:
    return "Setback";
  case ResearchOutcomeKind::partial_success:
    return "PartialSuccess";
  case ResearchOutcomeKind::hypothesis_supported:
    return "HypothesisSupported";
  case ResearchOutcomeKind::hypothesis_refined:
    return "HypothesisRefined";
  case ResearchOutcomeKind::hypothesis_disproven:
    return "HypothesisDisproven";
  case ResearchOutcomeKind::anomalous_result:
    return "AnomalousResult";
  case ResearchOutcomeKind::hazard_incident:
    return "HazardIncident";
  case ResearchOutcomeKind::side_discovery:
    return "SideDiscovery";
  }
  throw std::out_of_range("Unknown outcome.");
}

Json catalog_json(const AdaptiveResearchOutcomeCatalog &catalog) {
  const auto &policy = catalog.policy();
  Json weights = Json::object();
  for (const auto &profile : policy.base_weights) {
    Json entries = Json::object();
    for (const auto &entry : profile.weights)
      entries[outcome_name(entry.outcome)] = number(entry.weight);
    weights[profile_name(profile.profile)] = std::move(entries);
  }
  Json gains = Json::object();
  for (const auto &entry : policy.competence_gains)
    gains[outcome_name(entry.outcome)] = {
        {"Theoretical", number(entry.gain.theoretical)},
        {"Experimental", number(entry.gain.experimental)},
        {"Engineering", number(entry.gain.engineering)}};
  Json side = Json::array();
  for (const auto &entry : catalog.side_discovery_candidates())
    side.push_back(
        {{"NodeId", entry.node_id}, {"Candidates", entry.candidate_node_ids}});
  return {
      {"Policy",
       {{"SetbackStageProgressLossFraction",
         number(policy.setback_stage_progress_loss_fraction)},
        {"PartialSuccessStageProgressCreditFraction",
         number(policy.partial_success_stage_progress_credit_fraction)},
        {"RefinedHypothesisStageProgressPreservedFraction",
         number(policy.refined_hypothesis_stage_progress_preserved_fraction)},
        {"HighReadinessRiskReductionMaxFraction",
         number(policy.high_readiness_risk_reduction_max_fraction)},
        {"MaxSideDiscoveryCandidates", policy.max_side_discovery_candidates},
        {"MaxMaterializedSideDiscoveries",
         policy.max_materialized_side_discoveries},
        {"MinimumSharedKnowledgeFields",
         policy.minimum_shared_knowledge_fields},
        {"SameSolutionFamilyDepthWindow",
         policy.same_solution_family_depth_window},
        {"MaxRecentOutcomeRecords", policy.max_recent_outcome_records},
        {"MaxPerNodeRecentRecords", policy.max_per_node_recent_records},
        {"BaseWeights", std::move(weights)},
        {"CompetenceGains", std::move(gains)},
        {"FrontierEngineeringNodeIds", policy.frontier_engineering_node_ids},
        {"HazardousNodeIds", policy.hazardous_node_ids}}},
      {"SideDiscoveryCandidatesByNode", std::move(side)}};
}

Json planned_json(const PlannedResearchOutcome &value) {
  return {{"NodeId", value.node_id},
          {"CheckpointId", value.checkpoint_id},
          {"AttemptIndex", value.attempt_index},
          {"Profile", static_cast<int>(value.profile)},
          {"Outcome", static_cast<int>(value.outcome)},
          {"DeterministicRoll", number(value.deterministic_roll)},
          {"PlannedSideDiscoveryNodeId",
           optional_string(value.planned_side_discovery_node_id)},
          {"Explanation", value.explanation}};
}

Json runtime_event_json(const AdaptiveResearchRuntimeEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"CivilizationId", value.civilization_id},
          {"NodeId", optional_string(value.node_id)},
          {"SubjectId", optional_string(value.subject_id)},
          {"Message", value.message}};
}

Json outcome_event_json(const AdaptiveResearchOutcomeEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"CivilizationId", value.civilization_id},
          {"NodeId", value.node_id},
          {"SubjectId", optional_string(value.subject_id)},
          {"Message", value.message}};
}

Json application_json(const ResearchOutcomeApplicationResult &value) {
  Json research = Json::array();
  for (const auto &event : value.research_events)
    research.push_back(runtime_event_json(event));
  Json outcomes = Json::array();
  for (const auto &event : value.outcome_events)
    outcomes.push_back(outcome_event_json(event));
  return {{"Accepted", value.accepted},
          {"Resolution",
           value.resolution ? planned_json(*value.resolution) : Json(nullptr)},
          {"ResearchEvents", std::move(research)},
          {"OutcomeEvents", std::move(outcomes)},
          {"Message", value.message}};
}

Json summary_json(const ResearchOutcomeNodeSummary &value) {
  return {{"NodeId", value.node_id},
          {"Attempts", value.attempts},
          {"Setbacks", value.setbacks},
          {"PartialSuccesses", value.partial_successes},
          {"Refinements", value.refinements},
          {"Disproofs", value.disproofs},
          {"Anomalies", value.anomalies},
          {"Hazards", value.hazards},
          {"SideDiscoveries", value.side_discoveries},
          {"LastOutcomeId", optional_string(value.last_outcome_id)},
          {"LastOutcomeYear", number(value.last_outcome_year)}};
}

Json history_json(const ResearchOutcomeHistoryRecord &value) {
  return {
      {"Sequence", value.sequence},
      {"NodeId", value.node_id},
      {"CheckpointId", value.checkpoint_id},
      {"AttemptIndex", value.attempt_index},
      {"Outcome", static_cast<int>(value.outcome)},
      {"SideDiscoveryNodeId", optional_string(value.side_discovery_node_id)},
      {"Year", number(value.year)},
      {"Explanation", value.explanation}};
}

Json outcome_state_json(const AdaptiveResearchOutcomeState &state) {
  Json summaries = Json::array();
  for (const auto &value : state.summaries())
    summaries.push_back(summary_json(value));
  Json records = Json::array();
  for (const auto &value : state.recent_records())
    records.push_back(history_json(value));
  return {{"Revision", state.revision()},
          {"NextSequence", state.next_sequence()},
          {"Summaries", std::move(summaries)},
          {"RecentRecords", std::move(records)}};
}

struct Error {
  std::string type;
  std::string message;
};

template <class Call> Error caught(Call &&call) {
  try {
    call();
    return {};
  } catch (const AdaptiveResearchOutcomeFileError &error) {
    return {"FileNotFoundException", error.what()};
  } catch (const AdaptiveResearchOutcomeJsonError &error) {
    switch (error.kind()) {
    case AdaptiveResearchOutcomeJsonErrorKind::reader:
      return {"JsonReaderException", error.what()};
    case AdaptiveResearchOutcomeJsonErrorKind::invalid_operation:
      return {"InvalidOperationException", error.what()};
    case AdaptiveResearchOutcomeJsonErrorKind::format:
      return {"FormatException", error.what()};
    case AdaptiveResearchOutcomeJsonErrorKind::missing_property:
      return {"KeyNotFoundException", error.what()};
    }
  } catch (const AdaptiveResearchOutcomeCatalogError &error) {
    return {"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchOutcomeHistoryRangeError &error) {
    return {"ArgumentOutOfRangeException", error.what()};
  } catch (const AdaptiveResearchOutcomeArgumentRangeError &error) {
    return {"ArgumentOutOfRangeException", error.what()};
  } catch (const AdaptiveResearchOutcomeIndexError &error) {
    return {"IndexOutOfRangeException", error.what()};
  } catch (const std::overflow_error &error) {
    return {"OverflowException", error.what()};
  } catch (const std::invalid_argument &error) {
    return {"ArgumentException", error.what()};
  } catch (const std::out_of_range &error) {
    return {"KeyNotFoundException", error.what()};
  }
  throw std::logic_error("Unreachable Outcome error mapping.");
}

bool exact_message_category(std::string_view type) {
  return type == "InvalidDataException" || type == "ArgumentException" ||
         type == "KeyNotFoundException" ||
         type == "ArgumentOutOfRangeException" ||
         type == "IndexOutOfRangeException";
}

void compare_error(const Error &actual, const Json &expected,
                   const std::string &name) {
  if (expected.is_null()) {
    require(actual.type.empty(),
            name + " unexpectedly failed: " + actual.message);
    return;
  }
  const auto type = expected.at("Type").get<std::string>();
  require(actual.type == type,
          name + " error type differed: " + actual.type + " != " + type);
  if (exact_message_category(type))
    require(actual.message == expected.at("Message").get<std::string>(),
            name + " error message differed: " + actual.message);
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture and research-root paths.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(bytes(fixture_path));
    require(fingerprint(root) ==
                fixture.at("CanonicalFingerprint").get<std::string>(),
            "Canonical Outcome fingerprint differs.");
    auto authority = load_adaptive_research_authority(root);
    auto pressure_catalog =
        load_adaptive_research_pressure_catalog(root, authority.catalog());
    AdaptiveResearchPressureRuntime pressure(authority.kernel(),
                                             pressure_catalog);
    auto catalog_initial =
        load_adaptive_research_outcome_catalog(root, authority.catalog());
    auto catalog = std::move(catalog_initial);
    AdaptiveResearchOutcomeRuntime initial(authority, catalog, pressure);
    AdaptiveResearchOutcomeRuntime runtime(std::move(initial));
    auto common = authority.compose_reference_profile(
        "fixture:outcome", "reference_humanlike_solar_2050", "fixture:context",
        2050);
    auto common_state = std::move(common.state);
    std::size_t checked{};
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      if (name == "catalog-canonical") {
        require(catalog_json(catalog) == row.at("Result"), name + " differed");
      } else if (name.starts_with("sha256-")) {
        require(sha256(row.at("Input").get<std::string>()) ==
                    row.at("Result").get<std::string>(),
                name + " differed");
      } else if (name.starts_with("plan-") &&
                 !name.starts_with("plan-blank-") &&
                 name != "plan-unknown-node" &&
                 name != "plan-side-discovery-wrap") {
        const auto node_id = row.at("Input").at("Id").get<std::string>();
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        std::optional<PlannedResearchOutcome> value;
        Error error = caught([&] {
          value.emplace(runtime.plan_outcome(common_state, node_id, seed,
                                             "fixture_checkpoint"));
        });
        require(error.type.empty(), name + " failed: " + error.message);
        require(planned_json(*value) == row.at("Result"), name + " differed");
      } else if (name.starts_with("apply-") &&
                 !name.starts_with("apply-progress-year-") &&
                 name != "apply-anomalous-without-pressure-runtime" &&
                 name != "apply-invalid-outcome-enum") {
        const auto node_id = row.at("Input").at("Id").get<std::string>();
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        const auto outcome = static_cast<ResearchOutcomeKind>(
            row.at("Input").at("Outcome").get<int>());
        const auto &node = authority.catalog().get_node(node_id);
        auto composition = authority.compose_reference_profile(
            "fixture:apply:" + outcome_name(outcome),
            "reference_humanlike_solar_2050", "fixture:context", 2050);
        auto state = std::move(composition.state);
        const bool paused = node.is_hypothesis;
        StateWriter::set_node_state(state,
                                    {node_id, ResearchMaturity::experimental,
                                     std::string("fixture"), 12, 25, 0});
        StateWriter::set_project(
            state, {node_id, ResearchMaturity::experimental, std::nullopt,
                    static_cast<double>(
                        std::max(node.project_requirements.minimum_labs, 1)),
                    1, paused,
                    paused ? std::optional<std::string>(
                                 "hypothesis_resolution_required")
                           : std::nullopt,
                    12, 25, 0});
        std::optional<ResearchOutcomeApplicationResult> value;
        Error error = caught([&] {
          if (paused)
            value.emplace(runtime.resolve_pending_hypothesis(state, node_id,
                                                             seed, 2125.5));
          else
            value.emplace(runtime.resolve_active_checkpoint(
                state, node_id, seed, "fixture_checkpoint", 2125.5));
        });
        require(error.type.empty(), name + " failed: " + error.message);
        const Json projected = {
            {"Result", application_json(*value)},
            {"State", state_projection(state)},
            {"OutcomeState", outcome_state_json(runtime.state(state))}};
        require(projected == row.at("Result"), name + " differed");
      } else if (name == "plan-side-discovery-wrap") {
        const auto node_id = row.at("Input").at("NodeId").get<std::string>();
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        const auto occupied = row.at("Input").at("Occupied").get<std::string>();
        auto composition = authority.compose_reference_profile(
            "fixture:outcome", "reference_humanlike_solar_2050",
            "fixture:context", 2050);
        auto state = std::move(composition.state);
        StateWriter::set_node_state(state,
                                    {occupied, ResearchMaturity::hypothesized,
                                     std::string("occupied"), 0, 0, 0});
        std::optional<PlannedResearchOutcome> value;
        Error error = caught([&] {
          value.emplace(
              runtime.plan_outcome(state, node_id, seed, "fixture_checkpoint"));
        });
        require(error.type.empty(), name + " failed: " + error.message);
        require(planned_json(*value) == row.at("Result"), name + " differed");
      } else if (name.starts_with("apply-progress-year-")) {
        const auto node_id = row.at("Input").at("Id").get<std::string>();
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        const auto year_json = row.at("Input").at("Year");
        const auto year =
            year_json.is_string()
                ? (year_json == "NaN" ? std::numeric_limits<double>::quiet_NaN()
                   : year_json == "Infinity"
                       ? std::numeric_limits<double>::infinity()
                       : -std::numeric_limits<double>::infinity())
                : year_json.get<double>();
        const auto label =
            name.substr(std::string("apply-progress-year-").size());
        const auto &node = authority.catalog().get_node(node_id);
        auto composition = authority.compose_reference_profile(
            "fixture:year:" + label, "reference_humanlike_solar_2050",
            "fixture:context", 2050);
        auto state = std::move(composition.state);
        StateWriter::set_node_state(state,
                                    {node_id, ResearchMaturity::experimental,
                                     std::string("fixture"), 12, 25, 0});
        StateWriter::set_project(
            state, {node_id, ResearchMaturity::experimental, std::nullopt,
                    static_cast<double>(
                        std::max(node.project_requirements.minimum_labs, 1)),
                    1, false, std::nullopt, 12, 25, 0});
        std::optional<ResearchOutcomeApplicationResult> value;
        Error error = caught([&] {
          value.emplace(runtime.resolve_active_checkpoint(
              state, node_id, seed, "fixture_checkpoint", year));
        });
        require(error.type.empty(), name + " failed: " + error.message);
        const Json projected = {
            {"Result", application_json(*value)},
            {"State", state_projection(state)},
            {"OutcomeState", outcome_state_json(runtime.state(state))}};
        require(projected == row.at("Result"), name + " differed");
      } else if (name == "apply-anomalous-without-pressure-runtime") {
        const auto node_id = row.at("Input").at("Id").get<std::string>();
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        const auto &node = authority.catalog().get_node(node_id);
        auto composition = authority.compose_reference_profile(
            "fixture:no-pressure", "reference_humanlike_solar_2050",
            "fixture:context", 2050);
        auto state = std::move(composition.state);
        StateWriter::set_node_state(state,
                                    {node_id, ResearchMaturity::experimental,
                                     std::string("fixture"), 12, 25, 0});
        StateWriter::set_project(
            state, {node_id, ResearchMaturity::experimental, std::nullopt,
                    static_cast<double>(
                        std::max(node.project_requirements.minimum_labs, 1)),
                    1, true, std::string("hypothesis_resolution_required"), 12,
                    25, 0});
        AdaptiveResearchOutcomeRuntime without_pressure(authority, catalog);
        std::optional<ResearchOutcomeApplicationResult> value;
        Error error = caught([&] {
          value.emplace(without_pressure.resolve_pending_hypothesis(
              state, node_id, seed, 2130));
        });
        require(error.type.empty(), name + " failed: " + error.message);
        const Json projected = {
            {"Result", application_json(*value)},
            {"State", state_projection(state)},
            {"OutcomeState",
             outcome_state_json(without_pressure.state(state))}};
        require(projected == row.at("Result"), name + " differed");
      } else if (name == "outcome-id-invalid-enum") {
        Error error = caught([&] {
          (void)AdaptiveResearchOutcomeCatalog::outcome_id(
              static_cast<ResearchOutcomeKind>(999));
        });
        compare_error(error, row.at("Error"), name);
      } else if (name == "apply-invalid-outcome-enum") {
        const auto node_id = row.at("Input").at("Id").get<std::string>();
        const auto &node = authority.catalog().get_node(node_id);
        auto composition = authority.compose_reference_profile(
            "fixture:invalid-outcome", "reference_humanlike_solar_2050",
            "fixture:context", 2050);
        auto state = std::move(composition.state);
        StateWriter::set_node_state(state,
                                    {node_id, ResearchMaturity::experimental,
                                     std::string("fixture"), 12, 25, 0});
        StateWriter::set_project(
            state, {node_id, ResearchMaturity::experimental, std::nullopt,
                    static_cast<double>(
                        std::max(node.project_requirements.minimum_labs, 1)),
                    1, false, std::nullopt, 12, 25, 0});
        const auto project = state.active_projects().back();
        PlannedResearchOutcome resolution{
            node_id,
            "invalid",
            0,
            ResearchUncertaintyProfile::frontier_engineering,
            static_cast<ResearchOutcomeKind>(999),
            0,
            std::nullopt,
            "invalid"};
        Error error = caught([&] {
          (void)RuntimeAccess::apply(runtime, state, project, resolution, 2200);
        });
        compare_error(error, row.at("Error"), name);
        require(state_projection(state) == row.at("State"),
                name + " mutated state before rejecting the enum");
      } else if (name == "reject-pending-no-project") {
        const auto node_id = row.at("Input").at("NodeId").get<std::string>();
        std::optional<ResearchOutcomeApplicationResult> value;
        Error error = caught([&] {
          value.emplace(runtime.resolve_pending_hypothesis(
              common_state, node_id, "seed", 2100));
        });
        require(error.type.empty(), name + " failed");
        require(application_json(*value) == row.at("Result"),
                name + " differed");
      } else if (name == "reject-active-no-project") {
        const auto node_id = row.at("Input").at("NodeId").get<std::string>();
        std::optional<ResearchOutcomeApplicationResult> value;
        Error error = caught([&] {
          value.emplace(runtime.resolve_active_checkpoint(
              common_state, node_id, "seed", "checkpoint", 2100));
        });
        require(error.type.empty(), name + " failed");
        require(application_json(*value) == row.at("Result"),
                name + " differed");
      } else if (name.starts_with("plan-blank-seed-")) {
        const auto seed = row.at("Input").at("Seed").get<std::string>();
        const auto node_id = authority.catalog().nodes().front().id;
        Error error = caught([&] {
          (void)runtime.plan_outcome(common_state, node_id, seed, "checkpoint");
        });
        compare_error(error, row.at("Error"), name);
      } else if (name.starts_with("plan-blank-checkpoint-")) {
        const auto checkpoint =
            row.at("Input").at("Checkpoint").get<std::string>();
        const auto node_id = authority.catalog().nodes().front().id;
        Error error = caught([&] {
          (void)runtime.plan_outcome(common_state, node_id, "seed", checkpoint);
        });
        compare_error(error, row.at("Error"), name);
      } else if (name == "plan-unknown-node") {
        Error error = caught([&] {
          (void)runtime.plan_outcome(common_state, "unknown_node", "seed",
                                     "checkpoint");
        });
        compare_error(error, row.at("Error"), name);
      } else if (name == "state-bounded-history") {
        AdaptiveResearchOutcomeState state;
        OutcomeWriter::record(state, "node-a", "checkpoint", 0,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 2, 1);
        OutcomeWriter::record(state, "node-b", "checkpoint", 0,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 2, 1);
        OutcomeWriter::record(state, "node-a", "checkpoint", 1,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 2, 1);
        require(outcome_state_json(state) == row.at("Result"),
                name + " differed");
      } else if (name == "state-interleaved-per-node-zero") {
        AdaptiveResearchOutcomeState state;
        OutcomeWriter::record(state, "node-a", "checkpoint", 0,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 10, 10);
        OutcomeWriter::record(state, "node-b", "checkpoint", 0,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 10, 10);
        OutcomeWriter::record(state, "node-a", "checkpoint", 1,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 10, 0);
        require(outcome_state_json(state) == row.at("Result"),
                name + " differed");
      } else if (name == "state-negative-per-node") {
        AdaptiveResearchOutcomeState state;
        OutcomeWriter::record(state, "node-a", "checkpoint", 0,
                              ResearchOutcomeKind::progress, std::nullopt,
                              2100.25, "history", 10, -1);
        require(outcome_state_json(state) == row.at("Result"),
                name + " differed");
      } else if (name == "state-negative-recent-partial") {
        AdaptiveResearchOutcomeState state;
        Error error = caught([&] {
          OutcomeWriter::record(state, "node-a", "checkpoint", 0,
                                ResearchOutcomeKind::progress, std::nullopt,
                                2100.25, "history", -1, 1);
        });
        require(outcome_state_json(state) == row.at("Result"),
                name + " differed");
        compare_error(error, row.at("Error"), name);
      } else if (row.value("Phase", std::string{}) == "Load") {
        OwnedScratch scratch(root);
        apply_changes(scratch.path(), row);
        require(fingerprint(scratch.path()) ==
                    row.at("BeforeFingerprint").get<std::string>(),
                name + " retained input fingerprint differed");
        std::optional<AdaptiveResearchOutcomeCatalog> loaded;
        Error error = caught([&] {
          loaded.emplace(load_adaptive_research_outcome_catalog(
              scratch.path(), authority.catalog()));
        });
        const Json result = loaded ? Json{{"Loaded", true}} : Json(nullptr);
        require(fingerprint(scratch.path()) ==
                    row.at("AfterFingerprint").get<std::string>(),
                name + " mutated retained input");
        require(result == row.at("Result"), name + " result differed");
        if (name == "load-overflow-weight") {
          require(error.type == "FormatException" &&
                      error.message.find("number overflow") !=
                          std::string::npos,
                  name + " did not preserve the documented native JSON-number "
                         "boundary");
        } else {
          compare_error(error, row.at("Error"), name);
        }
      } else {
        throw std::runtime_error("Unhandled actual-source row '" + name + "'.");
      }
      require(row.at("BeforeFingerprint") == row.at("AfterFingerprint"),
              name + " source call mutated its inputs");
      ++checked;
    }

    AdaptiveResearchOutcomeState boundary_state;
    const auto boundary_before = outcome_state_json(boundary_state);
    Error attempt_overflow = caught([&] {
      OutcomeWriter::record(boundary_state, "overflow", "checkpoint",
                            std::numeric_limits<int>::max(),
                            ResearchOutcomeKind::progress, std::nullopt, 2200,
                            "overflow", 4, 2);
    });
    require(attempt_overflow.type == "OverflowException" &&
                outcome_state_json(boundary_state) == boundary_before,
            "Attempt overflow was not rejected before mutation.");
    Error sequence_overflow = caught([&] {
      OutcomeWriter::restore_record(
          boundary_state,
          {std::numeric_limits<std::int64_t>::max(), "overflow", "checkpoint",
           0, ResearchOutcomeKind::progress, std::nullopt, 2200, "overflow"});
    });
    require(sequence_overflow.type == "OverflowException" &&
                outcome_state_json(boundary_state) == boundary_before,
            "Sequence overflow was not rejected before mutation.");
    OutcomeWriter::restore_summary(
        boundary_state,
        {"counter", 0, std::numeric_limits<int>::max(), 0, 0, 0, 0, 0, 0,
         std::nullopt, -std::numeric_limits<double>::infinity()});
    const auto counter_before = outcome_state_json(boundary_state);
    Error counter_overflow = caught([&] {
      OutcomeWriter::record(boundary_state, "counter", "checkpoint", 0,
                            ResearchOutcomeKind::setback, std::nullopt, 2200,
                            "counter", 4, 2);
    });
    require(counter_overflow.type == "OverflowException" &&
                outcome_state_json(boundary_state) == counter_before,
            "Counter overflow was not rejected before mutation.");

    const auto &alias_node_definition =
        authority.catalog().get_node("prototype_warp_drive");
    auto alias_composition = authority.compose_reference_profile(
        "fixture:alias", "reference_humanlike_solar_2050", "fixture:context",
        2050);
    auto alias_state = std::move(alias_composition.state);
    StateWriter::set_node_state(
        alias_state, {alias_node_definition.id, ResearchMaturity::experimental,
                      std::string("fixture_checkpoint"), 12, 25, 0});
    std::string alias_seed;
    for (int index = 0; index < 300000; ++index) {
      const auto candidate = "fixture:alias:" + std::to_string(index);
      if (runtime
              .plan_outcome(alias_state, alias_node_definition.id, candidate,
                            "fixture_checkpoint")
              .outcome == ResearchOutcomeKind::progress) {
        alias_seed = candidate;
        break;
      }
    }
    require(!alias_seed.empty(), "Could not find alias regression seed.");
    StateWriter::set_project(
        alias_state,
        {alias_node_definition.id, ResearchMaturity::experimental, std::nullopt,
         static_cast<double>(std::max(
             alias_node_definition.project_requirements.minimum_labs, 1)),
         1, false, alias_seed, 12, 25, 0});
    const std::string_view node_alias =
        alias_state.active_projects().back().node_id;
    const std::string_view seed_alias =
        *alias_state.active_projects().back().pause_reason;
    const std::string_view checkpoint_alias =
        *alias_state.node_states().back().resolution;
    const auto alias_result = runtime.resolve_active_checkpoint(
        alias_state, node_alias, seed_alias, checkpoint_alias, 2200);
    require(alias_result.accepted &&
                alias_result.resolution->node_id == "prototype_warp_drive" &&
                alias_result.resolution->checkpoint_id == "fixture_checkpoint",
            "Caller-borrowed Outcome strings did not survive mutation.");

    const auto weak_before = RuntimeAccess::state_count(runtime);
    {
      auto ephemeral = authority.create_civilization_state("fixture:ephemeral");
      (void)runtime.state(ephemeral);
      require(RuntimeAccess::state_count(runtime) == weak_before + 1,
              "Outcome weak state did not create one entry.");
    }
    RuntimeAccess::maintain(runtime, 10000);
    require(RuntimeAccess::state_count(runtime) <= weak_before,
            "Outcome weak state retained a dead civilization.");

    auto copy = runtime.state(common_state);
    OutcomeWriter::record(copy, "copy-only", "copy", 0,
                          ResearchOutcomeKind::progress, std::nullopt, 2200,
                          "copy", 4, 2);
    require(runtime.state(common_state).summaries().empty(),
            "Outcome state copy mutated runtime-owned support.");
    AdaptiveResearchOutcomeRuntime assigned(authority, catalog, pressure);
    assigned = std::move(runtime);
    const auto move_plan = assigned.plan_outcome(
        common_state, authority.catalog().nodes().front().id, "move-seed",
        "move-checkpoint");
    require(!move_plan.node_id.empty(),
            "Moved Outcome runtime did not execute.");
    auto survivor = std::make_unique<AdaptiveResearchOutcomeRuntime>(
        authority, catalog, pressure);
    {
      AdaptiveResearchOutcomeRuntime other(authority, catalog, pressure);
      (void)other.plan_outcome(common_state,
                               authority.catalog().nodes().front().id,
                               "other-seed", "other-checkpoint");
    }
    require(!survivor
                 ->plan_outcome(common_state,
                                authority.catalog().nodes().front().id,
                                "survivor-seed", "survivor-checkpoint")
                 .node_id.empty(),
            "Shared-dependency Outcome runtime failed after peer destruction.");
    auto copied_civilization = common_state;
    require(assigned.state(copied_civilization).summaries().empty(),
            "Copied civilization reused Outcome weak identity.");
    RuntimeAccess::maintain(assigned, 0);
    require(RuntimeAccess::state_count(assigned) >= 1,
            "Outcome weak-state maintenance removed live state.");

    require(checked == fixture.at("Rows").size(),
            "Not all Outcome rows replayed.");
    std::cout << "validated " << checked << " actual-source Outcome rows\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd=" << std::filesystem::current_path() << '\n'
              << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n'
              << "research-root=" << (argc > 2 ? argv[2] : "<missing>") << '\n';
    return 1;
  }
}
