#pragma once
#include <stellar/core/construction_state.hpp>
#include <limits>

namespace stellar::core {
inline constexpr float surface_area_half_size = 512.0F, surface_hub_radius = 24.0F;
inline constexpr double surface_industry_per_site_per_day = 30.0;
struct ConstructionCost { double credit_cost{}, industry_cost{}; };
double surface_construction_cost_multiplier(ConstructionReadView world, const Colony& colony);
double surface_authorization_cost(ConstructionReadView world, const Colony& colony, const SurfaceBuildingDefinition& definition);
double surface_upgrade_authorization_cost(ConstructionReadView world, const Colony& colony, const SurfaceBuildingDefinition& definition);
std::optional<ConstructionCost> surface_hub_upgrade_cost(ConstructionReadView world, const Colony& colony);
std::optional<std::string> surface_building_upgrade_lock_reason(ConstructionReadView world, int civilization_id,
    const SurfaceBuildingDefinition& definition);
std::optional<std::string> surface_hub_upgrade_lock_reason(ConstructionReadView world, int civilization_id, const Colony& colony);
bool surface_available_for_settlement(const Colony& colony, const SurfaceBuildingDefinition& definition);
float surface_terrain_height(float x, float z);
std::optional<std::string> surface_placement_error(std::span<const SurfaceBuilding> buildings,
    std::string_view type_id, float x, float z, float rotation_degrees);
ConstructionOrderResult place_surface_building(ConstructionWorld world, int civilization_id, int colony_id,
    std::string_view type_id, float x, float z, float rotation_degrees);
ConstructionOrderResult remove_surface_building(ConstructionWorld world, int civilization_id, int colony_id, int building_id);
ConstructionOrderResult upgrade_surface_building(ConstructionWorld world, int civilization_id, int colony_id, int building_id);
ConstructionOrderResult upgrade_surface_hub(ConstructionWorld world, int civilization_id, int colony_id);
double surface_repair_industry_cost(const SurfaceBuilding& building);
ConstructionOrderResult repair_surface_building(ConstructionWorld world, int civilization_id, int colony_id, int building_id);
ConstructionOrderResult set_surface_building_enabled(ConstructionWorld world, int civilization_id, int colony_id, int building_id, bool enabled);
ConstructionOrderResult set_surface_building_priority(ConstructionWorld world, int civilization_id, int colony_id, int building_id, bool prioritized);
double surface_construction_industry_demand(ConstructionReadView world, int civilization_id,
    double simulation_days = std::numeric_limits<double>::infinity());
void advance_surface_construction(ConstructionWorld world, int civilization_id, double budget, double simulation_days = 1.0);
void validate_surface_construction(const Colony& colony);
} // namespace stellar::core
