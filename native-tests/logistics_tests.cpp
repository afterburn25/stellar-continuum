#include <stellar/engine/logistics.hpp>

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

using namespace stellar::engine;

LogisticsNetwork basic_net() {
    LogisticsNetwork net;
    for (std::uint64_t i = 1; i <= 4; ++i) net.add_node(i);
    net.add_route(10, {1, 2, 3}, {2.0, 3.0}, 0.0); // 5-day, uncapped
    net.add_route(11, {1, 4}, {1.0}, 10.0);        // 1-day, cap 10
    return net;
}

} // namespace

int main() {
    // --- Route validation ----------------------------------------------------
    {
        LogisticsNetwork net;
        net.add_node(1);
        net.add_node(2);
        check(!net.add_route(1, {1}, {}, 0.0), "single-node route rejected");
        check(!net.add_route(1, {1, 9}, {1.0}, 0.0), "unknown node rejected");
        check(!net.add_route(1, {1, 2}, {1.0, 1.0}, 0.0),
              "leg_days size mismatch rejected");
        check(!net.add_route(1, {1, 2}, {-1.0}, 0.0), "negative leg rejected");
        check(net.add_route(1, {1, 2}, {1.0}, 5.0), "valid route added");
        check(!net.add_route(1, {1, 2}, {1.0}, 5.0), "duplicate route id");
        check(!net.remove_node(1), "route-referenced node cannot be removed");
    }

    // --- Transit + delivery ----------------------------------------------------
    {
        auto net = basic_net();
        check(net.dispatch(1, 10, "ore", 5.0), "dispatch accepted");
        auto r = net.advance(1.0);
        check(r.departed.size() == 1 && r.departed[0] == 1,
              "shipment departs on first advance");
        check(r.deliveries.empty(), "nothing delivered yet");
        check(net.shipment_progress(1).has_value(), "in transit");
        r = net.advance(4.0); // total 5 days elapsed
        check(r.deliveries.size() == 1, "delivered at eta");
        check(r.deliveries[0].node == 3, "delivered to route end");
        check(r.deliveries[0].resource == "ore" &&
                  near(r.deliveries[0].quantity, 5.0),
              "cargo intact");
        check(net.shipment(1) == nullptr, "shipment consumed on delivery");
    }

    // --- Route capacity queues shipments ------------------------------------------
    {
        auto net = basic_net();
        net.dispatch(1, 11, "ore", 6.0);
        net.dispatch(2, 11, "ore", 6.0); // would exceed cap 10 in-flight
        net.dispatch(3, 11, "ore", 4.0);
        auto r = net.advance(0.5);
        check(r.departed.size() == 2, "cap-limited departures");
        check(net.is_queued(2), "overflow shipment queued");
        // After delivery (1-day route), capacity frees and queue drains.
        r = net.advance(0.6); // now=1.1 > eta 1.0 for departed
        check(r.deliveries.size() == 2, "first wave delivered");
        r = net.advance(0.5);
        check(!net.is_queued(2), "queued shipment departs when capacity frees");
    }

    // --- Disabled route holds queue, keeps in-flight --------------------------------
    {
        auto net = basic_net();
        net.dispatch(1, 11, "ore", 4.0);
        net.advance(0.1); // departs, eta 1.1
        net.dispatch(2, 11, "ore", 4.0);
        net.set_route_enabled(11, false);
        auto r = net.advance(0.1);
        check(net.is_queued(2), "disabled route holds queue");
        r = net.advance(1.5);
        check(r.deliveries.size() == 1, "in-flight still arrives");
        check(net.is_queued(2), "still queued");
        net.set_route_enabled(11, true);
        r = net.advance(0.1);
        check(!net.is_queued(2), "re-enabled route resumes departures");
    }

    // --- Cancellation ------------------------------------------------------------------
    {
        auto net = basic_net();
        net.dispatch(1, 10, "ore", 1.0);
        check(net.cancel(1), "queued shipment cancelled");
        check(!net.cancel(1), "second cancel fails");
        auto r = net.advance(0.1);
        check(r.departed.empty(), "cancelled never departs");
        net.dispatch(2, 10, "ore", 1.0);
        net.advance(0.1);
        check(!net.cancel(2), "in-flight shipment cannot be cancelled");
    }

    // --- Deterministic delivery order -----------------------------------------------------
    {
        auto net = basic_net();
        // Two routes, dispatch out of id order; arrivals must be (eta,id).
        net.dispatch(9, 11, "a", 1.0); // eta = depart + 1
        net.dispatch(1, 10, "b", 1.0); // eta = depart + 5
        net.dispatch(5, 11, "c", 1.0);
        auto r = net.advance(6.0);
        check(r.deliveries.size() == 3, "all delivered");
        check(r.deliveries[0].shipment == 1 || r.deliveries[0].shipment == 5 ||
                  r.deliveries[0].shipment == 9,
              "delivery ordering sane");
        // eta 1.1 shipments (9 then 5) before eta 5.1 shipment (1)
        check(r.deliveries.back().shipment == 1, "longest transit last");
        check(r.deliveries[0].shipment == 5 || r.deliveries[0].shipment == 9,
              "short transits first");
        check(r.deliveries[0].shipment < r.deliveries[1].shipment,
              "same-eta deliveries ordered by id");
    }

    // --- Determinism ---------------------------------------------------------------------
    {
        auto run = [] {
            auto net = basic_net();
            for (std::uint64_t s = 1; s <= 50; ++s)
                net.dispatch(s, s % 2 ? 10 : 11, "ore", 2.0);
            double delivered = 0.0;
            for (int t = 0; t < 10; ++t)
                for (const auto& d : net.advance(0.6).deliveries)
                    delivered += d.quantity;
            return delivered;
        };
        check(run() == run(), "identical runs bit-equal");
    }

    // --- Scale ------------------------------------------------------------------------------
    {
        LogisticsNetwork net;
        for (std::uint64_t i = 0; i < 500; ++i) net.add_node(i);
        for (std::uint64_t i = 0; i + 1 < 500; ++i)
            net.add_route(i + 1, {i, i + 1}, {0.5}, 1000.0);
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint64_t s = 0; s < 20000; ++s)
            net.dispatch(s + 1, (s % 499) + 1, "ore", 1.0);
        double delivered = 0.0;
        for (int t = 0; t < 30; ++t)
            for (const auto& d : net.advance(0.25).deliveries)
                delivered += d.quantity;
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "scale: 20k shipments over 499 routes, 30 ticks in " << ms
                  << " ms; delivered=" << delivered << "\n";
        check(delivered > 0.0, "scale run delivers cargo");
        check(ms < 30000.0, "scale run within budget");
    }

    if (failures == 0) {
        std::cout << "all logistics tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
