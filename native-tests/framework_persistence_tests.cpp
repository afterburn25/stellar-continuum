#include <stellar/engine/colony.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/resource_economy.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <stellar/engine/warfare.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// Framework persistence round-trips: every specialization framework
// exposes capture_state()/restore_state() as versioned plain structs.
// Definitions (profiles, specs, recipes, ship classes, actions) are
// content — re-registered on load; instance state is restored exactly.

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

void define_specs(Colony& c) {
    DistrictSpec ind;
    ind.id = "district.industrial";
    ind.build_days = 10.0;
    c.define_district(ind);
    StructureSpec plant;
    plant.id = "structure.plant";
    plant.jobs = 10.0;
    plant.utility_supply_per_day = {{"power", 50.0}};
    c.define_structure(plant);
}

void define_profiles(Population& p) {
    DemographicProfile prof;
    prof.id = "species.human";
    p.define_profile(prof);
}

void define_classes(WarfareModel& w) {
    ShipClass dd;
    dd.id = "class.destroyer";
    dd.attack = 4.0;
    dd.defense = 0.5;
    dd.hull = 10.0;
    dd.speed = 2.0;
    w.define_class(dd);
}

} // namespace

int main() {
    // --- Population ---------------------------------------------------
    {
        Population a;
        define_profiles(a);
        CohortKey miners;
        miners.profile = "species.human";
        miners.occupation = "miner";
        a.add(miners, 5000.0);
        SettlementConditions cond;
        cond.food_ratio = 0.7; // stressed — produces nonzero drift
        cond.jobs_available = 1000.0;
        a.advance(30.0, cond);
        const auto state = a.capture_state();

        Population b;
        define_profiles(b);
        b.restore_state(state);
        check(std::abs(b.total() - a.total()) < 1e-9,
              "population total restored");

        // Both evolve identically afterwards.
        for (int i = 0; i < 5; ++i) {
            const auto da = a.advance(30.0, cond);
            const auto db = b.advance(30.0, cond);
            check(da.births == db.births && da.deaths == db.deaths,
                  "restored population evolves identically");
        }
        check(a.total() == b.total(), "restored population stays identical");

        // Missing profile is an error, not silent data loss.
        Population c;
        bool threw = false;
        try {
            c.restore_state(state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "restore without profile throws");
    }

    // --- Colony -------------------------------------------------------
    {
        Colony a;
        define_specs(a);
        a.build_district(1, "district.industrial");
        a.build_structure(2, "structure.plant");
        a.build_structure(3, "structure.plant", 1);
        ColonyInputs inputs;
        inputs.workers_available = 500.0;
        a.advance(12.0, inputs); // completes the 10-day district
        const auto state = a.capture_state();

        Colony b;
        define_specs(b);
        b.restore_state(state);
        check(b.structure_count() == a.structure_count(),
              "colony structure count restored");
        check(b.district(1) && b.district(1)->complete == a.district(1)->complete,
              "district construction state restored");
        for (int i = 0; i < 5; ++i) {
            const auto da = a.advance(10.0, inputs);
            const auto db = b.advance(10.0, inputs);
            check(da.jobs_filled == db.jobs_filled,
                  "restored colony produces identically");
        }

        Colony c;
        bool threw = false;
        try {
            c.restore_state(state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "restore without specs throws");
    }

    // --- FlowNetwork --------------------------------------------------
    {
        FlowNetwork a("power");
        a.add_node(1, 100.0, 0.0, 50.0);
        a.add_node(2, 0.0, 80.0);
        a.add_node(3, 0.0, 60.0);
        a.add_edge(1, 1, 2, 40.0);
        a.add_edge(2, 1, 3, 90.0);
        a.advance(1.0);
        a.deposit_storage(1, 20.0);
        const auto state = a.capture_state();

        FlowNetwork b("power");
        b.restore_state(state);
        check(b.node_count() == 3 && b.edge_count() == 2,
              "flow topology restored");
        check(b.node(1) && std::abs(b.node(1)->storage - 20.0) < 1e-9,
              "flow storage restored");
        for (int i = 0; i < 4; ++i) {
            const auto ra = a.advance(1.0);
            const auto rb = b.advance(1.0);
            check(ra.total_served == rb.total_served &&
                      ra.total_unmet == rb.total_unmet,
                  "restored flow advances identically");
        }

        FlowNetwork wrong("water");
        bool threw = false;
        try {
            wrong.restore_state(state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "resource mismatch throws");
    }

    // --- LogisticsNetwork ---------------------------------------------
    {
        LogisticsNetwork a;
        a.add_node(1);
        a.add_node(2);
        a.add_node(3);
        a.add_route(10, {1, 2}, {5.0}, 100.0);
        a.add_route(20, {2, 3}, {3.0}, 0.0);
        a.dispatch(1, 10, "res.ore", 40.0);
        a.advance(2.0);
        a.dispatch(2, 20, "res.ore", 25.0);
        const auto state = a.capture_state();

        LogisticsNetwork b;
        b.restore_state(state);
        check(b.now() == a.now(), "logistics clock restored");
        check(b.in_transit().size() == a.in_transit().size(),
              "in-flight shipments restored");
        for (int i = 0; i < 5; ++i) {
            const auto ra = a.advance(1.0);
            const auto rb = b.advance(1.0);
            check(ra.deliveries.size() == rb.deliveries.size() &&
                      ra.departed == rb.departed,
                  "restored logistics advances identically");
        }
    }

    // --- WarfareModel -------------------------------------------------
    {
        WarfareModel a;
        define_classes(a);
        a.add_fleet(1, 7, 0.0, 0.0);
        a.add_fleet(2, 9, 10.0, 0.0);
        a.add_ships(1, "class.destroyer", 50.0, 0.9, 0.3);
        a.add_ships(2, "class.destroyer", 30.0);
        a.set_order(1, {FleetOrderKind::Move, 10.0, 0.0});
        a.advance(2.0);
        a.resolve(1, 2, 0.5);
        const auto state = a.capture_state();

        WarfareModel b;
        define_classes(b);
        b.restore_state(state);
        check(b.fleets().size() == 2, "fleets restored");
        const auto* fc = b.cohort(1, "class.destroyer");
        const auto* fac = a.cohort(1, "class.destroyer");
        check(fc && fac && fc->count == fac->count &&
                  fc->condition == fac->condition,
              "ship cohort state restored");
        check(b.fleet(1)->x == a.fleet(1)->x, "fleet position restored");
        for (int i = 0; i < 3; ++i) {
            a.advance(1.0);
            b.advance(1.0);
            check(a.fleet(1)->x == b.fleet(1)->x,
                  "restored fleets move identically");
        }
    }

    // --- StrategicMind ------------------------------------------------
    {
        StrategicMind a;
        int commits_a = 0;
        UtilityAction mine;
        mine.id = "expand.mining";
        mine.domain = "economy";
        mine.score = [] { return 0.8; };
        mine.commit = [&commits_a] { ++commits_a; };
        mine.cooldown_days = 10.0;
        a.add_action(mine);
        UtilityAction research;
        research.id = "research.push";
        research.domain = "economy";
        research.score = [] { return 0.4; };
        a.add_action(research);
        a.decide("economy", 100.0);
        const auto state = a.capture_state();

        StrategicMind b;
        int commits_b = 0;
        UtilityAction mine_b = mine;
        mine_b.commit = [&commits_b] { ++commits_b; };
        b.add_action(mine_b);
        b.add_action(research);
        b.restore_state(state);
        check(b.incumbent("economy") == a.incumbent("economy"),
              "incumbent restored");
        check(b.journal().size() == a.journal().size(),
              "journal restored");
        // Cooldown survives: 5 days after the commit (inside the 10-day
        // cooldown) expand.mining cannot recommit — both minds must pick
        // research.push. If last_commit were lost, mining would win again.
        const auto ra = a.decide("economy", 105.0);
        const auto rb = b.decide("economy", 105.0);
        check(ra == rb && ra == std::optional<std::string>{"research.push"},
              "cooldown survives restore");
    }

    // --- ResourceNetwork ----------------------------------------------
    {
        ResourceNetwork a;
        a.define({"res.ore", "ORE", true});
        a.define({"res.steel", "STEEL", true});
        Recipe smelt;
        smelt.id = "smelt";
        smelt.inputs = {{"res.ore", 2.0}};
        smelt.outputs = {{"res.steel", 1.0}};
        smelt.duration_days = 2.0;
        a.add_recipe(smelt);
        auto& n1 = a.add_node(1);
        a.add_node(2, 100.0);
        n1.inventory.add("res.ore", 10.0);
        a.add_producer(1, "smelt");
        a.transfer(1, 2, "res.ore", 6.0, 2.0);
        a.advance(1.0);
        const auto state = a.capture_state();

        ResourceNetwork b;
        b.define({"res.ore", "ORE", true});
        b.define({"res.steel", "STEEL", true});
        b.add_recipe(smelt);
        b.restore_state(state);
        check(std::abs(b.node(1)->inventory.quantity("res.ore") -
                       a.node(1)->inventory.quantity("res.ore")) < 1e-9,
              "inventory restored");
        for (int i = 0; i < 4; ++i) {
            a.advance(1.0);
            b.advance(1.0);
        }
        check(std::abs(b.node(1)->inventory.quantity("res.steel") -
                       a.node(1)->inventory.quantity("res.steel")) < 1e-9,
              "restored network produces identically");
        check(std::abs(b.node(2)->inventory.quantity("res.ore") -
                       a.node(2)->inventory.quantity("res.ore")) < 1e-9,
              "restored transfers complete identically");

        ResourceNetwork c;
        bool threw = false;
        try {
            c.restore_state(state);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "restore without recipe throws");
    }

    if (failures != 0) {
        std::cerr << failures << " framework persistence checks failed\n";
        return 1;
    }
    std::cout << "Framework persistence round-trip tests passed "
                 "(population/colony/flow/logistics/warfare/ai/economy)\n";
    return 0;
}
