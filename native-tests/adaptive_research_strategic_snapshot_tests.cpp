#include <stellar/core/adaptive_research_strategic_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_agenda_support_access.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

using Json = nlohmann::json;
using namespace stellar::core;

static_assert(std::is_move_constructible_v<AdaptiveResearchStrategicSnapshotCodec>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchStrategicSnapshotCodec>);
static_assert(!std::is_constructible_v<AdaptiveResearchStrategicSnapshotCodec,
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
      {reinterpret_cast<const std::uint8_t *>(combined.data()),
       combined.size()}));
}

struct Error {
  std::string type;
  std::string message;
};

template <class Call>
std::optional<Error> invoke_semantic(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const AdaptiveResearchStrategicSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const std::invalid_argument &error) {
    return Error{"ArgumentException", error.what()};
  } catch (const std::out_of_range &error) {
    return Error{"ArgumentOutOfRangeException", error.what()};
  }
  return std::nullopt;
}

template <class Call>
std::optional<Error> invoke_priority(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const std::out_of_range &error) {
    return Error{"KeyNotFoundException", error.what()};
  }
  return std::nullopt;
}

template <class Call>
std::optional<Error> invoke_json(Call &&call) {
  try {
    std::forward<Call>(call)();
  } catch (const AdaptiveResearchSnapshotJsonError &error) {
    return Error{"JsonException", error.what()};
  } catch (const AdaptiveResearchSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  } catch (const AdaptiveResearchStrategicSnapshotError &error) {
    return Error{"InvalidDataException", error.what()};
  }
  return std::nullopt;
}

void compare_error(const Json &expected, const std::optional<Error> &actual,
                   std::string_view name, bool message_exact = true) {
  if (expected.is_null()) {
    require(!actual, std::string(name) + " unexpectedly failed.");
    return;
  }
  require(actual.has_value(), std::string(name) + " unexpectedly succeeded.");
  require(actual->type == expected.at("Type").get<std::string>(),
          std::string(name) + " error type differed: " + actual->type);
  if (message_exact)
    require(actual->message == expected.at("Message").get<std::string>(),
            std::string(name) + " error message differed: " + actual->message);
}

const ResearchPressureRuleDefinition &metric_rule(
    const AdaptiveResearchPressureCatalog &catalog) {
  for (const auto &rule : catalog.rules())
    if (!rule.metric_signal_ids.empty())
      return rule;
  throw std::runtime_error("Canonical pressure catalog has no metric signal.");
}

struct Inputs {
  std::string metric_id;
  std::string pressure_id;
  std::string domain_id;
  std::string field_id;
  std::string capability_id;
  std::string priority_id;
  std::string axis_id;
};

Inputs inputs(const AdaptiveResearchStrategicRuntime &runtime) {
  const auto &rule = metric_rule(runtime.pressure_catalog());
  const auto default_priority = runtime.agenda_catalog().runtime_policy().default_priority_id;
  auto priority = runtime.agenda_catalog().priorities().begin();
  while (priority != runtime.agenda_catalog().priorities().end() &&
         priority->id == default_priority)
    ++priority;
  require(priority != runtime.agenda_catalog().priorities().end(),
          "Canonical agenda has no non-default priority.");
  return {rule.metric_signal_ids.front(), rule.id,
          runtime.authority().catalog().nodes().front().domain_id,
          runtime.authority().expertise_catalog().fields().front().id,
          runtime.authority().catalog().capabilities().front().id,
          priority->id, runtime.agenda_catalog().culture_axes().front().id};
}

AdaptiveResearchCivilizationState compose(
    const AdaptiveResearchStrategicRuntime &runtime, std::string civilization,
    std::string profile, std::string context, double year) {
  return runtime.authority()
      .compose_reference_profile(std::move(civilization), std::move(profile),
                                 std::move(context), year)
      .state;
}

void configure_first(AdaptiveResearchStrategicRuntime &runtime,
                     AdaptiveResearchCivilizationState &state,
                     const Inputs &value) {
  runtime.pressure().report_metric_signal(state, value.metric_id, .625);
  runtime.agenda().set_domain_priority(state, value.domain_id,
                                       value.priority_id);
  runtime.agenda().set_field_priority(state, value.field_id, value.priority_id);
  runtime.agenda().set_problem_priority(state, value.pressure_id,
                                        value.priority_id);
  runtime.agenda().set_capability_priority(state, value.capability_id,
                                           value.priority_id);
  runtime.agenda().set_orientations(state, {11, 22, 33, 44});
  runtime.agenda().set_scientific_culture_axis(state, value.axis_id, 77);
  runtime.agenda().apply_recommendation(
      state, {value.domain_id, value.priority_id, 1, 2, 3, "fixture"},
      2234.5, "fixture provenance");
}

void compare_next_behavior(const Json &row,
                           AdaptiveResearchStrategicRuntime &runtime,
                           const AdaptiveResearchCivilizationState &state,
                           const Inputs &value) {
  const auto expected_target = row.at("NextMetricTarget");
  if (!expected_target.is_null())
    require(std::abs(runtime.pressure().get_metric_target(state,
                                                          value.pressure_id) -
                     expected_target.get<double>()) < 1e-12,
            row.at("Name").get<std::string>() + " pressure target differed.");
  const auto expected = row.at("NextShortlist");
  if (expected.is_null())
    return;
  const auto actual = runtime.agenda().build_visible_shortlist(state);
  require(std::min<std::size_t>(3, actual.size()) == expected.size(),
          row.at("Name").get<std::string>() + " shortlist size differed.");
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(actual[index].node_id ==
                expected[index].at("NodeId").get<std::string>(),
            row.at("Name").get<std::string>() + " shortlist node differed.");
    require(std::abs(actual[index].utility_score -
                     expected[index].at("UtilityScore").get<double>()) < 1e-10,
            row.at("Name").get<std::string>() + " shortlist score differed.");
  }
}

void compare_transient_revisions(
    const Json &expected, const AdaptiveResearchStrategicRuntime &runtime,
    const AdaptiveResearchCivilizationState &state, std::string_view name) {
  require(state.revision() == expected.at("Revision").get<std::int64_t>(),
          std::string(name) + " state revision differed.");
  require(state.materialized_view_revision() ==
              expected.at("MaterializedViewRevision").get<std::int64_t>(),
          std::string(name) + " materialized revision differed.");
  require(state.expertise().revision() ==
              expected.at("ExpertiseRevision").get<std::int64_t>(),
          std::string(name) + " expertise revision differed.");
  const auto &expected_nodes = expected.at("NodeStates");
  require(state.node_states().size() == expected_nodes.size(),
          std::string(name) + " node-state count differed.");
  for (std::size_t index = 0; index < expected_nodes.size(); ++index) {
    require(state.node_states()[index].node_id ==
                expected_nodes[index].at("NodeId").get<std::string>() &&
                state.node_states()[index].revision ==
                    expected_nodes[index].at("Revision").get<std::int64_t>(),
            std::string(name) + " node revision/order differed.");
  }
  const auto &expected_projects = expected.at("ActiveProjects");
  require(state.active_projects().size() == expected_projects.size(),
          std::string(name) + " active-project count differed.");
  for (std::size_t index = 0; index < expected_projects.size(); ++index) {
    require(state.active_projects()[index].node_id ==
                expected_projects[index].at("NodeId").get<std::string>() &&
                state.active_projects()[index].revision ==
                    expected_projects[index].at("Revision").get<std::int64_t>(),
            std::string(name) + " project revision/order differed.");
  }
  require(runtime.pressure().get_support_state(state).revision() ==
              expected.at("Pressure").at("Revision").get<std::int64_t>(),
          std::string(name) + " pressure support revision differed.");
  require(runtime.agenda().state(state).revision() ==
              expected.at("Agenda").at("Revision").get<std::int64_t>(),
          std::string(name) + " agenda revision differed.");
}

AdaptiveResearchStateSnapshotV3 typed_snapshot(
    std::string_view name, const AdaptiveResearchStateSnapshotV3 &base,
    const Inputs &value, const AdaptiveResearchStrategicRuntime &runtime) {
  auto result = base;
  result.pressure_support = {};
  result.agenda.domain_priorities.clear();
  result.agenda.field_priorities.clear();
  result.agenda.problem_priorities.clear();
  result.agenda.capability_priorities.clear();
  result.agenda.culture_axes.clear();
  result.agenda.last_major_review_year.reset();
  if (name == "typed-schema") result.schema_version = 4;
  else if (name == "typed-catalog") result.catalog_id = "wrong";
  else if (name == "typed-catalog-before-support") {
    result.catalog_id = "wrong";
    result.pressure_support.metric_signals.push_back({"unknown", 1});
  } else if (name == "typed-unknown-metric")
    result.pressure_support.metric_signals.push_back({"unknown", .5});
  else if (name == "typed-metric-zero")
    result.pressure_support.metric_signals.push_back({value.metric_id, 0});
  else if (name == "typed-metric-negative")
    result.pressure_support.metric_signals.push_back({value.metric_id, -1e-5});
  else if (name == "typed-metric-over-one")
    result.pressure_support.metric_signals.push_back({value.metric_id, 1.0001});
  else if (name == "typed-metric-nan")
    result.pressure_support.metric_signals.push_back(
        {value.metric_id, std::numeric_limits<double>::quiet_NaN()});
  else if (name == "typed-metric-positive-infinity")
    result.pressure_support.metric_signals.push_back(
        {value.metric_id, std::numeric_limits<double>::infinity()});
  else if (name == "typed-unknown-active-pressure")
    result.pressure_support.active_pressure_ids.push_back("unknown");
  else if (name == "typed-unknown-domain")
    result.agenda.domain_priorities.push_back({"unknown", value.priority_id});
  else if (name == "typed-invalid-domain-priority")
    result.agenda.domain_priorities.push_back({value.domain_id, "unknown"});
  else if (name == "typed-unknown-field")
    result.agenda.field_priorities.push_back({"unknown", value.priority_id});
  else if (name == "typed-unknown-problem")
    result.agenda.problem_priorities.push_back({"unknown", value.priority_id});
  else if (name == "typed-unknown-capability")
    result.agenda.capability_priorities.push_back({"unknown", value.priority_id});
  else if (name == "typed-invalid-orientation")
    result.agenda.orientations = {-1, 0, 0, 0};
  else if (name == "typed-unknown-axis")
    result.agenda.culture_axes.push_back({"unknown", 50});
  else if (name == "typed-invalid-axis")
    result.agenda.culture_axes.push_back(
        {value.axis_id, std::numeric_limits<double>::quiet_NaN()});
  else if (name == "typed-nonfinite-review-captured-null") {
    result.agenda.last_major_review_year =
        std::numeric_limits<double>::infinity();
    result.agenda.policy_provenance = "infinity";
  } else if (name == "typed-preserves-agenda-insertion-order") {
    std::vector<std::string> domains;
    for (const auto &node : runtime.authority().catalog().nodes())
      if (std::ranges::find(domains, node.domain_id) == domains.end())
        domains.push_back(node.domain_id);
    require(domains.size() >= 2, "Canonical catalog has fewer than two domains.");
    result.agenda.domain_priorities = {
        {domains[1], value.priority_id}, {domains[0], value.priority_id}};
  }
  return result;
}

} // namespace

int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::invalid_argument(
        "Expected actual-source fixture and canonical research directory.");
  const auto fixture_path = std::filesystem::absolute(argv[1]);
  const auto research_path = std::filesystem::absolute(argv[2]);
  const auto fixture = Json::parse(read(fixture_path));
  const auto canonical_fingerprint = fingerprint(research_path);
  require(canonical_fingerprint ==
              fixture.at("CanonicalFingerprint").get<std::string>(),
          "Canonical research fingerprint differed from actual source.");
  auto runtime = load_adaptive_research_strategic_runtime(research_path);
  require(fingerprint(research_path) == canonical_fingerprint,
          "Strategic runtime loading changed canonical research inputs.");
  AdaptiveResearchStrategicSnapshotCodec codec(runtime);
  auto moved = std::move(codec);
  const auto value = inputs(runtime);
  const auto profiles = runtime.authority().starting_profiles().reference_profile_ids();
  const auto boundary_state = compose(runtime, "fixture:boundary",
                                      std::string(profiles.front()),
                                      "fixture:boundary:context", 2260);
  const auto boundary = moved.capture(boundary_state);

  std::size_t count{};
  for (const auto &row : fixture.at("Rows")) {
    const auto name = row.at("Name").get<std::string>();
    const auto kind = row.at("Kind").get<std::string>();
    require(row.at("BeforeFingerprint").get<std::string>() ==
                canonical_fingerprint &&
                row.at("AfterFingerprint").get<std::string>() ==
                    canonical_fingerprint,
            name + " source fingerprints were not canonical and immutable.");
    if (kind == "canonical-roundtrip") {
      const auto profile = name.substr(std::string("profile-").size());
      const auto found = std::ranges::find(profiles, profile);
      require(found != profiles.end(), name + " profile is unknown.");
      const auto index = static_cast<std::size_t>(found - profiles.begin());
      auto state = compose(runtime, "fixture:v3:" + std::to_string(index),
                           profile, "fixture:context:" + std::to_string(index),
                           2200 + static_cast<double>(index));
      if (index == 0) configure_first(runtime, state, value);
      const auto serialized = moved.serialize(state);
      require(serialized == row.at("Serialized").get<std::string>(),
              name + " serialized bytes differed.");
      auto restored = moved.deserialize(serialized);
      require(moved.serialize(restored) ==
                  row.at("RestoredSerialized").get<std::string>(),
              name + " restored state differed.");
      compare_next_behavior(row, runtime, restored, value);
    } else if (kind == "fallback") {
      const auto input = row.at("Input").get<std::string>();
      auto restored = moved.deserialize(input);
      require(moved.serialize(restored) ==
                  row.at("RestoredSerialized").get<std::string>(),
              name + " fallback state differed.");
      compare_next_behavior(row, runtime, restored, value);
    } else if (kind == "typed-restore") {
      const auto snapshot = typed_snapshot(name, boundary, value, runtime);
      std::optional<Error> error;
      if (name == "typed-invalid-domain-priority")
        error = invoke_priority([&] { static_cast<void>(moved.restore(snapshot)); });
      else
        error = invoke_semantic([&] { static_cast<void>(moved.restore(snapshot)); });
      compare_error(row.at("Error"), error, name);
      if (row.at("Error").is_null()) {
        auto restored = moved.restore(snapshot);
        require(!row.at("Restored").is_null(), name + " lacks source state.");
        require(moved.serialize(restored) ==
                    row.at("RestoredSerialized").get<std::string>(),
                name + " successful typed restore state/support differed.");
        compare_transient_revisions(row.at("Restored"), runtime, restored,
                                    name);
        compare_next_behavior(row, runtime, restored, value);
        if (name == "typed-preserves-agenda-insertion-order") {
          const auto priorities = runtime.agenda().state(restored).domain_priorities();
          require(priorities.size() == 2 &&
                      priorities[0].key == snapshot.agenda.domain_priorities[0].key &&
                      priorities[1].key == snapshot.agenda.domain_priorities[1].key,
                  name + " changed dictionary insertion order.");
        }
      }
    } else if (kind == "capture-nonfinite") {
      require(row.at("Error").is_null(), name + " source capture failed.");
      auto state = compose(runtime, "fixture:nonfinite",
                           std::string(profiles.front()),
                           "fixture:nonfinite:context", 2261);
      auto &agenda = detail::AdaptiveResearchAgendaSupportAccess::state(
          runtime.agenda(), state);
      detail::AdaptiveResearchAgendaSupportAccess::set_review_metadata_unchecked(
              agenda, std::numeric_limits<double>::infinity(), "default");
      const auto captured = moved.capture(state);
      require(!captured.agenda.last_major_review_year,
              name + " native capture did not normalize nonfinite review.");
      require(moved.serialize(state) == row.at("Serialized").get<std::string>(),
              name + " native nonfinite serialization differed.");
    } else {
      const auto input = row.at("Input").get<std::string>();
      const auto error = invoke_json(
          [&] { static_cast<void>(moved.deserialize(input)); });
      if (name == "json-nested-schema1" ||
          name == "json-nested-missing-schema" ||
          name == "json-missing-schema") {
        compare_error(row.at("Error"), error, name);
      } else if (kind == "source-null-boundary") {
        require(error.has_value(), name + " native boundary was accepted.");
      } else if (kind == "duplicate-scalar") {
        compare_error(row.at("Error"), error, name);
      } else {
        require(error.has_value(), name + " malformed JSON was accepted.");
      }
    }
    require(fingerprint(research_path) == canonical_fingerprint,
            name + " native production call changed canonical inputs.");
    ++count;
  }
  require(count == fixture.at("RowCount").get<std::size_t>(),
          "Fixture row count differed.");
  std::cout << "Adaptive Research strategic snapshot parity passed " << count
            << " actual-source rows.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  std::cerr << "cwd=" << std::filesystem::current_path().string() << '\n';
  if (argc > 1) std::cerr << "fixture=" << argv[1] << '\n';
  if (argc > 2) std::cerr << "researchRoot=" << argv[2] << '\n';
  return 1;
}
