#include <stellar/core/adaptive_research_snapshot.hpp>
#include <stellar/core/detail/adaptive_research_snapshot_json.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;
[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchSnapshotError(std::move(message));
}
std::string number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  char buffer[64];
  auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  std::string text(buffer, result.ptr);
  auto at = text.find('e');
  if (at != std::string::npos) {
    text[at] = 'E';
    auto digits = at + 1;
    if (digits < text.size() && (text[digits] == '+' || text[digits] == '-'))
      ++digits;
    if (text.size() - digits == 1)
      text.insert(digits, "0");
  }
  return text;
}
std::vector<std::uint16_t> utf16(std::string_view value) {
  std::vector<std::uint16_t> out;
  while (!value.empty()) {
    auto first = static_cast<unsigned char>(value.front());
    std::uint32_t cp = first;
    std::size_t n = 1;
    if ((first & 0xe0) == 0xc0) {
      cp = first & 0x1f;
      n = 2;
    } else if ((first & 0xf0) == 0xe0) {
      cp = first & 0x0f;
      n = 3;
    } else if ((first & 0xf8) == 0xf0) {
      cp = first & 7;
      n = 4;
    } else if (first >= 0x80)
      throw AdaptiveResearchSnapshotJsonError(
          "Invalid UTF-8 in snapshot string.");
    if (value.size() < n)
      throw AdaptiveResearchSnapshotJsonError(
          "Invalid UTF-8 in snapshot string.");
    for (std::size_t i = 1; i < n; ++i) {
      auto c = static_cast<unsigned char>(value[i]);
      if ((c & 0xc0) != 0x80)
        throw AdaptiveResearchSnapshotJsonError(
            "Invalid UTF-8 in snapshot string.");
      cp = (cp << 6) | (c & 0x3f);
    }
    const std::uint32_t minimum = n == 1   ? 0
                                  : n == 2 ? 0x80
                                  : n == 3 ? 0x800
                                           : 0x10000;
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw AdaptiveResearchSnapshotJsonError(
          "Invalid UTF-8 in snapshot string.");
    value.remove_prefix(n);
    if (cp <= 0xffff)
      out.push_back(static_cast<std::uint16_t>(cp));
    else {
      cp -= 0x10000;
      out.push_back(static_cast<std::uint16_t>(0xd800 + (cp >> 10)));
      out.push_back(static_cast<std::uint16_t>(0xdc00 + (cp & 0x3ff)));
    }
  }
  return out;
}
bool ordinal_less(std::string_view a, std::string_view b) {
  return utf16(a) < utf16(b);
}
Json maturity_json(ResearchMaturity value) {
  switch (value) {
  case ResearchMaturity::rumored:
    return "rumored";
  case ResearchMaturity::hypothesized:
    return "hypothesized";
  case ResearchMaturity::investigable:
    return "investigable";
  case ResearchMaturity::experimental:
    return "experimental";
  case ResearchMaturity::demonstrated:
    return "demonstrated";
  case ResearchMaturity::engineering:
    return "engineering";
  case ResearchMaturity::mature:
    return "mature";
  case ResearchMaturity::archived:
    return "archived";
  default:
    return static_cast<int>(value);
  }
}
std::string maturity_name(ResearchMaturity value) {
  switch (value) {
  case ResearchMaturity::rumored:
    return "Rumored";
  case ResearchMaturity::hypothesized:
    return "Hypothesized";
  case ResearchMaturity::investigable:
    return "Investigable";
  case ResearchMaturity::experimental:
    return "Experimental";
  case ResearchMaturity::demonstrated:
    return "Demonstrated";
  case ResearchMaturity::engineering:
    return "Engineering";
  case ResearchMaturity::mature:
    return "Mature";
  case ResearchMaturity::archived:
    return "Archived";
  default:
    return std::to_string(static_cast<int>(value));
  }
}
std::int32_t checked_int32(const Json &value) {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number <=
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
      return static_cast<std::int32_t>(number);
  } else if (value.is_number_integer()) {
    const auto number = value.get<std::int64_t>();
    if (number >= std::numeric_limits<std::int32_t>::min() &&
        number <= std::numeric_limits<std::int32_t>::max())
      return static_cast<std::int32_t>(number);
  }
  throw AdaptiveResearchSnapshotJsonError(
      "Adaptive Research snapshot integer is outside the Int32 range.");
}
ResearchMaturity maturity(const Json &value) {
  if (value.is_number_integer() || value.is_number_unsigned())
    return static_cast<ResearchMaturity>(checked_int32(value));
  if (!value.is_string())
    throw AdaptiveResearchSnapshotJsonError(
        "Adaptive Research snapshot maturity must be a string or integer.");
  const auto text = value.get<std::string>();
  const auto begin = text.find_first_not_of(" \t\r\n\f\v");
  const auto end = text.find_last_not_of(" \t\r\n\f\v");
  auto folded = begin == std::string::npos
                    ? std::string{}
                    : text.substr(begin, end - begin + 1);
  std::ranges::transform(folded, folded.begin(), [](unsigned char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A'))
                                : static_cast<char>(c);
  });
  if (folded == "rumored")
    return ResearchMaturity::rumored;
  if (folded == "hypothesized")
    return ResearchMaturity::hypothesized;
  if (folded == "investigable")
    return ResearchMaturity::investigable;
  if (folded == "experimental")
    return ResearchMaturity::experimental;
  if (folded == "demonstrated")
    return ResearchMaturity::demonstrated;
  if (folded == "engineering")
    return ResearchMaturity::engineering;
  if (folded == "mature")
    return ResearchMaturity::mature;
  if (folded == "archived")
    return ResearchMaturity::archived;
  std::int64_t numeric{};
  const auto parsed =
      std::from_chars(folded.data(), folded.data() + folded.size(), numeric);
  if (!folded.empty() && parsed.ec == std::errc{} &&
      parsed.ptr == folded.data() + folded.size() &&
      numeric >= std::numeric_limits<std::int32_t>::min() &&
      numeric <= std::numeric_limits<std::int32_t>::max())
    return static_cast<ResearchMaturity>(static_cast<std::int32_t>(numeric));
  throw AdaptiveResearchSnapshotJsonError(
      "Unknown Adaptive Research maturity JSON value '" + text + "'.");
}
template <class T>
std::optional<T> optional(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null())
    return std::nullopt;
  return found->get<T>();
}
template <class T>
T scalar_or_default(const Json &object, const char *name, T fallback = {}) {
  const auto found = object.find(name);
  return found == object.end() ? fallback : found->get<T>();
}
std::string string_or_empty(const Json &object, const char *name) {
  const auto found = object.find(name);
  return found == object.end() || found->is_null() ? std::string{}
                                                   : found->get<std::string>();
}
std::string required_record_string(const Json &object, const char *name) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null())
    throw AdaptiveResearchSnapshotJsonError(
        "Adaptive Research snapshot record string '" + std::string(name) +
        "' is null.");
  return found->get<std::string>();
}
ResearchMaturity maturity_or_default(const Json &object, const char *name) {
  const auto found = object.find(name);
  return found == object.end() ? ResearchMaturity{} : maturity(*found);
}
const Json &collection(const Json &object, const char *name,
                       bool expect_object) {
  const auto found = object.find(name);
  if (found == object.end() || found->is_null())
    throw AdaptiveResearchSnapshotJsonError(
        "Adaptive Research snapshot collection '" + std::string(name) +
        "' is null.");
  if ((expect_object && !found->is_object()) ||
      (!expect_object && !found->is_array()))
    throw AdaptiveResearchSnapshotJsonError(
        "Adaptive Research snapshot collection '" + std::string(name) +
        "' has the wrong JSON shape.");
  return *found;
}
bool has_deployment(const AdaptiveResearchCatalog &catalog,
                    std::string_view id) {
  return std::ranges::any_of(catalog.deployment_events(),
                             [&](const auto &value) { return value.id == id; });
}
void require_json_number(double value) {
  if (!std::isfinite(value))
    throw AdaptiveResearchSnapshotJsonError(
        "Non-finite values cannot be written as JSON numbers.");
}
Json json_number(double value) {
  require_json_number(value);
  constexpr double int64_upper_exclusive = 9223372036854775808.0;
  if ((value != 0.0 || !std::signbit(value)) &&
      value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
      value < int64_upper_exclusive && std::trunc(value) == value)
    return static_cast<std::int64_t>(value);
  return value;
}
Json to_json(const AdaptiveResearchStateSnapshot &s) {
  Json j;
  j["schemaVersion"] = s.schema_version;
  j["catalogId"] = s.catalog_id;
  j["civilizationId"] = s.civilization_id;
  j["directedProgramStageId"] = s.directed_program_stage_id;
  j["totalEffectiveResearchLabs"] =
      json_number(s.total_effective_research_labs);
  j["nodes"] = Json::array();
  for (const auto &v : s.nodes)
    j["nodes"].push_back(
        {{"nodeId", v.node_id},
         {"maturity", maturity_json(v.maturity)},
         {"resolution", v.resolution ? Json(*v.resolution) : Json(nullptr)},
         {"stageResearchPoints", json_number(v.stage_research_points)},
         {"totalResearchPoints", json_number(v.total_research_points)}});
  j["pressures"] = Json::object();
  for (const auto &v : s.pressures)
    j["pressures"][v.pressure_id] = json_number(v.value);
  j["evidence"] = Json::array();
  for (const auto &v : s.evidence)
    j["evidence"].push_back(
        {{"evidenceInstanceId", v.evidence_instance_id},
         {"evidenceTypeId", v.evidence_type_id},
         {"provenance", v.provenance},
         {"quality", json_number(v.quality)},
         {"confidence", json_number(v.confidence)},
         {"contextId", v.context_id ? Json(*v.context_id) : Json(nullptr)}});
  j["civilizationTraits"] = s.civilization_traits;
  j["applicabilityContexts"] = Json::object();
  for (const auto &v : s.applicability_contexts)
    j["applicabilityContexts"][v.context_id] = v.traits;
  j["capabilities"] = Json::array();
  for (const auto &v : s.capabilities)
    j["capabilities"].push_back(
        {{"capabilityId", v.capability_id},
         {"contextId", v.context_id ? Json(*v.context_id) : Json(nullptr)}});
  j["facilityCapabilities"] = s.facility_capabilities;
  j["enabledDeploymentEventIds"] = s.enabled_deployment_event_ids;
  j["activeProjects"] = Json::array();
  const auto project_json = [](const AdaptiveResearchProjectSnapshot &v) {
    return Json({{"nodeId", v.node_id},
         {"stage", maturity_json(v.stage)},
         {"targetApplicabilityContextId",
          v.target_applicability_context_id
              ? Json(*v.target_applicability_context_id)
              : Json(nullptr)},
         {"assignedEffectiveLabs", json_number(v.assigned_effective_labs)},
         {"readinessEfficiency", json_number(v.readiness_efficiency)},
         {"paused", v.paused},
         {"pauseReason",
          v.pause_reason ? Json(*v.pause_reason) : Json(nullptr)},
         {"stageResearchPoints", json_number(v.stage_research_points)},
         {"totalResearchPoints", json_number(v.total_research_points)}});
  };
  for (const auto &v : s.active_projects) j["activeProjects"].push_back(project_json(v));
  if (s.schema_version == AdaptiveResearchSnapshotCodec::current_schema_version) {
    j["cancelledProjects"] = Json::array();
    for (const auto &v : s.cancelled_projects) j["cancelledProjects"].push_back(project_json(v));
  }
  return j;
}
AdaptiveResearchStateSnapshot from_json(const Json &j) {
  if (!j.is_object())
    throw AdaptiveResearchSnapshotJsonError(
        "Adaptive Research snapshot root must be an object.");
  AdaptiveResearchStateSnapshot s;
  const auto schema = j.find("schemaVersion");
  s.schema_version = schema == j.end() ? 0 : checked_int32(*schema);
  s.catalog_id = string_or_empty(j, "catalogId");
  s.civilization_id = required_record_string(j, "civilizationId");
  s.directed_program_stage_id =
      required_record_string(j, "directedProgramStageId");
  s.total_effective_research_labs =
      scalar_or_default<double>(j, "totalEffectiveResearchLabs");
  for (const auto &v : collection(j, "nodes", false))
    s.nodes.push_back({required_record_string(v, "nodeId"),
                       maturity_or_default(v, "maturity"),
                       optional<std::string>(v, "resolution"),
                       scalar_or_default<double>(v, "stageResearchPoints"),
                       scalar_or_default<double>(v, "totalResearchPoints")});
  const auto &pressures = collection(j, "pressures", true);
  for (auto it = pressures.begin(); it != pressures.end(); ++it)
    s.pressures.push_back({it.key(), it.value().get<double>()});
  for (const auto &v : collection(j, "evidence", false))
    s.evidence.push_back({required_record_string(v, "evidenceInstanceId"),
                          required_record_string(v, "evidenceTypeId"),
                          required_record_string(v, "provenance"),
                          scalar_or_default<double>(v, "quality"),
                          scalar_or_default<double>(v, "confidence"),
                          optional<std::string>(v, "contextId")});
  s.civilization_traits = collection(j, "civilizationTraits", false)
                              .get<std::vector<std::string>>();
  const auto &contexts = collection(j, "applicabilityContexts", true);
  for (auto it = contexts.begin(); it != contexts.end(); ++it) {
    if (it.value().is_null())
      throw AdaptiveResearchSnapshotJsonError(
          "Adaptive Research snapshot applicability context trait collection "
          "is null.");
    s.applicability_contexts.push_back(
        {it.key(), it.value().get<std::vector<std::string>>()});
  }
  for (const auto &v : collection(j, "capabilities", false))
    s.capabilities.push_back({required_record_string(v, "capabilityId"),
                              optional<std::string>(v, "contextId")});
  s.facility_capabilities = collection(j, "facilityCapabilities", false)
                                .get<std::vector<std::string>>();
  s.enabled_deployment_event_ids =
      collection(j, "enabledDeploymentEventIds", false)
          .get<std::vector<std::string>>();
  for (const auto &v : collection(j, "activeProjects", false))
    s.active_projects.push_back(
        {required_record_string(v, "nodeId"), maturity_or_default(v, "stage"),
         optional<std::string>(v, "targetApplicabilityContextId"),
         scalar_or_default<double>(v, "assignedEffectiveLabs"),
         scalar_or_default<double>(v, "readinessEfficiency"),
         scalar_or_default<bool>(v, "paused"),
         optional<std::string>(v, "pauseReason"),
         scalar_or_default<double>(v, "stageResearchPoints"),
         scalar_or_default<double>(v, "totalResearchPoints")});
  if (s.schema_version == AdaptiveResearchSnapshotCodec::current_schema_version) {
  for (const auto &v : collection(j, "cancelledProjects", false))
    s.cancelled_projects.push_back(
        {required_record_string(v, "nodeId"), maturity_or_default(v, "stage"),
         optional<std::string>(v, "targetApplicabilityContextId"),
         scalar_or_default<double>(v, "assignedEffectiveLabs"),
         scalar_or_default<double>(v, "readinessEfficiency"),
         scalar_or_default<bool>(v, "paused"),
         optional<std::string>(v, "pauseReason"),
         scalar_or_default<double>(v, "stageResearchPoints"),
         scalar_or_default<double>(v, "totalResearchPoints")});
  }
  return s;
}
} // namespace

std::string detail::encode_adaptive_research_snapshot_dto(
    const AdaptiveResearchStateSnapshot &snapshot) {
  try {
    return to_json(snapshot).dump();
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

AdaptiveResearchStateSnapshot detail::decode_adaptive_research_snapshot_dto(
    std::string_view text) {
  try {
    return from_json(Json::parse(text));
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}

AdaptiveResearchSnapshotError::AdaptiveResearchSnapshotError(std::string value)
    : std::runtime_error(std::move(value)) {}
AdaptiveResearchSnapshotJsonError::AdaptiveResearchSnapshotJsonError(
    std::string value)
    : std::runtime_error(std::move(value)) {}
AdaptiveResearchSnapshotCodec::AdaptiveResearchSnapshotCodec(
    const AdaptiveResearchCatalog &c,
    const AdaptiveResearchApplicabilityCatalog &a,
    const AdaptiveResearchFacilityCatalog &f) noexcept
    : catalog_(&c), applicability_(&a), facilities_(&f) {}
AdaptiveResearchStateSnapshot AdaptiveResearchSnapshotCodec::capture(
    const AdaptiveResearchCivilizationState &state) const {
  AdaptiveResearchStateSnapshot s;
  s.schema_version = state.cancelled_projects().empty() ? 1 : current_schema_version;
  s.catalog_id = catalog_->metadata().catalog_id;
  s.civilization_id = state.civilization_id();
  s.directed_program_stage_id = state.directed_program_stage_id();
  s.total_effective_research_labs = state.total_effective_research_labs();
  for (const auto &v : state.node_states())
    s.nodes.push_back({v.node_id, v.maturity, v.resolution,
                       v.stage_research_points, v.total_research_points});
  std::ranges::sort(s.nodes, [&](const auto &a, const auto &b) {
    return ordinal_less(a.node_id, b.node_id);
  });
  for (const auto &v : state.pressures())
    s.pressures.push_back({v.pressure_id, v.value});
  std::ranges::sort(s.pressures, [&](const auto &a, const auto &b) {
    return ordinal_less(a.pressure_id, b.pressure_id);
  });
  for (const auto &v : state.evidence_instances())
    s.evidence.push_back({v.evidence_instance_id, v.evidence_type_id,
                          v.provenance, v.quality, v.confidence, v.context_id});
  std::ranges::sort(s.evidence, [&](const auto &a, const auto &b) {
    return ordinal_less(a.evidence_instance_id, b.evidence_instance_id);
  });
  s.civilization_traits.assign(state.civilization_traits().begin(),
                               state.civilization_traits().end());
  std::ranges::sort(s.civilization_traits, ordinal_less);
  for (auto v : state.applicability_contexts()) {
    std::ranges::sort(v.sorted_traits, ordinal_less);
    s.applicability_contexts.push_back(
        {std::move(v.context_id), std::move(v.sorted_traits)});
  }
  std::ranges::sort(s.applicability_contexts,
                    [&](const auto &a, const auto &b) {
                      return ordinal_less(a.context_id, b.context_id);
                    });
  for (const auto &v : state.capabilities())
    s.capabilities.push_back({v.capability_id, v.context_id});
  std::ranges::sort(s.capabilities, [&](const auto &a, const auto &b) {
    if (a.capability_id != b.capability_id)
      return ordinal_less(a.capability_id, b.capability_id);
    if (!a.context_id)
      return bool(b.context_id);
    if (!b.context_id)
      return false;
    return ordinal_less(*a.context_id, *b.context_id);
  });
  s.facility_capabilities.assign(state.facility_capabilities().begin(),
                                 state.facility_capabilities().end());
  std::ranges::sort(s.facility_capabilities, ordinal_less);
  s.enabled_deployment_event_ids.assign(
      state.enabled_deployment_event_ids().begin(),
      state.enabled_deployment_event_ids().end());
  std::ranges::sort(s.enabled_deployment_event_ids, ordinal_less);
  for (const auto &v : state.active_projects())
    s.active_projects.push_back(
        {v.node_id, v.stage, v.target_applicability_context_id,
         v.assigned_effective_labs, v.readiness_efficiency, v.paused,
         v.pause_reason, v.stage_research_points, v.total_research_points});
  std::ranges::sort(s.active_projects, [&](const auto &a, const auto &b) {
    return ordinal_less(a.node_id, b.node_id);
  });
  for (const auto &v : state.cancelled_projects())
    s.cancelled_projects.push_back(
        {v.node_id, v.stage, v.target_applicability_context_id,
         v.assigned_effective_labs, v.readiness_efficiency, v.paused,
         v.pause_reason, v.stage_research_points, v.total_research_points});
  std::ranges::sort(s.cancelled_projects, [&](const auto &a, const auto &b) {
    return ordinal_less(a.node_id, b.node_id);
  });
  return s;
}
std::string AdaptiveResearchSnapshotCodec::serialize(
    const AdaptiveResearchCivilizationState &state) const {
  try {
    return to_json(capture(state)).dump();
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}
AdaptiveResearchCivilizationState
AdaptiveResearchSnapshotCodec::deserialize(std::string_view value) const {
  Json root;
  try {
    root = Json::parse(value);
    if (root.is_null())
      fail("Adaptive Research snapshot deserialized to null.");
    // System.Text.Json materializes the whole record before Restore. Preserve
    // Restore's schema/catalog precedence over later null collection access.
    if (root.is_object()) {
      const auto schema = root.find("schemaVersion");
      const auto schema_version =
          schema == root.end() ? 0 : checked_int32(*schema);
      if (schema_version != 1 && schema_version != current_schema_version)
        fail("Unsupported Adaptive Research snapshot schema " +
             std::to_string(schema_version) + "; expected 1.");
      const auto catalog_id = string_or_empty(root, "catalogId");
      if (catalog_id != catalog_->metadata().catalog_id)
        fail("Adaptive Research snapshot catalog '" + catalog_id +
             "' does not match runtime catalog '" +
             catalog_->metadata().catalog_id + "'.");
    }
    return restore(from_json(root));
  } catch (const AdaptiveResearchSnapshotError &) {
    throw;
  } catch (const AdaptiveResearchSnapshotJsonError &) {
    throw;
  } catch (const Json::exception &error) {
    throw AdaptiveResearchSnapshotJsonError(error.what());
  }
}
AdaptiveResearchCivilizationState AdaptiveResearchSnapshotCodec::restore(
    const AdaptiveResearchStateSnapshot &s) const {
  using W = detail::AdaptiveResearchStateWriter;
  if (s.schema_version != 1 && s.schema_version != current_schema_version)
    fail("Unsupported Adaptive Research snapshot schema " +
         std::to_string(s.schema_version) + "; expected 1.");
  if (s.catalog_id != catalog_->metadata().catalog_id)
    fail("Adaptive Research snapshot catalog '" + s.catalog_id +
         "' does not match runtime catalog '" +
         catalog_->metadata().catalog_id + "'.");
  (void)catalog_->get_directed_program_stage(s.directed_program_stage_id);
  AdaptiveResearchCivilizationState state(s.civilization_id,
                                          s.directed_program_stage_id);
  W::set_total_effective_research_labs(state, s.total_effective_research_labs);
  for (const auto &v : s.pressures) {
    if (!catalog_->has_pressure(v.pressure_id))
      fail("Snapshot references unknown Research Pressure '" + v.pressure_id +
           "'.");
    W::set_pressure(state, v.pressure_id, v.value);
  }
  for (const auto &v : s.evidence) {
    if (!catalog_->has_evidence_type(v.evidence_type_id))
      fail("Snapshot evidence '" + v.evidence_instance_id +
           "' references unknown evidence type '" + v.evidence_type_id + "'.");
    if (v.quality < 0 || v.quality > 1 || !std::isfinite(v.quality))
      fail("Invalid evidence " + v.evidence_instance_id +
           " quality: " + number(v.quality) + ".");
    if (v.confidence < 0 || v.confidence > 1 || !std::isfinite(v.confidence))
      fail("Invalid evidence " + v.evidence_instance_id +
           " confidence: " + number(v.confidence) + ".");
    if (!W::add_evidence(state, {v.evidence_instance_id, v.evidence_type_id,
                                 v.provenance, v.quality, v.confidence,
                                 v.context_id, state.revision() + 1}))
      fail("Duplicate evidence instance '" + v.evidence_instance_id +
           "' in snapshot.");
  }
  for (const auto &id : s.civilization_traits) {
    const auto &trait = applicability_->get_trait(id);
    if (trait.scope != ResearchApplicabilityTraitScope::civilization)
      fail("Population-scoped trait '" + id +
           "' was stored as a civilization trait.");
    (void)W::add_civilization_trait(state, id);
  }
  for (const auto &context : s.applicability_contexts) {
    for (const auto &id : context.traits) {
      const auto &trait = applicability_->get_trait(id);
      if (trait.scope != ResearchApplicabilityTraitScope::population_or_species)
        fail("Civilization-scoped trait '" + id +
             "' was stored in applicability context '" + context.context_id +
             "'.");
    }
    (void)W::set_applicability_context_traits(state, context.context_id,
                                              context.traits);
  }
  for (const auto &v : s.capabilities) {
    const auto *definition = catalog_->find_capability(v.capability_id);
    if (!definition)
      fail("Snapshot references unknown capability '" + v.capability_id + "'.");
    if (definition->scope == ResearchCapabilityScope::civilization &&
        v.context_id)
      fail("Civilization capability '" + v.capability_id +
           "' has an invalid target context.");
    if (definition->scope != ResearchCapabilityScope::civilization &&
        !v.context_id)
      fail("Scoped capability '" + v.capability_id +
           "' is missing its target context.");
    (void)W::add_capability(state, {v.capability_id, v.context_id});
  }
  for (const auto &id : s.facility_capabilities) {
    if (std::ranges::find(facilities_->facility_capability_ids(), id) ==
        facilities_->facility_capability_ids().end())
      fail("Snapshot references unknown facility capability '" + id + "'.");
    (void)W::add_facility_capability(state, id);
  }
  for (const auto &id : s.enabled_deployment_event_ids) {
    if (!has_deployment(*catalog_, id))
      fail("Snapshot references unknown research-enabled deployment event '" +
           id + "'.");
    (void)W::add_enabled_deployment_event(state, id);
  }
  for (const auto &v : s.nodes) {
    (void)catalog_->get_node(v.node_id);
    if (v.maturity < ResearchMaturity::rumored ||
        v.maturity > ResearchMaturity::archived)
      fail("Node '" + v.node_id + "' has invalid maturity '" +
           maturity_name(v.maturity) + "'.");
    if (v.stage_research_points < 0 || v.total_research_points < 0)
      fail("Node '" + v.node_id + "' has negative research progress.");
    W::set_node_state(state, {v.node_id, v.maturity, v.resolution,
                              v.stage_research_points, v.total_research_points,
                              state.revision() + 1});
  }
  for (const auto &v : s.active_projects) {
    const auto &definition = catalog_->get_node(v.node_id);
    if (v.stage != ResearchMaturity::experimental &&
        v.stage != ResearchMaturity::demonstrated &&
        v.stage != ResearchMaturity::engineering)
      fail("Project '" + v.node_id + "' has invalid active stage '" +
           maturity_name(v.stage) + "'.");
    const auto *node = state.try_get_node_state(v.node_id);
    if (!node)
      fail("Project '" + v.node_id + "' has no visible node state.");
    if (node->maturity != v.stage)
      fail("Project '" + v.node_id + "' stage '" + maturity_name(v.stage) +
           "' does not match node state '" + maturity_name(node->maturity) +
           "'.");
    if (v.assigned_effective_labs + .000001 <
        definition.project_requirements.minimum_labs)
      fail("Project '" + v.node_id + "' is below its minimum lab requirement.");
    if (v.readiness_efficiency <= 0 || !std::isfinite(v.readiness_efficiency))
      fail("Project '" + v.node_id + "' has invalid readiness efficiency.");
    if (v.stage_research_points < 0 || v.total_research_points < 0)
      fail("Project '" + v.node_id + "' has negative research progress.");
    W::set_project(state,
                   {v.node_id, v.stage, v.target_applicability_context_id,
                    v.assigned_effective_labs, v.readiness_efficiency, v.paused,
                    v.pause_reason, v.stage_research_points,
                    v.total_research_points, state.revision() + 1});
  }
  if (s.schema_version == 1 && !s.cancelled_projects.empty())
    fail("Legacy research snapshot cannot contain cancelled projects.");
  if (s.cancelled_projects.size() > s.nodes.size())
    fail("Cancelled research exceeds known node count.");
  for (const auto &v : s.cancelled_projects) {
    const auto &definition = catalog_->get_node(v.node_id);
    const auto *node = state.try_get_node_state(v.node_id);
    if (!node || node->maturity != v.stage ||
        (v.stage != ResearchMaturity::experimental && v.stage != ResearchMaturity::demonstrated &&
         v.stage != ResearchMaturity::engineering) || !v.paused ||
        (v.pause_reason != "cancelled_by_order" && v.pause_reason != "hypothesis_resolution_required") ||
        (v.pause_reason == "hypothesis_resolution_required" &&
         (!definition.is_hypothesis || v.stage != ResearchMaturity::experimental)) ||
        !std::isfinite(v.assigned_effective_labs) ||
        v.assigned_effective_labs < definition.project_requirements.minimum_labs ||
        !std::isfinite(v.readiness_efficiency) || v.readiness_efficiency <= 0 ||
        !std::isfinite(v.stage_research_points) || v.stage_research_points < 0 ||
        !std::isfinite(v.total_research_points) || v.total_research_points < v.stage_research_points ||
        v.total_research_points > definition.project_requirements.base_research_points + .000001 ||
        std::abs(node->stage_research_points-v.stage_research_points) > .000001 ||
        std::abs(node->total_research_points-v.total_research_points) > .000001 ||
        state.cancelled_project(v.node_id) ||
        std::ranges::any_of(state.active_projects(), [&](const auto &p){return p.node_id==v.node_id;}))
      fail("Invalid or duplicate cancelled research project '" + v.node_id + "'.");
    if (v.target_applicability_context_id &&
        std::ranges::none_of(state.applicability_contexts(), [&](const auto &c){
          return c.context_id == *v.target_applicability_context_id;}))
      fail("Cancelled research references an unknown applicability context.");
    W::set_cancelled_project(state, {v.node_id, v.stage, v.target_applicability_context_id,
        v.assigned_effective_labs, v.readiness_efficiency, true, v.pause_reason,
        v.stage_research_points, v.total_research_points, 0});
  }
  if (state.assigned_effective_labs() >
      state.total_effective_research_labs() + .000001)
    fail("Active Adaptive Research projects assign more labs than the restored "
         "total Effective Research Lab capacity.");
  return state;
}
} // namespace stellar::core
