#include <stellar/core/adaptive_research_foreign_technology.hpp>
#include <stellar/core/detail/adaptive_research_foreign_technology_support_access.hpp>

#include <stellar/core/detail/adaptive_research_foreign_technology_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_weak_state_table.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

struct TransparentHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
  std::size_t operator()(const std::string &value) const noexcept {
    return (*this)(std::string_view(value));
  }
};
struct TransparentEqual {
  using is_transparent = void;
  bool operator()(std::string_view left,
                  std::string_view right) const noexcept {
    return left == right;
  }
};
template <class Value>
using Lookup =
    std::unordered_map<std::string, Value, TransparentHash, TransparentEqual>;
using StringSet =
    std::unordered_set<std::string, TransparentHash, TransparentEqual>;

Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not open " + path.string());
  Json result;
  input >> result;
  return result;
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
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
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
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
    value.remove_prefix(width);
  }
  return result;
}
bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}
void sort_unique(std::vector<std::string> &values) {
  std::ranges::sort(values, ordinal_less);
  values.erase(std::ranges::unique(values).begin(), values.end());
}

std::string required_string(const Json &value, std::string_view name,
                            std::string_view source) {
  const auto found = value.find(std::string(name));
  if (found == value.end() || !found->is_string() ||
      blank(found->get_ref<const std::string &>()))
    throw AdaptiveResearchForeignCatalogDataError(std::string(source) +
                             " is missing non-empty string '" +
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
const Json &require_object(const Json &value) {
  if (!value.is_object())
    throw AdaptiveResearchForeignJsonOperationError(
        "The requested operation requires an element of type 'Object', but "
        "the target element has type '" +
        std::string(json_kind(value)) + "'.");
  return value;
}
bool required_bool(const Json &value) {
  if (!value.is_boolean())
    throw AdaptiveResearchForeignJsonOperationError(
        "The requested operation requires an element of type 'Boolean', but "
        "the target element has type '" +
        std::string(json_kind(value)) + "'.");
  return value.get<bool>();
}
std::vector<std::string> string_array(const Json &value,
                                      std::string_view name) {
  const auto found = value.find(std::string(name));
  if (found == value.end())
    return {};
  if (!found->is_array())
    throw AdaptiveResearchForeignJsonOperationError("JSON value is not an array.");
  std::vector<std::string> result;
  for (const auto &entry : *found) {
    if (entry.is_null())
      throw AdaptiveResearchForeignCatalogDataError(std::string(name) +
                                                    " contains null.");
    result.push_back(entry.get<std::string>());
  }
  return result;
}
void validate_catalog_id(const Json &root, std::string_view expected,
                         std::string_view source) {
  const auto actual = required_string(root, "catalog_id", source);
  if (actual != expected)
    throw AdaptiveResearchForeignCatalogDataError(std::string(source) + " catalog_id '" + actual +
                             "' does not match '" + std::string(expected) +
                             "'.");
}
void validate_axis(const Json &root, std::string_view property,
                   std::span<const std::string_view> expected) {
  std::vector<std::string> actual;
  for (const auto &entry :
       require_array(root.at(std::string(property))))
    actual.push_back(required_string(
        entry, "id", "foreign_technology_model.json:" + std::string(property)));
  if (actual.size() != expected.size() || !std::ranges::equal(actual, expected))
    throw AdaptiveResearchForeignCatalogDataError("Foreign technology axis '" +
                             std::string(property) +
                             "' does not match the executable runtime order.");
}
std::int64_t next_revision(std::int64_t value) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw std::overflow_error(
        "Adaptive Research foreign-technology revision space is exhausted.");
  return value + 1;
}

template <class Value> class SlotMap {
  struct Entry {
    std::string key;
    Value value;
  };
  std::vector<Entry> entries_;
  Lookup<std::size_t> index_;

public:
  Value *find(std::string_view key) noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  const Value *find(std::string_view key) const noexcept {
    const auto found = index_.find(key);
    return found == index_.end() ? nullptr : &entries_[found->second].value;
  }
  void set(std::string key, Value value) {
    if (auto *existing = find(key)) {
      *existing = std::move(value);
      return;
    }
    index_.emplace(key, entries_.size());
    entries_.push_back({std::move(key), std::move(value)});
  }
  bool contains(std::string_view key) const noexcept { return find(key); }
  template <class Function> void each(Function function) const {
    for (const auto &entry : entries_)
      function(entry.value);
  }
};
} // namespace

struct AdaptiveResearchForeignTechnologyCatalog::Storage {
  std::vector<std::string> constraints;
  StringSet constraint_index;
  std::vector<ForeignTechnologyPackageComponentDefinition> components;
  Lookup<std::size_t> component_index;
  std::vector<std::string> rights;
  StringSet right_index;
  ForeignTechnologyRuntimePolicy policy;
  Lookup<ForeignUnderstandingState> minimum_index;
};

AdaptiveResearchForeignTechnologyCatalog::
    AdaptiveResearchForeignTechnologyCatalog(
        std::unique_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}
AdaptiveResearchForeignTechnologyCatalog::
    ~AdaptiveResearchForeignTechnologyCatalog() = default;
AdaptiveResearchForeignTechnologyCatalog::
    AdaptiveResearchForeignTechnologyCatalog(
        AdaptiveResearchForeignTechnologyCatalog &&) noexcept = default;
AdaptiveResearchForeignTechnologyCatalog &
AdaptiveResearchForeignTechnologyCatalog::operator=(
    AdaptiveResearchForeignTechnologyCatalog &&) noexcept = default;
std::span<const std::string>
AdaptiveResearchForeignTechnologyCatalog::constraint_ids() const noexcept {
  return storage_->constraints;
}
std::span<const ForeignTechnologyPackageComponentDefinition>
AdaptiveResearchForeignTechnologyCatalog::components() const noexcept {
  return storage_->components;
}
std::span<const std::string>
AdaptiveResearchForeignTechnologyCatalog::rights() const noexcept {
  return storage_->rights;
}
const ForeignTechnologyRuntimePolicy &
AdaptiveResearchForeignTechnologyCatalog::runtime_policy() const noexcept {
  return storage_->policy;
}
bool AdaptiveResearchForeignTechnologyCatalog::has_constraint(
    std::string_view id) const noexcept {
  return storage_->constraint_index.contains(id);
}
bool AdaptiveResearchForeignTechnologyCatalog::has_component(
    std::string_view id) const noexcept {
  return storage_->component_index.contains(id);
}
bool AdaptiveResearchForeignTechnologyCatalog::has_right(
    std::string_view id) const noexcept {
  return storage_->right_index.contains(id);
}
const ForeignTechnologyPackageComponentDefinition &
AdaptiveResearchForeignTechnologyCatalog::get_component(
    std::string_view id) const {
  const auto found = storage_->component_index.find(id);
  if (found == storage_->component_index.end())
    throw AdaptiveResearchForeignMissingRecord("Unknown technology-transfer component '" +
                            std::string(id) + "'.");
  return storage_->components[found->second];
}
ForeignUnderstandingState
AdaptiveResearchForeignTechnologyCatalog::minimum_understanding(
    std::string_view component_id) const {
  const auto found = storage_->minimum_index.find(component_id);
  if (found == storage_->minimum_index.end())
    throw AdaptiveResearchForeignMissingRecord("Unknown technology-transfer component '" +
                            std::string(component_id) + "'.");
  return found->second;
}
ForeignUnderstandingState
AdaptiveResearchForeignTechnologyCatalog::parse_understanding(
    std::string_view id) {
  if (id == "unknown")
    return ForeignUnderstandingState::unknown;
  if (id == "observed")
    return ForeignUnderstandingState::observed;
  if (id == "characterized")
    return ForeignUnderstandingState::characterized;
  if (id == "principle_understood")
    return ForeignUnderstandingState::principle_understood;
  if (id == "engineering_understood")
    return ForeignUnderstandingState::engineering_understood;
  throw AdaptiveResearchForeignCatalogDataError("Unknown foreign understanding state '" +
                           std::string(id) + "'.");
}

AdaptiveResearchForeignTechnologyCatalog
load_adaptive_research_foreign_technology_catalog(
    const std::filesystem::path &root_path,
    const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchExpertiseCatalog &expertise_catalog) {
  auto storage =
      std::make_unique<AdaptiveResearchForeignTechnologyCatalog::Storage>();
  const auto foreign = read_json(root_path / "foreign_technology_model.json");
  validate_catalog_id(foreign, catalog.metadata().catalog_id,
                      "foreign_technology_model.json");
  static constexpr std::string_view understanding[] = {
      "unknown", "observed", "characterized", "principle_understood",
      "engineering_understood"};
  static constexpr std::string_view operability[] = {
      "unknown",           "unusable",
      "origin_only",       "supported_operation",
      "adapted_operation", "native_operation"};
  static constexpr std::string_view reproduction[] = {
      "none", "component_replication", "subsystem_replication",
      "foreign_process_replication", "native_process_replication"};
  static constexpr std::string_view adaptation[] = {
      "none", "conceptual_inspiration", "interface_adaptation",
      "native_derivative", "hybrid_lineage"};
  validate_axis(foreign, "understanding_axis", understanding);
  validate_axis(foreign, "operability_axis", operability);
  validate_axis(foreign, "reproduction_axis", reproduction);
  validate_axis(foreign, "adaptation_axis", adaptation);
  for (const auto &entry :
       require_array(foreign.at("compatibility_constraints"))) {
    auto id = required_string(entry, "id", "foreign_technology_model.json");
    if (storage->constraint_index.insert(id).second)
      storage->constraints.push_back(std::move(id));
  }
  if (storage->constraints.size() != 12)
    throw AdaptiveResearchForeignCatalogDataError(
        "Expected 12 foreign-technology compatibility constraints, found " +
        std::to_string(storage->constraints.size()) + ".");

  const auto exchange = read_json(root_path / "technology_exchange_model.json");
  validate_catalog_id(exchange, catalog.metadata().catalog_id,
                      "technology_exchange_model.json");
  for (const auto &entry :
       require_array(exchange.at("transfer_package_components"))) {
    auto id = required_string(entry, "id", "technology_exchange_model.json");
    auto tacit = string_array(entry, "creates_or_transfers_tacit_asset_types");
    for (const auto &tacit_id : tacit)
      if (std::ranges::none_of(
              expertise_catalog.tacit_asset_types(),
              [&](const auto &value) { return value.id == tacit_id; }))
        throw AdaptiveResearchForeignCatalogDataError("Transfer component '" + id +
                                 "' references unknown tacit asset type '" +
                                 tacit_id + "'.");
    const auto creates_or_references_evidence =
        required_bool(entry.at("creates_or_references_evidence"));
    const auto index = storage->components.size();
    if (!storage->component_index.emplace(id, index).second)
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " + id);
    storage->components.push_back(
        {std::move(id), std::move(tacit), creates_or_references_evidence});
  }
  if (storage->components.size() != 10)
    throw AdaptiveResearchForeignCatalogDataError("Expected 10 transfer package components, found " +
                             std::to_string(storage->components.size()) + ".");
  for (const auto &entry : require_array(exchange.at("rights"))) {
    auto id = required_string(entry, "id", "technology_exchange_model.json");
    if (storage->right_index.insert(id).second)
      storage->rights.push_back(std::move(id));
  }
  if (storage->rights.size() != 11)
    throw AdaptiveResearchForeignCatalogDataError("Expected 11 technology-transfer rights, found " +
                             std::to_string(storage->rights.size()) + ".");

  const auto policy =
      read_json(root_path / "foreign_technology_runtime_policy.json");
  validate_catalog_id(policy, catalog.metadata().catalog_id,
                      "foreign_technology_runtime_policy.json");
  const auto &intake = policy.at("package_intake");
  for (const auto &[id, value] :
       require_object(intake.at("minimum_understanding_by_component")).items()) {
    if (!storage->component_index.contains(id))
      throw AdaptiveResearchForeignCatalogDataError(
          "Foreign technology runtime policy references unknown package "
          "component '" +
          id + "'.");
    const auto parsed_id = value.is_null() ? std::string{} : value.get<std::string>();
    const auto parsed =
        AdaptiveResearchForeignTechnologyCatalog::parse_understanding(
            parsed_id);
    if (!storage->minimum_index.emplace(id, parsed).second)
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " + id);
    storage->policy.minimum_understanding_by_component.push_back({id, parsed});
  }
  if (storage->minimum_index.size() != storage->components.size())
    throw AdaptiveResearchForeignCatalogDataError(
        "Foreign technology runtime policy must define intake understanding "
        "for every transfer component.");
  const auto &assimilation = policy.at("assimilation");
  const auto &confidence = policy.at("confidence");
  auto &runtime = storage->policy;
  runtime.conceptual_inspiration_if_characterized =
      intake.at("conceptual_inspiration_if_characterized").get<bool>();
  runtime.minimum_training_continuity_for_trained =
      assimilation.at("minimum_training_continuity_to_advance_to_trained")
          .get<double>();
  runtime.minimum_translation_quality_beyond_access =
      assimilation.at("minimum_translation_quality_to_advance_beyond_access")
          .get<double>();
  runtime.new_observation_confidence =
      confidence.at("new_assessment_from_observation").get<double>();
  runtime.new_characterized_confidence =
      confidence.at("new_assessment_from_characterized_package").get<double>();
  runtime.controlled_analysis_confidence_minimum =
      confidence.at("successful_controlled_analysis_minimum").get<double>();
  runtime.operational_fact_confidence_minimum =
      confidence.at("successful_operational_or_reproduction_test_minimum")
          .get<double>();
  runtime.engineering_understood_confidence_minimum =
      confidence.at("engineering_understood_minimum").get<double>();
  const double values[] = {runtime.minimum_training_continuity_for_trained,
                           runtime.minimum_translation_quality_beyond_access,
                           runtime.new_observation_confidence,
                           runtime.new_characterized_confidence,
                           runtime.controlled_analysis_confidence_minimum,
                           runtime.operational_fact_confidence_minimum,
                           runtime.engineering_understood_confidence_minimum};
  if (std::ranges::any_of(values, [](double value) {
        return value < 0 || value > 1 || std::isnan(value) || std::isinf(value);
      }))
    throw AdaptiveResearchForeignCatalogDataError(
        "Foreign technology runtime policy contains a value outside 0..1.");
  return AdaptiveResearchForeignTechnologyCatalog(std::move(storage));
}

struct AdaptiveResearchForeignTechnologyState::Storage {
  std::int64_t revision{};
  SlotMap<ForeignTechnologyAssessmentRuntimeState> assessments;
  SlotMap<ForeignTechnologyPackageRuntimeState> packages;
  std::vector<ForeignTechnologyAssessmentRuntimeState> assessment_cache;
  std::vector<ForeignTechnologyPackageRuntimeState> package_cache;

  void rebuild_assessments() {
    assessment_cache.clear();
    assessments.each(
        [&](const auto &value) { assessment_cache.push_back(value); });
  }
  void rebuild_packages() {
    package_cache.clear();
    packages.each([&](const auto &value) { package_cache.push_back(value); });
  }
};

class AdaptiveResearchForeignTechnologyState::Writer {
public:
  static void set_assessment(AdaptiveResearchForeignTechnologyState &state,
                             ForeignTechnologyAssessmentRuntimeState value) {
    if (value.confidence < 0 || value.confidence > 1 ||
        std::isnan(value.confidence) || std::isinf(value.confidence))
      throw AdaptiveResearchForeignArgumentOutOfRange(
          "Foreign technology confidence must be in 0..1. (Parameter "
          "'value')");
    auto &storage = *state.storage_;
    if (const auto *existing =
            storage.assessments.find(value.foreign_technology_reference);
        existing &&
        existing->source_lineage_reference != value.source_lineage_reference)
      throw std::runtime_error("Foreign technology '" +
                               value.foreign_technology_reference +
                               "' cannot change source lineage from '" +
                               existing->source_lineage_reference + "' to '" +
                               value.source_lineage_reference + "'.");
    sort_unique(value.known_constraint_ids);
    sort_unique(value.evidence_refs);
    sort_unique(value.tacit_asset_refs);
    const auto next = next_revision(storage.revision);
    value.revision = next;
    auto key = value.foreign_technology_reference;
    storage.assessments.set(std::move(key), std::move(value));
    storage.revision = next;
    storage.rebuild_assessments();
  }

  static void add_package(AdaptiveResearchForeignTechnologyState &state,
                          ForeignTechnologyPackageRuntimeState value) {
    if (value.integrity < 0 || value.integrity > 1 ||
        std::isnan(value.integrity) || std::isinf(value.integrity))
      throw AdaptiveResearchForeignArgumentOutOfRange(
          "Foreign technology package integrity must be in 0..1. (Parameter "
          "'value')");
    auto &storage = *state.storage_;
    if (storage.packages.contains(value.package_id))
      throw std::runtime_error("Foreign technology package '" +
                               value.package_id +
                               "' already exists for this holder.");
    sort_unique(value.component_ids);
    sort_unique(value.right_ids);
    sort_unique(value.evidence_refs);
    sort_unique(value.tacit_asset_refs);
    sort_unique(value.knowledge_field_ids);
    const auto next = next_revision(storage.revision);
    value.revision = next;
    auto key = value.package_id;
    storage.packages.set(std::move(key), std::move(value));
    storage.revision = next;
    storage.rebuild_packages();
  }

  static void set_revision_for_recovery(
      AdaptiveResearchForeignTechnologyState &state,
      std::int64_t revision) noexcept {
    state.storage_->revision = revision;
  }
};

AdaptiveResearchForeignTechnologyState::AdaptiveResearchForeignTechnologyState()
    : storage_(std::make_unique<Storage>()) {}
AdaptiveResearchForeignTechnologyState::
    ~AdaptiveResearchForeignTechnologyState() = default;
AdaptiveResearchForeignTechnologyState::AdaptiveResearchForeignTechnologyState(
    const AdaptiveResearchForeignTechnologyState &other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}
AdaptiveResearchForeignTechnologyState &
AdaptiveResearchForeignTechnologyState::operator=(
    const AdaptiveResearchForeignTechnologyState &other) {
  if (this != &other)
    storage_ = std::make_unique<Storage>(*other.storage_);
  return *this;
}
AdaptiveResearchForeignTechnologyState::AdaptiveResearchForeignTechnologyState(
    AdaptiveResearchForeignTechnologyState &&) noexcept = default;
AdaptiveResearchForeignTechnologyState &
AdaptiveResearchForeignTechnologyState::operator=(
    AdaptiveResearchForeignTechnologyState &&) noexcept = default;
std::int64_t AdaptiveResearchForeignTechnologyState::revision() const noexcept {
  return storage_->revision;
}
std::span<const ForeignTechnologyAssessmentRuntimeState>
AdaptiveResearchForeignTechnologyState::assessments() const noexcept {
  return storage_->assessment_cache;
}
std::span<const ForeignTechnologyPackageRuntimeState>
AdaptiveResearchForeignTechnologyState::packages() const noexcept {
  return storage_->package_cache;
}
const ForeignTechnologyAssessmentRuntimeState *
AdaptiveResearchForeignTechnologyState::try_get_assessment(
    std::string_view foreign_technology_reference) const noexcept {
  return storage_->assessments.find(foreign_technology_reference);
}
const ForeignTechnologyPackageRuntimeState *
AdaptiveResearchForeignTechnologyState::try_get_package(
    std::string_view package_id) const noexcept {
  return storage_->packages.find(package_id);
}
ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyState::get_or_unknown(
    std::string foreign_technology_reference,
    std::string source_lineage_reference) const {
  if (const auto *value = try_get_assessment(foreign_technology_reference))
    return *value;
  return {std::move(foreign_technology_reference),
          std::move(source_lineage_reference),
          ForeignUnderstandingState::unknown,
          ForeignOperabilityState::unknown,
          ForeignReproductionState::none,
          ForeignAdaptationState::none,
          {},
          {},
          {},
          0,
          0,
          storage_->revision};
}

void detail::AdaptiveResearchForeignTechnologyStateWriter::set_assessment(
    AdaptiveResearchForeignTechnologyState &state,
    ForeignTechnologyAssessmentRuntimeState value) {
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      state, std::move(value));
}
void detail::AdaptiveResearchForeignTechnologyStateWriter::add_package(
    AdaptiveResearchForeignTechnologyState &state,
    ForeignTechnologyPackageRuntimeState value) {
  AdaptiveResearchForeignTechnologyState::Writer::add_package(state,
                                                              std::move(value));
}
void detail::AdaptiveResearchForeignTechnologyStateWriter::
    set_revision_for_recovery(
        AdaptiveResearchForeignTechnologyState &state,
        std::int64_t revision) noexcept {
  AdaptiveResearchForeignTechnologyState::Writer::set_revision_for_recovery(
      state, revision);
}

namespace {
template <class Enum> Enum enum_max(Enum left, Enum right) noexcept {
  return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}
void validate_unit(double value, std::string_view name) {
  if (value < 0 || value > 1 || std::isnan(value) || std::isinf(value))
    throw AdaptiveResearchForeignArgumentOutOfRange("Value must be in 0..1. (Parameter '" +
                            std::string(name) + "')\r\nActual value was " +
                            detail::legacy_general(value) + ".");
}
void validate_100(double value, std::string_view name) {
  if (value < 0 || value > 100 || std::isnan(value) || std::isinf(value))
    throw AdaptiveResearchForeignArgumentOutOfRange("Value must be in 0..100. (Parameter '" +
                            std::string(name) + "')\r\nActual value was " +
                            detail::legacy_general(value) + ".");
}
std::vector<std::string>
distinct_in_order(std::span<const std::string> values) {
  StringSet seen;
  std::vector<std::string> result;
  for (const auto &value : values)
    if (seen.insert(value).second)
      result.push_back(value);
  return result;
}
std::optional<std::string_view>
view(const std::optional<std::string> &value) noexcept {
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}
} // namespace

struct AdaptiveResearchForeignTechnologyRuntime::Storage {
  const AdaptiveResearchAuthority *authority;
  const AdaptiveResearchForeignTechnologyCatalog *catalog;
  mutable detail::AdaptiveResearchWeakStateTable<
      AdaptiveResearchForeignTechnologyState>
      states;

  Storage(
      const AdaptiveResearchAuthority &authority_value,
      const AdaptiveResearchForeignTechnologyCatalog &catalog_value) noexcept
      : authority(&authority_value), catalog(&catalog_value) {}

  void validate_constraint(std::string_view id) const {
    if (!catalog->has_constraint(id))
      throw std::invalid_argument(
          "Unknown foreign-technology compatibility constraint '" +
          std::string(id) + "'. (Parameter 'constraintId')");
  }

  std::vector<std::string>
  merge_constraints(std::span<const std::string> existing,
                    std::span<const std::string> added) const {
    std::vector<std::string> result(existing.begin(), existing.end());
    result.insert(result.end(), added.begin(), added.end());
    sort_unique(result);
    for (const auto &id : result)
      validate_constraint(id);
    return result;
  }

  void validate_reference(std::string_view value,
                          std::string_view parameter) const {
    if (blank(value))
      throw std::invalid_argument("Reference cannot be empty. (Parameter '" +
                                  std::string(parameter) + "')");
  }

  void validate_package(const ForeignTechnologyPackageInput &input) const {
    validate_reference(input.package_id, "PackageId");
    validate_reference(input.foreign_technology_reference,
                       "ForeignTechnologyReference");
    validate_reference(input.source_lineage_reference,
                       "SourceLineageReference");
    if (input.integrity < 0 || input.integrity > 1 ||
        std::isnan(input.integrity) || std::isinf(input.integrity))
      throw AdaptiveResearchForeignArgumentOutOfRange("Specified argument was out of the range of "
                              "valid values. (Parameter 'Integrity')");
    validate_unit(input.translation_context_quality,
                  "TranslationContextQuality");
    validate_unit(input.training_continuity, "TrainingContinuity");
    for (const auto &id : input.component_ids)
      if (!catalog->has_component(id))
        throw std::invalid_argument("Unknown technology-transfer component '" +
                                    id + "'. (Parameter 'ComponentIds')");
    for (const auto &id : input.right_ids)
      if (!catalog->has_right(id))
        throw std::invalid_argument("Unknown technology-transfer right '" + id +
                                    "'. (Parameter 'RightIds')");
    for (const auto &id : input.knowledge_field_ids)
      if (std::ranges::none_of(
              authority->expertise_catalog().fields(),
              [&](const auto &field) { return field.id == id; }))
        throw std::invalid_argument("Unknown research knowledge field '" + id +
                                    "'. (Parameter 'KnowledgeFieldIds')");
    for (const auto &id : input.known_constraint_ids)
      validate_constraint(id);
    for (const auto &evidence : input.evidence) {
      if (!authority->catalog().has_evidence_type(evidence.evidence_type_id))
        throw std::invalid_argument("Unknown foreign package evidence type '" +
                                    evidence.evidence_type_id +
                                    "'. (Parameter 'Evidence')");
      validate_unit(evidence.quality, "Quality");
      validate_unit(evidence.confidence, "Confidence");
    }
  }

  const ForeignTechnologyAssessmentRuntimeState &
  require_assessment(const AdaptiveResearchForeignTechnologyState &state,
                     std::string_view reference) const {
    const auto *value = state.try_get_assessment(reference);
    if (!value)
      throw AdaptiveResearchForeignMissingRecord("Foreign technology '" + std::string(reference) +
                              "' has not been legitimately observed/acquired.");
    return *value;
  }
};

AdaptiveResearchForeignTechnologyRuntime::
    AdaptiveResearchForeignTechnologyRuntime(
        const AdaptiveResearchAuthority &authority,
        const AdaptiveResearchForeignTechnologyCatalog &catalog)
    : storage_(std::make_unique<Storage>(authority, catalog)) {}
AdaptiveResearchForeignTechnologyRuntime::
    ~AdaptiveResearchForeignTechnologyRuntime() = default;
AdaptiveResearchForeignTechnologyRuntime::
    AdaptiveResearchForeignTechnologyRuntime(
        AdaptiveResearchForeignTechnologyRuntime &&) noexcept = default;
AdaptiveResearchForeignTechnologyRuntime &
AdaptiveResearchForeignTechnologyRuntime::operator=(
    AdaptiveResearchForeignTechnologyRuntime &&) noexcept = default;
const AdaptiveResearchForeignTechnologyState &
AdaptiveResearchForeignTechnologyRuntime::state(
    const AdaptiveResearchCivilizationState &civilization) const {
  return storage_->states.get_or_create(civilization);
}

AdaptiveResearchForeignTechnologyState &
detail::AdaptiveResearchForeignTechnologySupportAccess::get_or_create(
    const AdaptiveResearchForeignTechnologyRuntime &runtime,
    const AdaptiveResearchCivilizationState &civilization) {
  return runtime.storage_->states.get_or_create(civilization);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::observe(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    std::string_view source_lineage_reference_view, double confidence,
    double year, std::span<const std::string> known_constraint_ids_view) const {
  const std::string foreign_reference(foreign_technology_reference_view);
  const std::string lineage_reference(source_lineage_reference_view);
  const std::vector<std::string> known_constraints(
      known_constraint_ids_view.begin(), known_constraint_ids_view.end());
  storage_->validate_reference(foreign_reference, "foreignTechnologyReference");
  storage_->validate_reference(lineage_reference, "sourceLineageReference");
  validate_unit(confidence, "value");
  auto &foreign = storage_->states.get_or_create(state);
  auto current = foreign.get_or_unknown(foreign_reference, lineage_reference);
  auto constraints = storage_->merge_constraints(current.known_constraint_ids,
                                                 known_constraints);
  current.understanding =
      enum_max(current.understanding, ForeignUnderstandingState::observed);
  current.known_constraint_ids = std::move(constraints);
  current.last_assessment_year = year;
  current.confidence = std::max(
      current.confidence,
      std::max(confidence,
               storage_->catalog->runtime_policy().new_observation_confidence));
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(foreign_reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::acquire_package(
    AdaptiveResearchCivilizationState &state,
    const ForeignTechnologyPackageInput &input_view) const {
  const ForeignTechnologyPackageInput input = input_view;
  storage_->validate_package(input);
  auto &foreign = storage_->states.get_or_create(state);
  if (foreign.try_get_package(input.package_id))
    throw std::runtime_error("Foreign technology package '" + input.package_id +
                             "' is already held.");
  std::vector<std::string> evidence_refs;
  for (const auto &evidence : input.evidence) {
    const auto quality =
        std::clamp(evidence.quality * input.integrity, 0.0, 1.0);
    const auto context = evidence.context_id
                             ? evidence.context_id
                             : input.target_applicability_context_id;
    (void)storage_->authority->add_evidence(
        state, evidence.evidence_instance_id, evidence.evidence_type_id,
        evidence.provenance, quality, evidence.confidence, view(context));
    evidence_refs.push_back(evidence.evidence_instance_id);
  }
  std::vector<std::string> tacit_refs;
  const auto component_ids = distinct_in_order(input.component_ids);
  const auto field_ids = distinct_in_order(input.knowledge_field_ids);
  for (const auto &component_id : component_ids) {
    const auto &component = storage_->catalog->get_component(component_id);
    for (const auto &asset_type_id : component.tacit_asset_type_ids) {
      for (const auto &field_id : field_ids) {
        auto asset_id = "foreign:" + input.package_id + ":" + component_id +
                        ":" + asset_type_id + ":" + field_id;
        storage_->authority->set_tacit_asset(
            state, asset_id, asset_type_id,
            ResearchTacitScopeKind::knowledge_field, field_id,
            ResearchTacitAssimilationStage::access, 100 * input.integrity, 1,
            input.translation_context_quality, input.training_continuity,
            input.provenance, view(input.target_applicability_context_id));
        tacit_refs.push_back(std::move(asset_id));
      }
    }
  }
  AdaptiveResearchForeignTechnologyState::Writer::add_package(
      foreign,
      {input.package_id, input.foreign_technology_reference,
       input.source_lineage_reference, input.component_ids, input.right_ids,
       evidence_refs, tacit_refs, input.knowledge_field_ids, input.provenance,
       input.integrity, next_revision(foreign.revision())});
  auto current = foreign.get_or_unknown(input.foreign_technology_reference,
                                        input.source_lineage_reference);
  auto intake = ForeignUnderstandingState::observed;
  if (!input.component_ids.empty()) {
    intake = storage_->catalog->minimum_understanding(input.component_ids[0]);
    for (const auto &id :
         std::span<const std::string>(input.component_ids).subspan(1))
      intake = enum_max(intake, storage_->catalog->minimum_understanding(id));
  }
  current.understanding = enum_max(current.understanding, intake);
  if (storage_->catalog->runtime_policy()
          .conceptual_inspiration_if_characterized &&
      static_cast<int>(current.understanding) >=
          static_cast<int>(ForeignUnderstandingState::characterized))
    current.adaptation = enum_max(
        current.adaptation, ForeignAdaptationState::conceptual_inspiration);
  const auto confidence_floor =
      static_cast<int>(current.understanding) >=
              static_cast<int>(ForeignUnderstandingState::characterized)
          ? storage_->catalog->runtime_policy().new_characterized_confidence
          : storage_->catalog->runtime_policy().new_observation_confidence;
  current.known_constraint_ids = storage_->merge_constraints(
      current.known_constraint_ids, input.known_constraint_ids);
  current.evidence_refs.insert(current.evidence_refs.end(),
                               evidence_refs.begin(), evidence_refs.end());
  current.tacit_asset_refs.insert(current.tacit_asset_refs.end(),
                                  tacit_refs.begin(), tacit_refs.end());
  current.last_assessment_year = input.acquired_year;
  current.confidence =
      std::max(current.confidence, confidence_floor * input.integrity);
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(input.foreign_technology_reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::record_analysis_result(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    ForeignUnderstandingState target_understanding, double confidence,
    double year,
    std::span<const std::string> newly_known_constraint_ids_view) const {
  const std::string reference(foreign_technology_reference_view);
  const std::vector<std::string> newly_known(
      newly_known_constraint_ids_view.begin(),
      newly_known_constraint_ids_view.end());
  validate_unit(confidence, "value");
  auto &foreign = storage_->states.get_or_create(state);
  const auto *found = foreign.try_get_assessment(reference);
  if (!found)
    throw std::runtime_error("Cannot analyze unobserved foreign technology '" +
                             reference + "'.");
  auto current = *found;
  if (static_cast<int>(target_understanding) <
      static_cast<int>(current.understanding))
    throw std::runtime_error(
        "Controlled analysis cannot erase established foreign-technology "
        "understanding.");
  const auto &policy = storage_->catalog->runtime_policy();
  if (static_cast<int>(target_understanding) >=
          static_cast<int>(ForeignUnderstandingState::principle_understood) &&
      confidence < policy.controlled_analysis_confidence_minimum)
    throw std::runtime_error(
        "Principle-understood assessment requires stronger "
        "controlled-analysis confidence.");
  if (target_understanding ==
          ForeignUnderstandingState::engineering_understood &&
      confidence < policy.engineering_understood_confidence_minimum)
    throw std::runtime_error(
        "Engineering-understood assessment requires the configured high "
        "confidence.");
  current.understanding = target_understanding;
  if (policy.conceptual_inspiration_if_characterized &&
      static_cast<int>(target_understanding) >=
          static_cast<int>(ForeignUnderstandingState::characterized))
    current.adaptation = enum_max(
        current.adaptation, ForeignAdaptationState::conceptual_inspiration);
  current.known_constraint_ids =
      storage_->merge_constraints(current.known_constraint_ids, newly_known);
  current.last_assessment_year = year;
  current.confidence = std::max(current.confidence, confidence);
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::record_operability_fact(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    ForeignOperabilityState operability, double confidence, double year) const {
  const std::string reference(foreign_technology_reference_view);
  validate_unit(confidence, "value");
  if (confidence <
      storage_->catalog->runtime_policy().operational_fact_confidence_minimum)
    throw std::runtime_error(
        "Operational/reproduction facts require the configured "
        "controlled-test confidence.");
  auto &foreign = storage_->states.get_or_create(state);
  auto current = storage_->require_assessment(foreign, reference);
  current.operability = operability;
  current.last_assessment_year = year;
  current.confidence = std::max(current.confidence, confidence);
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::record_reproduction_fact(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    ForeignReproductionState reproduction, double confidence,
    double year) const {
  const std::string reference(foreign_technology_reference_view);
  validate_unit(confidence, "value");
  if (confidence <
      storage_->catalog->runtime_policy().operational_fact_confidence_minimum)
    throw std::runtime_error(
        "Operational/reproduction facts require the configured "
        "controlled-test confidence.");
  auto &foreign = storage_->states.get_or_create(state);
  auto current = storage_->require_assessment(foreign, reference);
  current.reproduction = reproduction;
  current.last_assessment_year = year;
  current.confidence = std::max(current.confidence, confidence);
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::record_adaptation_result(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    ForeignAdaptationState adaptation, double confidence, double year) const {
  const std::string reference(foreign_technology_reference_view);
  validate_unit(confidence, "value");
  auto &foreign = storage_->states.get_or_create(state);
  auto current = storage_->require_assessment(foreign, reference);
  if (static_cast<int>(adaptation) < static_cast<int>(current.adaptation))
    throw std::runtime_error(
        "A completed foreign-derived native adaptation lineage cannot be "
        "unlearned by reassessment.");
  current.adaptation = adaptation;
  current.last_assessment_year = year;
  current.confidence = std::max(current.confidence, confidence);
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::confirm_constraint(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    std::string_view constraint_id_view, double year) const {
  const std::string reference(foreign_technology_reference_view);
  const std::string constraint_id(constraint_id_view);
  storage_->validate_constraint(constraint_id);
  auto &foreign = storage_->states.get_or_create(state);
  auto current = storage_->require_assessment(foreign, reference);
  const std::vector<std::string> added{constraint_id};
  current.known_constraint_ids =
      storage_->merge_constraints(current.known_constraint_ids, added);
  current.last_assessment_year = year;
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

ForeignTechnologyAssessmentRuntimeState
AdaptiveResearchForeignTechnologyRuntime::resolve_constraint(
    AdaptiveResearchCivilizationState &state,
    std::string_view foreign_technology_reference_view,
    std::string_view constraint_id_view, double year) const {
  const std::string reference(foreign_technology_reference_view);
  const std::string constraint_id(constraint_id_view);
  storage_->validate_constraint(constraint_id);
  auto &foreign = storage_->states.get_or_create(state);
  auto current = storage_->require_assessment(foreign, reference);
  std::erase(current.known_constraint_ids, constraint_id);
  current.last_assessment_year = year;
  AdaptiveResearchForeignTechnologyState::Writer::set_assessment(
      foreign, std::move(current));
  return *foreign.try_get_assessment(reference);
}

void AdaptiveResearchForeignTechnologyRuntime::
    advance_package_tacit_assimilation(
        AdaptiveResearchCivilizationState &state,
        std::string_view package_id_view,
        ResearchTacitAssimilationStage target_stage) const {
  const std::string package_id(package_id_view);
  auto &foreign = storage_->states.get_or_create(state);
  const auto *package = foreign.try_get_package(package_id);
  if (!package)
    throw AdaptiveResearchForeignMissingRecord("Unknown held foreign technology package '" +
                            package_id + "'.");
  const std::vector<std::string> asset_ids = package->tacit_asset_refs;
  for (const auto &asset_id : asset_ids) {
    const auto assets = state.expertise().tacit_assets();
    const auto found = std::ranges::find(
        assets, asset_id, &ResearchTacitAssetRuntimeState::asset_id);
    if (found == assets.end())
      continue;
    const auto asset = *found;
    if (static_cast<int>(target_stage) <
        static_cast<int>(asset.assimilation_stage))
      throw std::runtime_error(
          "Tacit assimilation cannot move backward through this command.");
    const auto source_next_stage = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(asset.assimilation_stage)) +
        std::uint32_t{1});
    if (static_cast<std::int32_t>(target_stage) > source_next_stage)
      throw std::runtime_error(
          "Tacit assimilation must advance through adjacent stages so real "
          "translation/training steps are represented.");
    const auto &policy = storage_->catalog->runtime_policy();
    if (static_cast<int>(target_stage) >
            static_cast<int>(ResearchTacitAssimilationStage::access) &&
        asset.translation_context_quality <
            policy.minimum_translation_quality_beyond_access)
      throw std::runtime_error(
          "Translation/context quality is insufficient to advance foreign "
          "tacit knowledge beyond Access.");
    if (static_cast<int>(target_stage) >=
            static_cast<int>(ResearchTacitAssimilationStage::trained) &&
        asset.training_continuity <
            policy.minimum_training_continuity_for_trained)
      throw std::runtime_error(
          "Training continuity is insufficient to establish Trained foreign "
          "practice.");
    storage_->authority->set_tacit_asset(
        state, asset.asset_id, asset.asset_type_id, asset.scope_kind,
        asset.scope_ref, target_stage, asset.depth, asset.availability,
        asset.translation_context_quality, asset.training_continuity,
        asset.provenance, view(asset.context_id));
  }
}

ForeignTechnologyRecipientValueAssessment
AdaptiveResearchForeignTechnologyRuntime::evaluate_recipient_value(
    const ForeignTechnologyPackageRuntimeState &package,
    const ForeignTechnologyRecipientValueContext &context,
    bool holder_currently_unusable) const {
  const double values[] = {context.capability_novelty,
                           context.strategic_need,
                           context.expected_native_work_saved,
                           context.recipient_readiness,
                           context.recipient_operability_fit,
                           context.dependency_safety,
                           context.known_third_party_demand,
                           context.package_transferability,
                           context.scarcity_or_exclusivity,
                           context.dependency_risk,
                           context.hazard_risk};
  static constexpr std::string_view names[] = {"CapabilityNovelty",
                                               "StrategicNeed",
                                               "ExpectedNativeWorkSaved",
                                               "RecipientReadiness",
                                               "RecipientOperabilityFit",
                                               "DependencySafety",
                                               "KnownThirdPartyDemand",
                                               "PackageTransferability",
                                               "ScarcityOrExclusivity",
                                               "DependencyRisk",
                                               "HazardRisk"};
  for (std::size_t index = 0; index < std::size(values); ++index)
    validate_100(values[index], names[index]);
  const auto completeness = package.integrity * 100;
  const auto research = (context.capability_novelty + context.strategic_need +
                         context.expected_native_work_saved +
                         context.recipient_readiness + completeness) /
                        5;
  const auto operational = (context.recipient_operability_fit + completeness +
                            context.dependency_safety) /
                           3;
  const auto resale =
      (context.known_third_party_demand + context.package_transferability +
       context.scarcity_or_exclusivity) /
      3;
  const auto brokerage = holder_currently_unusable && resale > 0;
  const auto brokerage_best =
      holder_currently_unusable && resale >= std::max(research, operational);
  return {research,
          operational,
          resale,
          context.dependency_risk,
          context.hazard_risk,
          brokerage,
          brokerage_best
              ? "The current holder may be unable to use the technology, but "
                "legitimately known third-party demand makes brokerage "
                "valuable."
              : "Value is recipient-specific across research, operation, "
                "dependencies, hazards, scarcity and resale demand; no "
                "universal currency price is assigned by Research."};
}

} // namespace stellar::core
