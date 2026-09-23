// JSON codec round-trip coverage for framework_state_json.hpp: every
// specialization framework's capture_state() serializes to JSON and back
// losslessly, and a restore through the codec produces a framework whose
// re-captured state serializes identically.
#include <stellar/engine/framework_state_json.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace stellar::engine;
using Json = nlohmann::ordered_json;

namespace {

int failures = 0;
void require(bool ok, const char *what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << "\n";
    ++failures;
  }
}

template <typename T> std::string round_trip(const T &state) {
  Json j = state;
  return j.dump();
}

template <typename T> T parse_state(const std::string &text) {
  return Json::parse(text).template get<T>();
}

// Serializes `before` and `after` and compares the byte form — JSON text
// is the persisted representation, so equality there is the guarantee
// campaign saves need.
template <typename T> void check_codec(const T &state, const char *what) {
  const std::string encoded = round_trip(state);
  const T decoded = parse_state<T>(encoded);
  const std::string reencoded = round_trip(decoded);
  require(encoded == reencoded, what);
}

void test_scheduler_codec() {
  SimulationExecutor executor;
  std::size_t runs = 0;
  executor.add(7, {.run = [](const SimulationTickContext &) {},
                   .tier = SimulationTier::Nearby});
  executor.add(3, {.run = [&runs](const SimulationTickContext &) { ++runs; },
                   .tier = SimulationTier::Active});
  executor.add(9, {.run = [](const SimulationTickContext &) {},
                   .tier = SimulationTier::Dormant});
  executor.mark_dirty(9);
  executor.wake(7);
  for (int i = 0; i < 3; ++i) executor.advance();

  check_codec(executor.capture_state(), "executor state codec");

  // Restore through the codec into a fresh executor.
  SimulationExecutor restored;
  restored.add(7, {.run = [](const SimulationTickContext &) {},
                   .tier = SimulationTier::Nearby});
  restored.add(3, {.run = [&runs](const SimulationTickContext &) { ++runs; },
                   .tier = SimulationTier::Active});
  restored.add(9, {.run = [](const SimulationTickContext &) {},
                   .tier = SimulationTier::Dormant});
  restored.restore_state(
      parse_state<SimulationExecutor::State>(round_trip(executor.capture_state())));
  check_codec(restored.capture_state(), "restored executor codec");
}

void test_population_codec() {
  Population pop;
  pop.define_profile({.id = "human"});
  pop.define_profile({.id = "vorlag", .base_fertility_per_year = 0.05});
  pop.add({.profile = "human", .occupation = "miner"}, 1000.0);
  pop.add({.profile = "vorlag", .culture = "clans"}, 400.0);
  SettlementConditions conditions;
  conditions.food_ratio = 0.9;
  pop.advance(30.0, conditions);

  check_codec(pop.capture_state(), "population state codec");

  Population restored;
  restored.define_profile({.id = "human"});
  restored.define_profile({.id = "vorlag", .base_fertility_per_year = 0.05});
  restored.restore_state(
      parse_state<Population::State>(round_trip(pop.capture_state())));
  check_codec(restored.capture_state(), "restored population codec");
}

void test_colony_codec() {
  Colony colony;
  colony.define_district({.id = "residential", .structure_slots = 4});
  colony.define_structure({.id = "hab"});
  colony.define_structure({.id = "lab", .district = "residential"});
  colony.build_district(1, "residential");
  colony.build_structure(2, "hab");
  colony.build_structure(3, "lab", 1);
  colony.advance(6.0, {});

  check_codec(colony.capture_state(), "colony state codec");

  Colony restored;
  restored.define_district({.id = "residential", .structure_slots = 4});
  restored.define_structure({.id = "hab"});
  restored.define_structure({.id = "lab", .district = "residential"});
  restored.restore_state(
      parse_state<Colony::State>(round_trip(colony.capture_state())));
  check_codec(restored.capture_state(), "restored colony codec");
}

void test_flow_codec() {
  FlowNetwork net("power");
  net.add_node(1, 10.0, 0.0, 20.0);
  net.add_node(2, 0.0, 4.0, 8.0);
  net.add_edge(1, 1, 2, 5.0);
  net.advance(2.0);

  check_codec(net.capture_state(), "flow state codec");

  FlowNetwork restored("power");
  restored.restore_state(
      parse_state<FlowNetwork::State>(round_trip(net.capture_state())));
  check_codec(restored.capture_state(), "restored flow codec");
}

void test_logistics_codec() {
  LogisticsNetwork net;
  net.add_node(1);
  net.add_node(2);
  net.add_node(3);
  net.add_route(10, {1, 2, 3}, {2.0, 3.0}, 50.0);
  net.dispatch(100, 10, "ore", 20.0);
  net.advance(1.0);
  net.dispatch(101, 10, "food", 10.0);

  check_codec(net.capture_state(), "logistics state codec");

  LogisticsNetwork restored;
  restored.restore_state(
      parse_state<LogisticsNetwork::State>(round_trip(net.capture_state())));
  check_codec(restored.capture_state(), "restored logistics codec");
}

void test_warfare_codec() {
  WarfareModel war;
  war.define_class({.id = "frigate", .attack = 5.0, .hull = 20.0});
  war.define_class({.id = "cruiser", .attack = 15.0, .hull = 80.0});
  war.add_fleet(1, 7, 0.0, 0.0);
  war.add_ships(1, "frigate", 10.0, 0.9, 0.3);
  war.add_ships(1, "cruiser", 3.0);
  war.add_fleet(2, 9, 100.0, 50.0);
  war.add_ships(2, "frigate", 5.0);
  war.set_order(1, {.kind = FleetOrderKind::Move, .target_x = 50.0});
  war.advance(1.0);

  check_codec(war.capture_state(), "warfare state codec");

  WarfareModel restored;
  restored.define_class({.id = "frigate", .attack = 5.0, .hull = 20.0});
  restored.define_class({.id = "cruiser", .attack = 15.0, .hull = 80.0});
  restored.restore_state(
      parse_state<WarfareModel::State>(round_trip(war.capture_state())));
  check_codec(restored.capture_state(), "restored warfare codec");
}

void test_strategic_ai_codec() {
  StrategicMind mind(8);
  mind.add_action({.id = "expand.mining",
                   .domain = "expansion",
                   .score = []() { return 0.8; },
                   .commit = []() {},
                   .cooldown_days = 30.0});
  mind.add_action({.id = "research.push",
                   .domain = "expansion",
                   .score = []() { return 0.5; },
                   .commit = []() {}});
  mind.decide("expansion", 100.0);

  check_codec(mind.capture_state(), "strategic ai state codec");

  StrategicMind restored(8);
  restored.restore_state(
      parse_state<StrategicMind::State>(round_trip(mind.capture_state())));
  check_codec(restored.capture_state(), "restored strategic ai codec");
}

void test_economy_codec() {
  ResourceNetwork net;
  net.define({.id = "ore"});
  net.define({.id = "metal"});
  net.add_recipe({.id = "smelt",
                  .inputs = {{"ore", 2.0}},
                  .outputs = {{"metal", 1.0}},
                  .duration_days = 2.0});
  auto &a = net.add_node(1, 100.0);
  a.inventory.add("ore", 50.0);
  net.add_node(2, 50.0);
  net.add_producer(1, "smelt");
  net.transfer(1, 2, "ore", 20.0, 5.0);
  net.advance(1.0);

  check_codec(net.capture_state(), "economy state codec");

  ResourceNetwork restored;
  restored.define({.id = "ore"});
  restored.define({.id = "metal"});
  restored.add_recipe({.id = "smelt",
                       .inputs = {{"ore", 2.0}},
                       .outputs = {{"metal", 1.0}},
                       .duration_days = 2.0});
  restored.restore_state(
      parse_state<ResourceNetwork::State>(round_trip(net.capture_state())));
  check_codec(restored.capture_state(), "restored economy codec");
}

void test_history_codec() {
  EventHistory h;
  HistoryEvent a;
  a.category = "colony.founded";
  a.at_day = 10.0;
  a.significance = 0.8;
  a.actors = {7};
  a.location = 11;
  a.tags = {"colony:101"};
  h.record(a);
  HistoryEvent b;
  b.category = "espionage.op";
  b.at_day = 11.0;
  b.significance = 0.9;
  b.visible_to = {7};
  h.record(b);

  check_codec(h.capture_state(), "history state codec");

  EventHistory restored;
  restored.restore_state(
      parse_state<EventHistory::State>(round_trip(h.capture_state())));
  check_codec(restored.capture_state(), "restored history codec");
  require(restored.size() == 2 && restored.next_id() == h.next_id(),
          "restored history keeps records and id counter");
}

void test_physics_codec() {
  PhysicsWorld world{128.0f};
  const auto mover = world.add_body(
      {PhysicsShapeKind::Circle, 10.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
  world.set_velocity(mover, 20.0f, 0.0f);
  [[maybe_unused]] const auto zone = world.add_body(
      {PhysicsShapeKind::Circle, 40.0f, 0.0f, 0.0f}, 60.0f, 0.0f, 1, true);
  [[maybe_unused]] const auto wall = world.add_body(
      {PhysicsShapeKind::Aabb, 0.0f, 8.0f, 8.0f}, 200.0f, 0.0f);
  for (int i = 0; i < 20; ++i) {
    [[maybe_unused]] const auto events = world.advance(0.25f);
  } // enter the zone

  check_codec(world.capture_state(), "physics state codec");

  PhysicsWorld restored{64.0f};
  restored.restore_state(
      parse_state<PhysicsWorld::State>(round_trip(world.capture_state())));
  check_codec(restored.capture_state(), "restored physics codec");
  require(restored.size() == world.size(),
          "restored physics keeps bodies");
  // The overlap set survived the codec: no ENTER refires.
  const auto events = restored.advance(0.25f);
  require(events.empty(), "restored physics emits no phantom ENTER");
}

void test_negative_cases() {
  bool threw = false;
  try {
    parse_state<Population::State>(R"({"version":99,"cohorts":[]})");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  require(threw, "unsupported version rejected");

  threw = false;
  try {
    parse_state<FlowNetwork::State>(
        R"({"version":1,"resource":"power","nodes":[],"edges":[]})");
  } catch (...) {
    threw = true;
  }
  require(!threw, "minimal valid state parses");

  threw = false;
  try {
    Json bad = R"({"version":1,"tick":0,"items":[{"key":1,"tier":99}]})"_json;
    (void)bad.get<SimulationScheduler::State>();
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  require(threw, "out-of-range tier rejected");
}

} // namespace

int main() {
  test_scheduler_codec();
  test_population_codec();
  test_colony_codec();
  test_flow_codec();
  test_logistics_codec();
  test_warfare_codec();
  test_strategic_ai_codec();
  test_economy_codec();
  test_history_codec();
  test_physics_codec();
  test_negative_cases();

  if (failures != 0) {
    std::cerr << failures << " framework state codec checks failed\n";
    return 1;
  }
  std::cout << "Framework state JSON codec tests passed "
               "(scheduler/population/colony/flow/logistics/warfare/ai/"
               "economy/history)\n";
  return 0;
}
