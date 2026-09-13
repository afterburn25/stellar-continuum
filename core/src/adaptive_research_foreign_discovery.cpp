#include <stellar/core/adaptive_research_foreign_discovery.hpp>

#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open " + std::filesystem::absolute(path).string());
  Json value;
  input >> value;
  return value;
}

bool consume_dotnet_whitespace(std::string_view &value) noexcept {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

bool blank(std::string_view value) noexcept {
  if (value.empty())
    return true;
  while (!value.empty())
    if (!consume_dotnet_whitespace(value))
      return false;
  return true;
}

std::string required_string(const Json &value, std::string_view name,
                            std::string_view source) {
  const auto found = value.find(std::string(name));
  if (found == value.end() || !found->is_string() ||
      blank(found->get_ref<const std::string &>()))
    throw AdaptiveResearchForeignCatalogDataError(
        std::string(source) + " is missing non-empty string '" +
        std::string(name) + "'.");
  return found->get<std::string>();
}

std::string_view json_kind(const Json &value) noexcept {
  if (value.is_null())
    return "Null";
  if (value.is_object())
    return "Object";
  if (value.is_array())
    return "Array";
  if (value.is_string())
    return "String";
  if (value.is_boolean())
    return value.get<bool>() ? "True" : "False";
  if (value.is_number())
    return "Number";
  return "Undefined";
}

const Json &require_array(const Json &value) {
  if (!value.is_array())
    throw AdaptiveResearchForeignJsonOperationError(
        "The requested operation requires an element of type 'Array', but "
        "the target element has type '" +
        std::string(json_kind(value)) + "'.");
  return value;
}

ResearchMaturity parse_awareness(std::string_view id) {
  if (id == "rumored")
    return ResearchMaturity::rumored;
  if (id == "hypothesized")
    return ResearchMaturity::hypothesized;
  if (id == "investigable")
    return ResearchMaturity::investigable;
  throw AdaptiveResearchForeignCatalogDataError(
      "Unsupported foreign awareness state '" + std::string(id) + "'.");
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
      point = first & 0x07;
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
      result.push_back(
          static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

bool ascii_iequal(std::string_view left, std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  const auto lower = [](char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32)
                                        : value;
  };
  for (std::size_t index = 0; index < left.size(); ++index)
    if (lower(left[index]) != lower(right[index]))
      return false;
  return true;
}

std::optional<std::string_view>
view(const std::optional<std::string> &value) noexcept {
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}

bool allows_direct_awareness(const AdaptiveResearchNodeDefinition &node) {
  return std::ranges::find(node.awareness_sources, "foreign_contact") !=
             node.awareness_sources.end() ||
         std::ranges::find(node.awareness_sources, "observation") !=
             node.awareness_sources.end();
}
} // namespace

struct AdaptiveResearchForeignDiscoveryCatalog::Storage {
  ResearchMaturity observed{};
  ResearchMaturity characterized{};
  std::vector<ForeignResearchMethodCandidateRule> rules;
};

AdaptiveResearchForeignDiscoveryCatalog::AdaptiveResearchForeignDiscoveryCatalog(
    std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchForeignDiscoveryCatalog::~AdaptiveResearchForeignDiscoveryCatalog() =
    default;
AdaptiveResearchForeignDiscoveryCatalog::AdaptiveResearchForeignDiscoveryCatalog(
    AdaptiveResearchForeignDiscoveryCatalog &&) noexcept = default;
AdaptiveResearchForeignDiscoveryCatalog &
AdaptiveResearchForeignDiscoveryCatalog::operator=(
    AdaptiveResearchForeignDiscoveryCatalog &&) noexcept = default;

ResearchMaturity AdaptiveResearchForeignDiscoveryCatalog::
    observed_evidence_state() const noexcept {
  return storage_->observed;
}
ResearchMaturity AdaptiveResearchForeignDiscoveryCatalog::
    characterized_evidence_state() const noexcept {
  return storage_->characterized;
}
std::span<const ForeignResearchMethodCandidateRule>
AdaptiveResearchForeignDiscoveryCatalog::method_rules() const noexcept {
  return storage_->rules;
}

ForeignAdaptationState
AdaptiveResearchForeignDiscoveryCatalog::parse_adaptation(
    std::string_view id) {
  if (id == "none")
    return ForeignAdaptationState::none;
  if (id == "conceptual_inspiration")
    return ForeignAdaptationState::conceptual_inspiration;
  if (id == "interface_adaptation")
    return ForeignAdaptationState::interface_adaptation;
  if (id == "native_derivative")
    return ForeignAdaptationState::native_derivative;
  if (id == "hybrid_lineage")
    return ForeignAdaptationState::hybrid_lineage;
  throw AdaptiveResearchForeignCatalogDataError(
      "Unknown foreign adaptation state '" + std::string(id) + "'.");
}

AdaptiveResearchForeignDiscoveryCatalog
load_adaptive_research_foreign_discovery_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog) {
  const auto absolute_root = std::filesystem::absolute(root_path);
  const auto root = read_json(
      absolute_root / "foreign_research_materialization_policy.json");
  const auto catalog_id = required_string(
      root, "catalog_id", "foreign_research_materialization_policy.json");
  if (catalog_id != catalog.metadata().catalog_id)
    throw AdaptiveResearchForeignCatalogDataError(
        "foreign_research_materialization_policy.json catalog_id '" +
        catalog_id + "' does not match '" + catalog.metadata().catalog_id +
        "'.");

  auto storage =
      std::make_unique<AdaptiveResearchForeignDiscoveryCatalog::Storage>();
  const auto &evidence = root.at("evidence_index_rules");
  storage->observed = parse_awareness(required_string(
      evidence, "observed_minimum_state", "evidence_index_rules"));
  storage->characterized = parse_awareness(required_string(
      evidence, "characterized_or_better_minimum_state",
      "evidence_index_rules"));
  if (storage->observed != ResearchMaturity::rumored ||
      storage->characterized != ResearchMaturity::hypothesized)
    throw AdaptiveResearchForeignCatalogDataError(
        "Foreign evidence seed states must remain Rumored / Hypothesized for "
        "the current runtime contract.");

  const auto &rules = require_array(root.at("cross_lineage_method_candidates"));
  for (const auto &element : rules) {
    ForeignResearchMethodCandidateRule rule;
    const auto axis = required_string(
        element, "trigger_axis", "cross_lineage_method_candidates");
    if (axis == "understanding") {
      rule.trigger_axis = ForeignDiscoveryTriggerAxis::understanding;
      const auto minimum = required_string(
          element, "minimum_state", "cross_lineage_method_candidates");
      rule.minimum_axis_rank = static_cast<int>(
          AdaptiveResearchForeignTechnologyCatalog::parse_understanding(
              minimum));
    } else if (axis == "adaptation") {
      rule.trigger_axis = ForeignDiscoveryTriggerAxis::adaptation;
      const auto minimum = required_string(
          element, "minimum_state", "cross_lineage_method_candidates");
      rule.minimum_axis_rank = static_cast<int>(
          AdaptiveResearchForeignDiscoveryCatalog::parse_adaptation(minimum));
    } else {
      throw AdaptiveResearchForeignCatalogDataError(
          "Unknown foreign discovery trigger axis '" + axis + "'.");
    }
    rule.awareness_state = parse_awareness(required_string(
        element, "awareness_state", "cross_lineage_method_candidates"));
    if (rule.awareness_state != ResearchMaturity::rumored &&
        rule.awareness_state != ResearchMaturity::hypothesized)
      throw AdaptiveResearchForeignCatalogDataError(
          "Foreign method candidate awareness may only be Rumored or "
          "Hypothesized.");
    const auto &node_ids = require_array(element.at("node_ids"));
    for (const auto &value : node_ids) {
      if (value.is_null())
        throw AdaptiveResearchForeignCatalogDataError(
            "Foreign materialization node id cannot be null.");
      auto id = value.get<std::string>();
      if (std::ranges::find(rule.node_ids, id) != rule.node_ids.end())
        continue;
      rule.node_ids.push_back(std::move(id));
    }
    for (const auto &id : rule.node_ids) {
      const auto *node = catalog.find_node(id);
      if (!node)
        throw AdaptiveResearchForeignCatalogDataError(
            "Foreign materialization policy references unknown node '" + id +
            "'.");
      if (!node->public_normal_research)
        throw AdaptiveResearchForeignCatalogDataError(
            "Foreign materialization policy cannot expose non-public node '" +
            id + "'.");
    }
    storage->rules.push_back(std::move(rule));
  }
  return AdaptiveResearchForeignDiscoveryCatalog(std::move(storage));
}

AdaptiveResearchForeignDiscoveryRuntime::
    AdaptiveResearchForeignDiscoveryRuntime(
        const AdaptiveResearchAuthority &authority,
        const AdaptiveResearchForeignTechnologyRuntime &foreign_technology,
        const AdaptiveResearchForeignDiscoveryCatalog &catalog) noexcept
    : authority_(&authority), foreign_technology_(&foreign_technology),
      catalog_(&catalog) {}

ForeignResearchDiscoveryResult AdaptiveResearchForeignDiscoveryRuntime::observe(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference,
    std::string_view source_lineage_reference, double confidence, double year,
    std::span<const std::string> known_constraint_ids,
    std::optional<std::string_view> target_applicability_context_id) const {
  const std::string reference(foreign_technology_reference);
  const std::string lineage(source_lineage_reference);
  const std::vector<std::string> constraints(known_constraint_ids.begin(),
                                             known_constraint_ids.end());
  const std::optional<std::string> context = target_applicability_context_id
                                                 ? std::optional<std::string>(
                                                       *target_applicability_context_id)
                                                 : std::nullopt;
  auto assessment = foreign_technology_->observe(
      state, reference, lineage, confidence, year, constraints);
  return {std::move(assessment), reevaluate(state, reference, view(context))};
}

ForeignResearchDiscoveryResult
AdaptiveResearchForeignDiscoveryRuntime::acquire_package(
    AdaptiveResearchCivilizationState &state,
    const ForeignTechnologyPackageInput &input_view) const {
  const ForeignTechnologyPackageInput input = input_view;
  auto assessment = foreign_technology_->acquire_package(state, input);
  return {std::move(assessment),
          reevaluate(state, input.foreign_technology_reference,
                     view(input.target_applicability_context_id))};
}

ForeignResearchDiscoveryResult
AdaptiveResearchForeignDiscoveryRuntime::record_analysis_result(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference,
    ForeignUnderstandingState target_understanding, double confidence,
    double year, std::span<const std::string> newly_known_constraint_ids,
    std::optional<std::string_view> target_applicability_context_id) const {
  const std::string reference(foreign_technology_reference);
  const std::vector<std::string> constraints(
      newly_known_constraint_ids.begin(), newly_known_constraint_ids.end());
  const std::optional<std::string> context = target_applicability_context_id
                                                 ? std::optional<std::string>(
                                                       *target_applicability_context_id)
                                                 : std::nullopt;
  auto assessment = foreign_technology_->record_analysis_result(
      state, reference, target_understanding, confidence, year, constraints);
  return {std::move(assessment), reevaluate(state, reference, view(context))};
}

ForeignResearchDiscoveryResult
AdaptiveResearchForeignDiscoveryRuntime::record_adaptation_result(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference,
    ForeignAdaptationState adaptation, double confidence, double year,
    std::optional<std::string_view> target_applicability_context_id) const {
  const std::string reference(foreign_technology_reference);
  const std::optional<std::string> context = target_applicability_context_id
                                                 ? std::optional<std::string>(
                                                       *target_applicability_context_id)
                                                 : std::nullopt;
  auto assessment = foreign_technology_->record_adaptation_result(
      state, reference, adaptation, confidence, year);
  return {std::move(assessment), reevaluate(state, reference, view(context))};
}

std::vector<ForeignResearchAwarenessEvent>
AdaptiveResearchForeignDiscoveryRuntime::reevaluate(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference,
    std::optional<std::string_view> target_applicability_context_id) const {
  const std::string reference(foreign_technology_reference);
  const std::optional<std::string> context = target_applicability_context_id
                                                 ? std::optional<std::string>(
                                                       *target_applicability_context_id)
                                                 : std::nullopt;
  const auto &foreign = foreign_technology_->state(state);
  const auto *assessment = foreign.try_get_assessment(reference);
  if (!assessment)
    throw AdaptiveResearchForeignMissingRecord(
        "Foreign technology '" + reference +
        "' has not been legitimately observed/acquired.");

  struct Desired {
    std::string node_id;
    ResearchMaturity state{};
    std::string reason;
  };
  std::vector<Desired> desired;
  std::unordered_map<std::string, std::size_t> desired_index;
  const auto merge = [&](std::string_view node_id, ResearchMaturity maturity,
                         std::string reason) {
    const auto found = desired_index.find(std::string(node_id));
    if (found != desired_index.end()) {
      auto &existing = desired[found->second];
      if (static_cast<int>(existing.state) >= static_cast<int>(maturity))
        return;
      existing.state = maturity;
      existing.reason = std::move(reason);
      return;
    }
    desired_index.emplace(node_id, desired.size());
    desired.push_back({std::string(node_id), maturity, std::move(reason)});
  };

  const auto evidence_awareness =
      static_cast<int>(assessment->understanding) >=
              static_cast<int>(ForeignUnderstandingState::characterized)
          ? catalog_->characterized_evidence_state()
          : catalog_->observed_evidence_state();
  for (const auto &evidence_ref : assessment->evidence_refs) {
    const auto evidence = std::ranges::find(
        state.evidence_instances(), evidence_ref,
        &ResearchEvidenceInstance::evidence_instance_id);
    if (evidence == state.evidence_instances().end())
      continue;
    for (const auto &node_id :
         authority_->catalog().nodes_for_evidence(evidence->evidence_type_id)) {
      const auto &node = authority_->catalog().get_node(node_id);
      if (allows_direct_awareness(node))
        merge(node_id, evidence_awareness,
              "Legitimate foreign evidence '" + evidence->evidence_type_id +
                  "' supports scientific awareness.");
    }
  }
  for (const auto &rule : catalog_->method_rules()) {
    const int current_rank =
        rule.trigger_axis == ForeignDiscoveryTriggerAxis::understanding
            ? static_cast<int>(assessment->understanding)
            : static_cast<int>(assessment->adaptation);
    if (current_rank < rule.minimum_axis_rank)
      continue;
    const auto axis_name =
        rule.trigger_axis == ForeignDiscoveryTriggerAxis::understanding
            ? "understanding"
            : "adaptation";
    for (const auto &node_id : rule.node_ids)
      merge(node_id, rule.awareness_state,
            "Foreign " + std::string(axis_name) +
                " now supports a cross-lineage research method hypothesis.");
  }

  std::ranges::sort(desired, [&](const Desired &left, const Desired &right) {
    const auto left_depth =
        authority_->catalog().get_node(left.node_id).graph_depth;
    const auto right_depth =
        authority_->catalog().get_node(right.node_id).graph_depth;
    return left_depth != right_depth
               ? left_depth < right_depth
               : ordinal_less(left.node_id, right.node_id);
  });
  std::vector<ForeignResearchAwarenessEvent> events;
  for (const auto &entry : desired) {
    const auto &node = authority_->catalog().get_node(entry.node_id);
    if (!node.public_normal_research)
      continue;
    const auto scientific = authority_->kernel()
                                .eligibility()
                                .evaluate_scientific_eligibility(
                                    state, node.id, view(context));
    const auto target = scientific.allowed ? ResearchMaturity::investigable
                                           : entry.state;
    if (static_cast<int>(target) >
        static_cast<int>(ResearchMaturity::investigable))
      throw std::runtime_error(
          "Foreign discovery may not directly materialize "
          "Experimental-or-higher native research state.");
    const auto *existing = state.try_get_node_state(node.id);
    if (existing) {
      if (existing->maturity == ResearchMaturity::experimental ||
          existing->maturity == ResearchMaturity::demonstrated ||
          existing->maturity == ResearchMaturity::engineering ||
          existing->maturity == ResearchMaturity::mature)
        continue;
      if (existing->counts_as_established_knowledge())
        continue;
      if (existing->maturity == ResearchMaturity::archived &&
          (!existing->resolution ||
           !ascii_iequal(*existing->resolution, "disproven")))
        continue;
      if (existing->maturity != ResearchMaturity::archived &&
          static_cast<int>(existing->maturity) >= static_cast<int>(target))
        continue;
    }
    const auto previous = existing
                              ? std::optional<ResearchMaturity>(
                                    existing->maturity)
                              : std::nullopt;
    const auto total = existing ? existing->total_research_points : 0.0;
    detail::AdaptiveResearchStateWriter::set_node_state(
        state,
        {node.id,
         target,
         target < ResearchMaturity::investigable
             ? std::optional<std::string>("foreign_awareness:" + reference)
             : std::nullopt,
         0,
         total,
         0});
    events.push_back(
        {reference, node.id, previous, target,
         scientific.allowed
             ? "Foreign contact supplied awareness/evidence and all normal "
               "native scientific requirements are now satisfied."
             : entry.reason});
  }
  return events;
}

} // namespace stellar::core
