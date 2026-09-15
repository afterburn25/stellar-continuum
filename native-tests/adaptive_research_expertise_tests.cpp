#include <stellar/core/adaptive_research_expertise.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

using Json = nlohmann::json;
using namespace stellar::core;
using Writer = detail::AdaptiveResearchExpertiseStateWriter;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

double number(const Json &value) {
  if (value.is_number()) {
    return value.get<double>();
  }
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  throw std::invalid_argument("Invalid named number.");
}

Json encoded(double value) {
  if (std::isnan(value))
    return "NaN";
  if (std::isinf(value))
    return value > 0.0 ? Json("Infinity") : Json("-Infinity");
  return value;
}

std::optional<std::string> optional_string(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional(value.get<std::string>());
}

ResearchCompetenceVector competence(const Json &value) {
  return {number(value.at("Theoretical")), number(value.at("Experimental")),
          number(value.at("Engineering"))};
}

ResearchFieldCompetenceRuntimeState field(const Json &value) {
  return {value.at("FieldId"),
          competence(value.at("Current")),
          competence(value.at("HistoricalPeak")),
          number(value.at("LastTheoreticalActivityYear")),
          number(value.at("LastExperimentalActivityYear")),
          number(value.at("LastEngineeringActivityYear")),
          value.at("Revision")};
}

ResearchInstitutionRuntimeState institution(const Json &value) {
  return {value.at("InstitutionInstanceId"),
          value.at("InstitutionArchetypeId"),
          optional_string(value.at("ContextId")),
          value.at("TotalCount"),
          value.at("ActiveCount"),
          value.at("Revision")};
}

ResearchTacitAssetRuntimeState tacit(const Json &value) {
  return {value.at("AssetId"),
          value.at("AssetTypeId"),
          static_cast<ResearchTacitScopeKind>(value.at("ScopeKind").get<int>()),
          value.at("ScopeRef"),
          static_cast<ResearchTacitAssimilationStage>(
              value.at("AssimilationStage").get<int>()),
          number(value.at("Depth")),
          number(value.at("Availability")),
          number(value.at("TranslationContextQuality")),
          number(value.at("TrainingContinuity")),
          value.at("Provenance"),
          optional_string(value.at("ContextId")),
          value.at("Revision")};
}

Json competence_json(const ResearchCompetenceVector &value) {
  return {{"Theoretical", encoded(value.theoretical)},
          {"Experimental", encoded(value.experimental)},
          {"Engineering", encoded(value.engineering)}};
}

Json field_json(const ResearchFieldCompetenceRuntimeState &value) {
  return {{"FieldId", value.field_id},
          {"Current", competence_json(value.current)},
          {"HistoricalPeak", competence_json(value.historical_peak)},
          {"LastTheoreticalActivityYear",
           encoded(value.last_theoretical_activity_year)},
          {"LastExperimentalActivityYear",
           encoded(value.last_experimental_activity_year)},
          {"LastEngineeringActivityYear",
           encoded(value.last_engineering_activity_year)},
          {"Revision", value.revision}};
}

Json institution_json(const ResearchInstitutionRuntimeState &value) {
  return {{"InstitutionInstanceId", value.institution_instance_id},
          {"InstitutionArchetypeId", value.institution_archetype_id},
          {"ContextId", value.context_id},
          {"TotalCount", value.total_count},
          {"ActiveCount", value.active_count},
          {"Revision", value.revision},
          {"IsActive", value.is_active()}};
}

Json tacit_json(const ResearchTacitAssetRuntimeState &value) {
  return {
      {"AssetId", value.asset_id},
      {"AssetTypeId", value.asset_type_id},
      {"ScopeKind", static_cast<int>(value.scope_kind)},
      {"ScopeRef", value.scope_ref},
      {"AssimilationStage", static_cast<int>(value.assimilation_stage)},
      {"Depth", encoded(value.depth)},
      {"Availability", encoded(value.availability)},
      {"TranslationContextQuality", encoded(value.translation_context_quality)},
      {"TrainingContinuity", encoded(value.training_continuity)},
      {"Provenance", value.provenance},
      {"ContextId", value.context_id},
      {"Revision", value.revision}};
}

Json snapshot(const AdaptiveResearchExpertiseState &state) {
  Json fields = Json::array();
  for (const auto &value : state.field_competence())
    fields.push_back(field_json(value));
  Json institutions = Json::array();
  for (const auto &value : state.institutions())
    institutions.push_back(institution_json(value));
  Json assets = Json::array();
  for (const auto &value : state.tacit_assets())
    assets.push_back(tacit_json(value));
  return {{"Revision", state.revision()},
          {"FieldCompetence", fields},
          {"Institutions", institutions},
          {"TacitAssets", assets}};
}

void equal_json(const Json &actual, const Json &expected,
                const std::string &where) {
  if (actual.is_number() && expected.is_number()) {
    if (actual.is_number_integer() && expected.is_number_integer()) {
      require(actual.get<std::int64_t>() == expected.get<std::int64_t>(),
              where + " integer differs");
    } else {
      const auto a = actual.get<double>();
      const auto b = expected.get<double>();
      require(std::abs(a - b) <=
                  1e-12 * std::max({1.0, std::abs(a), std::abs(b)}),
              where + " number differs");
    }
    return;
  }
  require(actual.type() == expected.type(), where + " type differs");
  if (actual.is_array()) {
    require(actual.size() == expected.size(), where + " size differs");
    for (std::size_t i = 0; i < actual.size(); ++i)
      equal_json(actual[i], expected[i], where + "[" + std::to_string(i) + "]");
    return;
  }
  if (actual.is_object()) {
    require(actual.size() == expected.size(), where + " keys differ");
    for (const auto &[key, value] : expected.items()) {
      require(actual.contains(key), where + " missing " + key);
      equal_json(actual.at(key), value, where + "." + key);
    }
    return;
  }
  require(actual == expected, where + " value differs");
}

Json catalog_json(const AdaptiveResearchExpertiseCatalog &catalog) {
  Json fields = Json::array();
  for (const auto &value : catalog.fields())
    fields.push_back({{"Id", value.id},
                      {"Name", value.name},
                      {"Family", value.family},
                      {"RelatedFieldIds", value.related_field_ids}});
  Json stages = Json::array();
  for (const auto &value : catalog.stage_weights())
    stages.push_back({{"Stage", static_cast<int>(value.stage)},
                      {"Value",
                       {{"Theoretical", encoded(value.weights.theoretical)},
                        {"Experimental", encoded(value.weights.experimental)},
                        {"Engineering", encoded(value.weights.engineering)}}}});
  const auto &readiness = catalog.readiness_weights();
  Json tacit_types = Json::array();
  for (const auto &value : catalog.tacit_asset_types()) {
    Json components = Json::array();
    for (const auto component : value.supported_components)
      components.push_back(static_cast<int>(component));
    tacit_types.push_back(
        {{"Id", value.id}, {"SupportedComponents", components}});
  }
  Json institutions = Json::array();
  for (const auto &value : catalog.institutions())
    institutions.push_back(
        {{"InstitutionArchetypeId", value.institution_archetype_id},
         {"EffectiveLabUnits", encoded(value.effective_lab_units)},
         {"SpecializedFieldIds", value.specialized_field_ids},
         {"FacilityCapabilityIds", value.facility_capability_ids}});
  const auto &policy = catalog.runtime_policy();
  Json gains = Json::array();
  for (const auto &value : policy.stage_practice_gain)
    gains.push_back({{"Stage", static_cast<int>(value.stage)},
                     {"Value", competence_json(value.gain)}});
  Json factors = Json::array();
  for (const auto &value : policy.tacit_assimilation_factors)
    factors.push_back({{"Stage", static_cast<int>(value.stage)},
                       {"Value", encoded(value.factor)}});
  return {
      {"Fields", fields},
      {"StageWeights", stages},
      {"ReadinessWeights",
       {{"FieldCompetence", encoded(readiness.field_competence)},
        {"FacilityReadiness", encoded(readiness.facility_readiness)},
        {"EvidenceReadiness", encoded(readiness.evidence_readiness)},
        {"TacitExpertise", encoded(readiness.tacit_expertise)}}},
      {"TacitAssetTypes", tacit_types},
      {"Institutions", institutions},
      {"RuntimePolicy",
       {{"StagePracticeGain", gains},
        {"RelatedFieldTransferFraction",
         encoded(policy.related_field_transfer_fraction)},
        {"MinimumGainFactor", encoded(policy.minimum_gain_factor)},
        {"AnnualAtrophyRates", competence_json(policy.annual_atrophy_rates)},
        {"AtrophyGraceYears", encoded(policy.atrophy_grace_years)},
        {"GeneralLabMatchingFactor",
         encoded(policy.general_lab_matching_factor)},
        {"SpecializedMatchingFactor",
         encoded(policy.specialized_matching_factor)},
        {"NonmatchingSpecialistFactor",
         encoded(policy.nonmatching_specialist_factor)},
        {"TacitAssimilationFactors", factors},
        {"TacitBestWeight", encoded(policy.tacit_best_weight)},
        {"TacitMeanWeight", encoded(policy.tacit_mean_weight)},
        {"PreservationFloorFraction",
         encoded(policy.preservation_floor_fraction)},
        {"EstablishedKnowledgeTheoreticalFloorFraction",
         encoded(policy.established_knowledge_theoretical_floor_fraction)}}}};
}

struct Error {
  std::string type;
  std::string message;
};
template <class Function> std::optional<Error> capture(Function function) {
  try {
    function();
    return std::nullopt;
  } catch (const std::out_of_range &e) {
    return Error{std::string_view(e.what()).starts_with("The given key")
                     ? "KeyNotFoundException"
                     : "ArgumentOutOfRangeException",
                 e.what()};
  } catch (const std::exception &e) {
    return Error{"UnexpectedNativeException", e.what()};
  }
}

template <class Function>
std::optional<Error> capture_catalog(Function function) {
  try {
    function();
    return std::nullopt;
  } catch (const AdaptiveResearchExpertiseCatalogError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const std::invalid_argument &error) {
    return Error{"ArgumentException", error.what()};
  } catch (const std::out_of_range &error) {
    return Error{"KeyNotFoundException", error.what()};
  } catch (const std::logic_error &error) {
    return Error{"InvalidOperationException", error.what()};
  } catch (const std::exception &error) {
    return Error{"UnexpectedNativeException", error.what()};
  }
}

class ScratchDirectory {
public:
  explicit ScratchDirectory(const std::filesystem::path &source) {
    parent_ = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path() /
        "stellar-research-expertise-native-scratch");
    std::filesystem::create_directories(parent_);
    const auto stamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    path_ = (parent_ / ("case-" + std::to_string(stamp))).lexically_normal();
    require(path_.parent_path() == parent_,
            "Scratch directory escaped parent.");
    std::error_code error;
    require(std::filesystem::create_directory(path_, error) && !error,
            "Scratch directory was not exclusively claimed.");
    for (const auto &entry : std::filesystem::directory_iterator(source))
      std::filesystem::copy(entry.path(), path_ / entry.path().filename());
  }
  ~ScratchDirectory() { (void)cleanup(); }
  const std::filesystem::path &path() const noexcept { return path_; }
  bool cleanup() noexcept {
    if (path_.empty())
      return true;
    if (path_.parent_path() != parent_)
      return false;
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    if (error)
      return false;
    path_.clear();
    return true;
  }

private:
  std::filesystem::path parent_;
  std::filesystem::path path_;
};

std::string directory_fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(root))
    files.push_back(entry.path());
  std::ranges::sort(files, {},
                    [](const auto &path) { return path.filename().string(); });
  std::uint64_t hash = 14695981039346656037ULL;
  const auto add = [&](std::string_view bytes) {
    for (const unsigned char value : bytes) {
      hash ^= value;
      hash *= 1099511628211ULL;
    }
  };
  for (const auto &path : files) {
    const auto name = path.filename().string();
    add(name);
    add(std::string_view("\0", 1));
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    add(bytes);
  }
  char result[17]{};
  constexpr char digits[] = "0123456789ABCDEF";
  for (int index = 15; index >= 0; --index) {
    result[index] = digits[hash & 0xf];
    hash >>= 4;
  }
  return result;
}

std::string decode_hex(std::string_view text) {
  require(text.size() % 2 == 0, "Malformed retained hex payload.");
  const auto digit = [](char value) -> unsigned {
    if (value >= '0' && value <= '9')
      return value - '0';
    if (value >= 'A' && value <= 'F')
      return value - 'A' + 10;
    throw std::invalid_argument("Malformed retained hex payload.");
  };
  std::string bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t index = 0; index < text.size(); index += 2)
    bytes.push_back(
        static_cast<char>((digit(text[index]) << 4) | digit(text[index + 1])));
  return bytes;
}

void write_retained_payload(const std::filesystem::path &root,
                            const Json &input) {
  const auto relative = input.at("RelativePath").get<std::string>();
  const auto path = (root / relative).lexically_normal();
  require(path.parent_path() == root,
          "Retained malformed payload escaped scratch root.");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  const auto bytes = decode_hex(input.at("ContentHex").get<std::string>());
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(output.good(), "Failed to write retained malformed payload.");
}

void check_error(const std::optional<Error> &actual, const Json &expected,
                 const std::string &where) {
  require(actual.has_value() == !expected.is_null(),
          where + " error presence differs");
  if (actual) {
    require(actual->type == expected.at("Type").get<std::string>(),
            where + " error type differs");
    require(actual->message == expected.at("Message").get<std::string>(),
            where + " error message differs");
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3,
            "usage: research_expertise_tests <fixture> <research-data>");
    std::ifstream input(argv[1]);
    Json root;
    input >> root;
    require(root.at("Schema") ==
                "stellar-adaptive-research-expertise-foundation-v1",
            "Unexpected schema.");
    auto base = load_adaptive_research_catalog(argv[2]);
    auto facilities = load_adaptive_research_facility_catalog(argv[2], base);
    auto catalog =
        load_adaptive_research_expertise_catalog(argv[2], base, facilities);
    require(directory_fingerprint(argv[2]) ==
                root.at("CanonicalFingerprintBefore").get<std::string>(),
            "Canonical input fingerprint differs before replay.");
    equal_json(catalog_json(catalog), root.at("Catalog"), "catalog");

    for (const auto &row : root.at("Malformed")) {
      const auto recipe = row.at("Recipe").get<std::string>();
      ScratchDirectory scratch(argv[2]);
      write_retained_payload(scratch.path(), row.at("Input"));
      require(directory_fingerprint(scratch.path()) ==
                  row.at("Input").at("Fingerprint").get<std::string>(),
              recipe + " retained input fingerprint differs");
      const auto error = capture_catalog([&] {
        (void)load_adaptive_research_expertise_catalog(scratch.path(), base,
                                                       facilities);
      });
      check_error(error, row.at("Error"), recipe);
      require(directory_fingerprint(scratch.path()) ==
                  row.at("AfterFingerprint").get<std::string>(),
              recipe + " loader mutated its input");
      require(scratch.cleanup(), recipe + " scratch cleanup failed");
    }
    require(directory_fingerprint(argv[2]) ==
                root.at("CanonicalFingerprintAfter").get<std::string>(),
            "Canonical input fingerprint differs after malformed replay.");

    const ResearchCompetenceVector sample{1.25, 2.5, 3.75};
    for (const auto &row : root.at("VectorCases")) {
      const auto component = static_cast<ResearchCompetenceComponent>(
          row.at("Component").get<int>());
      double result{};
      const auto error = capture([&] { result = sample.get(component); });
      check_error(error, row.at("Error"), "vector");
      if (!error)
        equal_json(encoded(result), row.at("Result"), "vector result");
    }

    AdaptiveResearchExpertiseState state;
    for (const auto &row : root.at("Commands")) {
      const auto name = row.at("Name").get<std::string>();
      const auto &value = row.at("Input");
      const auto field_value =
          (value.contains("FieldId") && value.contains("Current"))
              ? std::optional(field(value))
              : std::nullopt;
      const auto institution_value =
          (name.find("institution") != std::string::npos &&
           value.contains("InstitutionInstanceId"))
              ? std::optional(institution(value))
              : std::nullopt;
      const auto tacit_value =
          (name.find("tacit") != std::string::npos &&
           value.contains("AssetId") && value.contains("AssetTypeId"))
              ? std::optional(tacit(value))
              : std::nullopt;
      const auto id = value.contains("Id")
                          ? std::optional(value.at("Id").get<std::string>())
                          : std::nullopt;
      const auto field_id =
          value.contains("FieldId")
              ? std::optional(value.at("FieldId").get<std::string>())
              : std::nullopt;
      equal_json(snapshot(state), row.at("Before"), name + " before");
      using OperationResult =
          std::variant<std::monostate, ResearchFieldCompetenceRuntimeState,
                       double, bool>;
      OperationResult result;
      const auto error = capture([&] {
        if (name.starts_with("get-missing"))
          result = state.get_field(*field_id);
        else if (name.starts_with("set-field") ||
                 name.starts_with("replace-field") ||
                 name.starts_with("reinsert-field") ||
                 name.starts_with("invalid-current") ||
                 name.starts_with("invalid-peak"))
          Writer::set_field(state, *field_value);
        else if (name.starts_with("remove-nonzero") ||
                 name.starts_with("remove-zero"))
          Writer::remove_field_if_zero(state, *field_id);
        else if (name == "total-labs-includes-inactive-lookup" ||
                 name == "total-labs-validates-inactive-archetype")
          result = state.total_active_effective_lab_units(catalog);
        else if (name.starts_with("set-institution") ||
                 name.starts_with("set-inactive") ||
                 name.starts_with("zero-") ||
                 name.starts_with("invalid-institution"))
          Writer::set_institution(state, *institution_value);
        else if (name.starts_with("remove-institution") ||
                 name == "remove-unknown-institution")
          result = Writer::remove_institution(state, *id);
        else if (name.starts_with("set-unknown"))
          Writer::set_institution(state, *institution_value);
        else if (name.starts_with("set-tacit") ||
                 name.starts_with("replace-tacit") ||
                 name.starts_with("invalid-tacit"))
          Writer::set_tacit_asset(state, *tacit_value);
        else if (name.starts_with("remove-tacit"))
          result = Writer::remove_tacit_asset(state, *id);
        else
          throw std::invalid_argument("Unknown command.");
      });
      check_error(error, row.at("Error"), name);
      if (!error) {
        const auto projected = std::visit(
            [](const auto &value) -> Json {
              using Value = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<Value, std::monostate>)
                return nullptr;
              else if constexpr (std::is_same_v<
                                     Value,
                                     ResearchFieldCompetenceRuntimeState>)
                return field_json(value);
              else if constexpr (std::is_same_v<Value, double>)
                return encoded(value);
              else
                return value;
            },
            result);
        equal_json(projected, row.at("Result"), name + " result");
      }
      equal_json(snapshot(state), row.at("After"), name + " after");
    }

    const auto original_image = snapshot(state);
    auto copied = state;
    Writer::set_field(copied, {"native:copy", {}, {}, 0.0, 0.0, 0.0, 0});
    equal_json(snapshot(state), original_image, "copy leaves original owned");
    require(snapshot(copied) != original_image,
            "copy mutation must remain independent");
    auto moved = std::move(copied);
    const auto moved_before = snapshot(moved);
    Writer::set_field(moved, {"native:move", {}, {}, 0.0, 0.0, 0.0, 0});
    require(snapshot(moved) != moved_before,
            "moved state must remain independently mutable");
    require(detail::checked_next_adaptive_research_expertise_revision(
                std::numeric_limits<std::int64_t>::max() - 1) ==
                std::numeric_limits<std::int64_t>::max(),
            "last safe expertise revision differs");
    const auto overflow = capture([&] {
      (void)detail::checked_next_adaptive_research_expertise_revision(
          std::numeric_limits<std::int64_t>::max());
    });
    require(overflow.has_value() &&
                overflow->type == "UnexpectedNativeException" &&
                overflow->message ==
                    "Adaptive Research expertise revision exhausted.",
            "native expertise revision boundary differs");
    std::cout << "research expertise parity: " << catalog.fields().size()
              << " fields, " << catalog.institutions().size()
              << " institutions, " << root.at("Commands").size()
              << " state commands\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "research expertise parity failure\n"
              << "exception_type: " << typeid(error).name() << '\n'
              << "message: " << error.what() << '\n'
              << "cwd: " << std::filesystem::current_path().string() << '\n'
              << "fixture: " << (argc > 1 ? argv[1] : "<missing>") << '\n'
              << "research_root: " << (argc > 2 ? argv[2] : "<missing>")
              << '\n';
    return 1;
  }
}
