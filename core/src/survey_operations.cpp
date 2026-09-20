#include <stellar/core/survey_operations.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace stellar::core {

double SurveyOperationsProfile::progress_per_day() const {
  return 1.0 / estimated_science_survey_days;
}

namespace {
struct SurveySummary {
  int planets{}, moons{}, anomalies{}, rare_resources{};
  bool has_physical_hazard{};
  double physical_hazard{};
  void add(const PlanetaryBody& body) {
    if (body.kind == PlanetaryBodyKind::Moon)
      ++moons;
    else
      ++planets;
    anomalies += body.has_anomaly ? 1 : 0;
    rare_resources += body.has_rare_resource ? 1 : 0;

    // Enumerable.Max(double) ignores NaN once any ordered value is present,
    // but returns NaN for an all-NaN nonempty sequence.
    if (!has_physical_hazard ||
        (physical_hazard != physical_hazard &&
         body.environment.radiation_hazard ==
             body.environment.radiation_hazard) ||
        body.environment.radiation_hazard > physical_hazard) {
      physical_hazard = body.environment.radiation_hazard;
    }
    has_physical_hazard = true;
  }
};
SurveyOperationsProfile finish(const StellarSystem* system, const SurveySummary& summary) {
  const auto& [planets,moons,anomalies,rare_resources,has_physical_hazard,physical_hazard]=summary;
  double days = 8.0 + planets * 0.85 + moons * 0.35;
  double content_days = 0.0;
  switch (system->archetype) {
  case StarArchetype::Nebula:
    content_days = 2.2;
    break;
  case StarArchetype::NeutronPulsar:
    content_days = 3.6;
    break;
  case StarArchetype::BlackHole:
    content_days = 4.2;
    break;
  case StarArchetype::Dangerous:
    content_days = 3.0;
    break;
  case StarArchetype::AncientRuin:
    content_days = 1.8;
    break;
  case StarArchetype::Legendary:
    content_days = 2.0;
    break;
  default:
    break;
  }

  double stellar_days = 0.0;
  if (system->primary) {
    switch (*system->primary) {
    case StellarClass::NeutronStar:
    case StellarClass::Pulsar:
      stellar_days = 3.6;
      break;
    case StellarClass::BlackHole:
      stellar_days = 4.2;
      break;
    case StellarClass::HotBlueStar:
    case StellarClass::Giant:
    case StellarClass::Protostar:
      stellar_days = 2.4;
      break;
    case StellarClass::WhiteDwarf:
      stellar_days = 1.4;
      break;
    default:
      break;
    }
  }
  days += std::max(content_days, stellar_days);
  days += std::min(2.4, anomalies * 0.8);
  days += std::min(1.5, rare_resources * 0.5);
  days = std::clamp(days, SurveyOperationsProfiler::minimum_survey_days, SurveyOperationsProfiler::maximum_survey_days);

  const bool severe_star =
      (system->primary && (*system->primary == StellarClass::NeutronStar ||
                           *system->primary == StellarClass::Pulsar ||
                           *system->primary == StellarClass::BlackHole ||
                           *system->primary == StellarClass::HotBlueStar)) ||
      system->archetype == StarArchetype::NeutronPulsar ||
      system->archetype == StarArchetype::BlackHole ||
      system->archetype == StarArchetype::Dangerous;
  const bool elevated_star =
      (system->primary && (*system->primary == StellarClass::Giant ||
                           *system->primary == StellarClass::Protostar ||
                           *system->primary == StellarClass::WhiteDwarf)) ||
      system->archetype == StarArchetype::Nebula;
  const auto hazard = severe_star || physical_hazard >= 0.72
                          ? SurveyOperationalHazard::Severe
                      : elevated_star || physical_hazard >= 0.40
                          ? SurveyOperationalHazard::Elevated
                          : SurveyOperationalHazard::Routine;
  return {system->id, planets, moons, days, hazard};
}
} // namespace

SurveyOperationsProfile SurveyOperationsProfiler::build(
    std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies,int system_id) const {
  const auto system=std::find_if(systems.begin(),systems.end(),[=](const auto& s){return s.id==system_id;});
  if(system==systems.end())throw std::runtime_error("Unknown system "+std::to_string(system_id)+".");
  SurveySummary summary;
  for(const auto& body:bodies)if(body.system_id==system_id)summary.add(body);
  return finish(&*system,summary);
}

SurveyOperationsProfile SurveyOperationsBatch::build(int system_id) {
  if(!prepared_){
    std::unordered_map<int,SurveySummary> summaries;
    summaries.reserve(systems_.size());
    for(const auto& system:systems_)summaries.try_emplace(system.id);
    // Preserve source order, including legacy NaN maximum semantics. Bodies
    // whose parent system is absent do not enter the profile table.
    for(const auto& body:bodies_)if(auto it=summaries.find(body.system_id);it!=summaries.end())it->second.add(body);
    std::unordered_map<int,SurveyOperationsProfile> profiles;
    profiles.reserve(systems_.size());
    for(const auto& system:systems_)profiles.try_emplace(system.id,finish(&system,summaries.at(system.id)));
    profiles_=std::move(profiles);prepared_=true;
  }
  if(auto it=profiles_.find(system_id);it!=profiles_.end())return it->second;
  throw std::runtime_error("Unknown system "+std::to_string(system_id)+".");
}

} // namespace stellar::core
