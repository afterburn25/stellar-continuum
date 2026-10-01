#include <stellar/core/campaign_economy_projection.hpp>
#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <unordered_map>

namespace stellar::core {

engine::EconomyCatalog sustenance_economy_catalog() {
    engine::EconomyCatalog catalog;
    auto define = [&catalog](std::string id) {
        engine::ResourceSpec spec;
        spec.id = std::move(id);
        spec.name_key = "RES_" + spec.id;
        spec.category = engine::ResourceCategory::Consumable;
        spec.mass_per_unit = 1.0;
        catalog.define(std::move(spec));
    };
    define("res.food");
    define("res.water");
    define("res.power");
    catalog.add_recipe({.id = "colony.sustenance.food",
                        .name_key = "RECIPE_COLONY_FOOD",
                        .outputs = {{"res.food", 1.0}},
                        .duration_days = 1.0});
    catalog.add_recipe({.id = "colony.sustenance.water",
                        .name_key = "RECIPE_COLONY_WATER",
                        .outputs = {{"res.water", 1.0}},
                        .duration_days = 1.0});
    catalog.add_recipe({.id = "colony.power.grid",
                        .name_key = "RECIPE_COLONY_POWER",
                        .outputs = {{"res.power", 1.0}},
                        .duration_days = 1.0});
    return catalog;
}

namespace {

void accumulate(const Colony& colony, std::span<const PlanetaryBody> bodies,
                double interval_days,
                std::vector<engine::ResourceAmount>& demand,
                std::unordered_map<std::string, engine::ResourceObservation>&
                    observed) {
    const auto surface = surface_colony_output(colony, interval_days);
    const auto sustenance = colony_sustenance_capacity(
        bodies, colony, surface_sustenance_projection(surface));
    const auto reserves =
        preview_colony_reserves(colony, sustenance, interval_days);
    const double population = std::max(0.0, colony.population_millions);

    demand.push_back({"res.food", population});
    demand.push_back({"res.water", population});
    auto& food = observed["res.food"];
    food.produced_per_day += sustenance.food_capacity_millions;
    food.consumed_per_day += population;
    // Stock expressed in the clamped units the authoritative reserve
    // calc reports, so reserve_days == preview_colony_reserves exactly.
    food.stock += reserves.food_reserve_days * population;
    auto& water = observed["res.water"];
    water.produced_per_day += sustenance.water_capacity_millions;
    water.consumed_per_day += population;
    water.stock += reserves.water_reserve_days * population;
    // Power grid: installed supply vs building demand is a real
    // bottleneck axis (buildings shed when undersupplied).
    // stored_power_days is already denominated in days of demand, so
    // stock = stored days × demand keeps reserve_days ==
    // stored_power_days exactly.
    demand.push_back({"res.power", surface.demand});
    auto& power = observed["res.power"];
    power.produced_per_day += surface.supply;
    power.consumed_per_day += surface.demand;
    power.stock += surface.stored_power_days * surface.demand;
}

} // namespace

std::vector<engine::EconomyDiagnostic>
analyze_colony_sustenance(const Colony& colony,
                          std::span<const PlanetaryBody> bodies,
                          double interval_days,
                          const engine::EconomyCatalog& catalog) {
    std::vector<engine::ResourceAmount> demand;
    std::unordered_map<std::string, engine::ResourceObservation> observed;
    accumulate(colony, bodies, interval_days, demand, observed);
    return engine::analyze_economy(catalog, demand, observed);
}

std::vector<engine::EconomyDiagnostic>
analyze_civilization_sustenance(std::span<const Colony> colonies,
                                int civilization_id,
                                const SettlementBodyIndex& body_index,
                                double interval_days,
                                const engine::EconomyCatalog& catalog) {
    std::vector<engine::ResourceAmount> demand;
    std::unordered_map<std::string, engine::ResourceObservation> observed;
    std::vector<const Colony*> owned;
    for (const auto& colony : colonies)
        if (colony.civilization_id == civilization_id)
            owned.push_back(&colony);
    std::sort(owned.begin(), owned.end(),
              [](const Colony* a, const Colony* b) { return a->id < b->id; });
    for (const auto* colony : owned)
        accumulate(*colony, body_index.bodies_for(*colony), interval_days,
                   demand, observed);
    return engine::analyze_economy(catalog, demand, observed);
}

} // namespace stellar::core
