#pragma once

#include <stellar/core/strategic_input_builder.hpp>
#include <stellar/core/strategic_planning.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace stellar::core {

using StrategicKnowledgeQuery =
    std::function<StrategicKnowledgeSnapshot(int, std::int64_t)>;

class CivilizationStrategicDirector {
public:
  explicit CivilizationStrategicDirector(
      CivilizationStrategicInputBuilder input_builder = CivilizationStrategicInputBuilder{},
      CivilizationStrategicPlanner planner = CivilizationStrategicPlanner{},
      CivilizationStrategicIntentBuilder intent_builder = CivilizationStrategicIntentBuilder{});

  [[nodiscard]] CivilizationStrategicReview
  review(StrategicInputWorldView world, int civilization_id,
         const CivilizationTraits &traits, const StrategicKnowledgeSnapshot &knowledge,
         std::int64_t now_tick, bool force_review = false);
  void invalidate(int civilization_id) noexcept;
  void remove_civilization(int civilization_id) noexcept;
  void clear() noexcept;
  [[nodiscard]] std::size_t cached_plan_count() const noexcept;

private:
  CivilizationStrategicInputBuilder input_builder_;
  CivilizationStrategicPlanner planner_;
  CivilizationStrategicIntentBuilder intent_builder_;
};

struct StrategicRuntimeWorldView {
  std::int64_t campaign_seed{};
  StrategicInputWorldView input;
};

class CivilizationStrategicRuntimeCoordinator {
public:
  explicit CivilizationStrategicRuntimeCoordinator(
      CivilizationStrategicDirector director = CivilizationStrategicDirector{},
      StrategicKnowledgeQuery knowledge = {});

  [[nodiscard]] std::vector<CivilizationStrategicReview>
  advance(StrategicRuntimeWorldView world, double simulation_days);
  [[nodiscard]] IndustryPriorityWeights
  get_industry_weights(int civilization_id) const noexcept;
  [[nodiscard]] ShipbuildingStrategicPreference
  get_shipbuilding_preference(int civilization_id) const;
  [[nodiscard]] std::size_t published_intent_count() const noexcept;
  [[nodiscard]] std::size_t published_preference_count() const noexcept;
  [[nodiscard]] std::size_t cached_plan_count() const noexcept;
  [[nodiscard]] double strategic_days() const noexcept;
  [[nodiscard]] std::optional<std::int64_t> campaign_seed() const noexcept;
  void reset() noexcept;

private:
  void ensure_campaign(std::int64_t seed) noexcept;
  CivilizationStrategicDirector director_;
  StrategicIndustryPriorityProvider industry_;
  StrategicShipbuildingPreferenceProvider shipbuilding_;
  StrategicKnowledgeQuery knowledge_;
  std::unordered_map<int, std::int64_t> next_review_tick_;
  std::optional<std::int64_t> campaign_seed_;
  double strategic_days_{};
};

} // namespace stellar::core
