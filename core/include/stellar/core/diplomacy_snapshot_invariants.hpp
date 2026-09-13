#pragma once

#include <stellar/core/diplomacy_state.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace stellar::core {

struct DiplomacySnapshotValidationResult {
  int contact_count{};
  int relationship_count{};
  int access_permission_count{};
  int claim_count{};
  int claim_response_count{};
  int agreement_count{};
  int proposal_count{};
  int history_event_count{};
};

class DiplomacySnapshotValidationError final : public std::runtime_error {
public:
  explicit DiplomacySnapshotValidationError(std::string message);
};

struct DiplomaticRelationshipValidationView {
  int civilization_a_id{};
  int civilization_b_id{};
  DiplomaticPoliticalState political_state{};
  double trust{};
  double hostility{};
  double fear{};
  double respect{};
  double cooperation{};
  std::optional<std::span<const DiplomaticGrievanceSnapshot>> grievances;
};

struct TerritorialClaimValidationView {
  std::int64_t claim_id{};
  int claimant_civilization_id{};
  int system_id{};
  std::int64_t asserted_at_tick{};
  bool active{};
  std::optional<std::span<const int>> known_to_civilization_ids;
};

struct DiplomaticHistoryEventValidationView {
  std::int64_t event_id{};
  std::int64_t tick{};
  DiplomaticEventKind kind{};
  int primary_civilization_id{};
  std::optional<int> secondary_civilization_id;
  std::optional<int> system_id;
  std::string_view summary;
  std::optional<std::span<const int>> known_to_civilization_ids;
};

// Parser-facing borrowed view. A disengaged optional represents a null/missing
// top-level array and remains distinct from an engaged empty span. All spans
// need remain valid only for the validation call; the validator stores nothing
// and never mutates the snapshot records.
struct DiplomacySnapshotValidationView {
  std::optional<std::span<const DiplomaticContactSnapshot>> contacts;
  std::optional<std::span<const DiplomaticRelationshipValidationView>> relationships;
  std::optional<std::span<const DiplomaticAccessSnapshot>> access_permissions;
  std::optional<std::span<const TerritorialClaimValidationView>> claims;
  std::optional<std::span<const TerritorialClaimResponseSnapshot>> claim_responses;
  std::optional<std::span<const DiplomaticAgreementSnapshot>> agreements;
  std::optional<std::span<const DiplomaticProposalSnapshot>> proposals;
  std::optional<std::span<const DiplomaticHistoryEventValidationView>> recent_history;
  std::int64_t next_claim_id{};
  std::int64_t next_agreement_id{};
  std::int64_t next_proposal_id{};
  std::int64_t next_event_id{};
};

class DiplomacySnapshotInvariantValidator final {
public:
  [[nodiscard]] static DiplomacySnapshotValidationResult
  validate(const DiplomacyStateSnapshot &snapshot);

  [[nodiscard]] static DiplomacySnapshotValidationResult
  validate(DiplomacySnapshotValidationView snapshot);
};

} // namespace stellar::core
