#pragma once
#include <stellar/core/construction_state.hpp>
#include <limits>
#include <string>

namespace stellar::core {
inline constexpr float surface_area_half_size = 512.0F, surface_hub_radius = 24.0F;
inline constexpr double surface_industry_per_site_per_day = 30.0;
struct ConstructionCost { double credit_cost{}, industry_cost{}; };
struct SurfaceBuildingPlacementAssessment {
    bool accepted{};
    std::string message;
    int civilization_id{}, colony_id{};
    std::string type_id, building_name;
    float x{}, z{}, normalized_rotation_degrees{};
    int prepared_building_id{};
    double authorization_cost{}, industry_cost{};
    std::string formatted_authorization;
};
struct SurfaceBuildingRemovalAssessment {
    bool accepted{};
    std::string message;
    int civilization_id{}, colony_id{}, building_id{};
    std::string type_id, building_name;
    bool cancellation{};
    double refund{};
    std::string formatted_refund;
};
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
SurfaceBuildingPlacementAssessment assess_surface_building_placement(
    ConstructionReadView world, int civilization_id, int colony_id,
    std::string_view type_id, float x, float z, float rotation_degrees);
ConstructionOrderResult commit_surface_building_placement(
    ConstructionWorld world, const SurfaceBuildingPlacementAssessment& assessment);
ConstructionOrderResult place_surface_building(ConstructionWorld world, int civilization_id, int colony_id,
    std::string_view type_id, float x, float z, float rotation_degrees);
SurfaceBuildingRemovalAssessment assess_surface_building_removal(
    ConstructionReadView world, int civilization_id, int colony_id,
    int building_id);
ConstructionOrderResult commit_surface_building_removal(
    ConstructionWorld world, const SurfaceBuildingRemovalAssessment& assessment);
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
