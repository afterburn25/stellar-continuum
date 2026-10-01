#include <stellar/engine/warfare.hpp>

#include <chrono>
#include <cmath>
#include <iostream>

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

ShipClass destroyer() {
    ShipClass c;
    c.id = "class.destroyer";
    c.role = "line";
    c.attack = 2.0;
    c.defense = 0.0;
    c.hull = 10.0;
    c.speed = 4.0;
    c.supply_per_day = 0.1;
    c.interdiction = 0.05;
    return c;
}

ShipClass cruiser() {
    ShipClass c;
    c.id = "class.cruiser";
    c.role = "line";
    c.attack = 5.0;
    c.defense = 0.5;
    c.hull = 100.0;
    c.speed = 2.0;
    c.supply_per_day = 0.5;
    c.interdiction = 0.5;
    return c;
}

WarfareModel make_model() {
    WarfareModel m;
    m.define_class(destroyer());
    m.define_class(cruiser());
    return m;
}

} // namespace

int main() {
    // --- Validation -------------------------------------------------------------
    {
        WarfareModel m;
        bool threw = false;
        try {
            ShipClass bad;
            bad.hull = 0.0;
            m.define_class(bad);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "invalid class rejected");
        m = make_model();
        check(!m.add_ships(1, "class.destroyer", 5.0),
              "ships need a fleet");
        check(m.add_fleet(1, /*owner*/ 7, 0.0, 0.0), "fleet added");
        check(!m.add_ships(1, "class.unknown", 5.0), "unknown class rejected");
        check(!m.add_ships(1, "class.destroyer", -1.0), "negative rejected");
    }

    // --- Cohort aggregation ------------------------------------------------------
    {
        auto m = make_model();
        m.add_fleet(1, 7, 0.0, 0.0);
        m.add_ships(1, "class.destroyer", 100.0);
        m.add_ships(1, "class.destroyer", 100.0, /*cond*/ 0.5);
        const auto* c = m.cohort(1, "class.destroyer");
        check(c && near(c->count, 200.0), "same-class adds merge");
        check(near(c->condition, 0.75), "merge weights condition");
        const auto r = m.report(1);
        check(near(r.ships, 200.0), "report counts cohorts");
        // attack = 200 * 2 * 0.75 * (1+0) = 300
        check(near(r.attack, 300.0), "attack aggregates with condition");
    }

    // --- Movement -----------------------------------------------------------------
    {
        auto m = make_model();
        m.add_fleet(1, 7, 0.0, 0.0);
        m.add_ships(1, "class.destroyer", 10.0);
        m.add_ships(1, "class.cruiser", 5.0); // slower: speed 2
        FleetOrder o;
        o.kind = FleetOrderKind::Move;
        o.target_x = 10.0;
        o.target_y = 0.0;
        m.set_order(1, o);
        m.advance(1.0);
        check(near(m.fleet(1)->x, 2.0), "fleet moves at slowest ship speed");
        m.advance(10.0);
        check(near(m.fleet(1)->x, 10.0), "arrival clamps to target");
        check(m.fleet(1)->order.kind == FleetOrderKind::Hold,
              "order clears on arrival");
    }

    // --- Interdiction ----------------------------------------------------------------
    {
        auto m = make_model();
        m.add_fleet(1, /*owner*/ 7, 0.0, 0.0);
        m.add_ships(1, "class.cruiser", 10.0); // radius 10*0.5 = 5
        FleetOrder o;
        o.kind = FleetOrderKind::Interdict;
        m.set_order(1, o);
        check(m.interdicted(3.0, 0.0, /*mover owner*/ 9),
              "hostile point inside zone interdicted");
        check(!m.interdicted(8.0, 0.0, 9), "outside zone free");
        check(!m.interdicted(3.0, 0.0, /*owner*/ 7),
              "same-owner movement not gated");
        // Hold order projects no zone — presence alone doesn't block.
        m.set_order(1, FleetOrder{});
        check(!m.interdicted(3.0, 0.0, 9),
              "presence without interdict order doesn't gate");
    }

    // --- Engagement: superior force destroys inferior ----------------------------------
    {
        auto m = make_model();
        m.add_fleet(1, 7, 0.0, 0.0);
        m.add_fleet(2, 9, 1.0, 0.0);
        m.add_ships(1, "class.destroyer", 100.0);
        m.add_ships(2, "class.cruiser", 10.0);
        auto r = m.resolve(1, 2, 10.0);
        // B loses min(10, (2000 - 50 absorb)/100) = min(10, 19.5) = 10.
        check(near(r.b_ships_lost, 10.0), "inferior fleet destroyed");
        check(r.b_destroyed, "destroyed flag set");
        // A loses (50 dps * 10d - 0 absorb) / 10 hull = 50 ships.
        check(near(r.a_ships_lost, 50.0), "attrition on attacker too");
        check(!r.a_destroyed, "victor survives");
        check(!m.fleet(2)->engaged, "destroyed fleet disengaged");
    }

    // --- Defense absorbs damage -----------------------------------------------------
    {
        auto m = make_model();
        m.add_fleet(1, 7, 0.0, 0.0);
        m.add_fleet(2, 9, 0.0, 0.0);
        m.add_ships(1, "class.destroyer", 10.0); // dps 20
        ShipClass fortress = cruiser();
        fortress.id = "class.fortress";
        fortress.defense = 3.0; // absorbs 3*10*1 = 30/day > 20 dps
        m.define_class(fortress);
        m.add_ships(2, "class.fortress", 10.0);
        auto r = m.resolve(1, 2, 1.0);
        check(near(r.b_ships_lost, 0.0), "defense absorbs all damage");
    }

    // --- Determinism ---------------------------------------------------------------------
    {
        auto run = [] {
            auto m = make_model();
            m.add_fleet(1, 7, 0.0, 0.0);
            m.add_fleet(2, 9, 5.0, 5.0);
            m.add_ships(1, "class.destroyer", 40.0);
            m.add_ships(1, "class.cruiser", 5.0);
            m.add_ships(2, "class.destroyer", 60.0);
            double loss = 0.0;
            for (int t = 0; t < 10; ++t) {
                auto r = m.resolve(1, 2, 1.0);
                loss += r.a_ships_lost + r.b_ships_lost;
            }
            return loss;
        };
        check(run() == run(), "identical engagements bit-equal");
    }

    // --- Scale: 2000 fleets moving + engaging ---------------------------------------------
    {
        auto m = make_model();
        for (std::uint64_t i = 0; i < 2000; ++i) {
            m.add_fleet(i + 1, (i % 2) + 1, double(i % 100), double(i / 100));
            m.add_ships(i + 1, "class.destroyer", 50.0);
            m.add_ships(i + 1, "class.cruiser", 5.0);
            FleetOrder o;
            o.kind = FleetOrderKind::Move;
            o.target_x = 500.0;
            o.target_y = 500.0;
            m.set_order(i + 1, o);
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < 30; ++t) m.advance(1.0);
        // A few hundred engagements.
        for (std::uint64_t i = 0; i + 1 < 400; i += 2)
            m.resolve(i + 1, i + 2, 1.0);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "scale: 2000 fleets x 30 moves + 200 engagements in "
                  << ms << " ms\n";
        check(ms < 30000.0, "scale run within budget");
    }

    if (failures == 0) {
        std::cout << "all warfare tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
