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
    const auto view = diplomacy.build_view(civ);

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
    // Offensive review: evaluate war against every identified counterpart in
    // stable order; at most one declaration per review tick per civilization.
    if (review_tick) {
      for (const int target : identified) {
        if (target == civ || at_war.contains(target))
          continue;
        const auto relationship =
            diplomacy.state().get_relationship(civ, target);

        bool shared_border = false;
        const auto &their_systems = colony_systems[target];
        for (const auto *own : own_systems) {
          for (const auto *theirs : their_systems)
            if (system_distance(*own, *theirs) <= shared_border_light_years)
              shared_border = true;
        }

        std::int64_t last_observation = 0;
        const auto observed = observed_military_strength(
            world.combat_intelligence, civ, target, world.fleets,
            last_observation);

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
