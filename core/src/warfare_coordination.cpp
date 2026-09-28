#include <stellar/core/warfare_coordination.hpp>

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/combat_command_runtime.hpp>
#include <stellar/core/combat_state.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>
#include <stellar/core/fleet_reach.hpp>
#include <stellar/core/interstellar_distance.hpp>
#include <stellar/core/lane_network.hpp>
#include <stellar/core/strategic_planning.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace stellar::core {
namespace {

bool fleet_is_armed(const FleetState &fleet) noexcept {
  if (!fleet.is_active)
    return false;
  if (fleet.combat) {
    const auto *profile = find_combat_profile(fleet.combat->profile_id);
    return profile && profile->has_weapon();
  }
  return get_combat_profile(default_combat_profile_id(fleet.role)).has_weapon();
}

bool fleet_is_idle(const FleetState &fleet) noexcept {
  return !fleet.destination_system_id &&
         fleet.transit_phase == FleetTransitPhase::None;
}

double system_distance(const StellarSystem &a, const StellarSystem &b) noexcept {
  return distance_light_years(a.position, b.position);
}

const StellarSystem *find_system(std::span<const StellarSystem> systems,
                                 int system_id) noexcept {
  for (const auto &system : systems)
    if (system.id == system_id)
      return &system;
  return nullptr;
}

bool has_active_pact(const DiplomaticStateView &view, int observer,
                     int target) noexcept {
  for (const auto &agreement : view.agreements) {
    if (agreement.status != DiplomaticAgreementStatus::active)
      continue;
    const bool involves =
        (agreement.civilization_a_id == observer &&
         agreement.civilization_b_id == target) ||
        (agreement.civilization_a_id == target &&
         agreement.civilization_b_id == observer);
    if (involves &&
        (agreement.type == DiplomaticAgreementType::non_aggression ||
         agreement.type == DiplomaticAgreementType::peace ||
         agreement.type == DiplomaticAgreementType::cooperation))
      return true;
  }
  return false;
}

// Sums the observer's latest recorded power estimate for every fleet owned by
// the target civilization. Returns nullopt when nothing has been observed.
std::optional<double> observed_military_strength(
    std::span<const FleetPowerObservation> intelligence, int observer_id,
    int target_id, std::span<const FleetState> fleets,
    std::int64_t &latest_observation_tick) noexcept {
  double total = 0;
  bool any = false;
  for (const auto &fleet : fleets) {
    if (fleet.civilization_id != target_id || !fleet.is_active)
      continue;
    const auto observed =
        observed_fleet_combat_power(intelligence, observer_id, fleet);
    if (!observed)
      continue;
    any = true;
    total += *observed;
    for (auto entry = intelligence.rbegin(); entry != intelligence.rend();
         ++entry)
      if (entry->observer_id == observer_id && entry->fleet_id == fleet.id) {
        latest_observation_tick = std::max(
            latest_observation_tick,
            static_cast<std::int64_t>(entry->observed_day));
        break;
      }
  }
  return any ? std::optional<double>(total) : std::nullopt;
}

// Nearest system (by catalogue geometry) containing any asset of a hostile
// civilization, for deterministic power-projection target selection.
std::optional<int>
nearest_hostile_system(const FleetState &fleet,
                       const std::unordered_set<int> &hostile_ids,
                       std::span<const StellarSystem> systems,
                       std::span<const Colony> colonies,
                       std::span<const FleetState> fleets) {
  const auto *origin =
      fleet.current_system_id ? find_system(systems, *fleet.current_system_id)
                              : nullptr;
  if (!origin)
    return std::nullopt;
  std::optional<int> best;
  double best_distance = std::numeric_limits<double>::infinity();
  const auto consider = [&](int system_id) {
    const auto *system = find_system(systems, system_id);
    if (!system)
      return;
    const double distance = system_distance(*origin, *system);
    if (!best || distance < best_distance ||
        (distance == best_distance && system_id < *best)) {
      best = system_id;
      best_distance = distance;
    }
  };
  for (const auto &colony : colonies)
    if (hostile_ids.contains(colony.civilization_id))
      consider(colony.system_id);
  for (const auto &other : fleets)
    if (other.is_active && hostile_ids.contains(other.civilization_id) &&
        other.current_system_id)
      consider(*other.current_system_id);
  return best;
}

} // namespace

WarfareStepResult
WarfareCoordinator::advance(WarfareWorldView world,
                            DiplomacyCampaignRuntimeCoordinator &diplomacy,
                            std::int64_t diplomacy_tick,
                            std::int64_t elapsed_ticks) {
  if (diplomacy_tick < 0 || elapsed_ticks < 0)
    throw DiplomacyArgumentRangeError("The given Int64 value was out of range.");
  WarfareStepResult result;
  DiplomacySimulation simulation(diplomacy.state());
  auto combat = diplomacy.create_combat_command_runtime();
  CombatWorldView combat_world{world.systems, world.fleets};
  OperationalReachWorldView reach_world{world.systems, world.colonies,
                                        world.lanes};

  std::vector<const Civilization *> civilizations;
  for (const auto &civilization : world.civilizations)
    if (civilization_uses_ai(civilization, world.control))
      civilizations.push_back(&civilization);
  std::stable_sort(civilizations.begin(), civilizations.end(),
                   [](const auto *a, const auto *b) { return a->id < b->id; });

  // Colony systems indexed by owning civilization for border/trespass checks.
  std::unordered_map<int, std::vector<const StellarSystem *>> colony_systems;
  for (const auto &colony : world.colonies)
    if (const auto *system = find_system(world.systems, colony.system_id))
      colony_systems[colony.civilization_id].push_back(system);

  StrategicDecisionEvaluator evaluator;
  for (const auto *civilization : civilizations) {
    const int civ = civilization->id;
    auto view = diplomacy.build_view(civ);

    // Identified counterparts and war state, in stable id order.
    std::vector<int> identified;
    for (const auto &contact : view.contacts)
      if (contact.target_civilization_id &&
          contact.awareness >= ContactAwareness::identified)
        identified.push_back(*contact.target_civilization_id);
    std::sort(identified.begin(), identified.end());
    identified.erase(std::unique(identified.begin(), identified.end()),
                     identified.end());

    std::unordered_set<int> at_war;
    for (const auto &relationship : view.relationships)
      if (relationship.political_state == DiplomaticPoliticalState::at_war)
        at_war.insert(relationship.other_civilization_id);

    double own_strength = 0;
    for (const auto &fleet : world.fleets)
      if (fleet.civilization_id == civ && fleet_is_armed(fleet))
        own_strength += own_fleet_combat_power(fleet);
    const auto &own_systems = colony_systems[civ];

    // Strategic knowledge about a counterpart: observed fleet power when the
    // observer's scanners have recorded it, a deliberately uncertain neutral
    // prior otherwise. Shared by the war and peace evaluations.
    const auto build_known = [&](int target) {
      bool shared_border = false;
      const auto &their_systems = colony_systems[target];
      for (const auto *own : own_systems)
        for (const auto *theirs : their_systems)
          if (system_distance(*own, *theirs) <= shared_border_light_years)
            shared_border = true;
      std::int64_t last_observation = 0;
      const auto observed =
          observed_military_strength(world.combat_intelligence, civ, target,
                                   world.fleets, last_observation);
      const auto relationship =
          diplomacy.state().get_relationship(civ, target);
      KnownCivilization known;
      known.civilization_id = target;
      known.trust = relationship ? relationship->trust - relationship->hostility
                                 : 0.0;
      known.has_shared_border = shared_border;
      known.known_war_exhaustion = 0;
      known.has_defense_treaty_with_observer =
          has_active_pact(view, civ, target);
      if (observed) {
        known.has_military_estimate = true;
        known.estimated_military_low = *observed * 0.8;
        known.estimated_military_high = *observed * 1.25;
        known.estimate_confidence = 0.6;
        known.last_military_observation_tick = last_observation;
      } else {
        // Neutral prior: the counterpart could range from unarmed to
        // somewhat stronger than the observer; the near-zero confidence
        // makes the evaluator price in that uncertainty.
        known.has_military_estimate = true;
        known.estimated_military_low = 0;
        known.estimated_military_high =
            std::max(1.0, own_strength * unobserved_strength_multiplier);
        known.estimate_confidence = unobserved_strength_confidence;
        known.last_military_observation_tick = diplomacy_tick;
      }
      return known;
    };

    // War weariness from the diplomacy journal: age of the most recent
    // war_declared between the pair, normalized to full weariness at ten
    // years. A war whose declaration scrolled off the bounded journal counts
    // as fully wearisome.
    const auto war_weariness = [&](int target) {
      std::int64_t declared = -1;
      for (const auto &event : view.recent_events) {
        const bool between =
            (event.primary_civilization_id == civ &&
             event.secondary_civilization_id == target) ||
            (event.primary_civilization_id == target &&
             event.secondary_civilization_id == civ);
        if (event.kind == DiplomaticEventKind::war_declared && between)
          declared = std::max(declared, event.tick);
      }
      if (declared < 0)
        return 1.0;
      return std::min(
          1.0, static_cast<double>(diplomacy_tick - declared) /
                   static_cast<double>(war_weariness_full_ticks));
    };

    // Territorial friction: identified foreign military presence inside a
    // system holding one of the observer's colonies records a trespass. This
    // runs on the civ's review cadence so events stay bounded.
    // The window check is step-size agnostic: a civ reviews when its
    // phase-shifted interval boundary was crossed since the previous advance
    // (floor quotient changed). Works for any elapsed_ticks, including
    // deltas larger than the interval.
    const std::int64_t phase = (static_cast<std::int64_t>(civ) * 997) %
                               review_interval_ticks;
    const auto floor_div = [](std::int64_t value) {
      return static_cast<std::int64_t>(std::floor(
          static_cast<double>(value) / review_interval_ticks));
    };
    const bool review_tick =
        elapsed_ticks > 0 &&
        floor_div(diplomacy_tick - phase) !=
            floor_div(diplomacy_tick - elapsed_ticks - phase);
    if (review_tick) {
      // Belligerents cannot lose track of each other: an active war or
      // ceasefire is continuing mutual contact, so stale or never-created
      // contacts are reacquired through the canonical observation pipeline.
      // Without this, wars started on already-stale contacts — or declared
      // on a civilization that never identified its aggressor — could never
      // be settled, since every proposal path requires mutual communication.
      bool reacquired = false;
      std::vector<int> belligerents;
      for (const auto &other : world.civilizations) {
        if (other.id == civ)
          continue;
        const auto relationship =
            diplomacy.state().get_relationship(civ, other.id);
        if (relationship &&
            (relationship->political_state ==
                 DiplomaticPoliticalState::at_war ||
             relationship->political_state ==
                 DiplomaticPoliticalState::ceasefire))
          belligerents.push_back(other.id);
      }
      std::sort(belligerents.begin(), belligerents.end());
      for (const int belligerent : belligerents) {
        const DiplomaticContactView *latest = nullptr;
        for (const auto &contact : view.contacts)
          if (contact.target_civilization_id == belligerent &&
              (!latest ||
               contact.last_observed_tick > latest->last_observed_tick))
            latest = &contact;
        if (latest && latest->awareness >= ContactAwareness::identified &&
            latest->condition != ContactCondition::stale_or_lost)
          continue;
        FirstContactOpportunity revival;
        revival.observer_civilization_id = civ;
        revival.contact_id = latest
                                 ? latest->contact_id
                                 : "civilization:" +
                                       std::to_string(belligerent);
        revival.target_civilization_id = belligerent;
        revival.observed_at_tick = diplomacy_tick;
        revival.awareness = ContactAwareness::identified;
        revival.condition = ContactCondition::active;
        revival.confidence = 1.0;
        (void)simulation.process_contact_opportunity(revival);
        ++result.belligerent_contacts_reacquired;
        reacquired = true;
      }
      if (reacquired) {
        // Fresh contact records change what the observer knows (the war
        // relationship only becomes visible once the counterpart is
        // identified), so rebuild the view and derived sets.
        view = diplomacy.build_view(civ);
        identified.clear();
        for (const auto &contact : view.contacts)
          if (contact.target_civilization_id &&
              contact.awareness >= ContactAwareness::identified)
            identified.push_back(*contact.target_civilization_id);
        std::sort(identified.begin(), identified.end());
        identified.erase(
            std::unique(identified.begin(), identified.end()),
            identified.end());
        at_war.clear();
        for (const auto &relationship : view.relationships)
          if (relationship.political_state == DiplomaticPoliticalState::at_war)
            at_war.insert(relationship.other_civilization_id);
      }

      // Open communication channels with identified counterparts — the
      // proposal path (peace, ceasefire, future agreements) requires a
      // live channel and nothing else establishes one autonomously.
      for (const int target : identified) {
        if (target == civ)
          continue;
        const bool has_channel = std::ranges::any_of(
            view.contacts, [&](const auto &contact) {
              return contact.target_civilization_id == target &&
                     contact.communication_available &&
                     contact.condition != ContactCondition::stale_or_lost;
            });
        if (has_channel)
          continue;
        const auto opened = diplomacy.commands().establish_communication(
            civ, target, diplomacy_tick);
        if (opened.accepted)
          ++result.communications_established;
      }

      // Settlement review: answer pending incoming peace/ceasefire offers
      // first, since acceptance may end a war before this civilization weighs
      // its own offers or declarations.
      std::vector<const DiplomaticProposalSnapshot *> pending_peace;
      for (const auto &proposal : view.proposals)
        if (proposal.recipient_civilization_id == civ &&
            proposal.status == DiplomaticProposalStatus::pending &&
            (proposal.kind == DiplomaticProposalKind::peace_offer ||
             proposal.kind == DiplomaticProposalKind::ceasefire_offer))
          pending_peace.push_back(&proposal);
      std::ranges::sort(pending_peace, {},
                        &DiplomaticProposalSnapshot::proposal_id);
      for (const auto *proposal : pending_peace) {
        const int proposer = proposal->proposer_civilization_id;
        const auto relationship = std::ranges::find(
            view.relationships, proposer,
            &DiplomaticRelationshipView::other_civilization_id);
        const bool at_war_with =
            relationship != view.relationships.end() &&
            relationship->political_state == DiplomaticPoliticalState::at_war;
        const bool ceasefire_with =
            relationship != view.relationships.end() &&
            relationship->political_state ==
                DiplomaticPoliticalState::ceasefire;
        bool accept = false;
        if (at_war_with || ceasefire_with) {
          const auto peace = evaluator.evaluate_peace(
              civilization->traits, own_strength, build_known(proposer),
              relationship->hostility, relationship->fear,
              war_weariness(proposer), ceasefire_with);
          accept = peace.accept_terms;
        }
        const auto answered = diplomacy.commands().respond_to_proposal(
            civ, proposal->proposal_id, accept, diplomacy_tick);
        if (answered.accepted)
          ++(accept ? result.peace_offers_accepted
                    : result.peace_offers_rejected);
      }
      if (!pending_peace.empty()) {
        // Responses may have ended wars or activated agreements; rebuild the
        // observer's view so the offer/declaration passes see current state.
        view = diplomacy.build_view(civ);
        at_war.clear();
        for (const auto &relationship : view.relationships)
          if (relationship.political_state == DiplomaticPoliticalState::at_war)
            at_war.insert(relationship.other_civilization_id);
      }

      // Settlement offers: for each counterpart still at war or in ceasefire,
      // evaluate whether this civilization wants the conflict to end.
      std::vector<int> settleable;
      for (const auto &relationship : view.relationships) {
        if (relationship.political_state ==
                DiplomaticPoliticalState::at_war ||
            relationship.political_state ==
                DiplomaticPoliticalState::ceasefire)
          settleable.push_back(relationship.other_civilization_id);
      }
      std::sort(settleable.begin(), settleable.end());
      for (const int target : settleable) {
        if (target == civ)
          continue;
        const auto relationship = std::ranges::find(
            view.relationships, target,
            &DiplomaticRelationshipView::other_civilization_id);
        if (relationship == view.relationships.end())
          continue;
        const bool ceasefire_active =
            relationship->political_state == DiplomaticPoliticalState::ceasefire;
        // One outstanding offer per pair at a time; a rejection cools the
        // offer cadence so the journal isn't flooded by refusals.
        const auto pending_offer = std::ranges::any_of(
            view.proposals, [&](const auto &proposal) {
              return proposal.proposer_civilization_id == civ &&
                     proposal.recipient_civilization_id == target &&
                     proposal.status == DiplomaticProposalStatus::pending &&
                     (proposal.kind == DiplomaticProposalKind::peace_offer ||
                      proposal.kind == DiplomaticProposalKind::ceasefire_offer);
            });
        if (pending_offer)
          continue;
        const auto recently_rejected = std::ranges::any_of(
            view.recent_events, [&](const auto &event) {
              return event.kind == DiplomaticEventKind::proposal_rejected &&
                     event.primary_civilization_id == target &&
                     event.secondary_civilization_id == civ &&
                     diplomacy_tick - event.tick <= peace_offer_cooldown_ticks;
            });
        if (recently_rejected)
          continue;
        const auto peace = evaluator.evaluate_peace(
            civilization->traits, own_strength, build_known(target),
            relationship->hostility, relationship->fear,
            war_weariness(target), ceasefire_active);
        std::optional<DiplomaticProposalKind> kind;
        if (ceasefire_active)
          kind = peace.offer_peace
                     ? std::optional{DiplomaticProposalKind::peace_offer}
                     : std::nullopt;
        else if (peace.offer_peace)
          kind = DiplomaticProposalKind::peace_offer;
        else if (peace.offer_ceasefire)
          kind = DiplomaticProposalKind::ceasefire_offer;
        if (!kind)
          continue;
        const auto issued = diplomacy.commands().send_proposal(
            civ, target, *kind, diplomacy_tick,
            *kind == DiplomaticProposalKind::peace_offer
                ? "Offers peace to end the war."
                : "Offers a ceasefire to halt active hostilities.");
        if (issued.accepted)
          ++result.peace_offers_sent;
      }

      // Offensive review: evaluate war against every identified counterpart
      // in stable order; at most one declaration per review tick per
      // civilization.
      for (const int target : identified) {
        if (target == civ || at_war.contains(target))
          continue;
        // A ceasefire entered by mutual consent is not a free reload —
        // without this guard an aggressive civilization's own declaration
        // pass would break a ceasefire it just accepted on the same tick.
        const auto fresh_ceasefire = std::ranges::any_of(
            view.agreements, [&](const auto &agreement) {
              if (agreement.status != DiplomaticAgreementStatus::active ||
                  agreement.type != DiplomaticAgreementType::ceasefire)
                return false;
              const bool involves =
                  (agreement.civilization_a_id == civ &&
                   agreement.civilization_b_id == target) ||
                  (agreement.civilization_a_id == target &&
                   agreement.civilization_b_id == civ);
              return involves &&
                     diplomacy_tick - agreement.started_at_tick <=
                         peace_offer_cooldown_ticks;
            });
        if (fresh_ceasefire)
          continue;
        const auto known = build_known(target);
        const auto assessment = evaluator.evaluate_war(
            civilization->traits, own_strength, known, diplomacy_tick);
        if (!assessment.recommend_war)
          continue;
        const auto issued =
            diplomacy.commands().declare_war(civ, target, diplomacy_tick);
        if (issued.accepted) {
          ++result.wars_declared;
          at_war.insert(target);
        }
        break;
      }
    }

    if (review_tick && !own_systems.empty()) {
      std::unordered_set<int> own_system_ids;
      for (const auto *system : own_systems)
        own_system_ids.insert(system->id);
      std::unordered_set<int> intruder_set;
      for (const auto &fleet : world.fleets)
        if (fleet.is_active && fleet.civilization_id != civ &&
            fleet.current_system_id &&
            own_system_ids.contains(*fleet.current_system_id) &&
            // An invader at war is not a trespasser — the war itself is
            // already the recorded diplomatic fact.
            !at_war.contains(fleet.civilization_id) &&
            std::binary_search(identified.begin(), identified.end(),
                               fleet.civilization_id) &&
            diplomacy.state().get_access_permission(civ,
                                                    fleet.civilization_id) !=
                AccessPermission::granted)
          intruder_set.insert(fleet.civilization_id);
      std::vector<int> intruders(intruder_set.begin(), intruder_set.end());
      std::sort(intruders.begin(), intruders.end());
      for (const int intruder : intruders) {
        int system_id = std::numeric_limits<int>::max();
        for (const auto &fleet : world.fleets)
          if (fleet.civilization_id == intruder && fleet.current_system_id &&
              own_system_ids.contains(*fleet.current_system_id))
            system_id = std::min(system_id, *fleet.current_system_id);
        simulation.record_trespass(civ, intruder, system_id, diplomacy_tick);
        ++result.trespasses_recorded;
      }
    }

    // Execution: engage co-located hostiles immediately; deploy idle armed
    // fleets toward the nearest hostile-occupied system.
    if (at_war.empty())
      continue;
    std::vector<FleetState *> armed;
    for (auto &fleet : world.fleets)
      if (fleet.civilization_id == civ && fleet_is_armed(fleet))
        armed.push_back(&fleet);
    std::stable_sort(armed.begin(), armed.end(),
                     [](const auto *a, const auto *b) { return a->id < b->id; });
    for (auto *fleet : armed) {
      const auto engagement = combat.issue_engage_hostiles(
          combat_world, civ, fleet->id);
      if (engagement.accepted) {
        ++result.engagement_orders;
        continue;
      }
      if (!fleet_is_idle(*fleet))
        continue;
      const auto destination = nearest_hostile_system(
          *fleet, at_war, world.systems, world.colonies, world.fleets);
      if (!destination || destination == fleet->current_system_id)
        continue;
      const auto reach = assess_operational_reach(
          reach_world, civ, *fleet, *destination,
          InterstellarMissionKind::MilitaryDeployment);
      if (!reach.is_supported)
        continue;
      assign_fleet_route(reach_world, *fleet, *destination, reach);
      ++result.deployment_orders;
    }
  }
  return result;
}

} // namespace stellar::core
