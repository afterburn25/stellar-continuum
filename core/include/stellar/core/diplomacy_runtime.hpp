#pragma once

#include <stellar/core/combat_command_runtime.hpp>
#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/exploration_advance.hpp>
#include <stellar/core/strategic_planning.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace stellar::core {

// These adapters borrow a stable external DiplomacyState. The state must
// outlive the adapter and remain unmoved. Moving an adapter transfers its
// borrow; moved-from objects may only be destroyed or assigned.
class ExplorationDiplomacyBridge final {
public:
  explicit ExplorationDiplomacyBridge(DiplomacySimulation &simulation) noexcept;
  ExplorationDiplomacyBridge(DiplomacySimulation &&) = delete;
  ExplorationDiplomacyBridge(const ExplorationDiplomacyBridge &) = delete;
  ExplorationDiplomacyBridge &operator=(const ExplorationDiplomacyBridge &) = delete;
  ExplorationDiplomacyBridge(ExplorationDiplomacyBridge &&) noexcept = default;
  ExplorationDiplomacyBridge &operator=(ExplorationDiplomacyBridge &&) noexcept = default;
  [[nodiscard]] int process(std::span<const ExplorationEvent> events,
                            std::int64_t observed_at_tick);
private:
  DiplomacySimulation *simulation_{};
};

class CombatDiplomacyBridge final {
public:
  explicit CombatDiplomacyBridge(DiplomacyState &state) noexcept;
  CombatDiplomacyBridge(DiplomacyState &&) = delete;
  CombatDiplomacyBridge(const CombatDiplomacyBridge &) = delete;
  CombatDiplomacyBridge &operator=(const CombatDiplomacyBridge &) = delete;
  CombatDiplomacyBridge(CombatDiplomacyBridge &&) noexcept = default;
  CombatDiplomacyBridge &operator=(CombatDiplomacyBridge &&) noexcept = default;
  [[nodiscard]] int process(std::span<const CombatEvent> events,
                            std::int64_t tick);
private:
  DiplomacyState *state_{};
  DiplomacySimulation simulation_;
};

class DiplomacyCombatHostilityView final {
public:
  explicit DiplomacyCombatHostilityView(const DiplomacyState &state) noexcept;
  DiplomacyCombatHostilityView(DiplomacyState &&) = delete;
  [[nodiscard]] bool are_hostile(int first, int second) const;
  // The callback owns no state and must not outlive the borrowed DiplomacyState.
  [[nodiscard]] CombatHostilityView combat_hostility_view() const;
private:
  const DiplomacyState *state_{};
};

class DiplomacyStrategicKnowledgeProvider final {
public:
  explicit DiplomacyStrategicKnowledgeProvider(const DiplomacyState &state) noexcept;
  DiplomacyStrategicKnowledgeProvider(DiplomacyState &&) = delete;
  [[nodiscard]] StrategicKnowledgeSnapshot build(int observer,
                                                  std::int64_t now_tick) const;
private:
  const DiplomacyState *state_{};
};

struct DiplomacyCampaignRuntimeStepResult {
  std::int64_t tick{};
  int first_contact_events_processed{};
  int combat_incidents_processed{};
  DiplomacyCampaignMaintenanceResult maintenance;
  [[nodiscard]] int maintenance_transitions() const noexcept;
  [[nodiscard]] int processed_diplomacy_events() const noexcept;
};

class DiplomacyCampaignRuntimeCoordinator final {
public:
  explicit DiplomacyCampaignRuntimeCoordinator(
      DiplomacyState &state,
      std::optional<DiplomacyCampaignMaintenancePolicy> maintenance_policy =
          std::nullopt);
  DiplomacyCampaignRuntimeCoordinator(
      DiplomacyState &&,
      std::optional<DiplomacyCampaignMaintenancePolicy> = std::nullopt) = delete;
  ~DiplomacyCampaignRuntimeCoordinator();
  DiplomacyCampaignRuntimeCoordinator(const DiplomacyCampaignRuntimeCoordinator &) = delete;
  DiplomacyCampaignRuntimeCoordinator &operator=(const DiplomacyCampaignRuntimeCoordinator &) = delete;
  DiplomacyCampaignRuntimeCoordinator(DiplomacyCampaignRuntimeCoordinator &&) noexcept;
  DiplomacyCampaignRuntimeCoordinator &operator=(DiplomacyCampaignRuntimeCoordinator &&) noexcept;

  [[nodiscard]] DiplomacyState &state() noexcept;
  [[nodiscard]] const DiplomacyState &state() const noexcept;
  [[nodiscard]] ObserverDiplomacyCommandService &commands() noexcept;
  [[nodiscard]] const ObserverDiplomacyCommandService &commands() const noexcept;
  [[nodiscard]] const DiplomacyCombatHostilityView &hostility_view() const noexcept;
  [[nodiscard]] std::int64_t last_processed_tick() const noexcept;
  [[nodiscard]] std::int64_t next_maintenance_review_tick() const noexcept;
  [[nodiscard]] CombatCommandRuntime create_combat_command_runtime() const;
  [[nodiscard]] CombatSimulation create_combat_simulation() const;
  [[nodiscard]] DiplomaticStateView build_view(int observer) const;
  void reset(double simulation_days, bool review_immediately = true);
  [[nodiscard]] DiplomacyCampaignRuntimeStepResult
  process(std::span<const ExplorationEvent> exploration_events,
          std::span<const CombatEvent> combat_events, double simulation_days);

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
