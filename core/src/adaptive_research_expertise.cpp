#include <stellar/core/adaptive_research_expertise.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {

using Json = nlohmann::ordered_json;

[[noreturn]] void fail(std::string message) {
  throw AdaptiveResearchExpertiseCatalogError(std::move(message));
}

Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input) {
    fail("Unable to read Adaptive Research file: " + path.string());
  }
  try {
    return Json::parse(input);
  } catch (const Json::exception &) {
    fail("Malformed Adaptive Research JSON in " + path.string());
  }
}

std::string json_kind(const Json &value) {
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

void require_kind(const Json &value, std::string_view expected) {
  const auto matches = (expected == "Object" && value.is_object()) ||
                       (expected == "Array" && value.is_array()) ||
                       (expected == "String" && value.is_string()) ||
                       (expected == "Number" && value.is_number());
  if (!matches) {
    throw std::logic_error(
        "The requested operation requires an element of type '" +
        std::string(expected) + "', but the target element has type '" +
        json_kind(value) + "'.");
  }
}

const Json &property(const Json &value, std::string_view name,
                     const std::string &source) {
  (void)source;
  require_kind(value, "Object");
  const auto found = value.find(name);
  if (found == value.end()) {
    throw std::out_of_range("The given key was not present in the dictionary.");
  }
  return *found;
}

std::string required_string(const Json &value, std::string_view name,
                            const std::string &source) {
  require_kind(value, "Object");
  const auto found = value.find(name);
  if (found == value.end() || !found->is_string()) {
    fail(source + " is missing string '" + std::string(name) + "'.");
  }
  return found->get<std::string>();
}

double required_double(const Json &value, std::string_view name,
                       const std::string &source) {
  const auto &item = property(value, name, source);
  require_kind(item, "Number");
  return item.get<double>();
}

std::vector<std::string> strings(const Json &value, std::string_view name) {
  require_kind(value, "Object");
  const auto found = value.find(name);
  if (found == value.end()) {
    return {};
  }
  require_kind(*found, "Array");
  std::vector<std::string> result;
  for (const auto &item : *found) {
    if (item.is_null()) {
      fail(std::string(name) + " contains null.");
    }
    require_kind(item, "String");
    result.push_back(item.get<std::string>());
  }
  return result;
}

void require_array(const Json &value, const std::string &) {
  require_kind(value, "Array");
}

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

void validate_catalog_id(const Json &root,
                         const AdaptiveResearchCatalog &catalog,
                         const std::string &source) {
  const auto actual = required_string(root, "catalog_id", source);
  if (actual != catalog.metadata().catalog_id) {
    fail(source + " catalog_id '" + actual + "' does not match '" +
         catalog.metadata().catalog_id + "'.");
  }
}

ResearchCompetenceComponent parse_component(const std::string &value) {
  if (value == "theoretical") {
    return ResearchCompetenceComponent::theoretical;
  }
  if (value == "experimental") {
    return ResearchCompetenceComponent::experimental;
  }
  if (value == "engineering") {
    return ResearchCompetenceComponent::engineering;
  }
  fail("Unknown competence component '" + value + "'.");
}

ResearchCompetenceVector vector(const Json &value, const std::string &source) {
  return {
      .theoretical = required_double(value, "theoretical", source),
      .experimental = required_double(value, "experimental", source),
      .engineering = required_double(value, "engineering", source),
  };
}

ResearchStageCompetenceWeights stage_weights(const Json &value,
                                             const std::string &source) {
  return {
      .theoretical = required_double(value, "theoretical", source),
      .experimental = required_double(value, "experimental", source),
      .engineering = required_double(value, "engineering", source),
  };
}

void validate_weight_sum(const ResearchStageCompetenceWeightEntry &entry) {
  const auto sum = entry.weights.theoretical + entry.weights.experimental +
                   entry.weights.engineering;
  if (std::abs(sum - 1.0) > 0.000001) {
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), sum);
    std::string text(buffer, converted.ptr);
    const auto stage =
        entry.stage == ResearchMaturity::experimental   ? "Experimental"
        : entry.stage == ResearchMaturity::demonstrated ? "Demonstrated"
                                                        : "Engineering";
    fail("Competence stage weights for " + std::string(stage) + " sum to " +
         text + ", not 1.0.");
  }
}

} // namespace

struct AdaptiveResearchExpertiseCatalog::Storage {
  std::vector<ResearchKnowledgeFieldDefinition> fields;
  std::unordered_map<std::string, std::size_t, TransparentHash,
                     TransparentEqual>
      field_index;
  std::vector<ResearchStageCompetenceWeightEntry> stage_weights;
  ResearchReadinessComponentWeights readiness_weights;
  std::vector<ResearchTacitAssetTypeDefinition> tacit_types;
  std::vector<ResearchInstitutionSpecializationDefinition> institutions;
  std::unordered_map<std::string, std::size_t, TransparentHash,
                     TransparentEqual>
      institution_index;
  ResearchExpertiseRuntimePolicy runtime_policy;
};

AdaptiveResearchExpertiseCatalogError::AdaptiveResearchExpertiseCatalogError(
    std::string message)
    : std::runtime_error(std::move(message)) {}

AdaptiveResearchExpertiseCatalog::AdaptiveResearchExpertiseCatalog(
    Storage storage)
    : storage_(std::make_unique<Storage>(std::move(storage))) {}
AdaptiveResearchExpertiseCatalog::AdaptiveResearchExpertiseCatalog(
    AdaptiveResearchExpertiseCatalog &&) noexcept = default;
AdaptiveResearchExpertiseCatalog &AdaptiveResearchExpertiseCatalog::operator=(
    AdaptiveResearchExpertiseCatalog &&) noexcept = default;
AdaptiveResearchExpertiseCatalog::~AdaptiveResearchExpertiseCatalog() = default;

std::span<const ResearchKnowledgeFieldDefinition>
AdaptiveResearchExpertiseCatalog::fields() const noexcept {
  return storage_->fields;
}
std::span<const ResearchStageCompetenceWeightEntry>
AdaptiveResearchExpertiseCatalog::stage_weights() const noexcept {
  return storage_->stage_weights;
}
const ResearchReadinessComponentWeights &
AdaptiveResearchExpertiseCatalog::readiness_weights() const noexcept {
  return storage_->readiness_weights;
}
std::span<const ResearchTacitAssetTypeDefinition>
AdaptiveResearchExpertiseCatalog::tacit_asset_types() const noexcept {
  return storage_->tacit_types;
}
std::span<const ResearchInstitutionSpecializationDefinition>
AdaptiveResearchExpertiseCatalog::institutions() const noexcept {
  return storage_->institutions;
}
const ResearchExpertiseRuntimePolicy &
AdaptiveResearchExpertiseCatalog::runtime_policy() const noexcept {
  return storage_->runtime_policy;
}
const ResearchKnowledgeFieldDefinition &
AdaptiveResearchExpertiseCatalog::get_field(std::string_view field_id) const {
  const auto found = storage_->field_index.find(field_id);
  if (found == storage_->field_index.end()) {
    throw std::out_of_range("Unknown research knowledge field '" +
                            std::string(field_id) + "'.");
  }
  return storage_->fields[found->second];
}
const ResearchInstitutionSpecializationDefinition &
AdaptiveResearchExpertiseCatalog::get_institution(
    std::string_view institution_archetype_id) const {
  const auto found = storage_->institution_index.find(institution_archetype_id);
  if (found == storage_->institution_index.end()) {
    throw std::out_of_range("The given key '" +
                            std::string(institution_archetype_id) +
                            "' was not present in the dictionary.");
  }
  return storage_->institutions[found->second];
}

double
ResearchCompetenceVector::get(ResearchCompetenceComponent component) const {
  switch (component) {
  case ResearchCompetenceComponent::theoretical:
    return theoretical;
  case ResearchCompetenceComponent::experimental:
    return experimental;
  case ResearchCompetenceComponent::engineering:
    return engineering;
  }
  throw std::out_of_range(
      "Specified argument was out of the range of valid values. (Parameter "
      "'component')");
}

AdaptiveResearchExpertiseCatalog load_adaptive_research_expertise_catalog(
    const std::filesystem::path &root, const AdaptiveResearchCatalog &catalog,
    const AdaptiveResearchFacilityCatalog &facilities) {
  AdaptiveResearchExpertiseCatalog::Storage storage;

  const auto fields_json = read_json(root / "knowledge_fields.json");
  validate_catalog_id(fields_json, catalog, "knowledge_fields.json");
  const auto &field_items =
      property(fields_json, "fields", "knowledge_fields.json");
  require_array(field_items, "knowledge_fields.json.fields");
  for (const auto &item : field_items) {
    auto id = required_string(item, "id", "knowledge_fields.json");
    ResearchKnowledgeFieldDefinition definition{
        id, required_string(item, "name", id),
        required_string(item, "family", id), strings(item, "related_fields")};
    if (!storage.field_index.emplace(id, storage.fields.size()).second) {
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " + id);
    }
    storage.fields.push_back(std::move(definition));
  }
  if (storage.fields.size() != catalog.metadata().knowledge_field_count) {
    fail("Runtime expertise field catalog does not match the Adaptive Research "
         "knowledge-field catalog.");
  }
  for (const auto &field : storage.fields) {
    if (!std::ranges::contains(catalog.knowledge_field_ids(), field.id)) {
      fail("Runtime expertise field catalog does not match the Adaptive "
           "Research knowledge-field catalog.");
    }
  }
  for (const auto &field : storage.fields) {
    for (const auto &related : field.related_field_ids) {
      if (!storage.field_index.contains(related)) {
        fail("Knowledge field '" + field.id +
             "' references unknown related field '" + related + "'.");
      }
    }
  }

  const auto competence = read_json(root / "research_competence_model.json");
  validate_catalog_id(competence, catalog, "research_competence_model.json");
  const auto &stage_root = property(
      property(property(competence, "project_field_readiness",
                        "research_competence_model.json"),
               "stage_component_weights", "research_competence_model.json"),
      "experimental", "research_competence_model.json");
  const auto &all_stage_root =
      property(property(competence, "project_field_readiness",
                        "research_competence_model.json"),
               "stage_component_weights", "research_competence_model.json");
  storage.stage_weights = {
      {ResearchMaturity::experimental,
       stage_weights(stage_root, "experimental")},
      {ResearchMaturity::demonstrated,
       stage_weights(property(all_stage_root, "demonstrated", "demonstrated"),
                     "demonstrated")},
      {ResearchMaturity::engineering,
       stage_weights(property(all_stage_root, "engineering", "engineering"),
                     "engineering")},
  };
  for (const auto &entry : storage.stage_weights) {
    validate_weight_sum(entry);
  }
  const auto &inputs = property(property(competence, "project_readiness",
                                         "research_competence_model.json"),
                                "inputs", "research_competence_model.json");
  const auto input_weight = [&](std::string_view name) {
    return required_double(property(inputs, name, "project_readiness.inputs"),
                           "weight", std::string(name));
  };
  storage.readiness_weights = {
      input_weight("field_competence"), input_weight("facility_readiness"),
      input_weight("evidence_readiness"), input_weight("tacit_expertise")};
  const auto readiness_sum = storage.readiness_weights.field_competence +
                             storage.readiness_weights.facility_readiness +
                             storage.readiness_weights.evidence_readiness +
                             storage.readiness_weights.tacit_expertise;
  if (std::abs(readiness_sum - 1.0) > 0.000001) {
    char buffer[64];
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), readiness_sum);
    fail("Project readiness component weights sum to " +
         std::string(buffer, converted.ptr) + ", not 1.0.");
  }

  const auto tacit = read_json(root / "tacit_knowledge_model.json");
  validate_catalog_id(tacit, catalog, "tacit_knowledge_model.json");
  std::unordered_set<std::string> tacit_ids;
  const auto &tacit_items =
      property(tacit, "knowledge_asset_types", "tacit_knowledge_model.json");
  require_array(tacit_items,
                "tacit_knowledge_model.json.knowledge_asset_types");
  for (const auto &item : tacit_items) {
    auto id = required_string(item, "id", "tacit_knowledge_model.json");
    std::vector<ResearchCompetenceComponent> components;
    for (const auto &name : strings(item, "primary_support")) {
      const auto component = parse_component(name);
      if (!std::ranges::contains(components, component)) {
        components.push_back(component);
      }
    }
    if (!tacit_ids.insert(id).second) {
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " + id);
    }
    storage.tacit_types.push_back({std::move(id), std::move(components)});
  }

  const auto facility_index = read_json(root / "research_facility_index.json");
  validate_catalog_id(facility_index, catalog, "research_facility_index.json");
  const auto &facility_files = property(
      facility_index, "facility_catalog_files", "research_facility_index.json");
  require_array(facility_files,
                "research_facility_index.json.facility_catalog_files");
  for (const auto &file_value : facility_files) {
    if (!file_value.is_string()) {
      if (file_value.is_null()) {
        fail("Facility catalog filename cannot be null.");
      }
      require_kind(file_value, "String");
    }
    const auto file_name = file_value.get<std::string>();
    const auto document = read_json(root / file_name);
    validate_catalog_id(document, catalog, file_name);
    const auto &institution_items =
        property(document, "institution_archetypes", file_name);
    require_array(institution_items, file_name + ".institution_archetypes");
    for (const auto &item : institution_items) {
      auto id = required_string(item, "id", file_name);
      auto specialized = strings(item, "specialized_fields");
      for (const auto &field_id : specialized) {
        if (!storage.field_index.contains(field_id)) {
          fail("Institution '" + id +
               "' references unknown specialized field '" + field_id + "'.");
        }
      }
      auto capabilities = strings(item, "facility_capabilities");
      const auto *base = facilities.find_institution(id);
      if (base == nullptr) {
        fail("Expertise institution '" + id +
             "' is missing from the research facility catalog.");
      }
      if (storage.institution_index.contains(id)) {
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: " + id);
      }
      storage.institution_index.emplace(id, storage.institutions.size());
      storage.institutions.push_back({std::move(id), base->effective_lab_units,
                                      std::move(specialized),
                                      std::move(capabilities)});
    }
  }
  if (storage.institutions.size() != facilities.institutions().size()) {
    fail("Expertise institution count does not match research facility "
         "institution count.");
  }

  const auto runtime =
      read_json(root / "research_runtime_expertise_policy.json");
  validate_catalog_id(runtime, catalog,
                      "research_runtime_expertise_policy.json");
  const auto &gains =
      property(runtime, "competence_practice_gain_per_completed_stage",
               "research_runtime_expertise_policy.json");
  storage.runtime_policy.stage_practice_gain = {
      {ResearchMaturity::experimental,
       vector(property(gains, "experimental", "practice gains"),
              "experimental")},
      {ResearchMaturity::demonstrated,
       vector(property(gains, "demonstrated", "practice gains"),
              "demonstrated")},
      {ResearchMaturity::engineering,
       vector(property(gains, "engineering", "practice gains"), "engineering")},
  };
  const auto &gain_rules = property(runtime, "competence_gain_rules",
                                    "research_runtime_expertise_policy.json");
  const auto &atrophy = property(
      runtime, "annual_active_competence_atrophy_toward_preservation_floor",
      "research_runtime_expertise_policy.json");
  const auto &facility = property(runtime, "facility_readiness",
                                  "research_runtime_expertise_policy.json");
  const auto &factors = property(runtime, "tacit_assimilation_factors",
                                 "research_runtime_expertise_policy.json");
  const auto factor = [&](std::string_view name) {
    const auto found = factors.find(name);
    if (found == factors.end()) {
      throw std::out_of_range(
          "The given key was not present in the dictionary.");
    }
    require_kind(*found, "Number");
    return found->get<double>();
  };
  auto tacit_factors = std::vector<ResearchTacitAssimilationFactor>{
      {ResearchTacitAssimilationStage::access, factor("access")},
      {ResearchTacitAssimilationStage::interpreted, factor("interpreted")},
      {ResearchTacitAssimilationStage::codified, factor("codified")},
      {ResearchTacitAssimilationStage::trained, factor("trained")},
      {ResearchTacitAssimilationStage::native_practice,
       factor("native_practice")},
  };
  const auto &preservation = property(runtime, "preservation_floor",
                                      "research_runtime_expertise_policy.json");
  storage.runtime_policy.related_field_transfer_fraction = required_double(
      gain_rules, "related_field_transfer_fraction", "competence_gain_rules");
  storage.runtime_policy.minimum_gain_factor = required_double(
      gain_rules, "minimum_gain_factor", "competence_gain_rules");
  storage.runtime_policy.annual_atrophy_rates =
      vector(atrophy, "annual atrophy");
  storage.runtime_policy.atrophy_grace_years =
      required_double(property(runtime, "atrophy_rules",
                               "research_runtime_expertise_policy.json"),
                      "only_after_years_without_meaningful_component_activity",
                      "atrophy_rules");
  storage.runtime_policy.general_lab_matching_factor = required_double(
      facility, "general_lab_matching_factor", "facility_readiness");
  storage.runtime_policy.specialized_matching_factor = required_double(
      facility, "specialized_matching_factor", "facility_readiness");
  storage.runtime_policy.nonmatching_specialist_factor = required_double(
      facility, "nonmatching_specialist_factor", "facility_readiness");
  storage.runtime_policy.tacit_assimilation_factors = std::move(tacit_factors);
  storage.runtime_policy.tacit_best_weight = 0.70;
  storage.runtime_policy.tacit_mean_weight = 0.30;
  storage.runtime_policy.preservation_floor_fraction =
      required_double(preservation, "floor_fraction", "preservation_floor");
  storage.runtime_policy.established_knowledge_theoretical_floor_fraction =
      required_double(
          preservation,
          "established_knowledge_theoretical_floor_fraction_of_historical_peak",
          "preservation_floor");

  return AdaptiveResearchExpertiseCatalog(std::move(storage));
}

} // namespace stellar::core
