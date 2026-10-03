#include "native_campaign_calendar.hpp"
#include "native_diplomacy_controller.hpp"

#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/empire_policy.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <iomanip>
#include <locale>
#include <type_traits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace stellar::native_diplomacy {
namespace {
using namespace stellar::core;

std::string resolve(const stellar::engine::LocalizationTable *locale,
                    std::string_view key, std::string_view fallback) {
  if (locale && locale->contains(key))
    return std::string(locale->translate(key));
  return std::string(fallback);
}

std::string resolved(const stellar::engine::LocalizationTable *locale,
                     std::string_view key,
                     std::initializer_list<std::string> args,
                     std::string_view fallback) {
  if (locale && locale->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

// Observer-command results are stable English literals. Recompose them
// through the locale table at the controller boundary so workspace notices
// stay localized without changing the authoritative results or saves.
std::string localized_command_message(
    const stellar::engine::LocalizationTable *locale,
    std::string_view message) {
  static const std::pair<std::string_view, std::string_view> messages[] = {
      {"No active diplomatic channel is available to that counterpart.",
       "DIPLOMACY_ERR_NO_CHANNEL"},
      {"No usable diplomatic contact is available to that counterpart.",
       "DIPLOMACY_ERR_NO_CONTACT"},
      {"That diplomatic action is not currently available.",
       "DIPLOMACY_ERR_ACTION"},
      {"The diplomatic request is not valid.", "DIPLOMACY_ERR_REQUEST"},
      {"That territorial claim action is not currently available.",
       "DIPLOMACY_ERR_TERRITORIAL_ACTION"},
      {"The territorial claim request is not valid.",
       "DIPLOMACY_ERR_TERRITORIAL_REQUEST"},
      {"That border warning action is not currently available.",
       "DIPLOMACY_ERR_BORDER_ACTION"},
      {"The border warning request is not valid.",
       "DIPLOMACY_ERR_BORDER_REQUEST"},
      {"Communication channel is already available.",
       "DIPLOMACY_MSG_CHANNEL_EXISTS"},
      {"Communication channel established.", "DIPLOMACY_MSG_CHANNEL_OPENED"},
      {"Proposal sent.", "DIPLOMACY_MSG_PROPOSAL_SENT"},
      {"Proposal accepted.", "DIPLOMACY_MSG_PROPOSAL_ACCEPTED"},
      {"Proposal rejected.", "DIPLOMACY_MSG_PROPOSAL_REJECTED"},
      {"Proposal withdrawn.", "DIPLOMACY_MSG_PROPOSAL_WITHDRAWN"},
      {"Access permission updated.", "DIPLOMACY_MSG_ACCESS_UPDATED"},
      {"War declared.", "DIPLOMACY_MSG_WAR_DECLARED"},
      {"Agreement was already terminated.",
       "DIPLOMACY_MSG_AGREEMENT_ALREADY_ENDED"},
      {"Agreement terminated.", "DIPLOMACY_MSG_AGREEMENT_TERMINATED"},
      {"Territorial claim was already communicated.",
       "DIPLOMACY_MSG_CLAIM_ALREADY_SENT"},
      {"Territorial claim communicated.", "DIPLOMACY_MSG_CLAIM_SENT"},
      {"Territorial claim response was already recorded.",
       "DIPLOMACY_MSG_CLAIM_RESPONSE_EXISTS"},
      {"Territorial claim recognized.", "DIPLOMACY_MSG_CLAIM_RECOGNIZED"},
      {"Territorial claim disputed.", "DIPLOMACY_MSG_CLAIM_DISPUTED"},
      {"Territorial claim is already active.",
       "DIPLOMACY_MSG_CLAIM_ACTIVE"},
      {"Territorial claim asserted.", "DIPLOMACY_MSG_CLAIM_ASSERTED"},
      {"Border warning was already issued.",
       "DIPLOMACY_MSG_WARNING_EXISTS"},
      {"Border warning issued.", "DIPLOMACY_MSG_WARNING_SENT"},
      {"Unknown empire policy.", "DIPLOMACY_ERR_POLICY_UNKNOWN"},
      {"Unknown civilization.", "DIPLOMACY_ERR_POLICY_CIVILIZATION"},
      {"That policy is already active.", "DIPLOMACY_ERR_POLICY_ACTIVE"},
      {"That policy domain was changed recently; the empire must wait "
       "before changing it again.", "DIPLOMACY_ERR_POLICY_COOLDOWN"},
      {"Policy changes cannot be applied at a negative tick.",
       "DIPLOMACY_ERR_REQUEST"},
  };
  for (const auto &[literal, key] : messages)
    if (message == literal) return resolve(locale, key, literal);
  return std::string(message);
}

// Display-name resolvers for the vocabularies players read as statuses.
std::string political_name(const stellar::engine::LocalizationTable *locale,
                           DiplomaticPoliticalState value) {
  switch (value) {
  case DiplomaticPoliticalState::unknown:
    return resolve(locale, "DIPLOMACY_STATE_UNKNOWN", "Unknown");
  case DiplomaticPoliticalState::peace:
    return resolve(locale, "DIPLOMACY_STATE_PEACE", "Peace");
  case DiplomaticPoliticalState::hostile:
    return resolve(locale, "DIPLOMACY_STATE_HOSTILE", "Hostile");
  case DiplomaticPoliticalState::at_war:
    return resolve(locale, "DIPLOMACY_STATE_AT_WAR", "AtWar");
  case DiplomaticPoliticalState::ceasefire:
    return resolve(locale, "DIPLOMACY_STATE_CEASEFIRE", "Ceasefire");
  }
  return resolve(locale, "DIPLOMACY_STATE_UNKNOWN", "Unknown");
}
std::string access_name(const stellar::engine::LocalizationTable *locale,
                        AccessPermission value) {
  switch (value) {
  case AccessPermission::unspecified:
    return resolve(locale, "DIPLOMACY_ACCESS_UNSPECIFIED", "UNSPECIFIED");
  case AccessPermission::granted:
    return resolve(locale, "DIPLOMACY_ACCESS_GRANTED", "GRANTED");
  case AccessPermission::denied:
    return resolve(locale, "DIPLOMACY_ACCESS_DENIED", "DENIED");
  }
  return resolve(locale, "DIPLOMACY_ACCESS_UNSPECIFIED", "UNSPECIFIED");
}
std::string agreement_status_name(
    const stellar::engine::LocalizationTable *locale,
    DiplomaticAgreementStatus value) {
  switch (value) {
  case DiplomaticAgreementStatus::active:
    return resolve(locale, "DIPLOMACY_AGREEMENT_ACTIVE", "ACTIVE");
  case DiplomaticAgreementStatus::terminated:
    return resolve(locale, "DIPLOMACY_AGREEMENT_TERMINATED", "TERMINATED");
  }
  return resolve(locale, "DIPLOMACY_AGREEMENT_TERMINATED", "TERMINATED");
}

std::string awareness_name(const stellar::engine::LocalizationTable *locale,
                           ContactAwareness value) {
  switch (value) {
  case ContactAwareness::unknown:
    return resolve(locale, "DIPLOMACY_AWARE_UNKNOWN", "unknown");
  case ContactAwareness::detected_unidentified:
    return resolve(locale, "DIPLOMACY_AWARE_DETECTED_UNIDENTIFIED",
                   "detected unidentified");
  case ContactAwareness::identified:
    return resolve(locale, "DIPLOMACY_AWARE_IDENTIFIED", "identified");
  case ContactAwareness::contact_possible:
    return resolve(locale, "DIPLOMACY_AWARE_CONTACT_POSSIBLE",
                   "contact possible");
  case ContactAwareness::contact_established:
    return resolve(locale, "DIPLOMACY_AWARE_CONTACT_ESTABLISHED",
                   "contact established");
  case ContactAwareness::communication_available:
    return resolve(locale, "DIPLOMACY_AWARE_COMMUNICATION",
                   "communication available");
  }
  return resolve(locale, "DIPLOMACY_AWARE_UNKNOWN", "unknown");
}
std::string condition_name(const stellar::engine::LocalizationTable *locale,
                           ContactCondition value) {
  switch (value) {
  case ContactCondition::active:
    return resolve(locale, "DIPLOMACY_COND_ACTIVE", "active");
  case ContactCondition::hostile:
    return resolve(locale, "DIPLOMACY_COND_HOSTILE", "hostile");
  case ContactCondition::stale_or_lost:
    return resolve(locale, "DIPLOMACY_COND_STALE_OR_LOST", "stale or lost");
  }
  return resolve(locale, "DIPLOMACY_COND_ACTIVE", "active");
}
std::string agreement_type_name(
    const stellar::engine::LocalizationTable *locale,
    DiplomaticAgreementType value) {
  switch (value) {
  case DiplomaticAgreementType::peace:
    return resolve(locale, "DIPLOMACY_ATYPE_PEACE", "Peace");
  case DiplomaticAgreementType::non_aggression:
    return resolve(locale, "DIPLOMACY_ATYPE_NON_AGGRESSION",
                   "Non Aggression");
  case DiplomaticAgreementType::access:
    return resolve(locale, "DIPLOMACY_ATYPE_ACCESS", "Access");
  case DiplomaticAgreementType::trade:
    return resolve(locale, "DIPLOMACY_ATYPE_TRADE", "Trade");
  case DiplomaticAgreementType::research_exchange:
    return resolve(locale, "DIPLOMACY_ATYPE_RESEARCH_EXCHANGE",
                   "Research Exchange");
  case DiplomaticAgreementType::ceasefire:
    return resolve(locale, "DIPLOMACY_ATYPE_CEASEFIRE", "Ceasefire");
  case DiplomaticAgreementType::cooperation:
    return resolve(locale, "DIPLOMACY_ATYPE_COOPERATION", "Cooperation");
  }
  return resolve(locale, "DIPLOMACY_ATYPE_PEACE", "Peace");
}
std::string proposal_kind_name(
    const stellar::engine::LocalizationTable *locale,
    DiplomaticProposalKind value) {
  switch (value) {
  case DiplomaticProposalKind::agreement:
    return resolve(locale, "DIPLOMACY_PKIND_AGREEMENT", "Agreement");
  case DiplomaticProposalKind::access_request:
    return resolve(locale, "DIPLOMACY_PKIND_ACCESS_REQUEST",
                   "Access Request");
  case DiplomaticProposalKind::trade_offer:
    return resolve(locale, "DIPLOMACY_PKIND_TRADE_OFFER", "Trade Offer");
  case DiplomaticProposalKind::demand:
    return resolve(locale, "DIPLOMACY_PKIND_DEMAND", "Demand");
  case DiplomaticProposalKind::peace_offer:
    return resolve(locale, "DIPLOMACY_PKIND_PEACE_OFFER", "Peace Offer");
  case DiplomaticProposalKind::ceasefire_offer:
    return resolve(locale, "DIPLOMACY_PKIND_CEASEFIRE_OFFER",
                   "Ceasefire Offer");
  }
  return resolve(locale, "DIPLOMACY_PKIND_AGREEMENT", "Agreement");
}
std::string event_kind_name(const stellar::engine::LocalizationTable *locale,
                            DiplomaticEventKind value) {
  switch (value) {
  case DiplomaticEventKind::contact_observed:
    return resolve(locale, "DIPLOMACY_EVENT_CONTACT_OBSERVED",
                   "Contact Observed");
  case DiplomaticEventKind::contact_established:
    return resolve(locale, "DIPLOMACY_EVENT_CONTACT_ESTABLISHED",
                   "Contact Established");
  case DiplomaticEventKind::communication_available:
    return resolve(locale, "DIPLOMACY_EVENT_COMMUNICATION_AVAILABLE",
                   "Communication Available");
  case DiplomaticEventKind::contact_lost:
    return resolve(locale, "DIPLOMACY_EVENT_CONTACT_LOST", "Contact Lost");
  case DiplomaticEventKind::access_changed:
    return resolve(locale, "DIPLOMACY_EVENT_ACCESS_CHANGED",
                   "Access Changed");
  case DiplomaticEventKind::claim_asserted:
    return resolve(locale, "DIPLOMACY_EVENT_CLAIM_ASSERTED",
                   "Claim Asserted");
  case DiplomaticEventKind::claim_communicated:
    return resolve(locale, "DIPLOMACY_EVENT_CLAIM_COMMUNICATED",
                   "Claim Communicated");
  case DiplomaticEventKind::claim_responded:
    return resolve(locale, "DIPLOMACY_EVENT_CLAIM_RESPONDED",
                   "Claim Responded");
  case DiplomaticEventKind::border_warning_issued:
    return resolve(locale, "DIPLOMACY_EVENT_BORDER_WARNING",
                   "Border Warning Issued");
  case DiplomaticEventKind::trespass_recorded:
    return resolve(locale, "DIPLOMACY_EVENT_TRESPASS_RECORDED",
                   "Trespass Recorded");
  case DiplomaticEventKind::proposal_sent:
    return resolve(locale, "DIPLOMACY_EVENT_PROPOSAL_SENT",
                   "Proposal Sent");
  case DiplomaticEventKind::proposal_accepted:
    return resolve(locale, "DIPLOMACY_EVENT_PROPOSAL_ACCEPTED",
                   "Proposal Accepted");
  case DiplomaticEventKind::proposal_rejected:
    return resolve(locale, "DIPLOMACY_EVENT_PROPOSAL_REJECTED",
                   "Proposal Rejected");
  case DiplomaticEventKind::proposal_withdrawn:
    return resolve(locale, "DIPLOMACY_EVENT_PROPOSAL_WITHDRAWN",
                   "Proposal Withdrawn");
  case DiplomaticEventKind::proposal_expired:
    return resolve(locale, "DIPLOMACY_EVENT_PROPOSAL_EXPIRED",
                   "Proposal Expired");
  case DiplomaticEventKind::agreement_activated:
    return resolve(locale, "DIPLOMACY_EVENT_AGREEMENT_ACTIVATED",
                   "Agreement Activated");
  case DiplomaticEventKind::agreement_terminated:
    return resolve(locale, "DIPLOMACY_EVENT_AGREEMENT_TERMINATED",
                   "Agreement Terminated");
  case DiplomaticEventKind::relationship_changed:
    return resolve(locale, "DIPLOMACY_EVENT_RELATIONSHIP_CHANGED",
                   "Relationship Changed");
  case DiplomaticEventKind::war_declared:
    return resolve(locale, "DIPLOMACY_EVENT_WAR_DECLARED", "War Declared");
  case DiplomaticEventKind::war_goal_achieved:
    return resolve(locale, "DIPLOMACY_EVENT_WAR_GOAL_ACHIEVED",
                   "War Goal Achieved");
  case DiplomaticEventKind::war_ended:
    return resolve(locale, "DIPLOMACY_EVENT_WAR_ENDED", "War Ended");
  }
  return resolve(locale, "DIPLOMACY_EVENT_RELATIONSHIP_CHANGED",
                 "Relationship Changed");
}
std::string war_goal_kind_name(const stellar::engine::LocalizationTable *locale,
                             WarGoalKind value) {
  switch (value) {
  case WarGoalKind::humiliate:
    return resolve(locale, "DIPLOMACY_GOAL_HUMILIATE", "Humiliate");
  case WarGoalKind::secure_claims:
    return resolve(locale, "DIPLOMACY_GOAL_SECURE_CLAIMS", "Secure claims");
  case WarGoalKind::conquer_system:
    return resolve(locale, "DIPLOMACY_GOAL_CONQUER", "Conquer system");
  case WarGoalKind::resist_aggression:
    return resolve(locale, "DIPLOMACY_GOAL_RESIST", "Resist aggression");
  }
  return resolve(locale, "DIPLOMACY_GOAL_HUMILIATE", "Humiliate");
}
std::string war_outcome_name(const stellar::engine::LocalizationTable *locale,
                             WarOutcome value) {
  switch (value) {
  case WarOutcome::active:
    return resolve(locale, "DIPLOMACY_OUTCOME_ACTIVE", "Ongoing");
  case WarOutcome::aggressor_victory:
    return resolve(locale, "DIPLOMACY_OUTCOME_AGGRESSOR", "Aggressor victory");
  case WarOutcome::defender_victory:
    return resolve(locale, "DIPLOMACY_OUTCOME_DEFENDER", "Defender victory");
  case WarOutcome::white_peace:
    return resolve(locale, "DIPLOMACY_OUTCOME_WHITE", "White peace");
  }
  return resolve(locale, "DIPLOMACY_OUTCOME_ACTIVE", "Ongoing");
}
std::string policy_domain_name(const stellar::engine::LocalizationTable *locale,
                               EmpirePolicyDomain value) {
  switch (value) {
  case EmpirePolicyDomain::economy:
    return resolve(locale, "DIPLOMACY_POLICY_ECONOMY", "Economy");
  case EmpirePolicyDomain::military:
    return resolve(locale, "DIPLOMACY_POLICY_MILITARY", "Military");
  case EmpirePolicyDomain::research:
    return resolve(locale, "DIPLOMACY_POLICY_RESEARCH", "Research");
  case EmpirePolicyDomain::frontier:
    return resolve(locale, "DIPLOMACY_POLICY_FRONTIER", "Frontier");
  }
  return resolve(locale, "DIPLOMACY_POLICY_ECONOMY", "Economy");
}
// Condensed signed-factor summary for one policy definition.
std::string policy_effects_name(
    const stellar::engine::LocalizationTable *locale,
    const EmpirePolicyEffects &effects) {
  const std::pair<double, std::pair<std::string_view, std::string_view>>
      factors[] = {
          {effects.industry_factor,
           {"DIPLOMACY_EFFECT_INDUSTRY", "Industry"}},
          {effects.credit_factor,
           {"DIPLOMACY_EFFECT_TRADE", "Trade"}},
          {effects.science_factor,
           {"DIPLOMACY_EFFECT_SCIENCE", "Science"}},
          {effects.shipbuilding_factor,
           {"DIPLOMACY_EFFECT_SHIPBUILDING", "Shipbuilding"}},
          {effects.expansion_factor,
           {"DIPLOMACY_EFFECT_EXPANSION", "Expansion"}},
          {effects.war_exhaustion_factor,
           {"DIPLOMACY_EFFECT_EXHAUSTION", "War exhaustion"}}};
  std::string out;
  for (const auto &[factor, names] : factors) {
    if (std::fabs(factor - 1.0) < 1e-9) continue;
    if (!out.empty()) out += " · ";
    const auto percent_delta = static_cast<int>(std::lround((factor - 1.0) * 100.0));
    out += resolve(locale, names.first, names.second);
    out += ' ';
    out += percent_delta > 0 ? '+' : '-';
    out += std::to_string(std::abs(percent_delta));
    out += '%';
  }
  return out.empty() ? resolve(locale, "DIPLOMACY_EFFECT_NEUTRAL",
                               "No modifiers")
                     : out;
}

using stellar::native_campaign::format_campaign_date;

[[nodiscard]] bool pair_matches(int first, int second, int observer,
                                int target) noexcept {
  return (first == observer && second == target) ||
         (first == target && second == observer);
}
[[nodiscard]] bool pair_matches(int first, std::optional<int> second,
                                int observer, int target) noexcept {
  return second && pair_matches(first, *second, observer, target);
}

[[nodiscard]] AccessPermission latest_access(const DiplomaticStateView &view,
                                             int grantor, int visitor) {
  AccessPermission result = AccessPermission::unspecified;
  std::int64_t newest = std::numeric_limits<std::int64_t>::min();
  for (const auto &access : view.access_permissions) {
    if (access.grantor_civilization_id != grantor ||
        access.visitor_civilization_id != visitor || access.updated_at_tick < newest)
      continue;
    newest = access.updated_at_tick;
    result = access.permission;
  }
  return result;
}

[[nodiscard]] std::string percent(std::optional<double> value) {
  if (!value) return "UNKNOWN";
  const auto rounded = static_cast<int>(
      std::lround(std::clamp(*value, 0., 1.) * 100.));
  return std::to_string(rounded) + "%";
}

void append_signature(std::ostringstream &out, std::string_view value) {
  out << value.size() << ':' << value << ';';
}
template <class T> void append_signature(std::ostringstream &out, const T &value) {
  if constexpr (std::is_convertible_v<T, std::string_view>)
    append_signature(out, std::string_view(value));
  else if constexpr (std::is_same_v<T, bool>)
    out << (value ? '1' : '0') << ';';
  else
    out << value << ';';
}

// Selection-independent authoritative fingerprint: contacts, proposals,
// agreements, access permissions and history — never the UI selection.
[[nodiscard]] std::string signature_for(const DiplomaticStateView &view,
                                        std::string_view date) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10);
  append_signature(out, date);
  append_signature(out, view.observer_civilization_id);
  append_signature(out, view.contacts.size());
  for (const auto &contact : view.contacts) {
    append_signature(out, contact.contact_id);
    append_signature(out, contact.target_civilization_id.value_or(-1));
    append_signature(out, static_cast<int>(contact.awareness));
    append_signature(out, static_cast<int>(contact.condition));
    append_signature(out, contact.communication_available);
    append_signature(out, contact.confidence);
    append_signature(out, contact.last_observed_tick);
    append_signature(out, contact.last_observed_system_id.value_or(-1));
  }
  append_signature(out, view.relationships.size());
  for (const auto &relationship : view.relationships) {
    append_signature(out, relationship.other_civilization_id);
    append_signature(out, static_cast<int>(relationship.political_state));
    append_signature(out, relationship.trust);
    append_signature(out, relationship.hostility);
    append_signature(out, relationship.fear);
    append_signature(out, relationship.respect);
    append_signature(out, relationship.cooperation);
    append_signature(out, relationship.grievances.size());
  }
  append_signature(out, view.access_permissions.size());
  for (const auto &access : view.access_permissions) {
    append_signature(out, access.grantor_civilization_id);
    append_signature(out, access.visitor_civilization_id);
    append_signature(out, static_cast<int>(access.permission));
    append_signature(out, access.updated_at_tick);
  }
  append_signature(out, view.agreements.size());
  for (const auto &agreement : view.agreements) {
    append_signature(out, agreement.agreement_id);
    append_signature(out, agreement.civilization_a_id);
    append_signature(out, agreement.civilization_b_id);
    append_signature(out, static_cast<int>(agreement.type));
    append_signature(out, static_cast<int>(agreement.status));
    append_signature(out, agreement.started_at_tick);
    append_signature(out, agreement.ended_at_tick.value_or(-1));
    append_signature(out, agreement.external_terms_reference.has_value());
    append_signature(out, agreement.external_terms_reference.value_or(""));
  }
  append_signature(out, view.proposals.size());
  for (const auto &proposal : view.proposals) {
    append_signature(out, proposal.proposal_id);
    append_signature(out, proposal.proposer_civilization_id);
    append_signature(out, proposal.recipient_civilization_id);
    append_signature(out, static_cast<int>(proposal.kind));
    append_signature(out, proposal.agreement_type ? static_cast<int>(*proposal.agreement_type) : -1);
    append_signature(out, static_cast<int>(proposal.status));
    append_signature(out, proposal.created_at_tick);
    append_signature(out, proposal.resolved_at_tick.value_or(-1));
    append_signature(out, proposal.summary);
    append_signature(out, proposal.external_terms_reference.has_value());
    append_signature(out, proposal.external_terms_reference.value_or(""));
  }
  append_signature(out, view.recent_events.size());
  for (const auto &event : view.recent_events)
    append_signature(out, event.event_id);
  // Wars sign their structure — belligerents, goals and resolution — but not
  // the drifting score/exhaustion meters: passive accrual must not make every
  // pending command read as a stale view.
  append_signature(out, view.wars.size());
  for (const auto &war : view.wars) {
    append_signature(out, war.war_id);
    append_signature(out, war.aggressor_civilization_id);
    append_signature(out, war.defender_civilization_id);
    append_signature(out, war.goals.size());
    for (const auto &goal : war.goals) {
      append_signature(out, static_cast<int>(goal.kind));
      append_signature(out, goal.beneficiary_civilization_id);
      append_signature(out, goal.system_id.value_or(-1));
      append_signature(out, goal.achieved);
    }
    append_signature(out, war.resolved_at_tick.value_or(-1));
    append_signature(out, static_cast<int>(war.outcome));
  }
  return out.str();
}

struct Projection {
  NativeDiplomacyView view;
  std::string signature;
};

[[nodiscard]] std::string observer_safe_system_name(
    const FreshCampaignState &world, const DiplomaticStateView &view,
    int system_id) {
  const auto observer = view.observer_civilization_id;
  const bool observed = std::ranges::any_of(
      view.contacts, [&](const auto &contact) {
        return contact.last_observed_system_id == system_id;
      });
  if (!observed &&
      !world.knowledge.is_system_known(observer, system_id))
    return "Unknown system";
  const auto *system = [&]() -> const StellarSystem * {
    const auto found =
        std::ranges::find(world.systems, system_id, &StellarSystem::id);
    return found == world.systems.end() ? nullptr : &*found;
  }();
  if (!world.knowledge.is_system_known(observer, system_id) || !system)
    return "Unidentified system";
  return system->name;
}

[[nodiscard]] Projection project(CampaignFrame &frame, std::uint64_t generation,
                                 std::size_t contact_index,
                                 const stellar::engine::LocalizationTable *locale) {
  auto &runtime = frame.runtime();
  auto &world = runtime.world().campaign();
  const auto observer = world.player_civilization_id;
  const auto view = ObserverDiplomacyCommandService(runtime.diplomacy())
                        .build_view(observer);
  const auto availability = build_observer_diplomacy_action_availability(view);
  const auto identified_name = [&](int id) {
    const auto found =
        std::ranges::find(world.civilizations, id, &Civilization::id);
    return found != world.civilizations.end()
               ? found->name
               : resolved(locale, "DIPLOMACY_CIVILIZATION_NAME",
                          {std::to_string(id)}, "Civilization {0}");
  };

  Projection out;
  auto &v = out.view;
  v.campaign_generation = generation;
  v.observer_civilization_id = observer;
  v.date = format_campaign_date(frame.clock().simulation_days());
  out.signature = signature_for(view, v.date);
  // The player's empire-policy assignment belongs to the guarded mutation set:
  // a committed change must invalidate pending diplomacy commands.
  {
    std::ostringstream policy_tail;
    policy_tail.imbue(std::locale::classic());
    const auto found = std::ranges::find(world.empire_policies, observer,
                                         &EmpirePolicyState::civilization_id);
    if (found != world.empire_policies.end())
      for (const auto &assignment : found->assignments) {
        append_signature(policy_tail, static_cast<int>(assignment.domain));
        append_signature(policy_tail, assignment.policy_id);
        append_signature(policy_tail, assignment.changed_at_tick);
      }
    out.signature += policy_tail.str();
  }

  const auto tick_date = [](std::int64_t tick) {
    return format_campaign_date(
        static_cast<double>(tick) /
        static_cast<double>(DiplomacyCampaignClock::ticks_per_simulation_day));
  };
  // War ledger: the view's wars are already observer-filtered. Scores are
  // presented from the observer's side when they are a belligerent.
  for (const auto &war : view.wars) {
    NativeDiplomacyWarRow row;
    row.war_id = war.war_id;
    row.aggressor_civilization_id = war.aggressor_civilization_id;
    row.defender_civilization_id = war.defender_civilization_id;
    row.aggressor_name = identified_name(war.aggressor_civilization_id);
    row.defender_name = identified_name(war.defender_civilization_id);
    row.observer_is_aggressor = observer == war.aggressor_civilization_id;
    row.observer_is_belligerent =
        row.observer_is_aggressor || observer == war.defender_civilization_id;
    if (row.observer_is_aggressor) {
      row.counterpart_id = war.defender_civilization_id;
      row.counterpart_name = row.defender_name;
      row.score = war.war_score;
      row.observer_exhaustion = war.aggressor_exhaustion;
      row.counterpart_exhaustion = war.defender_exhaustion;
    } else if (observer == war.defender_civilization_id) {
      row.counterpart_id = war.aggressor_civilization_id;
      row.counterpart_name = row.aggressor_name;
      row.score = -war.war_score;
      row.observer_exhaustion = war.defender_exhaustion;
      row.counterpart_exhaustion = war.aggressor_exhaustion;
    } else {
      row.counterpart_name = resolved(
          locale, "DIPLOMACY_WAR_BETWEEN",
          {row.aggressor_name, row.defender_name}, "{0} vs {1}");
      row.score = war.war_score;
      row.observer_exhaustion = war.aggressor_exhaustion;
      row.counterpart_exhaustion = war.defender_exhaustion;
    }
    row.declared = tick_date(war.declared_at_tick);
    if (war.resolved_at_tick) row.resolved = tick_date(*war.resolved_at_tick);
    row.outcome = war_outcome_name(locale, war.outcome);
    row.goals.reserve(war.goals.size());
    for (const auto &goal : war.goals) {
      NativeDiplomacyWarGoalRow goal_row;
      goal_row.kind = goal.kind;
      goal_row.system_id = goal.system_id;
      goal_row.label = war_goal_kind_name(locale, goal.kind);
      if (goal.system_id)
        goal_row.label = resolved(
            locale, "DIPLOMACY_GOAL_AT",
            {goal_row.label,
             observer_safe_system_name(world, view, *goal.system_id)},
            "{0}: {1}");
      if (goal.beneficiary_civilization_id != observer)
        goal_row.label = resolved(locale, "DIPLOMACY_GOAL_THEIRS",
                                  {goal_row.label}, "Their goal · {0}");
      goal_row.achieved = goal.achieved;
      row.goals.push_back(std::move(goal_row));
    }
    v.wars.push_back(std::move(row));
  }
  // Empire policy panel: the catalog is public, the active assignment comes
  // from authoritative campaign state for the observer's own civilization.
  {
    const auto found = std::ranges::find(world.empire_policies, observer,
                                         &EmpirePolicyState::civilization_id);
    const EmpirePolicyState empty{observer, {}};
    const auto &state =
        found == world.empire_policies.end() ? empty : *found;
    for (const auto &definition : empire_policy_catalog()) {
      NativeDiplomacyPolicyRow row;
      row.domain_index = static_cast<int>(definition.domain);
      row.domain_label = policy_domain_name(locale, definition.domain);
      row.policy_id = std::string(definition.id);
      row.display_name = std::string(definition.display_name);
      row.summary = std::string(definition.summary);
      row.effects = policy_effects_name(locale, definition.effects);
      const auto *active = active_empire_policy(state, definition.domain);
      row.active = active && active->id == definition.id;
      v.policies.push_back(std::move(row));
    }
  }

  v.contacts.reserve(view.contacts.size());
  for (std::size_t index = 0; index < view.contacts.size(); ++index) {
    const auto &contact = view.contacts[index];
    NativeDiplomacyContact row;
    row.contact_id = contact.contact_id;
    row.source_index = index;
    row.confidence = std::clamp(contact.confidence, 0., 1.);
    row.identified = contact.target_civilization_id.has_value();
    row.civilization_id = contact.target_civilization_id;
    row.last_observed_system_id = contact.last_observed_system_id;
    const auto *relationship = static_cast<const DiplomaticRelationshipView *>(
        nullptr);
    if (row.identified) {
      const auto target = *contact.target_civilization_id;
      const auto found = std::ranges::find_if(
          view.relationships, [&](const auto &candidate) {
            return candidate.other_civilization_id == target;
          });
      if (found != view.relationships.end()) relationship = &*found;
      row.display_name = identified_name(target);
      row.political_state =
          relationship ? std::optional{relationship->political_state}
                       : std::nullopt;
      row.status = relationship ? political_name(locale, relationship->political_state)
                                : resolve(locale, "DIPLOMACY_NO_RELATIONSHIP", "NO FORMAL RELATIONSHIP");
      row.cooperation = relationship
                            ? std::optional<double>{relationship->cooperation}
                            : std::nullopt;
      row.pending_proposal_count = static_cast<int>(std::ranges::count_if(
          view.proposals, [&](const auto &proposal) {
            return proposal.status == DiplomaticProposalStatus::pending &&
                   pair_matches(proposal.proposer_civilization_id,
                                proposal.recipient_civilization_id, observer,
                                target);
          }));
      const auto civilization = std::ranges::find(
          world.civilizations, target, &Civilization::id);
      if (civilization != world.civilizations.end()) {
        for (const auto &profile : species_environment_profiles())
          if (profile.id == civilization->species_id)
            row.species_name = profile.display_name;
      }
    } else {
      row.display_name = resolve(locale, "DIPLOMACY_UNKNOWN_CONTACT", "UNKNOWN CONTACT");
      row.status = resolve(locale, "DIPLOMACY_IDENTITY_UNKNOWN", "IDENTITY UNKNOWN");
    }
    const bool channel = contact.communication_available &&
                         contact.condition != ContactCondition::stale_or_lost;
    row.communication_available = channel;
    row.communication =
        channel ? resolve(locale, "DIPLOMACY_CHANNEL_AVAILABLE", "CHANNEL AVAILABLE")
                : resolve(locale, "DIPLOMACY_CHANNEL_UNAVAILABLE", "CHANNEL UNAVAILABLE");
    if (row.last_observed_system_id)
      row.last_observed_system_name = observer_safe_system_name(
          world, view, *row.last_observed_system_id);
    v.contacts.push_back(std::move(row));
  }

  if (!v.contacts.empty()) {
    const auto index =
        std::min(contact_index, v.contacts.size() - std::size_t{1});
    const auto &contact_row = v.contacts[index];
    const auto &raw = view.contacts[index];
    auto &s = v.selected;
    s.present = true;
    s.contact_index = index;
    s.target_civilization_id = raw.target_civilization_id;
    const bool channel = raw.communication_available &&
                         raw.condition != ContactCondition::stale_or_lost;
    s.has_visible_communication = channel;
    s.contact_name = raw.target_civilization_id
                         ? contact_row.display_name
                         : resolved(locale, "DIPLOMACY_UNIDENTIFIED_CONTACT",
                                    {contact_row.contact_id}, "UNIDENTIFIED CONTACT {0}");
    s.contact_status =
        resolved(locale, "DIPLOMACY_CONTACT_STATUS",
                 {awareness_name(locale, raw.awareness),
                  condition_name(locale, raw.condition),
                  std::to_string(static_cast<int>(std::lround(raw.confidence * 100.)))},
                 "{0} · {1} · {2}% confidence");
    s.communication_status =
        channel ? resolve(locale, "DIPLOMACY_CHANNEL_AVAILABLE_SHORT", "Channel available")
                : resolve(locale, "DIPLOMACY_CHANNEL_UNAVAILABLE_SHORT", "Channel unavailable");

    const DiplomaticRelationshipView *relationship = nullptr;
    if (s.target_civilization_id) {
      const auto found = std::ranges::find_if(
          view.relationships, [&](const auto &candidate) {
            return candidate.other_civilization_id == *s.target_civilization_id;
          });
      if (found != view.relationships.end()) relationship = &*found;
    }
    const auto political = relationship ? relationship->political_state
                                        : DiplomaticPoliticalState::unknown;
    s.political_status =
        relationship ? political_name(locale, relationship->political_state)
                     : (s.target_civilization_id
                            ? resolve(locale, "DIPLOMACY_NO_RELATIONSHIP_LONG", "No formal relationship")
                            : resolve(locale, "DIPLOMACY_IDENTITY_UNKNOWN_LONG", "Identity unknown"));
    if (relationship) {
      s.trust = relationship->trust;
      s.hostility = relationship->hostility;
      s.fear = relationship->fear;
      s.respect = relationship->respect;
      s.cooperation = relationship->cooperation;
    }
    if (s.target_civilization_id) {
      const auto civilization = std::ranges::find(
          world.civilizations, *s.target_civilization_id, &Civilization::id);
      if (civilization != world.civilizations.end())
        s.species_id = civilization->species_id;
    }

    const auto inbound = s.target_civilization_id
                             ? latest_access(view, *s.target_civilization_id,
                                             observer)
                             : AccessPermission::unspecified;
    const auto outbound = s.target_civilization_id
                              ? latest_access(view, observer,
                                              *s.target_civilization_id)
                              : AccessPermission::unspecified;
    s.their_access = access_name(locale, inbound);
    s.our_access = access_name(locale, outbound);
    s.access_summary =
        s.target_civilization_id
            ? resolved(locale, "DIPLOMACY_ACCESS_SUMMARY",
                       {s.our_access, s.their_access},
                       "Your access: {0} · Their access: {1}")
            : resolve(locale, "DIPLOMACY_ACCESS_UNIDENTIFIED",
                      "Transit rights unavailable until identification");

    if (s.target_civilization_id) {
      const auto target = *s.target_civilization_id;
      for (const auto &agreement : view.agreements) {
        if (!pair_matches(agreement.civilization_a_id,
                          agreement.civilization_b_id, observer, target))
          continue;
        NativeDiplomacyAgreementRow row;
        row.agreement_id = agreement.agreement_id;
        row.type = agreement_type_name(locale, agreement.type);
        row.agreement_status = agreement.status;
        row.status = agreement_status_name(locale, agreement.status);
        row.started = format_campaign_date(
            static_cast<double>(agreement.started_at_tick) /
            static_cast<double>(DiplomacyCampaignClock::ticks_per_simulation_day));
        if (agreement.ended_at_tick)
          row.ended = format_campaign_date(
              static_cast<double>(*agreement.ended_at_tick) /
              static_cast<double>(
                  DiplomacyCampaignClock::ticks_per_simulation_day));
        v.agreements.push_back(std::move(row));
      }
      std::ranges::sort(v.agreements, {}, &NativeDiplomacyAgreementRow::agreement_id);
      const auto active = std::ranges::count_if(v.agreements, [](const auto &row) {
        return row.agreement_status == DiplomaticAgreementStatus::active;
      });
      if (active == 0) {
        s.agreements_summary = resolve(locale, "DIPLOMACY_NO_AGREEMENTS", "No active agreements");
      } else {
        s.agreements_summary.clear();
        bool first = true;
        for (const auto &row : v.agreements)
          if (row.agreement_status == DiplomaticAgreementStatus::active) {
            if (!first) s.agreements_summary += " · ";
            s.agreements_summary += row.type;
            first = false;
          }
      }

      for (const auto &proposal : view.proposals) {
        if (proposal.status != DiplomaticProposalStatus::pending ||
            !pair_matches(proposal.proposer_civilization_id,
                          proposal.recipient_civilization_id, observer, target))
          continue;
        NativeDiplomacyProposalRow row;
        row.proposal_id = proposal.proposal_id;
        const bool incoming = proposal.recipient_civilization_id == observer;
        row.direction = incoming ? resolve(locale, "DIPLOMACY_INCOMING", "INCOMING")
                                 : resolve(locale, "DIPLOMACY_OUTGOING", "OUTGOING");
        row.kind = proposal_kind_name(locale, proposal.kind);
        if (proposal.agreement_type)
          row.agreement_type =
              agreement_type_name(locale, *proposal.agreement_type);
        row.summary = proposal.summary;
        row.can_accept = incoming;
        row.can_reject = incoming;
        row.can_withdraw = !incoming;
        v.proposals.push_back(std::move(row));
      }
      std::ranges::sort(v.proposals, {}, &NativeDiplomacyProposalRow::proposal_id);
      s.proposal_summary = v.proposals.empty()
                               ? resolve(locale, "DIPLOMACY_NO_PROPOSALS", "No pending proposals")
                               : resolved(locale, "DIPLOMACY_PENDING_PROPOSALS",
                                          {std::to_string(v.proposals.size())},
                                          "{0} pending proposal(s)");

      for (const auto &event : view.recent_events) {
        if (!pair_matches(event.primary_civilization_id,
                          event.secondary_civilization_id, observer, target))
          continue;
        NativeDiplomacyHistoryRow row;
        row.event_id = event.event_id;
        row.date = format_campaign_date(
            static_cast<double>(event.tick) /
            static_cast<double>(DiplomacyCampaignClock::ticks_per_simulation_day));
        row.kind = event_kind_name(locale, event.kind);
        row.summary = event.summary;
        v.history.push_back(std::move(row));
      }
      std::ranges::reverse(v.history);
      for (const auto &row : v.history | std::views::take(3))
        s.recent_events.push_back(row.summary);
    }

    if (s.target_civilization_id) {
      const auto target = *s.target_civilization_id;
      const auto found = std::ranges::find_if(
          availability, [&](const auto &candidate) {
            return candidate.counterpart_civilization_id == target;
          });
      if (found != availability.end()) {
        s.can_attempt_communication = found->can_attempt_communication;
        s.can_declare_war = found->can_declare_war;
        s.communication_blocker = found->attempt_communication_blocker;
        s.declare_war_blocker = found->declare_war_blocker;
      }
      const auto active_non_aggression = std::ranges::any_of(
          view.agreements, [&](const auto &agreement) {
            return agreement.type == DiplomaticAgreementType::non_aggression &&
                   agreement.status == DiplomaticAgreementStatus::active &&
                   pair_matches(agreement.civilization_a_id,
                                agreement.civilization_b_id, observer, target);
          });
      s.can_offer_non_aggression =
          channel && political != DiplomaticPoliticalState::at_war &&
          !active_non_aggression;
      s.can_request_access =
          channel && inbound != AccessPermission::granted;
      s.can_offer_peace =
          channel && (political == DiplomaticPoliticalState::hostile ||
                      political == DiplomaticPoliticalState::at_war ||
                      political == DiplomaticPoliticalState::ceasefire);
      s.can_offer_ceasefire =
          channel && (political == DiplomaticPoliticalState::hostile ||
                      political == DiplomaticPoliticalState::at_war);
      s.can_set_access = channel;
      using enum stellar::core::DiplomacyActionBlocker;
      const auto channel_blocker = channel ? none : no_channel;
      s.offer_non_aggression_blocker =
          s.can_offer_non_aggression
              ? none
              : !channel ? no_channel
              : political == DiplomaticPoliticalState::at_war ? already_at_war
              : agreement_active;
      s.request_access_blocker =
          s.can_request_access ? none
                               : !channel ? no_channel : access_granted;
      s.offer_peace_blocker =
          s.can_offer_peace ? none : !channel ? no_channel : not_hostile;
      s.offer_ceasefire_blocker =
          s.can_offer_ceasefire ? none : !channel ? no_channel : not_hostile;
      s.set_access_blocker = channel_blocker;
      s.negotiate_blocker =
          !channel
              ? no_channel
              : !(s.can_offer_non_aggression || s.can_request_access ||
                  s.can_offer_peace || s.can_offer_ceasefire ||
                  s.can_set_access)
                    ? no_terms
                    : none;

      // War-goal picker options: the two standing goals plus a conquer row
      // for each active claim the observer has asserted — the authoritative
      // command revalidates every attached goal.
      if (s.can_declare_war) {
        s.war_goal_options.push_back(
            {WarGoalKind::humiliate, std::nullopt,
             resolve(locale, "DIPLOMACY_GOAL_HUMILIATE", "Humiliate")});
        s.war_goal_options.push_back(
            {WarGoalKind::secure_claims, std::nullopt,
             resolve(locale, "DIPLOMACY_GOAL_SECURE_CLAIMS", "Secure claims")});
        for (const auto &claim : view.claims)
          if (claim.active && claim.claimant_civilization_id == observer)
            s.war_goal_options.push_back(
                {WarGoalKind::conquer_system, claim.system_id,
                 resolved(locale, "DIPLOMACY_GOAL_CONQUER_AT",
                          {observer_safe_system_name(world, view,
                                                     claim.system_id)},
                          "Conquer {0}")});
      }
      // War status for the selected counterpart when a visible war exists.
      for (const auto &war : v.wars) {
        if (!war.counterpart_id || *war.counterpart_id != target || war.resolved)
          continue;
        const auto score = static_cast<int>(std::lround(war.score * 100.0));
        s.war_summary = resolved(
            locale, "DIPLOMACY_WAR_SUMMARY",
            {std::to_string(score),
             std::to_string(
                 static_cast<int>(std::lround(war.observer_exhaustion * 100.))),
             std::to_string(static_cast<int>(
                 std::lround(war.counterpart_exhaustion * 100.)))},
            "War score {0}% · Our exhaustion {1}% · Their exhaustion {2}%");
        break;
      }
    }
  }

  return out;
}

[[nodiscard]] NativeDiplomacyCommandOutcome stale(
    const stellar::engine::LocalizationTable *locale) {
  return {false, resolve(locale, "DIPLOMACY_MSG_CHANGED",
                         "The diplomacy state changed; review the current terms.")};
}

} // namespace

std::vector<const NativeDiplomacyContact *> filter_native_diplomacy_contacts(
    const NativeDiplomacyView &view, NativeDiplomacyContactFilter filter) {
  std::vector<const NativeDiplomacyContact *> out;
  for (const auto &contact : view.contacts) {
    const bool keep = [&] {
      switch (filter) {
      case NativeDiplomacyContactFilter::all: return true;
      case NativeDiplomacyContactFilter::identified: return contact.identified;
      case NativeDiplomacyContactFilter::unidentified: return !contact.identified;
      case NativeDiplomacyContactFilter::cooperative:
        return contact.cooperation && *contact.cooperation >= .5;
      case NativeDiplomacyContactFilter::neutral:
        return contact.identified &&
               (!contact.political_state ||
                *contact.political_state == stellar::core::DiplomaticPoliticalState::unknown ||
                *contact.political_state == stellar::core::DiplomaticPoliticalState::peace);
      case NativeDiplomacyContactFilter::hostile:
        return contact.political_state == stellar::core::DiplomaticPoliticalState::hostile;
      case NativeDiplomacyContactFilter::at_war:
        return contact.political_state == stellar::core::DiplomaticPoliticalState::at_war;
      case NativeDiplomacyContactFilter::pending_proposal:
        return contact.pending_proposal_count > 0;
      case NativeDiplomacyContactFilter::communication_available:
        return contact.communication_available;
      }
      return true;
    }();
    if (keep) out.push_back(&contact);
  }
  return out;
}

NativeDiplomacyView
NativeDiplomacyController::build(CampaignFrame &frame,
                                 std::uint64_t generation,
                                 std::size_t contact_index) {
  require_owner();
  if (generation_ && generation < *generation_)
    throw std::invalid_argument(
        "A stale campaign generation cannot replace diplomacy.");
  if (!generation_ || *generation_ != generation) {
    generation_ = generation;
    revision_ = 0;
    signature_.reset();
  }
  auto p = project(frame, generation, contact_index, locale_);
  if (!signature_ || *signature_ != p.signature) {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Native diplomacy revision is exhausted.");
    ++revision_;
    signature_ = p.signature;
  }
  p.view.diplomacy_revision = revision_;
  return p.view;
}

NativeDiplomacyCommandOutcome NativeDiplomacyController::execute(
    CampaignFrame &frame, std::uint64_t generation, std::uint64_t revision,
    DiplomacyWorkspaceAction action, std::optional<int> target,
    std::optional<std::int64_t> proposal_id,
    std::span<const WarGoalSpec> war_goals, std::string_view policy_id) {
  require_owner();
  if (!generation_ || *generation_ != generation || revision != revision_ ||
      !signature_)
    return stale(locale_);
  auto check = project(frame, generation, 0, locale_);
  if (check.signature != *signature_) return stale(locale_);
  auto &runtime = frame.runtime();
  const auto observer =
      runtime.world().campaign().player_civilization_id;
  const auto tick = DiplomacyCampaignClock::from_simulation_days(
      frame.clock().simulation_days());
  ObserverDiplomacyCommandService service(runtime.diplomacy());
  ObserverDiplomacyCommandResult result;
  switch (action) {
  case DiplomacyWorkspaceAction::establish_communication:
    if (!target) return stale(locale_);
    result = service.establish_communication(observer, *target, tick);
    break;
  case DiplomacyWorkspaceAction::propose_non_aggression:
    if (!target) return stale(locale_);
    result = service.send_proposal(
        observer, *target, DiplomaticProposalKind::agreement, tick,
        "Proposal for a non-aggression agreement.",
        DiplomaticAgreementType::non_aggression);
    break;
  case DiplomacyWorkspaceAction::request_access:
    if (!target) return stale(locale_);
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::access_request, tick,
                                   "Request for transit access.");
    break;
  case DiplomacyWorkspaceAction::offer_peace:
    if (!target) return stale(locale_);
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::peace_offer, tick,
                                   "Offer to establish peace.");
    break;
  case DiplomacyWorkspaceAction::offer_ceasefire:
    if (!target) return stale(locale_);
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::ceasefire_offer, tick,
                                   "Offer to establish a ceasefire.");
    break;
  case DiplomacyWorkspaceAction::grant_access:
    if (!target) return stale(locale_);
    result = service.set_access_permission(observer, *target,
                                           AccessPermission::granted, tick);
    break;
  case DiplomacyWorkspaceAction::deny_access:
    if (!target) return stale(locale_);
    result = service.set_access_permission(observer, *target,
                                           AccessPermission::denied, tick);
    break;
  case DiplomacyWorkspaceAction::declare_war:
    if (!target) return stale(locale_);
    result = service.declare_war(observer, *target, tick, war_goals);
    break;
  case DiplomacyWorkspaceAction::accept_proposal:
    if (!proposal_id) return stale(locale_);
    result = service.respond_to_proposal(observer, *proposal_id, true, tick);
    break;
  case DiplomacyWorkspaceAction::reject_proposal:
    if (!proposal_id) return stale(locale_);
    result = service.respond_to_proposal(observer, *proposal_id, false, tick);
    break;
  case DiplomacyWorkspaceAction::withdraw_proposal:
    if (!proposal_id) return stale(locale_);
    result = service.withdraw_proposal(observer, *proposal_id, tick);
    break;
  case DiplomacyWorkspaceAction::set_empire_policy: {
    const auto policy = set_empire_policy(runtime.world().campaign(), observer,
                                          policy_id, tick);
    if (policy.accepted) signature_.reset();
    if (!policy.accepted)
      return {false, localized_command_message(locale_, policy.message)};
    const auto *definition = find_empire_policy(policy_id);
    return {true,
            resolved(locale_, "DIPLOMACY_MSG_POLICY_SET",
                     {definition ? std::string(definition->display_name)
                                 : std::string(policy_id)},
                     "Policy set: {0}")};
  }
  }
  if (result.accepted) signature_.reset();
  return {result.accepted, localized_command_message(locale_, result.message)};
}

std::string NativeDiplomacyController::tr(std::string_view key,
                                          std::string_view fallback) const {
  return resolve(locale_, key, fallback);
}

void NativeDiplomacyController::require_owner() const {
  if (owner_ != std::this_thread::get_id())
    throw std::logic_error(
        "Native diplomacy commands only run on the owning thread.");
}

} // namespace stellar::native_diplomacy
