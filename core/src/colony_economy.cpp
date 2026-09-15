#include <stellar/core/colony_economy.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr double sealed_capacity_per_infrastructure = 500.0;
constexpr double natural_biosphere_capacity_per_earth_area = 12000.0;

std::string limiting_name(double food, double water, double housing) {
    const double minimum = std::min(food, std::min(water, housing));
    std::string result;
    const auto append = [&](const char* value) {
        if (!result.empty()) result += " and ";
        result += value;
    };
    if (std::abs(food - minimum) <= .001) append("food");
    if (std::abs(water - minimum) <= .001) append("potable water");
    if (std::abs(housing - minimum) <= .001) append("housing");
    return result;
}

struct ReserveBalance { double effective_supply, reserve; };

ReserveBalance apply_reserve_balance(double reserve, double daily_production, double daily_demand,
    double maximum_reserve, double simulation_days) {
    if (daily_production >= daily_demand) {
        reserve = std::min(maximum_reserve, reserve + (daily_production - daily_demand) * simulation_days);
        return {daily_demand, reserve};
    }
    const double deficit = daily_demand - daily_production;
    const double withdrawn = std::min(reserve, deficit * simulation_days);
    reserve -= withdrawn;
    return {daily_production + (simulation_days <= 0.0 ? 0.0 : withdrawn / simulation_days), reserve};
}

struct ReserveCalculation { double food, water; ColonySustenanceReserveSnapshot snapshot; };

ReserveCalculation calculate_reserves(const Colony& colony, const ColonySustenanceCapacity& capacity,
    double simulation_days) {
    const double population = std::max(.001, colony.population_millions);
    const double food_maximum = std::max(population, capacity.food_capacity_millions) * maximum_food_reserve_days;
    const double water_maximum = std::max(population, capacity.water_capacity_millions) * maximum_water_reserve_days;
    const double stored_food = std::clamp(colony.stored_food_population_days_millions, 0.0, food_maximum);
    const double stored_water = std::clamp(colony.stored_water_population_days_millions, 0.0, water_maximum);
    const auto food = apply_reserve_balance(stored_food, capacity.food_capacity_millions, population, food_maximum, simulation_days);
    const auto water = apply_reserve_balance(stored_water, capacity.water_capacity_millions, population, water_maximum, simulation_days);
    const double effective = std::min(food.effective_supply, std::min(water.effective_supply, capacity.housing_capacity_millions));
    return {food.reserve, water.reserve, {food.reserve / population, water.reserve / population,
        std::max(0.0, effective / population), limiting_name(food.effective_supply, water.effective_supply, capacity.housing_capacity_millions)}};
}

Colony new_colony(int id, const Civilization& civilization, std::optional<int> body_id, std::string name,
    double population, double infrastructure, double stability, int hub_level) {
    Colony result;
    result.id = id;
    result.civilization_id = civilization.id;
    result.system_id = civilization.home_system_id;
    result.planetary_body_id = body_id;
    result.name = std::move(name);
    result.population_species_id = civilization.species_id;
    result.population_millions = population;
    result.infrastructure = infrastructure;
    result.stability = stability;
    result.surface_hub_level = hub_level;
    result.stored_food_population_days_millions = population * maximum_food_reserve_days;
    result.stored_water_population_days_millions = population * maximum_water_reserve_days;
    return result;
}
} // namespace

std::vector<Colony> seed_legacy_colonies(std::span<const Civilization> civilizations) {
    std::vector<Colony> result;
    result.reserve(civilizations.size());
    int id = 0;
    for (const Civilization& civilization : civilizations) {
        const bool ancient = civilization.is_seeded_ancient;
        result.push_back(new_colony(id++, civilization, {}, civilization.name + " Prime",
            ancient ? 12000.0 : 9500.0, ancient ? 3.0 : 1.0, 1.0, ancient ? 3 : 2));
    }
    return result;
}

std::vector<Colony> seed_colonies(std::span<const Civilization> civilizations, std::span<const PlanetaryBody> bodies) {
    std::vector<Colony> result;
    result.reserve(civilizations.size() + 2);
    int id = 0;
    for (const Civilization& civilization : civilizations) {
        const auto home = resolve_species_homeworld(civilization.id, civilization.species_id, civilization.home_system_id, bodies);
        const bool ancient = civilization.is_seeded_ancient;
        const bool earth = civilization.species_id == "terran_baseline" && civilization.home_system_id == sol_system_id && home.planetary_body_id == earth_body_id;
        result.push_back(new_colony(id++, civilization, home.planetary_body_id, earth ? "Earth" : civilization.name + " Prime",
            ancient ? 12000.0 : 9500.0, ancient ? 3.0 : 1.0, 1.0, ancient ? 3 : 2));
        if (!earth) continue;
        const auto add_sol_settlement = [&](int body_id, const char* name, double population, double infrastructure) {
            const bool available = std::any_of(bodies.begin(), bodies.end(), [&](const PlanetaryBody& body) {
                return body.id == body_id && body.system_id == sol_system_id && body.environment.has_solid_surface;
            });
            if (!available) throw std::runtime_error{"Canonical Sol settlement body " + std::to_string(body_id) + " is unavailable."};
            Colony dependent = new_colony(id++, civilization, body_id, name, population, infrastructure, .92, 1);
            dependent.system_id = sol_system_id;
            result.push_back(std::move(dependent));
        };
        add_sol_settlement(moon_body_id, "Luna", .10, .28);
        add_sol_settlement(4, "Mars", .25, .24);
    }
    return result;
}

std::vector<CivilizationEconomy> seed_economies(std::span<const Civilization> civilizations) {
    std::vector<CivilizationEconomy> result;
    result.reserve(civilizations.size());
    for (const Civilization& civilization : civilizations) {
        const bool ancient = civilization.is_seeded_ancient;
        CivilizationEconomy economy;
        economy.civilization_id = civilization.id;
        economy.credits = ancient ? 50000.0 : 500.0;
        economy.industry = ancient ? 25000.0 : 200.0;
        economy.science = ancient ? 10000.0 : 0.0;
        result.push_back(economy);
    }
    return result;
}

ColonyLaborSnapshot colony_labor(const Colony& colony, bool industrial_automation, double additional_represented_jobs_millions) {
    const double population = std::max(0.0, colony.population_millions);
    const double working_age = population * .45;
    const double infrastructure = std::clamp(colony.infrastructure, .1, 5.0);
    const double capacity_rate = std::clamp(.70 + .075 * infrastructure + (industrial_automation ? .05 : 0.0), 0.0, .98);
    const double employed = std::min(working_age, working_age * capacity_rate + std::max(0.0, additional_represented_jobs_millions));
    return {population, working_age, employed, std::max(0.0, working_age - employed), working_age <= 0.0 ? 0.0 : employed / working_age};
}

ColonySustenanceCapacity colony_sustenance_capacity(std::span<const PlanetaryBody> bodies,
    const Colony& colony, const SurfaceSustenanceCapacity& surface) {
    const double infrastructure = std::clamp(colony.infrastructure, .1, 5.0);
    const double sealed = sealed_capacity_per_infrastructure * infrastructure;
    const PlanetaryBody* body = nullptr;
    if (colony.planetary_body_id) {
        const auto found = std::find_if(bodies.begin(), bodies.end(), [&](const PlanetaryBody& candidate) {
            return candidate.id == *colony.planetary_body_id && candidate.system_id == colony.system_id;
        });
        if (found != bodies.end()) body = &*found;
    }
    double natural_food, natural_water, natural_housing;
    if (!body) {
        natural_food = natural_water = natural_housing = std::max(0.0, colony.population_millions - sealed);
    } else {
        const auto assessment = assess_species_planet(species_environment_profile(colony.population_species_id), *body);
        const double area = std::clamp(body->radius_earth * body->radius_earth, .02, 25.0);
        const double fit = std::clamp(assessment.environment.natural_habitability, 0.0, 1.0);
        const double base = natural_biosphere_capacity_per_earth_area * area * infrastructure;
        natural_food = base * fit;
        natural_water = base * fit * std::clamp(assessment.environment.solvent_suitability, 0.0, 1.0);
        natural_housing = base * fit;
    }
    const double food = sealed + natural_food + surface.food_capacity_millions;
    const double water = sealed + natural_water + surface.water_capacity_millions;
    const double housing = sealed + natural_housing + surface.housing_capacity_millions;
    const double supported = std::max(.001, std::min(food, std::min(water, housing)));
    return {natural_food, natural_water, natural_housing, surface.food_capacity_millions, surface.water_capacity_millions,
        surface.housing_capacity_millions, food, water, housing, supported,
        colony.population_millions <= 0.0 ? 1.0 : supported / colony.population_millions, limiting_name(food, water, housing)};
}

ColonySustenanceReserveSnapshot preview_colony_reserves(const Colony& colony, const ColonySustenanceCapacity& capacity, double simulation_days) {
    return calculate_reserves(colony, capacity, simulation_days).snapshot;
}

ColonySustenanceReserveSnapshot advance_colony_reserves(Colony& colony, const ColonySustenanceCapacity& capacity, double simulation_days) {
    const auto calculation = calculate_reserves(colony, capacity, simulation_days);
    colony.stored_food_population_days_millions = calculation.food;
    colony.stored_water_population_days_millions = calculation.water;
    return calculation.snapshot;
}

TreasuryHealthSnapshot assess_treasury(double balance, double net_per_day, double arrears) {
    if (!std::isfinite(balance) || balance < 0.0) throw std::out_of_range{"balance"};
    if (!std::isfinite(net_per_day)) throw std::out_of_range{"net_per_day"};
    if (!std::isfinite(arrears) || arrears < 0.0) throw std::out_of_range{"arrears"};
    if (arrears > .000001) return {TreasuryHealthState::Arrears, 0.0, balance, net_per_day};
    if (net_per_day >= 0.0) return {TreasuryHealthState::Surplus, std::numeric_limits<double>::infinity(), balance, net_per_day};
    if (balance <= .000001) return {TreasuryHealthState::Depleted, 0.0, balance, net_per_day};
    return {TreasuryHealthState::Deficit, balance / -net_per_day, balance, net_per_day};
}
} // namespace stellar::core
