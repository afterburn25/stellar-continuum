#include <stellar/core/adaptive_research_expertise_snapshot.hpp>

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <span>
#include <type_traits>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

static_assert(std::is_move_constructible_v<AdaptiveResearchSnapshotV2Codec>);
static_assert(std::is_move_assignable_v<AdaptiveResearchSnapshotV2Codec>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchSnapshotV2Codec>);
static_assert(!std::is_copy_assignable_v<AdaptiveResearchSnapshotV2Codec>);
static_assert(!std::is_constructible_v<AdaptiveResearchSnapshotV2Codec,
                                       AdaptiveResearchAuthority &&>);

namespace {

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open fixture '" + path.string() + "'.");
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
  std::ranges::sort(files, [](const auto &left, const auto &right) {
    return left.filename().string() < right.filename().string();
  });
  std::string combined;
  for (const auto &path : files) {
    combined += path.filename().string();
    combined += read_bytes(path);
  }
  return sha256({reinterpret_cast<const unsigned char *>(combined.data()),
                 combined.size()});
}

void require(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

Json maybe(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

Json projection(const AdaptiveResearchCivilizationState &state) {
  Json nodes = Json::array();
  for (const auto &value : state.node_states())
    nodes.push_back({{"NodeId", value.node_id}, {"Maturity", static_cast<int>(value.maturity)},
                     {"Resolution", maybe(value.resolution)},
                     {"StageResearchPoints", value.stage_research_points},
                     {"TotalResearchPoints", value.total_research_points},
                     {"Revision", value.revision},
                     {"CountsAsEstablishedKnowledge", value.counts_as_established_knowledge()}});
  Json pressures = Json::array();
  for (const auto &value : state.pressures())
    pressures.push_back({{"Id", value.pressure_id}, {"Value", value.value}});
  Json evidence = Json::array();
  for (const auto &value : state.evidence_instances())
    evidence.push_back({{"EvidenceInstanceId", value.evidence_instance_id},
                        {"EvidenceTypeId", value.evidence_type_id},
                        {"Provenance", value.provenance}, {"Quality", value.quality},
                        {"Confidence", value.confidence}, {"ContextId", maybe(value.context_id)},
                        {"Revision", value.revision}});
  Json contexts = Json::array();
  for (const auto &value : state.applicability_contexts())
    contexts.push_back({{"Id", value.context_id}, {"Traits", value.sorted_traits}});
  Json capabilities = Json::array();
  for (const auto &value : state.capabilities())
    capabilities.push_back({{"CapabilityId", value.capability_id},
                            {"ContextId", maybe(value.context_id)}});
  Json projects = Json::array();
  for (const auto &value : state.active_projects())
    projects.push_back({{"NodeId", value.node_id}, {"Stage", static_cast<int>(value.stage)},
                        {"TargetApplicabilityContextId", maybe(value.target_applicability_context_id)},
                        {"AssignedEffectiveLabs", value.assigned_effective_labs},
                        {"ReadinessEfficiency", value.readiness_efficiency}, {"Paused", value.paused},
                        {"PauseReason", maybe(value.pause_reason)},
                        {"StageResearchPoints", value.stage_research_points},
                        {"TotalResearchPoints", value.total_research_points}, {"Revision", value.revision}});
  Json fields = Json::array();
  for (const auto &value : state.expertise().field_competence())
    fields.push_back({{"FieldId", value.field_id},
      {"Current", {{"Theoretical", value.current.theoretical}, {"Experimental", value.current.experimental}, {"Engineering", value.current.engineering}}},
      {"HistoricalPeak", {{"Theoretical", value.historical_peak.theoretical}, {"Experimental", value.historical_peak.experimental}, {"Engineering", value.historical_peak.engineering}}},
      {"LastTheoreticalActivityYear", value.last_theoretical_activity_year},
      {"LastExperimentalActivityYear", value.last_experimental_activity_year},
      {"LastEngineeringActivityYear", value.last_engineering_activity_year}, {"Revision", value.revision}});
  Json institutions = Json::array();
  for (const auto &value : state.expertise().institutions())
    institutions.push_back({{"InstitutionInstanceId", value.institution_instance_id},
                            {"InstitutionArchetypeId", value.institution_archetype_id},
                            {"ContextId", maybe(value.context_id)}, {"TotalCount", value.total_count},
                            {"ActiveCount", value.active_count}, {"Revision", value.revision},
                            {"IsActive", value.is_active()}});
  Json tacit = Json::array();
  for (const auto &value : state.expertise().tacit_assets())
    tacit.push_back({{"AssetId", value.asset_id}, {"AssetTypeId", value.asset_type_id},
                     {"ScopeKind", static_cast<int>(value.scope_kind)}, {"ScopeRef", value.scope_ref},
                     {"AssimilationStage", static_cast<int>(value.assimilation_stage)},
                     {"Depth", value.depth}, {"Availability", value.availability},
                     {"TranslationContextQuality", value.translation_context_quality},
                     {"TrainingContinuity", value.training_continuity}, {"Provenance", value.provenance},
                     {"ContextId", maybe(value.context_id)}, {"Revision", value.revision}});
  return {{"CivilizationId", state.civilization_id()}, {"Revision", state.revision()},
          {"MaterializedViewRevision", state.materialized_view_revision()},
          {"DirectedProgramStageId", state.directed_program_stage_id()},
          {"TotalEffectiveResearchLabs", state.total_effective_research_labs()},
          {"AssignedEffectiveLabs", state.assigned_effective_labs()},
          {"FreeEffectiveLabs", state.free_effective_labs()}, {"NodeStates", std::move(nodes)},
          {"Pressures", std::move(pressures)}, {"EvidenceInstances", std::move(evidence)},
          {"CivilizationTraits", std::vector<std::string>(state.civilization_traits().begin(), state.civilization_traits().end())},
          {"ApplicabilityContexts", std::move(contexts)},
          {"Capabilities", std::move(capabilities)},
          {"FacilityCapabilities", std::vector<std::string>(state.facility_capabilities().begin(), state.facility_capabilities().end())},
          {"EnabledDeploymentEventIds", std::vector<std::string>(state.enabled_deployment_event_ids().begin(), state.enabled_deployment_event_ids().end())},
          {"ActiveProjects", std::move(projects)},
          {"Expertise", {{"Revision", state.expertise().revision()},
                         {"FieldCompetence", std::move(fields)},
                         {"Institutions", std::move(institutions)},
                         {"TacitAssets", std::move(tacit)}}}};
}

AdaptiveResearchCivilizationState make_profile_state(
    const AdaptiveResearchAuthority &authority, std::string_view profile,
    int index) {
  auto composition = authority.compose_reference_profile(
      "fixture:v2:" + std::to_string(index), std::string(profile),
      "fixture:context:" + std::to_string(index), 2050.0 + index);
  auto state = std::move(composition.state);
  if (index < 2) {
    const auto view = authority.build_view(state);
    const auto candidate = std::ranges::find_if(view.visible_nodes, [](const auto &node) {
      return node.state == ResearchMaturity::investigable &&
             node.blockers.empty() && node.minimum_labs.has_value();
    });
    require(candidate != view.visible_nodes.end(), "No startable fixture node.");
    const auto started = authority.start_directed_research(
        state, candidate->node_id, static_cast<double>(*candidate->minimum_labs),
        std::string_view("fixture:context:" + std::to_string(index)));
    require(started.accepted, started.message);
    if (index == 0)
      require(authority.pause_directed_research(state, candidate->node_id).accepted,
              "Could not pause fixture project.");
  }
  if (index == 0) {
    const auto &asset_type = authority.expertise_catalog().tacit_asset_types().front();
    const auto &field = authority.expertise_catalog().fields().front();
    authority.set_tacit_asset(
        state, "fixture:tacit", asset_type.id,
        ResearchTacitScopeKind::knowledge_field, field.id,
        ResearchTacitAssimilationStage::interpreted, .7, .8, .9, .6,
        "fixture provenance", std::string_view("fixture:context:0"));
    authority.kernel().add_facility_capability(state,
                                                "large_scale_prototyping");
  }
  return state;
}

AdaptiveResearchStateSnapshotV2 typed_case(
    const AdaptiveResearchAuthority &authority,
    const AdaptiveResearchSnapshotV2Codec &codec, std::string_view name) {
  auto state = authority.compose_reference_profile(
      "fixture:boundary", authority.starting_profiles().reference_profile_ids().front(),
      "fixture:boundary:context", 2100).state;
  auto snapshot = codec.capture(state);
  const auto field_id = authority.expertise_catalog().fields().front().id;
  const auto institution_id = authority.expertise_catalog()
                                  .institutions().front()
                                  .institution_archetype_id;
  const auto asset_type = authority.expertise_catalog().tacit_asset_types().front().id;
  const ResearchCompetenceVector zero{};
  auto field = [&](std::string id, ResearchCompetenceVector current,
                   ResearchCompetenceVector peak) {
    return ResearchFieldCompetenceSnapshot{std::move(id), current, peak, 0, 0, 0};
  };
  if (name == "restore-schema-unsupported") snapshot.schema_version = 3;
  else if (name == "restore-catalog-mismatch") snapshot.catalog_id = "wrong";
  else if (name == "restore-field-unknown")
    snapshot.expertise.fields = {field("unknown", zero, zero)};
  else if (name == "restore-current-negative")
    snapshot.expertise.fields = {field(field_id, {-.5, 0, 0}, zero)};
  else if (name == "restore-current-nan")
    snapshot.expertise.fields = {field(field_id, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, zero)};
  else if (name == "restore-current-positive-infinity")
    snapshot.expertise.fields = {field(field_id, {std::numeric_limits<double>::infinity(), 0, 0}, zero)};
  else if (name == "restore-current-huge")
    snapshot.expertise.fields = {field(field_id, {1e300, 0, 0}, zero)};
  else if (name == "restore-current-small-negative")
    snapshot.expertise.fields = {field(field_id, {-1e-8, 0, 0}, zero)};
  else if (name == "restore-current-one-million")
    snapshot.expertise.fields = {field(field_id, {1e6, 0, 0}, zero)};
  else if (name == "restore-current-one-quadrillion")
    snapshot.expertise.fields = {field(field_id, {1e15, 0, 0}, zero)};
  else if (name == "restore-current-ten-quadrillion")
    snapshot.expertise.fields = {field(field_id, {1e16, 0, 0}, zero)};
  else if (name == "restore-current-negative-fourth")
    snapshot.expertise.fields = {field(field_id, {-1e-4, 0, 0}, zero)};
  else if (name == "restore-current-negative-fifth")
    snapshot.expertise.fields = {field(field_id, {-1e-5, 0, 0}, zero)};
  else if (name == "restore-peak-over-100")
    snapshot.expertise.fields = {field(field_id, zero, {100.5, 0, 0})};
  else if (name == "restore-current-exceeds-peak")
    snapshot.expertise.fields = {field(field_id, {1, 1, 1}, zero)};
  else if (name == "restore-field-order-unknown-before-invalid")
    snapshot.expertise.fields = {field("unknown", zero, zero),
                                 field(field_id, {-1, 0, 0}, zero)};
  else if (name == "restore-field-order-invalid-before-unknown")
    snapshot.expertise.fields = {field(field_id, {-1, 0, 0}, zero),
                                 field("unknown", zero, zero)};
  else if (name == "restore-core-before-expertise") {
    snapshot.core.schema_version = 2;
    snapshot.expertise.fields = {field("unknown", zero, zero)};
  }
  else if (name == "restore-fields-before-institutions") {
    snapshot.expertise.fields = {field("unknown", zero, zero)};
    snapshot.expertise.institutions = {{"instance", "unknown", {}, 1, 1}};
  }
  else if (name == "restore-institution-unknown")
    snapshot.expertise.institutions = {{"instance", "unknown", {}, 1, 1}};
  else if (name == "restore-institution-negative-total")
    snapshot.expertise.institutions = {{"instance", institution_id, {}, -1, 0}};
  else if (name == "restore-institution-active-exceeds-total")
    snapshot.expertise.institutions = {{"instance", institution_id, {}, 1, 2}};
  else if (name == "restore-institution-lab-mismatch")
    snapshot.expertise.institutions = {{"instance", institution_id, {}, 1, 1}};
  else if (name == "restore-institution-preserves-unsorted-physical-facilities") {
    snapshot.core.total_effective_research_labs = authority.expertise_catalog()
                                                        .institutions().front()
                                                        .effective_lab_units;
    snapshot.core.facility_capabilities = {
        "large_scale_prototyping", "advanced_computation",
        "large_scale_prototyping"};
    snapshot.expertise.institutions = {{"instance", institution_id, {}, 1, 1}};
  }
  else if (name == "restore-empty-institutions-skip-lab-mismatch") {
    snapshot.core.total_effective_research_labs += 123;
    snapshot.expertise.institutions.clear();
  } else {
    ResearchTacitAssetSnapshot asset{"asset", asset_type,
        ResearchTacitScopeKind::knowledge_field, field_id,
        ResearchTacitAssimilationStage::access, .5, .5, .5, .5,
        "fixture", {}};
    if (name == "restore-tacit-type-unknown") asset.asset_type_id = "unknown";
    else if (name == "restore-tacit-depth-invalid") asset.depth = -1;
    else if (name == "restore-tacit-availability-invalid") asset.availability = 2;
    else if (name == "restore-tacit-empty-provenance") asset.provenance.clear();
    else if (name == "restore-tacit-unknown-scope-enum") {
      asset.scope_kind = static_cast<ResearchTacitScopeKind>(99);
      asset.scope_ref = "unknown-ref";
    } else if (name == "restore-tacit-unknown-assimilation-enum")
      asset.assimilation_stage = static_cast<ResearchTacitAssimilationStage>(99);
    else
      throw std::runtime_error("Unhandled typed row '" + std::string(name) + "'.");
    snapshot.expertise.tacit_assets = {std::move(asset)};
  }
  return snapshot;
}

struct CaughtError {
  std::string category;
  std::string message;
};

template <class Operation>
CaughtError catch_production(Operation &&operation) {
  try {
    operation();
    return {};
  } catch (const AdaptiveResearchSnapshotJsonError &error) {
    return {"json", error.what()};
  } catch (const AdaptiveResearchSnapshotError &error) {
    return {"semantic", error.what()};
  } catch (const std::out_of_range &error) {
    return {"out_of_range", error.what()};
  } catch (const std::invalid_argument &error) {
    return {"invalid_argument", error.what()};
  } catch (const std::exception &error) {
    return {"unexpected", error.what()};
  }
}

std::string expected_category(const Json &source_error) {
  const auto type = source_error.at("Type").get<std::string>();
  if (type == "InvalidDataException") return "semantic";
  if (type == "ArgumentOutOfRangeException") return "out_of_range";
  if (type == "ArgumentException") return "invalid_argument";
  return "json";
}

std::string expected_typed_message(const Json &row) {
  return row.at("Error").at("Message").get<std::string>();
}

void check_expected_error(const Json &row, const CaughtError &error,
                          bool typed_restore) {
  if (row["Error"].is_null()) {
    require(error.category.empty(), row["Name"].get<std::string>() +
                                        " unexpectedly failed: " + error.message);
    return;
  }
  require(!error.category.empty(), row["Name"].get<std::string>() +
                                       " unexpectedly succeeded.");
  require(error.category == expected_category(row.at("Error")),
          row["Name"].get<std::string>() + " mapped category differed: " +
              error.category);
  if (typed_restore || row["Error"]["Type"] == "InvalidDataException")
    require(error.message == expected_typed_message(row),
            row["Name"].get<std::string>() + " mapped message differed: " +
                error.message);
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("Expected fixture and research-root paths.");
    const auto fixture_path = std::filesystem::absolute(argv[1]);
    const auto research_root = std::filesystem::absolute(argv[2]);
    const auto fixture = Json::parse(read_bytes(fixture_path));
    auto authority = load_adaptive_research_authority(research_root);
    require(fingerprint(research_root) ==
                fixture.at("CanonicalFingerprint").get<std::string>(),
            "Canonical research fingerprint differs from actual source fixture.");
    AdaptiveResearchSnapshotV2Codec original(authority);
    AdaptiveResearchSnapshotV2Codec moved(std::move(original));
    AdaptiveResearchSnapshotV2Codec codec(authority);
    codec = std::move(moved);
    int profile_index = 0;
    std::size_t checked = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto before = fingerprint(research_root);
      require(before == row.at("BeforeFingerprint").get<std::string>(),
              name + " input fingerprint differed before production call.");
      if (name.starts_with("profile-")) {
        auto state = make_profile_state(authority, row.at("ProfileId").get<std::string>(),
                                        profile_index++);
        require(projection(state) == row.at("OriginalProjection"),
                name + " original full projection differed.");
        const auto serialized = codec.serialize(state);
        require(serialized == row.at("Serialized").get<std::string>(),
                name + " serialization differed.");
        auto restored = codec.deserialize(serialized);
        require(projection(restored) == row.at("RestoredProjection"),
                name + " full restored projection differed.");
        require(codec.serialize(restored) == row.at("RestoredSerialized").get<std::string>(),
                name + " restored state differed.");
      } else if (row.value("Kind", "") == "utf16-order") {
        auto state = authority.compose_reference_profile(
            "fixture:ordinal",
            authority.starting_profiles().reference_profile_ids().front(),
            "fixture:ordinal:context", 2099).state;
        const auto &asset_type = authority.expertise_catalog().tacit_asset_types().front();
        const auto &field = authority.expertise_catalog().fields().front();
        authority.set_tacit_asset(state, "\xEE\x80\x80", asset_type.id,
            ResearchTacitScopeKind::knowledge_field, field.id,
            ResearchTacitAssimilationStage::access, .5, .5, .5, .5, "bmp", std::nullopt);
        authority.set_tacit_asset(state, "\xF0\x90\x80\x80", asset_type.id,
            ResearchTacitScopeKind::knowledge_field, field.id,
            ResearchTacitAssimilationStage::access, .5, .5, .5, .5,
            "supplementary", std::nullopt);
        require(projection(state) == row.at("Projection"),
                "UTF-16 ordinal projection differed.");
        require(Json::parse(codec.serialize(state)) ==
                    Json::parse(row.at("Serialized").get<std::string>()),
                "UTF-16 ordinal semantic serialization differed.");
      } else if (name == "schema1-direct-fallback") {
        AdaptiveResearchSnapshotCodec v1(authority.catalog(), authority.applicability(),
                                          authority.facilities());
        auto restored = codec.deserialize(row.at("Serialized").get<std::string>());
        require(projection(restored) == row.at("RestoredProjection"),
                "Schema 1 restored projection differed.");
        require(v1.serialize(restored) == row.at("RestoredV1").get<std::string>(),
                "Schema 1 fallback changed the v1 payload.");
      } else if (row.value("Kind", "") == "known-parser-order-boundary") {
        const auto input = row.at("Input").get<std::string>();
        const auto error = catch_production([&] { (void)codec.deserialize(input); });
        require(error.category == "semantic",
                name + " expected the documented earlier native semantic error.");
      } else if (row.value("Kind", "") == "source-only-null-boundary") {
        const auto input = row.at("Input").get<std::string>();
        const auto error = catch_production([&] { (void)codec.deserialize(input); });
        require(error.category == "json", name + " native null boundary was not a JSON error.");
      } else if (row.value("Kind", "") == "json-deserialize") {
        const auto input = row.at("Input").get<std::string>();
        std::optional<AdaptiveResearchCivilizationState> restored;
        const auto error = catch_production(
            [&] { restored.emplace(codec.deserialize(input)); });
        check_expected_error(row, error, false);
        if (restored)
          require(projection(*restored) == row.at("RestoredProjection"),
                  name + " successful JSON projection differed.");
        if (restored)
          require(codec.serialize(*restored) == row.at("RestoredSerialized").get<std::string>(),
                  name + " successful JSON restore differed.");
      } else if (row.value("Kind", "") == "nonfinite-serialize") {
        auto state = authority.compose_reference_profile(
            "fixture:nonfinite",
            authority.starting_profiles().reference_profile_ids().front(),
            "fixture:nonfinite:context", 2101).state;
        const auto fields = state.expertise().field_competence();
        require(!fields.empty(), "Non-finite fixture has no competence field.");
        // Test-only corruption mirrors the source oracle's approved internal
        // writer-equivalent setup. The production call begins after setup.
        auto &mutable_field = const_cast<ResearchFieldCompetenceRuntimeState &>(fields.front());
        mutable_field.current.theoretical = std::numeric_limits<double>::quiet_NaN();
        const auto error = catch_production([&] { (void)codec.serialize(state); });
        require(error.category == "json",
                "Non-finite serialization did not produce the native JSON boundary.");
      } else {
        auto snapshot = typed_case(authority, codec, name);
        std::optional<AdaptiveResearchCivilizationState> restored;
        const auto error = catch_production(
            [&] { restored.emplace(codec.restore(snapshot)); });
        check_expected_error(row, error, true);
        if (restored)
          require(projection(*restored) == row.at("RestoredProjection"),
                  name + " typed projection differed.");
        if (restored)
          require(codec.serialize(*restored) == row.at("RestoredSerialized").get<std::string>(),
                  name + " typed restore differed.");
      }
      const auto after = fingerprint(research_root);
      require(after == row.at("AfterFingerprint").get<std::string>(),
              name + " input fingerprint differed after production call.");
      ++checked;
    }
    require(checked == 81, "Fixture row count changed.");
    std::cout << "Adaptive Research expertise snapshot parity passed "
              << checked << " rows.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n';
    std::cerr << "cwd=" << std::filesystem::current_path().string() << '\n';
    if (argc > 1) std::cerr << "fixture=" << std::filesystem::absolute(argv[1]).string() << '\n';
    if (argc > 2) std::cerr << "research-root=" << std::filesystem::absolute(argv[2]).string() << '\n';
    return 1;
  }
}
