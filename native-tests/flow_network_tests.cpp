#include <stellar/engine/flow_network.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

double edge_flow(const stellar::engine::FlowAdvanceResult& r,
                 std::uint64_t edge) {
    for (const auto& [id, q] : r.flow_by_edge)
        if (id == edge) return q;
    return 0.0;
}

double node_unmet(const stellar::engine::FlowAdvanceResult& r,
                  std::uint64_t node) {
    for (const auto& [id, q] : r.unmet_by_node)
        if (id == node) return q;
    return 0.0;
}

using namespace stellar::engine;

} // namespace

int main() {
    // --- Basic local serve -------------------------------------------------
    {
        FlowNetwork net("power");
        net.add_node(1, /*supply*/ 10.0, /*demand*/ 4.0);
        auto r = net.advance(1.0);
        check(near(r.total_served, 4.0), "local demand served");
        check(r.unmet_by_node.empty(), "no unmet");
        check(near(net.node(1)->last_exported, 0.0), "no exports without edges");
    }

    // --- Edge transfer balances producer -> consumer -------------------------
    {
        FlowNetwork net("power");
        net.add_node(1, 10.0, 0.0); // generator
        net.add_node(2, 0.0, 6.0);  // consumer
        check(net.add_edge(100, 1, 2, 8.0), "edge added");
        check(!net.add_edge(100, 1, 2, 8.0), "duplicate edge id rejected");
        check(!net.add_edge(101, 1, 2, 8.0), "duplicate direction rejected");
        check(net.add_edge(101, 2, 1, 8.0), "reverse direction allowed");
        auto r = net.advance(1.0);
        check(near(edge_flow(r, 100), 6.0), "edge carries demand");
        check(near(net.node(2)->last_served, 6.0), "consumer served via edge");
        check(near(node_unmet(r, 2), 0.0), "consumer fully met");
    }

    // --- Capacity-limited edge leaves residual unmet -------------------------
    {
        FlowNetwork net("power");
        net.add_node(1, 10.0, 0.0);
        net.add_node(2, 0.0, 6.0);
        net.add_edge(100, 1, 2, 2.0); // cap 2/day < demand 6
        auto r = net.advance(1.0);
        check(near(edge_flow(r, 100), 2.0), "edge saturates at capacity");
        check(near(node_unmet(r, 2), 4.0), "residual unmet reported");
        check(near(r.total_unmet, 4.0), "total unmet consistent");
    }

    // --- Storage buffers -----------------------------------------------------
    {
        FlowNetwork net("water");
        net.add_node(1, 0.0, 5.0, /*storage*/ 10.0);
        net.deposit_storage(1, 8.0);
        auto r = net.advance(1.0);
        check(near(r.storage_released, 5.0), "storage covers deficit");
        check(near(net.node(1)->storage, 3.0), "storage drained");
        check(near(node_unmet(r, 1), 0.0), "deficit fully buffered");
        // Next tick: storage 3 < demand 5 -> 2 unmet.
        r = net.advance(1.0);
        check(near(node_unmet(r, 1), 2.0), "partial storage leaves unmet");
        // Surplus node refills storage after serving itself.
        net.set_node_rates(1, 7.0, 5.0);
        r = net.advance(1.0);
        check(near(r.storage_absorbed, 2.0), "surplus absorbed into storage");
        check(near(net.node(1)->storage, 2.0), "storage refilled");
    }

    // --- Islands: no path means unmet despite surplus elsewhere ---------------
    {
        FlowNetwork net("power");
        net.add_node(1, 10.0, 0.0);
        net.add_node(2, 0.0, 6.0); // no edge
        auto r = net.advance(1.0);
        check(near(node_unmet(r, 2), 6.0), "islanded demand unmet");
        const auto& comps = net.components();
        check(comps.size() == 2, "two components");
        // Disabling the generator's edge has same effect as removing it.
        net.add_edge(100, 1, 2, 8.0);
        check(net.topology_dirty(), "edge add dirties topology");
        check(net.components().size() == 1, "components merged");
        net.set_edge_enabled(100, false);
        check(net.components().size() == 2, "disabled edge splits component");
        r = net.advance(1.0);
        check(near(node_unmet(r, 2), 6.0), "disabled edge carries nothing");
    }

    // --- Multi-hop greedy transfer ---------------------------------------------
    {
        FlowNetwork net("power");
        net.add_node(1, 10.0, 0.0); // generator
        net.add_node(2, 0.0, 0.0);  // relay
        net.add_node(3, 0.0, 5.0);  // consumer
        net.add_edge(100, 1, 2, 8.0);
        net.add_edge(101, 2, 3, 8.0);
        auto r = net.advance(1.0);
        // Greedy single pass: node 2 has no export at edge-scan time
        // unless pass ordering re-feeds. Node 2's surplus arrives at edge
        // 100 during pass B — but pass B already scanned edge 101? Edges
        // scan ascending: 100 fills node2's export AFTER edge 101 checked.
        check(near(node_unmet(r, 3), 5.0),
              "single-pass greedy does not chain transfers");
    }

    // --- Determinism -------------------------------------------------------------
    {
        auto run = [] {
            FlowNetwork net("power");
            net.add_node(1, 10.0, 2.0);
            net.add_node(2, 4.0, 6.0);
            net.add_node(3, 0.0, 5.0, 4.0);
            net.add_edge(10, 1, 2, 3.0);
            net.add_edge(11, 1, 3, 6.0);
            net.add_edge(12, 2, 3, 2.0);
            double unmet = 0.0;
            for (int t = 0; t < 20; ++t) unmet += net.advance(1.0).total_unmet;
            return std::pair{unmet, net.node(3)->storage};
        };
        const auto a = run();
        const auto b = run();
        check(a.first == b.first && a.second == b.second,
              "identical runs bit-equal");
    }

    // --- remove_node drops incident edges -----------------------------------------
    {
        FlowNetwork net("power");
        net.add_node(1, 5.0, 0.0);
        net.add_node(2, 0.0, 5.0);
        net.add_edge(7, 1, 2, 5.0);
        check(net.remove_node(1), "node removed");
        check(net.edge_count() == 0, "incident edges dropped");
        check(!net.set_node_enabled(9, true), "missing node rejected");
    }

    // --- Scale: 10k nodes, 15k edges -----------------------------------------------
    {
        FlowNetwork net("power");
        const int n = 10000;
        for (int i = 0; i < n; ++i) {
            net.add_node(i, i % 4 == 0 ? 3.0 : 0.0, 1.0, i % 16 == 0 ? 2.0 : 0.0);
        }
        for (int i = 0; i < n - 1; ++i) {
            net.add_edge(100000 + i, i, i + 1, 2.0);
            if (i + 5 < n) net.add_edge(200000 + i, i + 5, i, 1.0);
        }
        const auto& comps = net.components();
        check(!comps.empty(), "components built");
        const auto t0 = std::chrono::steady_clock::now();
        double unmet = 0.0;
        for (int t = 0; t < 10; ++t) unmet += net.advance(1.0).total_unmet;
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "scale: 10k nodes, " << net.edge_count()
                  << " edges x 10 ticks in " << ms << " ms; unmet=" << unmet
                  << "\n";
        check(unmet > 0.0, "scale run produced deficits");
        check(ms < 20000.0, "scale run within budget");
    }

    if (failures == 0) {
        std::cout << "all flow network tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
