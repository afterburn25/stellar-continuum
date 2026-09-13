#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
using Definition = SurfaceBuildingDefinition;

const std::vector<Definition>& definitions() {
    static const std::vector<Definition> catalog = [] {
        std::vector<Definition> values;
        values.reserve(14);
        const auto add = [&](std::string id, std::string name, std::string description,
                             double industry_cost, float footprint_radius) -> Definition& {
            values.emplace_back();
            auto& value = values.back();
            value.id = std::move(id);
            value.name = std::move(name);
            value.description = std::move(description);
            value.industry_cost = industry_cost;
            value.footprint_radius = footprint_radius;
            return value;
        };

        auto& power = add("power_generator", "Power generator", "+4 colony power · 20,000 workers · operating upkeep", 300, 12);
        power.power_supply = 4; power.credit_cost = 25; power.upkeep_credits_per_day = .02;
        power.upgrade_type_id = "advanced_power_generator"; power.upgrade_credit_cost = 30; power.upgrade_industry_cost = 240;
        power.workforce_required_millions = .020; power.upgrade_requirement_id = "fusion_power";
        power.upgrade_requirement_name = "Practical Fusion Power";

        auto& science = add("science_lab", "Science lab", "+1 Effective Research Lab · 50,000 workers · uses 2 power · operating upkeep", 400, 15);
        science.power_demand = 2; science.science_per_day = 1; science.credit_cost = 40; science.upkeep_credits_per_day = .04;
        science.upgrade_type_id = "advanced_science_lab"; science.upgrade_credit_cost = 50; science.upgrade_industry_cost = 320;
        science.workforce_required_millions = .050;

        auto& fabricator = add("fabricator", "Fabricator", "+1 industry/day · 40,000 workers · uses 2 power · operating upkeep", 450, 17);
        fabricator.power_demand = 2; fabricator.industry_per_day = 1; fabricator.credit_cost = 50; fabricator.upkeep_credits_per_day = .05;
        fabricator.upgrade_type_id = "advanced_fabricator"; fabricator.upgrade_credit_cost = 60; fabricator.upgrade_industry_cost = 360;
        fabricator.workforce_required_millions = .040; fabricator.upgrade_requirement_id = "additive_manufacturing";
        fabricator.upgrade_requirement_name = "Advanced Additive Manufacturing";

        auto& trade = add("trade_hub", "Trade hub", "Adds local revenue · 30,000 workers · uses 2 power · operating upkeep", 380, 15);
        trade.power_demand = 2; trade.credit_cost = 45; trade.credits_per_day = .08; trade.upkeep_credits_per_day = .03;
        trade.upgrade_type_id = "advanced_trade_hub"; trade.upgrade_credit_cost = 55; trade.upgrade_industry_cost = 300;
        trade.workforce_required_millions = .030; trade.upgrade_requirement_id = "interplanetary_trade_standards";
        trade.upgrade_requirement_name = "Interplanetary Trade Standards";

        auto& habitat = add("habitat_complex", "Habitat complex", "Reduces local life-support cost 20% · 15,000 workers · uses 2 power · operating upkeep", 350, 15);
        habitat.power_demand = 2; habitat.credit_cost = 45; habitat.upkeep_credits_per_day = .04;
        habitat.upgrade_type_id = "advanced_habitat_complex"; habitat.upgrade_credit_cost = 50; habitat.upgrade_industry_cost = 300;
        habitat.habitat_support_reduction = .20; habitat.housing_capacity_millions = 1000; habitat.workforce_required_millions = .015;
        habitat.upgrade_requirement_id = "closed_loop_recycling"; habitat.upgrade_requirement_name = "Closed-Loop Recycling";

        auto& agriculture = add("controlled_agriculture", "Controlled agriculture", "+2B food support · 35,000 workers · uses 2 power · operating upkeep", 420, 17);
        agriculture.power_demand = 2; agriculture.credit_cost = 50; agriculture.upkeep_credits_per_day = .05;
        agriculture.food_capacity_millions = 2000; agriculture.workforce_required_millions = .035;

        auto& water = add("water_reclamation", "Water reclamation", "+2B potable-water support · 25,000 workers · uses 2 power · operating upkeep", 360, 15);
        water.power_demand = 2; water.credit_cost = 40; water.upkeep_credits_per_day = .04;
        water.water_capacity_millions = 2000; water.workforce_required_millions = .025;

        auto& battery = add("grid_battery", "Grid battery complex", "Stores surplus grid energy and bridges short generation gaps · 10,000 workers · operating upkeep", 320, 14);
        battery.credit_cost = 35; battery.upkeep_credits_per_day = .025; battery.workforce_required_millions = .010;
        battery.power_storage_days = 12; battery.power_charge_rate = 4; battery.power_discharge_rate = 4;

        auto& cargo = add("cargo_terminal", "Cargo terminal", "+20 material/day port handling · 25,000 workers · uses 2 power · operating upkeep", 340, 17);
        cargo.power_demand = 2; cargo.credit_cost = 45; cargo.upkeep_credits_per_day = .04;
        cargo.workforce_required_millions = .025; cargo.cargo_transfer_capacity_per_day = 20;

        auto& advanced_power = add("advanced_power_generator", "Fusion power complex", "+8 colony power · operating upkeep", 300, 12);
        advanced_power.power_supply = 8; advanced_power.credit_cost = 55; advanced_power.upkeep_credits_per_day = .04;
        advanced_power.available_for_placement = false; advanced_power.workforce_required_millions = .035;

        auto& advanced_science = add("advanced_science_lab", "Advanced science campus", "+2.5 Effective Research Labs · uses 3 power · operating upkeep", 400, 15);
        advanced_science.power_demand = 3; advanced_science.science_per_day = 2.5; advanced_science.credit_cost = 90;
        advanced_science.upkeep_credits_per_day = .08; advanced_science.available_for_placement = false;
        advanced_science.workforce_required_millions = .080;

        auto& advanced_fabricator = add("advanced_fabricator", "Automated fabrication arcology", "+2.5 industry/day · uses 3 power · operating upkeep", 450, 17);
        advanced_fabricator.power_demand = 3; advanced_fabricator.industry_per_day = 2.5; advanced_fabricator.credit_cost = 110;
        advanced_fabricator.upkeep_credits_per_day = .10; advanced_fabricator.available_for_placement = false;
        advanced_fabricator.workforce_required_millions = .060;

        auto& advanced_trade = add("advanced_trade_hub", "Interstellar trade exchange", "Adds major local revenue · uses 3 power · operating upkeep", 380, 15);
        advanced_trade.power_demand = 3; advanced_trade.credit_cost = 100; advanced_trade.credits_per_day = .18;
        advanced_trade.upkeep_credits_per_day = .06; advanced_trade.available_for_placement = false;
        advanced_trade.workforce_required_millions = .050;

        auto& advanced_habitat = add("advanced_habitat_complex", "Closed-loop habitat arcology", "Reduces local life-support cost 40% · uses 3 power · operating upkeep", 350, 15);
        advanced_habitat.power_demand = 3; advanced_habitat.credit_cost = 95; advanced_habitat.upkeep_credits_per_day = .08;
        advanced_habitat.available_for_placement = false; advanced_habitat.habitat_support_reduction = .40;
        advanced_habitat.housing_capacity_millions = 3000; advanced_habitat.workforce_required_millions = .025;
        return values;
    }();
    return catalog;
}

bool operational(const SurfaceBuilding& building) {
    return building.is_complete && building.is_enabled && building.condition > minimum_operational_condition;
}
double efficiency(const SurfaceBuilding& building) { return .5 + .5 * building.condition; }
bool contains_id(const std::vector<int>& ids, int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
void add_unique_id(std::vector<int>& ids, int id) { if (!contains_id(ids, id)) ids.push_back(id); }
const Definition& require_definition(std::string_view type_id) {
    const auto* value = find_surface_building(type_id);
    if (!value) throw std::invalid_argument("The surface building has an unknown type.");
    return *value;
}
} // namespace

std::span<const Definition> surface_building_catalog() { return definitions(); }
const Definition* find_surface_building(std::string_view id) {
    for (const auto& definition : definitions()) if (definition.id == id) return &definition;
    return nullptr;
}
std::string_view surface_functional_family(std::string_view id) { return id.starts_with("advanced_") ? id.substr(9) : id; }
int surface_essential_service_priority(std::string_view type_id) {
    const auto family = surface_functional_family(type_id);
    if (family == "power_generator" || family == "grid_battery") return 4;
    if (family == "water_reclamation") return 3;
    if (family == "controlled_agriculture") return 2;
    if (family == "habitat_complex") return 1;
    return 0;
}
int surface_building_capacity(const Colony& colony) {
    if (colony.kind == SettlementKind::ResourceOutpost) return 8;
    if (colony.surface_hub_level == 1) return 16;
    if (colony.surface_hub_level == 2) return 32;
    return 64;
}

SurfaceConstructionStage surface_construction_stage(const SurfaceBuilding& building) {
    const auto& definition = require_definition(building.type_id);
    if (building.is_complete) return {"operational", "Operational", 1, 1, 0};
    const double overall = std::clamp(building.industry_progress / definition.industry_cost, 0.0, 1.0);
    std::string id, name; double start = 0, end = 0;
    if (overall < .15) { id = "preparation"; name = "Site preparation"; end = .15; }
    else if (overall < .40) { id = "foundations"; name = "Foundations and utilities"; start = .15; end = .40; }
    else if (overall < .75) { id = "structure"; name = "Primary structure"; start = .40; end = .75; }
    else if (overall < .95) { id = "equipment"; name = "Equipment installation"; start = .75; end = .95; }
    else { id = "commissioning"; name = "Testing and commissioning"; start = .95; end = 1; }
    return {std::move(id), std::move(name), std::clamp((overall-start)/(end-start), 0.0, 1.0), overall,
        std::max(0.0, definition.industry_cost-building.industry_progress)};
}

SurfaceColonySpecialization surface_colony_specialization(const Colony& colony) {
    struct Family { std::string_view id, name, output; };
    constexpr Family families[] = {{"science_lab","Research district","research capacity"},{"fabricator","Industrial district","industry"},
        {"trade_hub","Commercial district","trade revenue"},{"power_generator","Energy district","generator supply"}};
    const Family* selected = &families[0]; int selected_count = 0;
    for (const auto& family : families) {
        int count = 0;
        for (const auto& building : colony.surface_buildings)
            if (operational(building) && surface_functional_family(building.type_id) == family.id) ++count;
        if (count > selected_count) { selected = &family; selected_count = count; }
    }
    if (selected_count == 0) return {"general","General settlement","Complete matching complexes to develop a specialized district.",0,false};
    const bool active = selected_count >= 3;
    const auto description = active ? "Active · +25% "+std::string(selected->output)+" from the completed district."
        : "Developing · "+std::to_string(selected_count)+"/3 matching completed complexes.";
    return {std::string(selected->id),std::string(selected->name),description,selected_count,active};
}

SurfaceColonyOutput surface_colony_output(const Colony& colony, double interval) {
    if (!std::isfinite(interval) || interval <= 0) throw std::out_of_range("Power allocation requires a finite positive interval.");
    std::vector<const SurfaceBuilding*> completed;
    for (const auto& building : colony.surface_buildings) if (operational(building)) completed.push_back(&building);
    std::stable_sort(completed.begin(), completed.end(), [](const auto* left, const auto* right) {
        if (left->operating_priority != right->operating_priority) return left->operating_priority > right->operating_priority;
        const int lp = surface_essential_service_priority(left->type_id), rp = surface_essential_service_priority(right->type_id);
        return lp != rp ? lp > rp : left->id < right->id;
    });

    SurfaceColonyOutput result; result.supply = 2;
    result.workforce_available_millions = std::max(0.0, colony.population_millions * surface_workforce_participation_rate);
    double workforce_remaining = result.workforce_available_millions;
    const auto specialization = surface_colony_specialization(colony);
    for (const auto* building : completed) {
        const auto& definition = require_definition(building->type_id);
        result.workforce_demand_millions += definition.workforce_required_millions;
        result.upkeep_credits_per_day += definition.upkeep_credits_per_day;
        if (definition.workforce_required_millions > workforce_remaining + .0000001) continue;
        workforce_remaining -= definition.workforce_required_millions;
        add_unique_id(result.staffed_building_ids, building->id);
        const double bonus = specialization.active && specialization.id == "power_generator" ? 1.25 : 1;
        result.supply += definition.power_supply * efficiency(*building) * bonus;
        result.demand += definition.power_demand;
    }

    std::vector<const SurfaceBuilding*> batteries;
    for (const auto& building : colony.surface_buildings) {
        const auto* definition = find_surface_building(building.type_id);
        if (building.is_complete && definition && definition->power_storage_days > 0) {
            result.stored_power_days += building.stored_power_days;
            result.power_storage_capacity_days += definition->power_storage_days;
        }
    }
    for (const auto* building : completed) {
        const auto& definition = require_definition(building->type_id);
        if (contains_id(result.staffed_building_ids, building->id) && definition.power_storage_days > 0) batteries.push_back(building);
    }
    double discharge_capacity = 0;
    for (const auto* battery : batteries) {
        const auto& definition = require_definition(battery->type_id);
        discharge_capacity += std::min(definition.power_discharge_rate*efficiency(*battery),
            battery->stored_power_days*power_storage_efficiency/interval);
    }

    double available_power = result.supply + discharge_capacity;
    for (const auto* building : completed) {
        const auto& definition = require_definition(building->type_id);
        if (!contains_id(result.staffed_building_ids, building->id) || definition.power_demand > available_power) continue;
        available_power -= definition.power_demand; add_unique_id(result.powered_building_ids, building->id);
        const double e = efficiency(*building);
        result.science_per_day += definition.science_per_day*e; result.industry_per_day += definition.industry_per_day*e;
        result.credits_per_day += definition.credits_per_day*e; result.habitat_support_reduction += definition.habitat_support_reduction*e;
        result.food_capacity_millions += definition.food_capacity_millions*e; result.water_capacity_millions += definition.water_capacity_millions*e;
        result.housing_capacity_millions += definition.housing_capacity_millions*e;
        result.cargo_transfer_capacity_per_day += definition.cargo_transfer_capacity_per_day*e;
    }
    if (specialization.active) {
        if (specialization.id == "science_lab") result.science_per_day *= 1.25;
        if (specialization.id == "fabricator") result.industry_per_day *= 1.25;
        if (specialization.id == "trade_hub") result.credits_per_day *= 1.25;
    }
    result.habitat_support_reduction = std::min(.75, result.habitat_support_reduction);
    double consumed_power = 0;
    for (const auto* building : completed) if (contains_id(result.powered_building_ids, building->id))
        consumed_power += require_definition(building->type_id).power_demand;
    result.storage_discharge_per_day = std::min(discharge_capacity, std::max(0.0, consumed_power-result.supply));
    double charge_limit = 0;
    for (const auto* battery : batteries) {
        const auto& definition = require_definition(battery->type_id);
        const double remaining = std::max(0.0, definition.power_storage_days-battery->stored_power_days);
        charge_limit += std::min(definition.power_charge_rate*efficiency(*battery), remaining/(power_storage_efficiency*interval));
    }
    result.storage_charge_per_day = std::min(std::max(0.0, result.supply-consumed_power), charge_limit);
    return result;
}

void advance_surface_power_storage(Colony& colony, const SurfaceColonyOutput& output, double days) {
    if (!std::isfinite(days) || days < 0) throw std::out_of_range("Power storage requires finite nonnegative elapsed days.");
    if (days <= 0) return;
    std::vector<SurfaceBuilding*> batteries;
    for (auto& building : colony.surface_buildings) {
        const auto* definition = find_surface_building(building.type_id);
        if (operational(building) && contains_id(output.staffed_building_ids,building.id) && definition && definition->power_storage_days > 0)
            batteries.push_back(&building);
    }
    std::stable_sort(batteries.begin(),batteries.end(),[](const auto* left,const auto* right){return left->id<right->id;});
    double discharge = output.storage_discharge_per_day*days/power_storage_efficiency;
    for (auto* battery : batteries) { const double amount=std::min(battery->stored_power_days,discharge); battery->stored_power_days-=amount; discharge-=amount; }
    double charge = output.storage_charge_per_day*days*power_storage_efficiency;
    for (auto* battery : batteries) { const double capacity=require_definition(battery->type_id).power_storage_days;
        const double amount=std::min(std::max(0.0,capacity-battery->stored_power_days),charge); battery->stored_power_days+=amount; charge-=amount; }
}

SurfaceSustenanceCapacity surface_sustenance_projection(const SurfaceColonyOutput& output) {
    return {output.food_capacity_millions,output.water_capacity_millions,output.housing_capacity_millions};
}
} // namespace stellar::core
