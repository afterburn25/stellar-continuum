#pragma once

#include <stellar/core/adaptive_research_agenda.hpp>
#include <stellar/core/adaptive_research_foreign_discovery.hpp>
#include <stellar/core/adaptive_research_outcomes.hpp>

#include <filesystem>
#include <memory>

namespace stellar::core {

// Owns the complete research dependency graph in one stable allocation.
// Member addresses and per-civilization support survive moving this owner.
// Returned references borrow that allocation: destruction or replacement of
// its current owner invalidates them. A moved-from owner can only be destroyed
// or assigned a new valid owner. Civilization states do not keep it alive.
class AdaptiveResearchStrategicRuntime final {
public:
  ~AdaptiveResearchStrategicRuntime();
  AdaptiveResearchStrategicRuntime(AdaptiveResearchStrategicRuntime &&) noexcept;
  AdaptiveResearchStrategicRuntime &
  operator=(AdaptiveResearchStrategicRuntime &&) noexcept;
  AdaptiveResearchStrategicRuntime(const AdaptiveResearchStrategicRuntime &) = delete;
  AdaptiveResearchStrategicRuntime &
  operator=(const AdaptiveResearchStrategicRuntime &) = delete;

  [[nodiscard]] const AdaptiveResearchAuthority &authority() const noexcept;
  [[nodiscard]] const AdaptiveResearchPressureCatalog &pressure_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchPressureRuntime &pressure() const noexcept;
  [[nodiscard]] const AdaptiveResearchAgendaCatalog &agenda_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchAgendaRuntime &agenda() const noexcept;
  [[nodiscard]] const AdaptiveResearchForeignTechnologyCatalog &foreign_technology_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchForeignTechnologyRuntime &foreign_technology() const noexcept;
  [[nodiscard]] const AdaptiveResearchForeignDiscoveryCatalog &foreign_discovery_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchForeignDiscoveryRuntime &foreign_discovery() const noexcept;
  [[nodiscard]] const AdaptiveResearchOutcomeCatalog &outcome_catalog() const noexcept;
  [[nodiscard]] const AdaptiveResearchOutcomeRuntime &outcomes() const noexcept;

private:
  struct Storage;
  explicit AdaptiveResearchStrategicRuntime(std::unique_ptr<Storage>) noexcept;
  std::unique_ptr<Storage> storage_;
  friend AdaptiveResearchStrategicRuntime
  load_adaptive_research_strategic_runtime(const std::filesystem::path &);
};

[[nodiscard]] AdaptiveResearchStrategicRuntime
load_adaptive_research_strategic_runtime(const std::filesystem::path &root_path);

} // namespace stellar::core
