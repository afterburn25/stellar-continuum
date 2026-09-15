#pragma once

#include <stellar/core/diplomacy_state.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

struct Civilization;
struct Colony;
class CivilizationKnowledgeState;
struct StellarSystem;

enum class ObserverDiplomacyCommandStatus {
  accepted,
  channel_unavailable,
  action_unavailable,
  invalid_request,
};

struct ObserverDiplomacyCommandResult {
  bool accepted{};
  ObserverDiplomacyCommandStatus status{};
  std::string message;
  std::optional<std::int64_t> proposal_id;
  std::optional<std::int64_t> agreement_id;
};

struct CampaignTerritorialClaimCommandResult {
  bool accepted{};
  ObserverDiplomacyCommandStatus status{};
  std::string message;
  std::optional<std::int64_t> claim_id;
};

struct ObserverDiplomacyActionAvailability {
  int counterpart_civilization_id{};
  ContactAwareness contact_awareness{};
  ContactCondition contact_condition{};
  double contact_confidence{};
  std::int64_t last_observed_tick{};
  DiplomaticPoliticalState political_state{};
  bool communication_available{};
  bool can_attempt_communication{};
  bool can_send_proposal{};
  bool can_set_access_permission{};
  bool can_declare_war{};
  bool can_respond_to_pending_proposal{};
  bool can_withdraw_pending_proposal{};
  bool can_terminate_active_agreement{};
  int pending_incoming_proposal_count{};
  int pending_outgoing_proposal_count{};
  int active_agreement_count{};
};

[[nodiscard]] std::vector<ObserverDiplomacyActionAvailability>
build_observer_diplomacy_action_availability(const DiplomaticStateView &view);

// Borrows one stable mutable DiplomacyState. The state must outlive the service
// and remain unmoved. Inputs that may alias state-owned strings are copied
// before any authoritative mutation.
class ObserverDiplomacyCommandService final {
public:
  explicit ObserverDiplomacyCommandService(DiplomacyState &state) noexcept;
  ObserverDiplomacyCommandService(DiplomacyState &&) = delete;
  ObserverDiplomacyCommandService(const ObserverDiplomacyCommandService &) =
      delete;
  ObserverDiplomacyCommandService &
  operator=(const ObserverDiplomacyCommandService &) = delete;
  ObserverDiplomacyCommandService(ObserverDiplomacyCommandService &&) noexcept =
      default;
  ObserverDiplomacyCommandService &
  operator=(ObserverDiplomacyCommandService &&) noexcept = default;

  [[nodiscard]] DiplomaticStateView build_view(int observer) const;
  [[nodiscard]] ObserverDiplomacyCommandResult
  establish_communication(int observer, int target, std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult send_proposal(
      int observer, int target, DiplomaticProposalKind kind, std::int64_t tick,
      std::string_view summary,
      std::optional<DiplomaticAgreementType> agreement_type = std::nullopt,
      std::optional<std::string_view> external_terms_reference = std::nullopt);
  [[nodiscard]] ObserverDiplomacyCommandResult
  respond_to_proposal(int observer, std::int64_t proposal_id, bool accept,
                      std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult
  withdraw_proposal(int observer, std::int64_t proposal_id, std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult
  set_access_permission(int observer, int target, AccessPermission permission,
                        std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult
  declare_war(int observer, int target, std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult terminate_agreement(
      int observer, std::int64_t agreement_id, std::int64_t tick,
      std::string_view reason);
  [[nodiscard]] ObserverDiplomacyCommandResult communicate_territorial_claim(
      int observer, std::int64_t claim_id, int recipient, std::int64_t tick);
  [[nodiscard]] ObserverDiplomacyCommandResult respond_to_territorial_claim(
      int observer, std::int64_t claim_id, TerritorialClaimResponse response,
      std::int64_t tick);

private:
  [[nodiscard]] bool has_visible_active_communication(int observer,
                                                       int target) const;
  DiplomacyState *state_{};
};

// The spans and knowledge object need only outlive the command call. Services
// never retain this view, so campaign vector relocation cannot stale them.
struct DiplomacyCampaignCommandWorldView {
  std::span<const Civilization> civilizations;
  std::span<const StellarSystem> systems;
  std::span<const Colony> colonies;
  const CivilizationKnowledgeState &knowledge;
};

class CampaignDiplomacyTerritorialClaimCommandService final {
public:
  explicit CampaignDiplomacyTerritorialClaimCommandService(
      DiplomacyState &state) noexcept;
  CampaignDiplomacyTerritorialClaimCommandService(DiplomacyState &&) = delete;
  CampaignDiplomacyTerritorialClaimCommandService(
      const CampaignDiplomacyTerritorialClaimCommandService &) = delete;
  CampaignDiplomacyTerritorialClaimCommandService &operator=(
      const CampaignDiplomacyTerritorialClaimCommandService &) = delete;
  CampaignDiplomacyTerritorialClaimCommandService(
      CampaignDiplomacyTerritorialClaimCommandService &&) noexcept = default;
  CampaignDiplomacyTerritorialClaimCommandService &operator=(
      CampaignDiplomacyTerritorialClaimCommandService &&) noexcept = default;

  [[nodiscard]] CampaignTerritorialClaimCommandResult assert_territorial_claim(
      DiplomacyCampaignCommandWorldView world, int observer, int system_id,
      std::int64_t tick);

private:
  DiplomacyState *state_{};
};

class CampaignDiplomacyBorderWarningCommandService final {
public:
  explicit CampaignDiplomacyBorderWarningCommandService(
      DiplomacyState &state) noexcept;
  CampaignDiplomacyBorderWarningCommandService(DiplomacyState &&) = delete;
  CampaignDiplomacyBorderWarningCommandService(
      const CampaignDiplomacyBorderWarningCommandService &) = delete;
  CampaignDiplomacyBorderWarningCommandService &operator=(
      const CampaignDiplomacyBorderWarningCommandService &) = delete;
  CampaignDiplomacyBorderWarningCommandService(
      CampaignDiplomacyBorderWarningCommandService &&) noexcept = default;
  CampaignDiplomacyBorderWarningCommandService &operator=(
      CampaignDiplomacyBorderWarningCommandService &&) noexcept = default;

  [[nodiscard]] ObserverDiplomacyCommandResult issue_border_warning(
      DiplomacyCampaignCommandWorldView world, int issuer, int recipient,
      int system_id, std::int64_t tick);

private:
  DiplomacyState *state_{};
};

} // namespace stellar::core
