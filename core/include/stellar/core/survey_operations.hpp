#pragma once

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>

#include <span>

namespace stellar::core {

enum class SurveyOperationalHazard { Routine, Elevated, Severe };

struct SurveyOperationsProfile {
  int system_id{};
  int planet_count{};
  int moon_count{};
  double estimated_science_survey_days{};
  SurveyOperationalHazard operational_hazard{};

  double progress_per_day() const;
};

class SurveyOperationsProfiler {
public:
  static constexpr double minimum_survey_days = 9.0;
  static constexpr double maximum_survey_days = 28.0;

  SurveyOperationsProfile build(std::span<const StellarSystem> systems,
                                std::span<const PlanetaryBody> bodies,
                                int system_id) const;
};

} // namespace stellar::core
