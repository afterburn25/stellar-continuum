#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::core {

enum class ContactAwareness {
  unknown,
  detected_unidentified,
  identified,
  contact_possible,
  contact_established,
  communication_available,
};
enum class ContactCondition { active, hostile, stale_or_lost };
enum class DiplomaticPoliticalState {
  unknown,
  peace,
  hostile,
  at_war,
  ceasefire,
};
enum class AccessPermission { unspecified, granted, denied };
enum class DiplomaticAgreementType {
  peace,
  non_aggression,
  access,
  trade,
  research_exchange,
  ceasefire,
  cooperation,
};
enum class DiplomaticAgreementStatus { active, terminated };
enum class DiplomaticProposalKind {
  agreement,
  access_request,
  trade_offer,
  demand,
  peace_offer,
  ceasefire_offer,
};
enum class DiplomaticProposalStatus {
  pending,
  accepted,
  rejected,
  withdrawn,
  expired,
};
enum class TerritorialClaimResponse { none, recognized, disputed };
enum class DiplomaticEventKind {
  contact_observed,
  contact_established,
  communication_available,
  contact_lost,
  access_changed,
  claim_asserted,
  claim_communicated,
  claim_responded,
  border_warning_issued,
  trespass_recorded,
  proposal_sent,
  proposal_accepted,
  proposal_rejected,
  proposal_withdrawn,
  proposal_expired,
  agreement_activated,
  agreement_terminated,
  relationship_changed,
  war_declared,
};

class DiplomacyArgumentRangeError final : public std::out_of_range {
public:
  explicit DiplomacyArgumentRangeError(std::string message);
};
class DiplomacyArgumentError final : public std::invalid_argument {
public:
  explicit DiplomacyArgumentError(std::string message);
};
class DiplomacyOperationError final : public std::runtime_error {
public:
  explicit DiplomacyOperationError(std::string message);
};
class DiplomacyOverflowError final : public std::overflow_error {
public:
  explicit DiplomacyOverflowError(std::string message);
};

struct FirstContactOpportunity {
  int observer_civilization_id{};
  std::string contact_id;
  std::optional<int> target_civilization_id;
  std::int64_t observed_at_tick{};
  std::optional<int> observed_system_id;
  ContactAwareness awareness{};
  ContactCondition condition{};
  bool communication_available{};
  double confidence{};

  void validate() const;
};

struct RelationshipImpact {
  double trust_delta{};
  double hostility_delta{};
  double fear_delta{};
  double respect_delta{};
  double cooperation_delta{};
  double grievance_severity{};
  std::string reason;

  void validate() const;
};

struct DiplomaticContactSnapshot {
  int observer_civilization_id{};
  std::string contact_id;
  std::optional<int> target_civilization_id;
  std::int64_t first_observed_tick{};
  std::int64_t last_observed_tick{};
  std::optional<int> last_observed_system_id;
  ContactAwareness awareness{};
  ContactCondition condition{};
  bool communication_available{};
  double confidence{};
};
struct DiplomaticGrievanceSnapshot {
  std::int64_t created_at_tick{};
  int source_civilization_id{};
  double severity{};
  std::string reason;
};
struct DiplomaticRelationshipSnapshot {
  int civilization_a_id{};
  int civilization_b_id{};
  DiplomaticPoliticalState political_state{};
  double trust{};
  double hostility{};
  double fear{};
  double respect{};
  double cooperation{};
  std::vector<DiplomaticGrievanceSnapshot> grievances;
};
struct DiplomaticAccessSnapshot {
  int grantor_civilization_id{};
  int visitor_civilization_id{};
  AccessPermission permission{};
  std::int64_t updated_at_tick{};
};
struct TerritorialClaimSnapshot {
  std::int64_t claim_id{};
  int claimant_civilization_id{};
  int system_id{};
  std::int64_t asserted_at_tick{};
  bool active{};
  std::vector<int> known_to_civilization_ids;
};
struct TerritorialClaimResponseSnapshot {
  std::int64_t claim_id{};
  int responding_civilization_id{};
  TerritorialClaimResponse response{};
  std::int64_t responded_at_tick{};
};
struct DiplomaticAgreementSnapshot {
  std::int64_t agreement_id{};
  int civilization_a_id{};
  int civilization_b_id{};
  DiplomaticAgreementType type{};
  DiplomaticAgreementStatus status{};
  std::int64_t started_at_tick{};
  std::optional<std::int64_t> ended_at_tick;
  std::optional<std::string> external_terms_reference;
};
struct DiplomaticProposalSnapshot {
  std::int64_t proposal_id{};
  int proposer_civilization_id{};
  int recipient_civilization_id{};
  DiplomaticProposalKind kind{};
  std::optional<DiplomaticAgreementType> agreement_type;
  DiplomaticProposalStatus status{};
  std::int64_t created_at_tick{};
  std::optional<std::int64_t> resolved_at_tick;
  std::string summary;
  std::optional<std::string> external_terms_reference;
};
struct DiplomaticHistoryEventSnapshot {
  std::int64_t event_id{};
  std::int64_t tick{};
  DiplomaticEventKind kind{};
  int primary_civilization_id{};
  std::optional<int> secondary_civilization_id;
  std::optional<int> system_id;
  std::string summary;
  std::vector<int> known_to_civilization_ids;
};
struct DiplomacyStateSnapshot {
  std::vector<DiplomaticContactSnapshot> contacts;
  std::vector<DiplomaticRelationshipSnapshot> relationships;
  std::vector<DiplomaticAccessSnapshot> access_permissions;
  std::vector<TerritorialClaimSnapshot> claims;
  std::vector<TerritorialClaimResponseSnapshot> claim_responses;
  std::vector<DiplomaticAgreementSnapshot> agreements;
  std::vector<DiplomaticProposalSnapshot> proposals;
  std::vector<DiplomaticHistoryEventSnapshot> recent_history;
  std::int64_t next_claim_id{};
  std::int64_t next_agreement_id{};
  std::int64_t next_proposal_id{};
  std::int64_t next_event_id{};
};

struct DiplomaticContactView {
  std::string contact_id;
  std::optional<int> target_civilization_id;
  ContactAwareness awareness{};
  ContactCondition condition{};
  bool communication_available{};
  double confidence{};
  std::int64_t last_observed_tick{};
  std::optional<int> last_observed_system_id;
};
struct DiplomaticRelationshipView {
  int other_civilization_id{};
  DiplomaticPoliticalState political_state{};
  double trust{};
  double hostility{};
  double fear{};
  double respect{};
  double cooperation{};
  std::vector<DiplomaticGrievanceSnapshot> grievances;
};
struct DiplomaticStateView {
  int observer_civilization_id{};
  std::vector<DiplomaticContactView> contacts;
  std::vector<DiplomaticRelationshipView> relationships;
  std::vector<DiplomaticAccessSnapshot> access_permissions;
  std::vector<TerritorialClaimSnapshot> claims;
  std::vector<TerritorialClaimResponseSnapshot> claim_responses;
  std::vector<DiplomaticAgreementSnapshot> agreements;
  std::vector<DiplomaticProposalSnapshot> proposals;
  std::vector<DiplomaticHistoryEventSnapshot> recent_events;
};

namespace detail {
class DiplomacyStateAccess;
}

// Owns all diplomatic state in stable Pimpl storage. Moving the state preserves
// addresses returned through the controlled detail access seam. Move-assignment
// invalidates references previously obtained from the target. Public snapshots,
// optionals and observer views own all returned values.
class DiplomacyState final {
public:
  static constexpr std::size_t max_contact_records = 4096;
  static constexpr std::size_t max_recent_history_events = 256;
  static constexpr std::size_t max_stored_proposals = 128;
  static constexpr std::size_t max_pending_proposals_per_pair = 8;
  static constexpr std::size_t max_grievances_per_relationship = 16;

  DiplomacyState();
  ~DiplomacyState();
  DiplomacyState(DiplomacyState &&) noexcept;
  DiplomacyState &operator=(DiplomacyState &&) noexcept;
  DiplomacyState(const DiplomacyState &) = delete;
  DiplomacyState &operator=(const DiplomacyState &) = delete;

  [[nodiscard]] std::optional<DiplomaticContactSnapshot>
  get_contact(int observer, std::string_view contact_id) const;
  [[nodiscard]] std::optional<DiplomaticContactSnapshot>
  get_contact(int observer, int target) const;
  [[nodiscard]] std::optional<DiplomaticRelationshipSnapshot>
  get_relationship(int civilization_a_id, int civilization_b_id) const;
  [[nodiscard]] AccessPermission
  get_access_permission(int grantor, int visitor) const noexcept;
  [[nodiscard]] bool is_transit_authorized(int grantor, int visitor) const;
  [[nodiscard]] DiplomaticStateView build_view_for(int observer) const;
  [[nodiscard]] DiplomacyStateSnapshot snapshot() const;

  // Low-level historical conversion matching DiplomacyState.Restore. This is
  // intentionally permissive and is separate from the later strict player-save
  // invariant validator.
  [[nodiscard]] static DiplomacyState
  restore(const DiplomacyStateSnapshot &snapshot);

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
  friend class detail::DiplomacyStateAccess;
};

} // namespace stellar::core
