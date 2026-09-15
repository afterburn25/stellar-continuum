#pragma once
#include <stellar/core/campaign_economy.hpp>
#include <memory>

namespace stellar::core {
enum class LogisticsNodeKind { Homeworld, OrbitalHub, LunarSettlement, PlanetarySettlement, ResourceSite, Depot, Shipyard };
struct LogisticsNode { int id{}, civilization_id{}, system_id{}; std::string name; LogisticsNodeKind kind{}; };
struct LogisticsLink {
    int id{}, civilization_id{}, from_node_id{}, to_node_id{};
    double capacity_per_day{}, transit_days{};
    bool bidirectional{true}, enabled{true};
};
struct LogisticsRoutePlan {
    int source_node_id{}, destination_node_id{};
    std::vector<int> node_ids, link_ids;
    double transit_days{}, bottleneck_capacity_per_day{};
};
// Owns immutable graph copies and a bounded cache. Owner-thread queries; rebuild after graph changes.
class LogisticsRoutePlanner {
public:
    LogisticsRoutePlanner(std::span<const LogisticsNode> nodes, std::span<const LogisticsLink> links, int cache_capacity = 256);
    ~LogisticsRoutePlanner();
    LogisticsRoutePlanner(LogisticsRoutePlanner&&) noexcept;
    LogisticsRoutePlanner& operator=(LogisticsRoutePlanner&&) noexcept;
    LogisticsRoutePlanner(const LogisticsRoutePlanner&) = delete;
    LogisticsRoutePlanner& operator=(const LogisticsRoutePlanner&) = delete;
    std::optional<LogisticsRoutePlan> find_route(int source_node_id, int destination_node_id);
    std::size_t cached_route_count() const;
    int cache_capacity() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct LogisticsSupplyOffer { int node_id{}; double available_per_day{}; };
struct LogisticsDemand { int node_id{}; double required_per_day{}; int priority{}; };
struct LogisticsFlowAllocation {
    int source_node_id{}, destination_node_id{};
    double allocated_per_day{}, transit_days{};
    std::vector<int> route_link_ids;
};
// Ordered key/value representation preserves source dictionary insertion order in accumulated totals.
struct LogisticsNodeQuantity { int node_id{}; double per_day{}; };
struct LogisticsFlowPlan {
    std::vector<LogisticsFlowAllocation> allocations;
    std::vector<LogisticsNodeQuantity> unmet_demand_per_day, unused_supply_per_day;
    double total_allocated_per_day{}, total_unmet_demand_per_day{};
};
LogisticsFlowPlan allocate_daily_logistics(LogisticsRoutePlanner& planner, std::span<const LogisticsLink> links,
    std::span<const LogisticsSupplyOffer> offers, std::span<const LogisticsDemand> demands);
enum class SupplyCondition { Healthy, Strained, Critical };
struct ColonyLogisticsSnapshot {
    int colony_id{}, system_id{};
    double support_demand_per_day{}, local_support_capacity_per_day{}, imported_support_required_per_day{}, coverage_ratio{};
    SupplyCondition condition{};
};
struct CivilizationLogisticsSnapshot {
    int civilization_id{};
    double total_support_demand_per_day{}, total_local_support_capacity_per_day{}, import_requirement_per_day{},
        cargo_handling_capacity_per_day{}, effective_coverage_ratio{};
    SupplyCondition condition{};
    std::vector<ColonyLogisticsSnapshot> colonies;
    int critical_colony_count{}, strained_colony_count{};
};
CivilizationLogisticsSnapshot economy_logistics(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id);
struct HomeSystemLogisticsNetwork {
    int civilization_id{}, home_system_id{};
    std::vector<LogisticsNode> nodes;
    std::vector<LogisticsLink> links;
    std::vector<LogisticsSupplyOffer> supply_offers;
    std::vector<LogisticsDemand> demands;
    LogisticsFlowPlan daily_flow;
    double total_demand_per_day{}, total_supply_offered_per_day{}, total_allocated_per_day{}, total_unmet_demand_per_day{};
};
HomeSystemLogisticsNetwork home_system_logistics(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id);
struct ExternalSystemLogisticsStatus {
    int civilization_id{}, system_id{}, colony_count{};
    double support_demand_per_day{}, local_support_capacity_per_day{}, import_requirement_per_day{}, local_surplus_per_day{};
    SupplyCondition condition{};
    bool has_represented_interstellar_freight_corridor{};
};
struct CivilizationLogisticsCoverage {
    int civilization_id{};
    HomeSystemLogisticsNetwork home_system;
    std::vector<ExternalSystemLogisticsStatus> external_systems;
    int owned_system_count{}, external_system_count{};
    double external_import_requirement_per_day{}, external_local_surplus_per_day{}, unrepresented_interstellar_support_per_day{};
    bool has_unrepresented_interstellar_support_gap{};
};
CivilizationLogisticsCoverage civilization_logistics_coverage(EconomyWorldView world, std::span<const Colony> colonies,
    std::span<const CivilizationEconomy> economies, int civilization_id);
} // namespace stellar::core
