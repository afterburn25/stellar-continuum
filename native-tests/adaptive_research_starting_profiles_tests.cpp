#include <stellar/core/adaptive_research_snapshot.hpp>
#include <stellar/core/adaptive_research_starting_profiles.hpp>

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
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

std::string sha256(std::span<const unsigned char> value) {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  DWORD object_size{};
  DWORD ignored{};
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
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(64);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 15]);
  }
  return result;
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
  if (std::isnan(value)) return "NaN";
  if (value == std::numeric_limits<double>::infinity()) return "Infinity";
  if (value == -std::numeric_limits<double>::infinity()) return "-Infinity";
  return value;
}

json runtime_event(const AdaptiveResearchRuntimeEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"CivilizationId", value.civilization_id},
          {"NodeId", optional(value.node_id)},
          {"SubjectId", optional(value.subject_id)},
          {"Message", value.message}};
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
  for (const auto &value : state.civilization_traits()) traits.push_back(value);
  auto contexts = json::array();
  for (const auto &value : state.applicability_contexts())
    contexts.push_back({{"Id", value.context_id}, {"Traits", value.sorted_traits}});
  auto capabilities = json::array();
  for (const auto &value : state.capabilities())
    capabilities.push_back({{"CapabilityId", value.capability_id},
                            {"ContextId", optional(value.context_id)}});
  auto facilities = json::array();
  for (const auto &value : state.facility_capabilities()) facilities.push_back(value);
  auto deployments = json::array();
  for (const auto &value : state.enabled_deployment_event_ids()) deployments.push_back(value);
  auto projects = json::array();
  for (const auto &value : state.active_projects())
    projects.push_back({{"NodeId", value.node_id},
                        {"Stage", static_cast<int>(value.stage)},
                        {"TargetApplicabilityContextId",
                         optional(value.target_applicability_context_id)},
                        {"AssignedEffectiveLabs", numeric(value.assigned_effective_labs)},
                        {"ReadinessEfficiency", numeric(value.readiness_efficiency)},
                        {"Paused", value.paused},
                        {"PauseReason", optional(value.pause_reason)},
                        {"StageResearchPoints", numeric(value.stage_research_points)},
                        {"TotalResearchPoints", numeric(value.total_research_points)},
                        {"Revision", value.revision}});
  return {{"CivilizationId", state.civilization_id()},
          {"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"DirectedProgramStageId", state.directed_program_stage_id()},
          {"TotalEffectiveResearchLabs", numeric(state.total_effective_research_labs())},
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

json result_json(const AdaptiveResearchStartingCompositionResult &value) {
  auto competence = json::array();
  for (const auto &[id, item] : value.deferred.field_competence)
    competence.push_back({{"Id", id},
                          {"Theoretical", numeric(item.theoretical)},
                          {"Experimental", numeric(item.experimental)},
                          {"Engineering", numeric(item.engineering)}});
  auto institutions = json::array();
  for (const auto &item : value.deferred.research_institutions)
    institutions.push_back({{"InstitutionArchetypeId", item.institution_archetype_id},
                            {"Count", item.count}});
  auto tacit = json::array();
  for (const auto &item : value.deferred.tacit_assets)
    tacit.push_back({{"AssetTypeId", item.asset_type_id},
                     {"Provenance", item.provenance},
                     {"ScopeRef", optional(item.scope_ref)}});
  auto events = json::array();
  for (const auto &item : value.initial_horizon_events)
    events.push_back(runtime_event(item));
  return {{"State", state_json(value.state)},
          {"Deferred", {{"FieldCompetence", std::move(competence)},
                        {"ResearchInstitutions", std::move(institutions)},
                        {"TacitAssets", std::move(tacit)},
                        {"SelectedFragmentIds", value.deferred.selected_fragment_ids},
                        {"ReferenceProfileId", value.deferred.reference_profile_id},
                        {"HistoricalNotes", optional(value.deferred.historical_notes)}}},
          {"InitialHorizonEvents", std::move(events)}};
}

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

class OwnedScratch final {
public:
  explicit OwnedScratch(const std::filesystem::path &canonical_root) {
    parent_ = std::filesystem::absolute(
        std::filesystem::temp_directory_path() / "stellar-gate052-owned");
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
      throw std::runtime_error("Could not establish exclusive scratch ownership.");
    for (const auto *name : {"starting_profile_index.json",
                             "starting_research_fragments.json",
                             "starting_biochemical_fragments.json",
                             "starting_reference_profiles.json",
                             "starting_biochemical_reference_profiles.json"})
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

std::string decode_base64(std::string_view value) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char character : value) {
    if (character == '=') break;
    const auto position = alphabet.find(character);
    if (position == std::string_view::npos)
      throw std::runtime_error("Fixture contains invalid base64.");
    buffer = (buffer << 6) | static_cast<std::uint32_t>(position);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return result;
}

void apply_retained_changes(const std::filesystem::path &scratch,
                            const json &row) {
  for (const auto &change : row.at("ChangedFiles")) {
    const auto relative = change.at("RelativePath").get<std::string>();
    const auto relative_path = std::filesystem::path(relative);
    require(!relative_path.is_absolute() && relative != "." && relative != ".." &&
                relative_path.filename() == relative,
            "Fixture changed-file path escaped scratch.");
    std::ofstream output(scratch / relative_path,
                         std::ios::binary | std::ios::trunc);
    require(output.is_open(), "Could not open retained changed file.");
    output << decode_base64(change.at("ContentBase64").get<std::string>());
    output.flush();
    require(output.good(), "Could not write retained changed file.");
  }
}


} // namespace

int main(int argc, char **argv) {
  const std::string fixture_diagnostic = argc > 1 ? argv[1] : "<missing>";
  const std::string research_diagnostic = argc > 2 ? argv[2] : "<missing>";
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture path and research directory.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]);
    const auto fixture = json::parse(bytes(fixture_path));

    static_assert(!std::is_copy_constructible_v<AdaptiveResearchStartingProfileComposer>);
    static_assert(std::is_move_constructible_v<AdaptiveResearchStartingProfileComposer>);
    static_assert(std::is_copy_constructible_v<AdaptiveResearchStartingCompositionResult>);
    static_assert(!std::is_constructible_v<AdaptiveResearchStartingProfileComposer,
                                           AdaptiveResearchRuntime &&,
                                           const std::filesystem::path &>);

    auto runtime = load_adaptive_research_runtime(root);
    AdaptiveResearchStartingProfileComposer composer(runtime, root);
    require(std::vector<std::string>(composer.reference_profile_ids().begin(),
                                     composer.reference_profile_ids().end()) ==
                fixture.at("ReferenceProfileIds").get<std::vector<std::string>>(),
            "Reference profile source order differed.");
    require(std::vector<std::string>(composer.fragment_ids().begin(),
                                     composer.fragment_ids().end()) ==
                fixture.at("FragmentIds").get<std::vector<std::string>>(),
            "Fragment source order differed.");
    auto moved_composer = std::move(composer);
    require(fingerprint(root) == fixture.at("CanonicalFingerprint").get<std::string>(),
            "Canonical input fingerprint differed before replay.");

    std::size_t replayed = 0;
    for (const auto &row : fixture.at("Rows")) {
      if (row.contains("Phase")) continue;
      const auto profile_id = row.at("ProfileId").get<std::string>();
      const auto civilization_id = row.at("CivilizationId").get<std::string>();
      const auto context_id = row.at("ContextId").get<std::string>();
      const auto before = fingerprint(root);
      std::optional<AdaptiveResearchStartingCompositionResult> actual;
      std::string production_error;
      try {
        actual.emplace(moved_composer.compose_reference_profile(
            civilization_id, profile_id, context_id));
      } catch (const std::exception &error) {
        production_error = error.what();
      }
      const auto after = fingerprint(root);
      require(before == after, "A production call changed canonical input bytes.");
      require(row.at("Error").is_null(), "Canonical source row unexpectedly failed.");
      require(production_error.empty(), "Canonical native row failed: " + production_error);
      require(actual.has_value(), "Canonical native row returned no result.");
      const auto projected = result_json(*actual);
      if (projected != row.at("Result")) {
        std::ofstream("work/052-research-starting-profiles/native-mismatch.json")
            << projected.dump(2);
        std::ofstream("work/052-research-starting-profiles/source-mismatch.json")
            << row.at("Result").dump(2);
        throw std::runtime_error("Canonical result differed for " + profile_id);
      }
      auto copy = *actual;
      require(result_json(copy) == row.at("Result"),
              "Owned composition result did not deep-copy.");
      copy.deferred.selected_fragment_ids.push_back("copy-only-fragment");
      (void)runtime.set_total_effective_research_labs(
          copy.state, copy.state.total_effective_research_labs() + 1.0);
      require(result_json(*actual) == row.at("Result"),
              "Mutating a result copy changed its source value.");
      require(result_json(copy) != row.at("Result"),
              "Result copy mutation was not independent.");
      ++replayed;
    }
    require(replayed == 8, "Expected all eight canonical profiles.");

    std::size_t malformed_replayed = 0;
    for (const auto &row : fixture.at("Rows")) {
      if (!row.contains("Phase")) continue;
      if (row.at("Phase") == "Invoke") {
        const auto before = fingerprint(root);
        const auto profile_id = row.at("ProfileId").get<std::string>();
        const auto civilization_id = row.at("CivilizationId").get<std::string>();
        const auto context_id = row.at("ContextId").get<std::string>();
        std::string native_category;
        std::string native_message;
        try {
          auto ignored = moved_composer.compose_reference_profile(
              civilization_id, profile_id, context_id);
          (void)ignored;
        } catch (const std::invalid_argument &error) {
          native_category = "ArgumentException";
          native_message = error.what();
        } catch (const std::out_of_range &error) {
          native_category = "KeyNotFoundException";
          native_message = error.what();
        }
        require(before == fingerprint(root),
                "A rejected invocation changed canonical input bytes.");
        const auto &expected = row.at("Error");
        require(native_category == expected.at("Type").get<std::string>(),
                "Mapped invocation category differed.");
        require(native_message == expected.at("Message").get<std::string>(),
                "Invocation error message differed: " + native_message);
        ++malformed_replayed;
        continue;
      }
      OwnedScratch scratch(root);
      const auto mutation = row.at("Mutation").get<std::string>();
      apply_retained_changes(scratch.path(), row);
      const auto before = fingerprint(scratch.path());
      require(before == row.at("BeforeFingerprint").get<std::string>(),
              "Retained malformed input fingerprint differed.");
      std::string native_category;
      std::string native_message;
      std::optional<AdaptiveResearchStartingCompositionResult> boundary_result;
      if (row.at("Phase") == "Load" || row.at("Phase") == "LoadJson" ||
          row.at("Phase") == "LoadShape") {
        try {
          AdaptiveResearchStartingProfileComposer malformed(runtime,
                                                             scratch.path());
          (void)malformed;
        } catch (const nlohmann::json::parse_error &error) {
          native_category = "JsonReaderException";
          native_message = error.what();
        } catch (const std::invalid_argument &error) {
          native_category = "ArgumentException";
          native_message = error.what();
        } catch (const std::overflow_error &error) {
          native_category = "OverflowException";
          native_message = error.what();
        } catch (const std::logic_error &error) {
          native_category = "InvalidOperationException";
          native_message = error.what();
        } catch (const std::runtime_error &error) {
          native_category = "InvalidDataException";
          native_message = error.what();
        }
      } else {
        AdaptiveResearchStartingProfileComposer malformed(runtime,
                                                           scratch.path());
        const auto profile_id = row.at("ProfileId").get<std::string>();
        const auto civilization_id = row.at("CivilizationId").get<std::string>();
        const auto context_id = row.at("ContextId").get<std::string>();
        try {
          boundary_result.emplace(malformed.compose_reference_profile(
              civilization_id, profile_id, context_id));
        } catch (const std::overflow_error &error) {
          native_category = "OverflowException";
          native_message = error.what();
        } catch (const std::runtime_error &error) {
          native_category = "InvalidDataException";
          native_message = error.what();
        }
      }
      const auto after = fingerprint(scratch.path());
      require(before == after, "A malformed production call changed input bytes.");
      require(after == row.at("AfterFingerprint").get<std::string>(),
              "Retained malformed output fingerprint differed.");
      const auto &expected = row.at("Error");
      if (row.at("Phase") == "ComposeValid") {
        require(expected.is_null(), "Valid source boundary unexpectedly failed.");
        require(native_message.empty(), "Valid native boundary unexpectedly failed.");
        require(boundary_result.has_value(), "Valid native boundary returned no result.");
        require(result_json(*boundary_result) == row.at("Result"),
                "Valid optional-field behavior differed.");
        ++malformed_replayed;
        continue;
      }
      require(!expected.is_null(), "Malformed source row unexpectedly succeeded.");
      require(native_category == expected.at("Type").get<std::string>(),
              "Mapped malformed error category differed for " + mutation);
      if (row.at("Phase") != "LoadJson")
        require(native_message == expected.at("Message").get<std::string>(),
                "Malformed error message differed for " + mutation + ": " +
                    native_message);
      ++malformed_replayed;
    }
    require(malformed_replayed == 42, "Expected all boundary source rows.");
    std::cout << "Gate 052 replayed " << replayed
              << " canonical and " << malformed_replayed
              << " boundary actual-source rows; fingerprint "
              << fixture.at("CanonicalFingerprint").get<std::string>() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "adaptive_research_starting_profiles_tests: "
              << typeid(error).name() << ": " << error.what()
              << " cwd=" << std::filesystem::current_path().string()
              << " fixture=" << fixture_diagnostic
              << " research-root=" << research_diagnostic << '\n';
    return 1;
  }
}
