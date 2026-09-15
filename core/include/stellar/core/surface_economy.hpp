#pragma once
#include <stellar/core/colony_economy.hpp>
#include <string_view>

namespace stellar::core {
struct SurfaceBuildingDefinition {
    std::string id, name, description;
    double industry_cost{};
    float footprint_radius{};
    double power_supply{}, power_demand{}, science_per_day{}, industry_per_day{};
    double credit_cost{}, credits_per_day{}, upkeep_credits_per_day{};
    bool available_for_placement{true};
    std::optional<std::string> upgrade_type_id;
    double upgrade_credit_cost{}, upgrade_industry_cost{}, habitat_support_reduction{};
    double food_capacity_millions{}, water_capacity_millions{}, housing_capacity_millions{};
    double workforce_required_millions{};
    std::optional<std::string> upgrade_requirement_id, upgrade_requirement_name;
    double power_storage_days{}, power_charge_rate{}, power_discharge_rate{};
    double cargo_transfer_capacity_per_day{};
};
std::span<const SurfaceBuildingDefinition> surface_building_catalog();
const SurfaceBuildingDefinition* find_surface_building(std::string_view id);
std::string_view surface_functional_family(std::string_view id);
int surface_essential_service_priority(std::string_view type_id);
int surface_building_capacity(const Colony& colony);
inline constexpr double surface_workforce_participation_rate = .45;
inline constexpr double minimum_operational_condition = .15;
inline constexpr double power_storage_efficiency = .90;

struct SurfaceColonyOutput {
    double supply{}, demand{}, science_per_day{}, industry_per_day{}, credits_per_day{}, upkeep_credits_per_day{};
    // Set semantics; stored in the stable allocation order for deterministic native diagnostics.
    std::vector<int> powered_building_ids;
    double habitat_support_reduction{}, food_capacity_millions{}, water_capacity_millions{}, housing_capacity_millions{};
    double workforce_available_millions{}, workforce_demand_millions{};
    std::vector<int> staffed_building_ids;
    double stored_power_days{}, power_storage_capacity_days{}, storage_charge_per_day{}, storage_discharge_per_day{};
    double cargo_transfer_capacity_per_day{};
};
struct SurfaceColonySpecialization {
    std::string id, name, description;
    int completed_complexes{};
    bool active{};
};
struct SurfaceConstructionStage {
    std::string id, name;
    double phase_progress{}, overall_progress{}, remaining_materials{};
};
SurfaceColonyOutput surface_colony_output(const Colony& colony, double power_interval_days = 1.0);
void advance_surface_power_storage(Colony& colony, const SurfaceColonyOutput& output, double simulation_days);
SurfaceColonySpecialization surface_colony_specialization(const Colony& colony);
SurfaceConstructionStage surface_construction_stage(const SurfaceBuilding& building);
SurfaceSustenanceCapacity surface_sustenance_projection(const SurfaceColonyOutput& output);
} // namespace stellar::core
