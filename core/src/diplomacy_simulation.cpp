#include <stellar/core/diplomacy_simulation.hpp>

#include <stellar/core/detail/diplomacy_state_access.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace stellar::core {
namespace {
using detail::DiplomacyStateAccess;
constexpr std::size_t max_grievances = 16;

std::string range_message(std::string_view parameter) {
  return "Specified argument was out of the range of valid values. (Parameter "
         "'" +
         std::string(parameter) + "')";
}

std::string argument_message(std::string_view message,
                             std::string_view parameter) {
  return std::string(message) + " (Parameter '" + std::string(parameter) + "')";
}

bool unicode_whitespace(std::uint32_t value) {
  return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
         value == 0x0085 || value == 0x00a0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000;
}

bool null_or_white_space(std::string_view text) {
  if (text.empty())
    return true;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index++]);
    std::uint32_t value = first;
    std::size_t count = 0;
    if (first >= 0x80) {
      count = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
      value = first & (count == 1 ? 0x1f : count == 2 ? 0x0f : 7);
      if (index + count > text.size())
        return false;
      for (std::size_t offset = 0; offset < count; ++offset) {
        const auto next = static_cast<unsigned char>(text[index++]);
        if ((next & 0xc0) != 0x80)
          return false;
        value = (value << 6) | (next & 0x3f);
      }
    }
    if (!unicode_whitespace(value))
      return false;
  }
  return true;
}

std::string awareness_name(ContactAwareness value) {
  switch (value) {
  case ContactAwareness::unknown:
    return "Unknown";
  case ContactAwareness::detected_unidentified:
    return "DetectedUnidentified";
  case ContactAwareness::identified:
    return "Identified";
  case ContactAwareness::contact_possible:
    return "ContactPossible";
  case ContactAwareness::contact_established:
    return "ContactEstablished";
  case ContactAwareness::communication_available:
    return "CommunicationAvailable";
  }
  return std::to_string(static_cast<int>(value));
}

std::string condition_name(ContactCondition value) {
  switch (value) {
  case ContactCondition::active:
    return "Active";
  case ContactCondition::hostile:
    return "Hostile";
  case ContactCondition::stale_or_lost:
    return "StaleOrLost";
  }
  return std::to_string(static_cast<int>(value));
}

std::string access_name(AccessPermission value) {
  switch (value) {
  case AccessPermission::unspecified:
    return "Unspecified";
  case AccessPermission::granted:
    return "Granted";
  case AccessPermission::denied:
    return "Denied";
  }
  return std::to_string(static_cast<int>(value));
}

std::string agreement_name(DiplomaticAgreementType value) {
  switch (value) {
  case DiplomaticAgreementType::peace:
    return "Peace";
  case DiplomaticAgreementType::non_aggression:
    return "NonAggression";
  case DiplomaticAgreementType::access:
    return "Access";
  case DiplomaticAgreementType::trade:
    return "Trade";
  case DiplomaticAgreementType::research_exchange:
    return "ResearchExchange";
  case DiplomaticAgreementType::ceasefire:
    return "Ceasefire";
  case DiplomaticAgreementType::cooperation:
    return "Cooperation";
  }
  return std::to_string(static_cast<int>(value));
}

std::string response_name(TerritorialClaimResponse value) {
  switch (value) {
  case TerritorialClaimResponse::none:
    return "none";
  case TerritorialClaimResponse::recognized:
    return "recognized";
  case TerritorialClaimResponse::disputed:
    return "disputed";
  }
  return std::to_string(static_cast<int>(value));
}

double clamp(double value) { return std::clamp(value, 0.0, 1.0); }
} // namespace

DiplomacySimulation::DiplomacySimulation(DiplomacyState &state) noexcept
    : state_(&state) {}

DiplomacySimulation::DiplomacySimulation(DiplomacySimulation &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}

DiplomacySimulation &
DiplomacySimulation::operator=(DiplomacySimulation &&other) noexcept {
  if (this != &other)
    state_ = std::exchange(other.state_, nullptr);
  return *this;
}

DiplomaticContactSnapshot DiplomacySimulation::process_contact_opportunity(
    const FirstContactOpportunity &opportunity) {
  opportunity.validate();
  auto &contact = DiplomacyStateAccess::upsert_contact(*state_, opportunity);
  if (contact.target &&
      contact.awareness >= ContactAwareness::contact_established)
    (void)DiplomacyStateAccess::relationship(*state_, contact.observer,
                                             *contact.target);
  const auto kind = contact.can_communicate
                        ? DiplomaticEventKind::communication_available
                    : contact.awareness >= ContactAwareness::contact_established
                        ? DiplomaticEventKind::contact_established
                        : DiplomaticEventKind::contact_observed;
  auto summary = "Observer " + std::to_string(contact.observer) + " recorded ";
  summary += contact.target ? "civilization " + std::to_string(*contact.target)
                            : "an unidentified contact";
  summary += " as " + awareness_name(contact.awareness) + " (" +
             condition_name(contact.condition) + ", confidence " +
             detail::legacy_custom_fixed(contact.confidence, 2, 2) + ").";
  DiplomacyStateAccess::record(*state_, opportunity.observed_at_tick, kind,
                               opportunity.observer_civilization_id,
                               contact.target, opportunity.observed_system_id,
                               std::move(summary),
                               {opportunity.observer_civilization_id});
  return contact.snapshot();
}

void DiplomacySimulation::mark_contact_lost(int observer,
                                            std::string_view contact_id,
                                            std::int64_t tick) {
  const std::string owned_id(contact_id);
  validate_tick(tick);
  auto *contact =
      DiplomacyStateAccess::mutable_contact(*state_, observer, owned_id);
  if (!contact)
    throw DiplomacyOperationError("Unknown contact.");
  if (tick < contact->last_tick)
    throw DiplomacyOperationError(
        "Contact loss cannot precede latest observation.");
  contact->last_tick = tick;
  contact->condition = ContactCondition::stale_or_lost;
  contact->can_communicate = false;
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::contact_lost, observer,
      contact->target, contact->system_id,
      "Contact " + owned_id + " became stale or was lost.", {observer});
}

void DiplomacySimulation::set_access_permission(int grantor, int visitor,
                                                AccessPermission permission,
                                                std::int64_t tick) {
  validate_tick(tick);
  if (grantor == visitor)
    throw DiplomacyArgumentError(
        "Access applies between different civilizations.");
  require_mutual(grantor, visitor);
  DiplomacyStateAccess::set_access(*state_, grantor, visitor, permission, tick);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::access_changed, grantor, visitor,
      std::nullopt,
      "Civilization " + std::to_string(grantor) +
          " set access for civilization " + std::to_string(visitor) + " to " +
          access_name(permission) + ".",
      {grantor, visitor});
}

std::int64_t DiplomacySimulation::assert_territorial_claim(int claimant,
                                                           int system_id,
                                                           std::int64_t tick) {
  validate_tick(tick);
  if (claimant < 0)
    throw DiplomacyArgumentRangeError(range_message("claimant"));
  if (system_id < 0)
    throw DiplomacyArgumentRangeError(range_message("systemId"));
  auto &claim =
      DiplomacyStateAccess::create_claim(*state_, claimant, system_id, tick);
  DiplomacyStateAccess::record(*state_, tick,
                               DiplomaticEventKind::claim_asserted, claimant,
                               std::nullopt, system_id,
                               "Civilization " + std::to_string(claimant) +
                                   " asserted a political claim over system " +
                                   std::to_string(system_id) + ".",
                               {claimant});
  return claim.id;
}

void DiplomacySimulation::communicate_territorial_claim(std::int64_t claim_id,
                                                        int recipient,
                                                        std::int64_t tick) {
  validate_tick(tick);
  auto &claim = DiplomacyStateAccess::claim(*state_, claim_id);
  require_mutual(claim.claimant, recipient);
  if (std::ranges::find(claim.known_to, recipient) == claim.known_to.end())
    claim.known_to.push_back(recipient);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::claim_communicated, claim.claimant,
      recipient, claim.system_id,
      "Civilization " + std::to_string(claim.claimant) +
          " communicated claim " + std::to_string(claim.id) + " over system " +
          std::to_string(claim.system_id) + ".",
      {claim.claimant, recipient});
}

void DiplomacySimulation::respond_to_territorial_claim(
    std::int64_t claim_id, int responder, TerritorialClaimResponse response,
    std::int64_t tick) {
  validate_tick(tick);
  if (response == TerritorialClaimResponse::none)
    throw DiplomacyArgumentError(
        argument_message("Use recognition or dispute.", "response"));
  auto &claim = DiplomacyStateAccess::claim(*state_, claim_id);
  if (std::ranges::find(claim.known_to, responder) == claim.known_to.end())
    throw DiplomacyOperationError("Cannot respond to an unknown claim.");
  require_mutual(claim.claimant, responder);
  DiplomacyStateAccess::respond_to_claim(*state_, claim_id, responder, response,
                                         tick);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::claim_responded, responder,
      claim.claimant, claim.system_id,
      "Civilization " + std::to_string(responder) + " " +
          response_name(response) + " claim " + std::to_string(claim_id) + ".",
      {responder, claim.claimant});
}

void DiplomacySimulation::issue_border_warning(int issuer, int recipient,
                                               int system_id,
                                               std::int64_t tick) {
  validate_tick(tick);
  require_mutual(issuer, recipient);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::border_warning_issued, issuer,
      recipient, system_id,
      "Civilization " + std::to_string(issuer) + " warned civilization " +
          std::to_string(recipient) +
          " against unauthorized presence in system " +
          std::to_string(system_id) + ".",
      {issuer, recipient});
}

void DiplomacySimulation::record_trespass(int territorial_civilization_id,
                                          int intruder, int system_id,
                                          std::int64_t tick) {
  validate_tick(tick);
  if (!DiplomacyStateAccess::has_identified(
          *state_, territorial_civilization_id, intruder))
    throw DiplomacyOperationError("Trespass requires attributed identity.");
  if (state_->get_access_permission(territorial_civilization_id, intruder) ==
      AccessPermission::granted)
    throw DiplomacyOperationError("Authorized entry is not trespass.");
  const bool reciprocal = DiplomacyStateAccess::has_identified(
      *state_, intruder, territorial_civilization_id);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::trespass_recorded,
      territorial_civilization_id, intruder, system_id,
      "Civilization " + std::to_string(intruder) + " entered system " +
          std::to_string(system_id) +
          " without political access authorization from civilization " +
          std::to_string(territorial_civilization_id) + ".",
      reciprocal ? std::vector<int>{territorial_civilization_id, intruder}
                 : std::vector<int>{territorial_civilization_id});
}

std::int64_t DiplomacySimulation::send_proposal(
    int proposer, int recipient, DiplomaticProposalKind kind, std::int64_t tick,
    std::string_view summary,
    std::optional<DiplomaticAgreementType> agreement_type,
    std::optional<std::string_view> external_terms_reference) {
  const std::string owned_summary(summary);
  const std::optional<std::string> owned_terms =
      external_terms_reference
          ? std::optional<std::string>(*external_terms_reference)
          : std::nullopt;
  validate_tick(tick);
  if (null_or_white_space(owned_summary))
    throw DiplomacyArgumentError(
        argument_message("Proposal summary is required.", "summary"));
  require_mutual(proposer, recipient);
  if (kind == DiplomaticProposalKind::agreement && !agreement_type)
    throw DiplomacyArgumentError(argument_message(
        "Agreement proposal requires agreement type.", "agreementType"));
  if (kind != DiplomaticProposalKind::agreement && agreement_type)
    throw DiplomacyArgumentError(
        argument_message("Agreement type only applies to agreement proposals.",
                         "agreementType"));
  if (kind == DiplomaticProposalKind::trade_offer &&
      (!owned_terms || null_or_white_space(*owned_terms)))
    throw DiplomacyArgumentError(argument_message(
        "Trade offer requires economy/logistics terms reference.",
        "externalTermsReference"));
  auto &proposal = DiplomacyStateAccess::create_proposal(
      *state_, proposer, recipient, kind, agreement_type, tick, owned_summary,
      owned_terms);
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::proposal_sent, proposer, recipient,
      std::nullopt,
      "Proposal " + std::to_string(proposal.id) + ": " + owned_summary,
      {proposer, recipient});
  return proposal.id;
}

void DiplomacySimulation::respond_to_proposal(std::int64_t proposal_id,
                                              int responder, bool accept,
                                              std::int64_t tick) {
  validate_tick(tick);
  auto &proposal = DiplomacyStateAccess::proposal(*state_, proposal_id);
  if (proposal.status != DiplomaticProposalStatus::pending)
    throw DiplomacyOperationError("Proposal is resolved.");
  if (proposal.recipient != responder)
    throw DiplomacyOperationError("Only recipient may respond.");
  require_mutual(proposal.proposer, proposal.recipient);
  if (accept && proposal.kind == DiplomaticProposalKind::agreement &&
      proposal.agreement_type == DiplomaticAgreementType::non_aggression) {
    const auto relationship =
        state_->get_relationship(proposal.proposer, proposal.recipient);
    if (relationship &&
        relationship->political_state == DiplomaticPoliticalState::at_war)
      throw DiplomacyOperationError(
          "Negotiate peace or ceasefire before non-aggression.");
  }
  if (accept)
    apply_accepted(proposal, tick);
  proposal.status = accept ? DiplomaticProposalStatus::accepted
                           : DiplomaticProposalStatus::rejected;
  proposal.resolved = tick;
  DiplomacyStateAccess::record(*state_, tick,
                               accept ? DiplomaticEventKind::proposal_accepted
                                      : DiplomaticEventKind::proposal_rejected,
                               responder, proposal.proposer, std::nullopt,
                               "Proposal " + std::to_string(proposal.id) +
                                   " was " +
                                   (accept ? "accepted" : "rejected") + ".",
                               {responder, proposal.proposer});
}

void DiplomacySimulation::withdraw_proposal(std::int64_t proposal_id,
                                            int proposer, std::int64_t tick) {
  validate_tick(tick);
  auto &proposal = DiplomacyStateAccess::proposal(*state_, proposal_id);
  if (proposal.status != DiplomaticProposalStatus::pending ||
      proposal.proposer != proposer)
    throw DiplomacyOperationError(
        "Only proposer may withdraw pending proposal.");
  proposal.status = DiplomaticProposalStatus::withdrawn;
  proposal.resolved = tick;
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::proposal_withdrawn, proposer,
      proposal.recipient, std::nullopt,
      "Proposal " + std::to_string(proposal.id) + " was withdrawn.",
      {proposer, proposal.recipient});
}

void DiplomacySimulation::expire_proposal(std::int64_t proposal_id,
                                          std::int64_t tick) {
  validate_tick(tick);
  auto &proposal = DiplomacyStateAccess::proposal(*state_, proposal_id);
  if (proposal.status != DiplomaticProposalStatus::pending)
    return;
  if (tick < proposal.created)
    throw DiplomacyOperationError("Proposal cannot expire before creation.");
  proposal.status = DiplomaticProposalStatus::expired;
  proposal.resolved = tick;
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::proposal_expired, proposal.proposer,
      proposal.recipient, std::nullopt,
      "Proposal " + std::to_string(proposal.id) + " expired.",
      {proposal.proposer, proposal.recipient});
}

void DiplomacySimulation::apply_relationship_impact(
    int observer, int target, const RelationshipImpact &impact,
    std::int64_t tick) {
  const RelationshipImpact owned_impact = impact;
  validate_tick(tick);
  owned_impact.validate();
  if (!DiplomacyStateAccess::has_identified(*state_, observer, target))
    throw DiplomacyOperationError(
        "Relationship impact requires legitimately identified contact.");
  auto &relationship =
      DiplomacyStateAccess::relationship(*state_, observer, target);
  relationship.trust = clamp(relationship.trust + owned_impact.trust_delta);
  relationship.hostility =
      clamp(relationship.hostility + owned_impact.hostility_delta);
  relationship.fear = clamp(relationship.fear + owned_impact.fear_delta);
  relationship.respect =
      clamp(relationship.respect + owned_impact.respect_delta);
  relationship.cooperation =
      clamp(relationship.cooperation + owned_impact.cooperation_delta);
  if (owned_impact.grievance_severity > 0) {
    relationship.grievances.push_back(
        {tick, target, owned_impact.grievance_severity, owned_impact.reason});
    while (relationship.grievances.size() > max_grievances)
      relationship.grievances.erase(relationship.grievances.begin());
  }
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::relationship_changed, observer,
      target, std::nullopt, owned_impact.reason, {observer});
}

void DiplomacySimulation::set_hostile(int a, int b, std::int64_t tick,
                                      std::string_view reason) {
  const std::string owned_reason(reason);
  validate_tick(tick);
  if (null_or_white_space(owned_reason))
    throw DiplomacyArgumentError(
        argument_message("Hostility requires reason.", "reason"));
  if (!DiplomacyStateAccess::has_identified(*state_, a, b))
    throw DiplomacyOperationError("Hostility requires identified counterpart.");
  auto &relationship = DiplomacyStateAccess::relationship(*state_, a, b);
  if (relationship.political_state == DiplomaticPoliticalState::peace ||
      relationship.political_state == DiplomaticPoliticalState::ceasefire)
    relationship.political_state = DiplomaticPoliticalState::hostile;
  DiplomacyStateAccess::record(*state_, tick,
                               DiplomaticEventKind::relationship_changed, a, b,
                               std::nullopt, owned_reason, {a});
}

void DiplomacySimulation::declare_war(int declarer, int target,
                                      std::int64_t tick) {
  validate_tick(tick);
  if (!DiplomacyStateAccess::has_identified(*state_, declarer, target))
    throw DiplomacyOperationError(
        "War declaration cannot target a hidden civilization.");
  auto &relationship =
      DiplomacyStateAccess::relationship(*state_, declarer, target);
  if (relationship.political_state == DiplomaticPoliticalState::at_war)
    return;
  relationship.political_state = DiplomaticPoliticalState::at_war;
  const auto pair = detail::DiplomacyCivilizationPair::create(declarer, target);
  for (auto *agreement :
       DiplomacyStateAccess::active_agreements(*state_, pair)) {
    agreement->status = DiplomaticAgreementStatus::terminated;
    agreement->ended = tick;
    DiplomacyStateAccess::record(
        *state_, tick, DiplomaticEventKind::agreement_terminated, pair.first,
        pair.second, std::nullopt,
        "Agreement " + std::to_string(agreement->id) + " (" +
            agreement_name(agreement->type) + ") ended when war began.",
        {pair.first, pair.second});
  }
  DiplomacyStateAccess::set_access(*state_, declarer, target,
                                   AccessPermission::denied, tick);
  DiplomacyStateAccess::set_access(*state_, target, declarer,
                                   AccessPermission::denied, tick);
  const bool target_knows =
      DiplomacyStateAccess::has_identified(*state_, target, declarer);
  DiplomacyStateAccess::record(*state_, tick, DiplomaticEventKind::war_declared,
                               declarer, target, std::nullopt,
                               "Civilization " + std::to_string(declarer) +
                                   " declared war on civilization " +
                                   std::to_string(target) + ".",
                               target_knows ? std::vector<int>{declarer, target}
                                            : std::vector<int>{declarer});
}

void DiplomacySimulation::apply_accepted(
    const detail::DiplomacyProposalState &proposal, std::int64_t tick) {
  switch (proposal.kind) {
  case DiplomaticProposalKind::agreement:
    if (!proposal.agreement_type)
      throw DiplomacyOperationError("Nullable object must have a value.");
    activate(proposal.proposer, proposal.recipient,
             proposal.agreement_type.value(), tick, proposal.external_terms);
    break;
  case DiplomaticProposalKind::access_request:
    DiplomacyStateAccess::set_access(*state_, proposal.recipient,
                                     proposal.proposer,
                                     AccessPermission::granted, tick);
    DiplomacyStateAccess::record(
        *state_, tick, DiplomaticEventKind::access_changed, proposal.recipient,
        proposal.proposer, std::nullopt,
        "Civilization " + std::to_string(proposal.recipient) +
            " granted access to civilization " +
            std::to_string(proposal.proposer) + ".",
        {proposal.recipient, proposal.proposer});
    break;
  case DiplomaticProposalKind::trade_offer:
    activate(proposal.proposer, proposal.recipient,
             DiplomaticAgreementType::trade, tick, proposal.external_terms);
    break;
  case DiplomaticProposalKind::peace_offer:
    activate(proposal.proposer, proposal.recipient,
             DiplomaticAgreementType::peace, tick, proposal.external_terms);
    break;
  case DiplomaticProposalKind::ceasefire_offer:
    activate(proposal.proposer, proposal.recipient,
             DiplomaticAgreementType::ceasefire, tick, proposal.external_terms);
    break;
  case DiplomaticProposalKind::demand:
    break;
  default:
    throw DiplomacyArgumentRangeError(
        "Specified argument was out of the range of valid values.");
  }
}

void DiplomacySimulation::activate(int a, int b, DiplomaticAgreementType type,
                                   std::int64_t tick,
                                   std::optional<std::string> external_terms) {
  auto &relationship = DiplomacyStateAccess::relationship(*state_, a, b);
  if (type == DiplomaticAgreementType::non_aggression &&
      relationship.political_state == DiplomaticPoliticalState::at_war)
    throw DiplomacyOperationError("Non-aggression cannot replace active war.");
  auto &agreement = DiplomacyStateAccess::activate_agreement(
      *state_, a, b, type, tick, std::move(external_terms));
  if (type == DiplomaticAgreementType::peace)
    relationship.political_state = DiplomaticPoliticalState::peace;
  else if (type == DiplomaticAgreementType::ceasefire)
    relationship.political_state = DiplomaticPoliticalState::ceasefire;
  else if (type == DiplomaticAgreementType::access) {
    DiplomacyStateAccess::set_access(*state_, a, b, AccessPermission::granted,
                                     tick);
    DiplomacyStateAccess::set_access(*state_, b, a, AccessPermission::granted,
                                     tick);
  }
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::agreement_activated, a, b,
      std::nullopt,
      "Agreement " + std::to_string(agreement.id) + " (" +
          agreement_name(type) + ") became active.",
      {a, b});
}

void DiplomacySimulation::require_mutual(int a, int b) const {
  if (!DiplomacyStateAccess::has_mutual_communication(*state_, a, b))
    throw DiplomacyOperationError(
        "Diplomatic action requires current two-way communication.");
}

void DiplomacySimulation::validate_tick(std::int64_t tick) {
  if (tick < 0)
    throw DiplomacyArgumentRangeError(range_message("tick"));
}
} // namespace stellar::core
