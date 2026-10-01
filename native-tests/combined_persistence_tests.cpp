#include <stellar/engine/colony.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/simulation_executor.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <stellar/engine/warfare.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// Combined civilization-scale persistence: one executor-driven world
// spanning population, colony, infrastructure, logistics, warfare and
// strategic AI — captured mid-run, restored into fresh instances with
// content re-registered, then advanced in lockstep with the original.
// Verifies every domain's continuation is bit-identical, not just each
// framework's round-trip in isolation.

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::engine;

constexpr std::size_t kSettlements = 3;

struct Settlement {
  Population pop;
  Colony colony;
};

struct World {
  std::vector<Settlement> settlements{kSettlements};
  FlowNetwork grid{"power"};
  LogisticsNetwork freight;
  WarfareModel war;
  StrategicMind mind{64};
  SimulationExecutor exec;
  double day{0.0};
  double delivered{0.0};
  int commits{0};
};

// Content definitions — re-registered on load, never persisted.
void define_content(World &w) {
  for (auto &s : w.settlements) {
    DemographicProfile prof;
    prof.id = "species.human";
    s.pop.define_profile(prof);
    DistrictSpec ind;
    ind.id = "district.industrial";
    ind.build_days = 10.0;
    s.colony.define_district(ind);
    StructureSpec plant;
    plant.id = "structure.plant";
    plant.jobs = 10.0;
    plant.utility_supply_per_day = {{"power", 50.0}};
    s.colony.define_structure(plant);
  }
  ShipClass dd;
  dd.id = "class.destroyer";
  dd.attack = 4.0;
  dd.defense = 0.5;
  dd.hull = 10.0;
  dd.speed = 2.0;
  w.war.define_class(dd);
  UtilityAction mine;
  mine.id = "expand.mining";
  mine.domain = "economy";
  mine.score = [] { return 0.8; };
  mine.commit = [&w] { ++w.commits; };
  mine.cooldown_days = 10.0;
  w.mind.add_action(mine);
  UtilityAction research;
  research.id = "research.push";
  research.domain = "economy";
  research.score = [] { return 0.4; };
  w.mind.add_action(research);
}

// Instance state seeded before the simulation starts.
void seed_instances(World &w) {
  for (std::size_t i = 0; i < kSettlements; ++i) {
    CohortKey workers;
    workers.profile = "species.human";
    workers.occupation = "worker";
    w.settlements[i].pop.add(workers, 4000.0 + i * 1500.0);
    w.settlements[i].colony.build_district(10 + i,
                                           "district.industrial");
    w.settlements[i].colony.build_structure(20 + i, "structure.plant",
                                            10 + i);
  }
  w.grid.add_node(1, 12.0, 0.0, 40.0);
  w.grid.add_node(2, 0.0, 5.0, 10.0);
  w.grid.add_node(3, 0.0, 3.0, 10.0);
  w.grid.add_edge(1, 1, 2, 8.0);
  w.grid.add_edge(2, 1, 3, 6.0);
  w.freight.add_node(1);
  w.freight.add_node(2);
  w.freight.add_node(3);
  w.freight.add_route(10, {1, 2}, {2.0}, 20.0);
  w.freight.add_route(11, {2, 3}, {3.0}, 30.0);
  w.war.add_fleet(1, 1, 0.0, 0.0);
  w.war.add_fleet(2, 2, 40.0, 10.0);
  w.war.add_ships(1, "class.destroyer", 30.0, 0.95, 0.2);
  w.war.add_ships(2, "class.destroyer", 20.0, 1.0, 0.0);
  w.war.set_order(1, {FleetOrderKind::Move, 40.0, 10.0});
}

// Executor tasks — re-registered on load; capture/restore only carries
// scheduler state.
void add_tasks(World &w) {
  for (std::size_t i = 0; i < kSettlements; ++i) {
    w.exec.add(static_cast<SimulationExecutor::Key>(10 + i),
               {.run = [&w, i](const SimulationTickContext &ctx) {
                  SettlementConditions conditions;
                  conditions.food_ratio = 0.9;
                  conditions.jobs_available = 10000.0;
                  auto &s = w.settlements[i];
                  s.pop.advance(static_cast<double>(ctx.elapsed_ticks),
                                conditions);
                  ColonyInputs inputs;
                  inputs.workers_available = s.pop.total();
                  s.colony.advance(
                      static_cast<double>(ctx.elapsed_ticks), inputs);
                },
                .tier = i == 2 ? SimulationTier::Background
                               : SimulationTier::Normal,
                .domain = "settlement"});
  }
  w.exec.add(20, {.run = [&w](const SimulationTickContext &ctx) {
                    w.grid.advance(
                        static_cast<double>(ctx.elapsed_ticks));
                  },
                  .tier = SimulationTier::Active,
                  .domain = "infrastructure"});
  w.exec.add(21, {.run = [&w](const SimulationTickContext &ctx) {
                    w.delivered += static_cast<double>(
                        w.freight
                            .advance(static_cast<double>(
                                ctx.elapsed_ticks))
                            .deliveries.size());
                  },
                  .tier = SimulationTier::Active,
                  .domain = "logistics"});
  w.exec.add(22, {.run = [&w](const SimulationTickContext &ctx) {
                    w.war.advance(
                        static_cast<double>(ctx.elapsed_ticks));
                  },
                  .tier = SimulationTier::Normal,
                  .domain = "warfare"});
  w.exec.add(23, {.run = [&w](const SimulationTickContext &) {
                    w.day += 1.0;
                    w.mind.decide("economy", w.day);
                  },
                  .tier = SimulationTier::Active, .domain = "strategy"});
}

// Dispatch a shipment every few ticks so logistics has in-flight work
// at capture time — driven from the harness, not a task, so both
// worlds receive the identical sequence.
void tick(World &w) {
  w.exec.advance();
  const auto tick = w.exec.scheduler().tick();
  if (tick % 4 == 1)
    w.freight.dispatch(1000 + tick, 10, "res.ore", 5.0);
  if (tick % 7 == 3)
    w.freight.dispatch(2000 + tick, 11, "res.parts", 3.0);
}

std::uint64_t fnv(std::uint64_t h, double v) {
  std::uint64_t bits;
  std::memcpy(&bits, &v, 8);
  for (int i = 0; i < 8; ++i) {
    h ^= (bits >> (i * 8)) & 0xff;
    h *= 1099511628211ull;
  }
  return h;
}

std::uint64_t world_checksum(const World &w) {
  std::uint64_t h = 1469598103934665603ull;
  for (const auto &s : w.settlements) {
    h = fnv(h, s.pop.total());
    h = fnv(h, static_cast<double>(s.colony.structure_count()));
    h = fnv(h, static_cast<double>((s.colony.district(10) ? 1.0 : 0.0) + (s.colony.district(11) ? 1.0 : 0.0) + (s.colony.district(12) ? 1.0 : 0.0)));
  }
  for (const auto id : w.grid.node_ids())
    if (const auto *n = w.grid.node(id)) h = fnv(h, n->storage);
  h = fnv(h, w.freight.now());
  h = fnv(h, static_cast<double>(w.freight.in_transit().size()));
  h = fnv(h, w.delivered);
  for (const auto *f : w.war.fleets()) {
    h = fnv(h, f->x);
    h = fnv(h, f->y);
    for (const auto *c : w.war.cohorts(f->id)) h = fnv(h, c->count);
  }
  h = fnv(h, w.day);
  h = fnv(h, static_cast<double>(w.commits));
  h = fnv(h, static_cast<double>(w.exec.scheduler().tick()));
  return h;
}

} // namespace

int main() {
  World a;
  define_content(a);
  seed_instances(a);
  add_tasks(a);

  // Mid-run capture: advance far enough that shipments are in flight,
  // districts under construction and fleets mid-transit.
  for (int i = 0; i < 20; ++i) tick(a);

  std::vector<Population::State> pop_states;
  std::vector<Colony::State> colony_states;
  for (const auto &s : a.settlements) {
    pop_states.push_back(s.pop.capture_state());
    colony_states.push_back(s.colony.capture_state());
  }
  const auto grid_state = a.grid.capture_state();
  const auto freight_state = a.freight.capture_state();
  const auto war_state = a.war.capture_state();
  const auto mind_state = a.mind.capture_state();
  const auto exec_state = a.exec.capture_state();
  const auto day = a.day;
  const auto delivered = a.delivered;
  const auto commits = a.commits;

  // Restore into a fresh world: content re-registered, tasks rebound.
  World b;
  define_content(b);
  add_tasks(b);
  for (std::size_t i = 0; i < kSettlements; ++i) {
    b.settlements[i].pop.restore_state(pop_states[i]);
    b.settlements[i].colony.restore_state(colony_states[i]);
  }
  b.grid.restore_state(grid_state);
  b.freight.restore_state(freight_state);
  b.war.restore_state(war_state);
  b.mind.restore_state(mind_state);
  const auto skipped = b.exec.restore_state(exec_state);
  check(skipped.empty(),
        "executor restore skipped no registered task keys");
  b.day = day;
  b.delivered = delivered;
  b.commits = commits;

  check(world_checksum(a) == world_checksum(b),
        "restored world state matches at capture point");

  // Lockstep continuation — every domain must stay bit-identical.
  for (int i = 0; i < 60; ++i) {
    tick(a);
    tick(b);
  }
  check(world_checksum(a) == world_checksum(b),
        "restored world continues deterministically across all domains");

  // Spot-check the domains individually for a useful failure message.
  for (std::size_t i = 0; i < kSettlements; ++i) {
    check(a.settlements[i].pop.total() == b.settlements[i].pop.total(),
          "population continuation identical");
    check(a.settlements[i].colony.structure_count() ==
              b.settlements[i].colony.structure_count(),
          "colony continuation identical");
  }
  check(a.freight.in_transit().size() == b.freight.in_transit().size(),
        "in-flight shipments identical");
  check(a.war.fleet(1) && b.war.fleet(1) &&
            a.war.fleet(1)->x == b.war.fleet(1)->x,
        "fleet positions identical");
  check(a.mind.journal().size() == b.mind.journal().size(),
        "decision journal identical");

  if (failures != 0) {
    std::cerr << failures << " combined persistence checks failed\n";
    return 1;
  }
  std::cout << "combined persistence test passed "
               "(population/colony/flow/logistics/warfare/ai/executor)\n";
  return 0;
}
