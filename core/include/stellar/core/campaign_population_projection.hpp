#pragma once

#include <stellar/core/colony_biology.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/engine/population.hpp>

#include <span>

namespace stellar::core {

// Read-only projection of authoritative colony population state into
// the engine cohort-population framework. Core stays the simulation
// authority — the projection maps the colony into a single-cohort
// Population plus the SettlementConditions the authoritative systems
// already compute (sustenance capacity chain, colony_labor, stability,
// colony_habitat_support) so const queries like migration_pressure()
// describe the colony's emigration/unrest position. The projected
// Population is a query model — callers MUST NOT advance() it into a
// competing growth authority; Core's scalar population remains the
// persisted truth.
//
// Unit/field mapping (Core units are population-millions):
//   cohort key.profile        = colony.population_species_id
//   cohort size               = colony.population_millions
//   DemographicProfile        = species_biology_profile: lifespan and
//                               food_per_capita_per_day carry the
//                               authored values; rate fields keep
//                               framework defaults (the projection is
//                               not a growth authority)
//   cohort happiness/morale   = clamp01(colony.stability)
//   cohort employment_rate    = colony_labor(...).employment_rate with
//                               the authoritative workforce args
//   cohort environment_fit    = colony_habitat_support natural
//                               habitability (1.0 for legacy/no-body)
//   conditions.food_ratio     = sustenance food capacity ÷ population
//   conditions.goods_ratio    = sustenance water capacity ÷ population
//                               (water is the second sustenance axis)
//   conditions.housing_ratio  = sustenance housing capacity ÷ population
//   conditions.overcrowding   = max(0, population ÷ housing capacity − 1)
struct ColonyPopulationProjection {
    engine::Population population;
    engine::SettlementConditions conditions;
    engine::CohortKey cohort;
    double natural_habitability{1.0};
};

// `bodies` is the colony's planetary context (SettlementBodyIndex::
// bodies_for or equivalent); `interval_days` is the surface-output
// interval the economy phase uses; `industrial_automation` is the civ's
// completed-project flag the authoritative colony_labor call takes.
[[nodiscard]] ColonyPopulationProjection
project_colony_population(const Colony& colony,
                          std::span<const PlanetaryBody> bodies,
                          double interval_days,
                          bool industrial_automation);

} // namespace stellar::core
