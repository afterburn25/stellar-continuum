#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <stellar/core/adaptive_research_catalog.hpp>

namespace stellar::core {

enum class ResearchApplicabilityTraitScope { civilization, population_or_species };

struct ResearchApplicabilityTraitDefinition {
  std::string id;
  ResearchApplicabilityTraitScope scope{};
  bool is_mutable{};
};

class AdaptiveResearchApplicabilityCatalogError : public std::runtime_error {
public:
  explicit AdaptiveResearchApplicabilityCatalogError(std::string message);
};

// Immutable companion data for the trait IDs owned by AdaptiveResearchCatalog.
// Borrowed definitions remain valid until this object is moved, move-assigned, or destroyed.
class AdaptiveResearchApplicabilityCatalog final {
public:
  AdaptiveResearchApplicabilityCatalog(const AdaptiveResearchApplicabilityCatalog &) = delete;
  AdaptiveResearchApplicabilityCatalog &operator=(const AdaptiveResearchApplicabilityCatalog &) = delete;
  AdaptiveResearchApplicabilityCatalog(AdaptiveResearchApplicabilityCatalog &&) noexcept;
  AdaptiveResearchApplicabilityCatalog &operator=(AdaptiveResearchApplicabilityCatalog &&) noexcept;
  ~AdaptiveResearchApplicabilityCatalog();

  [[nodiscard]] std::span<const ResearchApplicabilityTraitDefinition> traits() const noexcept;
  [[nodiscard]] const ResearchApplicabilityTraitDefinition *find_trait(
      std::string_view trait_id) const noexcept;
  [[nodiscard]] const ResearchApplicabilityTraitDefinition &get_trait(
      std::string_view trait_id) const;

private:
  struct Storage;
  explicit AdaptiveResearchApplicabilityCatalog(Storage storage);
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchApplicabilityCatalog load_adaptive_research_applicability_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &);
};

[[nodiscard]] AdaptiveResearchApplicabilityCatalog
load_adaptive_research_applicability_catalog(const std::filesystem::path &root_path,
                                             const AdaptiveResearchCatalog &catalog);

} // namespace stellar::core
