#include <stellar/core/strategic_policies.hpp>

#include <stellar/core/diplomacy_lifecycle.hpp>
#include <stellar/core/empire_policy.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace stellar::core {
namespace {

double priority_score(const CivilizationStrategicReview &review,
                      StrategicPriorityType type) noexcept {
  const auto found = std::ranges::find(review.plan.priorities, type,
                                       &StrategicPriority::type);
  return found == review.plan.priorities.end() ? 0.0 : found->score;
}

bool is_primary(const CivilizationStrategicReview &review,
                StrategicPriorityType type) noexcept {
  const auto *primary = review.plan.primary_priority();
  return primary != nullptr && primary->type == type;
}

const WarSnapshot *own_active_war(const DiplomaticStateView &view,
                                int civilization) noexcept {
  for (const auto &war : view.wars)
    if (!war.resolved_at_tick &&
        (war.aggressor_civilization_id == civilization ||
         war.defender_civilization_id == civilization))
      return &war;
  return nullptr;
}

std::string_view current_policy(const FreshCampaignState &campaign,
                                int civilization,
                                EmpirePolicyDomain domain) noexcept {
  const auto found =
      std::ranges::find(campaign.empire_policies, civilization,
                        &EmpirePolicyState::civilization_id);
  const auto *policy =
      found == campaign.empire_policies.end()
          ? default_empire_policy(domain)
          : active_empire_policy(*found, domain);
  return policy ? policy->id : std::string_view{};
}

} // namespace

StrategicPolicyExecutor::Result StrategicPolicyExecutor::execute(
    FreshCampaignState &campaign, int civilization,
    const CivilizationTraits &traits,
    const CivilizationStrategicReview &review,
    const StrategicKnowledgeSnapshot &knowledge,
    const DiplomaticStateView &view, std::int64_t now_tick) const {
  if (civilization < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'civilizationId')");
  if (now_tick < 0)
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter "
        "'nowTick')");
  // The strategic review clock ticks once per campaign day while policy
  // cooldowns run on campaign milli-days (1000/day); stamp the change on
  // the diplomacy clock so AI and player cooldowns share one domain.
  const auto policy_tick = DiplomacyCampaignClock::from_simulation_days(
      static_cast<double>(now_tick));
  Result result;

  const auto *war = own_active_war(view, civilization);
  const double own_exhaustion =
      war == nullptr
          ? 0.0
          : (war->aggressor_civilization_id == civilization
                 ? war->aggressor_exhaustion
                 : war->defender_exhaustion);
  const bool defend_primary =
      is_primary(review, StrategicPriorityType::Defend);

  // Desired stance per domain. Military: mobilize at war (or when a
  // credible threat tops the plan), demilitarize only when exhausted and
  // not honor-bound, otherwise hold the standard doctrine. Frontier:
  // fortress under threat or war, charter when expansion is both
  // possible and prioritized. Economy: industry while supply or
  // production dominates the plan, mercantile when the ledger shows
  // real trade dependence. Research: directed when science leads.
  std::string_view military = "standard_doctrine";
  double military_urgency = 0.45;
  if (war != nullptr) {
    const bool spent = own_exhaustion >= 0.5 && !traits.honor_bound;
    military = spent ? "demilitarization" : "war_mobilization";
    military_urgency = 1.0;
  } else if (defend_primary) {
    military = "war_mobilization";
    military_urgency = 0.6;
  }

  std::string_view frontier = "consolidation";
  double frontier_urgency = 0.4;
  const double colonize = priority_score(review, StrategicPriorityType::Colonize);
  if (war != nullptr || defend_primary) {
    frontier = "fortress_border";
    frontier_urgency = war != nullptr ? 0.7 : 0.55;
  } else if (review.own_state.has_known_colonization_opportunity &&
             !review.intent.defer_new_colonization && colonize >= 0.4) {
    frontier = "expansion_charter";
    frontier_urgency = 0.45 + colonize * 0.25;
  }

  std::string_view economy = "balanced_economy";
  double economy_urgency = 0.4;
  double trade_dependence = 0.0;
  for (const auto &entry : knowledge.civilizations)
    trade_dependence =
        std::max(trade_dependence, entry.civilization.known_trade_dependence);
  if (is_primary(review, StrategicPriorityType::StabilizeSupply) ||
      is_primary(review, StrategicPriorityType::ExpandIndustry)) {
    economy = "industrial_focus";
    economy_urgency = 0.5 + std::max(
                                priority_score(review,
                                               StrategicPriorityType::StabilizeSupply),
                                priority_score(review,
                                               StrategicPriorityType::ExpandIndustry)) *
                                0.3;
  } else if (trade_dependence >= 0.4) {
    economy = "mercantile_focus";
    economy_urgency = 0.45 + trade_dependence * 0.2;
  }

  std::string_view research = "open_research";
  double research_urgency = 0.4;
  if (is_primary(review, StrategicPriorityType::ExpandResearch) ||
      (traits.scientific_curiosity >= 0.7 &&
       review.own_state.has_available_research)) {
    research = "directed_research";
    research_urgency =
        0.45 + priority_score(review, StrategicPriorityType::ExpandResearch) * 0.3;
  }

  struct Candidate {
    EmpirePolicyDomain domain;
    std::string_view policy_id;
    double urgency;
  };
  // Military first so wartime posture wins ties; then frontier, economy,
  // research. At most one domain changes per review — the per-domain
  // cooldown paces the rest.
  const std::array<Candidate, 4> candidates{{
      {EmpirePolicyDomain::military, military, military_urgency},
      {EmpirePolicyDomain::frontier, frontier, frontier_urgency},
      {EmpirePolicyDomain::economy, economy, economy_urgency},
      {EmpirePolicyDomain::research, research, research_urgency},
  }};
  const Candidate *best = nullptr;
  for (const auto &candidate : candidates) {
    if (candidate.policy_id ==
        current_policy(campaign, civilization, candidate.domain))
      continue;
    if (best == nullptr || candidate.urgency > best->urgency)
      best = &candidate;
  }
  if (best != nullptr && best->urgency >= 0.25 &&
      set_empire_policy(campaign, civilization, best->policy_id, policy_tick)
          .accepted)
    ++result.policies_changed;
  return result;
}

} // namespace stellar::core
