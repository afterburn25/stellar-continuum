#pragma once

#include <stellar/core/combat_simulation.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {

struct CombatOrderPreview {
  int civilization_id{}, fleet_id{};
  MilitaryOrder order;
  bool accepted{};
  std::string message;
};

struct FleetCombatOrderPreviewResult {
  int fleet_id{};
  bool accepted{};
  std::string message;
};

struct CombatBatchOrderPreview {
  int requested_fleet_count{}, accepted_count{}, rejected_count{};
  std::vector<FleetCombatOrderPreviewResult> fleet_results;
  bool all_accepted() const;
  bool any_accepted() const;
};

struct FleetCombatOrderResult {
  int fleet_id{};
  bool accepted{};
  std::string message;
};

struct CombatBatchOrderResult {
  int requested_fleet_count{}, accepted_count{}, rejected_count{};
  std::vector<FleetCombatOrderResult> fleet_results;
  bool all_accepted() const;
  bool any_accepted() const;
};

[[nodiscard]] CombatBatchOrderResult
issue_combat_batch(CombatSimulation &simulation, CombatWorldView world,
                   int civilization_id, std::span<const int> fleet_ids,
                   const MilitaryOrder &order);

class CombatCommandRuntime {
public:
  explicit CombatCommandRuntime(CombatHostilityView hostility = {});
  CombatCommandRuntime(const CombatCommandRuntime &) = delete;
  CombatCommandRuntime &operator=(const CombatCommandRuntime &) = delete;
  CombatCommandRuntime(CombatCommandRuntime &&) noexcept = default;
  CombatCommandRuntime &operator=(CombatCommandRuntime &&) noexcept = default;

  CombatOrderPreview preview_order(CombatWorldView world, int civilization_id,
                                   int fleet_id,
                                   const MilitaryOrder &order) const;
  CombatBatchOrderPreview preview_orders(CombatWorldView world,
                                         int civilization_id,
                                         std::span<const int> fleet_ids,
                                         const MilitaryOrder &order) const;
  CombatOrderResult issue_order(CombatWorldView world, int civilization_id,
                                int fleet_id, const MilitaryOrder &order);
  CombatBatchOrderResult issue_orders(CombatWorldView world,
                                      int civilization_id,
                                      std::span<const int> fleet_ids,
                                      const MilitaryOrder &order);
  CombatOrderResult issue_engage_hostiles(CombatWorldView world,
                                          int civilization_id, int fleet_id);

  CombatSimulation &simulation() noexcept;
  const CombatSimulation &simulation() const noexcept;

private:
  std::shared_ptr<CombatHostilityView> hostility_;
  CombatSimulation simulation_;
};

} // namespace stellar::core
