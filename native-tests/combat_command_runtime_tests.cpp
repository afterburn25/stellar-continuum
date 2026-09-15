#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stellar/core/combat_command_runtime.hpp>
#include <type_traits>
using Json = nlohmann::json;
using namespace stellar::core;
static_assert(std::is_move_constructible_v<CombatCommandRuntime>);
static_assert(!std::is_copy_constructible_v<CombatCommandRuntime>);
namespace {
[[noreturn]] void fail(const std::string &m) { throw std::runtime_error(m); }
void check(bool c, const std::string &m) {
  if (!c)
    fail(m);
}
double num(const Json &v) {
  if (v.is_number())
    return v.get<double>();
  auto s = v.get<std::string>();
  if (s == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (s == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (s == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("named number");
}
Json enc(double v) {
  if (std::isnan(v))
    return "NaN";
  if (v == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (v == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return v;
}
template <class T> std::optional<T> opt(const Json &v) {
  return v.is_null() ? std::nullopt : std::optional<T>(v.get<T>());
}
Vec2 vec(const Json &v) {
  return {static_cast<float>(num(v.at("X"))),
          static_cast<float>(num(v.at("Y")))};
}
Json evec(Vec2 v) { return {{"X", enc(v.x)}, {"Y", enc(v.y)}}; }
void eq(const Json &a, const Json &e, const std::string &f) {
  if (a.is_number_integer() && e.is_number_integer()) {
    check(a == e, f + " integer value");
    return;
  }
  if (a.is_number() && e.is_number()) {
    auto x = a.get<double>(), y = e.get<double>();
    check(std::abs(x - y) <= 1e-7 * std::max({1., std::abs(x), std::abs(y)}),
          f);
    return;
  }
  check(a.type() == e.type(), f + " type");
  if (a.is_array()) {
    check(a.size() == e.size(), f + " size");
    for (size_t i = 0; i < a.size(); ++i)
      eq(a[i], e[i], f + "[]");
  } else if (a.is_object()) {
    check(a.size() == e.size(), f + " object size");
    for (auto i = e.begin(); i != e.end(); ++i) {
      check(a.contains(i.key()), f + " missing " + i.key());
      eq(a.at(i.key()), i.value(), f + "." + i.key());
    }
  } else
    check(a == e, f + " value");
}
StellarSystem system(const Json &v) {
  StellarSystem s;
  s.id = v.at("Id");
  s.name = v.at("Name");
  auto p = vec(v.at("Position"));
  s.position = {p.x, p.y, opt<double>(v.at("GalacticDepthLightYears"))};
  s.archetype = static_cast<StarArchetype>(v.at("Archetype").get<int>());
  s.has_habitable_world = v.at("HasHabitableWorld");
  s.has_anomaly = v.at("HasAnomaly");
  s.has_rare_resource = v.at("HasRareResource");
  s.has_pre_warp_civilization = v.at("HasPreWarpCivilization");
  s.catalog_preset_id = opt<std::string>(v.at("CatalogPresetId"));
  s.stellar_catalog_id = opt<std::string>(v.at("StellarCatalogId"));
  return s;
}
FleetCombatState combat(const Json &v) {
  return {v.at("ProfileId"),
          num(v.at("Shields")),
          num(v.at("Armor")),
          num(v.at("Hull")),
          num(v.at("WeaponCooldownRemainingDays")),
          static_cast<MilitaryOrderType>(v.at("Order").get<int>()),
          opt<int>(v.at("TargetFleetId")),
          opt<int>(v.at("DefendSystemId")),
          num(v.at("RetreatProgressDays")),
          v.at("RetreatStarted"),
          v.at("IsDisengaged"),
          opt<int>(v.at("DisengagedSystemId"))};
}
Json ecombat(const FleetCombatState &s) {
  return {
      {"ProfileId", s.profile_id},
      {"Shields", enc(s.shields)},
      {"Armor", enc(s.armor)},
      {"Hull", enc(s.hull)},
      {"WeaponCooldownRemainingDays", enc(s.weapon_cooldown_remaining_days)},
      {"Order", static_cast<int>(s.order)},
      {"TargetFleetId", s.target_fleet_id},
      {"DefendSystemId", s.defend_system_id},
      {"RetreatProgressDays", enc(s.retreat_progress_days)},
      {"RetreatStarted", s.retreat_started},
      {"IsDisengaged", s.is_disengaged},
      {"DisengagedSystemId", s.disengaged_system_id}};
}
MassiveVesselState vessel(const Json &v) {
  MassiveVesselState r;
  r.id = v.at("Id");
  r.name = v.at("Name");
  r.design_id = v.at("DesignId");
  r.is_flagship = v.at("IsFlagship");
  r.is_carrier = v.at("IsCarrier");
  r.is_interdictor = v.at("IsInterdictor");
  r.is_story_ship = v.at("IsStoryShip");
  r.hull_fraction = static_cast<float>(num(v.at("HullFraction")));
  r.engine_fraction = static_cast<float>(num(v.at("EngineFraction")));
  r.sensor_fraction = static_cast<float>(num(v.at("SensorFraction")));
  r.warp_drive_fraction = static_cast<float>(num(v.at("WarpDriveFraction")));
  r.reactor_fraction = static_cast<float>(num(v.at("ReactorFraction")));
  r.interdictor_fraction = static_cast<float>(num(v.at("InterdictorFraction")));
  r.battles_fought = v.at("BattlesFought");
  r.confirmed_kills = v.at("ConfirmedKills");
  r.destroyed = v.at("Destroyed");
  r.escaped = v.at("Escaped");
  return r;
}
Json evessel(const MassiveVesselState &r) {
  return {{"Id", r.id},
          {"Name", r.name},
          {"DesignId", r.design_id},
          {"IsFlagship", r.is_flagship},
          {"IsCarrier", r.is_carrier},
          {"IsInterdictor", r.is_interdictor},
          {"IsStoryShip", r.is_story_ship},
          {"HullFraction", enc(r.hull_fraction)},
          {"EngineFraction", enc(r.engine_fraction)},
          {"SensorFraction", enc(r.sensor_fraction)},
          {"WarpDriveFraction", enc(r.warp_drive_fraction)},
          {"ReactorFraction", enc(r.reactor_fraction)},
          {"InterdictorFraction", enc(r.interdictor_fraction)},
          {"BattlesFought", r.battles_fought},
          {"ConfirmedKills", r.confirmed_kills},
          {"Destroyed", r.destroyed},
          {"Escaped", r.escaped}};
}
FleetState fleet(const Json &v) {
  FleetState f;
  f.id = v.at("Id");
  f.civilization_id = v.at("CivilizationId");
  f.name = v.at("Name");
  f.role = static_cast<FleetRole>(v.at("Role").get<int>());
  f.design_id = opt<std::string>(v.at("DesignId"));
  f.position = vec(v.at("Position"));
  f.current_system_id = opt<int>(v.at("CurrentSystemId"));
  f.destination_system_id = opt<int>(v.at("DestinationSystemId"));
  f.transit_phase =
      static_cast<FleetTransitPhase>(v.at("TransitPhase").get<int>());
  f.transit_origin_system_id = opt<int>(v.at("TransitOriginSystemId"));
  f.transit_target_system_id = opt<int>(v.at("TransitTargetSystemId"));
  f.transit_progress = num(v.at("TransitProgress"));
  f.local_transit_start = vec(v.at("LocalTransitStart"));
  f.local_transit_position = vec(v.at("LocalTransitPosition"));
  f.local_transit_target = vec(v.at("LocalTransitTarget"));
  f.planned_route_system_ids =
      v.at("PlannedRouteSystemIds").get<std::vector<int>>();
  f.hold_requested = v.at("HoldRequested");
  f.return_to_base_requested = v.at("ReturnToBaseRequested");
  f.return_to_base_failure_reason =
      opt<std::string>(v.at("ReturnToBaseFailureReason"));
  f.mission_order_revision = v.at("MissionOrderRevision");
  f.destination_planetary_body_id =
      opt<int>(v.at("DestinationPlanetaryBodyId"));
  f.prevent_automatic_settlement = v.at("PreventAutomaticSettlement");
  f.settlement_body_id = opt<int>(v.at("SettlementBodyId"));
  f.settlement_days_completed = num(v.at("SettlementDaysCompleted"));
  f.reconnaissance_system_id = opt<int>(v.at("ReconnaissanceSystemId"));
  f.reconnaissance_days_completed = num(v.at("ReconnaissanceDaysCompleted"));
  f.freight_target_outpost_id = opt<int>(v.at("FreightTargetOutpostId"));
  f.freight_home_colony_id = opt<int>(v.at("FreightHomeColonyId"));
  f.cargo_material_capacity = num(v.at("CargoMaterialCapacity"));
  f.cargo_materials = num(v.at("CargoMaterials"));
  f.strategic_speed = num(v.at("StrategicSpeed"));
  f.maximum_leg_range_light_years = num(v.at("MaximumLegRangeLightYears"));
  f.fuel_capacity_light_years = num(v.at("FuelCapacityLightYears"));
  f.fuel_remaining_light_years = num(v.at("FuelRemainingLightYears"));
  f.sensor_range = static_cast<float>(num(v.at("SensorRange")));
  f.is_active = v.at("IsActive");
  f.embarked_population_millions = num(v.at("EmbarkedPopulationMillions"));
  f.embarked_population_species_id =
      opt<std::string>(v.at("EmbarkedPopulationSpeciesId"));
  if (!v.at("Combat").is_null())
    f.combat = combat(v.at("Combat"));
  check(v.at("TacticalLoadout").is_null(), "tactical loadout fixture");
  if (!v.at("TacticalVessel").is_null())
    f.tactical_vessel = vessel(v.at("TacticalVessel"));
  return f;
}
Json efleet(const FleetState &f) {
  return {{"Id", f.id},
          {"CivilizationId", f.civilization_id},
          {"Name", f.name},
          {"Role", static_cast<int>(f.role)},
          {"DesignId", f.design_id},
          {"Position", evec(f.position)},
          {"CurrentSystemId", f.current_system_id},
          {"DestinationSystemId", f.destination_system_id},
          {"TransitPhase", static_cast<int>(f.transit_phase)},
          {"TransitOriginSystemId", f.transit_origin_system_id},
          {"TransitTargetSystemId", f.transit_target_system_id},
          {"TransitProgress", enc(f.transit_progress)},
          {"LocalTransitStart", evec(f.local_transit_start)},
          {"LocalTransitPosition", evec(f.local_transit_position)},
          {"LocalTransitTarget", evec(f.local_transit_target)},
          {"PlannedRouteSystemIds", f.planned_route_system_ids},
          {"HoldRequested", f.hold_requested},
          {"ReturnToBaseRequested", f.return_to_base_requested},
          {"ReturnToBaseFailureReason", f.return_to_base_failure_reason},
          {"MissionOrderRevision", f.mission_order_revision},
          {"DestinationPlanetaryBodyId", f.destination_planetary_body_id},
          {"PreventAutomaticSettlement", f.prevent_automatic_settlement},
          {"SettlementBodyId", f.settlement_body_id},
          {"SettlementDaysCompleted", enc(f.settlement_days_completed)},
          {"ReconnaissanceSystemId", f.reconnaissance_system_id},
          {"ReconnaissanceDaysCompleted", enc(f.reconnaissance_days_completed)},
          {"FreightTargetOutpostId", f.freight_target_outpost_id},
          {"FreightHomeColonyId", f.freight_home_colony_id},
          {"CargoMaterialCapacity", enc(f.cargo_material_capacity)},
          {"CargoMaterials", enc(f.cargo_materials)},
          {"StrategicSpeed", enc(f.strategic_speed)},
          {"MaximumLegRangeLightYears", enc(f.maximum_leg_range_light_years)},
          {"FuelCapacityLightYears", enc(f.fuel_capacity_light_years)},
          {"FuelRemainingLightYears", enc(f.fuel_remaining_light_years)},
          {"SensorRange", enc(f.sensor_range)},
          {"IsActive", f.is_active},
          {"EmbarkedPopulationMillions", enc(f.embarked_population_millions)},
          {"EmbarkedPopulationSpeciesId", f.embarked_population_species_id},
          {"Combat", f.combat ? ecombat(*f.combat) : Json(nullptr)},
          {"TacticalLoadout", nullptr},
          {"TacticalVessel",
           f.tactical_vessel ? evessel(*f.tactical_vessel) : Json(nullptr)}};
}
struct World {
  Json raw_systems;
  std::vector<StellarSystem> systems;
  std::vector<FleetState> fleets;
};
World world(const Json &v) {
  World w;
  w.raw_systems = v.at("Systems");
  for (auto &x : v.at("Systems"))
    w.systems.push_back(system(x));
  for (auto &x : v.at("Fleets"))
    w.fleets.push_back(fleet(x));
  return w;
}
Json eworld(const World &w) {
  Json f = Json::array();
  for (auto &x : w.fleets)
    f.push_back(efleet(x));
  return {{"Systems", w.raw_systems}, {"Fleets", f}};
}
Json event(const CombatEvent &e) {
  return {{"Type", static_cast<int>(e.type)},
          {"SystemId", e.system_id},
          {"ActorCivilizationId", e.actor_civilization_id},
          {"ActorFleetId", e.actor_fleet_id},
          {"TargetCivilizationId", e.target_civilization_id},
          {"TargetFleetId", e.target_fleet_id},
          {"ShieldDamage", enc(e.shield_damage)},
          {"ArmorDamage", enc(e.armor_damage)},
          {"HullDamage", enc(e.hull_damage)},
          {"Message", e.message},
          {"EmbarkedPopulationCasualtiesMillions",
           enc(e.embarked_population_casualties_millions)}};
}
struct Command {
  std::string kind;
  int civ{}, fleet_id{};
  MilitaryOrder order;
  std::vector<int> fleet_ids;
  double days{};
};
Command command(const Json &v) {
  Command c;
  c.kind = v.at("Kind");
  check(c.kind == "Preview" || c.kind == "PreviewBatch" || c.kind == "Issue" ||
            c.kind == "IssueBatch" || c.kind == "Engage" || c.kind == "Advance",
        "unknown command kind");
  c.civ = v.at("CivilizationId");
  c.fleet_id = v.at("FleetId");
  c.order = {static_cast<MilitaryOrderType>(v.at("OrderType").get<int>()),
             opt<int>(v.at("TargetFleetId")), opt<int>(v.at("DefendSystemId"))};
  c.fleet_ids = v.at("FleetIds").get<std::vector<int>>();
  c.days = num(v.at("Days"));
  return c;
}
Json eorder(const MilitaryOrder &order) {
  return {{"Type", static_cast<int>(order.type)},
          {"TargetFleetId", order.target_fleet_id},
          {"DefendSystemId", order.defend_system_id}};
}
Json epreview(const CombatOrderPreview &preview) {
  return {{"CivilizationId", preview.civilization_id},
          {"FleetId", preview.fleet_id},
          {"Order", eorder(preview.order)},
          {"Accepted", preview.accepted},
          {"Message", preview.message}};
}
Json epreview_batch(const CombatBatchOrderPreview &batch) {
  Json results = Json::array();
  for (const auto &result : batch.fleet_results)
    results.push_back({{"FleetId", result.fleet_id},
                       {"Accepted", result.accepted},
                       {"Message", result.message}});
  return {{"RequestedFleetCount", batch.requested_fleet_count},
          {"AcceptedCount", batch.accepted_count},
          {"RejectedCount", batch.rejected_count},
          {"FleetResults", std::move(results)},
          {"AllAccepted", batch.all_accepted()},
          {"AnyAccepted", batch.any_accepted()}};
}
Json eorder_result(const CombatOrderResult &result) {
  return {{"Accepted", result.accepted}, {"Message", result.message}};
}
Json eorder_batch(const CombatBatchOrderResult &batch) {
  Json results = Json::array();
  for (const auto &result : batch.fleet_results)
    results.push_back({{"FleetId", result.fleet_id},
                       {"Accepted", result.accepted},
                       {"Message", result.message}});
  return {{"RequestedFleetCount", batch.requested_fleet_count},
          {"AcceptedCount", batch.accepted_count},
          {"RejectedCount", batch.rejected_count},
          {"FleetResults", std::move(results)},
          {"AllAccepted", batch.all_accepted()},
          {"AnyAccepted", batch.any_accepted()}};
}
void run(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto &arguments = test.at("Arguments");
  World world_state = world(arguments.at("World"));
  std::vector<Command> commands;
  for (const auto &value : arguments.at("Commands"))
    commands.push_back(command(value));
  const bool default_hostile = arguments.at("DefaultHostile");
  const bool use_default_runtime = arguments.at("UseDefaultRuntime");
  const auto outcomes =
      arguments.at("HostilityOutcomes").get<std::vector<bool>>();
  const auto throw_calls = arguments.at("ThrowCalls").get<std::vector<int>>();
  const auto expected = test.at("Result");
  const auto expected_after = test.at("After");
  const auto expected_calls = test.at("HostilityCalls");
  check(expected.size() == commands.size(), name + " result count");
  eq(eworld(world_state), test.at("Before"), name + " before");

  struct Call {
    int first{}, second{};
  };
  std::vector<Call> calls;
  auto policy = [&, call = 0](int first, int second) mutable {
    ++call;
    calls.push_back({first, second});
    if (std::find(throw_calls.begin(), throw_calls.end(), call) !=
        throw_calls.end())
      throw std::runtime_error("Hostility oracle failure.");
    return call <= static_cast<int>(outcomes.size())
               ? outcomes[static_cast<std::size_t>(call - 1)]
               : default_hostile;
  };
  std::unique_ptr<CombatCommandRuntime> runtime =
      use_default_runtime
          ? std::make_unique<CombatCommandRuntime>()
          : std::make_unique<CombatCommandRuntime>(std::move(policy));

  for (std::size_t index = 0; index < commands.size(); ++index) {
    const auto &current = commands[index];
    const auto expected_error = expected[index].at("Error");
    std::string error_type, error_message;
    std::optional<CombatOrderPreview> preview;
    std::optional<CombatBatchOrderPreview> preview_batch;
    std::optional<CombatOrderResult> order_result;
    std::optional<CombatBatchOrderResult> order_batch;
    std::vector<CombatEvent> events;
    try {
      if (current.kind == "Preview")
        preview = runtime->preview_order(
            {world_state.systems, world_state.fleets}, current.civ,
            current.fleet_id, current.order);
      else if (current.kind == "PreviewBatch")
        preview_batch = runtime->preview_orders(
            {world_state.systems, world_state.fleets}, current.civ,
            current.fleet_ids, current.order);
      else if (current.kind == "Issue")
        order_result =
            runtime->issue_order({world_state.systems, world_state.fleets},
                                 current.civ, current.fleet_id, current.order);
      else if (current.kind == "IssueBatch")
        order_batch = runtime->issue_orders(
            {world_state.systems, world_state.fleets}, current.civ,
            current.fleet_ids, current.order);
      else if (current.kind == "Engage")
        order_result = runtime->issue_engage_hostiles(
            {world_state.systems, world_state.fleets}, current.civ,
            current.fleet_id);
      else
        events = runtime->simulation().advance(
            {world_state.systems, world_state.fleets}, current.days);
    } catch (const std::invalid_argument &error) {
      error_type = "ArgumentException";
      error_message = error.what();
    } catch (const std::out_of_range &error) {
      error_type = "ArgumentOutOfRangeException";
      error_message = error.what();
    } catch (const std::runtime_error &error) {
      error_type = "InvalidOperationException";
      error_message = error.what();
    }

    Json actual = nullptr;
    if (preview)
      actual = epreview(*preview);
    else if (preview_batch)
      actual = epreview_batch(*preview_batch);
    else if (order_result)
      actual = eorder_result(*order_result);
    else if (order_batch)
      actual = eorder_batch(*order_batch);
    else if (current.kind == "Advance" && error_type.empty()) {
      actual = Json::array();
      for (const auto &value : events)
        actual.push_back(event(value));
    }
    const Json actual_error =
        error_type.empty()
            ? Json(nullptr)
            : Json{{"Type", error_type}, {"Message", error_message}};
    eq(actual, expected[index].at("Result"),
       name + " result " + std::to_string(index));
    eq(actual_error, expected_error, name + " error " + std::to_string(index));
    eq(eworld(world_state), expected[index].at("After"),
       name + " state " + std::to_string(index));
  }

  Json actual_calls = Json::array();
  for (const auto &call : calls)
    actual_calls.push_back({{"FirstCivilizationId", call.first},
                            {"SecondCivilizationId", call.second}});
  eq(actual_calls, expected_calls, name + " hostility calls");
  eq(eworld(world_state), expected_after, name + " after");
}

void native_boundaries(const Json &cases) {
  const auto find_case = [&](const std::string &name) -> const Json & {
    for (const auto &test : cases)
      if (test.at("Name") == name)
        return test;
    fail("missing native boundary seed");
  };
  auto world_state =
      world(find_case("preview-attack-hostile").at("Arguments").at("World"));
  int calls = 0;
  CombatCommandRuntime original([&calls, call = 0](int, int) mutable {
    ++call;
    ++calls;
    return call != 100;
  });
  const MilitaryOrder attack{MilitaryOrderType::Attack, 2, std::nullopt};
  const auto preview = original.preview_order(
      {world_state.systems, world_state.fleets}, 1, 1, attack);
  CombatCommandRuntime issued_runtime(std::move(original));
  const auto issued = issued_runtime.issue_order(
      {world_state.systems, world_state.fleets}, 1, 1, attack);
  const auto first_events = issued_runtime.simulation().advance(
      {world_state.systems, world_state.fleets}, .1);
  const auto calls_before_move = calls;
  CombatCommandRuntime moved(std::move(issued_runtime));
  const auto second_events =
      moved.simulation().advance({world_state.systems, world_state.fleets}, .1);
  const auto started = [](const std::vector<CombatEvent> &events) {
    return std::any_of(
        events.begin(), events.end(), [](const CombatEvent &event) {
          return event.type == CombatEventType::EngagementStarted;
        });
  };
  check(preview.accepted && issued.accepted && started(first_events) &&
            !started(second_events) && calls > calls_before_move,
        "runtime moved after preview must retain policy and simulation state");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: combat_command_runtime_tests <fixture>");
    std::ifstream input(argv[1]);
    Json fixture;
    input >> fixture;
    check(fixture.at("Format") == "stellar-combat-command-runtime-oracle-v1",
          "fixture format");
    const auto &source_only = fixture.at("SourceOnlyObservations");
    check(source_only.size() == 11, "source-only null observation count");
    std::set<std::string> observation_names;
    for (const auto &observation : source_only) {
      check(observation_names.insert(observation.at("Name")).second,
            "duplicate source-only observation");
      const auto type = observation.at("Error").at("Type").get<std::string>();
      check(type == "ArgumentNullException" || type == "NullReferenceException",
            "unsupported source-only error category");
      check(!observation.at("Error").at("Message").get<std::string>().empty(),
            "empty source-only error message");
    }
    for (const auto &test : fixture.at("Cases"))
      run(test);
    native_boundaries(fixture.at("Cases"));
    std::cout << "combat command runtime parity: " << fixture.at("Cases").size()
              << " actual C# cases and 1 native boundary passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
