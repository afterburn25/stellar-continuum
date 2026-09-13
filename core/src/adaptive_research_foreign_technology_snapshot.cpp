#include <stellar/core/adaptive_research_foreign_technology_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_snapshot_json.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_support_access.hpp>
#include <stellar/core/detail/adaptive_research_strategic_snapshot_json.hpp>
#include <stellar/core/detail/dotnet_json_enum.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;
using Writer = detail::AdaptiveResearchForeignTechnologyStateWriter;

[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchForeignTechnologySnapshotError(std::move(message));
}

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t point = first;
  std::size_t width = 1;
  if ((first & 0xe0) == 0xc0) {
    point = first & 0x1f;
    width = 2;
  } else if ((first & 0xf0) == 0xe0) {
    point = first & 0x0f;
    width = 3;
  } else if ((first & 0xf8) == 0xf0) {
    point = first & 7;
    width = 4;
  } else if (first >= 0x80)
    return false;
  if (value.size() < width)
    return false;
  for (std::size_t index = 1; index < width; ++index) {
    const auto byte = static_cast<unsigned char>(value[index]);
    if ((byte & 0xc0) != 0x80)
      return false;
    point = (point << 6) | (byte & 0x3f);
  }
  value.remove_prefix(width);
  return (point >= 0x09 && point <= 0x0d) || point == 0x20 || point == 0x85 ||
         point == 0xa0 || point == 0x1680 ||
         (point >= 0x2000 && point <= 0x200a) || point == 0x2028 ||
         point == 0x2029 || point == 0x202f || point == 0x205f ||
         point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

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
    } else
      throw std::invalid_argument("Research identifier is not valid UTF-8.");
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
    if (point <= 0xffff)
      result.push_back(static_cast<std::uint16_t>(point));
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
double number(const Json &value) {
  if (!value.is_number())
    throw AdaptiveResearchSnapshotJsonError("JSON value is not a number.");
  return value.get<double>();
}
Json finite_number(double value) {
  if (!std::isfinite(value))
    throw AdaptiveResearchSnapshotJsonError(
        ".NET number values such as positive and negative infinity cannot be "
        "written as valid JSON. To make it work when using 'JsonSerializer', "
        "consider specifying "
        "'JsonNumberHandling.AllowNamedFloatingPointLiterals' (see "
        "https://docs.microsoft.com/dotnet/api/"
        "system.text.json.serialization.jsonnumberhandling).");
  constexpr double upper_exclusive = 9223372036854775808.0;
  if ((value != 0.0 || !std::signbit(value)) &&
      value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
      value < upper_exclusive && std::trunc(value) == value)
    return static_cast<std::int64_t>(value);
  return value;
}
std::vector<std::string> strings(const Json &value) {
  std::vector<std::string> result;
  for (const auto &entry : array(value))
    result.push_back(string(entry));
  return result;
}

template <class Enum>
Enum parse_enum(
    const Json &value,
    std::initializer_list<std::pair<std::string_view, Enum>> names) {
  if (value.is_number_integer() || value.is_number_unsigned())
    return static_cast<Enum>(checked_int32(value));
  if (const auto parsed = detail::try_parse_dotnet_json_enum_string<Enum>(
          string(value), {names.begin(), names.size()}))
    return *parsed;
  throw AdaptiveResearchSnapshotJsonError("JSON enum value is invalid.");
}

template <class Enum>
Json enum_json(Enum value,
               std::initializer_list<std::pair<Enum, std::string_view>> names) {
  for (const auto &[member, name] : names)
    if (value == member)
      return std::string(name);
  return static_cast<int>(value);
}

Json assessment_json(const ForeignTechnologyAssessmentSnapshot &value) {
  return {
      {"foreignTechnologyReference", value.foreign_technology_reference},
      {"sourceLineageReference", value.source_lineage_reference},
      {"understanding",
       enum_json(value.understanding,
                 {{ForeignUnderstandingState::unknown, "unknown"},
                  {ForeignUnderstandingState::observed, "observed"},
                  {ForeignUnderstandingState::characterized, "characterized"},
                  {ForeignUnderstandingState::principle_understood,
                   "principleUnderstood"},
                  {ForeignUnderstandingState::engineering_understood,
                   "engineeringUnderstood"}})},
      {"operability",
       enum_json(
           value.operability,
           {{ForeignOperabilityState::unknown, "unknown"},
            {ForeignOperabilityState::unusable, "unusable"},
            {ForeignOperabilityState::origin_only, "originOnly"},
            {ForeignOperabilityState::supported_operation,
             "supportedOperation"},
            {ForeignOperabilityState::adapted_operation, "adaptedOperation"},
            {ForeignOperabilityState::native_operation, "nativeOperation"}})},
      {"reproduction",
       enum_json(value.reproduction,
                 {{ForeignReproductionState::none, "none"},
                  {ForeignReproductionState::component_replication,
                   "componentReplication"},
                  {ForeignReproductionState::subsystem_replication,
                   "subsystemReplication"},
                  {ForeignReproductionState::foreign_process_replication,
                   "foreignProcessReplication"},
                  {ForeignReproductionState::native_process_replication,
                   "nativeProcessReplication"}})},
      {"adaptation",
       enum_json(
           value.adaptation,
           {{ForeignAdaptationState::none, "none"},
            {ForeignAdaptationState::conceptual_inspiration,
             "conceptualInspiration"},
            {ForeignAdaptationState::interface_adaptation,
             "interfaceAdaptation"},
            {ForeignAdaptationState::native_derivative, "nativeDerivative"},
            {ForeignAdaptationState::hybrid_lineage, "hybridLineage"}})},
      {"knownConstraintIds", value.known_constraint_ids},
      {"evidenceRefs", value.evidence_refs},
      {"tacitAssetRefs", value.tacit_asset_refs},
      {"lastAssessmentYear", finite_number(value.last_assessment_year)},
      {"confidence", finite_number(value.confidence)}};
}

Json package_json(const ForeignTechnologyPackageSnapshot &value) {
  return {{"packageId", value.package_id},
          {"foreignTechnologyReference", value.foreign_technology_reference},
          {"sourceLineageReference", value.source_lineage_reference},
          {"componentIds", value.component_ids},
          {"rightIds", value.right_ids},
          {"evidenceRefs", value.evidence_refs},
          {"tacitAssetRefs", value.tacit_asset_refs},
          {"knowledgeFieldIds", value.knowledge_field_ids},
          {"provenance", value.provenance},
          {"integrity", finite_number(value.integrity)}};
}

ForeignTechnologyAssessmentSnapshot parse_assessment(const Json &value) {
  const auto &v = object(value);
  return {
      string(v.value("foreignTechnologyReference", Json(""))),
      string(v.value("sourceLineageReference", Json(""))),
      parse_enum<ForeignUnderstandingState>(
          v.value("understanding", Json(0)),
          {{"unknown", ForeignUnderstandingState::unknown},
           {"observed", ForeignUnderstandingState::observed},
           {"characterized", ForeignUnderstandingState::characterized},
           {"principleUnderstood",
            ForeignUnderstandingState::principle_understood},
           {"engineeringUnderstood",
            ForeignUnderstandingState::engineering_understood}}),
      parse_enum<ForeignOperabilityState>(
          v.value("operability", Json(0)),
          {{"unknown", ForeignOperabilityState::unknown},
           {"unusable", ForeignOperabilityState::unusable},
           {"originOnly", ForeignOperabilityState::origin_only},
           {"supportedOperation", ForeignOperabilityState::supported_operation},
           {"adaptedOperation", ForeignOperabilityState::adapted_operation},
           {"nativeOperation", ForeignOperabilityState::native_operation}}),
      parse_enum<ForeignReproductionState>(
          v.value("reproduction", Json(0)),
          {{"none", ForeignReproductionState::none},
           {"componentReplication",
            ForeignReproductionState::component_replication},
           {"subsystemReplication",
            ForeignReproductionState::subsystem_replication},
           {"foreignProcessReplication",
            ForeignReproductionState::foreign_process_replication},
           {"nativeProcessReplication",
            ForeignReproductionState::native_process_replication}}),
      parse_enum<ForeignAdaptationState>(
          v.value("adaptation", Json(0)),
          {{"none", ForeignAdaptationState::none},
           {"conceptualInspiration",
            ForeignAdaptationState::conceptual_inspiration},
           {"interfaceAdaptation",
            ForeignAdaptationState::interface_adaptation},
           {"nativeDerivative", ForeignAdaptationState::native_derivative},
           {"hybridLineage", ForeignAdaptationState::hybrid_lineage}}),
      strings(v.value("knownConstraintIds", Json::array())),
      strings(v.value("evidenceRefs", Json::array())),
      strings(v.value("tacitAssetRefs", Json::array())),
      number(v.value("lastAssessmentYear", Json(0))),
      number(v.value("confidence", Json(0)))};
}

ForeignTechnologyPackageSnapshot parse_package(const Json &value) {
  const auto &v = object(value);
  return {string(v.value("packageId", Json(""))),
          string(v.value("foreignTechnologyReference", Json(""))),
          string(v.value("sourceLineageReference", Json(""))),
          strings(v.value("componentIds", Json::array())),
          strings(v.value("rightIds", Json::array())),
          strings(v.value("evidenceRefs", Json::array())),
          strings(v.value("tacitAssetRefs", Json::array())),
          strings(v.value("knowledgeFieldIds", Json::array())),
          string(v.value("provenance", Json(""))),
          number(v.value("integrity", Json(0)))};
}
} // namespace

struct AdaptiveResearchForeignTechnologySnapshotCodec::Storage {
  const AdaptiveResearchStrategicRuntime *runtime;
  AdaptiveResearchStrategicSnapshotCodec v3;
  explicit Storage(const AdaptiveResearchStrategicRuntime &value)
      : runtime(&value), v3(value) {}
};

AdaptiveResearchForeignTechnologySnapshotError::
    AdaptiveResearchForeignTechnologySnapshotError(std::string message)
    : std::runtime_error(std::move(message)) {}
AdaptiveResearchForeignTechnologySnapshotCodec::
    AdaptiveResearchForeignTechnologySnapshotCodec(
        const AdaptiveResearchStrategicRuntime &runtime)
    : storage_(std::make_unique<Storage>(runtime)) {}
AdaptiveResearchForeignTechnologySnapshotCodec::
    ~AdaptiveResearchForeignTechnologySnapshotCodec() = default;
AdaptiveResearchForeignTechnologySnapshotCodec::
    AdaptiveResearchForeignTechnologySnapshotCodec(
        AdaptiveResearchForeignTechnologySnapshotCodec &&) noexcept = default;
AdaptiveResearchForeignTechnologySnapshotCodec &
AdaptiveResearchForeignTechnologySnapshotCodec::operator=(
    AdaptiveResearchForeignTechnologySnapshotCodec &&) noexcept = default;

AdaptiveResearchStateSnapshotV4
AdaptiveResearchForeignTechnologySnapshotCodec::capture(
    const AdaptiveResearchCivilizationState &state) const {
  AdaptiveResearchStateSnapshotV4 result{
      current_schema_version,
      storage_->runtime->authority().catalog().metadata().catalog_id,
      storage_->v3.capture(state)};
  const auto &foreign = storage_->runtime->foreign_technology().state(state);
  for (const auto &v : foreign.assessments())
    result.foreign_assessments.push_back(
        {v.foreign_technology_reference, v.source_lineage_reference,
         v.understanding, v.operability, v.reproduction, v.adaptation,
         v.known_constraint_ids, v.evidence_refs, v.tacit_asset_refs,
         v.last_assessment_year, v.confidence});
  for (const auto &v : foreign.packages())
    result.foreign_packages.push_back(
        {v.package_id, v.foreign_technology_reference,
         v.source_lineage_reference, v.component_ids, v.right_ids,
         v.evidence_refs, v.tacit_asset_refs, v.knowledge_field_ids,
         v.provenance, v.integrity});
  std::ranges::sort(result.foreign_assessments,
                    [](const auto &a, const auto &b) {
                      return ordinal_less(a.foreign_technology_reference,
                                          b.foreign_technology_reference);
                    });
  std::ranges::sort(result.foreign_packages, [](const auto &a, const auto &b) {
    return ordinal_less(a.package_id, b.package_id);
  });
  return result;
}

std::string detail::encode_adaptive_research_snapshot_v4_dto(
    const AdaptiveResearchStateSnapshotV4 &value) {
  try {
    Json assessments = Json::array();
    Json packages = Json::array();
    for (const auto &entry : value.foreign_assessments)
      assessments.push_back(assessment_json(entry));
    for (const auto &entry : value.foreign_packages)
      packages.push_back(package_json(entry));
    return Json{{"schemaVersion", value.schema_version},
                {"catalogId", value.catalog_id},
                {"research",
                 Json::parse(detail::encode_adaptive_research_snapshot_v3_dto(
                     value.research))},
                {"foreignAssessments", std::move(assessments)},
                {"foreignPackages", std::move(packages)}}
        .dump();
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const nlohmann::json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

AdaptiveResearchStateSnapshotV4
detail::decode_adaptive_research_snapshot_v4_dto(std::string_view text) {
  try {
    const auto parsed = Json::parse(text);
    const auto &root = object(parsed);
    AdaptiveResearchStateSnapshotV4 dto;
    dto.schema_version = checked_int32(root.value("schemaVersion", Json(0)));
    dto.catalog_id = string(root.value("catalogId", Json("")));
    dto.research = detail::decode_adaptive_research_snapshot_v3_dto(
        root.value("research", Json(nullptr)).dump());
    for (const auto &v : array(root.value("foreignAssessments", Json::array())))
      dto.foreign_assessments.push_back(parse_assessment(v));
    for (const auto &v : array(root.value("foreignPackages", Json::array())))
      dto.foreign_packages.push_back(parse_package(v));
    return dto;
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const nlohmann::json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

std::string AdaptiveResearchForeignTechnologySnapshotCodec::serialize(
    const AdaptiveResearchCivilizationState &state) const {
  return detail::encode_adaptive_research_snapshot_v4_dto(capture(state));
}

AdaptiveResearchCivilizationState
AdaptiveResearchForeignTechnologySnapshotCodec::deserialize(
    std::string_view text) const {
  Json root;
  try {
    root = Json::parse(text);
  } catch (const nlohmann::json::exception &e) {
    throw AdaptiveResearchSnapshotJsonError(e.what());
  }
  object(root);
  const auto schema = root.find("schemaVersion");
  if (schema == root.end())
    fail("Adaptive Research snapshot is missing schemaVersion.");
  const auto version = checked_int32(*schema);
  if (version >= 1 && version <= 3)
    return storage_->v3.deserialize(text);
  if (version != current_schema_version)
    fail("Unsupported Adaptive Research foreign-technology snapshot schema " +
         std::to_string(version) + ".");
  return restore(detail::decode_adaptive_research_snapshot_v4_dto(text));
}

AdaptiveResearchCivilizationState
AdaptiveResearchForeignTechnologySnapshotCodec::restore(
    const AdaptiveResearchStateSnapshotV4 &snapshot) const {
  if (snapshot.schema_version != current_schema_version)
    fail("Unsupported Adaptive Research foreign-technology snapshot schema " +
         std::to_string(snapshot.schema_version) + ".");
  const auto &runtime = *storage_->runtime;
  const auto &catalog_id = runtime.authority().catalog().metadata().catalog_id;
  if (snapshot.catalog_id != catalog_id)
    fail("Adaptive Research snapshot catalog '" + snapshot.catalog_id +
         "' does not match runtime catalog '" + catalog_id + "'.");
  auto state = storage_->v3.restore(snapshot.research);
  auto &foreign =
      detail::AdaptiveResearchForeignTechnologySupportAccess::get_or_create(
          runtime.foreign_technology(), state);
  for (const auto &p : snapshot.foreign_packages) {
    if (blank(p.package_id) || blank(p.foreign_technology_reference) ||
        blank(p.source_lineage_reference))
      fail("Foreign package snapshot contains an empty stable reference.");
    if (p.integrity < 0 || p.integrity > 1 || std::isnan(p.integrity) ||
        std::isinf(p.integrity))
      fail("Foreign package '" + p.package_id + "' has invalid integrity " +
           detail::legacy_general(p.integrity) + ".");
    for (const auto &id : p.component_ids)
      if (!runtime.foreign_technology_catalog().has_component(id))
        fail("Foreign package '" + p.package_id +
             "' references unknown component '" + id + "'.");
    for (const auto &id : p.right_ids)
      if (!runtime.foreign_technology_catalog().has_right(id))
        fail("Foreign package '" + p.package_id +
             "' references unknown right '" + id + "'.");
    for (const auto &id : p.knowledge_field_ids)
      if (std::ranges::none_of(runtime.authority().expertise_catalog().fields(),
                               [&](const auto &f) { return f.id == id; }))
        fail("Foreign package '" + p.package_id +
             "' references unknown field '" + id + "'.");
    for (const auto &id : p.evidence_refs)
      if (std::ranges::none_of(state.evidence_instances(), [&](const auto &v) {
            return v.evidence_instance_id == id;
          }))
        fail("Foreign package '" + p.package_id +
             "' references missing evidence '" + id + "'.");
    for (const auto &id : p.tacit_asset_refs)
      if (std::ranges::none_of(state.expertise().tacit_assets(),
                               [&](const auto &v) { return v.asset_id == id; }))
        fail("Foreign package '" + p.package_id +
             "' references missing tacit asset '" + id + "'.");
    Writer::add_package(
        foreign, {p.package_id, p.foreign_technology_reference,
                  p.source_lineage_reference, p.component_ids, p.right_ids,
                  p.evidence_refs, p.tacit_asset_refs, p.knowledge_field_ids,
                  p.provenance, p.integrity, foreign.revision() + 1});
  }
  for (const auto &a : snapshot.foreign_assessments) {
    for (const auto &id : a.known_constraint_ids)
      if (!runtime.foreign_technology_catalog().has_constraint(id))
        fail("Foreign assessment '" + a.foreign_technology_reference +
             "' references unknown constraint '" + id + "'.");
    for (const auto &id : a.evidence_refs)
      if (std::ranges::none_of(state.evidence_instances(), [&](const auto &v) {
            return v.evidence_instance_id == id;
          }))
        fail("Foreign assessment '" + a.foreign_technology_reference +
             "' references missing evidence '" + id + "'.");
    for (const auto &id : a.tacit_asset_refs)
      if (std::ranges::none_of(state.expertise().tacit_assets(),
                               [&](const auto &v) { return v.asset_id == id; }))
        fail("Foreign assessment '" + a.foreign_technology_reference +
             "' references missing tacit asset '" + id + "'.");
    Writer::set_assessment(
        foreign,
        {a.foreign_technology_reference, a.source_lineage_reference,
         a.understanding, a.operability, a.reproduction, a.adaptation,
         a.known_constraint_ids, a.evidence_refs, a.tacit_asset_refs,
         a.last_assessment_year, a.confidence, foreign.revision() + 1});
  }
  return state;
}
} // namespace stellar::core
