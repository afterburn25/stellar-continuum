#include <stellar/core/campaign_event_history.hpp>

#include <stellar/core/diplomacy_lifecycle.hpp>

#include <algorithm>
#include <string>

namespace stellar::core {

namespace {

using engine::HistoryEvent;

std::string_view exploration_category(ExplorationEventType type) {
  switch (type) {
  case ExplorationEventType::SystemDetected:
    return "exploration.system_detected";
  case ExplorationEventType::SystemReconnoitered:
    return "exploration.system_reconnoitered";
  case ExplorationEventType::SystemSurveyStarted:
    return "exploration.system_survey_started";
  case ExplorationEventType::SystemSurveyed:
    return "exploration.system_surveyed";
  case ExplorationEventType::ResourceSignatureDetected:
    return "exploration.resource_signature";
  case ExplorationEventType::AnomalySignatureDetected:
    return "exploration.anomaly_signature";
  case ExplorationEventType::ActivitySignatureDetected:
    return "exploration.activity_signature";
  case ExplorationEventType::ResourceSurveyed:
    return "exploration.resource_surveyed";
  case ExplorationEventType::AnomalySurveyed:
    return "exploration.anomaly_surveyed";
  case ExplorationEventType::NativeCivilizationSurveyed:
    return "exploration.native_civilization_surveyed";
  case ExplorationEventType::SensorContact:
    return "exploration.sensor_contact";
  case ExplorationEventType::FirstContact:
    return "exploration.first_contact";
  }
  return "exploration.event";
}

double exploration_significance(ExplorationEventType type) {
  switch (type) {
  case ExplorationEventType::FirstContact:
    return 0.95;
  case ExplorationEventType::NativeCivilizationSurveyed:
    return 0.85;
  case ExplorationEventType::AnomalySurveyed:
    return 0.6;
  case ExplorationEventType::SystemSurveyed:
    return 0.5;
  case ExplorationEventType::ResourceSurveyed:
    return 0.45;
  case ExplorationEventType::SensorContact:
    return 0.35;
  case ExplorationEventType::ResourceSignatureDetected:
  case ExplorationEventType::AnomalySignatureDetected:
  case ExplorationEventType::ActivitySignatureDetected:
    return 0.3;
  case ExplorationEventType::SystemDetected:
  case ExplorationEventType::SystemReconnoitered:
    return 0.25;
  case ExplorationEventType::SystemSurveyStarted:
    return 0.2;
  }
  return 0.4;
}

std::string_view combat_category(CombatEventType type) {
  switch (type) {
  case CombatEventType::EngagementStarted:
    return "war.engagement_started";
  case CombatEventType::DamageApplied:
    return "war.damage_applied";
  case CombatEventType::FleetRetreatInitiated:
    return "war.fleet_retreat";
  case CombatEventType::FleetEscaped:
    return "war.fleet_escaped";
  case CombatEventType::FleetDestroyed:
    return "war.fleet_destroyed";
  case CombatEventType::EngagementEnded:
    return "war.engagement_ended";
  }
  return "war.event";
}

double combat_significance(CombatEventType type) {
  switch (type) {
  case CombatEventType::FleetDestroyed:
    return 0.9;
  case CombatEventType::EngagementStarted:
    return 0.7;
  case CombatEventType::EngagementEnded:
    return 0.55;
  case CombatEventType::FleetRetreatInitiated:
  case CombatEventType::FleetEscaped:
    return 0.4;
  case CombatEventType::DamageApplied:
    return 0.1; // high volume — surface via min_significance queries
  }
  return 0.5;
}

std::string_view diplomacy_category(DiplomaticEventKind kind) {
  using enum DiplomaticEventKind;
  switch (kind) {
  case contact_observed:        return "diplomacy.contact_observed";
  case contact_established:     return "diplomacy.contact_established";
  case communication_available: return "diplomacy.communication_available";
  case contact_lost:            return "diplomacy.contact_lost";
  case access_changed:          return "diplomacy.access_changed";
  case claim_asserted:          return "diplomacy.claim_asserted";
  case claim_communicated:      return "diplomacy.claim_communicated";
  case claim_responded:         return "diplomacy.claim_responded";
  case border_warning_issued:   return "diplomacy.border_warning";
  case trespass_recorded:       return "diplomacy.trespass";
  case proposal_sent:           return "diplomacy.proposal_sent";
  case proposal_accepted:       return "diplomacy.proposal_accepted";
  case proposal_rejected:       return "diplomacy.proposal_rejected";
  case proposal_withdrawn:      return "diplomacy.proposal_withdrawn";
  case proposal_expired:        return "diplomacy.proposal_expired";
  case agreement_activated:     return "diplomacy.agreement_activated";
  case agreement_terminated:    return "diplomacy.agreement_terminated";
  case relationship_changed:    return "diplomacy.relationship_changed";
  case war_declared:            return "diplomacy.war_declared";
  }
  return "diplomacy.event";
}

double diplomacy_significance(DiplomaticEventKind kind) {
  using enum DiplomaticEventKind;
  switch (kind) {
  case war_declared:            return 0.95;
  case agreement_activated:
  case agreement_terminated:    return 0.8;
  case proposal_accepted:
  case contact_established:     return 0.7;
  case claim_asserted:
  case border_warning_issued:
  case proposal_rejected:       return 0.6;
  case proposal_sent:
  case contact_lost:
  case communication_available:
  case claim_responded:         return 0.5;
  case contact_observed:
  case access_changed:
  case claim_communicated:
  case proposal_withdrawn:      return 0.4;
  case trespass_recorded:
  case proposal_expired:
  case relationship_changed:    return 0.3;
  }
  return 0.5;
}

// The journal's raw summary is internal phrasing ("Observer 1
// recorded ...") — the notification feed already substitutes generic
// kind text for the same reason, and the record's structured fields
// (actors/location/tags) carry the detail.
std::string_view diplomacy_summary(DiplomaticEventKind kind) {
  using enum DiplomaticEventKind;
  switch (kind) {
  case contact_observed:        return "A contact was observed.";
  case contact_established:     return "A diplomatic contact was established.";
  case communication_available: return "A communication channel is now available.";
  case contact_lost:            return "A diplomatic contact was lost.";
  case access_changed:          return "Territorial access changed.";
  case claim_asserted:          return "A territorial claim was asserted.";
  case claim_communicated:      return "A territorial claim was communicated.";
  case claim_responded:         return "A territorial claim received a response.";
  case border_warning_issued:   return "A border warning was issued.";
  case trespass_recorded:       return "A trespass was recorded.";
  case proposal_sent:           return "A diplomatic proposal has been sent.";
  case proposal_accepted:       return "A diplomatic proposal has been accepted.";
  case proposal_rejected:       return "A diplomatic proposal has been declined.";
  case proposal_withdrawn:      return "A diplomatic proposal was withdrawn.";
  case proposal_expired:        return "A diplomatic proposal has expired.";
  case agreement_activated:     return "A diplomatic agreement is now active.";
  case agreement_terminated:    return "A diplomatic agreement has ended.";
  case relationship_changed:    return "A diplomatic relationship has changed.";
  case war_declared:            return "A declaration of war has been recorded.";
  }
  return "A diplomatic event has been recorded.";
}

HistoryEvent base(std::string_view category, double at_day,
                  std::string summary) {
  HistoryEvent e;
  e.category = std::string(category);
  e.at_day = at_day;
  e.summary = std::move(summary);
  return e;
}

void visible_to_involved(HistoryEvent &e) {
  // Involved parties know their own events; everyone else does not.
  // Public knowledge is left to the knowledge layer, not assumed here.
  e.visible_to = e.actors;
}

} // namespace

std::vector<engine::HistoryEvent>
history_events_for_step(const IntegratedAdaptiveCampaignStepResult &step,
                        double end_day,
                        std::span<const DiplomaticHistoryEventSnapshot>
                            diplomacy_events) {
  std::vector<HistoryEvent> out;
  const auto civ = [](int id) { return static_cast<std::uint64_t>(id); };
  const auto tag_i = [](std::string prefix, int id) {
    return prefix + std::to_string(id);
  };

  out.reserve(step.core.construction_events.size() +
              step.core.shipbuilding_events.size() +
              step.core.research_events.size() + step.research_events.size() +
              step.core.exploration_events.size() +
              step.core.combat_events.size() +
              step.core.colonization_events.size());

  for (const auto &ev : step.core.construction_events) {
    auto e = base("construction.project", end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    e.significance = 0.35;
    e.tags = {"project:" + ev.project_id};
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.core.shipbuilding_events) {
    auto e = base("shipbuilding.ship", end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    e.significance = 0.45;
    e.tags = {"design:" + ev.design_id, tag_i("fleet:", ev.fleet_id)};
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.core.research_events) {
    auto e = base("research.legacy", end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    e.significance = 0.55;
    e.tags = {"tech:" + ev.technology_id};
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.research_events) {
    auto e = base("research.adaptive", end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    e.significance = ev.is_outcome ? 0.6 : 0.25;
    e.tags = {"node:" + ev.node_id};
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.core.exploration_events) {
    auto e = base(exploration_category(ev.type), end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    if (ev.target_civilization_id)
      e.actors.push_back(civ(*ev.target_civilization_id));
    e.location = static_cast<std::uint64_t>(ev.system_id);
    e.significance = exploration_significance(ev.type);
    e.tags = {tag_i("fleet:", ev.fleet_id)};
    if (ev.planetary_body_id)
      e.tags.push_back(tag_i("body:", *ev.planetary_body_id));
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.core.combat_events) {
    auto e = base(combat_category(ev.type), end_day, ev.message);
    e.actors = {civ(ev.actor_civilization_id)};
    if (ev.target_civilization_id)
      e.actors.push_back(civ(*ev.target_civilization_id));
    if (ev.system_id)
      e.location = static_cast<std::uint64_t>(*ev.system_id);
    e.significance = combat_significance(ev.type);
    e.tags = {tag_i("fleet:", ev.actor_fleet_id)};
    if (ev.target_fleet_id)
      e.tags.push_back(tag_i("fleet:", *ev.target_fleet_id));
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : step.core.colonization_events) {
    auto e = base("colony.founded", end_day, ev.message);
    e.actors = {civ(ev.civilization_id)};
    e.location = static_cast<std::uint64_t>(ev.system_id);
    e.significance = 0.8;
    e.tags = {tag_i("fleet:", ev.fleet_id), tag_i("colony:", ev.colony_id)};
    visible_to_involved(e);
    out.push_back(std::move(e));
  }
  for (const auto &ev : diplomacy_events) {
    // Journal ticks are campaign-milli-days (DiplomacyCampaignClock) —
    // at_day keeps the event's own timestamp rather than the step end.
    auto e = base(diplomacy_category(ev.kind),
                  static_cast<double>(ev.tick) /
                      DiplomacyCampaignClock::ticks_per_simulation_day,
                  std::string(diplomacy_summary(ev.kind)));
    e.actors = {civ(ev.primary_civilization_id)};
    e.tags = {tag_i("civ:", ev.primary_civilization_id)};
    if (ev.secondary_civilization_id) {
      e.actors.push_back(civ(*ev.secondary_civilization_id));
      e.tags.push_back(tag_i("civ:", *ev.secondary_civilization_id));
    }
    if (ev.system_id) {
      e.location = static_cast<std::uint64_t>(*ev.system_id);
      e.tags.push_back(tag_i("system:", *ev.system_id));
    }
    e.significance = diplomacy_significance(ev.kind);
    // The journal entry's own audience list IS the visibility set —
    // diplomacy already decided who knows.
    e.visible_to.assign(ev.known_to_civilization_ids.begin(),
                        ev.known_to_civilization_ids.end());
    out.push_back(std::move(e));
  }
  return out;
}

void widen_history_visibility(std::vector<engine::HistoryEvent> &events,
                              const FreshCampaignState &campaign) {
  for (auto &e : events) {
    if (e.location == 0) continue;
    // Diplomatic entries carry their own authoritative audience
    // (known_to_civilization_ids) — widening would reveal actor
    // identities to observers diplomacy explicitly excluded.
    if (e.category.starts_with("diplomacy.")) continue;
    const auto system = static_cast<int>(e.location);
    for (const auto &civilization : campaign.civilizations) {
      if (!campaign.knowledge.is_system_known(civilization.id, system))
        continue;
      const auto observer = static_cast<std::uint64_t>(civilization.id);
      if (std::find(e.visible_to.begin(), e.visible_to.end(), observer) ==
          e.visible_to.end())
        e.visible_to.push_back(observer);
    }
  }
}

std::vector<std::uint64_t>
record_step_events(engine::EventHistory &history,
                   const IntegratedAdaptiveCampaignStepResult &step,
                   double end_day) {
  auto events = history_events_for_step(step, end_day);
  std::vector<std::uint64_t> ids;
  ids.reserve(events.size());
  for (auto &e : events)
    ids.push_back(history.record(std::move(e)));
  return ids;
}

std::vector<std::uint64_t>
record_step_events(engine::EventHistory &history,
                   const IntegratedAdaptiveCampaignStepResult &step,
                   double end_day, const FreshCampaignState &campaign,
                   std::span<const DiplomaticHistoryEventSnapshot>
                       diplomacy_events) {
  auto events = history_events_for_step(step, end_day, diplomacy_events);
  widen_history_visibility(events, campaign);
  std::vector<std::uint64_t> ids;
  ids.reserve(events.size());
  for (auto &e : events)
    ids.push_back(history.record(std::move(e)));
  return ids;
}

std::size_t maintain_chronicle(engine::EventHistory &history,
                               double current_day) {
  if (history.size() < history.capacity() * 9 / 10) return 0;
  return history.prune_before(current_day - chronicle_prune_horizon_days,
                              chronicle_report_significance);
}

} // namespace stellar::core
