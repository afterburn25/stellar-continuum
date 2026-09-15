#include <stellar/core/diplomacy_snapshot_invariants.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace stellar::core {
namespace {

[[noreturn]] void fail(std::string message) {
  throw DiplomacySnapshotValidationError(std::move(message));
}

bool consume_dotnet_whitespace(std::string_view &value) {
  const auto first = static_cast<unsigned char>(value.front());
  std::uint32_t code_point = first;
  std::size_t length = 1;
  if ((first & 0xe0) == 0xc0) {
    code_point = first & 0x1f;
    length = 2;
  } else if ((first & 0xf0) == 0xe0) {
    code_point = first & 0x0f;
    length = 3;
  } else if ((first & 0xf8) == 0xf0) {
    code_point = first & 0x07;
    length = 4;
  } else if (first >= 0x80) {
    return false;
  }
  if (value.size() < length)
    return false;
  for (std::size_t index = 1; index < length; ++index) {
    const auto continuation = static_cast<unsigned char>(value[index]);
    if ((continuation & 0xc0) != 0x80)
      return false;
    code_point = (code_point << 6) | (continuation & 0x3f);
  }
  value.remove_prefix(length);
  return (code_point >= 0x09 && code_point <= 0x0d) || code_point == 0x20 ||
         code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202f ||
         code_point == 0x205f || code_point == 0x3000;
}

bool blank(std::string_view value) {
  if (value.empty())
    return true;
  while (!value.empty()) {
    if (!consume_dotnet_whitespace(value))
      return false;
  }
  return true;
}

void require_civilization(int id, std::string_view label) {
  if (id < 0)
    fail(std::string(label) + " cannot be negative.");
}

template <class Enum> bool defined(Enum value);
template <> bool defined(ContactAwareness value) {
  return value >= ContactAwareness::unknown && value <= ContactAwareness::communication_available;
}
template <> bool defined(ContactCondition value) {
  return value >= ContactCondition::active && value <= ContactCondition::stale_or_lost;
}
template <> bool defined(DiplomaticPoliticalState value) {
  return value >= DiplomaticPoliticalState::unknown && value <= DiplomaticPoliticalState::ceasefire;
}
template <> bool defined(AccessPermission value) {
  return value >= AccessPermission::unspecified && value <= AccessPermission::denied;
}
template <> bool defined(DiplomaticAgreementType value) {
  return value >= DiplomaticAgreementType::peace && value <= DiplomaticAgreementType::cooperation;
}
template <> bool defined(DiplomaticAgreementStatus value) {
  return value >= DiplomaticAgreementStatus::active && value <= DiplomaticAgreementStatus::terminated;
}
template <> bool defined(DiplomaticProposalKind value) {
  return value >= DiplomaticProposalKind::agreement && value <= DiplomaticProposalKind::ceasefire_offer;
}
template <> bool defined(DiplomaticProposalStatus value) {
  return value >= DiplomaticProposalStatus::pending && value <= DiplomaticProposalStatus::expired;
}
template <> bool defined(TerritorialClaimResponse value) {
  return value >= TerritorialClaimResponse::none && value <= TerritorialClaimResponse::disputed;
}
template <> bool defined(DiplomaticEventKind value) {
  return value >= DiplomaticEventKind::contact_observed && value <= DiplomaticEventKind::war_declared;
}

template <class Enum> void require_enum(Enum value, std::string label) {
  if (!defined(value))
    fail(std::move(label) + " contains undefined enum value " +
         std::to_string(static_cast<int>(value)) + ".");
}

void require_unit(double value, std::string_view label, std::pair<int, int> pair) {
  if (!std::isfinite(value) || value < 0 || value > 1) {
    fail("Relationship " + std::to_string(pair.first) + "/" +
         std::to_string(pair.second) + " " + std::string(label) +
         " must be finite and within [0,1].");
  }
}

std::pair<int, int> canonical(int first, int second) {
  return first < second ? std::pair{first, second} : std::pair{second, first};
}

template <class T>
std::span<const T> require_array(const std::optional<std::span<const T>> &values,
                                 std::string_view label) {
  if (!values)
    fail(std::string(label) + " cannot be null in a current Diplomacy snapshot.");
  return *values;
}

std::string agreement_type_name(DiplomaticAgreementType value) {
  switch (value) {
  case DiplomaticAgreementType::peace: return "Peace";
  case DiplomaticAgreementType::non_aggression: return "NonAggression";
  case DiplomaticAgreementType::access: return "Access";
  case DiplomaticAgreementType::trade: return "Trade";
  case DiplomaticAgreementType::research_exchange: return "ResearchExchange";
  case DiplomaticAgreementType::ceasefire: return "Ceasefire";
  case DiplomaticAgreementType::cooperation: return "Cooperation";
  }
  return std::to_string(static_cast<int>(value));
}

void require_next_id(std::int64_t next, std::int64_t maximum, std::string_view label) {
  if (next <= 0 || next <= maximum) {
    fail("Next " + std::string(label) + " ID " + std::to_string(next) +
         " does not advance beyond current maximum " + std::to_string(maximum) + ".");
  }
}

} // namespace

DiplomacySnapshotValidationError::DiplomacySnapshotValidationError(std::string message)
    : std::runtime_error(std::move(message)) {}

DiplomacySnapshotValidationResult
DiplomacySnapshotInvariantValidator::validate(const DiplomacyStateSnapshot &snapshot) {
  std::vector<DiplomaticRelationshipValidationView> relationships;
  relationships.reserve(snapshot.relationships.size());
  for (const auto &value : snapshot.relationships) {
    relationships.push_back({value.civilization_a_id, value.civilization_b_id,
                             value.political_state, value.trust, value.hostility,
                             value.fear, value.respect, value.cooperation,
                             std::span<const DiplomaticGrievanceSnapshot>(value.grievances)});
  }
  std::vector<TerritorialClaimValidationView> claims;
  claims.reserve(snapshot.claims.size());
  for (const auto &value : snapshot.claims) {
    claims.push_back({value.claim_id, value.claimant_civilization_id, value.system_id,
                      value.asserted_at_tick, value.active,
                      std::span<const int>(value.known_to_civilization_ids)});
  }
  std::vector<DiplomaticHistoryEventValidationView> history;
  history.reserve(snapshot.recent_history.size());
  for (const auto &value : snapshot.recent_history) {
    history.push_back({value.event_id, value.tick, value.kind,
                       value.primary_civilization_id, value.secondary_civilization_id,
                       value.system_id, value.summary,
                       std::span<const int>(value.known_to_civilization_ids)});
  }
  return validate({std::span<const DiplomaticContactSnapshot>(snapshot.contacts),
                   std::span<const DiplomaticRelationshipValidationView>(relationships),
                   std::span<const DiplomaticAccessSnapshot>(snapshot.access_permissions),
                   std::span<const TerritorialClaimValidationView>(claims),
                   std::span<const TerritorialClaimResponseSnapshot>(snapshot.claim_responses),
                   std::span<const DiplomaticAgreementSnapshot>(snapshot.agreements),
                   std::span<const DiplomaticProposalSnapshot>(snapshot.proposals),
                   std::span<const DiplomaticHistoryEventValidationView>(history),
                   snapshot.next_claim_id, snapshot.next_agreement_id,
                   snapshot.next_proposal_id, snapshot.next_event_id});
}

DiplomacySnapshotValidationResult
DiplomacySnapshotInvariantValidator::validate(DiplomacySnapshotValidationView snapshot) {
  const auto contacts = require_array(snapshot.contacts, "Contacts");
  const auto relationships = require_array(snapshot.relationships, "Relationships");
  const auto access_permissions = require_array(snapshot.access_permissions, "AccessPermissions");
  const auto claims = require_array(snapshot.claims, "Claims");
  const auto claim_responses = require_array(snapshot.claim_responses, "ClaimResponses");
  const auto agreements = require_array(snapshot.agreements, "Agreements");
  const auto proposals = require_array(snapshot.proposals, "Proposals");
  const auto history = require_array(snapshot.recent_history, "RecentHistory");

  if (contacts.size() > DiplomacyState::max_contact_records)
    fail("Contact count " + std::to_string(contacts.size()) + " exceeds the bounded limit " +
         std::to_string(DiplomacyState::max_contact_records) + ".");
  if (proposals.size() > DiplomacyState::max_stored_proposals)
    fail("Proposal count " + std::to_string(proposals.size()) + " exceeds the bounded limit " +
         std::to_string(DiplomacyState::max_stored_proposals) + ".");
  if (history.size() > DiplomacyState::max_recent_history_events)
    fail("History count " + std::to_string(history.size()) + " exceeds the bounded limit " +
         std::to_string(DiplomacyState::max_recent_history_events) + ".");

  std::set<std::pair<int, std::string>> contact_keys;
  std::set<std::pair<int, int>> identified_pairs;
  for (const auto &contact : contacts) {
    require_civilization(contact.observer_civilization_id, "contact observer");
    if (blank(contact.contact_id))
      fail("Diplomatic contact ID cannot be blank.");
    if (!contact_keys.emplace(contact.observer_civilization_id, contact.contact_id).second)
      fail("Duplicate contact key " + std::to_string(contact.observer_civilization_id) + ":" +
           contact.contact_id + ".");
    if (contact.target_civilization_id == contact.observer_civilization_id)
      fail("Contact " + contact.contact_id + " targets its own observer civilization.");
    if (contact.target_civilization_id)
      require_civilization(*contact.target_civilization_id, "contact target");
    if (contact.first_observed_tick < 0 || contact.last_observed_tick < contact.first_observed_tick)
      fail("Contact " + contact.contact_id + " has invalid observation chronology.");
    if (contact.last_observed_system_id && *contact.last_observed_system_id < 0)
      fail("Contact " + contact.contact_id + " has a negative observed system ID.");
    require_enum(contact.awareness, "contact " + contact.contact_id + " awareness");
    require_enum(contact.condition, "contact " + contact.contact_id + " condition");
    if (contact.awareness == ContactAwareness::unknown)
      fail("Stored contact " + contact.contact_id + " cannot have Unknown awareness.");
    if (!std::isfinite(contact.confidence) || contact.confidence < 0 || contact.confidence > 1)
      fail("Contact " + contact.contact_id + " confidence must be finite and within [0,1].");
    if (contact.awareness >= ContactAwareness::identified && !contact.target_civilization_id)
      fail("Identified contact " + contact.contact_id + " has no target civilization.");
    if (contact.awareness == ContactAwareness::detected_unidentified && contact.target_civilization_id)
      fail("Unidentified contact " + contact.contact_id + " exposes a target civilization.");
    if (contact.communication_available && !contact.target_civilization_id)
      fail("Communicating contact " + contact.contact_id + " has no identified target.");
    if (contact.communication_available && contact.awareness < ContactAwareness::contact_possible)
      fail("Communicating contact " + contact.contact_id + " has insufficient awareness.");
    if (contact.communication_available && contact.condition == ContactCondition::stale_or_lost)
      fail("Stale/lost contact " + contact.contact_id + " cannot have active communication.");
    if (contact.target_civilization_id && contact.awareness >= ContactAwareness::identified)
      identified_pairs.emplace(contact.observer_civilization_id, *contact.target_civilization_id);
  }

  std::set<std::pair<int, int>> relationship_pairs;
  for (const auto &relationship : relationships) {
    require_civilization(relationship.civilization_a_id, "relationship civilization A");
    require_civilization(relationship.civilization_b_id, "relationship civilization B");
    if (relationship.civilization_a_id >= relationship.civilization_b_id)
      fail("Relationship pair " + std::to_string(relationship.civilization_a_id) + "/" +
           std::to_string(relationship.civilization_b_id) + " is not canonical.");
    const auto pair = std::pair{relationship.civilization_a_id, relationship.civilization_b_id};
    if (!relationship_pairs.insert(pair).second)
      fail("Duplicate relationship pair " + std::to_string(pair.first) + "/" +
           std::to_string(pair.second) + ".");
    if (!identified_pairs.contains(pair) &&
        !identified_pairs.contains(std::pair{pair.second, pair.first}))
      fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
           " has no legitimate identified-contact basis.");
    require_enum(relationship.political_state,
                 "relationship " + std::to_string(pair.first) + "/" +
                     std::to_string(pair.second) + " political state");
    require_unit(relationship.trust, "trust", pair);
    require_unit(relationship.hostility, "hostility", pair);
    require_unit(relationship.fear, "fear", pair);
    require_unit(relationship.respect, "respect", pair);
    require_unit(relationship.cooperation, "cooperation", pair);
    const auto grievances = require_array(
        relationship.grievances,
        "relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
            " grievances");
    if (grievances.size() > DiplomacyState::max_grievances_per_relationship)
      fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
           " exceeds the bounded grievance limit " +
           std::to_string(DiplomacyState::max_grievances_per_relationship) + ".");
    for (const auto &grievance : grievances) {
      if (grievance.created_at_tick < 0)
        fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
             " has a grievance with a negative creation tick.");
      if (grievance.source_civilization_id != pair.first &&
          grievance.source_civilization_id != pair.second)
        fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
             " has a grievance sourced from an unrelated civilization.");
      if (!std::isfinite(grievance.severity) || grievance.severity < 0 || grievance.severity > 1)
        fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
             " has an invalid grievance severity.");
      if (blank(grievance.reason))
        fail("Relationship " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
             " has a grievance without a reason.");
    }
  }

  std::set<std::pair<int, int>> access_keys;
  for (const auto &access : access_permissions) {
    require_civilization(access.grantor_civilization_id, "access grantor");
    require_civilization(access.visitor_civilization_id, "access visitor");
    if (access.grantor_civilization_id == access.visitor_civilization_id)
      fail("Access permission cannot target the same civilization as its grantor.");
    if (!access_keys.emplace(access.grantor_civilization_id, access.visitor_civilization_id).second)
      fail("Duplicate access permission " + std::to_string(access.grantor_civilization_id) + "->" +
           std::to_string(access.visitor_civilization_id) + ".");
    require_enum(access.permission, "access permission");
    if (access.updated_at_tick < 0)
      fail("Access permission has a negative update tick.");
    if (!relationship_pairs.contains(canonical(access.grantor_civilization_id,
                                               access.visitor_civilization_id)))
      fail("Access permission " + std::to_string(access.grantor_civilization_id) + "->" +
           std::to_string(access.visitor_civilization_id) + " has no diplomatic relationship.");
  }

  std::set<std::int64_t> claim_ids;
  std::map<std::int64_t, TerritorialClaimValidationView> claim_by_id;
  for (const auto &claim : claims) {
    if (claim.claim_id <= 0 || !claim_ids.insert(claim.claim_id).second)
      fail("Territorial claim ID " + std::to_string(claim.claim_id) +
           " is invalid or duplicated.");
    require_civilization(claim.claimant_civilization_id, "claimant civilization");
    if (claim.system_id < 0 || claim.asserted_at_tick < 0)
      fail("Territorial claim " + std::to_string(claim.claim_id) +
           " has invalid system/tick data.");
    const auto known = require_array(claim.known_to_civilization_ids,
                                     "claim " + std::to_string(claim.claim_id) + " audience");
    std::set<int> distinct;
    for (const int id : known)
      if (id < 0 || !distinct.insert(id).second)
        fail("Territorial claim " + std::to_string(claim.claim_id) +
             " has an invalid or duplicate audience.");
    if (std::find(known.begin(), known.end(), claim.claimant_civilization_id) == known.end())
      fail("Territorial claim " + std::to_string(claim.claim_id) +
           " is not known to its claimant.");
    if (!std::is_sorted(known.begin(), known.end()))
      fail("Territorial claim " + std::to_string(claim.claim_id) +
           " audience is not canonical/sorted.");
    claim_by_id.emplace(claim.claim_id, claim);
  }

  std::set<std::pair<std::int64_t, int>> response_keys;
  for (const auto &response : claim_responses) {
    const auto found = claim_by_id.find(response.claim_id);
    if (found == claim_by_id.end())
      fail("Claim response references unknown claim " + std::to_string(response.claim_id) + ".");
    const auto &claim = found->second;
    require_civilization(response.responding_civilization_id, "claim responder");
    if (response.responding_civilization_id == claim.claimant_civilization_id)
      fail("Claim " + std::to_string(response.claim_id) +
           " claimant cannot respond to its own claim.");
    const auto known = *claim.known_to_civilization_ids;
    if (std::find(known.begin(), known.end(), response.responding_civilization_id) == known.end())
      fail("Claim response for " + std::to_string(response.claim_id) +
           " comes from a civilization that does not know the claim.");
    if (!response_keys.emplace(response.claim_id, response.responding_civilization_id).second)
      fail("Duplicate response to claim " + std::to_string(response.claim_id) +
           " from civilization " + std::to_string(response.responding_civilization_id) + ".");
    require_enum(response.response, "claim " + std::to_string(response.claim_id) + " response");
    if (response.response == TerritorialClaimResponse::none)
      fail("Stored response to claim " + std::to_string(response.claim_id) + " cannot be None.");
    if (response.responded_at_tick < claim.asserted_at_tick)
      fail("Response to claim " + std::to_string(response.claim_id) + " predates the claim.");
  }

  std::set<std::int64_t> agreement_ids;
  std::set<std::tuple<int, int, DiplomaticAgreementType>> active_agreements;
  for (const auto &agreement : agreements) {
    if (agreement.agreement_id <= 0 || !agreement_ids.insert(agreement.agreement_id).second)
      fail("Agreement ID " + std::to_string(agreement.agreement_id) + " is invalid or duplicated.");
    require_civilization(agreement.civilization_a_id, "agreement civilization A");
    require_civilization(agreement.civilization_b_id, "agreement civilization B");
    if (agreement.civilization_a_id >= agreement.civilization_b_id)
      fail("Agreement " + std::to_string(agreement.agreement_id) + " pair is not canonical.");
    const auto pair = std::pair{agreement.civilization_a_id, agreement.civilization_b_id};
    if (!relationship_pairs.contains(pair))
      fail("Agreement " + std::to_string(agreement.agreement_id) +
           " has no diplomatic relationship.");
    require_enum(agreement.type, "agreement " + std::to_string(agreement.agreement_id) + " type");
    require_enum(agreement.status,
                 "agreement " + std::to_string(agreement.agreement_id) + " status");
    if (agreement.started_at_tick < 0)
      fail("Agreement " + std::to_string(agreement.agreement_id) + " has a negative start tick.");
    if (agreement.status == DiplomaticAgreementStatus::active && agreement.ended_at_tick)
      fail("Active agreement " + std::to_string(agreement.agreement_id) + " has an end tick.");
    if (agreement.status == DiplomaticAgreementStatus::terminated &&
        (!agreement.ended_at_tick || *agreement.ended_at_tick < agreement.started_at_tick))
      fail("Terminated agreement " + std::to_string(agreement.agreement_id) +
           " lacks valid end chronology.");
    if (agreement.status == DiplomaticAgreementStatus::active &&
        !active_agreements.emplace(pair.first, pair.second, agreement.type).second)
      fail("Duplicate active " + agreement_type_name(agreement.type) + " agreement for pair " +
           std::to_string(pair.first) + "/" + std::to_string(pair.second) + ".");
    if (agreement.type == DiplomaticAgreementType::trade &&
        (!agreement.external_terms_reference || blank(*agreement.external_terms_reference)))
      fail("Trade agreement " + std::to_string(agreement.agreement_id) +
           " has no external economy/logistics terms reference.");
  }

  std::set<std::int64_t> proposal_ids;
  std::map<std::pair<int, int>, int> pending_by_pair;
  for (const auto &proposal : proposals) {
    if (proposal.proposal_id <= 0 || !proposal_ids.insert(proposal.proposal_id).second)
      fail("Proposal ID " + std::to_string(proposal.proposal_id) + " is invalid or duplicated.");
    require_civilization(proposal.proposer_civilization_id, "proposal proposer");
    require_civilization(proposal.recipient_civilization_id, "proposal recipient");
    if (proposal.proposer_civilization_id == proposal.recipient_civilization_id)
      fail("Proposal " + std::to_string(proposal.proposal_id) + " targets its proposer.");
    const auto pair = canonical(proposal.proposer_civilization_id,
                                proposal.recipient_civilization_id);
    if (!relationship_pairs.contains(pair))
      fail("Proposal " + std::to_string(proposal.proposal_id) +
           " has no diplomatic relationship.");
    require_enum(proposal.kind, "proposal " + std::to_string(proposal.proposal_id) + " kind");
    require_enum(proposal.status,
                 "proposal " + std::to_string(proposal.proposal_id) + " status");
    if (proposal.agreement_type)
      require_enum(*proposal.agreement_type,
                   "proposal " + std::to_string(proposal.proposal_id) + " agreement type");
    if ((proposal.kind == DiplomaticProposalKind::agreement) != proposal.agreement_type.has_value())
      fail("Proposal " + std::to_string(proposal.proposal_id) +
           " has inconsistent agreement-type data.");
    if (proposal.kind == DiplomaticProposalKind::trade_offer &&
        (!proposal.external_terms_reference || blank(*proposal.external_terms_reference)))
      fail("Trade proposal " + std::to_string(proposal.proposal_id) +
           " has no external economy/logistics terms reference.");
    if (proposal.created_at_tick < 0 || blank(proposal.summary))
      fail("Proposal " + std::to_string(proposal.proposal_id) + " has invalid creation data.");
    if (proposal.status == DiplomaticProposalStatus::pending) {
      if (proposal.resolved_at_tick)
        fail("Pending proposal " + std::to_string(proposal.proposal_id) +
             " already has a resolution tick.");
      if (++pending_by_pair[pair] > static_cast<int>(DiplomacyState::max_pending_proposals_per_pair))
        fail("Pair " + std::to_string(pair.first) + "/" + std::to_string(pair.second) +
             " exceeds the pending-proposal bound.");
    } else if (!proposal.resolved_at_tick ||
               *proposal.resolved_at_tick < proposal.created_at_tick) {
      fail("Resolved proposal " + std::to_string(proposal.proposal_id) +
           " lacks valid resolution chronology.");
    }
  }

  std::set<std::int64_t> history_ids;
  std::int64_t previous_event_id{};
  for (const auto &event : history) {
    if (event.event_id <= 0 || !history_ids.insert(event.event_id).second)
      fail("History event ID " + std::to_string(event.event_id) + " is invalid or duplicated.");
    if (event.event_id <= previous_event_id)
      fail("Diplomatic history event IDs are not in canonical insertion order.");
    previous_event_id = event.event_id;
    if (event.tick < 0 || blank(event.summary))
      fail("History event " + std::to_string(event.event_id) + " has invalid tick/summary data.");
    require_enum(event.kind, "history event " + std::to_string(event.event_id) + " kind");
    require_civilization(event.primary_civilization_id, "history primary civilization");
    if (event.secondary_civilization_id) {
      require_civilization(*event.secondary_civilization_id, "history secondary civilization");
      if (*event.secondary_civilization_id == event.primary_civilization_id)
        fail("History event " + std::to_string(event.event_id) +
             " has the same primary and secondary civilization.");
    }
    if (event.system_id && *event.system_id < 0)
      fail("History event " + std::to_string(event.event_id) + " has a negative system ID.");
    const auto audience = require_array(
        event.known_to_civilization_ids,
        "history event " + std::to_string(event.event_id) + " audience");
    std::set<int> distinct;
    for (const int id : audience)
      if (id < 0 || !distinct.insert(id).second)
        fail("History event " + std::to_string(event.event_id) +
             " has an invalid or duplicate audience.");
    if (std::find(audience.begin(), audience.end(), event.primary_civilization_id) == audience.end())
      fail("History event " + std::to_string(event.event_id) +
           " is not known to its primary civilization.");
    if (!std::is_sorted(audience.begin(), audience.end()))
      fail("History event " + std::to_string(event.event_id) +
           " audience is not canonical/sorted.");
  }

  require_next_id(snapshot.next_claim_id, claim_ids.empty() ? 0 : *claim_ids.rbegin(), "claim");
  require_next_id(snapshot.next_agreement_id,
                  agreement_ids.empty() ? 0 : *agreement_ids.rbegin(), "agreement");
  require_next_id(snapshot.next_proposal_id,
                  proposal_ids.empty() ? 0 : *proposal_ids.rbegin(), "proposal");
  require_next_id(snapshot.next_event_id,
                  history_ids.empty() ? 0 : *history_ids.rbegin(), "history event");

  return {static_cast<int>(contacts.size()), static_cast<int>(relationships.size()),
          static_cast<int>(access_permissions.size()), static_cast<int>(claims.size()),
          static_cast<int>(claim_responses.size()), static_cast<int>(agreements.size()),
          static_cast<int>(proposals.size()), static_cast<int>(history.size())};
}

} // namespace stellar::core
