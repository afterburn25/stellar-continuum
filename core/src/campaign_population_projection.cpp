#include <stellar/core/campaign_population_projection.hpp>
#include <stellar/core/surface_economy.hpp>

#include <algorithm>

namespace stellar::core {

ColonyPopulationProjection
project_colony_population(const Colony& colony,
                          std::span<const PlanetaryBody> bodies,
                          double interval_days,
                          bool industrial_automation) {
  ColonyPopulationProjection result;
  const double population = std::max(0.0, colony.population_millions);

  // Demographic template: authored species biology where the species is
  // catalogued, framework defaults otherwise — the projection is a
  // query model, never a growth authority.
  engine::DemographicProfile profile;
  profile.id = colony.population_species_id;
  for (const auto& species : species_biology_profiles()) {
    if (species.id != colony.population_species_id) continue;
    profile.lifespan_years = species.baseline_lifespan_years;
    profile.food_per_capita_per_day = species.baseline_metabolic_demand;
    break;
  }
  result.population.define_profile(std::move(profile));

  const auto surface = surface_colony_output(colony, interval_days);
  const auto sustenance = colony_sustenance_capacity(
      bodies, colony, surface_sustenance_projection(surface));
  const auto labor = colony_labor(
      colony, industrial_automation,
      std::min(surface.workforce_available_millions,
               surface.workforce_demand_millions));
  // colony_habitat_support validates positive population — a
  // depopulated/legacy colony carries no evaluable habitat, so the
  // projection reads neutral fit rather than throwing. Unknown species
  // on a populated colony still propagates: that is corrupt state.
  if (population > 0.0) {
    const auto habitat = colony_habitat_support(colony, bodies);
    if (habitat.environment)
      result.natural_habitability = habitat.environment->natural_habitability;
  }

  const double wellbeing = std::clamp(colony.stability, 0.0, 1.0);
  engine::CohortKey key;
  key.profile = colony.population_species_id;
  result.cohort = key;

  engine::PopulationCohort cohort;
  cohort.key = key;
  cohort.size = population;
  for (auto& bucket : cohort.age_distribution)
    bucket = 1.0 / static_cast<double>(engine::kAgeBuckets);
  cohort.happiness = wellbeing;
  cohort.morale = wellbeing;
  cohort.employment_rate = std::clamp(labor.employment_rate, 0.0, 1.0);
  cohort.environment_suitability =
      std::clamp(result.natural_habitability, 0.0, 1.0);
  const double housing_capacity =
      std::max(0.0, sustenance.housing_capacity_millions);
  cohort.housing_coverage =
      population <= 0.0 ? 1.0
                        : std::clamp(housing_capacity / population, 0.0, 1.0);
  result.population.add(cohort);

  auto& conditions = result.conditions;
  conditions.food_ratio =
      population <= 0.0
          ? 1.0
          : std::clamp(
                std::max(0.0, sustenance.food_capacity_millions) / population,
                0.0, 1.0);
  conditions.goods_ratio =
      population <= 0.0
          ? 1.0
          : std::clamp(
                std::max(0.0, sustenance.water_capacity_millions) /
                    population,
                0.0, 1.0);
  conditions.housing_ratio = cohort.housing_coverage;
  conditions.jobs_available = std::max(
      0.0, std::min(surface.workforce_available_millions,
                    surface.workforce_demand_millions) -
               labor.employed_population_millions);
  conditions.overcrowding =
      population <= 0.0
          ? 0.0
          : std::max(0.0, population / std::max(1e-9, housing_capacity) - 1.0);
  conditions.environment_suitability = cohort.environment_suitability;
  conditions.security = wellbeing;
  return result;
}

} // namespace stellar::core
