#include <stellar/core/adaptive_research_outcome_snapshot.hpp>

#include <stellar/core/detail/adaptive_research_foreign_technology_snapshot_json.hpp>
#include <stellar/core/detail/dotnet_json_enum.hpp>
#include <stellar/core/detail/adaptive_research_outcome_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_outcome_support_access.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>

namespace stellar::core {
namespace {

using Json = nlohmann::ordered_json;
using Writer = detail::AdaptiveResearchOutcomeStateWriter;

[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchOutcomeSnapshotError(std::move(message));
}

int checked_int32(const Json &value) {
  if (value.is_number_unsigned()) {
    const auto raw = value.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      throw AdaptiveResearchSnapshotJsonError("JSON integer is outside Int32.");
    return static_cast<int>(raw);
  }
  if (!value.is_number_integer())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not an Int32.");
  const auto raw = value.get<std::int64_t>();
  if (raw < std::numeric_limits<int>::min() ||
      raw > std::numeric_limits<int>::max())
    throw AdaptiveResearchSnapshotJsonError("JSON integer is outside Int32.");
  return static_cast<int>(raw);
}

std::int64_t checked_int64(const Json &value) {
  if (value.is_number_unsigned()) {
    const auto raw = value.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()))
      throw AdaptiveResearchSnapshotJsonError("JSON integer is outside Int64.");
    return static_cast<std::int64_t>(raw);
  }
  if (!value.is_number_integer())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not an Int64.");
  return value.get<std::int64_t>();
}

const Json &object(const Json &value) {
  if (!value.is_object())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not an object.");
  return value;
}

const Json &array(const Json &value) {
  if (!value.is_array())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not an array.");
  return value;
}

std::string string_default(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end() || found->is_null())
    return {};
  if (!found->is_string())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a string.");
  return found->get<std::string>();
}

double number_default(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end())
    return 0;
  if (!found->is_number())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a number.");
  return found->get<double>();
}

int int32_default(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  return found == value.end() ? 0 : checked_int32(*found);
}

std::int64_t int64_default(const Json &value, std::string_view name) {
  const auto found = value.find(std::string(name));
  return found == value.end() ? 0 : checked_int64(*found);
}

std::optional<std::string> optional_string(const Json &value,
                                           std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end() || found->is_null())
    return std::nullopt;
  if (!found->is_string())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a string.");
  return found->get<std::string>();
}

std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t point{};
    std::size_t width{};
    if (first <= 0x7f) { point = first; width = 1; }
    else if (first >= 0xc2 && first <= 0xdf) { point = first & 0x1f; width = 2; }
    else if (first >= 0xe0 && first <= 0xef) { point = first & 0x0f; width = 3; }
    else if (first >= 0xf0 && first <= 0xf4) { point = first & 7; width = 4; }
    else throw std::invalid_argument("Research identifier is not valid UTF-8.");
    if (value.size() < width)
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    for (std::size_t index = 1; index < width; ++index) {
      const auto byte = static_cast<unsigned char>(value[index]);
      if ((byte & 0xc0) != 0x80)
        throw std::invalid_argument("Research identifier is not valid UTF-8.");
      point = (point << 6) | (byte & 0x3f);
    }
    if ((width == 3 &&
         (point < 0x800 || (point >= 0xd800 && point <= 0xdfff))) ||
        (width == 4 && (point < 0x10000 || point > 0x10ffff)))
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    if (point <= 0xffff) result.push_back(static_cast<std::uint16_t>(point));
    else {
      point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

Json finite_number(double value) {
  if (!std::isfinite(value))
    throw AdaptiveResearchSnapshotJsonError(
        "Non-finite values cannot be written as JSON numbers.");
  constexpr double int64_upper_exclusive = 9223372036854775808.0;
  if ((value != 0 || !std::signbit(value)) &&
      value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
      value < int64_upper_exclusive && std::trunc(value) == value)
    return static_cast<std::int64_t>(value);
  return value;
}

Json outcome_json(ResearchOutcomeKind value) {
  switch (value) {
  case ResearchOutcomeKind::progress: return "progress";
  case ResearchOutcomeKind::setback: return "setback";
  case ResearchOutcomeKind::partial_success: return "partialSuccess";
  case ResearchOutcomeKind::hypothesis_supported: return "hypothesisSupported";
  case ResearchOutcomeKind::hypothesis_refined: return "hypothesisRefined";
  case ResearchOutcomeKind::hypothesis_disproven: return "hypothesisDisproven";
  case ResearchOutcomeKind::anomalous_result: return "anomalousResult";
  case ResearchOutcomeKind::hazard_incident: return "hazardIncident";
  case ResearchOutcomeKind::side_discovery: return "sideDiscovery";
  }
  return static_cast<int>(value);
}

ResearchOutcomeKind parse_outcome(const Json &value) {
  if (value.is_number_integer() || value.is_number_unsigned())
    return static_cast<ResearchOutcomeKind>(checked_int32(value));
  if (!value.is_string())
    throw AdaptiveResearchSnapshotJsonError("JSON enum value is invalid.");
  constexpr std::pair<std::string_view, ResearchOutcomeKind> names[] = {
      {"progress", ResearchOutcomeKind::progress},
      {"setback", ResearchOutcomeKind::setback},
      {"partialSuccess", ResearchOutcomeKind::partial_success},
      {"hypothesisSupported", ResearchOutcomeKind::hypothesis_supported},
      {"hypothesisRefined", ResearchOutcomeKind::hypothesis_refined},
      {"hypothesisDisproven", ResearchOutcomeKind::hypothesis_disproven},
      {"anomalousResult", ResearchOutcomeKind::anomalous_result},
      {"hazardIncident", ResearchOutcomeKind::hazard_incident},
      {"sideDiscovery", ResearchOutcomeKind::side_discovery}};
  if (const auto parsed = detail::try_parse_dotnet_json_enum_string(
          value.get<std::string>(),
          std::span<const std::pair<std::string_view, ResearchOutcomeKind>>{
              names}))
    return *parsed;
  throw AdaptiveResearchSnapshotJsonError("JSON enum value is invalid.");
}

Json summary_json(const ResearchOutcomeNodeSummary &value) {
  return {{"nodeId", value.node_id},
          {"attempts", value.attempts},
          {"setbacks", value.setbacks},
          {"partialSuccesses", value.partial_successes},
          {"refinements", value.refinements},
          {"disproofs", value.disproofs},
          {"anomalies", value.anomalies},
          {"hazards", value.hazards},
          {"sideDiscoveries", value.side_discoveries},
          {"lastOutcomeId", value.last_outcome_id
                                ? Json(*value.last_outcome_id) : Json(nullptr)},
          {"lastOutcomeYear", finite_number(value.last_outcome_year)}};
}

Json record_json(const ResearchOutcomeHistoryRecord &value) {
  return {{"sequence", value.sequence},
          {"nodeId", value.node_id},
          {"checkpointId", value.checkpoint_id},
          {"attemptIndex", value.attempt_index},
          {"outcome", outcome_json(value.outcome)},
          {"sideDiscoveryNodeId", value.side_discovery_node_id
                                      ? Json(*value.side_discovery_node_id)
                                      : Json(nullptr)},
          {"year", finite_number(value.year)},
          {"explanation", value.explanation}};
}

ResearchOutcomeNodeSummary parse_summary(const Json &value) {
  const auto &root = object(value);
  return {string_default(root, "nodeId"), int32_default(root, "attempts"),
          int32_default(root, "setbacks"),
          int32_default(root, "partialSuccesses"),
          int32_default(root, "refinements"), int32_default(root, "disproofs"),
          int32_default(root, "anomalies"), int32_default(root, "hazards"),
          int32_default(root, "sideDiscoveries"),
          optional_string(root, "lastOutcomeId"),
          number_default(root, "lastOutcomeYear")};
}

ResearchOutcomeHistoryRecord parse_record(const Json &value) {
  const auto &root = object(value);
  const auto outcome = root.find("outcome");
  return {int64_default(root, "sequence"), string_default(root, "nodeId"),
          string_default(root, "checkpointId"),
          int32_default(root, "attemptIndex"),
          outcome == root.end() ? ResearchOutcomeKind::progress
                                : parse_outcome(*outcome),
          optional_string(root, "sideDiscoveryNodeId"),
          number_default(root, "year"), string_default(root, "explanation")};
}

bool has_node(const AdaptiveResearchCatalog &catalog, std::string_view id) {
  return std::ranges::any_of(catalog.nodes(),
                             [&](const auto &node) { return node.id == id; });
}

} // namespace

struct AdaptiveResearchOutcomeSnapshotCodec::Storage {
  const AdaptiveResearchStrategicRuntime *runtime;
  AdaptiveResearchForeignTechnologySnapshotCodec v4;
  explicit Storage(const AdaptiveResearchStrategicRuntime &value)
      : runtime(&value), v4(value) {}
};

AdaptiveResearchOutcomeSnapshotError::AdaptiveResearchOutcomeSnapshotError(
    std::string message) : std::runtime_error(std::move(message)) {}

AdaptiveResearchOutcomeSnapshotCodec::AdaptiveResearchOutcomeSnapshotCodec(
    const AdaptiveResearchStrategicRuntime &runtime)
    : storage_(std::make_unique<Storage>(runtime)) {}
AdaptiveResearchOutcomeSnapshotCodec::~AdaptiveResearchOutcomeSnapshotCodec() =
    default;
AdaptiveResearchOutcomeSnapshotCodec::AdaptiveResearchOutcomeSnapshotCodec(
    AdaptiveResearchOutcomeSnapshotCodec &&) noexcept = default;
AdaptiveResearchOutcomeSnapshotCodec &
AdaptiveResearchOutcomeSnapshotCodec::operator=(
    AdaptiveResearchOutcomeSnapshotCodec &&) noexcept = default;

AdaptiveResearchStateSnapshotV5 AdaptiveResearchOutcomeSnapshotCodec::capture(
    const AdaptiveResearchCivilizationState &state) const {
  const auto &outcomes = storage_->runtime->outcomes().state(state);
  AdaptiveResearchOutcomeSnapshot snapshot;
  snapshot.summaries.assign(outcomes.summaries().begin(),
                            outcomes.summaries().end());
  std::ranges::sort(snapshot.summaries, [](const auto &left, const auto &right) {
    return ordinal_less(left.node_id, right.node_id);
  });
  snapshot.recent_records.assign(outcomes.recent_records().begin(),
                                 outcomes.recent_records().end());
  std::ranges::stable_sort(snapshot.recent_records, {},
                           &ResearchOutcomeHistoryRecord::sequence);
  return {current_schema_version,
          storage_->runtime->authority().catalog().metadata().catalog_id,
          storage_->v4.capture(state), std::move(snapshot)};
}

std::string detail::encode_adaptive_research_snapshot_v5_dto(
    const AdaptiveResearchStateSnapshotV5 &snapshot) {
  try {
    Json summaries = Json::array();
    for (const auto &summary : snapshot.outcomes.summaries)
      summaries.push_back(summary_json(summary));
    Json records = Json::array();
    for (const auto &record : snapshot.outcomes.recent_records)
      records.push_back(record_json(record));
    return Json{{"schemaVersion", snapshot.schema_version},
                {"catalogId", snapshot.catalog_id},
                {"research", Json::parse(
                    detail::encode_adaptive_research_snapshot_v4_dto(
                        snapshot.research))},
                {"outcomes", {{"summaries", std::move(summaries)},
                               {"recentRecords", std::move(records)}}}}.dump();
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

AdaptiveResearchStateSnapshotV5
detail::decode_adaptive_research_snapshot_v5_dto(std::string_view text) {
  try {
    const auto root = object(Json::parse(text));
    AdaptiveResearchStateSnapshotV5 snapshot;
    snapshot.schema_version = int32_default(root, "schemaVersion");
    snapshot.catalog_id = string_default(root, "catalogId");
    const auto research = root.find("research");
    if (research == root.end() || research->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research outcome snapshot research is null.");
    snapshot.research = detail::decode_adaptive_research_snapshot_v4_dto(
        research->dump());
    const auto outcomes = root.find("outcomes");
    if (outcomes == root.end() || outcomes->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research outcome snapshot outcomes is null.");
    const auto &outcome_root = object(*outcomes);
    const auto summaries = outcome_root.find("summaries");
    if (summaries == outcome_root.end() || summaries->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research outcome snapshot summaries is null.");
    for (const auto &summary : array(*summaries))
      snapshot.outcomes.summaries.push_back(parse_summary(summary));
    const auto records = outcome_root.find("recentRecords");
    if (records == outcome_root.end() || records->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research outcome snapshot recentRecords is null.");
    for (const auto &record : array(*records))
      snapshot.outcomes.recent_records.push_back(parse_record(record));
    return snapshot;
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

std::string AdaptiveResearchOutcomeSnapshotCodec::serialize(
    const AdaptiveResearchCivilizationState &state) const {
  return detail::encode_adaptive_research_snapshot_v5_dto(capture(state));
}

AdaptiveResearchCivilizationState AdaptiveResearchOutcomeSnapshotCodec::deserialize(
    std::string_view text) const {
  Json root;
  try { root = Json::parse(text); }
  catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
  object(root);
  const auto schema = root.find("schemaVersion");
  if (schema == root.end())
    fail("Adaptive Research snapshot is missing schemaVersion.");
  const auto version = checked_int32(*schema);
  if ((version >= 1 && version <= 4) || version == AdaptiveResearchSnapshotCodec::current_schema_version)
    return storage_->v4.deserialize(text);
  if (version != current_schema_version)
    fail("Unsupported Adaptive Research outcome snapshot schema " +
         std::to_string(version) + ".");
  return restore(detail::decode_adaptive_research_snapshot_v5_dto(text));
}

AdaptiveResearchCivilizationState AdaptiveResearchOutcomeSnapshotCodec::restore(
    const AdaptiveResearchStateSnapshotV5 &snapshot) const {
  if (snapshot.schema_version != current_schema_version)
    fail("Unsupported Adaptive Research outcome snapshot schema " +
         std::to_string(snapshot.schema_version) + ".");
  const auto &runtime = *storage_->runtime;
  const auto &catalog_id = runtime.authority().catalog().metadata().catalog_id;
  if (snapshot.catalog_id != catalog_id)
    fail("Adaptive Research snapshot catalog '" + snapshot.catalog_id +
         "' does not match runtime catalog '" + catalog_id + "'.");
  auto state = storage_->v4.restore(snapshot.research);
  auto &outcomes = detail::AdaptiveResearchOutcomeSupportAccess::state(
      runtime.outcomes(), state);
  for (const auto &summary : snapshot.outcomes.summaries) {
    if (!has_node(runtime.authority().catalog(), summary.node_id))
      fail("Outcome summary references unknown node '" + summary.node_id +
           "'.");
    if (summary.attempts < 0 || summary.setbacks < 0 ||
        summary.partial_successes < 0 || summary.refinements < 0 ||
        summary.disproofs < 0 || summary.anomalies < 0 ||
        summary.hazards < 0 || summary.side_discoveries < 0)
      fail("Outcome summary for '" + summary.node_id +
           "' contains negative counters.");
    if (summary.last_outcome_id) {
      static_cast<void>(AdaptiveResearchOutcomeCatalog::parse_outcome(
          *summary.last_outcome_id));
    }
    if (summary.last_outcome_year !=
            -std::numeric_limits<double>::infinity() &&
        !std::isfinite(summary.last_outcome_year))
      fail("Outcome summary for '" + summary.node_id +
           "' has invalid last-outcome year.");
    Writer::restore_summary(outcomes, summary);
  }

  auto records = snapshot.outcomes.recent_records;
  std::ranges::stable_sort(records, {}, &ResearchOutcomeHistoryRecord::sequence);
  std::int64_t previous{};
  for (const auto &record : records) {
    if (record.sequence <= previous)
      fail("Outcome history sequence must be strictly increasing.");
    previous = record.sequence;
    if (!has_node(runtime.authority().catalog(), record.node_id))
      fail("Outcome history references unknown node '" + record.node_id +
           "'.");
    if (record.attempt_index < 0 || !std::isfinite(record.year))
      fail("Outcome history for '" + record.node_id +
           "' has invalid attempt/year data.");
    if (record.side_discovery_node_id &&
        !has_node(runtime.authority().catalog(),
                  *record.side_discovery_node_id))
      fail("Outcome history references unknown side-discovery node '" +
           *record.side_discovery_node_id + "'.");
    Writer::restore_record(outcomes, record);
  }
  const auto max_recent =
      runtime.outcome_catalog().policy().max_recent_outcome_records;
  if (max_recent < 0 ||
      outcomes.recent_records().size() > static_cast<std::size_t>(max_recent))
    fail("Outcome snapshot exceeds configured bounded recent-history capacity.");
  return state;
}

} // namespace stellar::core
