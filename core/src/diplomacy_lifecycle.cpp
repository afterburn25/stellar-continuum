#include <stellar/core/diplomacy_lifecycle.hpp>

#include <stellar/core/detail/diplomacy_state_access.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace stellar::core {
namespace {
using detail::DiplomacyStateAccess;

std::string range_message(std::string_view parameter) {
  return "Specified argument was out of the range of valid values. (Parameter "
         "'" +
         std::string(parameter) + "')";
}
std::string argument_message(std::string_view message,
                             std::string_view parameter) {
  return std::string(message) + " (Parameter '" + std::string(parameter) + "')";
}
std::string proposal_kind_name(DiplomaticProposalKind value) {
  switch (value) {
  case DiplomaticProposalKind::agreement:
    return "Agreement";
  case DiplomaticProposalKind::access_request:
    return "AccessRequest";
  case DiplomaticProposalKind::trade_offer:
    return "TradeOffer";
  case DiplomaticProposalKind::demand:
    return "Demand";
  case DiplomaticProposalKind::peace_offer:
    return "PeaceOffer";
  case DiplomaticProposalKind::ceasefire_offer:
    return "CeasefireOffer";
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
bool whitespace(std::uint32_t value) {
  return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
         value == 0x0085 || value == 0x00a0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000;
}
struct Scalar {
  std::uint32_t value{};
  std::size_t bytes{};
};
Scalar scalar_at(std::string_view text, std::size_t index) {
  const auto first = static_cast<unsigned char>(text[index]);
  if (first < 0x80)
    return {first, 1};
  const std::size_t count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
  if (index + count > text.size())
    return {first, 1};
  std::uint32_t value = first & (count == 2 ? 0x1f : count == 3 ? 0x0f : 7);
  for (std::size_t i = 1; i < count; ++i) {
    const auto next = static_cast<unsigned char>(text[index + i]);
    if ((next & 0xc0) != 0x80)
      return {first, 1};
    value = (value << 6) | (next & 0x3f);
  }
  return {value, count};
}
std::string trim(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size()) {
    const auto value = scalar_at(text, first);
    if (!whitespace(value.value))
      break;
    first += value.bytes;
  }
  std::size_t last_non_space = first;
  for (std::size_t index = first; index < text.size();) {
    const auto value = scalar_at(text, index);
    index += value.bytes;
    if (!whitespace(value.value))
      last_non_space = index;
  }
  return std::string(text.substr(first, last_non_space - first));
}
bool null_or_white_space(std::string_view text) { return trim(text).empty(); }
std::int64_t unchecked_subtract(std::int64_t left, std::int64_t right) {
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) -
                                   static_cast<std::uint64_t>(right));
}
} // namespace

std::int64_t DiplomacyCampaignClock::from_simulation_days(double days) {
  if (!std::isfinite(days) || days < 0)
    throw DiplomacyArgumentRangeError(range_message("simulationDays"));
  const auto scaled = std::floor(days * ticks_per_simulation_day);
  return scaled >= static_cast<double>(std::numeric_limits<std::int64_t>::max())
             ? std::numeric_limits<std::int64_t>::max()
             : static_cast<std::int64_t>(scaled);
}

std::int64_t DiplomacyCampaignClock::ticks_for_whole_days(std::int64_t days) {
  if (days <= 0)
    throw DiplomacyArgumentRangeError(range_message("days"));
  if (days >
      std::numeric_limits<std::int64_t>::max() / ticks_per_simulation_day)
    return std::numeric_limits<std::int64_t>::max();
  return days * ticks_per_simulation_day;
}

#define STELLAR_MOVE_SERVICE(Type)                                             \
  Type::Type(DiplomacyState &state) noexcept : state_(&state) {}               \
  Type::Type(Type &&other) noexcept                                            \
      : state_(std::exchange(other.state_, nullptr)) {}                        \
  Type &Type::operator=(Type &&other) noexcept {                               \
    if (this != &other)                                                        \
      state_ = std::exchange(other.state_, nullptr);                           \
    return *this;                                                              \
  }

STELLAR_MOVE_SERVICE(DiplomaticContactAgingService)

DiplomaticContactAgingResult
DiplomaticContactAgingService::review(std::int64_t now_tick,
                                      std::int64_t stale_after_ticks) {
  if (now_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("nowTick"));
  if (stale_after_ticks <= 0)
    throw DiplomacyArgumentRangeError(range_message("staleAfterTicks"));
  const auto snapshot = state_->snapshot();
  int reviewed = 0;
  int newly_stale = 0;
  for (const auto &contact : snapshot.contacts) {
    ++reviewed;
    if (now_tick < contact.last_observed_tick)
      throw DiplomacyOperationError(
          "Contact aging review cannot precede the latest observation.");
    if (contact.condition == ContactCondition::stale_or_lost ||
        contact.communication_available)
      continue;
    const auto age = unchecked_subtract(now_tick, contact.last_observed_tick);
    if (age < stale_after_ticks)
      continue;
    auto *mutable_contact = DiplomacyStateAccess::mutable_contact(
        *state_, contact.observer_civilization_id, contact.contact_id);
    if (!mutable_contact)
      throw DiplomacyOperationError(
          "Diplomatic contact changed during a scheduled aging review.");
    mutable_contact->condition = ContactCondition::stale_or_lost;
    mutable_contact->can_communicate = false;
    DiplomacyStateAccess::record(
        *state_, now_tick, DiplomaticEventKind::contact_lost,
        contact.observer_civilization_id, contact.target_civilization_id,
        contact.last_observed_system_id,
        "Contact " + contact.contact_id + " became stale after " +
            std::to_string(age) + " ticks without a fresh observation.",
        {contact.observer_civilization_id});
    ++newly_stale;
  }
  return {reviewed, newly_stale};
}

STELLAR_MOVE_SERVICE(DiplomaticProposalLifecycleService)

DiplomaticProposalLifecycleReviewResult
DiplomaticProposalLifecycleService::review(
    std::int64_t now_tick, std::int64_t default_lifetime_ticks,
    std::span<const DiplomaticProposalLifetimeOverride> lifetime_by_kind) {
  if (now_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("nowTick"));
  if (default_lifetime_ticks <= 0)
    throw DiplomacyArgumentRangeError(range_message("defaultLifetimeTicks"));
  for (const auto &item : lifetime_by_kind) {
    if (item.lifetime_ticks <= 0)
      throw DiplomacyArgumentRangeError(argument_message(
          "Proposal lifetime for " + proposal_kind_name(item.kind) +
              " must be positive.",
          "lifetimeByKind"));
  }
  std::unordered_set<int> keys;
  for (const auto &item : lifetime_by_kind) {
    if (!keys.insert(static_cast<int>(item.kind)).second)
      throw DiplomacyArgumentError(
          "Duplicate proposal lifetime override kind.");
  }
  const auto snapshot = state_->snapshot();
  int reviewed = 0;
  int expired = 0;
  DiplomacySimulation diplomacy(*state_);
  for (const auto &proposal : snapshot.proposals) {
    if (proposal.status != DiplomaticProposalStatus::pending)
      continue;
    ++reviewed;
    if (now_tick < proposal.created_at_tick)
      throw DiplomacyOperationError(
          "Proposal lifecycle review cannot precede proposal creation.");
    auto lifetime = default_lifetime_ticks;
    for (const auto &item : lifetime_by_kind) {
      if (item.kind == proposal.kind) {
        lifetime = item.lifetime_ticks;
        break;
      }
    }
    if (unchecked_subtract(now_tick, proposal.created_at_tick) < lifetime)
      continue;
    diplomacy.expire_proposal(proposal.proposal_id, now_tick);
    ++expired;
  }
  return {reviewed, expired};
}

STELLAR_MOVE_SERVICE(DiplomaticCommunicationService)

void DiplomaticCommunicationService::establish_mutual_communication(
    int civilization_a, int civilization_b, std::int64_t tick) {
  if (civilization_a == civilization_b)
    throw DiplomacyArgumentError(
        "Diplomatic communication requires two different civilizations.");
  if (civilization_a < 0)
    throw DiplomacyArgumentRangeError(range_message("civilizationA"));
  if (civilization_b < 0)
    throw DiplomacyArgumentRangeError(range_message("civilizationB"));
  if (tick < 0)
    throw DiplomacyArgumentRangeError(range_message("tick"));
  auto find = [&](int observer, int target) {
    const auto view = state_->build_view_for(observer);
    std::optional<DiplomaticContactView> result;
    for (const auto &candidate : view.contacts) {
      if (candidate.target_civilization_id == target &&
          (!result ||
           candidate.last_observed_tick > result->last_observed_tick))
        result = candidate;
    }
    if (!result || result->awareness < ContactAwareness::identified)
      throw DiplomacyOperationError(
          "Mutual communication requires each civilization to have "
          "legitimately identified the other.");
    if (result->condition == ContactCondition::stale_or_lost)
      throw DiplomacyOperationError(
          "Stale or lost contact must be reacquired before communication can "
          "be established.");
    return *result;
  };
  const auto contact_a = find(civilization_a, civilization_b);
  const auto contact_b = find(civilization_b, civilization_a);
  if (tick < contact_a.last_observed_tick ||
      tick < contact_b.last_observed_tick)
    throw DiplomacyOperationError(
        "Communication cannot be established before either side's latest "
        "observation.");
  DiplomacySimulation diplomacy(*state_);
  auto promote = [&](const DiplomaticContactView &contact, int observer,
                     int target) {
    (void)diplomacy.process_contact_opportunity(
        {observer, contact.contact_id, target, tick,
         contact.last_observed_system_id,
         ContactAwareness::communication_available, contact.condition, true,
         contact.confidence});
  };
  promote(contact_a, civilization_a, civilization_b);
  promote(contact_b, civilization_b, civilization_a);
}

STELLAR_MOVE_SERVICE(DiplomaticAgreementTerminationService)

DiplomaticAgreementTerminationResult
DiplomaticAgreementTerminationService::terminate(std::int64_t agreement_id,
                                                 int requester_civilization_id,
                                                 std::int64_t tick,
                                                 std::string_view reason) {
  const std::string owned_reason(reason);
  if (agreement_id <= 0)
    throw DiplomacyArgumentRangeError(range_message("agreementId"));
  if (requester_civilization_id < 0)
    throw DiplomacyArgumentRangeError(range_message("requesterCivilizationId"));
  if (tick < 0)
    throw DiplomacyArgumentRangeError(range_message("tick"));
  if (null_or_white_space(owned_reason))
    throw DiplomacyArgumentError(
        argument_message("Agreement termination requires a reason.", "reason"));
  const auto all = state_->snapshot();
  const auto found =
      std::ranges::find_if(all.agreements, [&](const auto &item) {
        return item.agreement_id == agreement_id;
      });
  if (found == all.agreements.end())
    throw DiplomacyOperationError("Unknown diplomatic agreement.");
  const auto snapshot = *found;
  if (requester_civilization_id != snapshot.civilization_a_id &&
      requester_civilization_id != snapshot.civilization_b_id)
    throw DiplomacyOperationError(
        "Only an agreement participant may terminate it.");
  if (snapshot.status == DiplomaticAgreementStatus::terminated)
    return {agreement_id, snapshot.type, requester_civilization_id,
            false,        false,         false};
  if (tick < snapshot.started_at_tick)
    throw DiplomacyOperationError(
        "Agreement termination cannot precede activation.");
  if (!DiplomacyStateAccess::has_mutual_communication(
          *state_, snapshot.civilization_a_id, snapshot.civilization_b_id))
    throw DiplomacyOperationError(
        "Agreement termination requires current two-way communication.");
  const auto pair = detail::DiplomacyCivilizationPair::create(
      snapshot.civilization_a_id, snapshot.civilization_b_id);
  detail::DiplomacyAgreementState *agreement = nullptr;
  for (auto *candidate :
       DiplomacyStateAccess::active_agreements(*state_, pair)) {
    if (candidate->id == agreement_id) {
      if (agreement)
        throw DiplomacyOperationError(
            "Active agreement state changed before termination.");
      agreement = candidate;
    }
  }
  if (!agreement)
    throw DiplomacyOperationError(
        "Active agreement state changed before termination.");
  agreement->status = DiplomaticAgreementStatus::terminated;
  agreement->ended = tick;
  const int other = requester_civilization_id == snapshot.civilization_a_id
                        ? snapshot.civilization_b_id
                        : snapshot.civilization_a_id;
  bool cleared_access = false;
  bool resumed_hostility = false;
  if (snapshot.type == DiplomaticAgreementType::access) {
    DiplomacyStateAccess::set_access(*state_, snapshot.civilization_a_id,
                                     snapshot.civilization_b_id,
                                     AccessPermission::unspecified, tick);
    DiplomacyStateAccess::set_access(*state_, snapshot.civilization_b_id,
                                     snapshot.civilization_a_id,
                                     AccessPermission::unspecified, tick);
    cleared_access = true;
    DiplomacyStateAccess::record(
        *state_, tick, DiplomaticEventKind::access_changed,
        requester_civilization_id, other, std::nullopt,
        "Mutual access from agreement " + std::to_string(agreement_id) +
            " ended.",
        {snapshot.civilization_a_id, snapshot.civilization_b_id});
  }
  if (snapshot.type == DiplomaticAgreementType::ceasefire) {
    auto &relationship = DiplomacyStateAccess::relationship(
        *state_, snapshot.civilization_a_id, snapshot.civilization_b_id);
    if (relationship.political_state == DiplomaticPoliticalState::ceasefire) {
      relationship.political_state = DiplomaticPoliticalState::hostile;
      resumed_hostility = true;
      DiplomacyStateAccess::record(
          *state_, tick, DiplomaticEventKind::relationship_changed,
          requester_civilization_id, other, std::nullopt,
          "Ceasefire agreement " + std::to_string(agreement_id) +
              " ended; relations returned to Hostile.",
          {snapshot.civilization_a_id, snapshot.civilization_b_id});
    }
  }
  DiplomacyStateAccess::record(
      *state_, tick, DiplomaticEventKind::agreement_terminated,
      requester_civilization_id, other, std::nullopt,
      "Agreement " + std::to_string(agreement_id) + " (" +
          agreement_name(snapshot.type) +
          ") was terminated: " + trim(owned_reason),
      {snapshot.civilization_a_id, snapshot.civilization_b_id});
  return {agreement_id, snapshot.type,  requester_civilization_id,
          true,         cleared_access, resumed_hostility};
}

#undef STELLAR_MOVE_SERVICE

DiplomacyCampaignMaintenancePolicy
DiplomacyCampaignMaintenancePolicy::early_release_default() {
  return {DiplomacyCampaignClock::ticks_for_whole_days(1),
          DiplomacyCampaignClock::ticks_for_whole_days(90),
          DiplomacyCampaignClock::ticks_for_whole_days(30)};
}
void DiplomacyCampaignMaintenancePolicy::validate() const {
  if (review_interval_ticks <= 0)
    throw DiplomacyArgumentRangeError(range_message("ReviewIntervalTicks"));
  if (contact_stale_after_ticks <= 0)
    throw DiplomacyArgumentRangeError(range_message("ContactStaleAfterTicks"));
  if (proposal_lifetime_ticks <= 0)
    throw DiplomacyArgumentRangeError(range_message("ProposalLifetimeTicks"));
}
DiplomacyCampaignMaintenanceResult
DiplomacyCampaignMaintenanceResult::not_due(std::int64_t now_tick) noexcept {
  return {false, now_tick, {0, 0}, {0, 0}};
}

DiplomacyCampaignMaintenanceScheduler::DiplomacyCampaignMaintenanceScheduler(
    DiplomacyState &state,
    std::optional<DiplomacyCampaignMaintenancePolicy> policy)
    : state_(&state),
      policy_(policy.value_or(
          DiplomacyCampaignMaintenancePolicy::early_release_default())) {
  policy_.validate();
}
DiplomacyCampaignMaintenanceScheduler::DiplomacyCampaignMaintenanceScheduler(
    DiplomacyCampaignMaintenanceScheduler &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)), policy_(other.policy_),
      next_review_tick_(other.next_review_tick_),
      last_review_tick_(other.last_review_tick_),
      initialized_(other.initialized_) {}
DiplomacyCampaignMaintenanceScheduler &
DiplomacyCampaignMaintenanceScheduler::operator=(
    DiplomacyCampaignMaintenanceScheduler &&other) noexcept {
  if (this != &other) {
    state_ = std::exchange(other.state_, nullptr);
    policy_ = other.policy_;
    next_review_tick_ = other.next_review_tick_;
    last_review_tick_ = other.last_review_tick_;
    initialized_ = other.initialized_;
  }
  return *this;
}
std::int64_t
DiplomacyCampaignMaintenanceScheduler::next_review_tick() const noexcept {
  return next_review_tick_;
}
std::int64_t
DiplomacyCampaignMaintenanceScheduler::last_review_tick() const noexcept {
  return last_review_tick_;
}
void DiplomacyCampaignMaintenanceScheduler::reset(std::int64_t now_tick,
                                                  bool review_immediately) {
  if (now_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("nowTick"));
  last_review_tick_ = -1;
  next_review_tick_ =
      review_immediately
          ? now_tick
          : saturating_add(now_tick, policy_.review_interval_ticks);
  initialized_ = true;
}
void DiplomacyMaintenanceSnapshot::validate() const {
  policy.validate();
  if(next_review_tick<0||last_review_tick< -1||last_review_tick>next_review_tick||
      (!initialized&&(next_review_tick!=0||last_review_tick!=-1)))
    throw DiplomacyArgumentRangeError("Invalid diplomacy maintenance schedule.");
}
DiplomacyMaintenanceSnapshot DiplomacyCampaignMaintenanceScheduler::snapshot() const {
  return {policy_,next_review_tick_,last_review_tick_,initialized_};
}
void DiplomacyCampaignMaintenanceScheduler::restore(const DiplomacyMaintenanceSnapshot &state){
  state.validate();policy_=state.policy;next_review_tick_=state.next_review_tick;
  last_review_tick_=state.last_review_tick;initialized_=state.initialized;
}
DiplomacyCampaignMaintenanceResult
DiplomacyCampaignMaintenanceScheduler::review_if_due(std::int64_t now_tick) {
  if (now_tick < 0)
    throw DiplomacyArgumentRangeError(range_message("nowTick"));
  if (!initialized_)
    reset(now_tick, true);
  if (now_tick < next_review_tick_ || now_tick == last_review_tick_)
    return DiplomacyCampaignMaintenanceResult::not_due(now_tick);
  const auto contact = DiplomaticContactAgingService(*state_).review(
      now_tick, policy_.contact_stale_after_ticks);
  const auto proposals = DiplomaticProposalLifecycleService(*state_).review(
      now_tick, policy_.proposal_lifetime_ticks);
  last_review_tick_ = now_tick;
  next_review_tick_ = saturating_add(now_tick, policy_.review_interval_ticks);
  return {true, now_tick, contact, proposals};
}
std::int64_t
DiplomacyCampaignMaintenanceScheduler::saturating_add(std::int64_t value,
                                                      std::int64_t increment) {
  if (increment <= 0)
    throw DiplomacyArgumentRangeError(range_message("increment"));
  return value > std::numeric_limits<std::int64_t>::max() - increment
             ? std::numeric_limits<std::int64_t>::max()
             : value + increment;
}

} // namespace stellar::core
