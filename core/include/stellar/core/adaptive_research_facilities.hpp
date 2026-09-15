#pragma once

#include <stellar/core/adaptive_research_catalog.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct ResearchStageFacilityRequirement {
  std::vector<std::string> all_of;
  std::vector<std::string> any_of;
};

struct ResearchFacilityInstitutionDefinition {
  std::string id;
  double effective_lab_units{};
  std::vector<std::string> facility_capabilities;
};

class AdaptiveResearchFacilityCatalogError final : public std::runtime_error {
public:
  explicit AdaptiveResearchFacilityCatalogError(std::string message);
};

// Immutable, ordered research-facing capability definitions. All returned views
// borrow this catalog and remain valid until it is moved or destroyed.
class AdaptiveResearchFacilityCatalog final {
public:
  AdaptiveResearchFacilityCatalog(const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchFacilityCatalog &operator=(const AdaptiveResearchFacilityCatalog &) = delete;
  AdaptiveResearchFacilityCatalog(AdaptiveResearchFacilityCatalog &&) noexcept;
  AdaptiveResearchFacilityCatalog &operator=(AdaptiveResearchFacilityCatalog &&) noexcept;
  ~AdaptiveResearchFacilityCatalog();

  [[nodiscard]] std::span<const std::string> facility_capability_ids() const noexcept;
  [[nodiscard]] std::span<const ResearchFacilityInstitutionDefinition> institutions() const noexcept;
  [[nodiscard]] const ResearchFacilityInstitutionDefinition *find_institution(std::string_view id) const noexcept;
  [[nodiscard]] const ResearchStageFacilityRequirement *get_stage_requirement(
      std::string_view node_id, ResearchMaturity stage) const noexcept;

private:
  struct Storage;
  explicit AdaptiveResearchFacilityCatalog(Storage storage);
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchFacilityCatalog load_adaptive_research_facility_catalog(
      const std::filesystem::path &, const AdaptiveResearchCatalog &);
};

// The definitions borrow catalog only while loading for node/catalog-id validation.
[[nodiscard]] AdaptiveResearchFacilityCatalog load_adaptive_research_facility_catalog(
    const std::filesystem::path &root_path, const AdaptiveResearchCatalog &catalog);

} // namespace stellar::core
