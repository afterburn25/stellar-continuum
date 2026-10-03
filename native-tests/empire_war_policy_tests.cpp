#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/diplomacy_observer_commands.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/diplomacy_snapshot_invariants.hpp>
#include <stellar/core/empire_policy.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
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

Civilization civilization(int id) {
  Civilization result;
  result.id = id;
  result.name = "Civ " + std::to_string(id);
  return result;
}

void war_records() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  // Civ 3 has identified the aggressor only.
  DiplomacySimulation seeding(state);
  (void)seeding.process_contact_opportunity(contact(3, 1, 10));

  DiplomacySimulation simulation(state);
  const WarGoalSpec goals[] = {
      {WarGoalKind::conquer_system, 5},
      {WarGoalKind::humiliate, std::nullopt}};
  simulation.declare_war(1, 2, 20, goals);

  const auto wars = state.wars();
  check(wars.size() == 1, "war declaration creates one war record");
  const auto &war = wars.front();
  check(war.war_id == 1, "first war id is deterministic");
  check(war.aggressor_civilization_id == 1 && war.defender_civilization_id == 2,
        "war belligerents");
  check(war.goals.size() == 3, "declared goals plus defender resistance");
  check(war.goals[0].kind == WarGoalKind::conquer_system &&
            war.goals[0].beneficiary_civilization_id == 1 &&
            war.goals[0].system_id == std::optional<int>(5),
        "conquer goal carries the target system");
  check(war.goals.back().kind == WarGoalKind::resist_aggression &&
            war.goals.back().beneficiary_civilization_id == 2,
        "defender gains resist-aggression");
  check(!war.resolved_at_tick && war.outcome == WarOutcome::active,
        "war starts unresolved");
  check(state.active_war_between(2, 1).has_value(),
        "active_war_between is order-insensitive");

  // Duplicate declaration does not fork a second record.
  simulation.declare_war(1, 2, 30, goals);
  check(state.wars().size() == 1, "duplicate declaration reuses the war");

  // Visibility: belligerents always; third parties only after identifying both.
  check(state.wars(1).size() == 1 && state.wars(2).size() == 1,
        "belligerents see their war");
  check(state.wars(3).empty(),
        "observer without both identifications cannot see the war");
  (void)seeding.process_contact_opportunity(contact(3, 2, 30));
  check(state.wars(3).size() == 1,
        "observer sees the war once both sides are identified");
  check(state.build_view_for(3).wars.size() == 1,
        "observer view carries the war");

  // Invalid goals are rejected without mutation — tested against an
  // identified, non-warring target so validation reaches the goal layer.
  (void)seeding.process_contact_opportunity(contact(1, 4, 30));
  const WarGoalSpec bad_system[] = {{WarGoalKind::conquer_system, std::nullopt}};
  bool rejected = false;
  try {
    simulation.declare_war(1, 4, 40, bad_system);
  } catch (const DiplomacyArgumentError &) {
    rejected = true;
  }
  check(rejected, "conquer-system without a system is rejected");
  const WarGoalSpec resistance[] = {
      {WarGoalKind::resist_aggression, std::nullopt}};
  rejected = false;
  try {
    simulation.declare_war(1, 4, 40, resistance);
  } catch (const DiplomacyArgumentError &) {
    rejected = true;
  }
  check(rejected, "resist-aggression cannot be declared as an attacker goal");
  check(state.wars().size() == 1, "rejected declarations do not record wars");

  // Hidden targets are still protected.
  rejected = false;
  try {
    simulation.declare_war(1, 9, 40, {});
  } catch (const DiplomacyOperationError &) {
    rejected = true;
  }
  check(rejected, "declaration against a hidden civilization is rejected");
}

void war_score_and_exhaustion() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation simulation(state);
  const WarGoalSpec goals[] = {{WarGoalKind::conquer_system, 5}};
  simulation.declare_war(1, 2, 20, goals);

  check(!simulation.apply_battle_outcome(1, 9, std::nullopt, 1.0, 30),
        "battle between non-belligerents does nothing");

  check(simulation.apply_battle_outcome(1, 2, 5, 1.0, 30),
        "battle outcome applies to the active war");
  auto war = state.active_war_between(1, 2).value();
  check(near(war.war_score, 0.15),
        "conquer-goal battle moves the score toward the aggressor");
  check(near(war.defender_exhaustion, 0.03),
        "the loser accrues battle exhaustion");
  check(near(war.aggressor_exhaustion, 0.01),
        "the victor accrues a smaller share");

  check(simulation.apply_battle_outcome(2, 1, 6, 1.0, 40),
        "defender victories swing the score back");
  war = state.active_war_between(1, 2).value();
  check(near(war.war_score, 0.05), "score tracks the victor side");

  // Passive accrual: two days at the default rate, then a policy-scaled day.
  simulation.advance_wars(2020);
  war = state.active_war_between(1, 2).value();
  check(near(war.aggressor_exhaustion, 0.04 + 0.001 * 1.98, 1e-6),
        "passive exhaustion accrues over elapsed days");
  const auto before = war.defender_exhaustion;
  simulation.advance_wars(2020);
  check(near(state.active_war_between(1, 2)->defender_exhaustion, before),
        "repeated accrual for the same tick is a no-op");
  simulation.advance_wars(3020, [](int civilization) {
    return civilization == 2 ? 2.0 : 1.0;
  });
  war = state.active_war_between(1, 2).value();
  check(near(war.defender_exhaustion - before, 0.002, 1e-6),
        "policy factor scales exhaustion accrual");
}

void peace_settlement() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation simulation(state);
  const WarGoalSpec goals[] = {{WarGoalKind::conquer_system, 5},
                               {WarGoalKind::humiliate, std::nullopt}};
  simulation.declare_war(1, 2, 20, goals);
  for (int i = 0; i < 3; ++i)
    (void)simulation.apply_battle_outcome(1, 2, 5, 1.0,
                                          30 + i * 10);

  const auto proposal = simulation.send_proposal(
      1, 2, DiplomaticProposalKind::peace_offer, 100, "End the war.");
  simulation.respond_to_proposal(proposal, 2, true, 110);

  const auto war = state.get_war(1).value();
  check(war.resolved_at_tick == std::optional<std::int64_t>(110),
        "peace settles the war");
  check(war.outcome == WarOutcome::aggressor_victory,
        "high score resolves as aggressor victory");
  const auto achieved = std::count_if(
      war.goals.begin(), war.goals.end(),
      [](const WarGoalSnapshot &g) { return g.achieved; });
  check(achieved == 2, "victor goals are marked achieved");
  check(!war.goals.back().achieved,
        "loser's resistance goal is not achieved");
  const auto relationship = state.get_relationship(1, 2).value();
  check(relationship.political_state == DiplomaticPoliticalState::peace,
        "peace agreement restores the relationship");

  // A ceasefire settles as a white peace without victory assignment.
  mutual_contact(state, 3, 4, 10);
  simulation.declare_war(3, 4, 20, {});
  const auto ceasefire = simulation.send_proposal(
      3, 4, DiplomaticProposalKind::ceasefire_offer, 120, "Stand down.");
  simulation.respond_to_proposal(ceasefire, 4, true, 130);
  const auto second = state.wars().back();
  check(second.outcome == WarOutcome::white_peace &&
            second.resolved_at_tick == std::optional<std::int64_t>(130),
        "ceasefire resolves the war as a white peace");
}

void snapshot_roundtrip() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation simulation(state);
  simulation.declare_war(1, 2, 20, {});
  (void)simulation.apply_battle_outcome(1, 2, std::nullopt, 0.5, 30);

  auto restored = DiplomacyState::restore(state.snapshot());
  const auto &wars = restored.wars();
  check(wars.size() == 1 && wars[0].war_id == 1 &&
            wars[0].outcome == WarOutcome::active &&
            near(wars[0].war_score, 0.05),
        "war records survive snapshot/restore");
  DiplomacySimulation restore_simulation(restored);
  mutual_contact(restored, 1, 3, 40);
  restore_simulation.declare_war(1, 3, 50, {});
  check(restored.wars().back().war_id == 2,
        "restored state continues the war-id sequence");
}

void invariants() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation simulation(state);
  simulation.declare_war(1, 2, 20, {});
  const auto baseline = state.snapshot();
  (void)DiplomacySnapshotInvariantValidator::validate(baseline);

  auto broken = baseline;
  broken.wars.push_back(broken.wars.front());
  bool rejected = false;
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(broken);
  } catch (const DiplomacySnapshotValidationError &) {
    rejected = true;
  }
  check(rejected, "duplicate war ids are rejected");

  broken = baseline;
  broken.wars[0].war_score = 2.0;
  rejected = false;
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(broken);
  } catch (const DiplomacySnapshotValidationError &) {
    rejected = true;
  }
  check(rejected, "out-of-range war score is rejected");

  broken = baseline;
  broken.wars[0].defender_civilization_id =
      broken.wars[0].aggressor_civilization_id;
  rejected = false;
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(broken);
  } catch (const DiplomacySnapshotValidationError &) {
    rejected = true;
  }
  check(rejected, "self-war is rejected");

  // An unresolved war requires the pair to be at war.
  broken = baseline;
  for (auto &relationship : broken.relationships)
    relationship.political_state = DiplomaticPoliticalState::peace;
  rejected = false;
  try {
    (void)DiplomacySnapshotInvariantValidator::validate(broken);
  } catch (const DiplomacySnapshotValidationError &) {
    rejected = true;
  }
  check(rejected, "unresolved war requires an at-war relationship");
}

void observer_command_goals() {
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  ObserverDiplomacyCommandService service(state);
  const WarGoalSpec goals[] = {{WarGoalKind::secure_claims, std::nullopt}};
  const auto result = service.declare_war(1, 2, 20, goals);
  check(result.accepted, "observer command accepts a goal set");
  check(state.wars().size() == 1 &&
            state.wars()[0].goals[0].kind == WarGoalKind::secure_claims,
        "observer command records the declared goal");
  const auto stale = service.declare_war(1, 2, 30, goals);
  check(stale.accepted, "repeat declaration stays a no-op, not a failure");
}

void empire_policies() {
  check(empire_policy_catalog().size() == 12, "catalog exposes the policy set");
  check(find_empire_policy("war_mobilization") != nullptr,
        "catalog lookup by id");
  check(find_empire_policy("nonexistent") == nullptr,
        "unknown ids do not resolve");
  check(default_empire_policy(EmpirePolicyDomain::military)->id ==
            "standard_doctrine",
        "domain defaults resolve");

  FreshCampaignState campaign;
  campaign.civilizations.push_back(civilization(1));
  campaign.civilizations.push_back(civilization(2));

  auto result = set_empire_policy(campaign, 9, "war_mobilization", 100);
  check(!result.accepted, "unknown civilization is rejected");
  result = set_empire_policy(campaign, 1, "nonexistent", 100);
  check(!result.accepted, "unknown policy is rejected");
  check(campaign.empire_policies.empty(),
        "rejected changes materialize nothing");

  result = set_empire_policy(campaign, 1, "war_mobilization", 100);
  check(result.accepted, "policy change is accepted");
  result = set_empire_policy(campaign, 1, "war_mobilization", 200);
  check(!result.accepted, "re-setting the same policy is rejected");
  result = set_empire_policy(campaign, 1, "demilitarization", 200);
  check(!result.accepted, "the per-domain cooldown blocks immediate changes");
  result = set_empire_policy(campaign, 1, "demilitarization",
                             100 + empire_policy_change_cooldown_ticks);
  check(result.accepted, "the change applies once the cooldown elapses");

  const auto effects = empire_policy_effects(campaign.empire_policies, 1);
  check(near(effects.shipbuilding_factor, 0.85) &&
            near(effects.war_exhaustion_factor, 0.8),
        "effects reflect the active policy");
  check(near(effects.industry_factor, 1.0) && near(effects.credit_factor, 1.0),
        "untouched domains resolve to defaults");
  check(near(empire_policy_effects(campaign.empire_policies, 2)
                 .shipbuilding_factor,
             1.0),
        "civilizations without state resolve all defaults");

  const auto captured = capture_empire_policies(campaign.empire_policies);
  check(captured.size() == 1 && captured[0].policy_id == "demilitarization",
        "capture stores only divergent assignments");
  auto restored =
      restore_empire_policies(captured, campaign.civilizations);
  check(restored.size() == 1 && restored[0].civilization_id == 1 &&
            restored[0].assignments[0].policy_id == "demilitarization",
        "restore round-trips assignments");
  const EmpirePolicySaveDto stray[] = {{9, "war_mobilization", 0},
                                       {1, "nonexistent", 0}};
  check(restore_empire_policies(stray, campaign.civilizations).size() == 0 ||
            restore_empire_policies(stray, campaign.civilizations)[0]
                .assignments.empty(),
        "restore skips unknown civilizations and policies");
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
  run("war_records", war_records);
  run("war_score_and_exhaustion", war_score_and_exhaustion);
  run("peace_settlement", peace_settlement);
  run("snapshot_roundtrip", snapshot_roundtrip);
  run("invariants", invariants);
  run("observer_command_goals", observer_command_goals);
  run("empire_policies", empire_policies);
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "empire_war_policy: all checks passed\n";
  return 0;
}
