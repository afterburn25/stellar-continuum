#include <stellar/core/campaign_logistics_projection.hpp>

#include <algorithm>
#include <unordered_map>

namespace stellar::core {

engine::LogisticsNetwork
project_home_logistics_network(const HomeSystemLogisticsNetwork& network) {
  engine::LogisticsNetwork::State state;
  state.now = 0.0;

  state.nodes.reserve(network.nodes.size());
  for (const auto& node : network.nodes)
    state.nodes.push_back(static_cast<std::uint64_t>(node.id));
  std::sort(state.nodes.begin(), state.nodes.end());

  std::unordered_map<int, const LogisticsLink*> links;
  links.reserve(network.links.size());
  for (const auto& link : network.links) links.emplace(link.id, &link);

  // Per-link committed tonnage = allocated_per_day x transit_days,
  // gathered across every allocation traversing the link.
  std::unordered_map<int, double> in_flight;
  std::uint64_t shipment_id = 1;
  for (const auto& allocation : network.daily_flow.allocations) {
    if (allocation.allocated_per_day <= 0.0) continue;
    for (const int link_id : allocation.route_link_ids) {
      const auto it = links.find(link_id);
      if (it == links.end()) continue;
      const double transit = it->second->transit_days;
      const double quantity = allocation.allocated_per_day * transit;
      in_flight[link_id] += quantity;
      engine::LogisticsNetwork::ShipmentState shipment;
      shipment.id = shipment_id++;
      shipment.route = static_cast<std::uint64_t>(link_id);
      shipment.resource = "support";
      shipment.quantity = quantity;
      shipment.departed = 0.0;
      shipment.eta = transit;
      state.in_transit.push_back(shipment);
    }
  }

  state.routes.reserve(network.links.size());
  for (const auto& link : network.links) {
    engine::LogisticsNetwork::RouteState route;
    route.id = static_cast<std::uint64_t>(link.id);
    route.path = {static_cast<std::uint64_t>(link.from_node_id),
                  static_cast<std::uint64_t>(link.to_node_id)};
    route.leg_days = {link.transit_days};
    route.capacity = link.capacity_per_day * link.transit_days;
    route.enabled = link.enabled;
    route.in_flight = in_flight[link.id];
    state.routes.push_back(route);
  }
  std::sort(state.routes.begin(), state.routes.end(),
            [](const auto& a, const auto& b) { return a.id < b.id; });
  std::sort(state.in_transit.begin(), state.in_transit.end(),
            [](const auto& a, const auto& b) { return a.id < b.id; });

  engine::LogisticsNetwork projected;
  projected.restore_state(state);
  return projected;
}

} // namespace stellar::core
