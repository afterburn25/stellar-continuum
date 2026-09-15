#pragma once

#include <stellar/core/diplomacy_simulation.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace stellar::core {

// Service objects borrow one stable external DiplomacyState. The state must
// outlive each service and remain unmoved. Moving a service transfers its
// borrow; a moved-from service may only be destroyed or assigned. Text inputs
// are copied before mutation and must contain valid UTF-8 when Unicode
// whitespace/trim behavior is relevant.

class DiplomacyCampaignClock final {
public:
  static constexpr std::int64_t ticks_per_simulation_day = 1000;
  [[nodiscard]] static std::int64_t from_simulation_days(double days);
  [[nodiscard]] static std::int64_t ticks_for_whole_days(std::int64_t days);
};

struct DiplomaticContactAgingResult {
  int reviewed_contacts{};
  int newly_stale_contacts{};
  bool operator==(const DiplomaticContactAgingResult &) const = default;
};

class DiplomaticContactAgingService final {
public:
  explicit DiplomaticContactAgingService(DiplomacyState &state) noexcept;
  DiplomaticContactAgingService(DiplomacyState &&) = delete;
  DiplomaticContactAgingService(const DiplomaticContactAgingService &) = delete;
  DiplomaticContactAgingService &
  operator=(const DiplomaticContactAgingService &) = delete;
  DiplomaticContactAgingService(DiplomaticContactAgingService &&) noexcept;
  DiplomaticContactAgingService &
  operator=(DiplomaticContactAgingService &&) noexcept;

  [[nodiscard]] DiplomaticContactAgingResult
  review(std::int64_t now_tick, std::int64_t stale_after_ticks);

private:
  DiplomacyState *state_{};
};

struct DiplomaticProposalLifetimeOverride {
  DiplomaticProposalKind kind{};
  std::int64_t lifetime_ticks{};
  bool operator==(const DiplomaticProposalLifetimeOverride &) const = default;
};

struct DiplomaticProposalLifecycleReviewResult {
  int pending_proposals_reviewed{};
  int newly_expired_proposals{};
  bool
  operator==(const DiplomaticProposalLifecycleReviewResult &) const = default;
};

class DiplomaticProposalLifecycleService final {
public:
  explicit DiplomaticProposalLifecycleService(DiplomacyState &state) noexcept;
  DiplomaticProposalLifecycleService(DiplomacyState &&) = delete;
  DiplomaticProposalLifecycleService(
      const DiplomaticProposalLifecycleService &) = delete;
  DiplomaticProposalLifecycleService &
  operator=(const DiplomaticProposalLifecycleService &) = delete;
  DiplomaticProposalLifecycleService(
      DiplomaticProposalLifecycleService &&) noexcept;
  DiplomaticProposalLifecycleService &
  operator=(DiplomaticProposalLifecycleService &&) noexcept;

  [[nodiscard]] DiplomaticProposalLifecycleReviewResult
  review(std::int64_t now_tick, std::int64_t default_lifetime_ticks,
         std::span<const DiplomaticProposalLifetimeOverride> lifetime_by_kind =
             {});

private:
  DiplomacyState *state_{};
};

class DiplomaticCommunicationService final {
public:
  explicit DiplomaticCommunicationService(DiplomacyState &state) noexcept;
  DiplomaticCommunicationService(DiplomacyState &&) = delete;
  DiplomaticCommunicationService(const DiplomaticCommunicationService &) =
      delete;
  DiplomaticCommunicationService &
  operator=(const DiplomaticCommunicationService &) = delete;
  DiplomaticCommunicationService(DiplomaticCommunicationService &&) noexcept;
  DiplomaticCommunicationService &
  operator=(DiplomaticCommunicationService &&) noexcept;

  void establish_mutual_communication(int civilization_a, int civilization_b,
                                      std::int64_t tick);

private:
  DiplomacyState *state_{};
};

struct DiplomaticAgreementTerminationResult {
  std::int64_t agreement_id{};
  DiplomaticAgreementType agreement_type{};
  int requested_by_civilization_id{};
  bool terminated{};
  bool cleared_mutual_access{};
  bool resumed_hostility{};
  bool operator==(const DiplomaticAgreementTerminationResult &) const = default;
};

class DiplomaticAgreementTerminationService final {
public:
  explicit DiplomaticAgreementTerminationService(
      DiplomacyState &state) noexcept;
  DiplomaticAgreementTerminationService(DiplomacyState &&) = delete;
  DiplomaticAgreementTerminationService(
      const DiplomaticAgreementTerminationService &) = delete;
  DiplomaticAgreementTerminationService &
  operator=(const DiplomaticAgreementTerminationService &) = delete;
  DiplomaticAgreementTerminationService(
      DiplomaticAgreementTerminationService &&) noexcept;
  DiplomaticAgreementTerminationService &
  operator=(DiplomaticAgreementTerminationService &&) noexcept;

  [[nodiscard]] DiplomaticAgreementTerminationResult
  terminate(std::int64_t agreement_id, int requester_civilization_id,
            std::int64_t tick, std::string_view reason);

private:
  DiplomacyState *state_{};
};

struct DiplomacyCampaignMaintenancePolicy {
  std::int64_t review_interval_ticks{};
  std::int64_t contact_stale_after_ticks{};
  std::int64_t proposal_lifetime_ticks{};

  [[nodiscard]] static DiplomacyCampaignMaintenancePolicy
  early_release_default();
  void validate() const;
  bool operator==(const DiplomacyCampaignMaintenancePolicy &) const = default;
};

struct DiplomacyCampaignMaintenanceResult {
  bool ran{};
  std::int64_t review_tick{};
  DiplomaticContactAgingResult contact_aging;
  DiplomaticProposalLifecycleReviewResult proposal_lifecycle;

  [[nodiscard]] static DiplomacyCampaignMaintenanceResult
  not_due(std::int64_t now_tick) noexcept;
  bool operator==(const DiplomacyCampaignMaintenanceResult &) const = default;
};

class DiplomacyCampaignMaintenanceScheduler final {
public:
  explicit DiplomacyCampaignMaintenanceScheduler(
      DiplomacyState &state,
      std::optional<DiplomacyCampaignMaintenancePolicy> policy = std::nullopt);
  DiplomacyCampaignMaintenanceScheduler(
      DiplomacyState &&, std::optional<DiplomacyCampaignMaintenancePolicy> =
                             std::nullopt) = delete;
  DiplomacyCampaignMaintenanceScheduler(
      const DiplomacyCampaignMaintenanceScheduler &) = delete;
  DiplomacyCampaignMaintenanceScheduler &
  operator=(const DiplomacyCampaignMaintenanceScheduler &) = delete;
  DiplomacyCampaignMaintenanceScheduler(
      DiplomacyCampaignMaintenanceScheduler &&) noexcept;
  DiplomacyCampaignMaintenanceScheduler &
  operator=(DiplomacyCampaignMaintenanceScheduler &&) noexcept;

  [[nodiscard]] std::int64_t next_review_tick() const noexcept;
  [[nodiscard]] std::int64_t last_review_tick() const noexcept;
  void reset(std::int64_t now_tick, bool review_immediately = true);
  [[nodiscard]] DiplomacyCampaignMaintenanceResult
  review_if_due(std::int64_t now_tick);

private:
  static std::int64_t saturating_add(std::int64_t value,
                                     std::int64_t increment);
  DiplomacyState *state_{};
  DiplomacyCampaignMaintenancePolicy policy_;
  std::int64_t next_review_tick_{};
  std::int64_t last_review_tick_{-1};
  bool initialized_{};
};

} // namespace stellar::core
