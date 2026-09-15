#include <stellar/core/strategic_planning.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

namespace stellar::core {
namespace {
double clamp(double value, double low, double high) {
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}
double dotnet_max(double a, double b) noexcept {
  return std::isnan(a) || std::isnan(b)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::max(a, b);
}
double dotnet_min(double a, double b) noexcept {
  return std::isnan(a) || std::isnan(b)
             ? std::numeric_limits<double>::quiet_NaN()
             : std::min(a, b);
}
std::int64_t unchecked_subtract(std::int64_t a, std::int64_t b) noexcept {
  return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(a) -
                                     std::bit_cast<std::uint64_t>(b));
}
std::string percent(double value) {
  if (std::isnan(value))
    return "NaN %";
  if (std::isinf(value))
    return value > 0 ? "Infinity %" : "-Infinity %";
  return std::format("{:.0f} %", value * 100.0);
}
int dotnet_compare(double a, double b) noexcept {
  if (std::isnan(a))
    return std::isnan(b) ? 0 : -1;
  if (std::isnan(b))
    return 1;
  return a < b ? -1 : a > b ? 1 : 0;
}
}
double KnownCivilization::estimated_military_midpoint() const noexcept {
  return (estimated_military_low + estimated_military_high) * 0.5;
}
double KnownCivilization::freshness(std::int64_t now,
                                    std::int64_t stale) const noexcept {
  if (stale <= 0)
    return 0;
  const auto age = std::max<std::int64_t>(
      0, unchecked_subtract(now, last_military_observation_tick));
  return clamp(1.0 - static_cast<double>(age) / static_cast<double>(stale), 0,
               1);
}
WarAssessment StrategicDecisionEvaluator::evaluate_war(
    const CivilizationTraits &t, double own, const KnownCivilization &k,
    std::int64_t now, bool compelled) const {
  if (!k.has_military_estimate)
    return {-1, 1, 0, true, false};
  const auto fresh = k.freshness(now, 365);
  const auto confidence = clamp(k.estimate_confidence * fresh, .05, 1);
  const auto threat =
      k.estimated_military_midpoint() * (1 + (1 - confidence) * .35);
  const auto ratio = own / dotnet_max(1., threat);
  const auto floor = .70 + t.survival_priority * .20 - t.risk_tolerance * .15;
  const bool rejects = ratio < floor && !(t.honor_bound && compelled);
  auto score = t.aggression * .35 + (k.has_shared_border ? t.territoriality * .35 : 0) +
               clamp((ratio - .75) / 1.5, -.5, .6) -
               clamp(k.trust, -1, 1) * .30 -
               clamp(k.known_trade_dependence, 0, 1) * .25 -
               clamp(k.known_war_exhaustion, 0, 1) * .35 -
               (k.has_defense_treaty_with_observer ? 1. : 0.);
  if (rejects)
    score = dotnet_min(score, -.65);
  if (t.honor_bound && compelled)
    score = dotnet_max(score, .55);
  return {score, ratio, confidence, rejects, score >= .50};
}
CivilizationStrategicPlanner::CivilizationStrategicPlanner(
    StrategicDecisionEvaluator evaluator, std::int64_t interval)
    : evaluator_(std::move(evaluator)),
      review_interval_ticks_(std::max<std::int64_t>(1, interval)) {}
CivilizationStrategicPlan CivilizationStrategicPlanner::get_plan(
    int id, const CivilizationTraits &t, const CivilizationOwnState &own,
    const StrategicKnowledgeSnapshot &knowledge, std::int64_t now, bool force) {
  if (!force)
    if (const auto found = cached_plans_.find(id);
        found != cached_plans_.end() && now < found->second.review_after_tick)
      return found->second;
  std::vector<StrategicPriority> p;
  const auto supply = clamp(own.supply_coverage_ratio, 0, 2);
  if (supply < 1) p.push_back({StrategicPriorityType::StabilizeSupply,.60+clamp(1-supply,0,1)*1.10+t.survival_priority*.25,"Own supply coverage is "+percent(supply)+"; survival and expansion depend on restoring logistics."});
  const auto scarcity=1./(1.+dotnet_max(0.,own.industry_reserve)/250.);
  p.push_back({StrategicPriorityType::ExpandIndustry,.30+scarcity*.55+t.greed*.15,"Own industrial reserve constrains construction, shipbuilding and recovery capacity."});
  if(own.has_available_research){const auto need=1./(1.+dotnet_max(0.,own.research_capacity)/10.);p.push_back({StrategicPriorityType::ExpandResearch,.35+need*.35+t.scientific_curiosity*.50,"Known research opportunities exist and can be pursued with the civilization's own scientific capacity."});}
  if(own.has_unexplored_reachable_systems)p.push_back({StrategicPriorityType::Explore,.30+t.scientific_curiosity*.40+t.risk_tolerance*.15,"Legitimately known reachable space still contains unexplored targets."});
  if(own.has_known_colonization_opportunity){const auto restraint=supply<.95?(.95-supply)*.90:0.;p.push_back({StrategicPriorityType::Colonize,.45+t.greed*.25+t.risk_tolerance*.10-restraint,"A known colonization opportunity exists; logistics health moderates expansion appetite."});}
  std::optional<StrategicPriority> defense;double strongest=0;bool has=false;int threat_id=0;WarAssessment assessment;
  for(const auto&e:knowledge.civilizations){const auto&k=e.civilization;if(!k.has_military_estimate)continue;auto a=evaluator_.evaluate_war(t,own.military_strength,k,now);auto score=1./dotnet_max(.05,a.perceived_strength_ratio);score*=.75+(1-clamp(k.trust,-1,1))*.25;if(k.known_to_be_at_war)score*=.90;if(!has||score>strongest){has=true;strongest=score;threat_id=k.civilization_id;assessment=a;}}
  if(has){auto score=.40+clamp(1.10-assessment.perceived_strength_ratio,0,1)*.85+(1-assessment.intelligence_confidence)*.20+t.survival_priority*.25;defense=StrategicPriority{StrategicPriorityType::Defend,score,"Known civilization "+std::to_string(threat_id)+" is a credible threat under current observed/estimated intelligence."};}
  const KnownCivilization*unknown=nullptr;for(const auto&e:knowledge.civilizations){const auto&k=e.civilization;if(k.known_to_be_at_war&&!k.has_military_estimate&&(!unknown||k.civilization_id<unknown->civilization_id))unknown=&k;}
  if(unknown){const auto score=.95+t.survival_priority*.30;if(!defense||score>defense->score)defense=StrategicPriority{StrategicPriorityType::Defend,score,"Civilization "+std::to_string(unknown->civilization_id)+" is known to be at war with us; enemy strength remains legitimately unknown."};}
  if(defense)p.push_back(*defense);
  if(own.can_build_interstellar_ships&&own.has_fleet_capacity_shortfall)p.push_back({StrategicPriorityType::BuildFleet,.48+t.aggression*.25+t.survival_priority*.20,"Own fleet capacity is below current strategic needs and ship construction is known to be available."});
  const KnownCivilization*relation=nullptr;double opportunity=0;for(const auto&e:knowledge.civilizations){const auto&k=e.civilization;if(!(k.trust>-.25)||k.known_to_be_at_war)continue;const auto score=k.known_trade_dependence+k.trust;if(!relation||dotnet_compare(score,opportunity)>0){relation=&k;opportunity=score;}}
  if(relation)p.push_back({StrategicPriorityType::ImproveRelations,.22+clamp(relation->known_trade_dependence,0,1)*.20+clamp(relation->trust,-1,1)*.10,"Known relations with civilization "+std::to_string(relation->civilization_id)+" provide a plausible diplomatic opportunity."});
  std::stable_sort(p.begin(),p.end(),[](const auto&a,const auto&b){const auto c=dotnet_compare(b.score,a.score);return c?c<0:a.type<b.type;});
  if(now>std::numeric_limits<std::int64_t>::max()-review_interval_ticks_)throw std::overflow_error("Arithmetic operation resulted in an overflow.");
  CivilizationStrategicPlan plan{id,now,now+review_interval_ticks_,std::move(p)};cached_plans_[id]=plan;return plan;
}
void CivilizationStrategicPlanner::invalidate(int id) noexcept{cached_plans_.erase(id);}void CivilizationStrategicPlanner::remove_civilization(int id) noexcept{cached_plans_.erase(id);}void CivilizationStrategicPlanner::clear() noexcept{cached_plans_.clear();}std::size_t CivilizationStrategicPlanner::cached_plan_count()const noexcept{return cached_plans_.size();}
} // namespace stellar::core
