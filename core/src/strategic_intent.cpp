#include <stellar/core/strategic_intent.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace stellar::core {
namespace {
double managed_max(double a, double b) noexcept {
  return std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::quiet_NaN()
                                         : std::max(a, b);
}
double managed_clamp(double v, double lo, double hi) noexcept {
  if (std::isnan(v)) return v;
  return v < lo ? lo : (v > hi ? hi : v);
}
std::string source_fixed_2(double value) {
  return detail::legacy_custom_fixed(value, 2, 2);
}
std::string priority_name(StrategicPriorityType value) {
  switch (value) {
  case StrategicPriorityType::StabilizeSupply: return "StabilizeSupply";
  case StrategicPriorityType::ExpandIndustry: return "ExpandIndustry";
  case StrategicPriorityType::ExpandResearch: return "ExpandResearch";
  case StrategicPriorityType::Explore: return "Explore";
  case StrategicPriorityType::Colonize: return "Colonize";
  case StrategicPriorityType::BuildFleet: return "BuildFleet";
  case StrategicPriorityType::Defend: return "Defend";
  case StrategicPriorityType::ImproveRelations: return "ImproveRelations";
  }
  return std::to_string(static_cast<int>(value));
}
} // namespace

const StrategicPriority *CivilizationStrategicPlan::primary_priority() const noexcept {
  return priorities.empty() ? nullptr : &priorities.front();
}
double CivilizationStrategicIntent::get_weight(StrategicPriorityType type) const noexcept {
  const auto item = weights.find(type);
  return item == weights.end() ? 0.0 : item->second;
}
CivilizationStrategicIntent CivilizationStrategicIntentBuilder::build(
    const CivilizationStrategicPlan &plan) const {
  CivilizationStrategicIntent result{plan.civilization_id, plan.generated_at_tick,
                                     plan.review_after_tick};
  for (int value = static_cast<int>(StrategicPriorityType::StabilizeSupply);
       value <= static_cast<int>(StrategicPriorityType::ImproveRelations); ++value)
    result.weights.emplace(static_cast<StrategicPriorityType>(value), 0.0);
  for (const auto &priority : plan.priorities)
    result.weights[priority.type] = managed_clamp(priority.score, 0.0, 2.0);
  const double supply = result.get_weight(StrategicPriorityType::StabilizeSupply);
  const double defense = result.get_weight(StrategicPriorityType::Defend);
  const double colonize = result.get_weight(StrategicPriorityType::Colonize);
  const double explore = result.get_weight(StrategicPriorityType::Explore);
  const double fleet = result.get_weight(StrategicPriorityType::BuildFleet);
  result.defer_new_colonization = supply > managed_max(.75, colonize) ||
                                  defense > colonize + .35;
  if (defense >= managed_max(explore, colonize) && defense >= .60)
    result.preferred_new_fleet_role = FleetRole::Military;
  else if (colonize >= explore && colonize >= .55 && !result.defer_new_colonization)
    result.preferred_new_fleet_role = FleetRole::Colony;
  else if (explore >= .45)
    result.preferred_new_fleet_role = FleetRole::Scout;
  else if (fleet >= .55)
    result.preferred_new_fleet_role = FleetRole::Military;
  if (const auto *primary = plan.primary_priority())
    result.summary = "Primary: " + priority_name(primary->type) +
                     " (" + source_fixed_2(primary->score) + ") — " + primary->reason;
  else result.summary = "No urgent strategic priority.";
  return result;
}
std::size_t StrategicIndustryPriorityProvider::published_intent_count() const noexcept { return intents_.size(); }
void StrategicIndustryPriorityProvider::publish(const CivilizationStrategicReview &review) { publish(review.intent); }
void StrategicIndustryPriorityProvider::publish(const CivilizationStrategicIntent &intent) { intents_[intent.civilization_id] = intent; }
void StrategicIndustryPriorityProvider::remove(int id) noexcept { intents_.erase(id); }
void StrategicIndustryPriorityProvider::clear() noexcept { intents_.clear(); }
IndustryPriorityWeights StrategicIndustryPriorityProvider::get_weights(int id) const noexcept {
  const auto item = intents_.find(id); if (item == intents_.end()) return {1., 1.};
  const auto &i = item->second;
  const double construction = 1. + i.get_weight(StrategicPriorityType::StabilizeSupply)*.90 + i.get_weight(StrategicPriorityType::ExpandIndustry)*.75 + i.get_weight(StrategicPriorityType::ExpandResearch)*.35;
  const double shipbuilding = 1. + i.get_weight(StrategicPriorityType::BuildFleet)*.95 + i.get_weight(StrategicPriorityType::Defend)*1.15 + i.get_weight(StrategicPriorityType::Explore)*.25 + i.get_weight(StrategicPriorityType::Colonize)*.35;
  return {managed_clamp(construction,.25,4.), managed_clamp(shipbuilding,.25,4.)};
}
std::size_t StrategicShipbuildingPreferenceProvider::published_preference_count() const noexcept { return preferences_.size(); }
void StrategicShipbuildingPreferenceProvider::publish(const CivilizationStrategicReview &review) { publish(review.intent); }
void StrategicShipbuildingPreferenceProvider::publish(const CivilizationStrategicIntent &intent) { preferences_[intent.civilization_id] = {intent.civilization_id, intent.preferred_new_fleet_role, intent.defer_new_colonization}; }
void StrategicShipbuildingPreferenceProvider::remove(int id) noexcept { preferences_.erase(id); }
void StrategicShipbuildingPreferenceProvider::clear() noexcept { preferences_.clear(); }
ShipbuildingStrategicPreference StrategicShipbuildingPreferenceProvider::get_preference(int id) const {
  const auto item = preferences_.find(id); return item == preferences_.end() ? ShipbuildingStrategicPreference{id, std::nullopt, false} : item->second;
}
} // namespace stellar::core
