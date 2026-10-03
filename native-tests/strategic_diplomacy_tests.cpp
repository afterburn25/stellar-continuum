#include <stellar/core/diplomacy_runtime.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/strategic_diplomacy.hpp>

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

CivilizationTraits traits(double aggression = 1.0, double territoriality = 1.0,
                          double survival = 0.2, bool honor = false) {
  CivilizationTraits result{};
  result.aggression = aggression;
  result.territoriality = territoriality;
  result.survival_priority = survival;
  result.risk_tolerance = 0.5;
  result.honor_bound = honor;
  return result;
}

KnownCivilization known(int id, double trust, double low, double high,
                        bool border, bool at_war, bool treaty) {
  return {id,    trust, low, high, 0.8, 60,
          border, 0.0,  0.0, at_war, treaty, true};
}

CivilizationStrategicReview review(double military,
                                   StrategicPriorityType primary =
                                       StrategicPriorityType::BuildFleet) {
  CivilizationStrategicReview result{};
  result.own_state.military_strength = military;
  result.plan.civilization_id = 1;
  result.plan.review_after_tick = 90;
  result.plan.priorities.push_back({primary, 0.9, "test"});
  return result;
}

int pending_kind(const DiplomaticStateView &view, int a, int b,
                 DiplomaticProposalKind kind) {
  int count = 0;
  for (const auto &proposal : view.proposals)
    if (proposal.status == DiplomaticProposalStatus::pending &&
        proposal.kind == kind &&
        ((proposal.proposer_civilization_id == a &&
          proposal.recipient_civilization_id == b) ||
         (proposal.proposer_civilization_id == b &&
          proposal.recipient_civilization_id == a)))
      ++count;
  return count;
}

void declares_war_on_recommendation() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  // Contested claim: both sides claim system 10, visible to civ 1.
  DiplomacySimulation seeding(state);
  (void)seeding.assert_territorial_claim(1, 10, 15);
  const auto foreign = seeding.assert_territorial_claim(2, 10, 15);
  seeding.communicate_territorial_claim(foreign, 1, 15);

  const StrategicKnowledgeSnapshot knowledge{
      60, {{2, known(2, -0.3, 90, 110, true, false, false)}}};
  DiplomacySimulation simulation(state);
  const auto result = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(), review(1000), knowledge,
      state.build_view_for(1), 60);

  check(result.wars_declared == 1, "recommendation declares one war");
  const auto war = state.active_war_between(1, 2);
  check(war.has_value() && war->aggressor_civilization_id == 1,
        "the AI civilization is recorded as aggressor");
  check(war && war->goals.front().kind == WarGoalKind::secure_claims,
        "contested claims produce a secure-claims goal");

  const auto second = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(), review(1000), knowledge,
      state.build_view_for(1), 60);
  check(second.wars_declared == 0 && state.wars().size() == 1,
        "an existing war blocks repeat declarations");
}

void no_war_against_treaty_or_weakness() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  mutual_contact(state, 1, 3, 10);
  const StrategicKnowledgeSnapshot knowledge{
      60,
      {{2, known(2, -0.3, 90, 110, true, false, true)},
       {3, known(3, -0.3, 9000, 11000, true, false, false)}}};
  DiplomacySimulation simulation(state);
  const auto result = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(), review(1000), knowledge,
      state.build_view_for(1), 60);
  check(result.wars_declared == 0 && state.wars().empty(),
        "treaty partners and superior foes are never attacked");
}

void sues_for_peace_when_losing() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation seeding(state);
  seeding.declare_war(2, 1, 20, {});
  for (int i = 0; i < 15; ++i)
    (void)seeding.apply_battle_outcome(2, 1, std::nullopt, 1.0, 30 + i);

  const StrategicKnowledgeSnapshot knowledge{
      60, {{2, known(2, -0.5, 900, 1100, true, true, false)}}};
  DiplomacySimulation simulation(state);
  const auto result = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.1, 0.1, 0.5), review(200), knowledge,
      state.build_view_for(1), 60);

  check(result.wars_declared == 0, "a losing civilization declares nothing");
  check(result.proposals_sent == 1 &&
            pending_kind(state.build_view_for(1), 1, 2,
                         DiplomaticProposalKind::ceasefire_offer) == 1,
        "defeat plus exhaustion sends one ceasefire offer");

  const auto repeat = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.1, 0.1, 0.5), review(200), knowledge,
      state.build_view_for(1), 60);
  check(repeat.proposals_sent == 0 &&
            pending_kind(state.build_view_for(1), 1, 2,
                         DiplomaticProposalKind::ceasefire_offer) == 1,
        "a pending overture is never duplicated");
}

void severe_exhaustion_seeks_peace() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation seeding(state);
  seeding.declare_war(1, 2, 20, {});
  for (int i = 0; i < 27; ++i)
    (void)seeding.apply_battle_outcome(2, 1, std::nullopt, 1.0, 30 + i);

  const StrategicKnowledgeSnapshot knowledge{
      60, {{2, known(2, -0.5, 900, 1100, true, true, false)}}};
  DiplomacySimulation simulation(state);
  const auto result = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.5, 0.5, 0.2), review(200), knowledge,
      state.build_view_for(1), 60);
  check(pending_kind(state.build_view_for(1), 1, 2,
                     DiplomaticProposalKind::peace_offer) == 1,
        "severe exhaustion escalates to a peace offer");
}

void outreach_follows_the_plan() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  mutual_contact(state, 1, 3, 10);
  // Civ 2 is the better partner (higher trust); civ 3 is hostile. Neither
  // has a military estimate, so the war path can never fire here.
  auto friendly = known(2, 0.4, 90, 110, false, false, false);
  friendly.has_military_estimate = false;
  auto hostile = known(3, -0.6, 90, 110, false, false, false);
  hostile.has_military_estimate = false;
  const StrategicKnowledgeSnapshot knowledge{
      60, {{2, friendly}, {3, hostile}}};
  DiplomacySimulation simulation(state);
  const auto result = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.2, 0.2, 0.2), review(500),
      knowledge, state.build_view_for(1), 60);
  check(result.proposals_sent == 0,
        "outreach requires the ImproveRelations priority");

  const auto seeking = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.2, 0.2, 0.2),
      review(500, StrategicPriorityType::ImproveRelations), knowledge,
      state.build_view_for(1), 60);
  check(seeking.proposals_sent == 1,
        "the priority sends exactly one agreement proposal");
  const auto view = state.build_view_for(1);
  check(pending_kind(view, 1, 2, DiplomaticProposalKind::agreement) == 1 &&
            pending_kind(view, 1, 3, DiplomaticProposalKind::agreement) == 0,
        "the best partner receives the proposal");

  const auto repeat = StrategicDiplomacyExecutor{}.execute(
      simulation, 1, traits(0.2, 0.2, 0.2),
      review(500, StrategicPriorityType::ImproveRelations), knowledge,
      state.build_view_for(1), 60);
  check(repeat.proposals_sent == 0,
        "a pending agreement proposal is never duplicated");
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
  run("declares_war_on_recommendation", declares_war_on_recommendation);
  run("no_war_against_treaty_or_weakness", no_war_against_treaty_or_weakness);
  run("sues_for_peace_when_losing", sues_for_peace_when_losing);
  run("severe_exhaustion_seeks_peace", severe_exhaustion_seeks_peace);
  run("outreach_follows_the_plan", outreach_follows_the_plan);
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "strategic_diplomacy: all checks passed\n";
  return 0;
}
