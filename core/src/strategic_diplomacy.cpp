#include <stellar/core/strategic_diplomacy.hpp>

#include <stellar/core/diplomacy_lifecycle.hpp>

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

// A strategic action that fails the simulation's own validation (e.g. an
// identification lapse between the knowledge build and the call) is
// dropped, never propagated — one civilization's stale view must not
// abort the whole review wave.
template <typename Fn> bool try_diplomatic_action(Fn &&fn) {
  try {
    fn();
    return true;
  } catch (const DiplomacyOperationError &) {
    return false;
  } catch (const DiplomacyArgumentError &) {
    return false;
  }
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
  // The strategic review clock ticks once per campaign day while the
  // diplomacy simulation stamps campaign milli-days (1000/day); every
  // authoritative command must be stamped on the diplomacy clock so
  // AI actions order and age identically to player-issued ones.
  const auto diplomacy_tick = DiplomacyCampaignClock::from_simulation_days(
      static_cast<double>(now_tick));
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
      if (try_diplomatic_action([&] {
            simulation.declare_war(
                civilization, target, diplomacy_tick,
                contested ? std::span<const WarGoalSpec>(claims_goal)
                          : std::span<const WarGoalSpec>{});
          })) {
        ++result.wars_declared;
        ++active_wars;
        war_action_taken = true;
      }
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
            if (try_diplomatic_action([&] {
                  (void)simulation.send_proposal(
                      civilization, target,
                      DiplomaticProposalKind::peace_offer, diplomacy_tick,
                      "We seek an end to this ruinous war.");
                })) {
              ++result.proposals_sent;
              war_action_taken = true;
            }
          } else if (own_exhaustion >= tolerance ||
                     score_against >= 0.5) {
            if (try_diplomatic_action([&] {
                  (void)simulation.send_proposal(
                      civilization, target,
                      DiplomaticProposalKind::ceasefire_offer, diplomacy_tick,
                      "We propose an immediate ceasefire.");
                })) {
              ++result.proposals_sent;
              war_action_taken = true;
            }
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
      if (!active_agreement(view, civilization, target, type) &&
          try_diplomatic_action([&] {
            (void)simulation.send_proposal(
                civilization, target, DiplomaticProposalKind::agreement,
                diplomacy_tick,
                "We propose a lasting accord between our peoples.", type);
          }))
        ++result.proposals_sent;
    }
  }

  // Inbound: answer pending proposals addressed to this civilization and
  // respond to communicated foreign claims. Bounded per review; every
  // answer is decided from the same observer-legitimate state as the
  // outbound pass.
  const auto trust_of = [&](int other) {
    const auto found = std::ranges::find(
        view.relationships, other,
        &DiplomaticRelationshipView::other_civilization_id);
    return found == view.relationships.end() ? 0.0 : found->trust;
  };
  const auto known_of = [&](int other) -> const KnownCivilization * {
    const auto found = std::ranges::find(knowledge.civilizations, other,
                                         &KnownCivilizationEntry::key);
    return found == knowledge.civilizations.end() ? nullptr
                                                  : &found->civilization;
  };

  int responses = 0;
  for (const auto &proposal : view.proposals) {
    if (responses >= 2)
      break;
    if (proposal.status != DiplomaticProposalStatus::pending ||
        proposal.recipient_civilization_id != civilization)
      continue;
    const int proposer = proposal.proposer_civilization_id;
    const double trust = trust_of(proposer);
    const auto *war = active_war_with(view, civilization, proposer);
    const auto side = [&](double aggressor_value,
                          double defender_value) {
      return war && war->aggressor_civilization_id == civilization
                 ? aggressor_value
                 : defender_value;
    };
    const double own_exhaustion =
        war ? side(war->aggressor_exhaustion, war->defender_exhaustion)
            : 0.0;
    const double score_against =
        war ? side(-war->war_score, war->war_score) : 0.0;
    bool accept;
    switch (proposal.kind) {
    case DiplomaticProposalKind::peace_offer:
      accept = war == nullptr || own_exhaustion >= tolerance * 0.7 ||
               score_against > -0.2;
      break;
    case DiplomaticProposalKind::ceasefire_offer:
      accept = war == nullptr || own_exhaustion >= tolerance * 0.5 ||
               score_against > -0.6;
      break;
    case DiplomaticProposalKind::agreement:
      if (war != nullptr) {
        accept = false;
        break;
      }
      switch (proposal.agreement_type.value_or(
          DiplomaticAgreementType::non_aggression)) {
      case DiplomaticAgreementType::non_aggression:
        accept = trust > -0.4;
        break;
      case DiplomaticAgreementType::access:
        accept = trust > 0.2;
        break;
      case DiplomaticAgreementType::trade:
        accept = trust > -0.2;
        break;
      case DiplomaticAgreementType::research_exchange:
        accept = trust > 0.0 && traits.scientific_curiosity > 0.3;
        break;
      case DiplomaticAgreementType::cooperation:
        accept = trust > 0.3;
        break;
      default:
        accept = trust > 0.1;
        break;
      }
      break;
    case DiplomaticProposalKind::demand: {
      // Yield to overwhelming known force; otherwise refuse.
      const auto *known = known_of(proposer);
      const double threat =
          known && known->has_military_estimate
              ? known->estimated_military_midpoint()
              : 0.0;
      accept = threat > 0 &&
               threat > review.own_state.military_strength * 1.5;
      break;
    }
    case DiplomaticProposalKind::access_request:
      accept = trust > 0.2;
      break;
    case DiplomaticProposalKind::trade_offer:
      accept = trust > -0.1;
      break;
    }
    if (try_diplomatic_action([&] {
          simulation.respond_to_proposal(proposal.proposal_id,
                                         civilization, accept,
                                         diplomacy_tick);
        })) {
      ++result.responses_given;
      ++responses;
    }
  }

  const auto own_claims = active_claims_by(view, civilization);
  int answered = 0;
  for (const auto &claim : view.claims) {
    if (answered >= 2)
      break;
    if (!claim.active || claim.claimant_civilization_id == civilization)
      continue;
    const int claimant = claim.claimant_civilization_id;
    const bool already =
        std::ranges::any_of(view.claim_responses, [&](const auto &r) {
          return r.claim_id == claim.claim_id &&
                 r.responding_civilization_id == civilization &&
                 r.response != TerritorialClaimResponse::none;
        });
    if (already)
      continue;
    const auto *known = known_of(claimant);
    const double threat =
        known && known->has_military_estimate
            ? known->estimated_military_midpoint()
            : 0.0;
    const double trust = trust_of(claimant);
    TerritorialClaimResponse response;
    if (own_claims.contains(claim.system_id)) {
      // Contested: dispute unless hopelessly outmatched and pragmatic.
      const bool outmatched =
          threat > 0 &&
          threat > review.own_state.military_strength * 1.5;
      response = outmatched && !traits.honor_bound
                     ? TerritorialClaimResponse::recognized
                     : TerritorialClaimResponse::disputed;
    } else if (active_war_with(view, civilization, claimant) != nullptr) {
      response = TerritorialClaimResponse::disputed;
    } else if (trust >= 0.2) {
      response = TerritorialClaimResponse::recognized;
    } else {
      continue; // no stake in the system — stay silent
    }
    if (try_diplomatic_action([&] {
          simulation.respond_to_territorial_claim(claim.claim_id,
                                                civilization, response,
                                                diplomacy_tick);
        })) {
      ++result.claims_answered;
      ++answered;
    }
  }

  return result;
}

} // namespace stellar::core
