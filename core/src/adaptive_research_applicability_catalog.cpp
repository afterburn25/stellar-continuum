#include <stellar/core/adaptive_research_applicability_catalog.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {
using Json = nlohmann::ordered_json;

struct StringHash {
  using is_transparent = void;
  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
    std::size_t hash = 1469598103934665603ull;
    for (const unsigned char character : value) {
      hash ^= character;
      hash *= 1099511628211ull;
    }
    return hash;
  }
  [[nodiscard]] std::size_t operator()(const std::string &value) const noexcept {
    return (*this)(std::string_view{value});
  }
};
struct StringEqual {
  using is_transparent = void;
  [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
    return left == right;
  }
};

[[noreturn]] void semantic(std::string message) {
  throw AdaptiveResearchApplicabilityCatalogError(std::move(message));
}

std::string required_string(const Json &element, std::string_view name) {
  const auto found = element.find(name);
  if (found == element.end() || !found->is_string())
    semantic("Missing required string '" + std::string(name) + "'.");
  return found->get<std::string>();
}
} // namespace

struct AdaptiveResearchApplicabilityCatalog::Storage {
  std::vector<ResearchApplicabilityTraitDefinition> traits;
  std::unordered_map<std::string, std::size_t, StringHash, StringEqual> lookup;
};

AdaptiveResearchApplicabilityCatalogError::AdaptiveResearchApplicabilityCatalogError(
    std::string message) : std::runtime_error(std::move(message)) {}
AdaptiveResearchApplicabilityCatalog::AdaptiveResearchApplicabilityCatalog(Storage storage)
    : storage_(std::make_unique<Storage>(std::move(storage))) {}
AdaptiveResearchApplicabilityCatalog::AdaptiveResearchApplicabilityCatalog(
    AdaptiveResearchApplicabilityCatalog &&) noexcept = default;
AdaptiveResearchApplicabilityCatalog &AdaptiveResearchApplicabilityCatalog::operator=(
    AdaptiveResearchApplicabilityCatalog &&) noexcept = default;
AdaptiveResearchApplicabilityCatalog::~AdaptiveResearchApplicabilityCatalog() = default;

std::span<const ResearchApplicabilityTraitDefinition>
AdaptiveResearchApplicabilityCatalog::traits() const noexcept { return storage_->traits; }
const ResearchApplicabilityTraitDefinition *AdaptiveResearchApplicabilityCatalog::find_trait(
    std::string_view trait_id) const noexcept {
  const auto found = storage_->lookup.find(trait_id);
  return found == storage_->lookup.end() ? nullptr : &storage_->traits[found->second];
}
const ResearchApplicabilityTraitDefinition &AdaptiveResearchApplicabilityCatalog::get_trait(
    std::string_view trait_id) const {
  if (const auto *definition = find_trait(trait_id)) return *definition;
  throw std::out_of_range("Unknown Adaptive Research applicability trait '" +
                          std::string(trait_id) + "'.");
}

AdaptiveResearchApplicabilityCatalog load_adaptive_research_applicability_catalog(
    const std::filesystem::path &root_path, const AdaptiveResearchCatalog &catalog) {
  const auto path = std::filesystem::absolute(root_path) / "applicability_traits.json";
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::ios_base::failure("Unable to read applicability traits file: " + path.string());
  Json root;
  try {
    stream >> root;
  } catch (const nlohmann::json::parse_error &) {
    throw; // parser boundary: this is intentionally separate from semantic validation.
  }
  const auto &catalog_id_element = root.at("catalog_id");
  if (catalog_id_element.is_null() ||
      catalog.metadata().catalog_id != catalog_id_element.get<std::string>())
    semantic("applicability_traits.json catalog_id does not match the loaded research catalog.");
  const auto &trait_rows = root.at("traits").get_ref<const Json::array_t &>();

  AdaptiveResearchApplicabilityCatalog::Storage storage;
  storage.traits.reserve(trait_rows.size());
  storage.lookup.reserve(trait_rows.size());
  for (const auto &element : trait_rows) {
    const auto id = required_string(element, "id");
    const auto scope_text = required_string(element, "scope");
    const auto scope = scope_text == "civilization" ? ResearchApplicabilityTraitScope::civilization :
        scope_text == "population_or_species" ? ResearchApplicabilityTraitScope::population_or_species :
        (semantic("Unknown applicability trait scope '" + scope_text + "' on '" + id + "'."), ResearchApplicabilityTraitScope{});
    const auto is_mutable = element.at("mutable").get<bool>();
    if (!catalog.has_trait(id))
      semantic("Applicability trait '" + id + "' is not registered by the base research catalog.");
    const auto [_, inserted] = storage.lookup.emplace(id, storage.traits.size());
    if (!inserted) semantic("Duplicate applicability trait '" + id + "'.");
    storage.traits.push_back({id, scope, is_mutable});
  }
  if (storage.traits.size() != static_cast<std::size_t>(catalog.metadata().trait_count))
    semantic("Applicability trait count " + std::to_string(storage.traits.size()) +
             " does not match base catalog count " + std::to_string(catalog.metadata().trait_count) + ".");
  return AdaptiveResearchApplicabilityCatalog(std::move(storage));
}
} // namespace stellar::core
