#include <stellar/core/survey_operations.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace stellar::core {

double SurveyOperationsProfile::progress_per_day() const {
  return 1.0 / estimated_science_survey_days;
}

SurveyOperationsProfile
SurveyOperationsProfiler::build(std::span<const StellarSystem> systems,
                                std::span<const PlanetaryBody> bodies,
                                int system_id) const {
  const auto system = std::find_if(
      systems.begin(), systems.end(),
      [system_id](const auto &item) { return item.id == system_id; });
  if (system == systems.end())
    throw std::runtime_error("Unknown system " + std::to_string(system_id) +
                             ".");

  int planets = 0;
  int moons = 0;
  int anomalies = 0;
  int rare_resources = 0;
  bool has_physical_hazard = false;
  double physical_hazard = 0.0;
  for (const auto &body : bodies) {
    if (body.system_id != system_id)
      continue;
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
  days = std::clamp(days, minimum_survey_days, maximum_survey_days);

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
  return {system_id, planets, moons, days, hazard};
}

} // namespace stellar::core
