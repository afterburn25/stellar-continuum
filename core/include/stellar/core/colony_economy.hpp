#pragma once
#include <stellar/core/civilization_catalog.hpp>

namespace stellar::core {
enum class SettlementKind { Colony, ResourceOutpost };
struct SurfaceBuilding {
    int id{}; std::string type_id; float x{},z{},rotation_degrees{};
    double industry_progress{}; bool is_complete{},is_enabled{true};
    std::optional<std::string> pending_upgrade_type_id;
    double upgrade_days_remaining{}; int operating_priority{}; double condition{1.0},stored_power_days{};
};
struct Colony {
    int id{},civilization_id{},system_id{}; std::optional<int> planetary_body_id;
    std::string name; SettlementKind kind{SettlementKind::Colony};
    std::string population_species_id{"terran_baseline"};
    double population_millions{},infrastructure{1.0},stability{1.0};
    double stored_food_population_days_millions{},stored_water_population_days_millions{},stored_extracted_materials{};
    std::optional<double> remaining_extractable_materials;
    int surface_hub_level{1}; double surface_hub_upgrade_days_remaining{};
    std::vector<SurfaceBuilding> surface_buildings;
};
enum class IndustryPriority { Balanced, InfrastructureFirst, ShipbuildingFirst };
struct CivilizationEconomy {
    int civilization_id{}; double credits{500.0},industry{200.0},science{};
    double last_credits_per_second{},last_industry_per_second{},last_science_per_second{},last_research_spending_per_day{};
    double last_research_funding_fraction{1.0},operating_arrears{},last_base_operations_funding_fraction{1.0};
    std::optional<IndustryPriority> industry_priority;
};
inline constexpr double maximum_food_reserve_days=30.0,maximum_water_reserve_days=7.0;
std::vector<Colony> seed_colonies(std::span<const Civilization> civilizations,std::span<const PlanetaryBody> bodies);
std::vector<Colony> seed_legacy_colonies(std::span<const Civilization> civilizations);
std::vector<CivilizationEconomy> seed_economies(std::span<const Civilization> civilizations);
struct ColonyLaborSnapshot {
    double population_millions{},working_age_population_millions{},employed_population_millions{},unemployed_population_millions{},employment_rate{};
};
ColonyLaborSnapshot colony_labor(const Colony& colony,bool industrial_automation=false,double additional_represented_jobs_millions=0.0);
// Explicit projection of the existing surface output; no native building allocator is implied.
struct SurfaceSustenanceCapacity { double food_capacity_millions{},water_capacity_millions{},housing_capacity_millions{}; };
struct ColonySustenanceCapacity {
    double natural_food_capacity_millions{},natural_water_capacity_millions{},natural_housing_capacity_millions{};
    double built_food_capacity_millions{},built_water_capacity_millions{},built_housing_capacity_millions{};
    double food_capacity_millions{},water_capacity_millions{},housing_capacity_millions{};
    double supported_population_millions{},support_ratio{}; std::string limiting_supply;
};
ColonySustenanceCapacity colony_sustenance_capacity(std::span<const PlanetaryBody> bodies,
    const Colony& colony,const SurfaceSustenanceCapacity& surface);
struct ColonySustenanceReserveSnapshot {
    double food_reserve_days{},water_reserve_days{},effective_support_ratio{}; std::string limiting_supply;
};
ColonySustenanceReserveSnapshot preview_colony_reserves(const Colony& colony,const ColonySustenanceCapacity& capacity,double simulation_days);
ColonySustenanceReserveSnapshot advance_colony_reserves(Colony& colony,const ColonySustenanceCapacity& capacity,double simulation_days);
enum class TreasuryHealthState { Surplus, Deficit, Depleted, Arrears };
struct TreasuryHealthSnapshot { TreasuryHealthState state{}; double runway_days{},balance{},net_per_day{}; };
TreasuryHealthSnapshot assess_treasury(double balance,double net_per_day,double arrears=0.0);
}
