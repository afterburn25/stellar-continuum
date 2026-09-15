#include <stellar/core/adaptive_research_catalog.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

[[noreturn]] void invalid(std::string_view source, std::string message) {
  throw AdaptiveResearchCatalogError("Adaptive Research catalog error in " +
                                     std::string(source) + ": " + message);
}

const Json &required(const Json &value, std::string_view name,
                     std::string_view source) {
  const auto found = value.find(name);
  if (found == value.end())
    invalid(source, "Missing required property '" + std::string(name) + "'.");
  return *found;
}

std::string required_string(const Json &value, std::string_view name,
                            std::string_view source) {
  const auto &item = required(value, name, source);
  if (!item.is_string() ||
      std::all_of(item.get_ref<const std::string &>().begin(),
                  item.get_ref<const std::string &>().end(),
                  [](unsigned char character) { return std::isspace(character) != 0; }))
    invalid(source, "Property '" + std::string(name) +
                        "' must be a non-empty string.");
  const auto result = item.get<std::string>();
  if (result.empty())
    invalid(source, "Property '" + std::string(name) +
                        "' must be a non-empty string.");
  return result;
}

int required_int(const Json &value, std::string_view name,
                 std::string_view source) {
  const auto &item = required(value, name, source);
  if (!item.is_number())
    throw std::logic_error("Property '" + std::string(name) +
                           "' has the wrong JSON type.");
  if (!item.is_number_integer())
    invalid(source, "Property '" + std::string(name) + "' must be an integer.");
  if (item.is_number_unsigned()) {
    const auto raw = item.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      invalid(source, "Property '" + std::string(name) + "' must be an integer.");
    return static_cast<int>(raw);
  }
  const auto raw = item.get<std::int64_t>();
  if (raw < std::numeric_limits<int>::min() ||
      raw > std::numeric_limits<int>::max())
    invalid(source, "Property '" + std::string(name) + "' must be an integer.");
  return static_cast<int>(raw);
}

double required_double(const Json &value, std::string_view name,
                       std::string_view source) {
  const auto &item = required(value, name, source);
  if (!item.is_number())
    invalid(source, "Property '" + std::string(name) + "' must be numeric.");
  return item.get<double>();
}

bool required_bool(const Json &value, std::string_view name,
                   std::string_view source) {
  const auto &item = required(value, name, source);
  if (!item.is_boolean())
    invalid(source, "Property '" + std::string(name) + "' must be boolean.");
  return item.get<bool>();
}

std::optional<int> optional_int(const Json &value, std::string_view name) {
  const auto found = value.find(name);
  if (found == value.end() || found->is_null())
    return std::nullopt;
  if (!found->is_number())
    throw std::logic_error("Property '" + std::string(name) +
                           "' has the wrong JSON type.");
  if (!found->is_number_integer())
    throw std::range_error("Property '" + std::string(name) +
                           "' is not an Int32 value.");
  if (found->is_number_unsigned()) {
    const auto raw = found->get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
      throw std::range_error("Property '" + std::string(name) +
                             "' is not an Int32 value.");
    return static_cast<int>(raw);
  }
  const auto raw = found->get<std::int64_t>();
  if (raw < std::numeric_limits<int>::min() ||
      raw > std::numeric_limits<int>::max())
    throw std::range_error("Property '" + std::string(name) +
                           "' is not an Int32 value.");
  return static_cast<int>(raw);
}

std::vector<std::string> string_array(const Json &value,
                                      std::string_view name) {
  const auto found = value.find(name);
  if (found == value.end() || found->is_null())
    return {};
  if (!found->is_array())
    throw AdaptiveResearchCatalogError("Property '" + std::string(name) +
                                       "' must be an array.");
  std::vector<std::string> result;
  result.reserve(found->size());
  for (const auto &entry : *found) {
    if (!entry.is_string())
      throw AdaptiveResearchCatalogError("Property '" + std::string(name) +
                                         "' contains non-string value.");
    result.push_back(entry.get<std::string>());
  }
  return result;
}

std::vector<ResearchNumberRequirement> number_dictionary(
    const Json &value, std::string_view name) {
  const auto found = value.find(name);
  if (found == value.end() || found->is_null())
    return {};
  if (!found->is_object())
    throw AdaptiveResearchCatalogError("Property '" + std::string(name) +
                                       "' must be an object.");
  std::vector<ResearchNumberRequirement> result;
  result.reserve(found->size());
  for (const auto &[id, amount] : found->items())
    result.push_back({id, amount.get<double>()});
  return result;
}

Json load_json(const std::filesystem::path &root,
               const std::filesystem::path &relative) {
  const auto path = root / relative;
  if (!std::filesystem::is_regular_file(path))
    throw AdaptiveResearchCatalogError(
        "Required Adaptive Research file not found: " + path.string());
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw AdaptiveResearchCatalogError("Unable to read Adaptive Research file: " +
                                       path.string());
  try {
    return Json::parse(stream);
  } catch (const nlohmann::json::exception &error) {
    throw AdaptiveResearchCatalogError("Malformed Adaptive Research JSON in " +
                                       path.string() + ": " + error.what());
  }
}

void validate_catalog_id(const Json &root, std::string_view expected,
                         std::string_view source) {
  const auto actual = required_string(root, "catalog_id", source);
  if (actual != expected)
    invalid(source, "catalog_id '" + actual + "' != '" +
                        std::string(expected) + "'.");
}

template <class Values>
bool contains_id(const Values &values, std::string_view id) {
  return std::ranges::any_of(values, [id](const auto &value) {
    if constexpr (requires { value.id; })
      return value.id == id;
    else
      return value == id;
  });
}

std::vector<std::string> id_set(const Json &array, std::string_view source) {
  std::vector<std::string> result;
  for (const auto &element : array) {
    auto id = required_string(element, "id", source);
    if (contains_id(result, id))
      invalid(source, "Duplicate id '" + id + "'.");
    result.push_back(std::move(id));
  }
  return result;
}

struct ComplexityDefault {
  int minimum_labs{};
  int recommended_labs{};
  double base_research_points{};
};
struct NodeOverride {
  std::optional<int> minimum_labs;
  std::optional<int> recommended_labs;
  std::vector<ResearchNumberRequirement> required_pressure;
  std::vector<ResearchNumberRequirement> required_pressure_any;
  std::vector<std::string> required_evidence;
};
struct EconomyResult {
  std::vector<std::pair<std::string, ComplexityDefault>> defaults;
  std::vector<std::pair<std::string, NodeOverride>> overrides;
  ResearchLabScaling lab_scaling;
  std::vector<DirectedResearchProgramStage> stages;
  double base_rp{};
  std::string starting_stage_id;
};

double round_to_even(double value) {
  if (!std::isfinite(value))
    return value;
  const auto lower = std::floor(value);
  const auto fraction = value - lower;
  if (fraction < 0.5)
    return lower;
  if (fraction > 0.5)
    return lower + 1.0;
  return std::fmod(std::abs(lower), 2.0) == 0.0 ? lower : lower + 1.0;
}

template <class T>
const T *find_pair_value(const std::vector<std::pair<std::string, T>> &values,
                         std::string_view id) {
  const auto found = std::ranges::find(values, id, &std::pair<std::string, T>::first);
  return found == values.end() ? nullptr : &found->second;
}

EconomyResult parse_economy(const Json &root,
                            std::span<const std::string> pressure_ids,
                            std::span<const std::string> evidence_ids) {
  EconomyResult result;
  const auto &defaults = required(root, "complexity_defaults",
                                  "research_economy.json");
  for (const auto &[id, value] : defaults.items()) {
    if (find_pair_value(result.defaults, id))
      invalid("research_economy.json", "Duplicate complexity '" + id + "'.");
    result.defaults.emplace_back(
        id, ComplexityDefault{
                required_int(value, "minimum_labs", "research_economy.json:" + id),
                required_int(value, "recommended_labs", "research_economy.json:" + id),
                required_double(value, "base_research_points",
                                "research_economy.json:" + id)});
  }
  if (const auto found = root.find("node_requirement_overrides");
      found != root.end()) {
    for (const auto &[id, value] : found->items()) {
      NodeOverride item{optional_int(value, "minimum_labs"),
                        optional_int(value, "recommended_labs"),
                        number_dictionary(value, "required_pressure"),
                        number_dictionary(value, "required_pressure_any"),
                        string_array(value, "required_evidence")};
      for (const auto &pressure : item.required_pressure)
        if (!contains_id(pressure_ids, pressure.id))
          invalid("research_economy.json", "Override '" + id +
                      "' references unknown pressure '" + pressure.id + "'.");
      for (const auto &pressure : item.required_pressure_any)
        if (!contains_id(pressure_ids, pressure.id))
          invalid("research_economy.json", "Override '" + id +
                      "' references unknown pressure '" + pressure.id + "'.");
      for (const auto &evidence : item.required_evidence)
        if (!contains_id(evidence_ids, evidence))
          invalid("research_economy.json", "Override '" + id +
                      "' references unknown evidence '" + evidence + "'.");
      if (find_pair_value(result.overrides, id))
        invalid("research_economy.json", "Duplicate override '" + id + "'.");
      result.overrides.emplace_back(id, std::move(item));
    }
  }
  const auto &scaling = required(root, "lab_scaling", "research_economy.json");
  result.lab_scaling = {
      required_double(scaling,
                      "at_or_below_recommended_labs_efficiency_per_lab",
                      "research_economy.json:lab_scaling"),
      required_double(scaling,
                      "above_recommended_to_2x_recommended_efficiency_per_extra_lab",
                      "research_economy.json:lab_scaling"),
      required_double(scaling,
                      "above_2x_recommended_efficiency_per_extra_lab",
                      "research_economy.json:lab_scaling")};
  const auto &concurrency = required(root, "directed_program_concurrency",
                                     "research_economy.json");
  for (const auto &value : required(concurrency, "progression",
                                    "research_economy.json:directed_program_concurrency")) {
    auto id = required_string(value, "id", "research_economy.json directed stage");
    if (contains_id(result.stages, id))
      throw std::invalid_argument(
          "An item with the same key has already been added. Key: " + id);
    std::optional<std::string> required_technology;
    if (const auto found = value.find("required_technology");
        found != value.end() && !found->is_null())
      required_technology = found->get<std::string>();
    const auto policy = value.contains("limit_policy")
                            ? value.at("limit_policy").get<std::string>()
                            : std::string{};
    result.stages.push_back({std::move(id),
                             optional_int(value, "directed_program_limit"),
                             policy == "lab_capacity_only",
                             std::move(required_technology)});
  }
  const auto &starting = required(concurrency, "starting_stage",
                                  "research_economy.json:directed_program_concurrency");
  result.starting_stage_id = required_string(
      starting, "id", "research_economy.json starting_stage");
  if (!contains_id(result.stages, result.starting_stage_id))
    invalid("research_economy.json", "Starting directed-program stage '" +
                result.starting_stage_id + "' is not in progression.");
  const auto &model = required(root, "research_model", "research_economy.json");
  const auto &points = required(model, "research_points",
                                "research_economy.json:research_model");
  result.base_rp = required_double(points, "base_rp_per_effective_lab_per_year",
                                   "research_economy.json:research_points");
  return result;
}

AdaptiveResearchNodeDefinition parse_node(const Json &value,
                                           std::string_view file,
                                           const EconomyResult &economy) {
  auto id = required_string(value, "id", file);
  auto complexity = required_string(value, "complexity", std::string(file) + ":" + id);
  const auto *defaults = find_pair_value(economy.defaults, complexity);
  if (!defaults)
    invalid(file, "node '" + id + "' references unknown complexity '" +
                      complexity + "'.");
  const auto &prerequisites = required(value, "prerequisites",
                                       std::string(file) + ":" + id);
  const auto &applicability = required(value, "applicability",
                                       std::string(file) + ":" + id);
  ResearchCapabilityRequirements capability{{}, {}, "civilization"};
  if (const auto found = value.find("capability_requirements");
      found != value.end()) {
    capability.all_of = string_array(*found, "all_of");
    capability.any_of = string_array(*found, "any_of");
    if (const auto context = found->find("context"); context != found->end())
      capability.context = context->is_null() ? "civilization"
                                              : context->get<std::string>();
  }
  const auto *override_value = find_pair_value(economy.overrides, id);
  const auto graph_depth = required_int(value, "graph_depth",
                                        std::string(file) + ":" + id);
  const auto base_points = round_to_even(
      defaults->base_research_points * (1.0 + 0.08 * graph_depth));
  return {
      id,
      required_string(value, "name", std::string(file) + ":" + id),
      required_string(value, "domain", std::string(file) + ":" + id),
      complexity, graph_depth,
      required_string(value, "solution_family", std::string(file) + ":" + id),
      string_array(value, "knowledge_fields"),
      string_array(value, "awareness_sources"),
      string_array(value, "pressure_affinities"),
      {string_array(prerequisites, "all_of"),
       string_array(prerequisites, "any_of")},
      {string_array(applicability, "requires_traits"),
       string_array(applicability, "requires_evidence")},
      std::move(capability), string_array(value, "capabilities"),
      required_bool(value, "is_hypothesis", std::string(file) + ":" + id),
      required_bool(value, "public_normal_research", std::string(file) + ":" + id),
      {base_points,
       override_value && override_value->minimum_labs
           ? *override_value->minimum_labs
           : defaults->minimum_labs,
       override_value && override_value->recommended_labs
           ? *override_value->recommended_labs
           : defaults->recommended_labs,
       override_value ? override_value->required_pressure
                      : std::vector<ResearchNumberRequirement>{},
       override_value ? override_value->required_pressure_any
                      : std::vector<ResearchNumberRequirement>{},
       override_value ? override_value->required_evidence
                      : std::vector<std::string>{}}};
}

template <class T>
const T *find_named(std::span<const T> values, std::string_view id) {
  const auto found = std::ranges::find(values, id, &T::id);
  return found == values.end() ? nullptr : std::addressof(*found);
}
template <class T>
const T *find_named(std::span<T> values, std::string_view id) {
  return find_named(std::span<const T>{values}, id);
}

void add_index(std::vector<AdaptiveResearchWakeIndexEntry> &index,
               std::string_view key, std::string_view node_id) {
  auto found = std::ranges::find(index, key, &AdaptiveResearchWakeIndexEntry::key);
  if (found == index.end()) {
    index.push_back({std::string(key), {std::string(node_id)}});
    return;
  }
  if (!contains_id(found->node_ids, node_id))
    found->node_ids.emplace_back(node_id);
}

} // namespace

struct AdaptiveResearchCatalog::Storage {
  struct TransparentHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view>{}(value);
    }
  };
  using Lookup = std::unordered_map<std::string, std::size_t, TransparentHash,
                                    std::equal_to<>>;
  using IdSet = std::unordered_set<std::string, TransparentHash, std::equal_to<>>;

  AdaptiveResearchCatalogMetadata metadata;
  std::vector<AdaptiveResearchNodeDefinition> nodes;
  std::vector<ResearchCapabilityDefinition> capabilities;
  std::vector<ResearchCapabilityImplication> implications;
  std::vector<std::string> pressure_ids;
  std::vector<std::string> trait_ids;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> field_ids;
  ResearchLabScaling lab_scaling;
  std::vector<DirectedResearchProgramStage> stages;
  std::vector<ResearchNodeGrant> demonstrated;
  std::vector<ResearchNodeGrant> mature;
  std::vector<ResearchDeploymentEventDefinition> deployment_events;
  std::vector<AdaptiveResearchWakeIndexEntry> children;
  std::vector<AdaptiveResearchWakeIndexEntry> by_pressure;
  std::vector<AdaptiveResearchWakeIndexEntry> by_evidence;
  std::vector<AdaptiveResearchWakeIndexEntry> by_trait;
  std::vector<AdaptiveResearchWakeIndexEntry> by_capability;
  Lookup node_lookup;
  Lookup capability_lookup;
  Lookup stage_lookup;
  Lookup children_lookup;
  Lookup pressure_index_lookup;
  Lookup evidence_index_lookup;
  Lookup trait_index_lookup;
  Lookup capability_index_lookup;
  IdSet pressure_lookup;
  IdSet trait_lookup;
  IdSet evidence_lookup;
  IdSet field_lookup;
};

AdaptiveResearchCatalogError::AdaptiveResearchCatalogError(std::string message)
    : std::runtime_error(std::move(message)) {}

double ResearchLabScaling::scale_assigned_labs(double assigned_labs,
                                                double recommended_labs) const {
  if (assigned_labs <= 0.0 || recommended_labs <= 0.0)
    return 0.0;
  const auto first = std::min(assigned_labs, recommended_labs);
  auto result = first * at_or_below_recommended_efficiency;
  if (assigned_labs > recommended_labs) {
    const auto second = std::min(assigned_labs - recommended_labs,
                                 recommended_labs);
    result += second * above_recommended_to_twice_efficiency;
  }
  if (assigned_labs > 2.0 * recommended_labs)
    result += (assigned_labs - 2.0 * recommended_labs) *
              above_twice_recommended_efficiency;
  return result;
}

AdaptiveResearchCatalog::AdaptiveResearchCatalog(Storage storage)
    : storage_(std::make_unique<Storage>(std::move(storage))) {}
AdaptiveResearchCatalog::AdaptiveResearchCatalog(AdaptiveResearchCatalog &&) noexcept = default;
AdaptiveResearchCatalog &AdaptiveResearchCatalog::operator=(AdaptiveResearchCatalog &&) noexcept = default;
AdaptiveResearchCatalog::~AdaptiveResearchCatalog() = default;

const AdaptiveResearchCatalogMetadata &AdaptiveResearchCatalog::metadata() const noexcept { return storage_->metadata; }
std::span<const AdaptiveResearchNodeDefinition> AdaptiveResearchCatalog::nodes() const noexcept { return storage_->nodes; }
const AdaptiveResearchNodeDefinition *AdaptiveResearchCatalog::find_node(std::string_view id) const noexcept {
  const auto found = storage_->node_lookup.find(id);
  return found == storage_->node_lookup.end() ? nullptr : &storage_->nodes[found->second];
}
const AdaptiveResearchNodeDefinition &AdaptiveResearchCatalog::get_node(std::string_view id) const {
  if (const auto *value = find_node(id)) return *value;
  throw std::out_of_range("Unknown Adaptive Research node '" + std::string(id) + "'.");
}
std::span<const ResearchCapabilityDefinition> AdaptiveResearchCatalog::capabilities() const noexcept { return storage_->capabilities; }
const ResearchCapabilityDefinition *AdaptiveResearchCatalog::find_capability(std::string_view id) const noexcept {
  const auto found = storage_->capability_lookup.find(id);
  return found == storage_->capability_lookup.end() ? nullptr : &storage_->capabilities[found->second];
}
std::span<const ResearchCapabilityImplication> AdaptiveResearchCatalog::capability_implications() const noexcept { return storage_->implications; }
std::span<const std::string> AdaptiveResearchCatalog::pressure_ids() const noexcept { return storage_->pressure_ids; }
std::span<const std::string> AdaptiveResearchCatalog::trait_ids() const noexcept { return storage_->trait_ids; }
std::span<const std::string> AdaptiveResearchCatalog::evidence_type_ids() const noexcept { return storage_->evidence_ids; }
std::span<const std::string> AdaptiveResearchCatalog::knowledge_field_ids() const noexcept { return storage_->field_ids; }
bool AdaptiveResearchCatalog::has_pressure(std::string_view id) const noexcept { return storage_->pressure_lookup.contains(id); }
bool AdaptiveResearchCatalog::has_trait(std::string_view id) const noexcept { return storage_->trait_lookup.contains(id); }
bool AdaptiveResearchCatalog::has_evidence_type(std::string_view id) const noexcept { return storage_->evidence_lookup.contains(id); }
bool AdaptiveResearchCatalog::has_knowledge_field(std::string_view id) const noexcept { return storage_->field_lookup.contains(id); }
const ResearchLabScaling &AdaptiveResearchCatalog::lab_scaling() const noexcept { return storage_->lab_scaling; }
std::span<const DirectedResearchProgramStage> AdaptiveResearchCatalog::directed_program_stages() const noexcept { return storage_->stages; }
const DirectedResearchProgramStage *AdaptiveResearchCatalog::find_directed_program_stage(std::string_view id) const noexcept {
  const auto found = storage_->stage_lookup.find(id);
  return found == storage_->stage_lookup.end() ? nullptr : &storage_->stages[found->second];
}
const DirectedResearchProgramStage &AdaptiveResearchCatalog::get_directed_program_stage(std::string_view id) const {
  if (const auto *value = find_directed_program_stage(id)) return *value;
  throw std::out_of_range("Unknown directed research stage '" + std::string(id) + "'.");
}
std::span<const ResearchNodeGrant> AdaptiveResearchCatalog::demonstrated_grants() const noexcept { return storage_->demonstrated; }
std::span<const ResearchNodeGrant> AdaptiveResearchCatalog::mature_grants() const noexcept { return storage_->mature; }
std::span<const ResearchDeploymentEventDefinition> AdaptiveResearchCatalog::deployment_events() const noexcept { return storage_->deployment_events; }
std::span<const AdaptiveResearchWakeIndexEntry> AdaptiveResearchCatalog::children_by_prerequisite() const noexcept { return storage_->children; }
std::span<const AdaptiveResearchWakeIndexEntry> AdaptiveResearchCatalog::nodes_by_pressure() const noexcept { return storage_->by_pressure; }
std::span<const AdaptiveResearchWakeIndexEntry> AdaptiveResearchCatalog::nodes_by_evidence() const noexcept { return storage_->by_evidence; }
std::span<const AdaptiveResearchWakeIndexEntry> AdaptiveResearchCatalog::nodes_by_trait() const noexcept { return storage_->by_trait; }
std::span<const AdaptiveResearchWakeIndexEntry> AdaptiveResearchCatalog::nodes_by_capability_requirement() const noexcept { return storage_->by_capability; }
std::span<const std::string> AdaptiveResearchCatalog::children_for(std::string_view id) const noexcept {
  const auto found = storage_->children_lookup.find(id);
  return found == storage_->children_lookup.end() ? std::span<const std::string>{} : storage_->children[found->second].node_ids;
}
std::span<const std::string> AdaptiveResearchCatalog::nodes_for_pressure(std::string_view id) const noexcept {
  const auto found = storage_->pressure_index_lookup.find(id);
  return found == storage_->pressure_index_lookup.end() ? std::span<const std::string>{} : storage_->by_pressure[found->second].node_ids;
}
std::span<const std::string> AdaptiveResearchCatalog::nodes_for_evidence(std::string_view id) const noexcept {
  const auto found = storage_->evidence_index_lookup.find(id);
  return found == storage_->evidence_index_lookup.end() ? std::span<const std::string>{} : storage_->by_evidence[found->second].node_ids;
}
std::span<const std::string> AdaptiveResearchCatalog::nodes_for_trait(std::string_view id) const noexcept {
  const auto found = storage_->trait_index_lookup.find(id);
  return found == storage_->trait_index_lookup.end() ? std::span<const std::string>{} : storage_->by_trait[found->second].node_ids;
}
std::span<const std::string> AdaptiveResearchCatalog::nodes_for_capability_requirement(std::string_view id) const noexcept {
  const auto found = storage_->capability_index_lookup.find(id);
  return found == storage_->capability_index_lookup.end() ? std::span<const std::string>{} : storage_->by_capability[found->second].node_ids;
}

AdaptiveResearchCatalog load_adaptive_research_catalog(
    const std::filesystem::path &root_path) {
  const auto supplied_path = root_path.string();
  if (supplied_path.empty() ||
      std::ranges::all_of(supplied_path, [](unsigned char character) {
        return std::isspace(character) != 0;
      }))
    throw std::invalid_argument("Value cannot be null or whitespace. (Parameter 'rootPath')");
  const auto root = std::filesystem::absolute(root_path).lexically_normal();
  if (!std::filesystem::is_directory(root))
    throw AdaptiveResearchCatalogError("Adaptive Research data directory not found: " + root.string());

  AdaptiveResearchCatalog::Storage storage;
  const auto index = load_json(root, "index.json");
  const auto catalog_id = required_string(index, "catalog_id", "index.json");
  const auto schema_version = required_int(index, "schema_version", "index.json");
  const auto declared_count = required_int(index, "node_count", "index.json");
  const auto &domain_files_json = required(index, "domain_files", "index.json");
  if (!domain_files_json.is_object())
    throw std::logic_error("Property 'domain_files' has the wrong JSON type.");
  std::vector<std::pair<std::string, std::string>> domain_files;
  for (const auto &[id, value] : domain_files_json.items()) {
    if (!value.is_string())
      invalid("index.json", "domain_files." + id + " must be a string");
    domain_files.emplace_back(id, value.get<std::string>());
  }
  const auto &domain_rows = required(index, "domains", "index.json");
  if (!domain_rows.is_array())
    throw std::logic_error("Property 'domains' has the wrong JSON type.");
  if (domain_rows.size() != domain_files.size())
    invalid("index.json", "domains count " + std::to_string(domain_rows.size()) +
                " != domain_files count " + std::to_string(domain_files.size()));

  const auto pressure = load_json(root, "pressure_dynamics.json");
  validate_catalog_id(pressure, catalog_id, "pressure_dynamics.json");
  for (const auto &[id, unused] : required(pressure, "rules", "pressure_dynamics.json").items()) {
    static_cast<void>(unused);
    storage.pressure_ids.push_back(id);
  }
  const auto traits = load_json(root, "applicability_traits.json");
  validate_catalog_id(traits, catalog_id, "applicability_traits.json");
  storage.trait_ids = id_set(required(traits, "traits", "applicability_traits.json"), "applicability_traits.json");
  const auto evidence = load_json(root, "evidence_types.json");
  validate_catalog_id(evidence, catalog_id, "evidence_types.json");
  storage.evidence_ids = id_set(required(evidence, "evidence_types", "evidence_types.json"), "evidence_types.json");
  const auto fields = load_json(root, "knowledge_fields.json");
  validate_catalog_id(fields, catalog_id, "knowledge_fields.json");
  storage.field_ids = id_set(required(fields, "fields", "knowledge_fields.json"), "knowledge_fields.json");

  const auto capability = load_json(root, "capability_model.json");
  validate_catalog_id(capability, catalog_id, "capability_model.json");
  for (const auto &value : required(capability, "cross_lineage_capabilities", "capability_model.json")) {
    auto id = required_string(value, "id", "capability_model.json");
    auto scope_text = required_string(value, "scope", "capability_model.json:" + id);
    ResearchCapabilityScope scope;
    if (scope_text == "civilization") scope = ResearchCapabilityScope::civilization;
    else if (scope_text == "population_or_species") scope = ResearchCapabilityScope::population_or_species;
    else if (scope_text == "colony_or_installation") scope = ResearchCapabilityScope::colony_or_installation;
    else invalid("capability_model.json", "Unknown scope '" + scope_text + "' for capability '" + id + "'.");
    if (contains_id(storage.capabilities, id))
      invalid("capability_model.json", "Duplicate capability '" + id + "'.");
    storage.capabilities.push_back({id, required_string(value, "name", "capability_model.json:" + id), scope});
  }
  if (const auto found = capability.find("implications"); found != capability.end()) {
    for (const auto &value : *found) {
      const auto from = required_string(value, "from", "capability_model.json implication");
      const auto to = required_string(value, "to", "capability_model.json implication");
      if (!find_named(std::span{storage.capabilities}, from) ||
          !find_named(std::span{storage.capabilities}, to))
        invalid("capability_model.json", "Implication '" + from + "' -> '" + to + "' references unknown capability.");
      storage.implications.push_back({from, to, value.contains("preserve_target_context") && value.at("preserve_target_context").get<bool>()});
    }
  }

  const auto economy_json = load_json(root, "research_economy.json");
  validate_catalog_id(economy_json, catalog_id, "research_economy.json");
  auto economy = parse_economy(economy_json, storage.pressure_ids, storage.evidence_ids);
  storage.lab_scaling = economy.lab_scaling;
  storage.stages = economy.stages;

  for (const auto &domain : domain_rows) {
    const auto domain_id = required_string(domain, "id", "index.json domain");
    const auto *file = find_pair_value(domain_files, domain_id);
    if (!file)
      invalid("index.json", "No domain file mapping for '" + domain_id + "'.");
    const auto domain_json = load_json(root, *file);
    validate_catalog_id(domain_json, catalog_id, *file);
    const auto file_domain = required_string(domain_json, "domain", *file);
    if (file_domain != domain_id)
      invalid(*file, "domain '" + file_domain + "' does not match index id '" + domain_id + "'.");
    for (const auto &value : required(domain_json, "nodes", *file)) {
      auto node = parse_node(value, *file, economy);
      if (node.domain_id != domain_id)
        invalid(*file, "node '" + node.id + "' domain '" + node.domain_id + "' does not match file domain '" + domain_id + "'.");
      if (find_named(std::span{storage.nodes}, node.id))
        invalid(*file, "Duplicate research node id '" + node.id + "'.");
      storage.nodes.push_back(std::move(node));
    }
  }
  if (static_cast<int>(storage.nodes.size()) != declared_count)
    invalid("index.json", "Declared node_count " + std::to_string(declared_count) +
                " != loaded node count " + std::to_string(storage.nodes.size()) + ".");

  for (const auto &node : storage.nodes) {
    for (const auto &id : node.prerequisites.all_of)
      if (!find_named(std::span{storage.nodes}, id)) invalid(node.id, "Unknown prerequisite '" + id + "'.");
    for (const auto &id : node.prerequisites.any_of)
      if (!find_named(std::span{storage.nodes}, id)) invalid(node.id, "Unknown prerequisite '" + id + "'.");
    for (const auto &id : node.pressure_affinities)
      if (!contains_id(storage.pressure_ids, id)) invalid(node.id, "Unknown pressure '" + id + "'.");
    for (const auto &item : node.project_requirements.required_pressure)
      if (!contains_id(storage.pressure_ids, item.id)) invalid(node.id, "Unknown pressure '" + item.id + "'.");
    for (const auto &item : node.project_requirements.required_pressure_any)
      if (!contains_id(storage.pressure_ids, item.id)) invalid(node.id, "Unknown pressure '" + item.id + "'.");
    for (const auto &id : node.applicability.traits)
      if (!contains_id(storage.trait_ids, id)) invalid(node.id, "Unknown applicability trait '" + id + "'.");
    for (const auto &id : node.applicability.evidence_types)
      if (!contains_id(storage.evidence_ids, id)) invalid(node.id, "Unknown evidence type '" + id + "'.");
    for (const auto &id : node.project_requirements.required_evidence)
      if (!contains_id(storage.evidence_ids, id)) invalid(node.id, "Unknown evidence type '" + id + "'.");
    for (const auto &id : node.knowledge_fields)
      if (!contains_id(storage.field_ids, id)) invalid(node.id, "Unknown knowledge field '" + id + "'.");
    for (const auto &id : node.capability_requirements.all_of)
      if (!find_named(std::span{storage.capabilities}, id)) invalid(node.id, "Unknown cross-lineage capability requirement '" + id + "'.");
    for (const auto &id : node.capability_requirements.any_of)
      if (!find_named(std::span{storage.capabilities}, id)) invalid(node.id, "Unknown cross-lineage capability requirement '" + id + "'.");
  }

  const auto grants = load_json(root, "technology_grants.json");
  validate_catalog_id(grants, catalog_id, "technology_grants.json");
  const auto parse_grants = [&](std::string_view section,
                                std::vector<ResearchNodeGrant> &destination) {
    const auto found = grants.find(section);
    if (found == grants.end()) return;
    for (const auto &[node_id, value] : found->items()) {
      if (!find_named(std::span{storage.nodes}, node_id))
        invalid("technology_grants.json", std::string(section) + " references unknown node '" + node_id + "'.");
      auto capability_ids = string_array(value, "grant_capabilities");
      for (const auto &id : capability_ids)
        if (!find_named(std::span{storage.capabilities}, id)) invalid("technology_grants.json", "Node '" + node_id + "' grants unknown capability '" + id + "'.");
      auto trait_ids = string_array(value, "add_civilization_traits");
      for (const auto &id : trait_ids)
        if (!contains_id(storage.trait_ids, id)) invalid("technology_grants.json", "Node '" + node_id + "' grants unknown trait '" + id + "'.");
      std::optional<std::string> stage;
      if (const auto stage_value = value.find("set_research_capacity_stage"); stage_value != value.end())
        stage = stage_value->is_null() ? std::nullopt : std::optional{stage_value->get<std::string>()};
      if (stage && !find_named(std::span{storage.stages}, *stage))
        invalid("technology_grants.json", "Node '" + node_id + "' grants unknown directed-program stage '" + *stage + "'.");
      destination.push_back({node_id, {std::move(capability_ids), std::move(trait_ids), std::move(stage), string_array(value, "unlock_deployment_events")}});
    }
  };
  parse_grants("on_demonstrated", storage.demonstrated);
  parse_grants("on_mature", storage.mature);
  if (const auto events = grants.find("deployment_events"); events != grants.end()) {
    for (const auto &[id, value] : events->items()) {
      auto required_nodes = string_array(value, "requires_any_mature_technology");
      for (const auto &node_id : required_nodes)
        if (!find_named(std::span{storage.nodes}, node_id)) invalid("technology_grants.json", "Deployment event '" + id + "' references unknown node '" + node_id + "'.");
      auto event_traits = string_array(value, "add_civilization_traits");
      for (const auto &trait_id : event_traits)
        if (!contains_id(storage.trait_ids, trait_id)) invalid("technology_grants.json", "Deployment event '" + id + "' references unknown trait '" + trait_id + "'.");
      storage.deployment_events.push_back({id, std::move(required_nodes), std::move(event_traits)});
    }
  }

  for (const auto &node : storage.nodes) {
    for (const auto &id : node.prerequisites.all_of) add_index(storage.children, id, node.id);
    for (const auto &id : node.prerequisites.any_of) add_index(storage.children, id, node.id);
    std::vector<std::string> pressures = node.pressure_affinities;
    for (const auto &item : node.project_requirements.required_pressure) pressures.push_back(item.id);
    for (const auto &item : node.project_requirements.required_pressure_any) pressures.push_back(item.id);
    for (const auto &id : pressures) add_index(storage.by_pressure, id, node.id);
    std::vector<std::string> evidence_index = node.applicability.evidence_types;
    evidence_index.insert(evidence_index.end(), node.project_requirements.required_evidence.begin(), node.project_requirements.required_evidence.end());
    for (const auto &id : evidence_index) add_index(storage.by_evidence, id, node.id);
    for (const auto &id : node.applicability.traits) add_index(storage.by_trait, id, node.id);
    for (const auto &id : node.capability_requirements.all_of) add_index(storage.by_capability, id, node.id);
    for (const auto &id : node.capability_requirements.any_of) add_index(storage.by_capability, id, node.id);
  }
  const auto sort_index = [](auto &index) {
    for (auto &entry : index)
      std::ranges::sort(entry.node_ids);
  };
  sort_index(storage.children);
  sort_index(storage.by_pressure);
  sort_index(storage.by_evidence);
  sort_index(storage.by_trait);
  sort_index(storage.by_capability);

  const auto populate_lookup = [](const auto &values, auto &lookup) {
    for (std::size_t index = 0; index < values.size(); ++index)
      lookup.emplace(values[index].id, index);
  };
  const auto populate_index_lookup = [](const auto &values, auto &lookup) {
    for (std::size_t index = 0; index < values.size(); ++index)
      lookup.emplace(values[index].key, index);
  };
  populate_lookup(storage.nodes, storage.node_lookup);
  populate_lookup(storage.capabilities, storage.capability_lookup);
  populate_lookup(storage.stages, storage.stage_lookup);
  populate_index_lookup(storage.children, storage.children_lookup);
  populate_index_lookup(storage.by_pressure, storage.pressure_index_lookup);
  populate_index_lookup(storage.by_evidence, storage.evidence_index_lookup);
  populate_index_lookup(storage.by_trait, storage.trait_index_lookup);
  populate_index_lookup(storage.by_capability, storage.capability_index_lookup);
  storage.pressure_lookup.insert(storage.pressure_ids.begin(), storage.pressure_ids.end());
  storage.trait_lookup.insert(storage.trait_ids.begin(), storage.trait_ids.end());
  storage.evidence_lookup.insert(storage.evidence_ids.begin(), storage.evidence_ids.end());
  storage.field_lookup.insert(storage.field_ids.begin(), storage.field_ids.end());

  storage.metadata = {schema_version, catalog_id, declared_count,
                      static_cast<int>(domain_rows.size()),
                      static_cast<int>(storage.pressure_ids.size()),
                      static_cast<int>(storage.trait_ids.size()),
                      static_cast<int>(storage.evidence_ids.size()),
                      static_cast<int>(storage.field_ids.size()),
                      static_cast<int>(storage.capabilities.size()),
                      economy.base_rp, economy.starting_stage_id};
  return AdaptiveResearchCatalog(std::move(storage));
}
} // namespace stellar::core
