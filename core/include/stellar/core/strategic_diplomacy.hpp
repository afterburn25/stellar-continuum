#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/diplomacy_simulation.hpp>
#include <stellar/core/diplomacy_state.hpp>
#include <stellar/core/strategic_intent.hpp>
#include <stellar/core/strategic_planning.hpp>

#include <cstdint>

namespace stellar::core {

// Bounded diplomatic action channel for AI civilizations. Translates
// observer-legitimate strategic knowledge into authoritative diplomacy
// commands through the simulation — never through privileged state.
// At most one war declaration, one peace overture, and one agreement
// proposal are issued per call, plus up to two inbound-proposal
// responses and two claim responses; every action is validated by the
// simulation's own rules, so hidden civilizations, duplicate wars and
// invalid goals can never be produced.
class StrategicDiplomacyExecutor {
public:
  struct Result {
    int wars_declared{};
    int proposals_sent{};
    int responses_given{};
    int claims_answered{};
  };

  // `view` must be the acting civilization's own observer-scoped
  // diplomatic view and `knowledge` its strategic knowledge snapshot at
  // `now_tick`; the review supplies own military strength and the plan
  // that gates diplomatic outreach. All decisions key off what the
  // civilization legitimately knows — estimates, not true fleet power.
  // `now_tick` is the strategic review clock (whole campaign days);
  // knowledge comparisons use it directly while every diplomacy command
  // is stamped on the campaign diplomacy clock (1000 ticks/day).
  [[nodiscard]] Result execute(DiplomacySimulation &simulation,
                               int civilization,
                               const CivilizationTraits &traits,
                               const CivilizationStrategicReview &review,
                               const StrategicKnowledgeSnapshot &knowledge,
                               const DiplomaticStateView &view,
                               std::int64_t now_tick) const;
};

} // namespace stellar::core
