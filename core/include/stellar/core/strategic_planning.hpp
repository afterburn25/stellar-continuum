#pragma once

#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/strategic_intent.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace stellar::core {

struct KnownCivilization {
  int civilization_id{};
  double trust{}, estimated_military_low{}, estimated_military_high{},
      estimate_confidence{};
  std::int64_t last_military_observation_tick{};
  bool has_shared_border{};
  double known_trade_dependence{}, known_war_exhaustion{};
  bool known_to_be_at_war{}, has_defense_treaty_with_observer{},
      has_military_estimate{true};
  [[nodiscard]] double estimated_military_midpoint() const noexcept;
  [[nodiscard]] double freshness(std::int64_t now_tick,
                                 std::int64_t stale_after_ticks) const noexcept;
};

struct KnownCivilizationEntry {
  int key{};
  KnownCivilization civilization;
};

struct StrategicKnowledgeSnapshot {
  std::int64_t observed_at_tick{};
  std::vector<KnownCivilizationEntry> civilizations;
};

struct WarAssessment {
  double score{}, perceived_strength_ratio{}, intelligence_confidence{};
  bool survival_gate_triggered{}, recommend_war{};
};

class StrategicDecisionEvaluator {
public:
  WarAssessment evaluate_war(const CivilizationTraits &traits,
                             double own_known_military_strength,
                             const KnownCivilization &target,
                             std::int64_t now_tick,
                             bool honor_compels_battle = false) const;
};

class CivilizationStrategicPlanner {
public:
  explicit CivilizationStrategicPlanner(
      StrategicDecisionEvaluator evaluator = {},
      std::int64_t review_interval_ticks = 30);
  CivilizationStrategicPlan
  get_plan(int civilization_id, const CivilizationTraits &traits,
           const CivilizationOwnState &own_state,
           const StrategicKnowledgeSnapshot &knowledge, std::int64_t now_tick,
           bool force_review = false);
  void invalidate(int civilization_id) noexcept;
  void remove_civilization(int civilization_id) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t cached_plan_count() const noexcept;

private:
  StrategicDecisionEvaluator evaluator_;
  std::int64_t review_interval_ticks_;
  std::unordered_map<int, CivilizationStrategicPlan> cached_plans_;
};
} // namespace stellar::core
