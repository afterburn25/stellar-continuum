#include <stellar/core/logistics.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace stellar::core {
namespace {
constexpr double minimum_population_scale = .001;

SupplyCondition classify(double coverage) {
    return coverage < .70 ? SupplyCondition::Critical : coverage < .95 ? SupplyCondition::Strained : SupplyCondition::Healthy;
}

const Civilization& civilization(std::span<const Civilization> civilizations, int id) {
    const auto it = std::find_if(civilizations.begin(), civilizations.end(), [=](const auto& item) { return item.id == id; });
    if (it == civilizations.end()) throw std::invalid_argument("Unknown civilization " + std::to_string(id) + ".");
    return *it;
}

const CivilizationEconomy& economy(std::span<const CivilizationEconomy> economies, int id) {
    const auto it = std::find_if(economies.begin(), economies.end(), [=](const auto& item) { return item.civilization_id == id; });
    if (it == economies.end()) throw std::invalid_argument("Civilization " + std::to_string(id) + " has no economy state.");
    return *it;
}

const EconomyConstructionState& construction(EconomyWorldView world, int id) {
    const auto it = std::find_if(world.construction.begin(), world.construction.end(), [=](const auto& item) { return item.civilization_id == id; });
    if (it == world.construction.end()) throw std::invalid_argument("Civilization " + std::to_string(id) + " has no construction state.");
    return *it;
}

bool completed(const EconomyConstructionState& state, std::string_view id) {
    return std::find(state.completed_project_ids.begin(), state.completed_project_ids.end(), id) != state.completed_project_ids.end();
}

int priority(SupplyCondition condition) {
    return condition == SupplyCondition::Critical ? 100 : condition == SupplyCondition::Strained ? 50 : 10;
}
}

CivilizationLogisticsSnapshot economy_logistics(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id) {
    const auto& state = economy(economies, civilization_id);
    const auto& projects = construction(world, civilization_id);
    CivilizationLogisticsSnapshot result; result.civilization_id = civilization_id;
    for (const auto& colony : colonies) {
        if (colony.civilization_id != civilization_id) continue;
        const double population = std::max(minimum_population_scale, colony.population_millions / 1000.0);
        const double infrastructure = std::clamp(colony.infrastructure, .10, 5.0);
        const double stability = std::clamp(colony.stability, .10, 1.20);
        const double demand = population * (.95 + .30 / infrastructure);
        const double local = population * .72 * infrastructure * stability;
        const double imports = std::max(0.0, demand - local);
        const double coverage = demand <= 0 ? 1.0 : std::clamp(local / demand, 0.0, 2.0);
        result.total_support_demand_per_day += demand;
        result.total_local_support_capacity_per_day += local;
        result.import_requirement_per_day += imports;
        result.colonies.push_back({colony.id, colony.system_id, demand, local, imports, coverage, classify(coverage)});
    }
    result.cargo_handling_capacity_per_day = std::max(.10, state.last_industry_per_second * 1.35);
    if (completed(projects, "orbital_launch_complex")) result.cargo_handling_capacity_per_day *= 1.45;
    if (completed(projects, "industrial_automation")) result.cargo_handling_capacity_per_day *= 1.20;
    const double covered = std::min(result.import_requirement_per_day, result.cargo_handling_capacity_per_day);
    result.effective_coverage_ratio = result.total_support_demand_per_day <= 0 ? 1.0 :
        std::clamp((result.total_local_support_capacity_per_day + covered) / result.total_support_demand_per_day, 0.0, 2.0);
    result.condition = classify(result.effective_coverage_ratio);
    result.critical_colony_count = static_cast<int>(std::count_if(result.colonies.begin(), result.colonies.end(), [](const auto& item) { return item.condition == SupplyCondition::Critical; }));
    result.strained_colony_count = static_cast<int>(std::count_if(result.colonies.begin(), result.colonies.end(), [](const auto& item) { return item.condition == SupplyCondition::Strained; }));
    return result;
}

HomeSystemLogisticsNetwork home_system_logistics(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id) {
    const auto& civ = civilization(world.civilizations, civilization_id);
    const auto& projects = construction(world, civilization_id);
    const auto snapshot = economy_logistics(world, colonies, economies, civilization_id);
    std::unordered_map<int, ColonyLogisticsSnapshot> by_colony;
    for (const auto& item : snapshot.colonies) {
        if (!by_colony.emplace(item.colony_id, item).second) throw std::invalid_argument("Duplicate colony logistics snapshot ID " + std::to_string(item.colony_id) + ".");
    }
    std::vector<const Colony*> home;
    for (const auto& item : colonies) if (item.civilization_id == civilization_id && item.system_id == civ.home_system_id) home.push_back(&item);
    std::sort(home.begin(), home.end(), [](auto left, auto right) { return left->id < right->id; });
    HomeSystemLogisticsNetwork result; result.civilization_id = civilization_id; result.home_system_id = civ.home_system_id;
    std::unordered_map<int, int> ids; int next_node = 1, next_link = 1;
    for (std::size_t index = 0; index < home.size(); ++index) {
        const auto& colony = *home[index]; const int node = next_node++; ids.emplace(colony.id, node);
        result.nodes.push_back({node, civilization_id, civ.home_system_id, colony.name, index == 0 ? LogisticsNodeKind::Homeworld : LogisticsNodeKind::PlanetarySettlement});
        const auto& logistics = by_colony.at(colony.id);
        const double surplus = std::max(0.0, logistics.local_support_capacity_per_day - logistics.support_demand_per_day);
        if (surplus > 0) result.supply_offers.push_back({node, surplus});
        if (logistics.imported_support_required_per_day > 0) result.demands.push_back({node, logistics.imported_support_required_per_day, priority(logistics.condition)});
    }
    std::optional<int> hub, yard, resource;
    if (completed(projects, "orbital_launch_complex") || completed(projects, "orbital_shipyard")) { hub = next_node++; result.nodes.push_back({*hub, civilization_id, civ.home_system_id, "Home-System Orbital Logistics Hub", LogisticsNodeKind::OrbitalHub}); }
    if (completed(projects, "orbital_shipyard")) { yard = next_node++; result.nodes.push_back({*yard, civilization_id, civ.home_system_id, "Orbital Shipyard", LogisticsNodeKind::Shipyard}); }
    if (completed(projects, "asteroid_resource_network")) { resource = next_node++; result.nodes.push_back({*resource, civilization_id, civ.home_system_id, "Asteroid Resource Network", LogisticsNodeKind::ResourceSite}); }
    if (!home.empty() && hub) {
        const double capacity = std::max(.10, snapshot.cargo_handling_capacity_per_day);
        for (const auto* colony : home) result.links.push_back({next_link++, civilization_id, ids.at(colony->id), *hub, capacity, .10, true, true});
        if (yard) result.links.push_back({next_link++, civilization_id, *hub, *yard, capacity, .03, true, true});
        if (resource) result.links.push_back({next_link++, civilization_id, *resource, *hub, capacity, .35, true, true});
    } else if (home.size() > 1) {
        const double capacity = std::max(.05, snapshot.cargo_handling_capacity_per_day * .35);
        for (std::size_t i = 1; i < home.size(); ++i) result.links.push_back({next_link++, civilization_id, ids.at(home[0]->id), ids.at(home[i]->id), capacity, .75, true, true});
    }
    LogisticsRoutePlanner planner(result.nodes, result.links, std::max(8, static_cast<int>(result.nodes.size() * 2)));
    result.daily_flow = allocate_daily_logistics(planner, result.links, result.supply_offers, result.demands);
    for (const auto& item : result.demands) result.total_demand_per_day += item.required_per_day;
    for (const auto& item : result.supply_offers) result.total_supply_offered_per_day += item.available_per_day;
    result.total_allocated_per_day = result.daily_flow.total_allocated_per_day;
    result.total_unmet_demand_per_day = result.daily_flow.total_unmet_demand_per_day;
    return result;
}

CivilizationLogisticsCoverage civilization_logistics_coverage(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id) {
    const auto& civ = civilization(world.civilizations, civilization_id);
    const auto snapshot = economy_logistics(world, colonies, economies, civilization_id);
    std::unordered_map<int, ColonyLogisticsSnapshot> by_colony;
    for (const auto& item : snapshot.colonies) {
        if (!by_colony.emplace(item.colony_id, item).second) throw std::invalid_argument("Duplicate colony logistics snapshot ID " + std::to_string(item.colony_id) + ".");
    }
    CivilizationLogisticsCoverage result; result.civilization_id = civilization_id; result.home_system = home_system_logistics(world, colonies, economies, civilization_id);
    std::vector<int> systems; for (const auto& item : colonies) if (item.civilization_id == civilization_id && item.system_id != civ.home_system_id) systems.push_back(item.system_id);
    std::sort(systems.begin(), systems.end()); systems.erase(std::unique(systems.begin(), systems.end()), systems.end());
    for (const int system : systems) {
        ExternalSystemLogisticsStatus external; external.civilization_id = civilization_id; external.system_id = system; external.condition = SupplyCondition::Healthy;
        std::vector<const Colony*> system_colonies;
        for (const auto& colony : colonies) if (colony.civilization_id == civilization_id && colony.system_id == system) system_colonies.push_back(&colony);
        std::sort(system_colonies.begin(), system_colonies.end(), [](const auto* left, const auto* right) { return left->id < right->id; });
        for (const auto* colony : system_colonies) {
            const auto& item = by_colony.at(colony->id); ++external.colony_count; external.support_demand_per_day += item.support_demand_per_day; external.local_support_capacity_per_day += item.local_support_capacity_per_day; external.import_requirement_per_day += item.imported_support_required_per_day;
            if (static_cast<int>(item.condition) > static_cast<int>(external.condition)) external.condition = item.condition;
        }
        external.local_surplus_per_day = std::max(0.0, external.local_support_capacity_per_day - external.support_demand_per_day);
        result.external_import_requirement_per_day += external.import_requirement_per_day; result.external_local_surplus_per_day += external.local_surplus_per_day; result.external_systems.push_back(external);
    }
    result.owned_system_count = 1 + static_cast<int>(result.external_systems.size()); result.external_system_count = static_cast<int>(result.external_systems.size());
    result.unrepresented_interstellar_support_per_day = result.external_import_requirement_per_day;
    result.has_unrepresented_interstellar_support_gap = result.unrepresented_interstellar_support_per_day > .000001;
    return result;
}
} // namespace stellar::core
