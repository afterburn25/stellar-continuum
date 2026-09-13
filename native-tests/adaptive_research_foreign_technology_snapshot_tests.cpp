#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stellar/core/adaptive_research_foreign_technology_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_strategic_snapshot_json.hpp>

namespace {
using Json = nlohmann::ordered_json;
using namespace stellar::core;
void require(bool v, std::string_view m) {
  if (!v)
    throw std::runtime_error(std::string(m));
}
std::vector<std::string> strings(const Json &v) {
  return v.get<std::vector<std::string>>();
}
template <class E>
E enumeration(const Json &v,
              std::initializer_list<std::pair<std::string_view, E>> names) {
  if (v.is_number_integer())
    return static_cast<E>(v.get<int>());
  auto s = v.get<std::string>();
  for (auto [n, e] : names)
    if (s == n)
      return e;
  throw std::runtime_error("invalid fixture enum");
}
ForeignTechnologyAssessmentSnapshot assessment(const Json &v) {
  return {
      v.at("foreignTechnologyReference").get<std::string>(),
      v.at("sourceLineageReference").get<std::string>(),
      enumeration<ForeignUnderstandingState>(
          v.at("understanding"),
          {{"unknown", ForeignUnderstandingState::unknown},
           {"observed", ForeignUnderstandingState::observed},
           {"characterized", ForeignUnderstandingState::characterized},
           {"principleUnderstood",
            ForeignUnderstandingState::principle_understood},
           {"engineeringUnderstood",
            ForeignUnderstandingState::engineering_understood}}),
      enumeration<ForeignOperabilityState>(
          v.at("operability"),
          {{"unknown", ForeignOperabilityState::unknown},
           {"unusable", ForeignOperabilityState::unusable},
           {"originOnly", ForeignOperabilityState::origin_only},
           {"supportedOperation", ForeignOperabilityState::supported_operation},
           {"adaptedOperation", ForeignOperabilityState::adapted_operation},
           {"nativeOperation", ForeignOperabilityState::native_operation}}),
      enumeration<ForeignReproductionState>(
          v.at("reproduction"),
          {{"none", ForeignReproductionState::none},
           {"componentReplication",
            ForeignReproductionState::component_replication},
           {"subsystemReplication",
            ForeignReproductionState::subsystem_replication},
           {"foreignProcessReplication",
            ForeignReproductionState::foreign_process_replication},
           {"nativeProcessReplication",
            ForeignReproductionState::native_process_replication}}),
      enumeration<ForeignAdaptationState>(
          v.at("adaptation"),
          {{"none", ForeignAdaptationState::none},
           {"conceptualInspiration",
            ForeignAdaptationState::conceptual_inspiration},
           {"interfaceAdaptation",
            ForeignAdaptationState::interface_adaptation},
           {"nativeDerivative", ForeignAdaptationState::native_derivative},
           {"hybridLineage", ForeignAdaptationState::hybrid_lineage}}),
      strings(v.at("knownConstraintIds")),
      strings(v.at("evidenceRefs")),
      strings(v.at("tacitAssetRefs")),
      v.at("lastAssessmentYear").get<double>(),
      v.at("confidence").get<double>()};
}
ForeignTechnologyPackageSnapshot package(const Json &v) {
  return {v.at("packageId").get<std::string>(),
          v.at("foreignTechnologyReference").get<std::string>(),
          v.at("sourceLineageReference").get<std::string>(),
          strings(v.at("componentIds")),
          strings(v.at("rightIds")),
          strings(v.at("evidenceRefs")),
          strings(v.at("tacitAssetRefs")),
          strings(v.at("knowledgeFieldIds")),
          v.at("provenance").get<std::string>(),
          v.at("integrity").get<double>()};
}
AdaptiveResearchStateSnapshotV4 snapshot(const Json &v) {
  AdaptiveResearchStateSnapshotV4 r;
  r.schema_version = v.at("schemaVersion").get<int>();
  r.catalog_id = v.at("catalogId").get<std::string>();
  r.research =
      detail::decode_adaptive_research_snapshot_v3_dto(v.at("research").dump());
  for (auto &i : v.at("foreignAssessments"))
    r.foreign_assessments.push_back(assessment(i));
  for (auto &i : v.at("foreignPackages"))
    r.foreign_packages.push_back(package(i));
  return r;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: adaptive_research_foreign_snapshot_tests <fixture> "
          "<research-data>");
    std::ifstream in(argv[1]);
    require(bool(in), "Could not open fixture.");
    Json fixture;
    in >> fixture;
    auto runtime = load_adaptive_research_strategic_runtime(argv[2]);
    AdaptiveResearchForeignTechnologySnapshotCodec codec(runtime);
    std::size_t count = 0;
    for (const auto &row : fixture.at("rows")) {
      const auto kind = row.at("kind").get<std::string>();
      require(kind == "Typed" || kind == "Text", "Unknown fixture kind.");
    std::optional<AdaptiveResearchStateSnapshotV4> dto;
    if (kind == "Typed")
      dto = snapshot(row.at("input"));
    const auto dto_before = dto ? std::optional<std::string>(
                                      detail::encode_adaptive_research_snapshot_v4_dto(*dto))
                                : std::nullopt;
      const auto text =
          kind == "Text" ? row.at("input").get<std::string>() : std::string{};
      std::optional<AdaptiveResearchCivilizationState> state;
      std::optional<std::pair<std::string, std::string>> error;
      if (kind == "Typed")
        require(row.at("input") == row.at("inputAfter"),
                "Managed typed snapshot input changed during restore.");
      try {
        if (dto)
          state.emplace(codec.restore(*dto));
        else
          state.emplace(codec.deserialize(text));
      } catch (const AdaptiveResearchForeignTechnologySnapshotError &e) {
        error = {{"InvalidDataException", e.what()}};
      } catch (const AdaptiveResearchStrategicSnapshotError &e) {
        error = {{"InvalidDataException", e.what()}};
      } catch (const AdaptiveResearchSnapshotError &e) {
        error = {{"InvalidDataException", e.what()}};
      } catch (const AdaptiveResearchSnapshotJsonError &e) {
        error = {{"JsonException", e.what()}};
    } catch (const std::exception &e) {
      error = {{"UnexpectedNativeException", e.what()}};
    }
    if (dto)
      require(detail::encode_adaptive_research_snapshot_v4_dto(*dto) ==
                  *dto_before,
              "Native typed snapshot input changed during restore.");
      if (row.at("name") == "restored-next-mutation" && state)
        (void)runtime.foreign_technology().confirm_constraint(
            *state, "foreign:schema4",
            fixture.at("controls").at("constraint").get<std::string>(), 2202);
      const Json foreign_revision =
          state ? Json(runtime.foreign_technology().state(*state).revision())
                : Json(nullptr);
      require(foreign_revision == row.at("foreignRevision"),
              row.at("name").get<std::string>() +
                  " foreign support revision differs");
      const Json serialized =
          state ? Json(codec.serialize(*state)) : Json(nullptr);
      if (serialized != row.at("serialized"))
        throw std::runtime_error(
            row.at("name").get<std::string>() + " serialized differs\nactual=" +
            serialized.dump() + "\nexpected=" + row.at("serialized").dump());
      Json actual_error =
          error ? Json{{"type", error->first}, {"message", error->second}}
                : Json(nullptr);
      if (!row.at("error").is_null() &&
          row.at("error").at("type") == "JsonException") {
        require(error && error->first == "JsonException" &&
                    !error->second.empty(),
                "JSON parser boundary category differs.");
      } else
        require(actual_error == row.at("error"),
                row.at("name").get<std::string>() + " error differs");
      ++count;
    }
    auto nonfinite = snapshot(fixture.at("canonical"));
    require(!nonfinite.foreign_assessments.empty(),
            "Nonfinite probe needs an assessment.");
    nonfinite.foreign_assessments[0].last_assessment_year =
        std::numeric_limits<double>::infinity();
    auto restored = codec.restore(nonfinite);
    std::optional<std::pair<std::string, std::string>> nonfinite_error;
    try {
      (void)codec.serialize(restored);
    } catch (const AdaptiveResearchSnapshotJsonError &e) {
      nonfinite_error = {{"ArgumentException", e.what()}};
    }
    Json actual_nonfinite = {
        {"restoreSucceeded", true},
        {"error", nonfinite_error ? Json{{"type", nonfinite_error->first},
                                         {"message", nonfinite_error->second}}
                                  : Json(nullptr)}};
    require(actual_nonfinite == fixture.at("nonfiniteSerialize"),
            "Nonfinite serialization boundary differs.");
    static_assert(
        !std::is_constructible_v<AdaptiveResearchForeignTechnologySnapshotCodec,
                                 AdaptiveResearchStrategicRuntime &&>);
    std::cout << "adaptive_research_foreign_snapshot_tests: " << count
              << " source rows passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "adaptive_research_foreign_snapshot_tests failed: " << e.what()
              << "\nCWD: " << std::filesystem::current_path() << "\n";
    return 1;
  }
}
