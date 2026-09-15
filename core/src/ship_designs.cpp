#include <stellar/core/ship_designs.hpp>
#include <stellar/core/construction_projects.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace stellar::core {
namespace {
const std::array<ShipDesignDefinition,6> designs{{
{"warp_scout","Pathfinder Scout","Fast first-generation interstellar survey ship optimized for rapid reconnaissance.",FleetRole::Scout,650,24,420,1200,140,{ {"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},0,{},24,70},
{"science_vessel","Deep-Space Science Vessel","Long-range research platform with enhanced sensors for anomalies and unusual systems.",FleetRole::Science,850,18,400,1100,185,{{"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},0,{},72,100},
{"patrol_corvette","Patrol Corvette","First-generation armed patrol and escort vessel for local defense and early fleet combat.",FleetRole::Military,1000,21,340,800,125,{{"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},0,"patrol_corvette_mk1",85,120},
{"colony_ship","Interstellar Colony Ship","Large settlement vessel carrying industrial seed equipment and a founding population.",FleetRole::Colony,1500,13.5,300,750,80,{{"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},250,{},320,180},
{"resource_outpost_ship","Sealed Resource Outpost Vessel","Carries a compact pressure-sealed habitat, extraction equipment, and a permanent specialist crew for valuable harsh worlds.",FleetRole::Colony,950,16,330,900,95,{{"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},8,{},180,130},
{"bulk_freighter","Interstellar Bulk Freighter","Early freight vessel that collects processed outpost material and returns it to a developed colony.",FleetRole::Logistics,800,17,350,1000,85,{{"spacecraft_construction","experimental_interstellar_transit"},{},{"orbital_shipyard"}},0,{},60,90,100,20}}};
const ConstructionState& construction_for(std::span<const ConstructionState> states, int civilization_id) {
    const auto state = std::find_if(states.begin(), states.end(), [=](const auto& item) {
        return item.civilization_id == civilization_id;
    });
    if (state == states.end()) throw std::out_of_range("Sequence contains no matching element");
    return *state;
}
bool completed(const ConstructionState& state, std::string_view id) {
    return std::find(state.completed_project_ids.begin(), state.completed_project_ids.end(), id) != state.completed_project_ids.end();
}
} // namespace
std::span<const ShipDesignDefinition> ship_design_catalog() { return designs; }
const ShipDesignDefinition* find_ship_design(std::string_view id) {
    const auto item=std::find_if(designs.begin(),designs.end(),[=](const auto& design){ return design.id==id; });
    return item==designs.end()?nullptr:&*item;
}
const ShipDesignDefinition& get_ship_design(std::string_view id) {
    const auto* design=find_ship_design(id);
    if(!design) throw std::out_of_range("Sequence contains no matching element");
    return *design;
}
const ShipDesignDefinition& ship_design_for_role(FleetRole role) {
    switch(role) {
    case FleetRole::Scout: return get_ship_design("warp_scout");
    case FleetRole::Science: return get_ship_design("science_vessel");
    case FleetRole::Colony: return get_ship_design("colony_ship");
    case FleetRole::Military: return get_ship_design("patrol_corvette");
    case FleetRole::Logistics: return get_ship_design("bulk_freighter");
    }
    throw std::invalid_argument("No baseline ship design is registered for fleet role "+std::to_string(static_cast<int>(role))+".");
}
const ShipDesignDefinition& resolve_fleet_ship_design(std::optional<std::string_view> id, FleetRole role) {
    if(id) if(const auto* design=find_ship_design(*id); design && design->role==role) return *design;
    return ship_design_for_role(role);
}
bool shipbuilding_has_capability(ShipDesignReadView world, int civilization_id, std::string_view id) {
    if(world.capability_query) return world.capability_query(civilization_id,id);
    const auto state=std::find_if(world.capabilities.begin(),world.capabilities.end(),[=](const auto& item){ return item.civilization_id==civilization_id; });
    return state!=world.capabilities.end() && std::find(state->capability_ids.begin(),state->capability_ids.end(),id)!=state->capability_ids.end();
}
std::string shipbuilding_capability_display_name(std::string_view id) {
    if(id=="spacecraft_construction") return "Spacecraft Construction";
    if(id=="experimental_interstellar_transit") return "Experimental Interstellar Transit";
    if(id=="reliable_ftl") return "Reliable Interstellar Transit";
    if(id=="extended_ftl_range") return "Extended Interstellar Transit";
    std::string display{id}; std::replace(display.begin(),display.end(),'_',' '); return display;
}
std::optional<std::string> ship_design_lock_reason(ShipDesignReadView world, int civilization_id, const ShipDesignDefinition& design) {
    const auto& state=construction_for(world.construction,civilization_id);
    std::vector<std::string> missing;
    for(const auto& id:design.prerequisites.all_civilization_capabilities)
        if(!shipbuilding_has_capability(world,civilization_id,id)) missing.push_back(shipbuilding_capability_display_name(id));
    for(const auto& id:design.prerequisites.required_construction_projects)
        if(!completed(state,id)) missing.push_back(get_construction_project(id).name);
    if(!design.prerequisites.any_civilization_capabilities.empty() && !std::any_of(design.prerequisites.any_civilization_capabilities.begin(),design.prerequisites.any_civilization_capabilities.end(),[&](const auto& id){ return shipbuilding_has_capability(world,civilization_id,id); })) {
        std::string group="one of ";
        for(size_t i=0;i<design.prerequisites.any_civilization_capabilities.size();++i) { if(i) group+=" or "; group+=shipbuilding_capability_display_name(design.prerequisites.any_civilization_capabilities[i]); }
        missing.push_back(std::move(group));
    }
    if(missing.empty()) return std::nullopt;
    std::string result="requires ";
    for(size_t i=0;i<missing.size();++i) { if(i) result+=", "; result+=missing[i]; }
    return result;
}
std::vector<ShipDesignDefinition> available_ship_designs(ShipDesignReadView world, int civilization_id) {
    std::vector<ShipDesignDefinition> available;
    for(const auto& design:designs) if(!ship_design_lock_reason(world,civilization_id,design)) available.push_back(design);
    return available;
}
ShipPropulsionPerformance effective_ship_propulsion(ShipDesignReadView world, int civilization_id, const ShipDesignDefinition& design) {
    ShipPropulsionPerformance result{design.strategic_speed,design.maximum_leg_range_light_years,design.fuel_endurance_light_years,"Prototype warp drive"};
    if(shipbuilding_has_capability(world,civilization_id,"extended_ftl_range")) { result.strategic_speed*=1.35; result.maximum_leg_range_light_years*=1.75; result.fuel_endurance_light_years*=1.75; result.propulsion_generation="Long-range warp architecture"; }
    else if(shipbuilding_has_capability(world,civilization_id,"reliable_ftl")) { result.strategic_speed*=1.18; result.maximum_leg_range_light_years*=1.30; result.fuel_endurance_light_years*=1.35; result.propulsion_generation="Stable warp drive"; }
    return result;
}
} // namespace stellar::core
