#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <stellar/core/combat_simulation.hpp>
using Json = nlohmann::json;
using namespace stellar::core;
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
  double days{};
};
Command command(const Json &v) {
  Command c;
  c.kind = v.at("Kind");
  check(c.kind == "Order" || c.kind == "Advance" || c.kind == "Summary",
        "unknown command");
  c.civ = v.at("CivilizationId");
  c.fleet_id = v.at("FleetId");
  c.order = {static_cast<MilitaryOrderType>(v.at("OrderType").get<int>()),
             opt<int>(v.at("TargetFleetId")), opt<int>(v.at("DefendSystemId"))};
  c.days = num(v.at("Days"));
  return c;
}
void run(const Json &t) {
  auto name = t.at("Name").get<std::string>();
  auto args = t.at("Arguments");
  World w = world(args.at("World"));
  std::vector<Command> commands;
  for (auto &x : args.at("Commands"))
    commands.push_back(command(x));
  bool hostile = args.at("Hostile");
  bool throw_hostility = args.at("ThrowHostility");
  auto expected = t.at("Result");
  auto expected_after = t.at("After");
  auto expected_calls = t.at("HostilityCalls");
  eq(eworld(w), t.at("Before"), name + " before");
  struct Call {
    int a, b;
  };
  std::vector<Call> calls;
  CombatSimulation sim([&](int a, int b) {
    calls.push_back({a, b});
    if (throw_hostility)
      throw std::runtime_error("Hostility oracle failure.");
    return hostile;
  });
  for (size_t i = 0; i < commands.size(); ++i) {
    const auto &c = commands[i];
    auto exp_error = expected[i].at("Error");
    std::string type, message;
    std::optional<CombatOrderResult> o;
    std::optional<MilitaryForceSummary> s;
    std::vector<CombatEvent> events;
    try {
      if (c.kind == "Order")
        o = sim.issue_order({w.systems, w.fleets}, c.civ, c.fleet_id, c.order);
      else if (c.kind == "Advance")
        events = sim.advance({w.systems, w.fleets}, c.days);
      else
        s = sim.get_own_military_force_summary({w.systems, w.fleets}, c.civ);
    } catch (const std::invalid_argument &e) {
      type = "ArgumentException";
      message = e.what();
    } catch (const std::out_of_range &e) {
      type = "ArgumentOutOfRangeException";
      message = e.what();
    } catch (const std::runtime_error &e) {
      type = "InvalidOperationException";
      message = e.what();
    }
    Json raw = nullptr;
    if (o)
      raw = {{"Accepted", o->accepted}, {"Message", o->message}};
    else if (s)
      raw = {{"CivilizationId", s->civilization_id},
             {"ActiveCombatVessels", s->active_combat_vessels},
             {"CurrentStrength", enc(s->current_strength)},
             {"MaximumStrength", enc(s->maximum_strength)}};
    else if (c.kind == "Advance" && type.empty()) {
      raw = Json::array();
      for (auto &e : events)
        raw.push_back(event(e));
    }
    Json err = type.empty() ? Json(nullptr)
                            : Json{{"Type", type}, {"Message", message}};
    eq(raw, expected[i].at("Result"), name + " result");
    eq(err, exp_error, name + " error");
    eq(eworld(w), expected[i].at("After"),
       name + " state after command " + std::to_string(i));
  }
  Json ecalls = Json::array();
  for (auto c : calls)
    ecalls.push_back(
        {{"FirstCivilizationId", c.a}, {"SecondCivilizationId", c.b}});
  eq(ecalls, expected_calls,
     name + " calls actual=" + ecalls.dump() +
         " expected=" + expected_calls.dump());
  eq(eworld(w), expected_after, name + " after");
}
void native_boundaries(const Json &cases) {
  const auto find_case = [&](const std::string &name) -> const Json & {
    for (const auto &test : cases)
      if (test.at("Name") == name)
        return test;
    fail("missing native boundary seed");
  };
  for (const auto days : {std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
    auto w = world(find_case("advance-idle").at("Arguments").at("World"));
    const auto before = eworld(w);
    CombatSimulation simulation;
    std::string message;
    try {
      static_cast<void>(simulation.advance({w.systems, w.fleets}, days));
    } catch (const std::out_of_range &error) {
      message = error.what();
    }
    check(message ==
              "Nonfinite combat time cannot be converted safely by the native "
              "volley counter.",
          "nonfinite native boundary message");
    eq(eworld(w), before, "nonfinite native boundary full state");
  }
  auto w =
      world(find_case("advance-attack-delta-43").at("Arguments").at("World"));
  w.fleets.at(1).combat.reset();
  auto expected_after_failure = w;
  expected_after_failure.fleets.at(1).combat =
      FleetCombatState{"patrol_corvette_mk1", 35, 45, 95};
  CombatSimulation simulation([](int, int) { return true; });
  std::string message;
  try {
    static_cast<void>(simulation.advance({w.systems, w.fleets}, 1e100));
  } catch (const std::overflow_error &error) {
    message = error.what();
  }
  check(message == "Combat volley count exceeds the native integer range.",
        "volley native boundary message");
  eq(eworld(w), eworld(expected_after_failure),
     "volley native boundary full postfailure state");
  const auto retry = simulation.advance({w.systems, w.fleets}, .1);
  check(!retry.empty() &&
            retry.front().type == CombatEventType::EngagementStarted,
        "failed volley conversion persisted an engagement");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      fail("fixture path");
    std::ifstream f(argv[1]);
    Json j;
    f >> j;
    check(j.at("Format") == "stellar-combat-simulation-oracle-v1", "format");
    for (auto &t : j.at("Cases"))
      run(t);
    native_boundaries(j.at("Cases"));
    std::cout << "combat simulation parity: " << j.at("Cases").size()
              << " actual C# cases and 3 native boundaries passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
