#include <stellar/core/strategic_diplomacy.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace stellar::core {
namespace {

bool pair_involves(int a, int b, int first, int second) noexcept {
  return (first == a && second == b) || (first == b && second == a);
}

bool pending_proposal(const DiplomaticStateView &view, int a, int b,
                      DiplomaticProposalKind kind) {
  for (const auto &proposal : view.proposals)
    if (proposal.status == DiplomaticProposalStatus::pending &&
        proposal.kind == kind &&
        pair_involves(a, b, proposal.proposer_civilization_id,
                      proposal.recipient_civilization_id))
      return true;
  return false;
}

bool active_agreement(const DiplomaticStateView &view, int a, int b,
                      DiplomaticAgreementType type) {
  for (const auto &agreement : view.agreements)
    if (agreement.status == DiplomaticAgreementStatus::active &&
        agreement.type == type &&
        pair_involves(a, b, agreement.civilization_a_id,
                      agreement.civilization_b_id))
      return true;
  return false;
}

const WarSnapshot *active_war_with(const DiplomaticStateView &view,
                                   int civilization, int target) {
  for (const auto &war : view.wars)
    if (!war.resolved_at_tick &&
        pair_involves(civilization, target,
                      war.aggressor_civilization_id,
                      war.defender_civilization_id))
      return &war;
  return nullptr;
}

std::unordered_set<int> active_claims_by(const DiplomaticStateView &view,
                                         int civilization) {
  std::unordered_set<int> systems;
  for (const auto &claim : view.claims)
    if (claim.active && claim.claimant_civilization_id == civilization)
      systems.insert(claim.system_id);
  return systems;
}

} // namespace

StrategicDiplomacyExecutor::Result StrategicDiplomacyExecutor::execute(
    DiplomacySimulation &simulation, int civilization,
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
  Result result;

  int active_wars = 0;
  for (const auto &war : view.wars)
    if (!war.resolved_at_tick &&
        (war.aggressor_civilization_id == civilization ||
         war.defender_civilization_id == civilization))
      ++active_wars;

  const StrategicDecisionEvaluator evaluator;
  const double honor = traits.honor_bound ? 1.0 : 0.0;
  const double tolerance =
      std::clamp(0.55 + honor * 0.25 - traits.survival_priority * 0.15,
                 0.25, 0.80);
  bool war_action_taken = false;
  const bool seeks_relations =
      review.plan.primary_priority() != nullptr &&
      review.plan.primary_priority()->type ==
          StrategicPriorityType::ImproveRelations;
  const KnownCivilization *best_relation = nullptr;
  double best_relation_score = 0;

  for (const auto &entry : knowledge.civilizations) {
    const auto &known = entry.civilization;
    const int target = known.civilization_id;
    if (target == civilization)
      continue;

    // War: the evaluator's recommendation, gated by a legitimate estimate,
    // no treaty, no existing war with the pair in the authoritative view,
    // and at most two concurrent wars. Contested claims justify a
    // secure-claims goal; otherwise the declaration is a generic
    // humiliate war.
    if (!war_action_taken &&
        active_war_with(view, civilization, target) == nullptr &&
        !known.has_defense_treaty_with_observer &&
        known.has_military_estimate && active_wars < 2 &&
        evaluator.evaluate_war(traits, review.own_state.military_strength,
                               known, now_tick)
            .recommend_war) {
      const auto own_claims = active_claims_by(view, civilization);
      bool contested = false;
      for (const auto &claim : view.claims)
        if (claim.active &&
            claim.claimant_civilization_id == target &&
            own_claims.contains(claim.system_id))
          contested = true;
      const WarGoalSpec claims_goal[] = {
          {WarGoalKind::secure_claims, std::nullopt}};
      simulation.declare_war(
          civilization, target, now_tick,
          contested ? std::span<const WarGoalSpec>(claims_goal)
                    : std::span<const WarGoalSpec>{});
      ++result.wars_declared;
      ++active_wars;
      war_action_taken = true;
      continue;
    }

    // Peace: an active war the civilization is losing or exhausted by
    // produces one bounded overture — ceasefire first, a full peace offer
    // only once exhaustion is severe. The authoritative view decides, so
    // a stale knowledge flag can never skip or repeat an overture.
    const auto *war = active_war_with(view, civilization, target);
    if (war != nullptr) {
      if (!war_action_taken) {
        const bool aggressor =
            war->aggressor_civilization_id == civilization;
        const double own_exhaustion = aggressor ? war->aggressor_exhaustion
                                                : war->defender_exhaustion;
        const double score_against =
            aggressor ? -war->war_score : war->war_score;
        const bool peace_pending =
            pending_proposal(view, civilization, target,
                             DiplomaticProposalKind::peace_offer) ||
            pending_proposal(view, civilization, target,
                             DiplomaticProposalKind::ceasefire_offer);
        if (!peace_pending) {
          if (own_exhaustion >= 0.75 + honor * 0.15) {
            (void)simulation.send_proposal(
                civilization, target, DiplomaticProposalKind::peace_offer,
                now_tick, "We seek an end to this ruinous war.");
            ++result.proposals_sent;
            war_action_taken = true;
          } else if (own_exhaustion >= tolerance ||
                     score_against >= 0.5) {
            (void)simulation.send_proposal(
                civilization, target,
                DiplomaticProposalKind::ceasefire_offer, now_tick,
                "We propose an immediate ceasefire.");
            ++result.proposals_sent;
            war_action_taken = true;
          }
        }
      }
      continue;
    }

    // Outreach: only while the plan prioritizes relations — one proposal
    // per review toward the best partner, mirroring the planner's own
    // relation scan (trust > -0.25, not at war, best dependence+trust).
    if (seeks_relations && known.trust > -0.25) {
      const double score = known.known_trade_dependence + known.trust;
      if (best_relation == nullptr || score > best_relation_score) {
        best_relation = &known;
        best_relation_score = score;
      }
    }
  }

  if (seeks_relations && best_relation != nullptr) {
    const int target = best_relation->civilization_id;
    const bool busy =
        pending_proposal(view, civilization, target,
                         DiplomaticProposalKind::agreement);
    if (!busy) {
      // Escalate through the agreement ladder the pair does not yet hold.
      DiplomaticAgreementType type;
      if (!active_agreement(view, civilization, target,
                            DiplomaticAgreementType::non_aggression))
        type = DiplomaticAgreementType::non_aggression;
      else if (!active_agreement(view, civilization, target,
                                 DiplomaticAgreementType::trade))
        type = DiplomaticAgreementType::trade;
      else if (!active_agreement(view, civilization, target,
                                 DiplomaticAgreementType::research_exchange))
        type = DiplomaticAgreementType::research_exchange;
      else
        type = DiplomaticAgreementType::cooperation;
      if (!active_agreement(view, civilization, target, type)) {
        (void)simulation.send_proposal(
            civilization, target, DiplomaticProposalKind::agreement,
            now_tick, "We propose a lasting accord between our peoples.",
            type);
        ++result.proposals_sent;
      }
    }
  }

  return result;
}

} // namespace stellar::core
