#include "native_campaign_calendar.hpp"
#include "native_diplomacy_controller.hpp"

#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/species_environment.hpp>

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

[[nodiscard]] std::string words(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  bool boundary = true;
  for (char c : value) {
    if (c == '_') {
      out += ' ';
      boundary = true;
      continue;
    }
    if (c == ' ') {
      out += ' ';
      boundary = true;
      continue;
    }
    out += boundary ? static_cast<char>(
                          std::toupper(static_cast<unsigned char>(c)))
                    : c;
    boundary = false;
  }
  return out;
}

[[nodiscard]] std::string_view name_of(ContactAwareness value) noexcept {
  switch (value) {
  case ContactAwareness::unknown: return "unknown";
  case ContactAwareness::detected_unidentified: return "detected unidentified";
  case ContactAwareness::identified: return "identified";
  case ContactAwareness::contact_possible: return "contact possible";
  case ContactAwareness::contact_established: return "contact established";
  case ContactAwareness::communication_available: return "communication available";
  }
  return "unknown";
}
[[nodiscard]] std::string_view name_of(ContactCondition value) noexcept {
  switch (value) {
  case ContactCondition::active: return "active";
  case ContactCondition::hostile: return "hostile";
  case ContactCondition::stale_or_lost: return "stale or lost";
  }
  return "unknown";
}
[[nodiscard]] std::string_view name_of(DiplomaticPoliticalState value) noexcept {
  switch (value) {
  case DiplomaticPoliticalState::unknown: return "Unknown";
  case DiplomaticPoliticalState::peace: return "Peace";
  case DiplomaticPoliticalState::hostile: return "Hostile";
  case DiplomaticPoliticalState::at_war: return "AtWar";
  case DiplomaticPoliticalState::ceasefire: return "Ceasefire";
  }
  return "Unknown";
}
[[nodiscard]] std::string_view name_of(AccessPermission value) noexcept {
  switch (value) {
  case AccessPermission::unspecified: return "UNSPECIFIED";
  case AccessPermission::granted: return "GRANTED";
  case AccessPermission::denied: return "DENIED";
  }
  return "UNSPECIFIED";
}
[[nodiscard]] std::string_view name_of(DiplomaticAgreementType value) noexcept {
  switch (value) {
  case DiplomaticAgreementType::peace: return "peace";
  case DiplomaticAgreementType::non_aggression: return "non aggression";
  case DiplomaticAgreementType::access: return "access";
  case DiplomaticAgreementType::trade: return "trade";
  case DiplomaticAgreementType::research_exchange: return "research exchange";
  case DiplomaticAgreementType::ceasefire: return "ceasefire";
  case DiplomaticAgreementType::cooperation: return "cooperation";
  }
  return "agreement";
}
[[nodiscard]] std::string_view name_of(DiplomaticAgreementStatus value) noexcept {
  switch (value) {
  case DiplomaticAgreementStatus::active: return "ACTIVE";
  case DiplomaticAgreementStatus::terminated: return "TERMINATED";
  }
  return "unknown";
}
[[nodiscard]] std::string_view name_of(DiplomaticProposalKind value) noexcept {
  switch (value) {
  case DiplomaticProposalKind::agreement: return "agreement";
  case DiplomaticProposalKind::access_request: return "access request";
  case DiplomaticProposalKind::trade_offer: return "trade offer";
  case DiplomaticProposalKind::demand: return "demand";
  case DiplomaticProposalKind::peace_offer: return "peace offer";
  case DiplomaticProposalKind::ceasefire_offer: return "ceasefire offer";
  }
  return "proposal";
}
[[nodiscard]] std::string_view name_of(DiplomaticEventKind value) noexcept {
  switch (value) {
  case DiplomaticEventKind::contact_observed: return "contact observed";
  case DiplomaticEventKind::contact_established: return "contact established";
  case DiplomaticEventKind::communication_available: return "communication available";
  case DiplomaticEventKind::contact_lost: return "contact lost";
  case DiplomaticEventKind::access_changed: return "access changed";
  case DiplomaticEventKind::claim_asserted: return "claim asserted";
  case DiplomaticEventKind::claim_communicated: return "claim communicated";
  case DiplomaticEventKind::claim_responded: return "claim responded";
  case DiplomaticEventKind::border_warning_issued: return "border warning issued";
  case DiplomaticEventKind::trespass_recorded: return "trespass recorded";
  case DiplomaticEventKind::proposal_sent: return "proposal sent";
  case DiplomaticEventKind::proposal_accepted: return "proposal accepted";
  case DiplomaticEventKind::proposal_rejected: return "proposal rejected";
  case DiplomaticEventKind::proposal_withdrawn: return "proposal withdrawn";
  case DiplomaticEventKind::proposal_expired: return "proposal expired";
  case DiplomaticEventKind::agreement_activated: return "agreement activated";
  case DiplomaticEventKind::agreement_terminated: return "agreement terminated";
  case DiplomaticEventKind::relationship_changed: return "relationship changed";
  case DiplomaticEventKind::war_declared: return "war declared";
  }
  return "event";
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
                                 std::size_t contact_index) {
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
               : "Civilization " + std::to_string(id);
  };

  Projection out;
  auto &v = out.view;
  v.campaign_generation = generation;
  v.observer_civilization_id = observer;
  v.date = format_campaign_date(frame.clock().simulation_days());
  out.signature = signature_for(view, v.date);

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
      row.status = relationship ? std::string(name_of(relationship->political_state))
                                : "NO FORMAL RELATIONSHIP";
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
      row.display_name = "UNKNOWN CONTACT";
      row.status = "IDENTITY UNKNOWN";
    }
    const bool channel = contact.communication_available &&
                         contact.condition != ContactCondition::stale_or_lost;
    row.communication_available = channel;
    row.communication =
        channel ? "CHANNEL AVAILABLE" : "CHANNEL UNAVAILABLE";
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
                         : "UNIDENTIFIED CONTACT " + contact_row.contact_id;
    s.contact_status =
        std::string(name_of(raw.awareness)) + " · " +
        std::string(name_of(raw.condition)) + " · " +
        std::to_string(static_cast<int>(std::lround(raw.confidence * 100.))) +
        "% confidence";
    s.communication_status =
        channel ? "Channel available" : "Channel unavailable";

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
        relationship ? std::string(name_of(relationship->political_state))
                     : (s.target_civilization_id ? "No formal relationship"
                                                 : "Identity unknown");
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
    s.their_access = std::string(name_of(inbound));
    s.our_access = std::string(name_of(outbound));
    s.access_summary =
        s.target_civilization_id
            ? "Your access: " + s.our_access + " · Their access: " + s.their_access
            : "Transit rights unavailable until identification";

    if (s.target_civilization_id) {
      const auto target = *s.target_civilization_id;
      for (const auto &agreement : view.agreements) {
        if (!pair_matches(agreement.civilization_a_id,
                          agreement.civilization_b_id, observer, target))
          continue;
        NativeDiplomacyAgreementRow row;
        row.agreement_id = agreement.agreement_id;
        row.type = words(name_of(agreement.type));
        row.status = std::string(name_of(agreement.status));
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
        return row.status == "ACTIVE";
      });
      if (active == 0) {
        s.agreements_summary = "No active agreements";
      } else {
        s.agreements_summary.clear();
        bool first = true;
        for (const auto &row : v.agreements)
          if (row.status == "ACTIVE") {
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
        row.direction = incoming ? "INCOMING" : "OUTGOING";
        row.kind = words(name_of(proposal.kind));
        if (proposal.agreement_type)
          row.agreement_type = words(name_of(*proposal.agreement_type));
        row.summary = proposal.summary;
        row.can_accept = incoming;
        row.can_reject = incoming;
        row.can_withdraw = !incoming;
        v.proposals.push_back(std::move(row));
      }
      std::ranges::sort(v.proposals, {}, &NativeDiplomacyProposalRow::proposal_id);
      s.proposal_summary = v.proposals.empty()
                               ? "No pending proposals"
                               : std::to_string(v.proposals.size()) +
                                     " pending proposal(s)";

      for (const auto &event : view.recent_events) {
        if (!pair_matches(event.primary_civilization_id,
                          event.secondary_civilization_id, observer, target))
          continue;
        NativeDiplomacyHistoryRow row;
        row.event_id = event.event_id;
        row.date = format_campaign_date(
            static_cast<double>(event.tick) /
            static_cast<double>(DiplomacyCampaignClock::ticks_per_simulation_day));
        row.kind = words(name_of(event.kind));
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
    }
  }

  return out;
}

[[nodiscard]] NativeDiplomacyCommandOutcome stale() {
  return {false, "The diplomacy state changed; review the current terms."};
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
        return contact.status == "NO FORMAL RELATIONSHIP" ||
               contact.status == "Unknown" || contact.status == "Peace";
      case NativeDiplomacyContactFilter::hostile:
        return contact.status == "Hostile";
      case NativeDiplomacyContactFilter::at_war:
        return contact.status == "AtWar";
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
  auto p = project(frame, generation, contact_index);
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
    std::optional<std::int64_t> proposal_id) {
  require_owner();
  if (!generation_ || *generation_ != generation || revision != revision_ ||
      !signature_)
    return stale();
  auto check = project(frame, generation, 0);
  if (check.signature != *signature_) return stale();
  auto &runtime = frame.runtime();
  const auto observer =
      runtime.world().campaign().player_civilization_id;
  const auto tick = DiplomacyCampaignClock::from_simulation_days(
      frame.clock().simulation_days());
  ObserverDiplomacyCommandService service(runtime.diplomacy());
  ObserverDiplomacyCommandResult result;
  switch (action) {
  case DiplomacyWorkspaceAction::establish_communication:
    if (!target) return stale();
    result = service.establish_communication(observer, *target, tick);
    break;
  case DiplomacyWorkspaceAction::propose_non_aggression:
    if (!target) return stale();
    result = service.send_proposal(
        observer, *target, DiplomaticProposalKind::agreement, tick,
        "Proposal for a non-aggression agreement.",
        DiplomaticAgreementType::non_aggression);
    break;
  case DiplomacyWorkspaceAction::request_access:
    if (!target) return stale();
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::access_request, tick,
                                   "Request for transit access.");
    break;
  case DiplomacyWorkspaceAction::offer_peace:
    if (!target) return stale();
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::peace_offer, tick,
                                   "Offer to establish peace.");
    break;
  case DiplomacyWorkspaceAction::offer_ceasefire:
    if (!target) return stale();
    result = service.send_proposal(observer, *target,
                                   DiplomaticProposalKind::ceasefire_offer, tick,
                                   "Offer to establish a ceasefire.");
    break;
  case DiplomacyWorkspaceAction::grant_access:
    if (!target) return stale();
    result = service.set_access_permission(observer, *target,
                                           AccessPermission::granted, tick);
    break;
  case DiplomacyWorkspaceAction::deny_access:
    if (!target) return stale();
    result = service.set_access_permission(observer, *target,
                                           AccessPermission::denied, tick);
    break;
  case DiplomacyWorkspaceAction::declare_war:
    if (!target) return stale();
    result = service.declare_war(observer, *target, tick);
    break;
  case DiplomacyWorkspaceAction::accept_proposal:
    if (!proposal_id) return stale();
    result = service.respond_to_proposal(observer, *proposal_id, true, tick);
    break;
  case DiplomacyWorkspaceAction::reject_proposal:
    if (!proposal_id) return stale();
    result = service.respond_to_proposal(observer, *proposal_id, false, tick);
    break;
  case DiplomacyWorkspaceAction::withdraw_proposal:
    if (!proposal_id) return stale();
    result = service.withdraw_proposal(observer, *proposal_id, tick);
    break;
  }
  if (result.accepted) signature_.reset();
  return {result.accepted, result.message};
}

void NativeDiplomacyController::require_owner() const {
  if (owner_ != std::this_thread::get_id())
    throw std::logic_error(
        "Native diplomacy commands only run on the owning thread.");
}

} // namespace stellar::native_diplomacy
