#include <stellar/core/adaptive_research_outcome_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
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

using Json = nlohmann::ordered_json;
using namespace stellar::core;

static_assert(std::is_move_constructible_v<AdaptiveResearchOutcomeSnapshotCodec>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchOutcomeSnapshotCodec>);
static_assert(!std::is_constructible_v<AdaptiveResearchOutcomeSnapshotCodec,
                                       AdaptiveResearchStrategicRuntime &&>);

namespace {

void require(bool condition, std::string message) {
  if (!condition)
    throw std::runtime_error(std::move(message));
}

std::string read(const std::filesystem::path &path) {
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
    combined += read(path);
  }
  return hex(detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(combined.data()), combined.size()}));
}

std::string decode_base64(std::string_view text) {
  const auto value = [](char item) -> int {
    if (item >= 'A' && item <= 'Z') return item - 'A';
    if (item >= 'a' && item <= 'z') return item - 'a' + 26;
    if (item >= '0' && item <= '9') return item - '0' + 52;
    if (item == '+') return 62;
    if (item == '/') return 63;
    return -1;
  };
  require(text.size() % 4 == 0, "Invalid retained base64 length.");
  std::string result;
  for (std::size_t index = 0; index < text.size(); index += 4) {
    int units[4]{};
    for (int offset = 0; offset < 4; ++offset) {
      if (text[index + offset] == '=') units[offset] = 0;
      else {
        units[offset] = value(text[index + offset]);
        require(units[offset] >= 0, "Invalid retained base64 character.");
      }
    }
    const auto combined = static_cast<unsigned>((units[0] << 18) |
                                                 (units[1] << 12) |
                                                 (units[2] << 6) | units[3]);
    result.push_back(static_cast<char>((combined >> 16) & 255));
    if (text[index + 2] != '=')
      result.push_back(static_cast<char>((combined >> 8) & 255));
    if (text[index + 3] != '=')
      result.push_back(static_cast<char>(combined & 255));
  }
  return result;
}

class OwnedScratch final {
public:
  explicit OwnedScratch(const std::filesystem::path &source) {
    parent_ = std::filesystem::temp_directory_path() /
              "stellar-research-outcome-snapshot-063-native";
    std::filesystem::create_directories(parent_);
    parent_ = std::filesystem::weakly_canonical(parent_);
    std::random_device random;
    for (int attempt = 0; attempt < 64; ++attempt) {
      root_ = parent_ / ("case-" + std::to_string(random()) + "-" +
                         std::to_string(random()));
      if (std::filesystem::create_directory(root_)) break;
      root_.clear();
    }
    require(!root_.empty(), "Could not create exclusive owned scratch root.");
    require(std::filesystem::weakly_canonical(root_).parent_path() == parent_,
            "Owned scratch root escaped its canonical parent.");
    for (const auto &entry : std::filesystem::directory_iterator(source))
      if (entry.is_regular_file() && entry.path().extension() == ".json")
        std::filesystem::copy_file(entry.path(), root_ / entry.path().filename());
  }

  ~OwnedScratch() { cleanup(); }
  OwnedScratch(const OwnedScratch &) = delete;
  OwnedScratch &operator=(const OwnedScratch &) = delete;
  [[nodiscard]] const std::filesystem::path &root() const noexcept {
    return root_;
  }
  void cleanup() noexcept {
    if (root_.empty()) return;
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(root_, error);
    if (!error && resolved.parent_path() == parent_)
      std::filesystem::remove_all(resolved, error);
    root_.clear();
  }

private:
  std::filesystem::path parent_;
  std::filesystem::path root_;
};

void write_exact(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "Could not open changed scratch file.");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  require(static_cast<bool>(output), "Could not write changed scratch bytes.");
  output.close();
  require(read(path) == bytes, "Changed scratch bytes did not persist exactly.");
}

struct Error {
  std::string type;
  std::string message;
};

template <class Call>
std::optional<Error> invoke(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const AdaptiveResearchOutcomeSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchOutcomeCatalogError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchForeignTechnologySnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchStrategicSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchSnapshotJsonError &error) {
    return Error{"JsonException", error.what()};
  }
  return std::nullopt;
}

template <class Call>
std::optional<Error> invoke_overflow_boundary(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const std::overflow_error &error) {
    return Error{"NativeOverflowBoundary", error.what()};
  }
  return std::nullopt;
}

void compare_error(const Json &expected, const std::optional<Error> &actual,
                   std::string_view name) {
  if (expected.is_null()) {
    require(!actual, std::string(name) + " unexpectedly failed: " +
                         (actual ? actual->message : ""));
    return;
  }
  require(actual.has_value(), std::string(name) + " unexpectedly succeeded.");
  require(actual->type == expected.at("Type").get<std::string>(),
          std::string(name) + " exception type differed: " + actual->type);
  require(actual->message == expected.at("Message").get<std::string>(),
          std::string(name) + " exception message differed: " + actual->message);
}

void compare_json_boundary(const Json &expected,
                           const std::optional<Error> &actual,
                           std::string_view name) {
  struct Boundary {
    std::string_view name;
    std::string_view source_type;
    std::string_view native_message;
  };
  constexpr Boundary boundaries[] = {
      {"json-schema-string", "InvalidOperationException",
       "JSON value is not an Int32."},
      {"json-schema-wide", "FormatException",
       "JSON integer is outside Int32."},
      {"json-root-null", "InvalidOperationException",
       "JSON value is not an object."},
      {"json-null-outcomes", "NullReferenceException",
       "Adaptive Research outcome snapshot outcomes is null."},
      {"json-missing-summaries", "NullReferenceException",
       "Adaptive Research outcome snapshot summaries is null."},
      {"json-null-records", "ArgumentNullException",
       "Adaptive Research outcome snapshot recentRecords is null."},
      {"json-summary-object", "JsonException", "JSON value is not an array."},
      {"json-records-object", "JsonException", "JSON value is not an array."},
      {"json-outcome-unicode-whitespace-plus-rejected", "JsonException",
       "JSON enum value is invalid."},
  };
  const auto found = std::ranges::find(boundaries, name, &Boundary::name);
  if (found == std::end(boundaries)) {
    compare_error(expected, actual, name);
    return;
  }
  require(expected.at("Type").get<std::string>() == found->source_type,
          std::string(name) + " source parser category changed.");
  require(actual && actual->type == "JsonException" &&
              actual->message == found->native_message,
          std::string(name) + " native JSON boundary changed.");
}

double projected_number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity") return std::numeric_limits<double>::infinity();
  if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
  throw std::runtime_error("Unknown projected number '" + text + "'.");
}

AdaptiveResearchStateSnapshotV5 typed_input(const Json &input) {
  AdaptiveResearchStateSnapshotV5 snapshot;
  snapshot.schema_version = input.at("SchemaVersion").get<int>();
  snapshot.catalog_id = input.at("CatalogId").get<std::string>();
  snapshot.research = detail::decode_adaptive_research_snapshot_v4_dto(
      input.at("Research").dump());
  const auto &outcomes = input.at("Outcomes");
  for (const auto &value : outcomes.at("Summaries")) {
    snapshot.outcomes.summaries.push_back({
        value.at("NodeId").get<std::string>(), value.at("Attempts").get<int>(),
        value.at("Setbacks").get<int>(),
        value.at("PartialSuccesses").get<int>(),
        value.at("Refinements").get<int>(), value.at("Disproofs").get<int>(),
        value.at("Anomalies").get<int>(), value.at("Hazards").get<int>(),
        value.at("SideDiscoveries").get<int>(),
        value.at("LastOutcomeId").is_null()
            ? std::optional<std::string>{}
            : value.at("LastOutcomeId").get<std::string>(),
        projected_number(value.at("LastOutcomeYear"))});
  }
  for (const auto &value : outcomes.at("RecentRecords")) {
    snapshot.outcomes.recent_records.push_back({
        value.at("Sequence").get<std::int64_t>(),
        value.at("NodeId").get<std::string>(),
        value.at("CheckpointId").get<std::string>(),
        value.at("AttemptIndex").get<int>(),
        static_cast<ResearchOutcomeKind>(value.at("Outcome").get<int>()),
        value.at("SideDiscoveryNodeId").is_null()
            ? std::optional<std::string>{}
            : value.at("SideDiscoveryNodeId").get<std::string>(),
        projected_number(value.at("Year")),
        value.at("Explanation").get<std::string>()});
  }
  return snapshot;
}

void compare_projection(const Json &expected,
                        const AdaptiveResearchStrategicRuntime &runtime,
                        const AdaptiveResearchCivilizationState &state,
                        std::string_view name) {
  require(state.civilization_id() == expected.at("CivilizationId").get<std::string>(),
          std::string(name) + " civilization differed.");
  require(state.revision() == expected.at("Revision").get<std::int64_t>(),
          std::string(name) + " state revision differed.");
  require(state.materialized_view_revision() ==
              expected.at("MaterializedViewRevision").get<std::int64_t>(),
          std::string(name) + " materialized revision differed.");
  require(state.expertise().revision() ==
              expected.at("ExpertiseRevision").get<std::int64_t>(),
          std::string(name) + " expertise revision differed.");
  const auto &nodes = expected.at("NodeRevisions");
  require(nodes.size() == state.node_states().size(),
          std::string(name) + " node count differed.");
  for (std::size_t index = 0; index < nodes.size(); ++index)
    require(nodes[index].at("NodeId").get<std::string>() ==
                    state.node_states()[index].node_id &&
                nodes[index].at("Revision").get<std::int64_t>() ==
                    state.node_states()[index].revision,
            std::string(name) + " node revision/order differed.");
  const auto &projects = expected.at("ProjectRevisions");
  require(projects.size() == state.active_projects().size(),
          std::string(name) + " project count differed.");
  for (std::size_t index = 0; index < projects.size(); ++index)
    require(projects[index].at("NodeId").get<std::string>() ==
                    state.active_projects()[index].node_id &&
                projects[index].at("Revision").get<std::int64_t>() ==
                    state.active_projects()[index].revision,
            std::string(name) + " project revision/order differed.");
  const auto &actual = runtime.outcomes().state(state);
  require(actual.revision() == expected.at("OutcomeRevision").get<std::int64_t>(),
          std::string(name) + " outcome revision differed.");
  const auto &summaries = expected.at("Summaries");
  require(actual.summaries().size() == summaries.size(),
          std::string(name) + " summary count differed.");
  for (std::size_t index = 0; index < summaries.size(); ++index) {
    const auto &left = actual.summaries()[index];
    const auto &right = summaries[index];
    require(left.node_id == right.at("NodeId").get<std::string>() &&
                left.attempts == right.at("Attempts").get<int>() &&
                left.setbacks == right.at("Setbacks").get<int>() &&
                left.partial_successes == right.at("PartialSuccesses").get<int>() &&
                left.refinements == right.at("Refinements").get<int>() &&
                left.disproofs == right.at("Disproofs").get<int>() &&
                left.anomalies == right.at("Anomalies").get<int>() &&
                left.hazards == right.at("Hazards").get<int>() &&
                left.side_discoveries == right.at("SideDiscoveries").get<int>() &&
                left.last_outcome_id ==
                    (right.at("LastOutcomeId").is_null()
                         ? std::optional<std::string>{}
                         : std::optional<std::string>{
                               right.at("LastOutcomeId").get<std::string>()}) &&
                ((std::isnan(left.last_outcome_year) &&
                  std::isnan(projected_number(right.at("LastOutcomeYear")))) ||
                 left.last_outcome_year ==
                     projected_number(right.at("LastOutcomeYear"))),
            std::string(name) + " summary/order differed.");
  }
  const auto &records = expected.at("RecentRecords");
  require(actual.recent_records().size() == records.size(),
          std::string(name) + " history count differed.");
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto &left = actual.recent_records()[index];
    const auto &right = records[index];
    require(left.sequence == right.at("Sequence").get<std::int64_t>() &&
                left.node_id == right.at("NodeId").get<std::string>() &&
                left.checkpoint_id == right.at("CheckpointId").get<std::string>() &&
                left.attempt_index == right.at("AttemptIndex").get<int>() &&
                static_cast<int>(left.outcome) == right.at("Outcome").get<int>() &&
                left.side_discovery_node_id ==
                    (right.at("SideDiscoveryNodeId").is_null()
                         ? std::optional<std::string>{}
                         : std::optional<std::string>{
                               right.at("SideDiscoveryNodeId").get<std::string>()}) &&
                left.year == projected_number(right.at("Year")) &&
                left.explanation == right.at("Explanation").get<std::string>(),
            std::string(name) + " history/order differed.");
  }
}

void compare_next(const Json &expected, const PlannedResearchOutcome &actual,
                  std::string_view name) {
  require(actual.node_id == expected.at("NodeId").get<std::string>() &&
              actual.checkpoint_id == expected.at("CheckpointId").get<std::string>() &&
              actual.attempt_index == expected.at("AttemptIndex").get<int>() &&
              static_cast<int>(actual.profile) == expected.at("Profile").get<int>() &&
              static_cast<int>(actual.outcome) == expected.at("Outcome").get<int>() &&
              actual.deterministic_roll == expected.at("DeterministicRoll").get<double>() &&
              actual.planned_side_discovery_node_id ==
                  (expected.at("PlannedSideDiscoveryNodeId").is_null()
                       ? std::optional<std::string>{}
                       : std::optional<std::string>{expected.at(
                             "PlannedSideDiscoveryNodeId").get<std::string>()}) &&
              actual.explanation == expected.at("Explanation").get<std::string>(),
          std::string(name) + " next deterministic outcome differed.");
}

} // namespace

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument(
        "Expected actual-source fixture and canonical research directory.");
  const auto fixture_path = std::filesystem::absolute(argv[1]);
  const auto research_path = std::filesystem::absolute(argv[2]);
  const auto fixture = Json::parse(read(fixture_path));
  const auto canonical = fingerprint(research_path);
  require(canonical == fixture.at("CanonicalFingerprint").get<std::string>(),
          "Canonical research fingerprint differed from actual source.");
  auto runtime = load_adaptive_research_strategic_runtime(research_path);
  require(fingerprint(research_path) == canonical,
          "Runtime loading changed canonical research inputs.");
  AdaptiveResearchOutcomeSnapshotCodec original_codec(runtime);
  auto codec = std::move(original_codec);
  const auto profiles = runtime.authority().starting_profiles().reference_profile_ids();

  std::size_t count{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto kind = row.at("Kind").get<std::string>();
    if (kind == "mutated-catalog-typed-restore")
      require(row.at("BeforeFingerprint").get<std::string>() ==
                  row.at("AfterFingerprint").get<std::string>(),
              name + " source scratch inputs changed during invocation.");
    else
      require(row.at("BeforeFingerprint").get<std::string>() == canonical &&
                  row.at("AfterFingerprint").get<std::string>() == canonical,
              name + " source inputs were not canonical and immutable.");
    if (kind == "canonical-roundtrip") {
      const auto profile = name.substr(std::string("profile-").size());
      const auto found = std::ranges::find(profiles, profile);
      require(found != profiles.end(), name + " profile is unknown.");
      const auto index = static_cast<std::size_t>(found - profiles.begin());
      auto state = runtime.authority()
                       .compose_reference_profile(
                           "fixture:v5:" + std::to_string(index), profile,
                           "fixture:context:" + std::to_string(index),
                           2300 + static_cast<double>(index))
                       .state;
      const auto serialized = codec.serialize(state);
      require(serialized == row.at("Serialized").get<std::string>(),
              name + " serialization bytes differed.");
      auto restored = codec.deserialize(serialized);
      compare_projection(row.at("Original"), runtime, state, name + " original");
      compare_projection(row.at("Restored"), runtime, restored, name + " restored");
    } else if (kind == "typed-restore") {
      const auto input = typed_input(row.at("Input"));
      std::optional<AdaptiveResearchCivilizationState> restored;
      const auto error = name == "typed-history-sequence-max-source-wrap"
                             ? invoke_overflow_boundary(
                                   [&] { restored.emplace(codec.restore(input)); })
                             : invoke(
                                   [&] { restored.emplace(codec.restore(input)); });
      if (name == "typed-history-sequence-max-source-wrap") {
        require(row.at("Error").is_null() &&
                    !row.at("Restored").is_null(),
                name + " source no longer accepts unchecked Int64 wrap.");
        require(error && error->type == "NativeOverflowBoundary" &&
                    error->message ==
                        "Adaptive Research outcome sequence overflow.",
                name + " native checked-overflow boundary changed.");
        ++count;
        continue;
      }
      compare_error(row.at("Error"), error, name);
      if (restored) {
        compare_projection(row.at("Restored"), runtime, *restored, name);
        if (!row.at("NextOutcome").is_null()) {
          const auto &expected = row.at("NextOutcome");
          const auto actual = runtime.outcomes().plan_outcome(
              *restored, expected.at("NodeId").get<std::string>(),
              "fixture-seed", "checkpoint:next", "fixture:outcome:context");
          compare_next(expected, actual, name);
        }
        std::optional<std::string> serialized;
        const auto serialize_error = invoke(
            [&] { serialized.emplace(codec.serialize(*restored)); });
        if (!row.at("SerializeError").is_null()) {
          require(row.at("SerializeError").at("Type").get<std::string>() ==
                      "ArgumentException",
                  name + " source nonfinite category changed.");
          require(serialize_error && serialize_error->type == "JsonException" &&
                      serialize_error->message ==
                          "Non-finite values cannot be written as JSON numbers.",
                  name + " native nonfinite JSON boundary changed.");
        } else {
          compare_error(row.at("SerializeError"), serialize_error,
                        name + " serialize");
        }
        if (serialized)
          require(*serialized == row.at("RestoredSerialized").get<std::string>(),
                  name + " restored serialization differed.");
      }
    } else if (kind == "mutated-catalog-typed-restore") {
      require(row.at("BaseFingerprint").get<std::string>() == canonical,
              name + " changed case did not derive from canonical inputs.");
      const auto relative = row.at("ChangedRelativePath").get<std::string>();
      const auto relative_path = std::filesystem::path(relative);
      require(!relative_path.is_absolute() && !relative_path.empty() &&
                  std::ranges::none_of(relative_path, [](const auto &part) {
                    return part == "." || part == "..";
                  }),
              name + " retained an unsafe changed path.");
      OwnedScratch scratch(research_path);
      const auto changed =
          std::filesystem::weakly_canonical(scratch.root()) / relative_path;
      require(changed.parent_path() ==
                  std::filesystem::weakly_canonical(scratch.root()),
              name + " changed path escaped scratch root.");
      write_exact(changed, decode_base64(
                               row.at("ChangedBytesBase64").get<std::string>()));
      require(fingerprint(scratch.root()) ==
                  row.at("BeforeFingerprint").get<std::string>(),
              name + " native changed-input fingerprint differed.");
      auto changed_runtime = load_adaptive_research_strategic_runtime(scratch.root());
      AdaptiveResearchOutcomeSnapshotCodec changed_codec(changed_runtime);
      const auto input = typed_input(row.at("Input"));
      std::optional<AdaptiveResearchCivilizationState> restored;
      const auto error = invoke(
          [&] { restored.emplace(changed_codec.restore(input)); });
      compare_error(row.at("Error"), error, name);
      require(!restored, name + " unexpectedly returned a state.");
      require(fingerprint(scratch.root()) ==
                  row.at("AfterFingerprint").get<std::string>(),
              name + " native changed inputs mutated during restore.");
      scratch.cleanup();
    } else {
      const auto input = row.at("Input").get<std::string>();
      std::optional<AdaptiveResearchCivilizationState> restored;
      const auto error = invoke([&] { restored.emplace(codec.deserialize(input)); });
      compare_json_boundary(row.at("Error"), error, name);
      if (restored)
        compare_projection(row.at("Restored"), runtime, *restored, name);
    }
    ++count;
  }
  require(count == fixture.at("RowCount").get<std::size_t>(),
          "Not every actual-source row was replayed.");
  AdaptiveResearchOutcomeSnapshotCodec assigned(runtime);
  assigned = std::move(codec);
  auto moved_state = runtime.authority()
                         .compose_reference_profile(
                             "fixture:moved-codec", profiles.front(),
                             "fixture:moved-codec:context", 2340)
                         .state;
  require(!assigned.serialize(moved_state).empty(),
          "Move-assigned snapshot codec could not execute.");
  require(fingerprint(research_path) ==
              fixture.at("FinalFingerprint").get<std::string>(),
          "Native replay changed canonical research inputs.");
  std::cout << "Adaptive Research outcome snapshot parity passed " << count
            << " actual-source rows.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n'
            << "exception=" << typeid(error).name() << '\n'
            << "cwd=" << std::filesystem::current_path().string() << '\n'
            << "fixture=" << (argc > 1 ? argv[1] : "<missing>") << '\n'
            << "researchRoot=" << (argc > 2 ? argv[2] : "<missing>") << '\n';
  return 1;
}
