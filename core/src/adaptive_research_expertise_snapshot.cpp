#include <stellar/core/adaptive_research_expertise_snapshot.hpp>

#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace stellar::core {
namespace {

using Json = nlohmann::ordered_json;
using StateWriter = detail::AdaptiveResearchStateWriter;
using ExpertiseWriter = detail::AdaptiveResearchExpertiseStateWriter;

[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchSnapshotError(std::move(message));
}

[[noreturn]] void json_fail(std::string message) {
  throw AdaptiveResearchSnapshotJsonError(std::move(message));
}

std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> result;
  while (!value.empty()) {
    const auto first = static_cast<unsigned char>(value.front());
    std::uint32_t code_point = first;
    std::size_t length = 1;
    if (first <= 0x7f) {
      code_point = first;
    } else if (first >= 0xc2 && first <= 0xdf) {
      code_point = first & 0x1f;
      length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      code_point = first & 0x0f;
      length = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code_point = first & 7;
      length = 4;
    } else {
      json_fail("Invalid UTF-8 in Adaptive Research v2 snapshot string.");
    }
    if (value.size() < length)
      json_fail("Invalid UTF-8 in Adaptive Research v2 snapshot string.");
    for (std::size_t index = 1; index < length; ++index) {
      const auto continuation = static_cast<unsigned char>(value[index]);
      if ((continuation & 0xc0) != 0x80)
        json_fail("Invalid UTF-8 in Adaptive Research v2 snapshot string.");
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if ((length == 3 && code_point < 0x800) ||
        (length == 4 && code_point < 0x10000) ||
        (code_point >= 0xd800 && code_point <= 0xdfff) ||
        code_point > 0x10ffff)
      json_fail("Invalid UTF-8 in Adaptive Research v2 snapshot string.");
    value.remove_prefix(length);
    if (code_point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(code_point));
    } else {
      code_point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (code_point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (code_point & 0x3ff)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

Json json_number(double value) {
  if (!std::isfinite(value))
    json_fail("Non-finite values cannot be written as JSON numbers.");
  constexpr double int64_upper_exclusive = 9223372036854775808.0;
  if ((value != 0.0 || !std::signbit(value)) &&
      value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
      value < int64_upper_exclusive && std::trunc(value) == value)
    return static_cast<std::int64_t>(value);
  return value;
}

Json optional(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

Json competence(const ResearchCompetenceVector &value) {
  return {{"theoretical", json_number(value.theoretical)},
          {"experimental", json_number(value.experimental)},
          {"engineering", json_number(value.engineering)}};
}

std::string_view scope_name(ResearchTacitScopeKind value) {
  switch (value) {
  case ResearchTacitScopeKind::knowledge_field: return "knowledgeField";
  case ResearchTacitScopeKind::technology_node: return "technologyNode";
  case ResearchTacitScopeKind::solution_family: return "solutionFamily";
  case ResearchTacitScopeKind::foreign_lineage: return "foreignLineage";
  case ResearchTacitScopeKind::facility_or_process: return "facilityOrProcess";
  }
  return {};
}

std::string_view assimilation_name(ResearchTacitAssimilationStage value) {
  switch (value) {
  case ResearchTacitAssimilationStage::access: return "access";
  case ResearchTacitAssimilationStage::interpreted: return "interpreted";
  case ResearchTacitAssimilationStage::codified: return "codified";
  case ResearchTacitAssimilationStage::trained: return "trained";
  case ResearchTacitAssimilationStage::native_practice: return "nativePractice";
  }
  return {};
}

Json expertise_json(const AdaptiveResearchExpertiseSnapshot &value) {
  Json result;
  result["fields"] = Json::array();
  for (const auto &field : value.fields)
    result["fields"].push_back(
        {{"fieldId", field.field_id},
         {"current", competence(field.current)},
         {"historicalPeak", competence(field.historical_peak)},
         {"lastTheoreticalActivityYear",
          json_number(field.last_theoretical_activity_year)},
         {"lastExperimentalActivityYear",
          json_number(field.last_experimental_activity_year)},
         {"lastEngineeringActivityYear",
          json_number(field.last_engineering_activity_year)}});
  result["institutions"] = Json::array();
  for (const auto &institution : value.institutions)
    result["institutions"].push_back(
        {{"institutionInstanceId", institution.institution_instance_id},
         {"institutionArchetypeId", institution.institution_archetype_id},
         {"contextId", optional(institution.context_id)},
         {"totalCount", institution.total_count},
         {"activeCount", institution.active_count}});
  result["tacitAssets"] = Json::array();
  for (const auto &asset : value.tacit_assets) {
    const auto scope = scope_name(asset.scope_kind);
    const auto assimilation = assimilation_name(asset.assimilation_stage);
    result["tacitAssets"].push_back(
        {{"assetId", asset.asset_id},
         {"assetTypeId", asset.asset_type_id},
         {"scopeKind", scope.empty() ? Json(static_cast<int>(asset.scope_kind))
                                     : Json(scope)},
         {"scopeRef", asset.scope_ref},
         {"assimilationStage",
          assimilation.empty()
              ? Json(static_cast<int>(asset.assimilation_stage))
              : Json(assimilation)},
         {"depth", json_number(asset.depth)},
         {"availability", json_number(asset.availability)},
         {"translationContextQuality",
          json_number(asset.translation_context_quality)},
         {"trainingContinuity", json_number(asset.training_continuity)},
         {"provenance", asset.provenance},
         {"contextId", optional(asset.context_id)}});
  }
  return result;
}

const Json &collection(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null())
    json_fail("Adaptive Research v2 snapshot collection '" +
              std::string(name) + "' is null.");
  if (!found->is_array())
    json_fail("Adaptive Research v2 snapshot collection '" +
              std::string(name) + "' has the wrong JSON shape.");
  return *found;
}

std::string required_string(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null())
    json_fail("Adaptive Research v2 snapshot record string '" +
              std::string(name) + "' is null.");
  if (!found->is_string())
    json_fail("Adaptive Research v2 snapshot record string '" +
              std::string(name) + "' has the wrong JSON shape.");
  return found->get<std::string>();
}

std::string nullable_string_default(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null()) return {};
  if (!found->is_string())
    json_fail("Adaptive Research v2 snapshot record string '" +
              std::string(name) + "' has the wrong JSON shape.");
  return found->get<std::string>();
}

template <class T>
T scalar(const Json &object, const char *name, T fallback = {}) {
  const auto found = object.find(name);
  if (found == object.end()) return fallback;
  if constexpr (std::is_floating_point_v<T>) {
    if (!found->is_number())
      json_fail("Adaptive Research v2 snapshot number '" +
                std::string(name) + "' has the wrong JSON shape.");
  }
  return found->get<T>();
}

int int32_scalar(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end()) return 0;
  if (!found->is_number_integer() && !found->is_number_unsigned())
    json_fail("Adaptive Research v2 snapshot integer '" + std::string(name) +
              "' must be an Int32 number.");
  if (found->is_number_unsigned()) {
    const auto value = found->get<std::uint64_t>();
    if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      json_fail("Adaptive Research v2 snapshot integer '" +
                std::string(name) + "' is outside Int32 range.");
    return static_cast<int>(value);
  }
  const auto value = found->get<std::int64_t>();
  if (value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    json_fail("Adaptive Research v2 snapshot integer '" + std::string(name) +
              "' is outside Int32 range.");
  return static_cast<int>(value);
}

std::optional<std::string> optional_string(const Json &object,
                                           const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null()) return std::nullopt;
  if (!found->is_string())
    json_fail("Adaptive Research v2 snapshot optional string '" +
              std::string(name) + "' has the wrong JSON shape.");
  return found->get<std::string>();
}

ResearchCompetenceVector parse_competence(const Json &value) {
  if (!value.is_object())
    json_fail("Adaptive Research competence vector has the wrong JSON shape.");
  return {scalar<double>(value, "theoretical"),
          scalar<double>(value, "experimental"),
          scalar<double>(value, "engineering")};
}

bool dotnet_whitespace(std::string_view code_unit) {
  if (code_unit.size() == 1) {
    const auto value = static_cast<unsigned char>(code_unit.front());
    return value == 0x20 || (value >= 0x09 && value <= 0x0d);
  }
  return code_unit == "\xc2\x85" || code_unit == "\xc2\xa0" ||
         code_unit == "\xe1\x9a\x80" || code_unit == "\xe2\x80\x80" ||
         code_unit == "\xe2\x80\x81" || code_unit == "\xe2\x80\x82" ||
         code_unit == "\xe2\x80\x83" || code_unit == "\xe2\x80\x84" ||
         code_unit == "\xe2\x80\x85" || code_unit == "\xe2\x80\x86" ||
         code_unit == "\xe2\x80\x87" || code_unit == "\xe2\x80\x88" ||
         code_unit == "\xe2\x80\x89" || code_unit == "\xe2\x80\x8a" ||
         code_unit == "\xe2\x80\xa8" || code_unit == "\xe2\x80\xa9" ||
         code_unit == "\xe2\x80\xaf" || code_unit == "\xe2\x81\x9f" ||
         code_unit == "\xe3\x80\x80";
}

std::string trim_dotnet_whitespace(std::string text) {
  auto unit_length = [](unsigned char first) -> std::size_t {
    if (first < 0x80) return 1;
    if ((first & 0xe0) == 0xc0) return 2;
    if ((first & 0xf0) == 0xe0) return 3;
    if ((first & 0xf8) == 0xf0) return 4;
    return 1;
  };
  while (!text.empty()) {
    const auto length = unit_length(static_cast<unsigned char>(text.front()));
    if (length > text.size() ||
        !dotnet_whitespace(std::string_view(text).substr(0, length)))
      break;
    text.erase(0, length);
  }
  while (!text.empty()) {
    std::size_t start = text.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
      --start;
    if (!dotnet_whitespace(std::string_view(text).substr(start))) break;
    text.erase(start);
  }
  return text;
}

template <class Enum>
Enum enum_value(const Json &value,
                std::span<const std::pair<std::string_view, Enum>> names) {
  if (value.is_number_integer() || value.is_number_unsigned()) {
    if (value.is_number_unsigned()) {
      const auto integer = value.get<std::uint64_t>();
      if (integer > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        json_fail("Adaptive Research v2 snapshot enum integer is outside Int32 range.");
      return static_cast<Enum>(static_cast<int>(integer));
    }
    const auto integer = value.get<std::int64_t>();
    if (integer < std::numeric_limits<int>::min() ||
        integer > std::numeric_limits<int>::max())
      json_fail("Adaptive Research v2 snapshot enum integer is outside Int32 range.");
    return static_cast<Enum>(static_cast<int>(integer));
  }
  if (!value.is_string())
    json_fail("Adaptive Research v2 snapshot enum has the wrong JSON shape.");
  auto text = trim_dotnet_whitespace(value.get<std::string>());
  int integer{};
  const auto numeric = text.starts_with('+') ? std::string_view(text).substr(1)
                                              : std::string_view(text);
  const auto parsed = std::from_chars(numeric.data(), numeric.data() + numeric.size(),
                                      integer);
  if (!numeric.empty() && parsed.ec == std::errc{} &&
      parsed.ptr == numeric.data() + numeric.size())
    return static_cast<Enum>(integer);
  auto fold = [](std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
      return character >= 'A' && character <= 'Z'
                 ? static_cast<char>(character + ('a' - 'A'))
                 : static_cast<char>(character);
    });
    return value;
  };
  text = fold(text);
  int combined{};
  bool saw_comma = false;
  std::size_t start{};
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    saw_comma = saw_comma || comma != std::string::npos;
    auto part = trim_dotnet_whitespace(
        text.substr(start, comma == std::string::npos ? std::string::npos
                                                      : comma - start));
    const auto found = std::ranges::find_if(names, [&](const auto &entry) {
      return part == fold(std::string(entry.first));
    });
    if (found == names.end()) break;
    combined |= static_cast<int>(found->second);
    if (comma == std::string::npos)
      return static_cast<Enum>(combined);
    start = comma + 1;
  }
  if (!saw_comma) {
    std::ranges::transform(text, text.begin(), [](unsigned char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character + ('a' - 'A'))
               : static_cast<char>(character);
    });
  }
  for (const auto &[name, result] : names) {
    std::string folded(name);
    std::ranges::transform(folded, folded.begin(), [](unsigned char character) {
      return character >= 'A' && character <= 'Z'
                 ? static_cast<char>(character + ('a' - 'A'))
                 : static_cast<char>(character);
    });
    if (text == folded) return result;
  }
  json_fail("Unknown Adaptive Research v2 snapshot enum value.");
}


AdaptiveResearchExpertiseSnapshot parse_expertise(const Json &value) {
  if (!value.is_object())
    json_fail("Adaptive Research v2 expertise must be an object.");
  AdaptiveResearchExpertiseSnapshot result;
  for (const auto &field : collection(value, "fields")) {
    const auto current = field.find("current");
    const auto peak = field.find("historicalPeak");
    if (current == field.end() || current->is_null())
      json_fail("Adaptive Research v2 snapshot field current vector is null.");
    if (peak == field.end() || peak->is_null())
      json_fail("Adaptive Research v2 snapshot field historicalPeak vector is null.");
    result.fields.push_back(
        {required_string(field, "fieldId"),
         parse_competence(*current), parse_competence(*peak),
         scalar<double>(field, "lastTheoreticalActivityYear"),
         scalar<double>(field, "lastExperimentalActivityYear"),
         scalar<double>(field, "lastEngineeringActivityYear")});
  }
  for (const auto &institution : collection(value, "institutions"))
    result.institutions.push_back(
        {required_string(institution, "institutionInstanceId"),
         required_string(institution, "institutionArchetypeId"),
         optional_string(institution, "contextId"),
         int32_scalar(institution, "totalCount"),
         int32_scalar(institution, "activeCount")});
  constexpr std::pair<std::string_view, ResearchTacitScopeKind> scopes[] = {
      {"knowledgeField", ResearchTacitScopeKind::knowledge_field},
      {"technologyNode", ResearchTacitScopeKind::technology_node},
      {"solutionFamily", ResearchTacitScopeKind::solution_family},
      {"foreignLineage", ResearchTacitScopeKind::foreign_lineage},
      {"facilityOrProcess", ResearchTacitScopeKind::facility_or_process},
  };
  constexpr std::pair<std::string_view, ResearchTacitAssimilationStage>
      assimilations[] = {
          {"access", ResearchTacitAssimilationStage::access},
          {"interpreted", ResearchTacitAssimilationStage::interpreted},
          {"codified", ResearchTacitAssimilationStage::codified},
          {"trained", ResearchTacitAssimilationStage::trained},
          {"nativePractice", ResearchTacitAssimilationStage::native_practice},
      };
  for (const auto &asset : collection(value, "tacitAssets")) {
    const auto scope = asset.find("scopeKind");
    const auto assimilation = asset.find("assimilationStage");
    result.tacit_assets.push_back(
        {required_string(asset, "assetId"),
         required_string(asset, "assetTypeId"),
         scope == asset.end()
             ? ResearchTacitScopeKind{}
             : enum_value<ResearchTacitScopeKind>(*scope, scopes),
         required_string(asset, "scopeRef"),
         assimilation == asset.end()
             ? ResearchTacitAssimilationStage{}
             : enum_value<ResearchTacitAssimilationStage>(*assimilation,
                                                           assimilations),
         scalar<double>(asset, "depth"),
         scalar<double>(asset, "availability"),
         scalar<double>(asset, "translationContextQuality"),
         scalar<double>(asset, "trainingContinuity"),
         required_string(asset, "provenance"),
         optional_string(asset, "contextId")});
  }
  return result;
}

void validate_competence(double value, std::string_view field_id,
                         std::string_view label, std::string_view component) {
  if (value < 0 || value > 100 || !std::isfinite(value)) {
    std::string formatted;
    if (std::isnan(value)) {
      formatted = "NaN";
    } else if (value == std::numeric_limits<double>::infinity()) {
      formatted = "Infinity";
    } else if (value == -std::numeric_limits<double>::infinity()) {
      formatted = "-Infinity";
    } else {
      formatted = detail::legacy_general(value);
    }
    fail("Invalid " + std::string(label) + " " + std::string(component) +
         " competence " + formatted + " for field '" +
         std::string(field_id) + "'.");
  }
}

void validate_vector(const ResearchCompetenceVector &value,
                     std::string_view field_id, std::string_view label) {
  validate_competence(value.theoretical, field_id, label, "theoretical");
  validate_competence(value.experimental, field_id, label, "experimental");
  validate_competence(value.engineering, field_id, label, "engineering");
}

} // namespace

struct AdaptiveResearchSnapshotV2Codec::Storage {
  const AdaptiveResearchAuthority *authority;
  AdaptiveResearchSnapshotCodec v1;

  explicit Storage(const AdaptiveResearchAuthority &value)
      : authority(&value),
        v1(value.catalog(), value.applicability(), value.facilities()) {}

  AdaptiveResearchCivilizationState apply_expertise(
      AdaptiveResearchCivilizationState state,
      const AdaptiveResearchStateSnapshot &core,
      const AdaptiveResearchExpertiseSnapshot &expertise) const {
    auto &expertise_state = StateWriter::expertise(state);
    for (const auto &field : expertise.fields) {
      if (std::ranges::none_of(authority->expertise_catalog().fields(),
                               [&](const auto &definition) {
                                 return definition.id == field.field_id;
                               }))
        fail("Snapshot references unknown competence field '" + field.field_id +
             "'.");
      validate_vector(field.current, field.field_id, "current");
      validate_vector(field.historical_peak, field.field_id, "historicalPeak");
      if (field.current.theoretical >
              field.historical_peak.theoretical + 0.000001 ||
          field.current.experimental >
              field.historical_peak.experimental + 0.000001 ||
          field.current.engineering >
              field.historical_peak.engineering + 0.000001)
        fail("Current competence exceeds historical peak for field '" +
             field.field_id + "'.");
      ExpertiseWriter::set_field(
          expertise_state,
          {field.field_id, field.current, field.historical_peak,
           field.last_theoretical_activity_year,
           field.last_experimental_activity_year,
           field.last_engineering_activity_year, 0});
    }
    for (const auto &institution : expertise.institutions) {
      if (std::ranges::none_of(authority->expertise_catalog().institutions(),
                               [&](const auto &definition) {
                                 return definition.institution_archetype_id ==
                                        institution.institution_archetype_id;
                               }))
        fail("Snapshot references unknown research institution '" +
             institution.institution_archetype_id + "'.");
      authority->expertise_service().set_institution(
          state, institution.institution_instance_id,
          institution.institution_archetype_id, institution.total_count,
          institution.active_count,
          institution.context_id
              ? std::optional<std::string_view>(*institution.context_id)
              : std::nullopt);
    }
    for (const auto &asset : expertise.tacit_assets)
      authority->expertise_service().set_tacit_asset(
          state, asset.asset_id, asset.asset_type_id, asset.scope_kind,
          asset.scope_ref, asset.assimilation_stage, asset.depth,
          asset.availability, asset.translation_context_quality,
          asset.training_continuity, asset.provenance,
          asset.context_id ? std::optional<std::string_view>(*asset.context_id)
                           : std::nullopt);

    StateWriter::set_facility_capabilities(state, core.facility_capabilities);
    if (!expertise.institutions.empty() &&
        std::abs(state.total_effective_research_labs() -
                 core.total_effective_research_labs) > 0.000001)
      fail("Expertise institution capacity does not reproduce the saved core "
           "Effective Research Lab total.");

    const std::vector<ResearchProjectRuntimeState> projects(
        state.active_projects().begin(), state.active_projects().end());
    for (auto project : projects) {
      const auto breakdown = authority->get_project_readiness(
          state, project.node_id, project.stage,
          project.assigned_effective_labs,
          project.target_applicability_context_id
              ? std::optional<std::string_view>(
                    *project.target_applicability_context_id)
              : std::nullopt);
      project.readiness_efficiency = breakdown.rp_efficiency;
      StateWriter::set_project(state, std::move(project));
    }
    StateWriter::mark_view_dirty(state);
    return state;
  }
};

AdaptiveResearchSnapshotV2Codec::AdaptiveResearchSnapshotV2Codec(
    const AdaptiveResearchAuthority &authority)
    : storage_(std::make_unique<Storage>(authority)) {}
AdaptiveResearchSnapshotV2Codec::~AdaptiveResearchSnapshotV2Codec() = default;
AdaptiveResearchSnapshotV2Codec::AdaptiveResearchSnapshotV2Codec(
    AdaptiveResearchSnapshotV2Codec &&) noexcept = default;
AdaptiveResearchSnapshotV2Codec &AdaptiveResearchSnapshotV2Codec::operator=(
    AdaptiveResearchSnapshotV2Codec &&) noexcept = default;

AdaptiveResearchStateSnapshotV2 AdaptiveResearchSnapshotV2Codec::capture(
    const AdaptiveResearchCivilizationState &state) const {
  AdaptiveResearchStateSnapshotV2 result;
  result.schema_version = current_schema_version;
  result.catalog_id = storage_->authority->catalog().metadata().catalog_id;
  result.core = storage_->v1.capture(state);
  const auto &expertise = state.expertise();
  for (const auto &field : expertise.field_competence())
    result.expertise.fields.push_back(
        {field.field_id, field.current, field.historical_peak,
         field.last_theoretical_activity_year,
         field.last_experimental_activity_year,
         field.last_engineering_activity_year});
  std::ranges::sort(result.expertise.fields, [](const auto &left,
                                                const auto &right) {
    return ordinal_less(left.field_id, right.field_id);
  });
  for (const auto &institution : expertise.institutions())
    result.expertise.institutions.push_back(
        {institution.institution_instance_id,
         institution.institution_archetype_id, institution.context_id,
         institution.total_count, institution.active_count});
  std::ranges::sort(result.expertise.institutions,
                    [](const auto &left, const auto &right) {
    return ordinal_less(left.institution_instance_id,
                        right.institution_instance_id);
  });
  for (const auto &asset : expertise.tacit_assets())
    result.expertise.tacit_assets.push_back(
        {asset.asset_id, asset.asset_type_id, asset.scope_kind, asset.scope_ref,
         asset.assimilation_stage, asset.depth, asset.availability,
         asset.translation_context_quality, asset.training_continuity,
         asset.provenance, asset.context_id});
  std::ranges::sort(result.expertise.tacit_assets,
                    [](const auto &left, const auto &right) {
    return ordinal_less(left.asset_id, right.asset_id);
  });
  return result;
}

std::string AdaptiveResearchSnapshotV2Codec::serialize(
    const AdaptiveResearchCivilizationState &state) const {
  const auto snapshot = capture(state);
  Json result;
  result["schemaVersion"] = snapshot.schema_version;
  result["catalogId"] = snapshot.catalog_id;
  result["core"] = Json::parse(storage_->v1.serialize(state));
  result["expertise"] = expertise_json(snapshot.expertise);
  return result.dump();
}

AdaptiveResearchCivilizationState AdaptiveResearchSnapshotV2Codec::deserialize(
    std::string_view text) const {
  Json document;
  try {
    document = Json::parse(text);
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
  if (!document.is_object())
    json_fail("Adaptive Research v2 snapshot root must be an object.");
  const auto schema = document.find("schemaVersion");
  if (schema == document.end())
    fail("Adaptive Research snapshot is missing schemaVersion.");
  if (!schema->is_number_integer() && !schema->is_number_unsigned())
    json_fail("Adaptive Research snapshot schemaVersion must be an Int32 number.");
  int schema_version{};
  if (schema->is_number_unsigned()) {
    const auto schema_integer = schema->get<std::uint64_t>();
    if (schema_integer > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      json_fail("Adaptive Research snapshot schemaVersion is outside Int32 range.");
    schema_version = static_cast<int>(schema_integer);
  } else {
    const auto schema_integer = schema->get<std::int64_t>();
    if (schema_integer < std::numeric_limits<int>::min() ||
        schema_integer > std::numeric_limits<int>::max())
      json_fail("Adaptive Research snapshot schemaVersion is outside Int32 range.");
    schema_version = static_cast<int>(schema_integer);
  }
  if (schema_version == AdaptiveResearchSnapshotCodec::current_schema_version)
    return storage_->v1.deserialize(text);
  if (schema_version != current_schema_version)
    fail("Unsupported Adaptive Research snapshot schema " +
         std::to_string(schema_version) + ".");
  AdaptiveResearchStateSnapshotV2 snapshot;
  snapshot.schema_version = schema_version;
  snapshot.catalog_id = nullable_string_default(document, "catalogId");
  const auto core = document.find("core");
  const auto expertise = document.find("expertise");
  if (snapshot.catalog_id !=
      storage_->authority->catalog().metadata().catalog_id)
    fail("Adaptive Research snapshot catalog '" + snapshot.catalog_id +
         "' does not match runtime catalog '" +
         storage_->authority->catalog().metadata().catalog_id + "'.");
  if (core == document.end() || core->is_null())
    json_fail("Adaptive Research v2 snapshot core is null.");
  auto state = storage_->v1.deserialize(core->dump());
  if (expertise == document.end() || expertise->is_null())
    json_fail("Adaptive Research v2 snapshot expertise is null.");
  AdaptiveResearchExpertiseSnapshot expertise_snapshot;
  try {
    expertise_snapshot = parse_expertise(*expertise);
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
  auto core_snapshot = storage_->v1.capture(state);
  // V1 capture sorts this collection. V2 restoration must replay the original
  // saved sequence after institution synchronization, including duplicate
  // input entries, just as the source uses snapshot.Core directly.
  const auto saved_facilities = core->find("facilityCapabilities");
  if (saved_facilities != core->end() && saved_facilities->is_array()) {
    core_snapshot.facility_capabilities.clear();
    for (const auto &capability : *saved_facilities)
      core_snapshot.facility_capabilities.push_back(capability.get<std::string>());
  }
  return storage_->apply_expertise(std::move(state), core_snapshot,
                                   expertise_snapshot);
}

AdaptiveResearchCivilizationState AdaptiveResearchSnapshotV2Codec::restore(
    const AdaptiveResearchStateSnapshotV2 &snapshot) const {
  if (snapshot.schema_version != current_schema_version)
    fail("Unsupported Adaptive Research snapshot schema " +
         std::to_string(snapshot.schema_version) + ".");
  if (snapshot.catalog_id !=
      storage_->authority->catalog().metadata().catalog_id)
    fail("Adaptive Research snapshot catalog '" + snapshot.catalog_id +
         "' does not match runtime catalog '" +
         storage_->authority->catalog().metadata().catalog_id + "'.");
  return storage_->apply_expertise(storage_->v1.restore(snapshot.core),
                                   snapshot.core, snapshot.expertise);
}

} // namespace stellar::core
