#include <stellar/core/strategic_runtime.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stellar::core {

CivilizationStrategicDirector::CivilizationStrategicDirector(
    CivilizationStrategicInputBuilder input_builder,
    CivilizationStrategicPlanner planner,
    CivilizationStrategicIntentBuilder intent_builder)
    : input_builder_(std::move(input_builder)), planner_(std::move(planner)),
      intent_builder_(std::move(intent_builder)) {}

CivilizationStrategicReview CivilizationStrategicDirector::review(
    StrategicInputWorldView world, int civilization_id,
    const CivilizationTraits &traits, const StrategicKnowledgeSnapshot &knowledge,
    std::int64_t now_tick, bool force_review) {
  if (knowledge.observed_at_tick > now_tick)
    throw std::out_of_range("AI knowledge snapshot cannot originate in the future. (Parameter 'knowledge')");
  if (std::none_of(world.civilizations.begin(), world.civilizations.end(),
                   [=](const auto &civilization) { return civilization.id == civilization_id; }))
    throw std::out_of_range("Unknown civilization " + std::to_string(civilization_id) + ". (Parameter 'civilizationId')");
  auto own_state = input_builder_.build(world, civilization_id);
  auto plan = planner_.get_plan(civilization_id, traits, own_state, knowledge,
                                now_tick, force_review);
  auto intent = intent_builder_.build(plan);
  return {std::move(own_state), std::move(plan), std::move(intent)};
}

void CivilizationStrategicDirector::invalidate(int id) noexcept { planner_.invalidate(id); }
void CivilizationStrategicDirector::remove_civilization(int id) noexcept { planner_.remove_civilization(id); }
void CivilizationStrategicDirector::clear() noexcept { planner_.clear(); }
std::size_t CivilizationStrategicDirector::cached_plan_count() const noexcept { return planner_.cached_plan_count(); }
std::vector<CivilizationStrategicPlan> CivilizationStrategicDirector::snapshot() const { return planner_.snapshot(); }
void CivilizationStrategicDirector::restore(std::span<const CivilizationStrategicPlan> plans){planner_.restore(plans);}

CivilizationStrategicRuntimeCoordinator::CivilizationStrategicRuntimeCoordinator(
    CivilizationStrategicDirector director, StrategicKnowledgeQuery knowledge)
    : director_(std::move(director)), knowledge_(std::move(knowledge)) {
  if (!knowledge_) knowledge_ = [](int, std::int64_t now_tick) {
    if (now_tick < 0) throw std::out_of_range("Specified argument was out of the range of valid values. (Parameter 'nowTick')");
    return StrategicKnowledgeSnapshot{now_tick, {}};
  };
}

std::vector<CivilizationStrategicReview>
CivilizationStrategicRuntimeCoordinator::advance(StrategicRuntimeWorldView world,
                                                 double simulation_days) {
  if (!std::isfinite(simulation_days) || simulation_days < 0.0)
    throw std::out_of_range("Strategic time must be finite and non-negative. (Parameter 'simulationDays')");
  ensure_campaign(world.campaign_seed);
  const double next_days = strategic_days_ + simulation_days;
  if (!std::isfinite(next_days) || next_days >= 0x1p63)
    throw std::overflow_error("Accumulated strategic time cannot be represented as a native tick.");
  strategic_days_ = next_days;
  const auto now_tick = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(strategic_days_)));

  std::vector<const Civilization *> civilizations;
  for (const auto &civilization : world.input.civilizations)
    if (civilization_uses_ai(civilization,world.control) && !civilization.is_seeded_ancient)
      civilizations.push_back(&civilization);
  std::stable_sort(civilizations.begin(), civilizations.end(),
                   [](const auto *left, const auto *right) { return left->id < right->id; });
  std::vector<int> inactive;
  for(const auto &[id,tick]:next_review_tick_)
    if(std::ranges::none_of(civilizations,[&](const auto *c){return c->id==id;}))inactive.push_back(id);
  for(const auto id:inactive)remove_civilization(id);

  std::vector<CivilizationStrategicReview> reviews;
  for (const auto *civilization : civilizations) {
    const auto next = next_review_tick_.find(civilization->id);
    if (next != next_review_tick_.end() && now_tick < next->second) continue;
    auto knowledge = knowledge_(civilization->id, now_tick);
    if (knowledge.observed_at_tick > now_tick)
      throw std::runtime_error("Strategic knowledge provider returned observations from the future.");
    auto review = director_.review(world.input, civilization->id,
                                   civilization->traits, knowledge, now_tick);
    industry_.publish(review);
    shipbuilding_.publish(review);
    next_review_tick_[civilization->id] = review.plan.review_after_tick;
    reviews.push_back(std::move(review));
  }
  return reviews;
}

IndustryPriorityWeights CivilizationStrategicRuntimeCoordinator::get_industry_weights(int id) const noexcept { return industry_.get_weights(id); }
ShipbuildingStrategicPreference CivilizationStrategicRuntimeCoordinator::get_shipbuilding_preference(int id) const { return shipbuilding_.get_preference(id); }
std::size_t CivilizationStrategicRuntimeCoordinator::published_intent_count() const noexcept { return industry_.published_intent_count(); }
std::size_t CivilizationStrategicRuntimeCoordinator::published_preference_count() const noexcept { return shipbuilding_.published_preference_count(); }
std::size_t CivilizationStrategicRuntimeCoordinator::cached_plan_count() const noexcept { return director_.cached_plan_count(); }
double CivilizationStrategicRuntimeCoordinator::strategic_days() const noexcept { return strategic_days_; }
std::optional<std::int64_t> CivilizationStrategicRuntimeCoordinator::campaign_seed() const noexcept { return campaign_seed_; }

void CivilizationStrategicRuntimeCoordinator::reset() noexcept {
  campaign_seed_.reset(); strategic_days_ = 0.0; next_review_tick_.clear();
  director_.clear(); industry_.clear(); shipbuilding_.clear();
}

void CivilizationStrategicRuntimeCoordinator::remove_civilization(int id) noexcept {
  director_.remove_civilization(id);next_review_tick_.erase(id);industry_.remove(id);shipbuilding_.remove(id);
}
StrategicRuntimeSnapshot CivilizationStrategicRuntimeCoordinator::snapshot() const {
  return {campaign_seed_,strategic_days_,director_.snapshot()};
}
void CivilizationStrategicRuntimeCoordinator::restore(const StrategicRuntimeSnapshot &state){
  if(!std::isfinite(state.strategic_days)||state.strategic_days<0||state.strategic_days>=0x1p63||
      (!state.campaign_seed&&(state.strategic_days!=0||!state.plans.empty())))
    throw std::invalid_argument("Invalid strategic runtime clock.");
  decltype(next_review_tick_) reviews;
  StrategicIndustryPriorityProvider industry;StrategicShipbuildingPreferenceProvider ships;
  CivilizationStrategicIntentBuilder intents;
  for(const auto &plan:state.plans){
    if(plan.generated_at_tick>std::floor(state.strategic_days))throw std::invalid_argument("Strategic plan originates in the future.");
    reviews.emplace(plan.civilization_id,plan.review_after_tick);
    const auto intent=intents.build(plan);industry.publish(intent);ships.publish(intent);
  }
  director_.restore(state.plans);
  campaign_seed_=state.campaign_seed;strategic_days_=state.strategic_days;
  next_review_tick_=std::move(reviews);industry_=std::move(industry);shipbuilding_=std::move(ships);
}

void CivilizationStrategicRuntimeCoordinator::ensure_campaign(std::int64_t seed) noexcept {
  if (campaign_seed_ == seed) return;
  campaign_seed_ = seed; strategic_days_ = 0.0; next_review_tick_.clear();
  director_.clear(); industry_.clear(); shipbuilding_.clear();
}
} // namespace stellar::core
