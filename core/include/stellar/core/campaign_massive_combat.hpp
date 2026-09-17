#pragma once

#include <stellar/core/combat_simulation.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/massive_combat_observer.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace stellar::core {

class CampaignMassiveCombatArgumentNullError final
    : public std::invalid_argument {
public:
  CampaignMassiveCombatArgumentNullError(std::string message,
                                         std::string parameter);
  [[nodiscard]] const std::string &parameter() const noexcept;

private:
  std::string parameter_;
};

class CampaignMassiveCombatSerializationError final
    : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Owns the hostility callable and tactical engine at stable Pimpl addresses.
// References captured by the callable must outlive this object and remain at
// stable addresses for every call.
class CampaignMassiveCombat final {
public:
  explicit CampaignMassiveCombat(CombatHostilityView hostility);
  ~CampaignMassiveCombat();
  CampaignMassiveCombat(CampaignMassiveCombat &&) noexcept;
  CampaignMassiveCombat &operator=(CampaignMassiveCombat &&) noexcept;
  CampaignMassiveCombat(const CampaignMassiveCombat &) = delete;
  CampaignMassiveCombat &operator=(const CampaignMassiveCombat &) = delete;

  // May materialize participant combat state before a later authored rejection.
  [[nodiscard]] CombatOrderResult begin(FreshCampaignState &galaxy,
                                        int civilization_id,
                                        int actor_fleet_id, double day);

  // Mirrors Main.UiIssueMassiveCombatOrder: rejects without an unreconciled
  // encounter, otherwise forwards to the shared tactical engine.
  [[nodiscard]] MassiveCombatOrderResult
  issue_order(FreshCampaignState &galaxy, int civilization_id,
              MassiveCombatOrder order);

  // Idempotently returns no events when no unreconciled encounter exists.
  [[nodiscard]] std::vector<CombatEvent>
  reconcile(FreshCampaignState &galaxy);

  [[nodiscard]] MassiveCombatSnapshot
  observe(const FreshCampaignState &galaxy, int observer_civilization_id,
          bool scanning_capability) const;

  [[nodiscard]] std::vector<CombatEvent>
  advance(FreshCampaignState &galaxy, double elapsed_seconds,
          const std::function<bool(int)> &has_combat_scanner = {});

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

} // namespace stellar::core
