// Combined simulation benchmark: population + colony + flow networks +
// logistics + warfare + strategic AI, all driven by SimulationExecutor
// tasks at different tiers. Proves the specialization frameworks
// compose deterministically at campaign scale.

#include <stellar/engine/colony.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/resource_economy.hpp>
#include <stellar/engine/simulation_executor.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <stellar/engine/warfare.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

using namespace stellar::engine;

constexpr int kSettlements = 400;
constexpr int kTicks = 240;

// FNV-1a over doubles — deterministic checksum of simulation state.
struct Checksum {
    std::uint64_t h{1469598103934665603ull};
    void add(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, 8);
        for (int i = 0; i < 8; ++i) {
            h ^= (bits >> (i * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    }
};

struct Settlement {
    Population pop;
    Colony colony;
    double stored_food{1000.0};
    double stored_parts{500.0};
};

struct World {
    std::vector<Settlement> settlements;
    std::vector<FlowNetwork> grids;      // regional power networks
    LogisticsNetwork freight;
    WarfareModel warfare;
    std::vector<StrategicMind> factions; // one mind per faction
};

DemographicProfile species() {
    DemographicProfile p;
    p.id = "species.human";
    p.base_fertility_per_year = 0.03;
    p.base_mortality_per_year = 0.012;
    p.workforce_participation = 0.55;
    return p;
}

void build_world(World& w) {
    // Species + structure catalog (shared definitions).
    for (int i = 0; i < kSettlements; ++i) {
        Settlement s;
        s.pop.define_profile(species());
        CohortKey k;
        k.profile = "species.human";
        k.occupation = "worker";
        s.pop.add(k, 8000.0);
        k.occupation = "agriculture";
        s.pop.add(k, 3000.0);
        k.occupation = "";
        s.pop.add(k, 4000.0);

        StructureSpec hab;
        hab.id = "hab";
        hab.housing = 6000.0;
        hab.utility_demand_per_day = {{"power", 1.0}};
        s.colony.define_structure(hab);
        StructureSpec plant;
        plant.id = "plant";
        plant.utility_supply_per_day = {{"power", 8.0}};
        plant.jobs = 200.0;
        s.colony.define_structure(plant);
        StructureSpec farm;
        farm.id = "farm";
        farm.utility_demand_per_day = {{"power", 1.0}};
        farm.jobs = 400.0;
        farm.inputs_per_day = {{"parts", 0.05}};
        farm.outputs_per_day = {{"food", 3.0}};
        s.colony.define_structure(farm);
        StructureSpec works;
        works.id = "works";
        works.utility_demand_per_day = {{"power", 2.0}};
        works.jobs = 300.0;
        works.inputs_per_day = {{"ore", 1.0}};
        works.outputs_per_day = {{"parts", 0.6}};
        s.colony.define_structure(works);

        s.colony.build_structure(1, "plant");
        s.colony.build_structure(2, "hab");
        s.colony.build_structure(3, "farm");
        s.colony.build_structure(4, "works");
        w.settlements.push_back(std::move(s));
    }

    // Regional power grids: 8 networks of 50 nodes, chain-linked.
    for (int g = 0; g < 8; ++g) {
        FlowNetwork net("power");
        for (int i = 0; i < 50; ++i) {
            const int sid = g * 50 + i;
            const double supply = (i % 5 == 0) ? 10.0 : 0.0;
            net.add_node(sid, supply, 4.0);
            if (i > 0) net.add_edge(1000 + sid, sid - 1, sid, 12.0);
        }
        w.grids.push_back(std::move(net));
    }

    // Freight web: settlement waypoints + ring routes.
    for (int i = 0; i < kSettlements; ++i) w.freight.add_node(i);
    for (int i = 0; i < kSettlements; ++i) {
        const int next = (i + 1) % kSettlements;
        w.freight.add_route(100 + i, {std::uint64_t(i), std::uint64_t(next)},
                            {0.5}, 500.0);
    }
    for (std::uint64_t s = 0; s < 2000; ++s)
        w.freight.dispatch(s + 1, 100 + (s % kSettlements), "ore", 2.0);

    // Fleets: 200, half per faction pair, moving.
    w.warfare.define_class([] {
        ShipClass c;
        c.id = "class.destroyer";
        c.attack = 2.0;
        c.hull = 10.0;
        c.speed = 4.0;
        c.interdiction = 0.05;
        return c;
    }());
    for (std::uint64_t i = 0; i < 200; ++i) {
        w.warfare.add_fleet(i + 1, (i % 4) + 1, double(i % 50),
                          double(i / 50));
        w.warfare.add_ships(i + 1, "class.destroyer", 30.0 + (i % 20));
        FleetOrder o;
        o.kind = FleetOrderKind::Move;
        o.target_x = 200.0 - double(i % 50);
        o.target_y = 200.0 - double(i / 50);
        w.warfare.set_order(i + 1, o);
    }

    // 50 factions x 6 strategy actions.
    w.factions.resize(50);
    for (int f = 0; f < 50; ++f) {
        for (int a = 0; a < 6; ++a) {
            UtilityAction act;
            act.id = "f" + std::to_string(f) + ".a" + std::to_string(a);
            act.domain = "strategy";
            act.score = [f, a] { return double((f * 7 + a) % 11) / 11.0; };
            w.factions[f].add_action(std::move(act));
        }
    }
}

std::uint64_t run_scenario() {
    World w;
    build_world(w);
    Checksum sum;

    SimulationExecutor exec;
    // Colony production — every tick.
    exec.add(1, SimulationTask{[&](const SimulationTickContext& c) {
                                   for (auto& s : w.settlements) {
                                       ColonyInputs in;
                                       in.workers_available =
                                           s.pop.workforce();
                                       in.maintenance = 0.9;
                                       Inventory stock;
                                       stock.add("parts", s.stored_parts);
                                       stock.add("ore", 1e6);
                                       in.stockpile = &stock;
                                       s.colony.advance(
                                           double(c.elapsed_ticks), in);
                                       s.stored_parts = stock.quantity("parts");
                                   }
                               },
                               SimulationTier::Active, JobPriority::Normal,
                               "colony", {}});
    // Population — every 4 ticks.
    exec.add(2, SimulationTask{[&](const SimulationTickContext& c) {
                                   for (auto& s : w.settlements) {
                                       SettlementConditions cond;
                                       cond.jobs_available =
                                           s.colony.jobs_total();
                                       cond.housing_ratio = std::min(
                                           1.0, s.colony.housing_capacity() /
                                                    (s.pop.total() + 1.0));
                                       cond.food_ratio = std::min(
                                           1.0, s.stored_food /
                                                    (s.pop.food_demand_per_day() *
                                                         double(c.elapsed_ticks) +
                                                     1.0));
                                       s.pop.advance(double(c.elapsed_ticks),
                                                     cond);
                                   }
                               },
                               SimulationTier::Normal, JobPriority::Normal,
                               "population", {1}});
    // Power grids — every 2 ticks.
    exec.add(3, SimulationTask{[&](const SimulationTickContext& c) {
                                   for (auto& g : w.grids)
                                       g.advance(double(c.elapsed_ticks));
                               },
                               SimulationTier::Nearby, JobPriority::Normal,
                               "power", {}});
    // Freight — every 16 ticks.
    exec.add(4, SimulationTask{[&](const SimulationTickContext& c) {
                                   auto r = w.freight.advance(
                                       double(c.elapsed_ticks));
                                   for (const auto& d : r.deliveries)
                                       sum.add(d.quantity);
                               },
                               SimulationTier::Background,
                               JobPriority::Low, "logistics", {}});
    // Fleets — every 2 ticks.
    exec.add(5, SimulationTask{[&](const SimulationTickContext& c) {
                                   w.warfare.advance(double(c.elapsed_ticks));
                               },
                               SimulationTier::Nearby, JobPriority::Normal,
                               "fleets", {}});
    // Faction AI — every 16 ticks.
    exec.add(6, SimulationTask{[&](const SimulationTickContext& c) {
                                   for (auto& m : w.factions)
                                       m.decide("strategy", double(c.tick));
                               },
                               SimulationTier::Background,
                               JobPriority::Low, "ai", {}});

    std::vector<double> tick_ns;
    tick_ns.reserve(kTicks);
    for (int t = 0; t < kTicks; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        exec.advance();
        const auto t1 = std::chrono::steady_clock::now();
        tick_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    // State checksum: population totals + colony operating + positions.
    for (const auto& s : w.settlements) {
        sum.add(s.pop.total());
        sum.add(s.pop.average_happiness());
        if (const auto* st = s.colony.structure(3)) sum.add(st->operating);
    }
    for (const auto* f : w.warfare.fleets()) {
        sum.add(f->x);
        sum.add(f->y);
    }
    for (const auto& g : w.grids) {
        for (const auto id : g.node_ids()) sum.add(g.node(id)->storage);
    }

    const double mean =
        std::accumulate(tick_ns.begin(), tick_ns.end(), 0.0) / kTicks;
    std::vector<double> sorted = tick_ns;
    std::sort(sorted.begin(), sorted.end());
    const double p50 = sorted[kTicks / 2];
    const double p95 = sorted[size_t(kTicks * 0.95)];
    std::cout << "combined: " << kSettlements << " settlements, 6 task "
              << "domains, " << kTicks << " ticks | mean " << mean / 1e3
              << " us | p50 " << p50 / 1e3 << " us | p95 " << p95 / 1e3
              << " us | checksum " << sum.h << "\n";
    return sum.h;
}

} // namespace

int main() {
    const auto a = run_scenario();
    const auto b = run_scenario();
    check(a == b, "combined scenario runs bit-equal");
    check(a != 0, "checksum non-trivial");
    if (failures == 0) {
        std::cout << "combined scale tests passed\n";
        return 0;
    }
    return 1;
}
