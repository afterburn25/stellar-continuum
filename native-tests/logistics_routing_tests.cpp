#include <stellar/core/logistics.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

double number(const Json& value) {
    if (value.is_number()) return value.get<double>();
    require(value.is_string(), "numeric fixture value must be a number or named floating-point string");
    const auto text = value.get<std::string>();
    if (text == "Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    throw std::runtime_error("unknown named floating-point value: " + text);
}

bool same_number(double actual, double expected) {
    if (std::isnan(actual) || std::isnan(expected)) return std::isnan(actual) && std::isnan(expected);
    if (std::isinf(actual) || std::isinf(expected)) return actual == expected;
    return std::abs(actual - expected) <= 1e-12;
}

std::vector<LogisticsNode> parse_nodes(const Json& values) {
    std::vector<LogisticsNode> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back({
            value.at("Id").get<int>(), value.at("CivilizationId").get<int>(), value.at("SystemId").get<int>(),
            value.at("Name").get<std::string>(), static_cast<LogisticsNodeKind>(value.at("Kind").get<int>()),
        });
    }
    return result;
}

std::vector<LogisticsLink> parse_links(const Json& values) {
    std::vector<LogisticsLink> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back({
            value.at("Id").get<int>(), value.at("CivilizationId").get<int>(), value.at("FromNodeId").get<int>(),
            value.at("ToNodeId").get<int>(), number(value.at("CapacityPerDay")), number(value.at("TransitDays")),
            value.at("Bidirectional").get<bool>(), value.at("Enabled").get<bool>(),
        });
    }
    return result;
}

std::vector<LogisticsSupplyOffer> parse_offers(const Json& values) {
    std::vector<LogisticsSupplyOffer> result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.push_back({value.at("NodeId").get<int>(), number(value.at("AvailablePerDay"))});
    return result;
}

std::vector<LogisticsDemand> parse_demands(const Json& values) {
    std::vector<LogisticsDemand> result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.push_back({value.at("NodeId").get<int>(), number(value.at("RequiredPerDay")), value.at("Priority").get<int>()});
    return result;
}

LogisticsRoutePlan parse_route(const Json& value) {
    return {
        value.at("SourceNodeId").get<int>(), value.at("DestinationNodeId").get<int>(),
        value.at("NodeIds").get<std::vector<int>>(), value.at("LinkIds").get<std::vector<int>>(),
        number(value.at("TransitDays")), number(value.at("BottleneckCapacityPerDay")),
    };
}

std::optional<LogisticsRoutePlan> parse_optional_route(const Json& value) {
    if (value.is_null()) return std::nullopt;
    return parse_route(value);
}

std::vector<LogisticsNodeQuantity> parse_quantities(const Json& values) {
    std::vector<LogisticsNodeQuantity> result;
    result.reserve(values.size());
    for (const auto& value : values)
        result.push_back({value.at("NodeId").get<int>(), number(value.at("PerDay"))});
    return result;
}

LogisticsFlowPlan parse_flow(const Json& value) {
    LogisticsFlowPlan result;
    for (const auto& allocation : value.at("Allocations")) {
        result.allocations.push_back({
            allocation.at("SourceNodeId").get<int>(), allocation.at("DestinationNodeId").get<int>(),
            number(allocation.at("AllocatedPerDay")), number(allocation.at("TransitDays")),
            allocation.at("RouteLinkIds").get<std::vector<int>>(),
        });
    }
    result.unmet_demand_per_day = parse_quantities(value.at("UnmetDemandPerDay"));
    result.unused_supply_per_day = parse_quantities(value.at("UnusedSupplyPerDay"));
    result.total_allocated_per_day = number(value.at("TotalAllocatedPerDay"));
    result.total_unmet_demand_per_day = number(value.at("TotalUnmetDemandPerDay"));
    return result;
}

bool same_node(const LogisticsNode& a, const LogisticsNode& b) {
    return a.id == b.id && a.civilization_id == b.civilization_id && a.system_id == b.system_id &&
        a.name == b.name && a.kind == b.kind;
}
bool same_link(const LogisticsLink& a, const LogisticsLink& b) {
    return a.id == b.id && a.civilization_id == b.civilization_id && a.from_node_id == b.from_node_id &&
        a.to_node_id == b.to_node_id && same_number(a.capacity_per_day, b.capacity_per_day) &&
        same_number(a.transit_days, b.transit_days) && a.bidirectional == b.bidirectional && a.enabled == b.enabled;
}
bool same_offer(const LogisticsSupplyOffer& a, const LogisticsSupplyOffer& b) {
    return a.node_id == b.node_id && same_number(a.available_per_day, b.available_per_day);
}
bool same_demand(const LogisticsDemand& a, const LogisticsDemand& b) {
    return a.node_id == b.node_id && same_number(a.required_per_day, b.required_per_day) && a.priority == b.priority;
}

template<class T, class Equal>
bool same_vector(const std::vector<T>& a, const std::vector<T>& b, Equal equal) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!equal(a[i], b[i])) return false;
    return true;
}

void compare_route(const std::optional<LogisticsRoutePlan>& actual, const std::optional<LogisticsRoutePlan>& expected, const std::string& name) {
    require(actual.has_value() == expected.has_value(), name + ": route presence differs");
    if (!expected) return;
    require(actual->source_node_id == expected->source_node_id, name + ": source node differs");
    require(actual->destination_node_id == expected->destination_node_id, name + ": destination node differs");
    require(actual->node_ids == expected->node_ids, name + ": route node sequence differs");
    require(actual->link_ids == expected->link_ids, name + ": route link sequence differs");
    require(same_number(actual->transit_days, expected->transit_days), name + ": route transit differs");
    require(same_number(actual->bottleneck_capacity_per_day, expected->bottleneck_capacity_per_day), name + ": route bottleneck differs");
}

void compare_flow(const LogisticsFlowPlan& actual, const LogisticsFlowPlan& expected, const std::string& name) {
    require(actual.allocations.size() == expected.allocations.size(), name + ": allocation count differs");
    for (std::size_t i = 0; i < expected.allocations.size(); ++i) {
        const auto& a = actual.allocations[i];
        const auto& e = expected.allocations[i];
        require(a.source_node_id == e.source_node_id, name + ": allocation source differs at " + std::to_string(i));
        require(a.destination_node_id == e.destination_node_id, name + ": allocation destination differs at " + std::to_string(i));
        require(same_number(a.allocated_per_day, e.allocated_per_day), name + ": allocation quantity differs at " + std::to_string(i));
        require(same_number(a.transit_days, e.transit_days), name + ": allocation transit differs at " + std::to_string(i));
        require(a.route_link_ids == e.route_link_ids, name + ": allocation route differs at " + std::to_string(i));
    }
    auto compare_quantities = [&](const std::vector<LogisticsNodeQuantity>& a, const std::vector<LogisticsNodeQuantity>& e, const std::string& field) {
        require(a.size() == e.size(), name + ": " + field + " count differs");
        for (std::size_t i = 0; i < e.size(); ++i) {
            require(a[i].node_id == e[i].node_id, name + ": " + field + " insertion order differs at " + std::to_string(i));
            require(same_number(a[i].per_day, e[i].per_day), name + ": " + field + " quantity differs at " + std::to_string(i));
        }
    };
    compare_quantities(actual.unmet_demand_per_day, expected.unmet_demand_per_day, "unmet demand");
    compare_quantities(actual.unused_supply_per_day, expected.unused_supply_per_day, "unused supply");
    require(same_number(actual.total_allocated_per_day, expected.total_allocated_per_day), name + ": total allocation differs");
    require(same_number(actual.total_unmet_demand_per_day, expected.total_unmet_demand_per_day), name + ": total unmet demand differs");
}

std::string exception_name(const std::exception& error) {
    if (dynamic_cast<const std::out_of_range*>(&error)) return "ArgumentOutOfRangeException";
    if (dynamic_cast<const std::invalid_argument*>(&error)) return "ArgumentException";
    if (dynamic_cast<const std::runtime_error*>(&error)) return "InvalidOperationException";
    return "Exception";
}

std::optional<std::string> expected_error(const Json& outcome) {
    if (!outcome.contains("ExpectedError")) return std::nullopt;
    const auto result = outcome.at("ExpectedError").get<std::string>();
    require(result == "ArgumentException" || result == "ArgumentOutOfRangeException" || result == "InvalidOperationException",
        "fixture contains unsupported expected error type: " + result);
    return result;
}

void compare_operation_error(const std::optional<std::string>& actual, const std::optional<std::string>& expected, const std::string& name) {
    require(actual.has_value() == expected.has_value(), name + (expected ? ": expected an operation error" : ": unexpected operation error"));
    if (expected) require(*actual == *expected, name + ": wrong error type; expected " + *expected + ", got " + *actual);
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "fixture path required");
        std::ifstream stream(argv[1]);
        require(stream.good(), "could not open fixture");
        Json root;
        stream >> root;
        require(root.at("Format").get<std::string>() == "stellar-logistics-routing-oracle-v2", "unexpected fixture format");

        std::size_t case_count = 0;
        for (const auto& test : root.at("Cases")) {
            ++case_count;
            const auto name = test.at("Name").get<std::string>();
            const auto kind = test.at("Kind").get<std::string>();
            require(kind == "Constructor" || kind == "Route" || kind == "Sequence" || kind == "OwnedCopy" || kind == "Flow",
                name + ": unknown case kind " + kind);

            const auto& input = test.at("Input");
            auto ns = parse_nodes(input.at("Nodes"));
            const auto ns_before = ns;
            const auto& outcome = test.at("Outcome");
            const auto wanted_error = expected_error(outcome);

            if (kind == "Constructor") {
                auto ls = parse_links(input.at("Links"));
                const auto ls_before = ls;
                const int capacity = test.at("Arguments").at("CacheCapacity").get<int>();
                const auto wanted_capacity = wanted_error ? 0 : outcome.at("CacheCapacity").get<int>();
                const auto wanted_count = wanted_error ? 0U : outcome.at("CacheCount").get<std::size_t>();
                std::optional<std::string> actual_error;
                std::optional<int> actual_capacity;
                std::optional<std::size_t> actual_count;
                try {
                    LogisticsRoutePlanner planner(ns, ls, capacity);
                    actual_capacity = planner.cache_capacity();
                    actual_count = planner.cached_route_count();
                } catch (const std::exception& error) { actual_error = exception_name(error); }
                compare_operation_error(actual_error, wanted_error, name);
                if (!wanted_error) {
                    require(actual_capacity == wanted_capacity, name + ": constructor cache capacity differs");
                    require(actual_count == wanted_count, name + ": constructor cache count differs");
                }
                require(same_vector(ns, ns_before, same_node) && same_vector(ls, ls_before, same_link), name + ": constructor mutated caller data");
            } else if (kind == "Route" || kind == "OwnedCopy") {
                auto ls = parse_links(input.at("Links"));
                const auto ls_before = ls;
                const int capacity = test.at("Arguments").at("CacheCapacity").get<int>();
                const int source = kind == "OwnedCopy" ? 1 : test.at("Arguments").at("Source").get<int>();
                const int destination = kind == "OwnedCopy" ? 2 : test.at("Arguments").at("Destination").get<int>();
                const auto wanted_route = wanted_error ? std::optional<LogisticsRoutePlan>{} : parse_optional_route(outcome.at("Expected"));
                const auto wanted_count = wanted_error ? 0U : outcome.at("CacheCount").get<std::size_t>();
                const auto wanted_capacity = wanted_error ? 0 : outcome.at("CacheCapacity").get<int>();
                std::optional<std::string> actual_error;
                std::optional<LogisticsRoutePlan> actual_route;
                std::optional<std::size_t> actual_count;
                std::optional<int> actual_capacity;
                try {
                    LogisticsRoutePlanner planner(ns, ls, capacity);
                    if (kind == "OwnedCopy") {
                        ns[0].id = 99;
                        ls[0].id = 99;
                        ls[0].enabled = false;
                    }
                    actual_route = planner.find_route(source, destination);
                    actual_count = planner.cached_route_count();
                    actual_capacity = planner.cache_capacity();
                } catch (const std::exception& error) { actual_error = exception_name(error); }
                compare_operation_error(actual_error, wanted_error, name);
                if (!wanted_error) {
                    compare_route(actual_route, wanted_route, name);
                    require(actual_count == wanted_count, name + ": cache count differs");
                    require(actual_capacity == wanted_capacity, name + ": cache capacity differs");
                }
                if (kind == "Route") require(same_vector(ns, ns_before, same_node) && same_vector(ls, ls_before, same_link), name + ": route query mutated caller data");
            } else if (kind == "Sequence") {
                auto ls = parse_links(input.at("Links"));
                const auto ls_before = ls;
                const int capacity = test.at("Arguments").at("CacheCapacity").get<int>();
                std::vector<std::pair<int,int>> queries;
                for (const auto& query : test.at("Arguments").at("Queries")) queries.emplace_back(query.at("Source").get<int>(), query.at("Destination").get<int>());
                std::vector<std::optional<LogisticsRoutePlan>> wanted_routes;
                for (const auto& route : outcome.at("Expected")) wanted_routes.push_back(parse_optional_route(route));
                const auto wanted_counts = outcome.at("CacheCounts").get<std::vector<std::size_t>>();
                const auto wanted_count = outcome.at("CacheCount").get<std::size_t>();
                const auto wanted_capacity = outcome.at("CacheCapacity").get<int>();
                require(queries.size() == wanted_routes.size() && queries.size() == wanted_counts.size(), name + ": malformed sequence fixture");
                std::optional<std::string> actual_error;
                std::vector<std::optional<LogisticsRoutePlan>> actual_routes;
                std::vector<std::size_t> actual_counts;
                std::optional<int> actual_capacity;
                try {
                    LogisticsRoutePlanner planner(ns, ls, capacity);
                    actual_capacity = planner.cache_capacity();
                    for (const auto& [source, destination] : queries) {
                        actual_routes.push_back(planner.find_route(source, destination));
                        actual_counts.push_back(planner.cached_route_count());
                    }
                } catch (const std::exception& error) { actual_error = exception_name(error); }
                compare_operation_error(actual_error, wanted_error, name);
                require(actual_routes.size() == wanted_routes.size(), name + ": route sequence stopped early");
                for (std::size_t i = 0; i < wanted_routes.size(); ++i) compare_route(actual_routes[i], wanted_routes[i], name + " query " + std::to_string(i));
                require(actual_counts == wanted_counts, name + ": per-query cache counts differ");
                require(actual_counts.back() == wanted_count, name + ": final cache count differs");
                require(actual_capacity == wanted_capacity, name + ": cache capacity differs");
                require(same_vector(ns, ns_before, same_node) && same_vector(ls, ls_before, same_link), name + ": sequence mutated caller data");
            } else {
                auto planner_links = parse_links(input.at("PlannerLinks"));
                auto allocator_links = parse_links(input.at("AllocatorLinks"));
                auto supply = parse_offers(input.at("Offers"));
                auto demand = parse_demands(input.at("Demands"));
                const auto planner_links_before = planner_links;
                const auto allocator_links_before = allocator_links;
                const auto supply_before = supply;
                const auto demand_before = demand;
                const auto wanted_flow = wanted_error ? LogisticsFlowPlan{} : parse_flow(outcome.at("Expected"));
                const auto wanted_count = wanted_error ? 0U : outcome.at("CacheCount").get<std::size_t>();
                std::optional<std::string> actual_error;
                std::optional<LogisticsFlowPlan> actual_flow;
                std::optional<std::size_t> actual_count;
                try {
                    LogisticsRoutePlanner planner(ns, planner_links);
                    actual_flow = allocate_daily_logistics(planner, allocator_links, supply, demand);
                    actual_count = planner.cached_route_count();
                } catch (const std::exception& error) { actual_error = exception_name(error); }
                compare_operation_error(actual_error, wanted_error, name);
                if (!wanted_error) {
                    compare_flow(*actual_flow, wanted_flow, name);
                    require(actual_count == wanted_count, name + ": allocator cache count differs");
                }
                require(same_vector(ns, ns_before, same_node) && same_vector(planner_links, planner_links_before, same_link) &&
                    same_vector(allocator_links, allocator_links_before, same_link) && same_vector(supply, supply_before, same_offer) &&
                    same_vector(demand, demand_before, same_demand), name + ": flow allocation mutated caller data");
            }
        }
        std::cout << "logistics routing parity passed: " << case_count << " oracle cases\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
