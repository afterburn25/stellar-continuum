#pragma once
#include <stellar/core/construction_state.hpp>
#include <stellar/core/fleet_role.hpp>
#include <functional>
#include <string_view>

namespace stellar::core {
struct ShipDesignPrerequisites {
    std::vector<std::string> all_civilization_capabilities;
    std::vector<std::string> any_civilization_capabilities;
    std::vector<std::string> required_construction_projects;
};
struct ShipDesignDefinition {
    std::string id, name, description;
    FleetRole role{};
    double industry_cost{}, strategic_speed{}, maximum_leg_range_light_years{}, fuel_endurance_light_years{};
    float sensor_range{};
    ShipDesignPrerequisites prerequisites;
    double population_cost_millions{};
    std::optional<std::string> combat_profile_id;
    int crew_complement_individuals{};
    double credit_cost{}, cargo_material_capacity{}, cargo_transfer_rate_per_day{};
};
struct ShipbuildingCapabilities {
    int civilization_id{};
    std::vector<std::string> capability_ids;
};
struct ShipDesignReadView {
    std::span<const ConstructionState> construction;
    std::span<const ShipbuildingCapabilities> capabilities;
    std::function<bool(int, std::string_view)> capability_query;
};
struct ShipPropulsionPerformance {
    double strategic_speed{}, maximum_leg_range_light_years{}, fuel_endurance_light_years{};
    std::string propulsion_generation;
};
std::span<const ShipDesignDefinition> ship_design_catalog();
const ShipDesignDefinition* find_ship_design(std::string_view id);
const ShipDesignDefinition& get_ship_design(std::string_view id);
const ShipDesignDefinition& ship_design_for_role(FleetRole role);
// Explicit read adapter for C# GetForFleet; does not pretend to be a complete fleet state.
const ShipDesignDefinition& resolve_fleet_ship_design(std::optional<std::string_view> design_id, FleetRole role);
bool shipbuilding_has_capability(ShipDesignReadView world, int civilization_id, std::string_view capability_id);
std::string shipbuilding_capability_display_name(std::string_view capability_id);
std::optional<std::string> ship_design_lock_reason(ShipDesignReadView world, int civilization_id,
    const ShipDesignDefinition& design);
std::vector<ShipDesignDefinition> available_ship_designs(ShipDesignReadView world, int civilization_id);
ShipPropulsionPerformance effective_ship_propulsion(ShipDesignReadView world, int civilization_id,
    const ShipDesignDefinition& design);
}
