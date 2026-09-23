#pragma once

#include <stellar/core/colony_economy.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/settlement_body_index.hpp>
#include <stellar/engine/economy_catalog.hpp>

#include <span>
#include <vector>

namespace stellar::core {

// Read-only projection of authoritative campaign sustenance state into
// the engine economy framework's demand/bottleneck analysis
// (analyze_economy). Core economy rules stay authoritative: capacities
// and reserves are computed through the same public functions the
// economy phase calls (surface_colony_output ->
// surface_sustenance_projection -> colony_sustenance_capacity ->
// preview_colony_reserves) — this adapter only re-shapes the results
// into ResourceObservation/demand rows. Nothing here writes back.
//
// Unit mapping (Core units are population-millions-days):
//   demand_per_day    = colony.population_millions (food & water)
//   produced_per_day  = sustenance capacity (units/day installed rate)
//   consumed_per_day  = colony.population_millions
//   stock             = stored_*_population_days_millions
//   reserve_days      = stock / consumed_per_day — matches the
//                       authoritative preview_colony_reserves days.

// Catalog carrying the sustenance resource vocabulary — one production
// recipe per channel so producer/import lookups resolve. Content
// definition only; callers may build once and cache.
[[nodiscard]] engine::EconomyCatalog sustenance_economy_catalog();

// Per-colony projection. `bodies` is the colony's planetary context
// (SettlementBodyIndex::bodies_for or equivalent); empty for legacy
// colonies without a body. `interval_days` is the surface output/power
// interval — the economy phase passes the step's simulation days.
[[nodiscard]] std::vector<engine::EconomyDiagnostic>
analyze_colony_sustenance(const Colony& colony,
                          std::span<const PlanetaryBody> bodies,
                          double interval_days,
                          const engine::EconomyCatalog& catalog);

// Aggregate projection across one civilization's colonies: demand,
// production, consumption and stock roll up before analysis, so
// bottlenecks describe the civilization-level sustenance position.
[[nodiscard]] std::vector<engine::EconomyDiagnostic>
analyze_civilization_sustenance(std::span<const Colony> colonies,
                                int civilization_id,
                                const SettlementBodyIndex& body_index,
                                double interval_days,
                                const engine::EconomyCatalog& catalog);

} // namespace stellar::core
