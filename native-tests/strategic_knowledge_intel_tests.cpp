#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/diplomacy_simulation.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

using namespace stellar::core;

namespace {
int failures{};

void check(bool condition, std::string message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool near(double a, double b, double epsilon = 1e-9) {
  return std::fabs(a - b) <= epsilon;
}

FirstContactOpportunity contact(int observer, int target, std::int64_t tick) {
  return {observer,
          "contact-" + std::to_string(observer) + "-" + std::to_string(target),
          target,
          tick,
          7,
          ContactAwareness::communication_available,
          ContactCondition::active,
          true,
          1};
}

void mutual_contact(DiplomacyState &state, int a, int b, std::int64_t tick) {
  DiplomacySimulation simulation(state);
  (void)simulation.process_contact_opportunity(contact(a, b, tick));
  (void)simulation.process_contact_opportunity(contact(b, a, tick));
}

void agreement(DiplomacySimulation &simulation, int a, int b,
               DiplomaticAgreementType type, std::int64_t tick) {
  const auto proposal = simulation.send_proposal(
      a, b, DiplomaticProposalKind::agreement, tick, "Accord", type);
  simulation.respond_to_proposal(proposal, b, true, tick);
}

const KnownCivilization *find(const StrategicKnowledgeSnapshot &snapshot,
                              int target) {
  const auto found = std::ranges::find(snapshot.civilizations, target,
                                       &KnownCivilizationEntry::key);
  return found == snapshot.civilizations.end() ? nullptr
                                             : &found->civilization;
}

void empty_intel_preserves_stub_fields() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  const auto snapshot =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60);
  const auto *known = find(snapshot, 2);
  check(known != nullptr, "identified civilization is known");
  check(known && !known->has_military_estimate &&
            known->estimated_military_low == 0 &&
            known->last_military_observation_tick == 0,
        "no intel means no military estimate");
  check(known && !known->has_shared_border &&
            known->known_trade_dependence == 0 &&
            !known->has_defense_treaty_with_observer,
        "no agreements or claims leave diplomatic fields empty");
}

void agreements_feed_treaty_and_dependence() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  mutual_contact(state, 1, 3, 10);
  DiplomacySimulation simulation(state);
  agreement(simulation, 1, 2, DiplomaticAgreementType::trade, 15);
  agreement(simulation, 1, 2, DiplomaticAgreementType::cooperation, 16);
  agreement(simulation, 1, 3, DiplomaticAgreementType::non_aggression, 17);

  const auto snapshot =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60);
  const auto *second = find(snapshot, 2);
  const auto *third = find(snapshot, 3);
  check(second && near(second->known_trade_dependence, 0.75),
        "trade plus cooperation weighs 0.75 dependence");
  check(second && second->has_defense_treaty_with_observer,
        "cooperation reads as a defense treaty");
  check(third && near(third->known_trade_dependence, 0.0) &&
            !third->has_defense_treaty_with_observer,
        "non-aggression is neither dependence nor treaty");
}

void claims_and_lanes_feed_shared_border() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  mutual_contact(state, 1, 3, 10);
  DiplomacySimulation simulation(state);
  (void)simulation.assert_territorial_claim(1, 10, 15);
  const auto foreign = simulation.assert_territorial_claim(2, 11, 15);
  simulation.communicate_territorial_claim(foreign, 1, 15);
  // Civ 3 claims beside the observer too, but never communicates it.
  (void)simulation.assert_territorial_claim(3, 12, 15);

  const InterstellarLane lanes[] = {{9, 11}, {10, 11}};
  std::vector<Colony> colonies(1);
  colonies[0].civilization_id = 1;
  colonies[0].system_id = 10;
  const StrategicKnowledgeIntel intel{{}, {}, lanes, colonies};

  const auto snapshot =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60, intel);
  check(find(snapshot, 2) && find(snapshot, 2)->has_shared_border,
        "lane-adjacent communicated claims share a border");
  check(find(snapshot, 3) && !find(snapshot, 3)->has_shared_border,
        "uncommunicated foreign claims cannot share a border");

  // Contested systems border without any lane intel.
  (void)simulation.assert_territorial_claim(1, 20, 16);
  const auto contested = simulation.assert_territorial_claim(3, 20, 16);
  simulation.communicate_territorial_claim(contested, 1, 16);
  const auto second =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60);
  check(find(second, 3) && find(second, 3)->has_shared_border,
        "a contested system shares a border without lanes");
}

void observations_feed_military_estimate() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation simulation(state);

  std::vector<FleetState> fleets(3);
  fleets[0].id = 101;
  fleets[0].civilization_id = 2;
  fleets[1].id = 102;
  fleets[1].civilization_id = 2;
  fleets[2].id = 103;
  fleets[2].civilization_id = 4; // never identified
  const FleetPowerObservation observations[] = {
      {1, 101, 100.0, 50.0}, // stale entry for fleet 101
      {1, 101, 120.0, 60.0}, // newest entry wins
      {1, 102, 80.0, 60.0},
      {1, 103, 500.0, 60.0}, // hidden civ's fleet never attributed
      {2, 102, 999.0, 60.0}, // another observer's record ignored
  };
  const StrategicKnowledgeIntel intel{fleets, observations, {}, {}};

  const auto snapshot =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60, intel);
  const auto *known = find(snapshot, 2);
  check(known && known->has_military_estimate,
        "observed fleets produce an estimate");
  check(known && near(known->estimated_military_low, 200.0),
        "the estimate sums the newest per-fleet observations");
  check(known && near(known->estimated_military_midpoint(), 310.0),
        "midpoint reflects the coverage-widened high bound");
  check(known && near(known->estimate_confidence, 0.5),
        "fresh observations of half the saturation count");
  check(known && known->last_military_observation_tick == 60,
        "the newest observed day anchors the estimate");
  check(find(snapshot, 4) == nullptr,
        "unidentified fleets never enter the snapshot");

  // Aged observations decay confidence and widen the high bound.
  const auto stale =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 425, intel);
  const auto *aged = find(stale, 2);
  check(aged && aged->has_military_estimate &&
            aged->estimate_confidence < 0.5 &&
            near(aged->estimated_military_low, 200.0),
        "a year-old observation decays confidence, not the low bound");
}

void war_state_feeds_exhaustion_and_at_war() {
  DiplomacyState state;
  mutual_contact(state, 1, 3, 10);
  DiplomacySimulation simulation(state);
  simulation.declare_war(1, 3, 20, {});
  (void)simulation.apply_battle_outcome(1, 3, 5, 1.0, 30);

  const auto snapshot =
      DiplomacyStrategicKnowledgeProvider(state).build(1, 60);
  const auto *known = find(snapshot, 3);
  check(known && known->known_to_be_at_war,
        "an active war marks the target at war");
  check(known && known->known_war_exhaustion > 0,
        "battle outcomes feed authoritative exhaustion");
}
} // namespace

int main() {
  const auto run = [](const char *name, void (*fn)()) {
    std::cerr << "-- " << name << '\n';
    try {
      fn();
    } catch (const std::exception &e) {
      std::cerr << "UNCAUGHT in " << name << ": " << e.what() << '\n';
      ++failures;
    }
  };
  run("empty_intel_preserves_stub_fields", empty_intel_preserves_stub_fields);
  run("agreements_feed_treaty_and_dependence",
      agreements_feed_treaty_and_dependence);
  run("claims_and_lanes_feed_shared_border", claims_and_lanes_feed_shared_border);
  run("observations_feed_military_estimate", observations_feed_military_estimate);
  run("war_state_feeds_exhaustion_and_at_war",
      war_state_feeds_exhaustion_and_at_war);
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "strategic_knowledge_intel: all checks passed\n";
  return 0;
}
