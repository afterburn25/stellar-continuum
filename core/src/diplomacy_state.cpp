#include <stellar/core/diplomacy_state.hpp>

#include <stellar/core/detail/diplomacy_state_access.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <ranges>
#include <tuple>
#include <utility>

namespace stellar::core {
namespace {

std::string range_message(std::string_view parameter) {
  return "Specified argument was out of the range of valid values. (Parameter '" +
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

std::vector<std::uint32_t> code_points(std::string_view text) {
  std::vector<std::uint32_t> result;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index++]);
    if (first < 0x80) {
      result.push_back(first);
      continue;
    }
    std::size_t count = first < 0xe0 ? 1 : first < 0xf0 ? 2 : 3;
    std::uint32_t value = first & (count == 1 ? 0x1f : count == 2 ? 0x0f : 7);
    if (index + count > text.size()) return {};
    for (std::size_t offset = 0; offset < count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index++]);
      if ((next & 0xc0) != 0x80) return {};
      value = (value << 6) | (next & 0x3f);
    }
    result.push_back(value);
  }
  return result;
}

bool null_or_white_space(std::string_view text) {
  if (text.empty()) return true;
  const auto values = code_points(text);
  return values.empty() || std::ranges::all_of(values, unicode_whitespace);
}

std::vector<std::uint16_t> utf16(std::string_view text) {
  std::vector<std::uint16_t> result;
  for (const auto value : code_points(text)) {
    if (value <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(value));
    } else {
      const auto scalar = value - 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (scalar >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (scalar & 0x3ff)));
    }
  }
  return result;
}
bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}
double clamp01(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}
std::int64_t next_after_max(std::int64_t value, std::string_view name) {
  if (value == std::numeric_limits<std::int64_t>::max())
    throw DiplomacyOverflowError("Diplomacy " + std::string(name) +
                                 " counter overflow.");
  return value + 1;
}

} // namespace

DiplomacyArgumentRangeError::DiplomacyArgumentRangeError(std::string message)
    : std::out_of_range(std::move(message)) {}
DiplomacyArgumentError::DiplomacyArgumentError(std::string message)
    : std::invalid_argument(std::move(message)) {}
DiplomacyOperationError::DiplomacyOperationError(std::string message)
    : std::runtime_error(std::move(message)) {}
DiplomacyOverflowError::DiplomacyOverflowError(std::string message)
    : std::overflow_error(std::move(message)) {}

void FirstContactOpportunity::validate() const {
  if (observer_civilization_id < 0)
    throw DiplomacyArgumentRangeError(range_message("ObserverCivilizationId"));
  if (null_or_white_space(contact_id))
    throw DiplomacyArgumentError(
        argument_message("ContactId is required.", "ContactId"));
  if (observed_at_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("ObservedAtTick"));
  if (!std::isfinite(confidence) || confidence < 0 || confidence > 1)
    throw DiplomacyArgumentRangeError(range_message("Confidence"));
  if (awareness == ContactAwareness::unknown)
    throw DiplomacyArgumentError(argument_message(
        "An opportunity requires an observation.", "Awareness"));
  if (target_civilization_id == observer_civilization_id)
    throw DiplomacyArgumentError(argument_message(
        "Foreign contact cannot target self.", "TargetCivilizationId"));
  if (awareness >= ContactAwareness::identified && !target_civilization_id)
    throw DiplomacyArgumentError(argument_message(
        "Identified contact requires target civilization ID.",
        "TargetCivilizationId"));
  if (awareness == ContactAwareness::detected_unidentified &&
      target_civilization_id)
    throw DiplomacyArgumentError(argument_message(
        "Unidentified contact cannot expose target civilization ID.",
        "TargetCivilizationId"));
  if (communication_available &&
      (awareness < ContactAwareness::contact_possible ||
       !target_civilization_id))
    throw DiplomacyArgumentError(argument_message(
        "Communication requires identified contact capability.",
        "CommunicationAvailable"));
  if (awareness == ContactAwareness::communication_available &&
      !communication_available)
    throw DiplomacyArgumentError(argument_message(
        "CommunicationAvailable awareness requires communication capability.",
        "CommunicationAvailable"));
  if (condition == ContactCondition::stale_or_lost && communication_available)
    throw DiplomacyArgumentError(argument_message(
        "Stale/lost contact cannot communicate.", "CommunicationAvailable"));
}

void RelationshipImpact::validate() const {
  for (const auto delta : {trust_delta, hostility_delta, fear_delta,
                           respect_delta, cooperation_delta})
    if (!std::isfinite(delta) || delta < -1 || delta > 1)
      throw DiplomacyArgumentRangeError(range_message("delta"));
  if (!std::isfinite(grievance_severity) || grievance_severity < 0 ||
      grievance_severity > 1)
    throw DiplomacyArgumentRangeError(range_message("GrievanceSeverity"));
  if (null_or_white_space(reason))
    throw DiplomacyArgumentError(argument_message(
        "Relationship impact requires a gameplay reason.", "Reason"));
}

namespace detail {
DiplomacyCivilizationPair DiplomacyCivilizationPair::create(int a, int b) {
  if (a == b)
    throw DiplomacyArgumentError(
        "Diplomatic pair requires different civilizations.");
  return a < b ? DiplomacyCivilizationPair{a, b}
               : DiplomacyCivilizationPair{b, a};
}
bool DiplomacyCivilizationPair::contains(int id) const noexcept {
  return id == first || id == second;
}
int DiplomacyCivilizationPair::other(int id) const noexcept {
  return id == first ? second : first;
}
DiplomaticContactSnapshot DiplomacyContactState::snapshot() const {
  return {observer, contact_id, target, first_tick, last_tick, system_id,
          awareness, condition, can_communicate, confidence};
}
DiplomaticRelationshipSnapshot DiplomacyRelationshipState::snapshot() const {
  return {pair.first, pair.second, political_state, trust, hostility, fear,
          respect, cooperation, grievances};
}
TerritorialClaimSnapshot DiplomacyClaimState::snapshot() const {
  auto audience = known_to;
  std::ranges::sort(audience);
  audience.erase(std::unique(audience.begin(), audience.end()), audience.end());
  return {id, claimant, system_id, tick, active, std::move(audience)};
}
DiplomaticAgreementSnapshot DiplomacyAgreementState::snapshot() const {
  return {id, pair.first, pair.second, type, status, started, ended,
          external_terms};
}
DiplomaticProposalSnapshot DiplomacyProposalState::snapshot() const {
  return {id, proposer, recipient, kind, agreement_type, status, created,
          resolved, summary, external_terms};
}
} // namespace detail

struct DiplomacyState::Storage {
  std::list<detail::DiplomacyContactState> contacts;
  std::list<detail::DiplomacyRelationshipState> relationships;
  std::vector<DiplomaticAccessSnapshot> access;
  std::list<detail::DiplomacyClaimState> claims;
  std::vector<TerritorialClaimResponseSnapshot> claim_responses;
  std::list<detail::DiplomacyAgreementState> agreements;
  std::list<detail::DiplomacyProposalState> proposals;
  std::vector<DiplomaticHistoryEventSnapshot> history;
  std::int64_t next_claim{1};
  std::int64_t next_agreement{1};
  std::int64_t next_proposal{1};
  std::int64_t next_event{1};
};

DiplomacyState::DiplomacyState() : storage_(std::make_unique<Storage>()) {}
DiplomacyState::~DiplomacyState() = default;
DiplomacyState::DiplomacyState(DiplomacyState &&) noexcept = default;
DiplomacyState &DiplomacyState::operator=(DiplomacyState &&) noexcept = default;

std::optional<DiplomaticContactSnapshot>
DiplomacyState::get_contact(int observer, std::string_view id) const {
  const auto found = std::ranges::find_if(storage_->contacts, [&](const auto &x) {
    return x.observer == observer && x.contact_id == id;
  });
  return found == storage_->contacts.end()
             ? std::nullopt
             : std::optional<DiplomaticContactSnapshot>(found->snapshot());
}
std::optional<DiplomaticContactSnapshot>
DiplomacyState::get_contact(int observer, int target) const {
  const detail::DiplomacyContactState *found = nullptr;
  for (const auto &contact : storage_->contacts)
    if (contact.observer == observer && contact.target == target &&
        (!found || contact.last_tick > found->last_tick))
      found = &contact;
  return found ? std::optional<DiplomaticContactSnapshot>(found->snapshot())
               : std::nullopt;
}
std::optional<DiplomaticRelationshipSnapshot>
DiplomacyState::get_relationship(int a, int b) const {
  const auto pair = detail::DiplomacyCivilizationPair::create(a, b);
  const auto found = std::ranges::find(storage_->relationships, pair,
                                       &detail::DiplomacyRelationshipState::pair);
  return found == storage_->relationships.end()
             ? std::nullopt
             : std::optional<DiplomaticRelationshipSnapshot>(found->snapshot());
}
AccessPermission DiplomacyState::get_access_permission(int grantor,
                                                        int visitor) const noexcept {
  const auto found = std::ranges::find_if(storage_->access, [&](const auto &x) {
    return x.grantor_civilization_id == grantor &&
           x.visitor_civilization_id == visitor;
  });
  return found == storage_->access.end() ? AccessPermission::unspecified
                                         : found->permission;
}
bool DiplomacyState::is_transit_authorized(int grantor, int visitor) const {
  const auto relationship = get_relationship(grantor, visitor);
  return (!relationship || relationship->political_state !=
                               DiplomaticPoliticalState::at_war) &&
         get_access_permission(grantor, visitor) == AccessPermission::granted;
}

std::vector<TerritorialClaimSnapshot> DiplomacyState::territorial_claims(std::optional<int> observer) const {
  std::vector<TerritorialClaimSnapshot> result;
  for(const auto &value:storage_->claims) {
    auto claim=value.snapshot();
    if(!observer || std::ranges::find(claim.known_to_civilization_ids,*observer)!=claim.known_to_civilization_ids.end())
      result.push_back(std::move(claim));
  }
  std::ranges::sort(result,{},&TerritorialClaimSnapshot::claim_id);
  return result;
}
DiplomacyStateSnapshot DiplomacyState::snapshot() const {
  DiplomacyStateSnapshot result;
  for (const auto &value : storage_->contacts)
    result.contacts.push_back(value.snapshot());
  std::stable_sort(result.contacts.begin(), result.contacts.end(),
                   [](const auto &a, const auto &b) {
    return a.observer_civilization_id != b.observer_civilization_id
               ? a.observer_civilization_id < b.observer_civilization_id
               : ordinal_less(a.contact_id, b.contact_id);
  });
  for (const auto &value : storage_->relationships)
    result.relationships.push_back(value.snapshot());
  std::ranges::sort(result.relationships, {}, [](const auto &x) {
    return std::pair{x.civilization_a_id, x.civilization_b_id};
  });
  result.access_permissions = storage_->access;
  std::ranges::sort(result.access_permissions, {}, [](const auto &x) {
    return std::pair{x.grantor_civilization_id, x.visitor_civilization_id};
  });
  for (const auto &value : storage_->claims)
    result.claims.push_back(value.snapshot());
  std::ranges::sort(result.claims, {}, &TerritorialClaimSnapshot::claim_id);
  result.claim_responses = storage_->claim_responses;
  std::ranges::sort(result.claim_responses, {}, [](const auto &x) {
    return std::pair{x.claim_id, x.responding_civilization_id};
  });
  for (const auto &value : storage_->agreements)
    result.agreements.push_back(value.snapshot());
  std::ranges::sort(result.agreements, {}, &DiplomaticAgreementSnapshot::agreement_id);
  for (const auto &value : storage_->proposals)
    result.proposals.push_back(value.snapshot());
  std::ranges::sort(result.proposals, {}, &DiplomaticProposalSnapshot::proposal_id);
  result.recent_history = storage_->history;
  result.next_claim_id = storage_->next_claim;
  result.next_agreement_id = storage_->next_agreement;
  result.next_proposal_id = storage_->next_proposal;
  result.next_event_id = storage_->next_event;
  return result;
}

DiplomaticStateView DiplomacyState::build_view_for(int observer) const {
  DiplomaticStateView result;
  result.observer_civilization_id = observer;
  std::vector<int> known;
  for (const auto &value : storage_->contacts) {
    if (value.observer != observer) continue;
    result.contacts.push_back({value.contact_id, value.target, value.awareness,
                               value.condition, value.can_communicate,
                               value.confidence, value.last_tick,
                               value.system_id});
    if (value.target && value.awareness >= ContactAwareness::identified)
      known.push_back(*value.target);
  }
  std::stable_sort(result.contacts.begin(), result.contacts.end(),
                   [](const auto &a, const auto &b) {
    return ordinal_less(a.contact_id, b.contact_id);
  });
  std::ranges::sort(known);
  known.erase(std::unique(known.begin(), known.end()), known.end());
  const auto knows = [&](int id) { return std::ranges::binary_search(known, id); };
  for (const auto &value : storage_->relationships)
    if (value.pair.contains(observer) && knows(value.pair.other(observer))) {
      const auto snapshot = value.snapshot();
      result.relationships.push_back(
          {value.pair.other(observer), snapshot.political_state, snapshot.trust,
           snapshot.hostility, snapshot.fear, snapshot.respect,
           snapshot.cooperation, snapshot.grievances});
    }
  std::ranges::sort(result.relationships, {},
                    &DiplomaticRelationshipView::other_civilization_id);
  for (const auto &value : storage_->access) {
    if (value.grantor_civilization_id != observer &&
        value.visitor_civilization_id != observer)
      continue;
    const auto other = value.grantor_civilization_id == observer
                           ? value.visitor_civilization_id
                           : value.grantor_civilization_id;
    if (knows(other)) result.access_permissions.push_back(value);
  }
  std::ranges::sort(result.access_permissions, {}, [](const auto &x) {
    return std::pair{x.grantor_civilization_id, x.visitor_civilization_id};
  });
  for (const auto &value : storage_->claims)
    if (std::ranges::find(value.known_to, observer) != value.known_to.end())
      result.claims.push_back(value.snapshot());
  std::ranges::sort(result.claims, {}, &TerritorialClaimSnapshot::claim_id);
  for (const auto &value : storage_->claim_responses) {
    const auto claim = std::ranges::find(storage_->claims, value.claim_id,
                                         &detail::DiplomacyClaimState::id);
    if (claim != storage_->claims.end() &&
        std::ranges::find(claim->known_to, observer) != claim->known_to.end() &&
        (value.responding_civilization_id == observer ||
         claim->claimant == observer))
      result.claim_responses.push_back(value);
  }
  for (const auto &value : storage_->agreements)
    if (value.pair.contains(observer) && knows(value.pair.other(observer)))
      result.agreements.push_back(value.snapshot());
  std::ranges::sort(result.agreements, {}, &DiplomaticAgreementSnapshot::agreement_id);
  for (const auto &value : storage_->proposals) {
    if (value.proposer != observer && value.recipient != observer) continue;
    const auto other = value.proposer == observer ? value.recipient : value.proposer;
    if (knows(other)) result.proposals.push_back(value.snapshot());
  }
  std::ranges::sort(result.proposals, {}, &DiplomaticProposalSnapshot::proposal_id);
  for (const auto &value : storage_->history)
    if (std::ranges::find(value.known_to_civilization_ids, observer) !=
        value.known_to_civilization_ids.end())
      result.recent_events.push_back(value);
  return result;
}

DiplomacyState DiplomacyState::restore(const DiplomacyStateSnapshot &input) {
  DiplomacyState state;
  auto &s = *state.storage_;
  for (const auto &value : input.contacts | std::views::take(max_contact_records)) {
    if (value.observer_civilization_id < 0 ||
        null_or_white_space(value.contact_id))
      continue;
    s.contacts.push_back({value.observer_civilization_id, value.contact_id,
                          value.target_civilization_id,
                          std::max<std::int64_t>(0, value.first_observed_tick),
                          std::max<std::int64_t>(0, value.last_observed_tick),
                          value.last_observed_system_id, value.awareness,
                          value.condition,
                          value.communication_available &&
                              value.condition != ContactCondition::stale_or_lost,
                          clamp01(value.confidence)});
  }
  for (const auto &value : input.relationships) {
    if (value.civilization_a_id == value.civilization_b_id) continue;
    detail::DiplomacyRelationshipState restored;
    restored.pair = detail::DiplomacyCivilizationPair::create(
        value.civilization_a_id, value.civilization_b_id);
    restored.political_state = value.political_state;
    restored.trust = clamp01(value.trust);
    restored.hostility = clamp01(value.hostility);
    restored.fear = clamp01(value.fear);
    restored.respect = clamp01(value.respect);
    restored.cooperation = clamp01(value.cooperation);
    const auto start = value.grievances.size() > max_grievances_per_relationship
                           ? value.grievances.size() - max_grievances_per_relationship
                           : 0;
    for (std::size_t index = start; index < value.grievances.size(); ++index) {
      auto grievance = value.grievances[index];
      if (!std::isfinite(grievance.severity) ||
          null_or_white_space(grievance.reason))
        continue;
      grievance.severity = std::clamp(grievance.severity, 0.0, 1.0);
      restored.grievances.push_back(std::move(grievance));
    }
    const auto found = std::ranges::find(s.relationships, restored.pair,
        &detail::DiplomacyRelationshipState::pair);
    if (found == s.relationships.end()) s.relationships.push_back(std::move(restored));
    else *found = std::move(restored);
  }
  for (auto value : input.access_permissions) {
    if (value.grantor_civilization_id == value.visitor_civilization_id) continue;
    value.updated_at_tick = std::max<std::int64_t>(0, value.updated_at_tick);
    const auto found = std::ranges::find_if(s.access, [&](const auto &x) {
      return x.grantor_civilization_id == value.grantor_civilization_id &&
             x.visitor_civilization_id == value.visitor_civilization_id;
    });
    if (found == s.access.end()) s.access.push_back(value);
    else *found = value;
  }
  for (const auto &value : input.claims) {
    if (value.claim_id <= 0) continue;
    detail::DiplomacyClaimState restored{
        value.claim_id, value.claimant_civilization_id, value.system_id,
        std::max<std::int64_t>(0, value.asserted_at_tick), value.active,
        value.known_to_civilization_ids};
    restored.known_to.push_back(value.claimant_civilization_id);
    std::ranges::sort(restored.known_to);
    restored.known_to.erase(
        std::unique(restored.known_to.begin(), restored.known_to.end()),
        restored.known_to.end());
    const auto found = std::ranges::find(
        s.claims, value.claim_id, &detail::DiplomacyClaimState::id);
    if (found == s.claims.end()) s.claims.push_back(std::move(restored));
    else *found = std::move(restored);
  }
  for (auto value : input.claim_responses) {
    if (std::ranges::find(s.claims, value.claim_id,
                          &detail::DiplomacyClaimState::id) == s.claims.end())
      continue;
    value.responded_at_tick = std::max<std::int64_t>(0, value.responded_at_tick);
    const auto found = std::ranges::find_if(
        s.claim_responses, [&](const auto &x) {
          return x.claim_id == value.claim_id &&
                 x.responding_civilization_id == value.responding_civilization_id;
        });
    if (found == s.claim_responses.end()) s.claim_responses.push_back(value);
    else *found = value;
  }
  for (const auto &value : input.agreements) {
    if (value.agreement_id <= 0 ||
        value.civilization_a_id == value.civilization_b_id)
      continue;
    detail::DiplomacyAgreementState restored{
        value.agreement_id,
        detail::DiplomacyCivilizationPair::create(
            value.civilization_a_id, value.civilization_b_id),
        value.type, value.status,
        std::max<std::int64_t>(0, value.started_at_tick), value.ended_at_tick,
        value.external_terms_reference};
    const auto found = std::ranges::find(
        s.agreements, value.agreement_id, &detail::DiplomacyAgreementState::id);
    if (found == s.agreements.end()) s.agreements.push_back(std::move(restored));
    else *found = std::move(restored);
  }
  const auto proposal_start = input.proposals.size() > max_stored_proposals
                                  ? input.proposals.size() - max_stored_proposals
                                  : 0;
  for (std::size_t index = proposal_start; index < input.proposals.size();
       ++index) {
    const auto &value = input.proposals[index];
    if (value.proposal_id <= 0 ||
        value.proposer_civilization_id == value.recipient_civilization_id ||
        null_or_white_space(value.summary))
      continue;
    detail::DiplomacyProposalState restored{
        value.proposal_id, value.proposer_civilization_id,
        value.recipient_civilization_id, value.kind, value.agreement_type,
        value.status, std::max<std::int64_t>(0, value.created_at_tick),
        value.resolved_at_tick, value.summary, value.external_terms_reference};
    const auto found = std::ranges::find(
        s.proposals, value.proposal_id, &detail::DiplomacyProposalState::id);
    if (found == s.proposals.end()) s.proposals.push_back(std::move(restored));
    else *found = std::move(restored);
  }
  const auto history_start = input.recent_history.size() > max_recent_history_events
                                 ? input.recent_history.size() -
                                       max_recent_history_events
                                 : 0;
  for (std::size_t index = history_start; index < input.recent_history.size();
       ++index) {
    auto value = input.recent_history[index];
    if (value.event_id <= 0 || null_or_white_space(value.summary)) continue;
    value.tick = std::max<std::int64_t>(0, value.tick);
    std::ranges::sort(value.known_to_civilization_ids);
    value.known_to_civilization_ids.erase(
        std::unique(value.known_to_civilization_ids.begin(),
                    value.known_to_civilization_ids.end()),
        value.known_to_civilization_ids.end());
    s.history.push_back(std::move(value));
  }
  std::int64_t max_claim=0,max_agreement=0,max_proposal=0,max_event=0;
  for(const auto&value:s.claims)max_claim=std::max(max_claim,value.id);
  for(const auto&value:s.agreements)max_agreement=std::max(max_agreement,value.id);
  for(const auto&value:s.proposals)max_proposal=std::max(max_proposal,value.id);
  for(const auto&e:s.history)max_event=std::max(max_event,e.event_id);
  s.next_claim=std::max({std::int64_t{1},input.next_claim_id,next_after_max(max_claim,"claim")});
  s.next_agreement=std::max({std::int64_t{1},input.next_agreement_id,next_after_max(max_agreement,"agreement")});
  s.next_proposal=std::max({std::int64_t{1},input.next_proposal_id,next_after_max(max_proposal,"proposal")});
  s.next_event=std::max({std::int64_t{1},input.next_event_id,next_after_max(max_event,"event")});
  return state;
}

} // namespace stellar::core

namespace stellar::core::detail {

DiplomacyContactState &DiplomacyStateAccess::upsert_contact(
    DiplomacyState &state, const FirstContactOpportunity &opportunity) {
  auto &values = state.storage_->contacts;
  auto found = std::ranges::find_if(values, [&](const auto &value) {
    return value.observer == opportunity.observer_civilization_id &&
           value.contact_id == opportunity.contact_id;
  });
  if (found == values.end()) {
    if (values.size() >= DiplomacyState::max_contact_records) {
      auto stale = values.end();
      for (auto current = values.begin(); current != values.end(); ++current)
        if (current->condition == ContactCondition::stale_or_lost &&
            !current->target &&
            (stale == values.end() || current->last_tick < stale->last_tick))
          stale = current;
      if (stale == values.end())
        throw DiplomacyOperationError("Diplomatic contact storage is full.");
      values.erase(stale);
    }
    values.push_back({opportunity.observer_civilization_id,
                      opportunity.contact_id,
                      opportunity.target_civilization_id,
                      opportunity.observed_at_tick,
                      opportunity.observed_at_tick,
                      opportunity.observed_system_id,
                      opportunity.communication_available
                          ? ContactAwareness::communication_available
                          : opportunity.awareness,
                      opportunity.condition,
                      opportunity.communication_available,
                      opportunity.confidence});
    return values.back();
  }
  if (opportunity.observed_at_tick < found->last_tick)
    throw DiplomacyOperationError(
        "Contact observations cannot move backward in time.");
  if (found->target && opportunity.target_civilization_id &&
      found->target != opportunity.target_civilization_id)
    throw DiplomacyOperationError("Stable contact ID cannot be reassigned.");
  if (!found->target) found->target = opportunity.target_civilization_id;
  found->last_tick = opportunity.observed_at_tick;
  if (opportunity.observed_system_id)
    found->system_id = opportunity.observed_system_id;
  found->confidence = opportunity.confidence;
  found->condition = opportunity.condition;
  found->can_communicate =
      opportunity.condition != ContactCondition::stale_or_lost &&
      (opportunity.communication_available || found->can_communicate);
  if (opportunity.awareness > found->awareness)
    found->awareness = opportunity.awareness;
  if (found->can_communicate &&
      found->awareness < ContactAwareness::communication_available)
    found->awareness = ContactAwareness::communication_available;
  return *found;
}

DiplomacyContactState *DiplomacyStateAccess::mutable_contact(
    DiplomacyState &state, int observer, std::string_view contact_id) noexcept {
  const auto found = std::ranges::find_if(state.storage_->contacts,
      [&](const auto &value) {
        return value.observer == observer && value.contact_id == contact_id;
      });
  return found == state.storage_->contacts.end() ? nullptr : &*found;
}
bool DiplomacyStateAccess::has_identified(const DiplomacyState &state,
                                          int observer, int target) noexcept {
  return std::ranges::any_of(state.storage_->contacts, [&](const auto &value) {
    return value.observer == observer && value.target == target &&
           value.awareness >= ContactAwareness::identified;
  });
}
bool DiplomacyStateAccess::has_communication(const DiplomacyState &state,
                                             int observer, int target) noexcept {
  return std::ranges::any_of(state.storage_->contacts, [&](const auto &value) {
    return value.observer == observer && value.target == target &&
           value.can_communicate &&
           value.condition != ContactCondition::stale_or_lost;
  });
}
bool DiplomacyStateAccess::has_mutual_communication(
    const DiplomacyState &state, int a, int b) noexcept {
  return has_communication(state, a, b) && has_communication(state, b, a);
}
DiplomacyRelationshipState &DiplomacyStateAccess::relationship(
    DiplomacyState &state, int a, int b) {
  const auto pair = DiplomacyCivilizationPair::create(a, b);
  auto found = std::ranges::find(state.storage_->relationships, pair,
                                 &DiplomacyRelationshipState::pair);
  if (found == state.storage_->relationships.end()) {
    state.storage_->relationships.push_back({pair});
    return state.storage_->relationships.back();
  }
  return *found;
}
void DiplomacyStateAccess::set_access(DiplomacyState &state, int grantor,
                                      int visitor, AccessPermission permission,
                                      std::int64_t tick) {
  auto &values = state.storage_->access;
  const auto found = std::ranges::find_if(values, [&](const auto &value) {
    return value.grantor_civilization_id == grantor &&
           value.visitor_civilization_id == visitor;
  });
  const DiplomaticAccessSnapshot replacement{grantor, visitor, permission, tick};
  if (found == values.end()) values.push_back(replacement);
  else *found = replacement;
}
DiplomacyClaimState &DiplomacyStateAccess::create_claim(
    DiplomacyState &state, int claimant, int system_id, std::int64_t tick) {
  const auto existing = std::ranges::find_if(state.storage_->claims,
      [&](const auto &value) {
        return value.active && value.claimant == claimant &&
               value.system_id == system_id;
      });
  if (existing != state.storage_->claims.end()) return *existing;
  const auto id = state.storage_->next_claim;
  state.storage_->next_claim = next_after_max(id, "claim");
  state.storage_->claims.push_back(
      {id, claimant, system_id, tick, true, {claimant}});
  return state.storage_->claims.back();
}
DiplomacyClaimState &DiplomacyStateAccess::claim(DiplomacyState &state,
                                                 std::int64_t id) {
  const auto found = std::ranges::find(state.storage_->claims, id,
                                       &DiplomacyClaimState::id);
  if (found == state.storage_->claims.end() || !found->active)
    throw DiplomacyOperationError("Unknown or inactive territorial claim.");
  return *found;
}
void DiplomacyStateAccess::respond_to_claim(
    DiplomacyState &state, std::int64_t id, int responder,
    TerritorialClaimResponse response, std::int64_t tick) {
  auto &values = state.storage_->claim_responses;
  const auto found = std::ranges::find_if(values, [&](const auto &value) {
    return value.claim_id == id && value.responding_civilization_id == responder;
  });
  const TerritorialClaimResponseSnapshot replacement{id, responder, response,
                                                       tick};
  if (found == values.end()) values.push_back(replacement);
  else *found = replacement;
}
DiplomacyProposalState &DiplomacyStateAccess::create_proposal(
    DiplomacyState &state, int proposer, int recipient,
    DiplomaticProposalKind kind,
    std::optional<DiplomaticAgreementType> agreement_type, std::int64_t tick,
    std::string summary, std::optional<std::string> external_terms) {
  const auto pair = DiplomacyCivilizationPair::create(proposer, recipient);
  const auto pending = std::ranges::count_if(state.storage_->proposals,
      [&](const auto &value) {
        return value.status == DiplomaticProposalStatus::pending &&
               DiplomacyCivilizationPair::create(value.proposer,
                                                  value.recipient) == pair;
      });
  if (pending >= DiplomacyState::max_pending_proposals_per_pair)
    throw DiplomacyOperationError(
        "Too many pending proposals between these civilizations.");
  while (state.storage_->proposals.size() >=
         DiplomacyState::max_stored_proposals) {
    auto oldest = state.storage_->proposals.end();
    for (auto current = state.storage_->proposals.begin();
         current != state.storage_->proposals.end(); ++current) {
      if (current->status == DiplomaticProposalStatus::pending) continue;
      const auto current_tick = current->resolved.value_or(current->created);
      if (oldest == state.storage_->proposals.end() ||
          std::pair{current_tick, current->id} <
              std::pair{oldest->resolved.value_or(oldest->created), oldest->id})
        oldest = current;
    }
    if (oldest == state.storage_->proposals.end())
      throw DiplomacyOperationError(
          "Proposal storage is full of unresolved proposals.");
    state.storage_->proposals.erase(oldest);
  }
  const auto id = state.storage_->next_proposal;
  state.storage_->next_proposal = next_after_max(id, "proposal");
  state.storage_->proposals.push_back(
      {id, proposer, recipient, kind, agreement_type,
       DiplomaticProposalStatus::pending, tick, std::nullopt,
       std::move(summary), std::move(external_terms)});
  return state.storage_->proposals.back();
}
DiplomacyProposalState &DiplomacyStateAccess::proposal(DiplomacyState &state,
                                                       std::int64_t id) {
  const auto found = std::ranges::find(state.storage_->proposals, id,
                                       &DiplomacyProposalState::id);
  if (found == state.storage_->proposals.end())
    throw DiplomacyOperationError("Unknown diplomatic proposal.");
  return *found;
}
DiplomacyAgreementState &DiplomacyStateAccess::activate_agreement(
    DiplomacyState &state, int a, int b, DiplomaticAgreementType type,
    std::int64_t tick, std::optional<std::string> external_terms) {
  const auto pair = DiplomacyCivilizationPair::create(a, b);
  const auto existing = std::ranges::find_if(state.storage_->agreements,
      [&](const auto &value) {
        return value.pair == pair && value.type == type &&
               value.status == DiplomaticAgreementStatus::active;
      });
  if (existing != state.storage_->agreements.end()) return *existing;
  const auto id = state.storage_->next_agreement;
  state.storage_->next_agreement = next_after_max(id, "agreement");
  state.storage_->agreements.push_back(
      {id, pair, type, DiplomaticAgreementStatus::active, tick, std::nullopt,
       std::move(external_terms)});
  return state.storage_->agreements.back();
}
std::vector<DiplomacyAgreementState *> DiplomacyStateAccess::active_agreements(
    DiplomacyState &state, DiplomacyCivilizationPair pair) {
  std::vector<DiplomacyAgreementState *> result;
  for (auto &value : state.storage_->agreements)
    if (value.pair == pair && value.status == DiplomaticAgreementStatus::active)
      result.push_back(&value);
  return result;
}
void DiplomacyStateAccess::record(
    DiplomacyState &state, std::int64_t tick, DiplomaticEventKind kind,
    int primary, std::optional<int> secondary, std::optional<int> system_id,
    std::string summary, std::vector<int> audience) {
  audience.push_back(primary);
  std::erase_if(audience, [](int value) { return value < 0; });
  std::ranges::sort(audience);
  audience.erase(std::unique(audience.begin(), audience.end()), audience.end());
  const auto id = state.storage_->next_event;
  state.storage_->next_event = next_after_max(id, "event");
  state.storage_->history.push_back({id, tick, kind, primary, secondary,
                                     system_id, std::move(summary),
                                     std::move(audience)});
  while (state.storage_->history.size() >
         DiplomacyState::max_recent_history_events)
    state.storage_->history.erase(state.storage_->history.begin());
}

} // namespace stellar::core::detail
