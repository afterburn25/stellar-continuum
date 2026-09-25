#include <stellar/core/campaign_logistics_projection.hpp>

#include <cmath>
#include <iostream>

// Logistics projection tests — the read-only adapter that re-shapes the
// authoritative HomeSystemLogisticsNetwork into the engine freight
// network so route_utilization() reports per-corridor saturation and
// in_transit() lists the committed manifest.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool near(double a, double b, double eps = 1e-9) {
  return std::fabs(a - b) <= eps;
}

double utilization_for(const stellar::engine::LogisticsNetwork &network,
                       std::uint64_t route_id) {
  for (const auto &[id, u] : network.route_utilization())
    if (id == route_id) return u;
  return -1.0;
}

} // namespace

int main() {
  using namespace stellar::core;

  // Two-colony home system: node 1 (homeworld, surplus) -> hub 3 ->
  // node 2 (importer). Link 10 carries the full allocated flow; link 11
  // is a disabled spare; link 12 has zero capacity.
  HomeSystemLogisticsNetwork home;
  home.civilization_id = 7;
  home.home_system_id = 42;
  home.nodes = {{1, 7, 42, "Homeworld", LogisticsNodeKind::Homeworld},
                {2, 7, 42, "Frontier", LogisticsNodeKind::PlanetarySettlement},
                {3, 7, 42, "Hub", LogisticsNodeKind::OrbitalHub},
                {4, 7, 42, "Depot", LogisticsNodeKind::Depot}};
  home.links = {{10, 7, 1, 3, 4.0, 0.5, true, true},
                {11, 7, 1, 4, 8.0, 0.2, true, false},
                {12, 7, 3, 2, 0.0, 0.3, true, true},
                {13, 7, 3, 2, 2.0, 0.3, true, true}};
  home.supply_offers = {{1, 6.0}};
  home.demands = {{2, 5.0, 10}};
  // Allocation: 4.0/day from node 1 to node 2 over links 10 then 13.
  LogisticsFlowAllocation allocation;
  allocation.source_node_id = 1;
  allocation.destination_node_id = 2;
  allocation.allocated_per_day = 4.0;
  allocation.transit_days = 0.8;
  allocation.route_link_ids = {10, 13};
  home.daily_flow.allocations = {allocation};
  home.daily_flow.total_allocated_per_day = 4.0;
  home.total_allocated_per_day = 4.0;

  const auto projected = project_home_logistics_network(home);

  // Nodes and routes survive the projection intact.
  check(projected.node_ids().size() == 4, "all nodes projected");
  check(projected.has_node(1) && projected.has_node(4), "node ids preserved");
  check(projected.routes().size() == 4, "all links projected as routes");

  const auto *route10 = projected.route(10);
  check(route10 != nullptr, "route 10 exists");
  check(route10 && route10->path.size() == 2 && route10->path[0] == 1 &&
            route10->path[1] == 3,
        "route 10 carries the link endpoints");
  check(route10 && near(route10->capacity, 4.0 * 0.5),
        "route capacity is committed-tonnage capacity");
  check(route10 && route10->enabled, "route 10 enabled");

  const auto *route11 = projected.route(11);
  check(route11 && !route11->enabled, "disabled link stays disabled");
  check(route11 && near(route11->in_flight, 0.0),
        "disabled link carries nothing");

  // Committed flow: 4.0/day x 0.5 transit = 2.0 tonnage in flight on
  // link 10 -> utilization = 2.0 / (4.0/day x 0.5d) = 1.0 (saturated).
  check(near(utilization_for(projected, 10), 1.0),
        "link 10 reports saturated utilization");
  // Link 13: 4.0/day x 0.3 = 1.2 in flight over capacity 2.0 x 0.3 = 0.6
  // -> utilization 2.0 (over-committed corridor reads > 1).
  check(near(utilization_for(projected, 13), 2.0),
        "over-committed link reads above full utilization");
  check(near(utilization_for(projected, 11), 0.0),
        "idle disabled link reads zero utilization");
  check(near(utilization_for(projected, 12), 0.0),
        "zero-capacity link reads zero utilization");

  // The committed manifest: one in-transit shipment per traversed link,
  // quantities equal real cargo mass on that leg.
  const auto manifest = projected.in_transit();
  check(manifest.size() == 2, "one shipment per traversed link");
  check(manifest[0]->route == 10 && near(manifest[0]->quantity, 4.0 * 0.5),
        "shipment on link 10 carries its leg tonnage");
  check(manifest[1]->route == 13 && near(manifest[1]->quantity, 4.0 * 0.3),
        "shipment on link 13 carries its leg tonnage");
  check(manifest[0]->resource == "support", "manifest resource tagged");

  // Freshly observed state: clock at 0, shipments just departed.
  check(near(projected.now(), 0.0), "projection clock starts at zero");
  const auto progress = projected.shipment_progress(manifest[0]->id);
  check(progress && near(*progress, 0.0), "fresh shipments show zero progress");

  // A subsequent advance() moves the projected state coherently:
  // shipments on the 0.3-day link deliver first (eta ordering).
  auto stepped = projected;
  const auto advanced = stepped.advance(0.35);
  check(advanced.deliveries.size() == 1 &&
            advanced.deliveries[0].node == 2 &&
            near(advanced.deliveries[0].quantity, 4.0 * 0.3),
        "advance delivers the shorter leg first to the destination node");
  check(near(utilization_for(stepped, 13), 0.0),
        "delivered leg frees its corridor");

  // Empty network: no nodes, links, or flow — a degenerate but valid
  // projection (civs with a single self-sufficient colony).
  const HomeSystemLogisticsNetwork empty{};
  const auto bare = project_home_logistics_network(empty);
  check(bare.node_ids().empty() && bare.routes().empty() &&
            bare.in_transit().empty(),
        "empty network projects to an empty snapshot");

  if (failures) {
    std::cerr << failures << " logistics projection test(s) failed.\n";
    return 1;
  }
  std::cout << "campaign logistics projection tests passed\n";
  return 0;
}
