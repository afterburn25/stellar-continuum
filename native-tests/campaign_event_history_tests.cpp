#include <stellar/core/campaign_event_history.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// Campaign event history adapter tests — the Core consumer contract for
// engine::EventHistory. Covers category vocabulary, actor/visibility
// mapping, significance ordering and at_day attribution.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::core;
using stellar::engine::HistoryEvent;
using stellar::engine::HistoryQuery;

const HistoryEvent *only(const std::vector<HistoryEvent> &events,
                         const std::string &category) {
  const HistoryEvent *found = nullptr;
  int count = 0;
  for (const auto &e : events)
    if (e.category == category) {
      found = &e;
      ++count;
    }
  if (count != 1) return nullptr;
  return found;
}

bool has_tag(const HistoryEvent &e, const std::string &tag) {
  return std::find(e.tags.begin(), e.tags.end(), tag) != e.tags.end();
}

} // namespace

int main() {
  IntegratedAdaptiveCampaignStepResult step;
  step.core.construction_events.push_back(
      {7, "project.shipyard", "Shipyard completed"});
  step.core.shipbuilding_events.push_back(
      {7, 42, "design.frigate", "Frigate commissioned"});
  step.core.research_events.push_back({7, "tech.fusion", "Fusion researched"});
  step.research_events.push_back({7, "node.drives", "Drive node unlocked", true});
  step.research_events.push_back(
      {7, "node.armor", "Armor progress", false});

  ExplorationEvent first_contact;
  first_contact.type = ExplorationEventType::FirstContact;
  first_contact.civilization_id = 7;
  first_contact.fleet_id = 42;
  first_contact.system_id = 9;
  first_contact.target_civilization_id = 3;
  first_contact.planetary_body_id = 55;
  first_contact.message = "First contact in system 9";
  step.core.exploration_events.push_back(first_contact);

  ExplorationEvent survey;
  survey.type = ExplorationEventType::SystemSurveyed;
  survey.civilization_id = 7;
  survey.fleet_id = 42;
  survey.system_id = 4;
  survey.message = "System 4 surveyed";
  step.core.exploration_events.push_back(survey);

  CombatEvent destroyed;
  destroyed.type = CombatEventType::FleetDestroyed;
  destroyed.system_id = 9;
  destroyed.actor_civilization_id = 3;
  destroyed.actor_fleet_id = 8;
  destroyed.target_civilization_id = 7;
  destroyed.target_fleet_id = 42;
  destroyed.message = "Fleet 42 destroyed";
  step.core.combat_events.push_back(destroyed);

  CombatEvent damage;
  damage.type = CombatEventType::DamageApplied;
  damage.system_id = 9;
  damage.actor_civilization_id = 3;
  damage.actor_fleet_id = 8;
  damage.target_civilization_id = 7;
  damage.message = "Exchange of fire";
  step.core.combat_events.push_back(damage);

  step.core.colonization_events.push_back(
      {7, 42, 11, 101, "Colony established on world 101"});

  const double end_day = 152.5;
  const auto events = history_events_for_step(step, end_day);
  check(events.size() == 10, "every emitted event mapped");

  // at_day attribution is uniform and correct.
  for (const auto &e : events)
    check(e.at_day == end_day, "at_day is the step end day");

  // Category vocabulary and fields.
  {
    const auto *e = only(events, "construction.project");
    check(e && e->actors == std::vector<std::uint64_t>{7},
          "construction event actor");
    check(e && has_tag(*e, "project:project.shipyard"),
          "construction project tag");
  }
  {
    const auto *e = only(events, "shipbuilding.ship");
    check(e && has_tag(*e, "design:design.frigate") &&
              has_tag(*e, "fleet:42"),
          "shipbuilding tags");
  }
  {
    check(only(events, "research.legacy") != nullptr,
          "legacy research mapped");
    const auto *outcome = [&] {
      for (const auto &e : events)
        if (e.category == "research.adaptive" && e.significance > 0.5)
          return &e;
      return static_cast<const HistoryEvent *>(nullptr);
    }();
    check(outcome && has_tag(*outcome, "node:node.drives"),
          "adaptive research outcome has node tag");
    // Progress events are lower-significance records, not dropped.
    int adaptive = 0;
    for (const auto &e : events)
      if (e.category == "research.adaptive") ++adaptive;
    check(adaptive == 2, "both adaptive research events recorded");
  }
  {
    const auto *e = only(events, "exploration.first_contact");
    check(e && e->significance >= 0.9, "first contact is major");
    check(e && e->actors == std::vector<std::uint64_t>{7, 3},
          "first contact lists both civilizations");
    check(e && e->location == 9, "first contact located at system");
    check(e && has_tag(*e, "body:55"), "first contact keeps body tag");
  }
  {
    const auto *e = only(events, "exploration.system_surveyed");
    check(e && e->significance < 0.9, "routine survey below major");
    check(e && e->visible_to == std::vector<std::uint64_t>{7},
          "survey visible to owner only");
  }
  {
    const auto *e = only(events, "war.fleet_destroyed");
    check(e && e->significance >= 0.85, "fleet destroyed is major");
    check(e && e->actors == std::vector<std::uint64_t>{3, 7},
          "combat lists aggressor then victim");
    check(e && e->visible_to == std::vector<std::uint64_t>{3, 7},
          "combat visible to both parties");
    check(e && has_tag(*e, "fleet:8") && has_tag(*e, "fleet:42"),
          "combat keeps both fleet tags");
    const auto *d = only(events, "war.damage_applied");
    check(d && d->significance < 0.2, "damage ticks stay low-significance");
  }
  {
    const auto *e = only(events, "colony.founded");
    check(e && e->location == 11 && has_tag(*e, "colony:101"),
          "colony founding located and tagged");
  }

  // Diplomatic journal entries join the chronicle with their own
  // timestamp, actors, location, significance and journal audience —
  // and generic kind text, never the internal journal summary.
  std::vector<DiplomaticHistoryEventSnapshot> diplomacy_events;
  {
    DiplomaticHistoryEventSnapshot war;
    war.event_id = 12;
    war.tick = 152400; // milli-days → at_day 152.4
    war.kind = DiplomaticEventKind::war_declared;
    war.primary_civilization_id = 3;
    war.secondary_civilization_id = 7;
    war.system_id = 9;
    war.summary = "Observer 3 internal phrasing";
    war.known_to_civilization_ids = {3, 7};
    diplomacy_events.push_back(war);
    DiplomaticHistoryEventSnapshot treaty;
    treaty.event_id = 13;
    treaty.tick = 152450;
    treaty.kind = DiplomaticEventKind::agreement_activated;
    treaty.primary_civilization_id = 5;
    treaty.secondary_civilization_id = 7;
    treaty.summary = "internal";
    treaty.known_to_civilization_ids = {5, 7, 11};
    diplomacy_events.push_back(treaty);
  }
  const auto dip_mapped =
      history_events_for_step(step, end_day, diplomacy_events);
  check(dip_mapped.size() == 12, "diplomatic entries mapped too");
  {
    const auto *e = only(dip_mapped, "diplomacy.war_declared");
    check(e && e->at_day == 152.4,
          "diplomatic entry keeps its journal timestamp");
    check(e && e->summary == "A declaration of war has been recorded.",
          "generic kind text replaces internal journal summary");
    check(e && e->actors == std::vector<std::uint64_t>{3, 7} &&
              e->location == 9 && e->significance >= 0.9,
          "war declaration fields");
    check(e && e->visible_to == std::vector<std::uint64_t>{3, 7},
          "journal audience is the visibility set");
    check(e && has_tag(*e, "civ:3") && has_tag(*e, "civ:7") &&
              has_tag(*e, "system:9"),
          "diplomatic entity tags");
    const auto *t = only(dip_mapped, "diplomacy.agreement_activated");
    check(t && t->visible_to ==
                   std::vector<std::uint64_t>{5, 7, 11},
          "agreement audience includes observers");
  }

  // A restored journal entry can carry an empty audience — EventHistory
  // reads empty visible_to as public, so the adapter falls back to the
  // involved parties instead of leaking the event to every observer.
  {
    DiplomaticHistoryEventSnapshot secret;
    secret.event_id = 14;
    secret.tick = 152500;
    secret.kind = DiplomaticEventKind::proposal_sent;
    secret.primary_civilization_id = 4;
    secret.secondary_civilization_id = 8;
    secret.summary = "internal";
    secret.known_to_civilization_ids = {};
    const auto mapped =
        history_events_for_step(step, end_day,
                                std::vector{secret});
    const auto *e = only(mapped, "diplomacy.proposal_sent");
    check(e && e->visible_to == std::vector<std::uint64_t>{4, 8},
          "empty journal audience falls back to involved parties");
  }

  // The journal watermark accessors feed exactly-once recording.
  {
    DiplomacyStateSnapshot snap;
    DiplomaticHistoryEventSnapshot e1;
    e1.event_id = 11; e1.tick = 1000; e1.summary = "older";
    e1.primary_civilization_id = 1;
    DiplomaticHistoryEventSnapshot e2 = e1;
    e2.event_id = 13; e2.tick = 2000;
    DiplomaticHistoryEventSnapshot e3 = e1;
    e3.event_id = 14; e3.tick = 3000;
    snap.recent_history = {e1, e2, e3};
    auto state = DiplomacyState::restore(snap);
    check(state.history_latest_event_id() == 14,
          "latest journal id reported");
    const auto tail = state.history_events_since(11);
    check(tail.size() == 2 && tail.front().event_id == 13 &&
              tail.back().event_id == 14,
          "journal tail returns only newer entries in order");
  }

  // Privacy: an uninvolved observer sees nothing from this step.
  {
    stellar::engine::EventHistory h;
    const auto ids = record_step_events(h, step, end_day);
    check(ids.size() == 10, "record assigns ids");
    check(h.feed(/*observer*/ 5, /*since*/ 0.0).empty(),
          "uninvolved civ sees no private events");
    // Civ 7 is involved in every event here (including combat as the
    // victim); civ 3 only in first contact + the two combat events.
    check(h.feed(7, 0.0).size() == 10, "actor civ sees all its events");
    check(h.feed(3, 0.0).size() == 3, "other civ sees only its events");
    check(h.feed(std::nullopt, 0.0, 0.8).size() == 3,
          "omniscient major-event feed: first contact + fleet destroyed + "
          "colony founding");
  }

  // Knowledge widening: civs that know the event's system see it;
  // locationless events stay involved-party-only.
  {
    FreshCampaignState campaign;
    Civilization c3, c5, c7, c11;
    c3.id = 3; c5.id = 5; c7.id = 7; c11.id = 11;
    campaign.civilizations = {c3, c5, c7, c11};
    campaign.knowledge.reveal_system(5, 9);   // civ 5 knows system 9
    campaign.knowledge.reveal_system(3, 9);   // aggressor knows it too
    campaign.knowledge.reveal_system(11, 4);  // civ 11 knows only system 4

    auto widened = history_events_for_step(step, end_day, diplomacy_events);
    widen_history_visibility(widened, campaign);

    const auto *battle = only(widened, "war.fleet_destroyed");
    check(battle != nullptr, "battle located at system 9");
    check(battle && std::find(battle->visible_to.begin(),
                              battle->visible_to.end(), 5ULL) !=
                        battle->visible_to.end(),
          "observer knowing the system sees the battle");
    check(battle && std::find(battle->visible_to.begin(),
                              battle->visible_to.end(), 11ULL) ==
                        battle->visible_to.end(),
          "uninformed civ excluded");
    const auto *research = only(widened, "research.legacy");
    check(research && research->visible_to ==
                          std::vector<std::uint64_t>{7},
          "locationless research stays private");
    const auto *surveyed = only(widened, "exploration.system_surveyed");
    check(surveyed && std::find(surveyed->visible_to.begin(),
                                surveyed->visible_to.end(), 11ULL) !=
                          surveyed->visible_to.end(),
          "civ knowing system 4 sees the survey there");

    // Diplomatic entries are exempt from knowledge widening — their
    // journal audience is authoritative; a civ that merely knows the
    // war's system must not learn the belligerents' identities.
    const auto *declaration = only(widened, "diplomacy.war_declared");
    check(declaration && declaration->visible_to ==
                             std::vector<std::uint64_t>{3, 7},
          "located diplomacy keeps its journal audience");

    stellar::engine::EventHistory h;
    record_step_events(h, step, end_day, campaign);
    check(h.feed(5, 0.0).size() == 3,
          "informed observer feed: first contact + 2 combat events");
    check(h.feed(11, 0.0).size() == 1,
          "civ sees only the survey in its known system");
  }

  // Retention: under the trigger the policy is a no-op; once the
  // chronicle nears capacity, old trivia is pruned while old majors
  // and recent records survive — capacity eviction no longer drops
  // reportable history first.
  {
    stellar::engine::EventHistory history{20};
    const auto add = [&](double day, double significance) {
      stellar::engine::HistoryEvent e;
      e.at_day = day;
      e.significance = significance;
      e.category = "war.damage_applied";
      history.record(std::move(e));
    };
    for (int i = 0; i < 10; ++i) add(10.0 + i, 0.1);   // old trivia
    for (int i = 0; i < 4; ++i) add(10.0 + i, 0.7);    // old majors
    for (int i = 0; i < 4; ++i) add(900.0 + i, 0.1);   // recent trivia
    check(history.size() == 18, "history filled past 90% trigger");

    // Under-capacity histories are untouched.
    stellar::engine::EventHistory small{40};
    check(maintain_chronicle(small, 1000.0) == 0 &&
              small.size() == 0,
          "under-trigger maintenance was not a no-op");

    const auto pruned = maintain_chronicle(history, 1000.0);
    check(pruned == 10 && history.size() == 8,
          "old trivia pruned, majors and recent kept");
    const auto remaining = history.feed(std::nullopt, 0.0);
    for (const auto *e : remaining)
      check(e->significance > chronicle_report_significance ||
                e->at_day > 1000.0 - chronicle_prune_horizon_days,
            "retained record violates retention policy");
    // Deterministic: same inputs, same outcome.
    stellar::engine::EventHistory again{20};
    for (int i = 0; i < 10; ++i) {
      stellar::engine::HistoryEvent e;
      e.at_day = 10.0 + i;
      e.significance = 0.1;
      again.record(std::move(e));
    }
    for (int i = 0; i < 8; ++i) {
      stellar::engine::HistoryEvent e;
      e.at_day = i < 4 ? 10.0 + i : 900.0 + (i - 4);
      e.significance = i < 4 ? 0.7 : 0.1;
      again.record(std::move(e));
    }
    check(maintain_chronicle(again, 1000.0) == 10,
          "retention not deterministic");
  }

  if (failures == 0) {
    std::cout << "campaign event history adapter tests passed\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
