#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/empire_policy.hpp>
#include <stellar/core/fresh_campaign.hpp>
#include <stellar/core/strategic_policies.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

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

FreshCampaignState campaign() {
  FreshCampaignState result;
  Civilization one;
  one.id = 1;
  one.name = "Executor";
  result.civilizations.push_back(one);
  return result;
}

CivilizationTraits traits() {
  CivilizationTraits result{};
  result.aggression = 0.6;
  result.survival_priority = 0.3;
  result.scientific_curiosity = 0.4;
  return result;
}

CivilizationStrategicReview review(StrategicPriorityType primary =
                                       StrategicPriorityType::BuildFleet) {
  CivilizationStrategicReview result{};
  result.own_state.military_strength = 500;
  result.plan.civilization_id = 1;
  result.plan.review_after_tick = 90;
  result.plan.priorities.push_back({primary, 0.9, "test"});
  return result;
}

const EmpirePolicyAssignment *assignment(const FreshCampaignState &campaign,
                                         int civilization,
                                         EmpirePolicyDomain domain) {
  const auto state =
      std::ranges::find(campaign.empire_policies, civilization,
                        &EmpirePolicyState::civilization_id);
  if (state == campaign.empire_policies.end()) return nullptr;
  const auto found = std::ranges::find(state->assignments, domain,
                                       &EmpirePolicyAssignment::domain);
  return found == state->assignments.end() ? nullptr : &*found;
}

std::string_view active(const FreshCampaignState &campaign, int civilization,
                        EmpirePolicyDomain domain) {
  const auto *found = assignment(campaign, civilization, domain);
  if (found != nullptr) return found->policy_id;
  const auto *policy = default_empire_policy(domain);
  return policy ? policy->id : std::string_view{};
}

void wartime_mobilizes() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation(state).declare_war(1, 2, 20, {});
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), review(), knowledge,
      state.build_view_for(1), 60);
  check(result.policies_changed == 1, "war triggers one policy change");
  check(active(state_campaign, 1, EmpirePolicyDomain::military) ==
            "war_mobilization",
        "an active war mobilizes the military domain");
  check(assignment(state_campaign, 1, EmpirePolicyDomain::military)
                ->changed_at_tick ==
            DiplomacyCampaignClock::from_simulation_days(60),
        "policy changes are stamped on the campaign diplomacy clock");
}

void exhaustion_demilitarizes() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation seeding(state);
  seeding.declare_war(1, 2, 20, {});
  for (int i = 0; i < 25; ++i)
    (void)seeding.apply_battle_outcome(2, 1, std::nullopt, 1.0, 30 + i);
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), review(), knowledge,
      state.build_view_for(1), 60);
  check(result.policies_changed == 1 &&
            active(state_campaign, 1, EmpirePolicyDomain::military) ==
                "demilitarization",
        "a spent, pragmatic civilization demilitarizes");

  CivilizationTraits honorable = traits();
  honorable.honor_bound = true;
  auto second = campaign();
  const auto repeat = StrategicPolicyExecutor{}.execute(
      second, 1, honorable, review(), knowledge,
      state.build_view_for(1), 60);
  check(repeat.policies_changed == 1 &&
            active(second, 1, EmpirePolicyDomain::military) ==
                "war_mobilization",
        "honor-bound civilizations keep fighting while exhausted");
}

void colonization_charters_the_frontier() {
  auto state_campaign = campaign();
  DiplomacyState state;
  auto colonize = review(StrategicPriorityType::Colonize);
  colonize.own_state.has_known_colonization_opportunity = true;
  colonize.plan.priorities.front().score = 0.6;
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), colonize, knowledge,
      state.build_view_for(1), 60);
  check(result.policies_changed == 1 &&
            active(state_campaign, 1, EmpirePolicyDomain::frontier) ==
                "expansion_charter",
        "a colonization push charters the frontier");
}

void plans_shape_economy_and_research() {
  DiplomacyState state;
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  auto industry = campaign();
  (void)StrategicPolicyExecutor{}.execute(
      industry, 1, traits(),
      review(StrategicPriorityType::StabilizeSupply), knowledge,
      state.build_view_for(1), 60);
  check(active(industry, 1, EmpirePolicyDomain::economy) ==
            "industrial_focus",
        "supply pressure focuses industry");
  auto science = campaign();
  (void)StrategicPolicyExecutor{}.execute(
      science, 1, traits(),
      review(StrategicPriorityType::ExpandResearch), knowledge,
      state.build_view_for(1), 60);
  check(active(science, 1, EmpirePolicyDomain::research) ==
            "directed_research",
        "a research-led plan directs research");
}

void trade_dependence_turns_mercantile() {
  auto state_campaign = campaign();
  DiplomacyState state;
  KnownCivilization partner{};
  partner.civilization_id = 2;
  partner.known_trade_dependence = 0.6;
  const StrategicKnowledgeSnapshot knowledge{60, {{2, partner}}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), review(), knowledge,
      state.build_view_for(1), 60);
  check(result.policies_changed == 1 &&
            active(state_campaign, 1, EmpirePolicyDomain::economy) ==
                "mercantile_focus",
        "real trade dependence turns the economy mercantile");
}

void one_domain_per_review() {
  auto state_campaign = campaign();
  DiplomacyState state;
  mutual_contact(state, 1, 2, 10);
  DiplomacySimulation(state).declare_war(1, 2, 20, {});
  auto colonize = review(StrategicPriorityType::Colonize);
  colonize.own_state.has_known_colonization_opportunity = true;
  colonize.plan.priorities.front().score = 0.6;
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto first = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), colonize, knowledge,
      state.build_view_for(1), 60);
  check(first.policies_changed == 1 &&
            active(state_campaign, 1, EmpirePolicyDomain::military) ==
                "war_mobilization",
        "wartime posture beats expansion for the first change");
  const auto second = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), colonize, knowledge,
      state.build_view_for(1), 60);
  check(second.policies_changed == 1 &&
            active(state_campaign, 1, EmpirePolicyDomain::frontier) ==
                "fortress_border",
        "the next review picks up the frontier domain");
}

void cooldown_blocks_churn() {
  auto state_campaign = campaign();
  DiplomacyState state;
  (void)set_empire_policy(state_campaign, 1, "mercantile_focus",
                          DiplomacyCampaignClock::from_simulation_days(60));
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(),
      review(StrategicPriorityType::StabilizeSupply), knowledge,
      state.build_view_for(1), 61);
  check(result.policies_changed == 0 &&
            active(state_campaign, 1, EmpirePolicyDomain::economy) ==
                "mercantile_focus",
        "the per-domain cooldown rejects immediate AI churn");
}

void peace_holds_defaults() {
  auto state_campaign = campaign();
  DiplomacyState state;
  const StrategicKnowledgeSnapshot knowledge{60, {}};
  const auto result = StrategicPolicyExecutor{}.execute(
      state_campaign, 1, traits(), review(), knowledge,
      state.build_view_for(1), 60);
  check(result.policies_changed == 0 && state_campaign.empire_policies.empty(),
        "an unremarkable peace changes nothing and persists nothing");
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
  run("wartime_mobilizes", wartime_mobilizes);
  run("exhaustion_demilitarizes", exhaustion_demilitarizes);
  run("colonization_charters_the_frontier",
      colonization_charters_the_frontier);
  run("plans_shape_economy_and_research", plans_shape_economy_and_research);
  run("trade_dependence_turns_mercantile", trade_dependence_turns_mercantile);
  run("one_domain_per_review", one_domain_per_review);
  run("cooldown_blocks_churn", cooldown_blocks_churn);
  run("peace_holds_defaults", peace_holds_defaults);
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "strategic_policies: all checks passed\n";
  return 0;
}
