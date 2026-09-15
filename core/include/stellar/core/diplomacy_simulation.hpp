#pragma once

#include <stellar/core/diplomacy_state.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stellar::core {
namespace detail {
struct DiplomacyProposalState;
}

// Authoritative Diplomacy command layer borrowing one stable mutable state.
// The state must outlive this object and remain unmoved. Move-assignment
// replaces the borrowed state association; a moved-from simulation may only be
// destroyed or assigned a valid simulation.
class DiplomacySimulation final {
public:
  explicit DiplomacySimulation(DiplomacyState &state) noexcept;
  DiplomacySimulation(DiplomacyState &&) = delete;
  DiplomacySimulation(const DiplomacySimulation &) = delete;
  DiplomacySimulation &operator=(const DiplomacySimulation &) = delete;
  DiplomacySimulation(DiplomacySimulation &&other) noexcept;
  DiplomacySimulation &operator=(DiplomacySimulation &&other) noexcept;

  [[nodiscard]] DiplomaticContactSnapshot
  process_contact_opportunity(const FirstContactOpportunity &opportunity);
  void mark_contact_lost(int observer, std::string_view contact_id,
                         std::int64_t tick);
  void set_access_permission(int grantor, int visitor,
                             AccessPermission permission, std::int64_t tick);
  [[nodiscard]] std::int64_t assert_territorial_claim(
      int claimant, int system_id, std::int64_t tick);
  void communicate_territorial_claim(std::int64_t claim_id, int recipient,
                                     std::int64_t tick);
  void respond_to_territorial_claim(std::int64_t claim_id, int responder,
                                    TerritorialClaimResponse response,
                                    std::int64_t tick);
  void issue_border_warning(int issuer, int recipient, int system_id,
                            std::int64_t tick);
  void record_trespass(int territorial_civilization_id, int intruder,
                       int system_id, std::int64_t tick);
  [[nodiscard]] std::int64_t send_proposal(
      int proposer, int recipient, DiplomaticProposalKind kind,
      std::int64_t tick, std::string_view summary,
      std::optional<DiplomaticAgreementType> agreement_type = std::nullopt,
      std::optional<std::string_view> external_terms_reference = std::nullopt);
  void respond_to_proposal(std::int64_t proposal_id, int responder,
                           bool accept, std::int64_t tick);
  void withdraw_proposal(std::int64_t proposal_id, int proposer,
                         std::int64_t tick);
  void expire_proposal(std::int64_t proposal_id, std::int64_t tick);
  void apply_relationship_impact(int observer, int target,
                                 const RelationshipImpact &impact,
                                 std::int64_t tick);
  void set_hostile(int a, int b, std::int64_t tick, std::string_view reason);
  void declare_war(int declarer, int target, std::int64_t tick);

private:
  void apply_accepted(const detail::DiplomacyProposalState &proposal,
                      std::int64_t tick);
  void activate(int a, int b, DiplomaticAgreementType type,
                std::int64_t tick,
                std::optional<std::string> external_terms);
  void require_mutual(int a, int b) const;
  static void validate_tick(std::int64_t tick);

  DiplomacyState *state_{};
};

} // namespace stellar::core
