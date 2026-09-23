#include <stellar/engine/colony.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/resource_economy.hpp>
#include <stellar/engine/simulation_executor.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

// Combined strategic simulation benchmark — the milestone-13 shape:
// every "system" is one SimulationExecutor task advancing a Population,
// a Colony, a power FlowNetwork and a ResourceNetwork node; a second
// "logistics" domain advances a shared galactic freight web. Exercises
// the full engine stack composition under tier cadences, dirty wakeups
// and JobSystem parallel waves. Serial and parallel runs must produce
// identical checksums.
//
// Usage: stellar_strategic_sim_tests <systems> [ticks]

namespace {

using namespace stellar::engine;
using Key = SimulationExecutor::Key;

// One simulated system: the aggregate a 4X campaign would own per world.
struct SystemState {
    Population population;
    Colony colony;
    Inventory stockpile{10000.0};
    FlowNetwork power{"power"};
    double checksum{0.0};
};

struct Scenario {
    SimulationExecutor exec;
    std::vector<std::unique_ptr<SystemState>> systems;
    LogisticsNetwork freight;
};

constexpr std::size_t tier_count =
    static_cast<std::size_t>(SimulationTier::Count);

SimulationTier tier_for(std::size_t i) {
    // 2% Active, 8% Nearby, 30% Normal, 40% Background, 20% Dormant.
    const std::size_t mod = i % 50;
    if (mod == 0) return SimulationTier::Active;
    if (mod < 5) return SimulationTier::Nearby;
    if (mod < 20) return SimulationTier::Normal;
    if (mod < 40) return SimulationTier::Background;
    return SimulationTier::Dormant;
}

std::unique_ptr<SystemState> make_system(std::uint64_t id) {
    auto s = std::make_unique<SystemState>();
    DemographicProfile profile;
    profile.id = "species.human";
    s->population.define_profile(profile);
    CohortKey miners;
    miners.profile = "species.human";
    miners.occupation = "miner";
    s->population.add(miners, 5000.0 + static_cast<double>(id % 7) * 500.0);
    CohortKey techs;
    techs.profile = "species.human";
    techs.occupation = "technician";
    techs.education = EducationLevel::Skilled;
    s->population.add(techs, 2000.0);

    DistrictSpec industrial;
    industrial.id = "district.industrial";
    industrial.build_days = 10.0;
    s->colony.define_district(industrial);
    StructureSpec plant;
    plant.id = "structure.power_plant";
    plant.jobs = 50.0;
    plant.utility_supply_per_day = {{"power", 100.0}};
    s->colony.define_structure(plant);
    StructureSpec factory;
    factory.id = "structure.factory";
    factory.district = "district.industrial";
    factory.jobs = 200.0;
    factory.utility_demand_per_day = {{"power", 40.0}};
    factory.inputs_per_day = {{"res.ore", 4.0}};
    factory.outputs_per_day = {{"res.goods", 2.0}};
    s->colony.define_structure(factory);
    s->colony.build_district(1, "district.industrial");
    s->colony.build_structure(2, "structure.power_plant");
    s->colony.build_structure(3, "structure.factory", 1);

    s->power.add_node(1, 120.0, 0.0);   // generator
    s->power.add_node(2, 0.0, 60.0);    // industry
    s->power.add_edge(1, 1, 2, 200.0);
    s->stockpile.add("res.ore", 500.0);
    return s;
}

Scenario build_scenario(std::size_t systems) {
    Scenario scenario;
    scenario.systems.reserve(systems);
    for (std::size_t i = 0; i < systems; ++i)
        scenario.freight.add_node(static_cast<std::uint64_t>(i) + 1);
    // Ring topology freight web: system i -> i+1, 5-day transit.
    for (std::size_t i = 0; i + 1 < systems; ++i)
        scenario.freight.add_route(static_cast<std::uint64_t>(i) + 1,
                                   {static_cast<std::uint64_t>(i) + 1,
                                    static_cast<std::uint64_t>(i) + 2},
                                   {5.0}, 1000.0);

    for (std::size_t i = 0; i < systems; ++i) {
        scenario.systems.push_back(make_system(static_cast<std::uint64_t>(i)));
        auto* sys = scenario.systems.back().get();
        SimulationTask task;
        task.tier = tier_for(i);
        task.domain = "system";
        task.run = [sys](const SimulationTickContext& ctx) {
            const double days =
                static_cast<double>(ctx.elapsed_ticks) * 1.0; // 1d/tick
            SettlementConditions cond;
            cond.jobs_available = sys->colony.jobs_total();
            cond.food_ratio = 0.95;
            cond.housing_ratio = 0.9;
            sys->population.advance(days, cond);
            ColonyInputs inputs;
            inputs.workers_available = sys->population.workforce();
            inputs.stockpile = &sys->stockpile;
            const ColonyDelta delta = sys->colony.advance(days, inputs);
            const FlowAdvanceResult flow = sys->power.advance(days);
            sys->checksum += sys->population.total() + delta.jobs_filled +
                             flow.total_served +
                             sys->stockpile.quantity("res.goods");
        };
        scenario.exec.add(static_cast<Key>(i) + 1, std::move(task));
    }

    // Logistics domain task advances the shared freight web and dispatches
    // a deterministic periodic shipment on every hundredth route.
    SimulationTask freight;
    freight.tier = SimulationTier::Normal;
    freight.domain = "logistics";
    freight.run = [&net = scenario.freight](const SimulationTickContext& ctx) {
        const auto advance = net.advance(static_cast<double>(ctx.elapsed_ticks));
        for (const auto& d : advance.deliveries) (void)d;
        if (ctx.tick % 5 == 0) {
            const std::uint64_t route_id =
                static_cast<std::uint64_t>(ctx.tick / 5) % 100;
            net.dispatch(1000000 + static_cast<std::uint64_t>(ctx.tick),
                         route_id + 1, "res.goods", 10.0);
        }
    };
    scenario.exec.add(static_cast<Key>(systems) + 1, std::move(freight));
    return scenario;
}

double checksum(const Scenario& s) {
    double sum = 0.0;
    for (const auto& sys : s.systems) sum += sys->checksum;
    return sum;
}

std::uint64_t percentile(std::vector<std::uint64_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(
        std::clamp(p * static_cast<double>(v.size() - 1), 0.0,
                   static_cast<double>(v.size() - 1)))];
}

void report(const char* mode, const std::vector<std::uint64_t>& times) {
    const double mean =
        std::accumulate(times.begin(), times.end(), 0.0) /
        static_cast<double>(times.size());
    std::cout << "  " << mode << " mean " << mean / 1e3 << " us, p95 "
              << percentile(times, 0.95) / 1e3 << " us, p99 "
              << percentile(times, 0.99) / 1e3 << " us, peak "
              << *std::max_element(times.begin(), times.end()) / 1e3
              << " us\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <systems> [ticks]\n";
        return 2;
    }
    const std::size_t systems = std::stoull(argv[1]);
    const int ticks = argc > 2 ? std::atoi(argv[2]) : 60;

    // Serial run.
    auto a = build_scenario(systems);
    std::vector<std::uint64_t> serial_ns;
    std::size_t total_ran = 0;
    for (int t = 0; t < ticks; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = a.exec.advance();
        serial_ns.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count()));
        total_ran += r.ran;
    }
    const double serial_checksum = checksum(a);

    // Parallel run over JobSystem waves.
    auto b = build_scenario(systems);
    JobSystem jobs;
    std::vector<std::uint64_t> parallel_ns;
    for (int t = 0; t < ticks; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = b.exec.advance_parallel(jobs);
        parallel_ns.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count()));
    }
    const double parallel_checksum = checksum(b);

    const auto tiers = a.exec.tier_counts();
    std::cout << "strategic sim: " << systems << " systems, " << ticks
              << " ticks, tasks ran " << total_ran << "\n";
    report("serial  ", serial_ns);
    report("parallel", parallel_ns);
    std::cout << "  tiers active=" << tiers[0] << " nearby=" << tiers[1]
              << " normal=" << tiers[2] << " background=" << tiers[3]
              << " dormant=" << tiers[4] << "\n";
    if (const auto* st = a.exec.domain_stats("system"))
        std::cout << "  system domain runs=" << st->runs
                  << " mean_ns=" << (st->runs ? st->total_ns / st->runs : 0)
                  << "\n";

    if (serial_checksum != parallel_checksum) {
        std::cerr << "FAIL: serial/parallel checksum mismatch "
                  << serial_checksum << " vs " << parallel_checksum << "\n";
        return 1;
    }
    if (serial_checksum <= 0.0) {
        std::cerr << "FAIL: no simulation work accumulated\n";
        return 1;
    }
    std::cout << "Strategic simulation benchmark passed "
                 "(serial==parallel checksum "
              << serial_checksum << ")\n";
    return 0;
}
