#pragma once
#include <stellar/core/colony_biology.hpp>
#include <stellar/core/surface_economy.hpp>

namespace stellar::core {
// Explicit read-only economic projections, not replacement full fleet/construction state.
enum class EconomyFleetRole { Scout, Science, Colony, Military, Logistics };
struct EconomyFleetState { int civilization_id{}; EconomyFleetRole role{}; bool is_active{true}; };
struct EconomyConstructionState { int civilization_id{}; std::vector<std::string> completed_project_ids; };
struct ConstructionEconomicProfile { std::string id; double industry_per_day{}, upkeep_credits_per_day{}; };
std::span<const ConstructionEconomicProfile> construction_economic_profiles();
std::vector<EconomyConstructionState> seed_economic_construction(std::span<const Civilization> civilizations);
struct EconomyWorldView {
    std::span<const Civilization> civilizations;
    std::span<const PlanetaryBody> bodies;
    std::span<const EconomyConstructionState> construction;
    std::span<const EconomyFleetState> fleets;
};
struct CreditFlowSnapshot {
    double colony_revenue_per_day{}, trade_revenue_per_day{}, colony_administration_per_day{}, population_services_per_day{};
    double habitat_support_per_day{}, fleet_operations_per_day{}, orbital_maintenance_per_day{}, surface_maintenance_per_day{}, research_operations_per_day{};
    double gross_income_per_day{}, operating_costs_per_day{}, net_credits_per_day{};
};
CreditFlowSnapshot economy_credit_flow(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id,
    bool include_research_operations = true, double power_interval_days = 1.0);
double colony_administration_cost(double population_millions);
double habitat_support_cost(const ColonyHabitatSupportBurden& burden);
double fleet_operating_cost(EconomyFleetRole role);
double industry_storage_capacity(EconomyWorldView world, std::span<const Colony> colonies, int civilization_id);
struct IndustryReserve { int civilization_id{}; double industry{}; };
void apply_industry_storage_caps(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<CivilizationEconomy> economies, std::span<const IndustryReserve> existing_reserves = {});
void advance_colony_economies(EconomyWorldView world, std::span<Colony> colonies,
    std::span<CivilizationEconomy> economies, double simulation_days, bool accrue_legacy_science = true);
} // namespace stellar::core
