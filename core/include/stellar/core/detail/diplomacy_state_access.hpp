#pragma once

#include <stellar/core/diplomacy_state.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stellar::core::detail {

struct DiplomacyCivilizationPair {
  int first{};
  int second{};

  [[nodiscard]] static DiplomacyCivilizationPair create(int a, int b);
  [[nodiscard]] bool contains(int civilization_id) const noexcept;
  [[nodiscard]] int other(int civilization_id) const noexcept;
  bool operator==(const DiplomacyCivilizationPair &) const = default;
};

struct DiplomacyContactState {
  int observer{};
  std::string contact_id;
  std::optional<int> target;
  std::int64_t first_tick{};
  std::int64_t last_tick{};
  std::optional<int> system_id;
  ContactAwareness awareness{};
  ContactCondition condition{};
  bool can_communicate{};
  double confidence{};

  [[nodiscard]] DiplomaticContactSnapshot snapshot() const;
};

struct DiplomacyRelationshipState {
  DiplomacyCivilizationPair pair;
  DiplomaticPoliticalState political_state{DiplomaticPoliticalState::peace};
  double trust{};
  double hostility{};
  double fear{};
  double respect{};
  double cooperation{};
  std::vector<DiplomaticGrievanceSnapshot> grievances;

  [[nodiscard]] DiplomaticRelationshipSnapshot snapshot() const;
};

struct DiplomacyClaimState {
  std::int64_t id{};
  int claimant{};
  int system_id{};
  std::int64_t tick{};
  bool active{true};
  std::vector<int> known_to;

  [[nodiscard]] TerritorialClaimSnapshot snapshot() const;
};

struct DiplomacyAgreementState {
  std::int64_t id{};
  DiplomacyCivilizationPair pair;
  DiplomaticAgreementType type{};
  DiplomaticAgreementStatus status{DiplomaticAgreementStatus::active};
  std::int64_t started{};
  std::optional<std::int64_t> ended;
  std::optional<std::string> external_terms;

  [[nodiscard]] DiplomaticAgreementSnapshot snapshot() const;
};

struct DiplomacyProposalState {
  std::int64_t id{};
  int proposer{};
  int recipient{};
  DiplomaticProposalKind kind{};
  std::optional<DiplomaticAgreementType> agreement_type;
  DiplomaticProposalStatus status{DiplomaticProposalStatus::pending};
  std::int64_t created{};
  std::optional<std::int64_t> resolved;
  std::string summary;
  std::optional<std::string> external_terms;

  [[nodiscard]] DiplomaticProposalSnapshot snapshot() const;
};

// Controlled equivalent of DiplomacyState's source-internal storage methods.
// Higher-level command and maintenance ports compose these primitives rather
// than gaining direct access to the Pimpl containers. Returned references and
// pointers remain stable across growth and unrelated mutations, but are
// invalidated when their own entry is removed, evicted, or replaced, and when
// the owning state is destroyed or replaced.
class DiplomacyStateAccess final {
public:
  [[nodiscard]] static DiplomacyContactState &
  upsert_contact(DiplomacyState &state,
                 const FirstContactOpportunity &opportunity);
  [[nodiscard]] static DiplomacyContactState *
  mutable_contact(DiplomacyState &state, int observer,
                  std::string_view contact_id) noexcept;
  [[nodiscard]] static bool has_identified(const DiplomacyState &state,
                                           int observer, int target) noexcept;
  [[nodiscard]] static bool has_communication(const DiplomacyState &state,
                                              int observer, int target) noexcept;
  [[nodiscard]] static bool
  has_mutual_communication(const DiplomacyState &state, int a, int b) noexcept;
  [[nodiscard]] static DiplomacyRelationshipState &
  relationship(DiplomacyState &state, int a, int b);
  static void set_access(DiplomacyState &state, int grantor, int visitor,
                         AccessPermission permission, std::int64_t tick);
  [[nodiscard]] static DiplomacyClaimState &
  create_claim(DiplomacyState &state, int claimant, int system_id,
               std::int64_t tick);
  [[nodiscard]] static DiplomacyClaimState &
  claim(DiplomacyState &state, std::int64_t claim_id);
  static void respond_to_claim(DiplomacyState &state, std::int64_t claim_id,
                               int responder,
                               TerritorialClaimResponse response,
                               std::int64_t tick);
  [[nodiscard]] static DiplomacyProposalState &create_proposal(
      DiplomacyState &state, int proposer, int recipient,
      DiplomaticProposalKind kind,
      std::optional<DiplomaticAgreementType> agreement_type,
      std::int64_t tick, std::string summary,
      std::optional<std::string> external_terms);
  [[nodiscard]] static DiplomacyProposalState &
  proposal(DiplomacyState &state, std::int64_t proposal_id);
  [[nodiscard]] static DiplomacyAgreementState &activate_agreement(
      DiplomacyState &state, int a, int b, DiplomaticAgreementType type,
      std::int64_t tick, std::optional<std::string> external_terms);
  [[nodiscard]] static std::vector<DiplomacyAgreementState *>
  active_agreements(DiplomacyState &state, DiplomacyCivilizationPair pair);
  static void record(DiplomacyState &state, std::int64_t tick,
                     DiplomaticEventKind kind, int primary,
                     std::optional<int> secondary, std::optional<int> system_id,
                     std::string summary, std::vector<int> audience);
};

} // namespace stellar::core::detail
