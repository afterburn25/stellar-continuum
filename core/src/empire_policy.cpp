#include <stellar/core/empire_policy.hpp>

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/fresh_campaign.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>

namespace stellar::core {
namespace {

constexpr EmpirePolicyDefinition catalog[] = {
    {"balanced_economy", "Balanced Economy", EmpirePolicyDomain::economy,
     EmpirePolicyEffects{}, "Unmodified economic allocation."},
    {"industrial_focus", "Industrial Focus", EmpirePolicyDomain::economy,
     EmpirePolicyEffects{1.15, 0.9, 1.0, 1.0, 1.0, 1.0},
     "Favors industrial output over trade revenue."},
    {"mercantile_focus", "Mercantile Focus", EmpirePolicyDomain::economy,
     EmpirePolicyEffects{0.9, 1.15, 1.0, 1.0, 1.0, 1.0},
     "Favors trade revenue over industrial output."},
    {"standard_doctrine", "Standard Doctrine", EmpirePolicyDomain::military,
     EmpirePolicyEffects{}, "Unmodified military posture."},
    {"war_mobilization", "War Mobilization", EmpirePolicyDomain::military,
     EmpirePolicyEffects{1.0, 1.0, 1.0, 1.2, 1.0, 1.25},
     "Accelerates fleet production at the cost of faster war exhaustion."},
    {"demilitarization", "Demilitarization", EmpirePolicyDomain::military,
     EmpirePolicyEffects{1.0, 1.0, 1.0, 0.85, 1.0, 0.8},
     "Slows fleet production while easing war exhaustion."},
    {"open_research", "Open Research", EmpirePolicyDomain::research,
     EmpirePolicyEffects{}, "Unmodified research allocation."},
    {"directed_research", "Directed Research", EmpirePolicyDomain::research,
     EmpirePolicyEffects{1.0, 0.95, 1.15, 1.0, 1.0, 1.0},
     "Concentrates funding on science at a modest revenue cost."},
    {"applied_industry", "Applied Industry", EmpirePolicyDomain::research,
     EmpirePolicyEffects{1.05, 1.0, 0.9, 1.0, 1.0, 1.0},
     "Redirects research capacity toward industrial application."},
    {"consolidation", "Consolidation", EmpirePolicyDomain::frontier,
     EmpirePolicyEffects{}, "Unmodified frontier posture."},
    {"expansion_charter", "Expansion Charter", EmpirePolicyDomain::frontier,
     EmpirePolicyEffects{0.95, 1.0, 1.0, 1.0, 1.2, 1.0},
     "Prioritizes outward expansion over home industrial growth."},
    {"fortress_border", "Fortress Border", EmpirePolicyDomain::frontier,
     EmpirePolicyEffects{1.0, 1.0, 1.0, 1.05, 0.9, 1.0},
     "Favors defensive readiness over expansion."},
};

constexpr std::string_view defaults[] = {"balanced_economy", "standard_doctrine",
                                         "open_research", "consolidation"};

} // namespace

std::span<const EmpirePolicyDefinition> empire_policy_catalog() {
  return catalog;
}
const EmpirePolicyDefinition *find_empire_policy(std::string_view id) noexcept {
  const auto found = std::ranges::find(catalog, id,
                                       &EmpirePolicyDefinition::id);
  return found == std::end(catalog) ? nullptr : &*found;
}
const EmpirePolicyDefinition *
default_empire_policy(EmpirePolicyDomain domain) noexcept {
  const auto index = static_cast<std::size_t>(domain);
  return index < std::size(defaults) ? find_empire_policy(defaults[index])
                                    : nullptr;
}

EmpirePolicyEffects
empire_policy_effects(std::span<const EmpirePolicyState> states,
                      int civilization_id) noexcept {
  const auto found = std::ranges::find(states, civilization_id,
                                       &EmpirePolicyState::civilization_id);
  if (found == states.end()) return {};
  EmpirePolicyEffects result;
  for (const auto domain :
       {EmpirePolicyDomain::economy, EmpirePolicyDomain::military,
        EmpirePolicyDomain::research, EmpirePolicyDomain::frontier}) {
    const auto *policy = active_empire_policy(*found, domain);
    if (!policy) continue;
    result.industry_factor *= policy->effects.industry_factor;
    result.credit_factor *= policy->effects.credit_factor;
    result.science_factor *= policy->effects.science_factor;
    result.shipbuilding_factor *= policy->effects.shipbuilding_factor;
    result.expansion_factor *= policy->effects.expansion_factor;
    result.war_exhaustion_factor *= policy->effects.war_exhaustion_factor;
  }
  return result;
}
const EmpirePolicyDefinition *
active_empire_policy(const EmpirePolicyState &state,
                     EmpirePolicyDomain domain) noexcept {
  const auto found = std::ranges::find(state.assignments, domain,
                                       &EmpirePolicyAssignment::domain);
  if (found == state.assignments.end()) return default_empire_policy(domain);
  const auto *policy = find_empire_policy(found->policy_id);
  return policy ? policy : default_empire_policy(domain);
}

EmpirePolicyCommandResult set_empire_policy(FreshCampaignState &campaign,
                                            int civilization_id,
                                            std::string_view policy_id,
                                            std::int64_t tick) {
  if (tick < 0)
    return {false, "Policy changes cannot be applied at a negative tick."};
  const auto *policy = find_empire_policy(policy_id);
  if (!policy)
    return {false, "Unknown empire policy."};
  if (std::ranges::find(campaign.civilizations, civilization_id,
                        &Civilization::id) == campaign.civilizations.end())
    return {false, "Unknown civilization."};
  auto state =
      std::ranges::find(campaign.empire_policies, civilization_id,
                        &EmpirePolicyState::civilization_id);
  if (state == campaign.empire_policies.end()) {
    campaign.empire_policies.push_back(
        EmpirePolicyState{civilization_id, {}});
    // Restore sorts by civilization id; keeping the same canonical order
    // at mutation time makes captures byte-stable regardless of the
    // order domains were changed in.
    std::ranges::sort(campaign.empire_policies, {},
                      &EmpirePolicyState::civilization_id);
    state = std::ranges::find(campaign.empire_policies, civilization_id,
                              &EmpirePolicyState::civilization_id);
  }
  auto assignment = std::ranges::find(state->assignments, policy->domain,
                                      &EmpirePolicyAssignment::domain);
  if (assignment != state->assignments.end()) {
    if (assignment->policy_id == policy->id)
      return {false, "That policy is already active."};
    if (tick - assignment->changed_at_tick <
        empire_policy_change_cooldown_ticks)
      return {false,
              "That policy domain was changed recently; the empire must wait "
              "before changing it again."};
    assignment->policy_id = std::string(policy->id);
    assignment->changed_at_tick = tick;
  } else {
    state->assignments.push_back(
        {policy->domain, std::string(policy->id), tick});
    std::ranges::sort(state->assignments, {},
                      &EmpirePolicyAssignment::domain);
  }
  return {true,
          "Policy set: " + std::string(policy->display_name) + "."};
}

std::vector<EmpirePolicySaveDto>
capture_empire_policies(std::span<const EmpirePolicyState> states) {
  std::vector<EmpirePolicySaveDto> result;
  for (const auto &state : states)
    for (const auto &assignment : state.assignments)
      result.push_back({state.civilization_id, assignment.policy_id,
                        assignment.changed_at_tick});
  return result;
}
std::vector<EmpirePolicyState>
restore_empire_policies(std::span<const EmpirePolicySaveDto> source,
                        std::span<const Civilization> civilizations) {
  std::vector<EmpirePolicyState> result;
  for (const auto &dto : source) {
    if (std::ranges::find(civilizations, dto.civilization_id,
                          &Civilization::id) == civilizations.end())
      continue;
    const auto *policy = find_empire_policy(dto.policy_id);
    if (!policy) continue;
    auto state = std::ranges::find(result, dto.civilization_id,
                                   &EmpirePolicyState::civilization_id);
    if (state == result.end()) {
      result.push_back(EmpirePolicyState{dto.civilization_id, {}});
      state = std::prev(result.end());
    }
    auto assignment = std::ranges::find(state->assignments, policy->domain,
                                        &EmpirePolicyAssignment::domain);
    if (assignment != state->assignments.end()) {
      if (dto.changed_at_tick >= assignment->changed_at_tick) {
        assignment->policy_id = dto.policy_id;
        assignment->changed_at_tick =
            std::max<std::int64_t>(0, dto.changed_at_tick);
      }
      continue;
    }
    state->assignments.push_back({policy->domain, dto.policy_id,
                                  std::max<std::int64_t>(0,
                                                         dto.changed_at_tick)});
  }
  for (auto &state : result)
    std::ranges::sort(state.assignments, {}, &EmpirePolicyAssignment::domain);
  std::ranges::sort(result, {}, &EmpirePolicyState::civilization_id);
  return result;
}

} // namespace stellar::core
