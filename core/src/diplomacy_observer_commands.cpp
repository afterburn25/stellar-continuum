#include <stellar/core/diplomacy_observer_commands.hpp>

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/knowledge.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <ranges>
#include <utility>

namespace stellar::core {
namespace {

constexpr std::string_view channel_unavailable_message =
    "No active diplomatic channel is available to that counterpart.";
constexpr std::string_view contact_unavailable_message =
    "No usable diplomatic contact is available to that counterpart.";
constexpr std::string_view action_unavailable_message =
    "That diplomatic action is not currently available.";
constexpr std::string_view invalid_request_message =
    "The diplomatic request is not valid.";
constexpr std::string_view territorial_action_unavailable_message =
    "That territorial claim action is not currently available.";
constexpr std::string_view territorial_invalid_request_message =
    "The territorial claim request is not valid.";
constexpr std::string_view border_action_unavailable_message =
    "That border warning action is not currently available.";
constexpr std::string_view border_invalid_request_message =
    "The border warning request is not valid.";

ObserverDiplomacyCommandResult observer_result(
    bool accepted, ObserverDiplomacyCommandStatus status,
    std::string_view message,
    std::optional<std::int64_t> proposal_id = std::nullopt,
    std::optional<std::int64_t> agreement_id = std::nullopt) {
  return {accepted, status, std::string(message), proposal_id, agreement_id};
}

ObserverDiplomacyCommandResult invalid_request() {
  return observer_result(false,
                         ObserverDiplomacyCommandStatus::invalid_request,
                         invalid_request_message);
}
ObserverDiplomacyCommandResult action_unavailable() {
  return observer_result(false,
                         ObserverDiplomacyCommandStatus::action_unavailable,
                         action_unavailable_message);
}
ObserverDiplomacyCommandResult channel_unavailable() {
  return observer_result(false,
                         ObserverDiplomacyCommandStatus::channel_unavailable,
                         channel_unavailable_message);
}

bool valid_actor_and_tick(int observer, std::int64_t tick) noexcept {
  return observer >= 0 && tick >= 0;
}

bool defined(DiplomaticProposalKind value) noexcept {
  return value >= DiplomaticProposalKind::agreement &&
         value <= DiplomaticProposalKind::ceasefire_offer;
}
bool defined(AccessPermission value) noexcept {
  return value >= AccessPermission::unspecified &&
         value <= AccessPermission::denied;
}
bool defined(TerritorialClaimResponse value) noexcept {
  return value >= TerritorialClaimResponse::none &&
         value <= TerritorialClaimResponse::disputed;
}

bool unicode_whitespace(std::uint32_t value) noexcept {
  return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
         value == 0x0085 || value == 0x00a0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000;
}

bool consume_code_point(std::string_view &text, std::uint32_t &value) noexcept {
  if (text.empty()) return false;
  const auto first = static_cast<unsigned char>(text.front());
  text.remove_prefix(1);
  if (first < 0x80) {
    value = first;
    return true;
  }
  const int continuation_count =
      first < 0xc2 ? -1 : first < 0xe0 ? 1 : first < 0xf0 ? 2 : first < 0xf5 ? 3 : -1;
  if (continuation_count < 0 ||
      text.size() < static_cast<std::size_t>(continuation_count))
    return false;
  value = first & (continuation_count == 1 ? 0x1f
                   : continuation_count == 2 ? 0x0f
                                             : 0x07);
  for (int index = 0; index < continuation_count; ++index) {
    const auto next = static_cast<unsigned char>(text.front());
    text.remove_prefix(1);
    if ((next & 0xc0) != 0x80) return false;
    value = (value << 6) | (next & 0x3f);
  }
  return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

bool null_or_white_space(std::string_view value) noexcept {
  if (value.empty()) return true;
  while (!value.empty()) {
    std::uint32_t point{};
    if (!consume_code_point(value, point) || !unicode_whitespace(point))
      return false;
  }
  return true;
}

std::vector<std::uint16_t> utf16(std::string_view text) {
  std::vector<std::uint16_t> result;
  while (!text.empty()) {
    std::uint32_t point{};
    if (!consume_code_point(text, point)) break;
    if (point <= 0xffff) {
      result.push_back(static_cast<std::uint16_t>(point));
    } else {
      point -= 0x10000;
      result.push_back(static_cast<std::uint16_t>(0xd800 + (point >> 10)));
      result.push_back(static_cast<std::uint16_t>(0xdc00 + (point & 0x3ff)));
    }
  }
  return result;
}

bool ordinal_less(std::string_view left, std::string_view right) {
  return utf16(left) < utf16(right);
}

template <class Range, class Predicate>
bool any(const Range &range, Predicate predicate) {
  return std::ranges::any_of(range, std::move(predicate));
}

} // namespace

std::vector<ObserverDiplomacyActionAvailability>
build_observer_diplomacy_action_availability(const DiplomaticStateView &view) {
  std::map<int, const DiplomaticContactView *> latest;
  for (const auto &contact : view.contacts) {
    if (!contact.target_civilization_id ||
        contact.awareness < ContactAwareness::identified)
      continue;
    const int target = *contact.target_civilization_id;
    const auto found = latest.find(target);
    if (found == latest.end() ||
        contact.last_observed_tick > found->second->last_observed_tick ||
        (contact.last_observed_tick == found->second->last_observed_tick &&
         ordinal_less(contact.contact_id, found->second->contact_id))) {
      latest[target] = &contact;
    }
  }

  std::vector<ObserverDiplomacyActionAvailability> result;
  result.reserve(latest.size());
  for (const auto &[counterpart, contact] : latest) {
    const auto relationship = std::ranges::find_if(
        view.relationships, [counterpart](const auto &candidate) {
          return candidate.other_civilization_id == counterpart;
        });
    const auto political = relationship == view.relationships.end()
                               ? DiplomaticPoliticalState::unknown
                               : relationship->political_state;
    const bool active_communication =
        contact->communication_available &&
        contact->condition != ContactCondition::stale_or_lost;
    const auto incoming = std::ranges::count_if(
        view.proposals, [&](const auto &proposal) {
          return proposal.proposer_civilization_id == counterpart &&
                 proposal.recipient_civilization_id ==
                     view.observer_civilization_id &&
                 proposal.status == DiplomaticProposalStatus::pending;
        });
    const auto outgoing = std::ranges::count_if(
        view.proposals, [&](const auto &proposal) {
          return proposal.proposer_civilization_id ==
                     view.observer_civilization_id &&
                 proposal.recipient_civilization_id == counterpart &&
                 proposal.status == DiplomaticProposalStatus::pending;
        });
    const auto agreements = std::ranges::count_if(
        view.agreements, [&](const auto &agreement) {
          return agreement.status == DiplomaticAgreementStatus::active &&
                 (agreement.civilization_a_id == counterpart ||
                  agreement.civilization_b_id == counterpart);
        });
    result.push_back({
        counterpart,
        contact->awareness,
        contact->condition,
        contact->confidence,
        contact->last_observed_tick,
        political,
        active_communication,
        !active_communication &&
            contact->condition != ContactCondition::stale_or_lost,
        active_communication,
        active_communication,
        political != DiplomaticPoliticalState::at_war,
        incoming > 0,
        outgoing > 0,
        active_communication && agreements > 0,
        static_cast<int>(incoming),
        static_cast<int>(outgoing),
        static_cast<int>(agreements),
    });
  }
  return result;
}

ObserverDiplomacyCommandService::ObserverDiplomacyCommandService(
    DiplomacyState &state) noexcept
    : state_(&state) {}

DiplomaticStateView
ObserverDiplomacyCommandService::build_view(int observer) const {
  if (observer < 0)
    throw DiplomacyArgumentRangeError(
        "Specified argument was out of the range of valid values. (Parameter 'observerCivilizationId')");
  return state_->build_view_for(observer);
}

bool ObserverDiplomacyCommandService::has_visible_active_communication(
    int observer, int target) const {
  return any(build_view(observer).contacts, [target](const auto &contact) {
    return contact.target_civilization_id == target &&
           contact.communication_available &&
           contact.condition != ContactCondition::stale_or_lost;
  });
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::establish_communication(
    int observer, int target, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || target < 0 || target == observer)
    return invalid_request();
  const auto view = build_view(observer);
  const DiplomaticContactView *contact{};
  for (const auto &candidate : view.contacts) {
    if (candidate.target_civilization_id == target &&
        (!contact || candidate.last_observed_tick > contact->last_observed_tick))
      contact = &candidate;
  }
  if (!contact || contact->awareness < ContactAwareness::identified ||
      contact->condition == ContactCondition::stale_or_lost) {
    return observer_result(false,
                           ObserverDiplomacyCommandStatus::channel_unavailable,
                           contact_unavailable_message);
  }
  if (contact->communication_available)
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Communication channel is already available.");
  try {
    DiplomaticCommunicationService(*state_).establish_mutual_communication(
        observer, target, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Communication channel established.");
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult ObserverDiplomacyCommandService::send_proposal(
    int observer, int target, DiplomaticProposalKind kind, std::int64_t tick,
    std::string_view summary,
    std::optional<DiplomaticAgreementType> agreement_type,
    std::optional<std::string_view> external_terms_reference) {
  const std::string owned_summary(summary);
  const std::optional<std::string> owned_terms =
      external_terms_reference
          ? std::optional<std::string>(*external_terms_reference)
          : std::nullopt;
  if (!valid_actor_and_tick(observer, tick) || target < 0 || target == observer ||
      null_or_white_space(owned_summary) || !defined(kind))
    return invalid_request();
  if (!has_visible_active_communication(observer, target))
    return channel_unavailable();
  try {
    const auto id = DiplomacySimulation(*state_).send_proposal(
        observer, target, kind, tick, owned_summary, agreement_type,
        owned_terms ? std::optional<std::string_view>(*owned_terms)
                    : std::nullopt);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Proposal sent.", id);
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::respond_to_proposal(
    int observer, std::int64_t proposal_id, bool accept, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || proposal_id <= 0)
    return invalid_request();
  const auto view = build_view(observer);
  if (!any(view.proposals, [&](const auto &proposal) {
        return proposal.proposal_id == proposal_id &&
               proposal.recipient_civilization_id == observer &&
               proposal.status == DiplomaticProposalStatus::pending;
      }))
    return action_unavailable();
  try {
    DiplomacySimulation(*state_).respond_to_proposal(proposal_id, observer,
                                                     accept, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           accept ? "Proposal accepted."
                                  : "Proposal rejected.",
                           proposal_id);
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::withdraw_proposal(
    int observer, std::int64_t proposal_id, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || proposal_id <= 0)
    return invalid_request();
  const auto view = build_view(observer);
  if (!any(view.proposals, [&](const auto &proposal) {
        return proposal.proposal_id == proposal_id &&
               proposal.proposer_civilization_id == observer &&
               proposal.status == DiplomaticProposalStatus::pending;
      }))
    return action_unavailable();
  try {
    DiplomacySimulation(*state_).withdraw_proposal(proposal_id, observer, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Proposal withdrawn.", proposal_id);
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::set_access_permission(
    int observer, int target, AccessPermission permission, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || target < 0 || target == observer ||
      !defined(permission))
    return invalid_request();
  if (!has_visible_active_communication(observer, target))
    return channel_unavailable();
  try {
    DiplomacySimulation(*state_).set_access_permission(observer, target,
                                                       permission, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Access permission updated.");
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult ObserverDiplomacyCommandService::declare_war(
    int observer, int target, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || target < 0 || target == observer)
    return invalid_request();
  if (!any(build_view(observer).contacts, [target](const auto &contact) {
        return contact.target_civilization_id == target &&
               contact.awareness >= ContactAwareness::identified;
      }))
    return action_unavailable();
  try {
    DiplomacySimulation(*state_).declare_war(observer, target, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "War declared.");
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::terminate_agreement(
    int observer, std::int64_t agreement_id, std::int64_t tick,
    std::string_view reason) {
  const std::string owned_reason(reason);
  if (!valid_actor_and_tick(observer, tick) || agreement_id <= 0 ||
      null_or_white_space(owned_reason))
    return invalid_request();
  const auto view = build_view(observer);
  const auto agreement = std::ranges::find_if(
      view.agreements, [agreement_id](const auto &candidate) {
        return candidate.agreement_id == agreement_id;
      });
  if (agreement == view.agreements.end()) return action_unavailable();
  if (agreement->status == DiplomaticAgreementStatus::terminated)
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Agreement was already terminated.", std::nullopt,
                           agreement_id);
  const int counterpart = agreement->civilization_a_id == observer
                              ? agreement->civilization_b_id
                              : agreement->civilization_a_id;
  if (!has_visible_active_communication(observer, counterpart))
    return channel_unavailable();
  try {
    const auto result = DiplomaticAgreementTerminationService(*state_).terminate(
        agreement_id, observer, tick, owned_reason);
    return observer_result(
        true, ObserverDiplomacyCommandStatus::accepted,
        result.terminated ? "Agreement terminated."
                          : "Agreement was already terminated.",
        std::nullopt, agreement_id);
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::communicate_territorial_claim(
    int observer, std::int64_t claim_id, int recipient, std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || claim_id <= 0 || recipient < 0 ||
      recipient == observer)
    return invalid_request();
  const auto view = build_view(observer);
  const auto claim =
      std::ranges::find_if(view.claims, [&](const auto &candidate) {
        return candidate.claim_id == claim_id &&
               candidate.claimant_civilization_id == observer &&
               candidate.active;
      });
  if (claim == view.claims.end()) return action_unavailable();
  if (!has_visible_active_communication(observer, recipient))
    return channel_unavailable();
  if (std::ranges::find(claim->known_to_civilization_ids, recipient) !=
      claim->known_to_civilization_ids.end())
    return observer_result(
        true, ObserverDiplomacyCommandStatus::accepted,
        "Territorial claim was already communicated.");
  try {
    DiplomacySimulation(*state_).communicate_territorial_claim(claim_id,
                                                               recipient, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Territorial claim communicated.");
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

ObserverDiplomacyCommandResult
ObserverDiplomacyCommandService::respond_to_territorial_claim(
    int observer, std::int64_t claim_id, TerritorialClaimResponse response,
    std::int64_t tick) {
  if (!valid_actor_and_tick(observer, tick) || claim_id <= 0 ||
      !defined(response) || response == TerritorialClaimResponse::none)
    return invalid_request();
  const auto view = build_view(observer);
  const auto claim =
      std::ranges::find_if(view.claims, [&](const auto &candidate) {
        return candidate.claim_id == claim_id && candidate.active;
      });
  if (claim == view.claims.end()) return action_unavailable();
  if (claim->claimant_civilization_id == observer) return invalid_request();
  if (!has_visible_active_communication(observer,
                                        claim->claimant_civilization_id))
    return channel_unavailable();
  const auto prior = std::ranges::find_if(
      view.claim_responses, [&](const auto &candidate) {
        return candidate.claim_id == claim_id &&
               candidate.responding_civilization_id == observer;
      });
  if (prior != view.claim_responses.end() && prior->response == response)
    return observer_result(
        true, ObserverDiplomacyCommandStatus::accepted,
        "Territorial claim response was already recorded.");
  try {
    DiplomacySimulation(*state_).respond_to_territorial_claim(
        claim_id, observer, response, tick);
    return observer_result(
        true, ObserverDiplomacyCommandStatus::accepted,
        response == TerritorialClaimResponse::recognized
            ? "Territorial claim recognized."
            : "Territorial claim disputed.");
  } catch (const DiplomacyArgumentError &) {
    return invalid_request();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid_request();
  } catch (const DiplomacyOperationError &) {
    return action_unavailable();
  }
}

CampaignDiplomacyTerritorialClaimCommandService::
    CampaignDiplomacyTerritorialClaimCommandService(
        DiplomacyState &state) noexcept
    : state_(&state) {}

CampaignTerritorialClaimCommandResult
CampaignDiplomacyTerritorialClaimCommandService::assert_territorial_claim(
    DiplomacyCampaignCommandWorldView world, int observer, int system_id,
    std::int64_t tick) {
  const auto invalid = [] {
    return CampaignTerritorialClaimCommandResult{
        false, ObserverDiplomacyCommandStatus::invalid_request,
        std::string(territorial_invalid_request_message), std::nullopt};
  };
  const auto unavailable = [] {
    return CampaignTerritorialClaimCommandResult{
        false, ObserverDiplomacyCommandStatus::action_unavailable,
        std::string(territorial_action_unavailable_message), std::nullopt};
  };
  if (observer < 0 || system_id < 0 || tick < 0 ||
      !any(world.civilizations,
           [observer](const auto &civilization) {
             return civilization.id == observer;
           }))
    return invalid();
  if (!any(world.systems,
           [system_id](const auto &system) { return system.id == system_id; }) ||
      !world.knowledge.is_system_known(observer, system_id))
    return unavailable();
  const auto view = state_->build_view_for(observer);
  const auto existing = std::ranges::find_if(
      view.claims, [&](const auto &claim) {
        return claim.active && claim.claimant_civilization_id == observer &&
               claim.system_id == system_id;
      });
  if (existing != view.claims.end())
    return {true, ObserverDiplomacyCommandStatus::accepted,
            "Territorial claim is already active.", existing->claim_id};
  try {
    const auto id = DiplomacySimulation(*state_).assert_territorial_claim(
        observer, system_id, tick);
    return {true, ObserverDiplomacyCommandStatus::accepted,
            "Territorial claim asserted.", id};
  } catch (const DiplomacyArgumentError &) {
    return invalid();
  } catch (const DiplomacyArgumentRangeError &) {
    return invalid();
  } catch (const DiplomacyOperationError &) {
    return unavailable();
  }
}

CampaignDiplomacyBorderWarningCommandService::
    CampaignDiplomacyBorderWarningCommandService(DiplomacyState &state) noexcept
    : state_(&state) {}

ObserverDiplomacyCommandResult
CampaignDiplomacyBorderWarningCommandService::issue_border_warning(
    DiplomacyCampaignCommandWorldView world, int issuer, int recipient,
    int system_id, std::int64_t tick) {
  const auto border_invalid = [] {
    return observer_result(false,
                           ObserverDiplomacyCommandStatus::invalid_request,
                           border_invalid_request_message);
  };
  const auto border_unavailable = [] {
    return observer_result(false,
                           ObserverDiplomacyCommandStatus::action_unavailable,
                           border_action_unavailable_message);
  };
  if (issuer < 0 || recipient < 0 || system_id < 0 || tick < 0 ||
      issuer == recipient ||
      !any(world.civilizations, [issuer](const auto &civilization) {
        return civilization.id == issuer;
      }))
    return border_invalid();
  const auto issuer_view = state_->build_view_for(issuer);
  if (!any(issuer_view.contacts, [recipient](const auto &contact) {
        return contact.target_civilization_id == recipient &&
               contact.communication_available &&
               contact.condition != ContactCondition::stale_or_lost;
      }))
    return channel_unavailable();
  if (!any(world.systems,
           [system_id](const auto &system) { return system.id == system_id; }) ||
      !world.knowledge.is_system_known(issuer, system_id))
    return border_unavailable();
  const bool has_claim = any(issuer_view.claims, [&](const auto &claim) {
    return claim.active && claim.claimant_civilization_id == issuer &&
           claim.system_id == system_id;
  });
  const bool has_colony = any(world.colonies, [&](const auto &colony) {
    return colony.civilization_id == issuer && colony.system_id == system_id;
  });
  if (!has_claim && !has_colony) return border_unavailable();
  if (state_->get_access_permission(issuer, recipient) ==
      AccessPermission::granted)
    return border_unavailable();
  if (any(issuer_view.recent_events, [&](const auto &event) {
        return event.kind == DiplomaticEventKind::border_warning_issued &&
               event.primary_civilization_id == issuer &&
               event.secondary_civilization_id == recipient &&
               event.system_id == system_id && event.tick == tick;
      }))
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Border warning was already issued.");
  try {
    DiplomacySimulation(*state_).issue_border_warning(issuer, recipient,
                                                      system_id, tick);
    return observer_result(true, ObserverDiplomacyCommandStatus::accepted,
                           "Border warning issued.");
  } catch (const DiplomacyArgumentError &) {
    return border_invalid();
  } catch (const DiplomacyArgumentRangeError &) {
    return border_invalid();
  } catch (const DiplomacyOperationError &) {
    return border_unavailable();
  }
}

} // namespace stellar::core
