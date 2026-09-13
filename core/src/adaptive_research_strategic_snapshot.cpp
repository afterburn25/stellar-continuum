#include <stellar/core/adaptive_research_strategic_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_agenda_support_access.hpp>
#include <stellar/core/detail/adaptive_research_expertise_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_pressure_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_pressure_support_access.hpp>
#include <stellar/core/detail/adaptive_research_strategic_snapshot_json.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace stellar::core {
namespace {

using Json = nlohmann::ordered_json;
using PressureWriter = detail::AdaptiveResearchPressureStateWriter;
using AgendaWriter = detail::AdaptiveResearchAgendaStateWriter;

std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t point{};
    std::size_t width{};
    if (first <= 0x7f) {
      point = first;
      width = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      point = first & 0x1f;
      width = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      point = first & 0x0f;
      width = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      point = first & 7;
      width = 4;
    } else {
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
    }
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
    if (point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(point));
    } else {
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

template <class Entry, class Projection>
void ordinal_sort(std::vector<Entry> &values, Projection projection) {
  std::ranges::sort(values, [&](const auto &left, const auto &right) {
    return ordinal_less(std::invoke(projection, left),
                        std::invoke(projection, right));
  });
}

[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchStrategicSnapshotError(std::move(message));
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

std::string string(const Json &value) {
  if (!value.is_string())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a string.");
  return value.get<std::string>();
}

std::string nullable_string_default(const Json &object_value,
                                    std::string_view name) {
  const auto found = object_value.find(std::string(name));
  if (found == object_value.end() || found->is_null())
    return {};
  return string(*found);
}

double number(const Json &value) {
  if (!value.is_number())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a number.");
  return value.get<double>();
}

Json json_number(double value) {
  if (!std::isfinite(value))
    throw AdaptiveResearchSnapshotJsonError(
        "Non-finite values cannot be written as JSON numbers.");
  constexpr double int64_upper_exclusive = 9223372036854775808.0;
  if ((value != 0.0 || !std::signbit(value)) &&
      value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
      value < int64_upper_exclusive && std::trunc(value) == value)
    return static_cast<std::int64_t>(value);
  return value;
}

std::vector<ResearchAgendaPriorityEntry> priorities(const Json &value) {
  std::vector<ResearchAgendaPriorityEntry> result;
  for (const auto &[key, item] : object(value).items())
    result.push_back({key, string(item)});
  return result;
}

AdaptiveResearchAgendaSnapshot parse_agenda(const Json &value) {
  const auto &root = object(value);
  AdaptiveResearchAgendaSnapshot result;
  result.domain_priorities = priorities(root.at("domainPriorities"));
  result.field_priorities = priorities(root.at("fieldPriorities"));
  result.problem_priorities = priorities(root.at("problemPriorities"));
  result.capability_priorities = priorities(root.at("capabilityPriorities"));
  const auto &orientations = object(root.at("orientations"));
  result.orientations = {
      number(orientations.value("basicVsAppliedOrientation", Json(0.0))),
      number(orientations.value("competencePreservationPolicy", Json(0.0))),
      number(orientations.value("portfolioDiversityPolicy", Json(0.0))),
      number(orientations.value("foreignScienceEngagement", Json(0.0)))};
  for (const auto &[key, item] : object(root.at("cultureAxes")).items())
    result.culture_axes.push_back({key, number(item)});
  if (const auto found = root.find("lastMajorReviewYear");
      found != root.end() && !found->is_null())
    result.last_major_review_year = number(*found);
  const auto provenance = root.find("policyProvenance");
  if (provenance != root.end() && !provenance->is_null())
    result.policy_provenance = string(*provenance);
  return result;
}

AdaptiveResearchPressureSupportSnapshot parse_pressure(const Json &value) {
  const auto &root = object(value);
  AdaptiveResearchPressureSupportSnapshot result;
  for (const auto &[key, item] : object(root.at("metricSignals")).items())
    result.metric_signals.push_back({key, number(item)});
  for (const auto &item : array(root.at("activePressureIds")))
    result.active_pressure_ids.push_back(string(item));
  return result;
}

Json priority_json(std::span<const ResearchAgendaPriorityEntry> values) {
  Json result = Json::object();
  for (const auto &value : values) result[value.key] = value.priority_id;
  return result;
}

Json agenda_json(const AdaptiveResearchAgendaSnapshot &value) {
  Json axes = Json::object();
  for (const auto &axis : value.culture_axes)
    axes[axis.axis_id] = json_number(axis.value);
  return {{"domainPriorities", priority_json(value.domain_priorities)},
          {"fieldPriorities", priority_json(value.field_priorities)},
          {"problemPriorities", priority_json(value.problem_priorities)},
          {"capabilityPriorities", priority_json(value.capability_priorities)},
          {"orientations",
           {{"basicVsAppliedOrientation",
             json_number(value.orientations.basic_vs_applied_orientation)},
            {"competencePreservationPolicy",
             json_number(value.orientations.competence_preservation_policy)},
            {"portfolioDiversityPolicy",
             json_number(value.orientations.portfolio_diversity_policy)},
            {"foreignScienceEngagement",
             json_number(value.orientations.foreign_science_engagement)}}},
          {"cultureAxes", std::move(axes)},
          {"lastMajorReviewYear",
           value.last_major_review_year ? json_number(*value.last_major_review_year)
                                        : Json(nullptr)},
          {"policyProvenance", value.policy_provenance}};
}

Json pressure_json(const AdaptiveResearchPressureSupportSnapshot &value) {
  Json signals = Json::object();
  for (const auto &entry : value.metric_signals)
    signals[entry.signal_id] = json_number(entry.value);
  return {{"metricSignals", std::move(signals)},
          {"activePressureIds", value.active_pressure_ids}};
}

struct StrategicSupportSnapshot {
  AdaptiveResearchPressureSupportSnapshot pressure;
  AdaptiveResearchAgendaSnapshot agenda;
};

StrategicSupportSnapshot capture_support(
    const AdaptiveResearchStrategicRuntime &runtime,
    const AdaptiveResearchCivilizationState &state) {
  const auto &pressure = runtime.pressure().get_support_state(state);
  const auto &agenda = runtime.agenda().state(state);
  StrategicSupportSnapshot result;
  result.pressure.metric_signals.assign(pressure.metric_signals().begin(),
                                        pressure.metric_signals().end());
  ordinal_sort(result.pressure.metric_signals,
               &ResearchPressureMetricSignalEntry::signal_id);
  result.pressure.active_pressure_ids.assign(
      pressure.active_pressure_ids().begin(), pressure.active_pressure_ids().end());
  std::set<std::string, std::less<>> seen(
      result.pressure.active_pressure_ids.begin(),
      result.pressure.active_pressure_ids.end());
  for (const auto &entry : state.pressures())
    if (entry.value > 0 && seen.insert(entry.pressure_id).second)
      result.pressure.active_pressure_ids.push_back(entry.pressure_id);
  std::ranges::sort(result.pressure.active_pressure_ids, ordinal_less);

  auto copy_priorities = [](auto values) {
    return std::vector<ResearchAgendaPriorityEntry>(values.begin(), values.end());
  };
  result.agenda.domain_priorities = copy_priorities(agenda.domain_priorities());
  result.agenda.field_priorities = copy_priorities(agenda.field_priorities());
  result.agenda.problem_priorities = copy_priorities(agenda.problem_priorities());
  result.agenda.capability_priorities =
      copy_priorities(agenda.capability_priorities());
  for (auto *values : {&result.agenda.domain_priorities,
                       &result.agenda.field_priorities,
                       &result.agenda.problem_priorities,
                       &result.agenda.capability_priorities})
    ordinal_sort(*values, &ResearchAgendaPriorityEntry::key);
  result.agenda.orientations = agenda.orientations();
  result.agenda.culture_axes.assign(agenda.culture_axes().begin(),
                                    agenda.culture_axes().end());
  ordinal_sort(result.agenda.culture_axes,
               &ResearchScientificCultureAxisState::axis_id);
  if (std::isfinite(agenda.last_major_review_year()))
    result.agenda.last_major_review_year = agenda.last_major_review_year();
  result.agenda.policy_provenance = agenda.policy_provenance();
  return result;
}

void restore_support(const AdaptiveResearchStrategicRuntime &runtime,
                     AdaptiveResearchCivilizationState &state,
                     const AdaptiveResearchPressureSupportSnapshot &pressure_snapshot,
                     const AdaptiveResearchAgendaSnapshot &agenda_snapshot) {
  auto &pressure = detail::AdaptiveResearchPressureSupportAccess::state(
      runtime.pressure(), state);
  for (const auto &entry : pressure_snapshot.metric_signals) {
    if (!runtime.pressure_catalog().is_known_metric_signal(entry.signal_id))
      fail("Strategic snapshot references unknown Pressure metric signal '" +
           entry.signal_id + "'.");
    if (entry.value <= 0 || entry.value > 1 || !std::isfinite(entry.value))
      fail("Strategic snapshot has invalid metric signal value " +
           detail::legacy_general(entry.value) + " for '" + entry.signal_id +
           "'.");
    PressureWriter::set_metric_signal(pressure, entry.signal_id, entry.value);
  }
  const auto known_pressure = [&](std::string_view id) {
    return std::ranges::any_of(runtime.pressure_catalog().rules(),
                               [&](const auto &rule) { return rule.id == id; });
  };
  for (const auto &id : pressure_snapshot.active_pressure_ids) {
    if (!known_pressure(id))
      fail("Strategic snapshot references unknown activated Pressure '" + id +
           "'.");
    PressureWriter::activate_pressure(pressure, id);
  }
  for (const auto &entry : state.pressures())
    if (entry.value > 0 && known_pressure(entry.pressure_id))
      PressureWriter::activate_pressure(pressure, entry.pressure_id);

  for (const auto &entry : agenda_snapshot.domain_priorities)
    runtime.agenda().set_domain_priority(state, entry.key, entry.priority_id);
  for (const auto &entry : agenda_snapshot.field_priorities)
    runtime.agenda().set_field_priority(state, entry.key, entry.priority_id);
  for (const auto &entry : agenda_snapshot.problem_priorities)
    runtime.agenda().set_problem_priority(state, entry.key, entry.priority_id);
  for (const auto &entry : agenda_snapshot.capability_priorities)
    runtime.agenda().set_capability_priority(state, entry.key,
                                             entry.priority_id);
  runtime.agenda().set_orientations(state, agenda_snapshot.orientations);
  for (const auto &axis : agenda_snapshot.culture_axes)
    runtime.agenda().set_scientific_culture_axis(state, axis.axis_id,
                                                 axis.value);
  if (agenda_snapshot.last_major_review_year)
    AgendaWriter::mark_reviewed(
        detail::AdaptiveResearchAgendaSupportAccess::state(runtime.agenda(),
                                                           state),
        *agenda_snapshot.last_major_review_year,
        agenda_snapshot.policy_provenance);
}

} // namespace

std::string detail::encode_adaptive_research_snapshot_v3_dto(
    const AdaptiveResearchStateSnapshotV3 &snapshot) {
  try {
    Json root = {
        {"schemaVersion", snapshot.schema_version},
        {"catalogId", snapshot.catalog_id},
        {"research",
         Json::parse(detail::encode_adaptive_research_snapshot_v2_dto(
             snapshot.research))},
        {"pressureSupport", pressure_json(snapshot.pressure_support)},
        {"agenda", agenda_json(snapshot.agenda)},
    };
    return root.dump();
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

AdaptiveResearchStateSnapshotV3
detail::decode_adaptive_research_snapshot_v3_dto(std::string_view text) {
  try {
    const auto root = Json::parse(text);
    object(root);
    AdaptiveResearchStateSnapshotV3 snapshot;
    const auto schema = root.find("schemaVersion");
    snapshot.schema_version = schema == root.end() ? 0 : checked_int32(*schema);
    snapshot.catalog_id = nullable_string_default(root, "catalogId");
    const auto research = root.find("research");
    if (research == root.end() || research->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research strategic snapshot research is null.");
    snapshot.research = detail::decode_adaptive_research_snapshot_v2_dto(
        research->dump());
    const auto pressure = root.find("pressureSupport");
    if (pressure == root.end() || pressure->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research strategic snapshot pressureSupport is null.");
    snapshot.pressure_support = parse_pressure(*pressure);
    const auto agenda = root.find("agenda");
    if (agenda == root.end() || agenda->is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research strategic snapshot agenda is null.");
    snapshot.agenda = parse_agenda(*agenda);
    return snapshot;
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

struct AdaptiveResearchStrategicSnapshotCodec::Storage {
  const AdaptiveResearchStrategicRuntime *runtime;
  AdaptiveResearchSnapshotV2Codec v2;
  explicit Storage(const AdaptiveResearchStrategicRuntime &value)
      : runtime(&value), v2(value.authority()) {}
};

AdaptiveResearchStrategicSnapshotError::AdaptiveResearchStrategicSnapshotError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

AdaptiveResearchStrategicSnapshotCodec::AdaptiveResearchStrategicSnapshotCodec(
    const AdaptiveResearchStrategicRuntime &runtime)
    : storage_(std::make_unique<Storage>(runtime)) {}
AdaptiveResearchStrategicSnapshotCodec::~AdaptiveResearchStrategicSnapshotCodec() =
    default;
AdaptiveResearchStrategicSnapshotCodec::AdaptiveResearchStrategicSnapshotCodec(
    AdaptiveResearchStrategicSnapshotCodec &&) noexcept = default;
AdaptiveResearchStrategicSnapshotCodec &
AdaptiveResearchStrategicSnapshotCodec::operator=(
    AdaptiveResearchStrategicSnapshotCodec &&) noexcept = default;

AdaptiveResearchStateSnapshotV3 AdaptiveResearchStrategicSnapshotCodec::capture(
    const AdaptiveResearchCivilizationState &state) const {
  auto support = capture_support(*storage_->runtime, state);
  return {current_schema_version,
          storage_->runtime->authority().catalog().metadata().catalog_id,
          storage_->v2.capture(state), std::move(support.pressure),
          std::move(support.agenda)};
}

std::string AdaptiveResearchStrategicSnapshotCodec::serialize(
    const AdaptiveResearchCivilizationState &state) const {
  return detail::encode_adaptive_research_snapshot_v3_dto(capture(state));
}

AdaptiveResearchCivilizationState
AdaptiveResearchStrategicSnapshotCodec::deserialize(std::string_view json) const {
  Json root;
  try {
    root = Json::parse(json);
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
  object(root);
  const auto schema_found = root.find("schemaVersion");
  if (schema_found == root.end())
    fail("Adaptive Research snapshot is missing schemaVersion.");
  const auto schema = checked_int32(*schema_found);
  if (schema == 1 || schema == 2) {
    auto state = storage_->v2.deserialize(json);
    auto &support = detail::AdaptiveResearchPressureSupportAccess::state(
        storage_->runtime->pressure(), state);
    const auto known_pressure = [&](std::string_view id) {
      return std::ranges::any_of(
          storage_->runtime->pressure_catalog().rules(),
          [&](const auto &rule) { return rule.id == id; });
    };
    for (const auto &entry : state.pressures())
      if (entry.value > 0 && known_pressure(entry.pressure_id))
        PressureWriter::activate_pressure(support, entry.pressure_id);
    return state;
  }
  if (schema != current_schema_version)
    fail("Unsupported Adaptive Research strategic snapshot schema " +
         std::to_string(schema) + ".");
  return restore(detail::decode_adaptive_research_snapshot_v3_dto(json));
}

AdaptiveResearchCivilizationState AdaptiveResearchStrategicSnapshotCodec::restore(
    const AdaptiveResearchStateSnapshotV3 &snapshot) const {
  if (snapshot.schema_version != current_schema_version)
    fail("Unsupported Adaptive Research strategic snapshot schema " +
         std::to_string(snapshot.schema_version) + ".");
  if (snapshot.catalog_id !=
      storage_->runtime->authority().catalog().metadata().catalog_id)
    fail("Adaptive Research snapshot catalog '" + snapshot.catalog_id +
         "' does not match runtime catalog '" +
         storage_->runtime->authority().catalog().metadata().catalog_id + "'.");
  auto state = storage_->v2.restore(snapshot.research);
  restore_support(*storage_->runtime, state, snapshot.pressure_support,
                  snapshot.agenda);
  return state;
}

} // namespace stellar::core
