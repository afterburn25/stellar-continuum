#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/colony_operations.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr double base_industry_storage = 500.0;
constexpr double industry_storage_per_infrastructure = 500.0;
constexpr double employment_tax_per_billion_workers = .75 / (.45 * .775);
constexpr double population_services_per_billion = .50;

const CivilizationEconomy& economy_for(std::span<const CivilizationEconomy> economies, int id) {
    const auto found = std::find_if(economies.begin(), economies.end(), [=](const auto& value) { return value.civilization_id == id; });
    if (found == economies.end()) throw std::out_of_range("Civilization economy is unavailable.");
    return *found;
}
const EconomyConstructionState& construction_for(std::span<const EconomyConstructionState> states, int id) {
    const auto found = std::find_if(states.begin(), states.end(), [=](const auto& value) { return value.civilization_id == id; });
    if (found == states.end()) throw std::out_of_range("Construction state is unavailable.");
    return *found;
}
bool completed(const EconomyConstructionState& state, std::string_view id) {
    return std::find(state.completed_project_ids.begin(), state.completed_project_ids.end(), id) != state.completed_project_ids.end();
}
std::vector<std::string_view> unique_projects(const EconomyConstructionState& state) {
    std::vector<std::string_view> result;
    for (const auto& id : state.completed_project_ids)
        if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
    return result;
}
const Civilization* civilization_for(std::span<const Civilization> civilizations, int id) {
    const auto found = std::find_if(civilizations.begin(), civilizations.end(), [=](const auto& value) { return value.id == id; });
    return found == civilizations.end() ? nullptr : &*found;
}
}

std::span<const ConstructionEconomicProfile> construction_economic_profiles() {
    static const std::array<ConstructionEconomicProfile, 6> profiles{{
        {"research_network", 0, 0}, {"industrial_automation", 0, 0},
        {"orbital_launch_complex", 0, .08}, {"orbital_shipyard", 0, .12},
        {"asteroid_resource_network", 1.50, .18}, {"warp_test_facility", 0, .15}}};
    return profiles;
}
std::vector<EconomyConstructionState> seed_economic_construction(std::span<const Civilization> civilizations) {
    std::vector<EconomyConstructionState> result; result.reserve(civilizations.size());
    for (const auto& civilization : civilizations) {
        EconomyConstructionState state; state.civilization_id = civilization.id;
        if (civilization.is_seeded_ancient) for (const auto& profile : construction_economic_profiles()) state.completed_project_ids.push_back(profile.id);
        result.push_back(std::move(state));
    }
    return result;
}
double colony_administration_cost(double population) { return std::clamp(population / 250.0, .12, 1.0); }
double habitat_support_cost(const ColonyHabitatSupportBurden& burden) {
    return (burden.environment ? burden.environment->required_mitigation_categories : 0) * .015 * std::max(1.0, std::sqrt(burden.population_millions));
}
double fleet_operating_cost(EconomyFleetRole role) {
    switch (role) { case EconomyFleetRole::Scout: return .08; case EconomyFleetRole::Science: return .12; case EconomyFleetRole::Colony: return .16; case EconomyFleetRole::Military: return .35; case EconomyFleetRole::Logistics: return .14; } return .12;
}
CreditFlowSnapshot economy_credit_flow(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id, bool include_research, double power_days) {
    const auto& construction = construction_for(world.construction, civilization_id);
    CreditFlowSnapshot result;
    const bool automation = completed(construction, "industrial_automation");
    for (const auto& colony : colonies) {
        if (colony.civilization_id != civilization_id) continue;
        const auto population_factor = std::max(.01, colony.population_millions / 1000.0);
        const auto infrastructure = std::clamp(colony.infrastructure, .1, 5.0);
        const auto stability = std::clamp(colony.stability, .1, 1.2);
        const auto surface = surface_colony_output(colony, power_days);
        if (colony.kind == SettlementKind::Colony) {
            const auto labor = colony_labor(colony, automation, std::min(surface.workforce_available_millions, surface.workforce_demand_millions));
            result.colony_revenue_per_day += labor.employed_population_millions / 1000.0 * employment_tax_per_billion_workers * infrastructure * stability;
            result.trade_revenue_per_day += surface.credits_per_day;
        }
        result.surface_maintenance_per_day += surface.upkeep_credits_per_day;
        result.colony_administration_per_day += colony_administration_cost(colony.population_millions);
        result.population_services_per_day += population_factor * population_services_per_billion * infrastructure;
        result.habitat_support_per_day += habitat_support_cost(colony_habitat_support(colony, world.bodies)) * (1.0 - surface.habitat_support_reduction);
    }
    for (const auto& fleet : world.fleets) if (fleet.is_active && fleet.civilization_id == civilization_id) result.fleet_operations_per_day += fleet_operating_cost(fleet.role);
    for (const auto& id : unique_projects(construction)) for (const auto& profile : construction_economic_profiles()) if (profile.id == id) result.orbital_maintenance_per_day += profile.upkeep_credits_per_day;
    result.research_operations_per_day = include_research ? economy_for(economies, civilization_id).last_research_spending_per_day : 0;
    result.gross_income_per_day = result.colony_revenue_per_day + result.trade_revenue_per_day;
    result.operating_costs_per_day = result.colony_administration_per_day + result.population_services_per_day + result.habitat_support_per_day + result.fleet_operations_per_day + result.orbital_maintenance_per_day + result.surface_maintenance_per_day + result.research_operations_per_day;
    result.net_credits_per_day = result.gross_income_per_day - result.operating_costs_per_day;
    return result;
}
double industry_storage_capacity(EconomyWorldView world, std::span<const Colony> colonies, int id) {
    const auto* civilization = civilization_for(world.civilizations, id); if (!civilization) throw std::out_of_range("Civilization is unavailable.");
    if (civilization->is_seeded_ancient) return 50000.0;
    double result = base_industry_storage;
    for (const auto& colony : colonies) if (colony.civilization_id == id) result += industry_storage_per_infrastructure * std::clamp(colony.infrastructure, .1, 5.0);
    const auto& construction = construction_for(world.construction, id);
    if (completed(construction,"industrial_automation")) result += 500;
    if (completed(construction,"orbital_launch_complex")) result += 250;
    if (completed(construction,"orbital_shipyard")) result += 500;
    if (completed(construction,"asteroid_resource_network")) result += 1000;
    return result;
}
void apply_industry_storage_caps(EconomyWorldView world, std::span<const Colony> colonies, std::span<CivilizationEconomy> economies, std::span<const IndustryReserve> reserves) {
    for (auto& economy : economies) {
        double cap = industry_storage_capacity(world, colonies, economy.civilization_id);
        bool seen = false;
        for (const auto& reserve : reserves) if (reserve.civilization_id == economy.civilization_id) {
            if (seen) throw std::invalid_argument("Industry reserves must have unique civilization IDs.");
            cap = std::max(cap, reserve.industry); seen = true;
        }
        if (economy.industry > cap) economy.industry = cap;
    }
}
void advance_colony_economies(EconomyWorldView world, std::span<Colony> colonies, std::span<CivilizationEconomy> economies, double days, bool accrue_science) {
    if (days <= 0) return;

    for (auto& economy : economies) {
        const auto flow = economy_credit_flow(world, colonies, economies, economy.civilization_id, false, days);
        const double opening_arrears = std::max(0.0, economy.operating_arrears);
        const double available_funds = std::max(0.0, economy.credits) + flow.gross_income_per_day * days;
        const double current_operating_obligations = flow.operating_costs_per_day * days;
        const double total_obligations = opening_arrears + current_operating_obligations;
        const double paid = std::min(available_funds, total_obligations);

        economy.credits = std::max(0.0, available_funds - paid);
        economy.operating_arrears = std::max(0.0, total_obligations - paid);
        const double paid_toward_current_operations = std::max(0.0, paid - opening_arrears);
        const double operating_funding_fraction = current_operating_obligations <= .0000001
            ? 1.0
            : std::clamp(paid_toward_current_operations / current_operating_obligations, 0.0, 1.0);
        economy.last_base_operations_funding_fraction = operating_funding_fraction;

        double industry_per_day = 0.0;
        double science_per_day = 0.0;
        for (auto& colony : colonies) {
            if (colony.civilization_id != economy.civilization_id) continue;

            const double population_factor = std::max(.01, colony.population_millions / 1000.0);
            const double infrastructure = std::clamp(colony.infrastructure, .1, 5.0);
            const double stability = std::clamp(colony.stability, .1, 1.2);
            const auto demographic = colony_population_turnover(colony, world.bodies);

            advance_surface_condition(world.bodies, colony, operating_funding_fraction, days);
            const auto surface = surface_colony_output(colony, days);
            advance_surface_power_storage(colony, surface, days);
            if (colony.kind == SettlementKind::Colony) {
                industry_per_day += population_factor * .42 * infrastructure * stability;
                science_per_day += population_factor * .25 * infrastructure * stability;
                industry_per_day += surface.industry_per_day;
            }
            science_per_day += surface.science_per_day;
            advance_resource_outpost(world.bodies, economies, colony, days, operating_funding_fraction);

            if (colony.kind == SettlementKind::Colony) {
                const auto sustenance = colony_sustenance_capacity(
                    world.bodies, colony, surface_sustenance_projection(surface));
                const auto reserves = advance_colony_reserves(colony, sustenance, days);
                const double population_rate = reserves.effective_support_ratio >= 1.0
                    ? .000055 * stability * demographic.effective_growth_pace_factor *
                        std::clamp(1.0 - (1.0 / sustenance.support_ratio), 0.0, 1.0)
                    : -.00040 * std::clamp(1.0 - reserves.effective_support_ratio, 0.0, 1.0);
                colony.population_millions *= std::exp(population_rate * days);
            }
        }

        const auto& construction = construction_for(world.construction, economy.civilization_id);
        if (completed(construction, "industrial_automation")) industry_per_day *= 1.35;
        for (const auto& id : unique_projects(construction)) {
            for (const auto& profile : construction_economic_profiles()) {
                if (profile.id == id) industry_per_day += profile.industry_per_day;
            }
        }
        if (completed(construction, "research_network")) science_per_day *= 1.30;

        industry_per_day *= operating_funding_fraction;
        science_per_day *= operating_funding_fraction;

        const double net_credits_per_day = flow.net_credits_per_day;
        economy.industry += industry_per_day * days;
        if (accrue_science) economy.science += science_per_day * days;
        economy.last_credits_per_second = net_credits_per_day;
        economy.last_industry_per_second = industry_per_day;
        economy.last_science_per_second = accrue_science ? science_per_day : 0.0;
    }
}
} // namespace stellar::core
