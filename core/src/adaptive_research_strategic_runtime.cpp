#include <stellar/core/adaptive_research_strategic_runtime.hpp>

#include <utility>

namespace stellar::core {

struct AdaptiveResearchStrategicRuntime::Storage {
  // Declaration order is load order and dependency lifetime order. Construct
  // each dependent against its final member, never a temporary later moved.
  AdaptiveResearchAuthority authority;
  AdaptiveResearchPressureCatalog pressure_catalog;
  AdaptiveResearchPressureRuntime pressure;
  AdaptiveResearchAgendaCatalog agenda_catalog;
  AdaptiveResearchAgendaRuntime agenda;
  AdaptiveResearchForeignTechnologyCatalog foreign_technology_catalog;
  AdaptiveResearchForeignTechnologyRuntime foreign_technology;
  AdaptiveResearchForeignDiscoveryCatalog foreign_discovery_catalog;
  AdaptiveResearchForeignDiscoveryRuntime foreign_discovery;
  AdaptiveResearchOutcomeCatalog outcome_catalog;
  AdaptiveResearchOutcomeRuntime outcomes;

  explicit Storage(const std::filesystem::path &root)
      : authority(load_adaptive_research_authority(root)),
        pressure_catalog(load_adaptive_research_pressure_catalog(root, authority.catalog())),
        pressure(authority.kernel(), pressure_catalog),
        agenda_catalog(load_adaptive_research_agenda_catalog(
            root, authority.catalog(), authority.expertise_catalog())),
        agenda(authority, agenda_catalog),
        foreign_technology_catalog(load_adaptive_research_foreign_technology_catalog(
            root, authority.catalog(), authority.expertise_catalog())),
        foreign_technology(authority, foreign_technology_catalog),
        foreign_discovery_catalog(load_adaptive_research_foreign_discovery_catalog(
            root, authority.catalog())),
        foreign_discovery(authority, foreign_technology, foreign_discovery_catalog),
        outcome_catalog(load_adaptive_research_outcome_catalog(root, authority.catalog())),
        outcomes(authority, outcome_catalog, pressure) {}

  Storage(const Storage &) = delete;
  Storage &operator=(const Storage &) = delete;
  Storage(Storage &&) = delete;
  Storage &operator=(Storage &&) = delete;
};

AdaptiveResearchStrategicRuntime::AdaptiveResearchStrategicRuntime(
    std::unique_ptr<Storage> storage) noexcept : storage_(std::move(storage)) {}
AdaptiveResearchStrategicRuntime::~AdaptiveResearchStrategicRuntime() = default;
AdaptiveResearchStrategicRuntime::AdaptiveResearchStrategicRuntime(
    AdaptiveResearchStrategicRuntime &&) noexcept = default;
AdaptiveResearchStrategicRuntime &AdaptiveResearchStrategicRuntime::operator=(
    AdaptiveResearchStrategicRuntime &&) noexcept = default;

const AdaptiveResearchAuthority &
AdaptiveResearchStrategicRuntime::authority() const noexcept {
  return storage_->authority;
}
const AdaptiveResearchPressureCatalog &
AdaptiveResearchStrategicRuntime::pressure_catalog() const noexcept {
  return storage_->pressure_catalog;
}
const AdaptiveResearchPressureRuntime &
AdaptiveResearchStrategicRuntime::pressure() const noexcept {
  return storage_->pressure;
}
const AdaptiveResearchAgendaCatalog &
AdaptiveResearchStrategicRuntime::agenda_catalog() const noexcept {
  return storage_->agenda_catalog;
}
const AdaptiveResearchAgendaRuntime &
AdaptiveResearchStrategicRuntime::agenda() const noexcept {
  return storage_->agenda;
}
const AdaptiveResearchForeignTechnologyCatalog &
AdaptiveResearchStrategicRuntime::foreign_technology_catalog() const noexcept {
  return storage_->foreign_technology_catalog;
}
const AdaptiveResearchForeignTechnologyRuntime &
AdaptiveResearchStrategicRuntime::foreign_technology() const noexcept {
  return storage_->foreign_technology;
}
const AdaptiveResearchForeignDiscoveryCatalog &
AdaptiveResearchStrategicRuntime::foreign_discovery_catalog() const noexcept {
  return storage_->foreign_discovery_catalog;
}
const AdaptiveResearchForeignDiscoveryRuntime &
AdaptiveResearchStrategicRuntime::foreign_discovery() const noexcept {
  return storage_->foreign_discovery;
}
const AdaptiveResearchOutcomeCatalog &
AdaptiveResearchStrategicRuntime::outcome_catalog() const noexcept {
  return storage_->outcome_catalog;
}
const AdaptiveResearchOutcomeRuntime &
AdaptiveResearchStrategicRuntime::outcomes() const noexcept {
  return storage_->outcomes;
}

AdaptiveResearchStrategicRuntime load_adaptive_research_strategic_runtime(
    const std::filesystem::path &root_path) {
  return AdaptiveResearchStrategicRuntime(
      std::make_unique<AdaptiveResearchStrategicRuntime::Storage>(root_path));
}

} // namespace stellar::core
