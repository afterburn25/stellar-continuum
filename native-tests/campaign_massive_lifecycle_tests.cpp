#include <stellar/core/campaign_massive_combat.hpp>

#define main gate079_unused_main
#include "massive_combat_persistence_tests.cpp"
#undef main

#include <fstream>
#include <cstdio>
#include <iostream>
#include <typeinfo>

namespace gate094 {

template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
}

FleetPowerObservation intelligence(const Json &value) {
  return {value.at("ObserverId"), value.at("FleetId"),
          number(value.at("Power")), number(value.at("ObservedDay")),
          value.at("Evidence")};
}
Json intelligence_json(const FleetPowerObservation &value) {
  return {{"ObserverId", value.observer_id}, {"FleetId", value.fleet_id},
          {"Power", floating(value.power)},
          {"ObservedDay", floating(value.observed_day)},
          {"Evidence", value.evidence}};
}

Json observed_point_json(const MassivePoint &value) {
  return {{"X", floating(value.x)}, {"Y", floating(value.y)},
          {"Vector", {{"X", floating(value.x)}, {"Y", floating(value.y)}}},
          {"IsFinite", value.is_finite()}};
}
Json observed_vector_json(const MassivePoint &value) {
  return {{"X", floating(value.x)}, {"Y", floating(value.y)}};
}
template <class T, class F> Json observed_array(const std::vector<T> &values,
                                                 F emit) {
  Json result = Json::array();
  for (const auto &value : values) result.push_back(emit(value));
  return result;
}
Json observed_snapshot_json(const MassiveCombatSnapshot &value) {
  auto nullable_point = [](const auto &point) {
    return point ? observed_point_json(*point) : Json(nullptr);
  };
  Json formations = Json::array();
  for (const auto &formation : value.formations) {
    Json cohorts = Json::array();
    for (const auto &cohort : formation.cohorts)
      cohorts.push_back({{"CohortId", cohort.cohort_id},
                         {"DisplayClass", cohort.display_class},
                         {"CountLow", cohort.count_low},
                         {"CountHigh", cohort.count_high},
                         {"Identified", cohort.identified}});
    Json vessels = Json::array();
    for (const auto &vessel : formation.important_vessels)
      vessels.push_back({{"VesselId", vessel.vessel_id},
                         {"DisplayName", vessel.display_name},
                         {"DesignId", vessel.design_id},
                         {"CombatPower", vessel.combat_power ? Json(floating(*vessel.combat_power)) : Json(nullptr)},
                         {"IsFlagship", vessel.is_flagship}, {"IsCarrier", vessel.is_carrier},
                         {"IsInterdictor", vessel.is_interdictor}, {"IsCriticallyDamaged", vessel.is_critically_damaged}});
    formations.push_back({{"FormationId", formation.formation_id}, {"CivilizationId", formation.civilization_id},
      {"DisplayName", formation.display_name}, {"Position", observed_vector_json(formation.position)}, {"Velocity", observed_vector_json(formation.velocity)},
      {"Shape", static_cast<int>(formation.shape)}, {"ShipCountLow", formation.ship_count_low}, {"ShipCountHigh", formation.ship_count_high},
      {"StrengthLow", formation.strength_low ? Json(floating(*formation.strength_low)) : Json(nullptr)}, {"StrengthHigh", formation.strength_high ? Json(floating(*formation.strength_high)) : Json(nullptr)},
      {"IsExact", formation.is_exact}, {"IsInterdicting", formation.is_interdicting}, {"IsWarpBlocked", formation.is_warp_blocked}, {"WarpSpoolProgress", floating(formation.warp_spool_progress)},
      {"PerShipCombatPower", formation.per_ship_combat_power ? Json(floating(*formation.per_ship_combat_power)) : Json(nullptr)}, {"Cohorts", cohorts}, {"ImportantVessels", vessels}, {"HeadingRadians", floating(formation.heading_radians)}});
  }
  Json events = Json::array();
  for (const auto &event : value.events)
    events.push_back({{"Sequence", event.sequence}, {"Tick", event.tick}, {"Type", static_cast<int>(event.type)},
      {"ActorCivilizationId", event.actor_civilization_id ? Json(*event.actor_civilization_id) : Json(nullptr)}, {"ActorFormationId", event.actor_formation_id ? Json(*event.actor_formation_id) : Json(nullptr)},
      {"TargetCivilizationId", event.target_civilization_id ? Json(*event.target_civilization_id) : Json(nullptr)}, {"TargetFormationId", event.target_formation_id ? Json(*event.target_formation_id) : Json(nullptr)},
      {"Magnitude", event.magnitude ? Json(*event.magnitude) : Json(nullptr)}, {"Position", nullable_point(event.position)}, {"Message", event.message}, {"DetailsKnown", event.details_known}, {"ImpactPosition", nullable_point(event.impact_position)}});
  Json salvos = Json::array();
  for (const auto &salvo : value.active_missile_salvos)
    salvos.push_back({{"SalvoId", salvo.salvo_id}, {"SourceFormationId", salvo.source_formation_id ? Json(*salvo.source_formation_id) : Json(nullptr)}, {"SourcePosition", nullable_point(salvo.source_position)},
      {"TargetFormationId", salvo.target_formation_id ? Json(*salvo.target_formation_id) : Json(nullptr)}, {"TargetPosition", nullable_point(salvo.target_position)}, {"CurrentPosition", nullable_point(salvo.current_position)},
      {"RemainingSeconds", floating(salvo.remaining_seconds)}, {"Progress01", salvo.progress_01 ? Json(floating(*salvo.progress_01)) : Json(nullptr)}, {"CountLow", salvo.count_low ? Json(*salvo.count_low) : Json(nullptr)}, {"CountHigh", salvo.count_high ? Json(*salvo.count_high) : Json(nullptr)}, {"IncomingToOwn", salvo.incoming_to_own}});
  // Guid.ToByteArray ordering is the stored battle-byte ordering.
  char guid[37]; const auto &b = value.battle_id;
  std::snprintf(guid, sizeof guid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[3],b[2],b[1],b[0],b[5],b[4],b[7],b[6],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
  return {{"BattleId", guid}, {"Tick", value.tick}, {"SimulatedSeconds", floating(value.simulated_seconds)}, {"ExactOwnShips", value.exact_own_ships}, {"Formations", formations}, {"Events", events}, {"ActiveMissileSalvos", salvos}};
}

MassiveCombatSnapshot doctrine_snapshot(const Json &value) {
  MassiveCombatSnapshot result;
  result.tick = value.at("Tick");
  result.simulated_seconds = number(value.at("SimulatedSeconds"));
  result.exact_own_ships = value.at("ExactOwnShips");
  for (const auto &item : value.at("Formations")) {
    MassiveObservedFormation formation;
    formation.formation_id = item.at("FormationId");
    formation.civilization_id = item.at("CivilizationId");
    formation.display_name = item.at("DisplayName");
    formation.position = {static_cast<float>(number(item.at("Position").at("X"))),
                          static_cast<float>(number(item.at("Position").at("Y")))};
    formation.velocity = {static_cast<float>(number(item.at("Velocity").at("X"))),
                          static_cast<float>(number(item.at("Velocity").at("Y")))};
    formation.shape = static_cast<MassiveFormationShape>(item.at("Shape").get<int>());
    formation.ship_count_low = item.at("ShipCountLow");
    formation.ship_count_high = item.at("ShipCountHigh");
    formation.is_exact = item.at("IsExact");
    formation.is_interdicting = item.at("IsInterdicting");
    formation.is_warp_blocked = item.at("IsWarpBlocked");
    formation.warp_spool_progress = static_cast<float>(number(item.at("WarpSpoolProgress")));
    formation.heading_radians = static_cast<float>(number(item.at("HeadingRadians")));
    result.formations.push_back(std::move(formation));
  }
  return result;
}

Json order_json(const MassiveCombatOrder &value) {
  return {{"FormationId", value.formation_id},
          {"Type", static_cast<int>(value.type)},
          {"TargetFormationId", value.target_formation_id
                                    ? Json(*value.target_formation_id)
                                    : Json(nullptr)},
          {"Objective", value.objective
                            ? Json{{"X", floating(value.objective->x)},
                                   {"Y", floating(value.objective->y)}}
                            : Json(nullptr)},
          {"Shape", value.shape ? Json(static_cast<int>(*value.shape))
                                  : Json(nullptr)}};
}

FleetCombatState combat(const Json &value) {
  FleetCombatState result;
  result.profile_id = value.at("ProfileId");
  result.shields = number(value.at("Shields"));
  result.armor = number(value.at("Armor"));
  result.hull = number(value.at("Hull"));
  result.weapon_cooldown_remaining_days =
      number(value.at("WeaponCooldownRemainingDays"));
  result.order = static_cast<MilitaryOrderType>(value.at("Order").get<int>());
  result.target_fleet_id = optional<int>(value.at("TargetFleetId"));
  result.defend_system_id = optional<int>(value.at("DefendSystemId"));
  result.retreat_progress_days = number(value.at("RetreatProgressDays"));
  result.retreat_started = value.at("RetreatStarted");
  result.is_disengaged = value.at("IsDisengaged");
  result.disengaged_system_id = optional<int>(value.at("DisengagedSystemId"));
  return result;
}

Json combat_json(const FleetCombatState &value) {
  return {{"ProfileId", value.profile_id},
          {"Shields", floating(value.shields)},
          {"Armor", floating(value.armor)},
          {"Hull", floating(value.hull)},
          {"WeaponCooldownRemainingDays",
           floating(value.weapon_cooldown_remaining_days)},
          {"Order", static_cast<int>(value.order)},
          {"TargetFleetId", value.target_fleet_id
                                ? Json(*value.target_fleet_id)
                                : Json(nullptr)},
          {"DefendSystemId", value.defend_system_id
                                ? Json(*value.defend_system_id)
                                : Json(nullptr)},
          {"RetreatProgressDays", floating(value.retreat_progress_days)},
          {"RetreatStarted", value.retreat_started},
          {"IsDisengaged", value.is_disengaged},
          {"DisengagedSystemId", value.disengaged_system_id
                                    ? Json(*value.disengaged_system_id)
                                    : Json(nullptr)}};
}

FleetState fleet(const Json &value) {
  FleetState result;
  result.id = value.at("Id");
  result.civilization_id = value.at("CivilizationId");
  result.name = value.at("Name");
  result.role = static_cast<FleetRole>(value.at("Role").get<int>());
  result.design_id = optional<std::string>(value.at("DesignId"));
  result.current_system_id = optional<int>(value.at("CurrentSystemId"));
  result.destination_system_id = optional<int>(value.at("DestinationSystemId"));
  result.planned_route_system_ids =
      value.at("PlannedRouteSystemIds").get<std::vector<int>>();
  result.destination_planetary_body_id =
      optional<int>(value.at("DestinationPlanetaryBodyId"));
  result.is_active = value.at("IsActive");
  result.embarked_population_millions =
      number(value.at("EmbarkedPopulationMillions"));
  result.embarked_population_species_id =
      optional<std::string>(value.at("EmbarkedPopulationSpeciesId"));
  if (!value.at("Combat").is_null())
    result.combat = combat(value.at("Combat"));
  if (!value.at("TacticalLoadout").is_null())
    result.tactical_loadout = loadout(value.at("TacticalLoadout"));
  if (!value.at("TacticalVessel").is_null())
    result.tactical_vessel = vessel(value.at("TacticalVessel"));
  return result;
}

void update_fleet_json(Json &result, const FleetState &value) {
  result["IsActive"] = value.is_active;
  result["DestinationSystemId"] = value.destination_system_id
                                      ? Json(*value.destination_system_id)
                                      : Json(nullptr);
  result["PlannedRouteSystemIds"] = value.planned_route_system_ids;
  result["DestinationPlanetaryBodyId"] = value.destination_planetary_body_id
                                             ? Json(*value.destination_planetary_body_id)
                                             : Json(nullptr);
  result["EmbarkedPopulationMillions"] =
      floating(value.embarked_population_millions);
  result["EmbarkedPopulationSpeciesId"] = value.embarked_population_species_id
                                              ? Json(*value.embarked_population_species_id)
                                              : Json(nullptr);
  result["Combat"] = value.combat ? combat_json(*value.combat) : Json(nullptr);
  result["TacticalLoadout"] = value.tactical_loadout
                                  ? loadout_json(*value.tactical_loadout)
                                  : Json(nullptr);
  result["TacticalVessel"] = value.tactical_vessel
                                 ? vessel_json(*value.tactical_vessel)
                                 : Json(nullptr);
}

struct World {
  Json original;
  FreshCampaignState state;
};

World world(const Json &value) {
  World result{value, {}};
  result.state.seed = value.at("Seed");
  result.state.player_civilization_id = value.at("PlayerCivilizationId");
  for (const auto &item : value.at("Systems")) {
    StellarSystem system;
    system.id = item.at("Id");
    result.state.systems.push_back(std::move(system));
  }
  for (const auto &item : value.at("Fleets"))
    result.state.fleets.push_back(fleet(item));
  if (value.contains("CombatIntelligence"))
    for (const auto &item : value.at("CombatIntelligence"))
      result.state.combat_intelligence.push_back(intelligence(item));
  if (!value.at("ActiveCombatEncounter").is_null())
    result.state.active_combat_encounter =
        encounter(value.at("ActiveCombatEncounter"));
  return result;
}

Json world_json(const World &prepared) {
  auto result = prepared.original;
  auto &fleets = result["Fleets"];
  for (std::size_t index = 0; index < prepared.state.fleets.size(); ++index)
    update_fleet_json(fleets.at(index), prepared.state.fleets[index]);
  result["ActiveCombatEncounter"] = prepared.state.active_combat_encounter
                                          ? encounter_json(*prepared.state.active_combat_encounter)
                                          : Json(nullptr);
  result["CombatIntelligence"] = observed_array(
      prepared.state.combat_intelligence,
      [](const FleetPowerObservation &value) { return intelligence_json(value); });
  // World() enables IncludeFields, so the Vector2 nested inside each
  // MassivePoint exposes its X/Y fields. Snapshot results use the default
  // options and deliberately keep that same nested Vector object empty.
  std::function<void(Json &)> include_point_vector_fields =
      [&](Json &node) {
        if (node.is_object()) {
          if (node.contains("IsFinite") && node.contains("X") &&
              node.contains("Y") && node.contains("Vector"))
            node["Vector"] = {{"X", node.at("X")}, {"Y", node.at("Y")}};
          for (auto &[key, child] : node.items()) {
            static_cast<void>(key);
            include_point_vector_fields(child);
          }
        } else if (node.is_array()) {
          for (auto &child : node) include_point_vector_fields(child);
        }
      };
  include_point_vector_fields(result);
  return result;
}

Json event_json_value(const CombatEvent &value) {
  return {{"Type", static_cast<int>(value.type)},
          {"SystemId", value.system_id ? Json(*value.system_id) : Json(nullptr)},
          {"ActorCivilizationId", value.actor_civilization_id},
          {"ActorFleetId", value.actor_fleet_id},
          {"TargetCivilizationId", value.target_civilization_id
                                          ? Json(*value.target_civilization_id)
                                          : Json(nullptr)},
          {"TargetFleetId", value.target_fleet_id
                                ? Json(*value.target_fleet_id)
                                : Json(nullptr)},
          {"ShieldDamage", floating(value.shield_damage)},
          {"ArmorDamage", floating(value.armor_damage)},
          {"HullDamage", floating(value.hull_damage)},
          {"Message", value.message},
          {"EmbarkedPopulationCasualtiesMillions",
           floating(value.embarked_population_casualties_millions)}};
}

void require_equal(const Json &actual, const Json &expected,
                   std::string label) {
  std::function<bool(const Json &, const Json &, std::string &)> same =
      [&](const Json &left, const Json &right, std::string &path) {
        if (left.is_number() && right.is_number())
          if (left.get<double>() == 0 && right.get<double>() == 0)
            return true;
        if (left.is_number() && right.is_number())
          return (left.is_number_integer() || left.is_number_unsigned()) &&
                         (right.is_number_integer() || right.is_number_unsigned())
                     ? left == right
                     : (path.contains("/Loadout/") ||
                        path.contains("/TacticalLoadout/") ||
                        path.contains("/TacticalVessel/") ||
                        path.contains("/Formations/") ||
                        path.contains("/ActiveMissileSalvos/") ||
                        path.ends_with("/X") || path.ends_with("/Y"))
                           ? std::bit_cast<std::uint32_t>(static_cast<float>(left.get<double>())) ==
                                 std::bit_cast<std::uint32_t>(static_cast<float>(right.get<double>()))
                     : (std::isnan(left.get<double>()) &&
                        std::isnan(right.get<double>())) ||
                           std::abs(left.get<double>() - right.get<double>()) <=
                               1e-10;
        if (left.type() != right.type() || left.size() != right.size())
          return false;
        if (left.is_object())
          for (const auto &[key, value] : left.items()) {
            if (!right.contains(key))
              return false;
            const auto before = path;
            path += "/" + key;
            if (!same(value, right.at(key), path))
              return false;
            path = before;
          }
        else if (left.is_array())
          for (std::size_t index = 0; index < left.size(); ++index) {
            const auto before = path;
            path += "/" + std::to_string(index);
            if (!same(left.at(index), right.at(index), path))
              return false;
            path = before;
          }
        else if (left != right)
          return false;
        return true;
      };
  std::string path = "$";
  if (!same(actual, expected, path))
    throw std::runtime_error(label + " differs at " + path + "\nactual=" +
                             actual.dump() + "\nexpected=" + expected.dump());
}

std::pair<std::string, std::string> error(const std::exception &value) {
  if (dynamic_cast<const CampaignMassiveCombatArgumentNullError *>(&value))
    return {"ArgumentNullException", value.what()};
  if (dynamic_cast<const CampaignMassiveCombatSerializationError *>(&value))
    return {"ArgumentException", value.what()};
  if (dynamic_cast<const MassiveCombatArgumentRangeError *>(&value))
    return {"ArgumentOutOfRangeException", value.what()};
  if (dynamic_cast<const std::overflow_error *>(&value))
    return {"OverflowException", value.what()};
  return {typeid(value).name(), value.what()};
}

int run(const fs::path &fixture_path, const fs::path &source_root) {
  const auto fixture_bytes = read_bytes(fixture_path, "fixture");
  const auto fixture_hash = sha256(fixture_bytes);
  const auto fixture = Json::parse(fixture_bytes);
  if (fixture.at("RowCount").get<int>() !=
      static_cast<int>(fixture.at("Rows").size()))
    throw std::runtime_error("Fixture row count differs from Rows.");
  std::vector<std::pair<fs::path, std::string>> sources;
  for (const auto &item : fixture.at("SourceFiles")) {
    const auto path = source_root / item.at("Path").get<std::string>();
    const auto expected = item.at("Sha256").get<std::string>();
    if (sha256(read_bytes(path, "source file")) != expected)
      throw std::runtime_error("Source fingerprint mismatch: " + path.string());
    sources.emplace_back(path, expected);
  }
  int passed = 0;
  for (const auto &row : fixture.at("Rows")) {
    auto prepared = world(row.at("Before"));
    Json result;
    std::string error_type, error_message;
    try {
      const auto operation = row.at("Operation").get<std::string>();
      if (operation == "Constructor") {
        CampaignMassiveCombat runtime({});
      } else {
        CampaignMassiveCombat runtime(
            [](int first, int second) { return first != second; });
        if (operation == "Begin") {
          const auto &call = row.at("Input");
          if (call.value("SecondLoadoutNegativeZero", false))
            prepared.state.fleets.at(2)
                .tactical_loadout->modules.at(0).effective_range = -0.0F;
          const auto value = runtime.begin(
              prepared.state, call.at("CivilizationId"),
              call.at("ActorFleetId"), number(call.at("Day")));
          result = {{"Accepted", value.accepted}, {"Message", value.message}};
        } else if (operation == "Reconcile") {
          result = Json::array();
          for (const auto &value : runtime.reconcile(prepared.state))
            result.push_back(event_json_value(value));
        } else if (operation == "ReconcileTwice") {
          result = Json::array();
          for (const auto &run_result : {runtime.reconcile(prepared.state),
                                         runtime.reconcile(prepared.state)}) {
            Json events = Json::array();
            for (const auto &value : run_result)
              events.push_back(event_json_value(value));
            result.push_back(std::move(events));
          }
        } else if (operation == "Observe") {
          const auto &call = row.at("Input");
          result = observed_snapshot_json(runtime.observe(prepared.state,
              call.at("ObserverId"), call.at("ScanningCapability")));
        } else if (operation == "Observer") {
          const auto &call = row.at("Input");
          MassiveCombatSensorView sensors;
          sensors.confidence = [&call](int, std::int64_t id) { return id == 1 ? 1.F : static_cast<float>(number(call.at("EnemyConfidence"))); };
          sensors.identifies_cohorts = [&call](int, std::int64_t id) { return id == 1 || call.at("Identify").get<bool>(); };
          sensors.identifies_important_vessels = sensors.identifies_cohorts;
          sensors.can_estimate_combat_power = [&call](int, std::int64_t id) { return id == 1 || call.at("Power").get<bool>(); };
          result = observed_snapshot_json(build_massive_combat_snapshot(prepared.state.active_combat_encounter->battle, call.at("ObserverId"), sensors));
        } else if (operation == "Advance" ||
                   operation == "AdvanceWithScannerReceipts") {
          const auto &call = row.at("Input");
          const auto scanner_ids = call.at("ScannerCivilizationIds").get<std::vector<int>>();
          std::set<int> scanners(scanner_ids.begin(), scanner_ids.end());
          std::vector<int> receipts;
          Json events = Json::array();
          for (const auto &value : runtime.advance(prepared.state, number(call.at("ElapsedSeconds")),
              [&scanners, &receipts](int civilization) {
                receipts.push_back(civilization);
                return scanners.contains(civilization);
              }))
            events.push_back(event_json_value(value));
          result = operation == "AdvanceWithScannerReceipts"
                       ? Json{{"Events", events}, {"ScannerCalls", receipts}}
                       : events;
        } else if (operation == "Doctrine") {
          const auto &call = row.at("Input");
          result = Json::array();
          for (const auto &value : decide_massive_combat_doctrine(
                   doctrine_snapshot(call.at("Snapshot")),
                   call.at("CivilizationId")))
            result.push_back(order_json(value));
        } else {
          throw std::runtime_error("Unknown operation.");
        }
      }
    } catch (const std::exception &value) {
      std::tie(error_type, error_message) = error(value);
    }
    require_equal(world_json(prepared), row.at("After"),
                  "world for " + row.at("Name").get<std::string>());
    const auto expected_type = row.at("ErrorType");
    if (expected_type.is_null()) {
      if (!error_type.empty())
        throw std::runtime_error("unexpected error in " +
                                 row.at("Name").get<std::string>() + ": " +
                                 error_message);
      require_equal(result, row.at("Result"), "result");
    } else if (error_type != expected_type.get<std::string>() ||
               error_message != row.at("ErrorMessage").get<std::string>()) {
      throw std::runtime_error("error mismatch in " +
                               row.at("Name").get<std::string>() +
                               ": actual " + error_type + " / " + error_message +
                               "; expected " + expected_type.get<std::string>() +
                               " / " + row.at("ErrorMessage").get<std::string>());
    }
    ++passed;
  }

  const auto begin_row = std::ranges::find_if(
      fixture.at("Rows"), [](const Json &row) {
        return row.at("Name") == "begin-two-vessels";
      });
  if (begin_row == fixture.at("Rows").end())
    throw std::runtime_error("Move probe source row is missing.");
  auto first_world = world(begin_row->at("Before"));
  int hostility_calls = 0;
  CampaignMassiveCombat first([&hostility_calls](int first_id, int second_id) {
    ++hostility_calls;
    return first_id != second_id;
  });
  CampaignMassiveCombat second(std::move(first));
  const auto &call = begin_row->at("Input");
  const auto move_constructed = second.begin(
      first_world.state, call.at("CivilizationId"), call.at("ActorFleetId"),
      number(call.at("Day")));
  if (!move_constructed.accepted || hostility_calls == 0)
    throw std::runtime_error(
        "Move construction did not preserve the owned hostility callback.");

  auto second_world = world(begin_row->at("Before"));
  CampaignMassiveCombat third([](int, int) { return false; });
  third = std::move(second);
  const auto calls_before_assignment_probe = hostility_calls;
  const auto move_assigned = third.begin(
      second_world.state, call.at("CivilizationId"), call.at("ActorFleetId"),
      number(call.at("Day")));
  if (!move_assigned.accepted ||
      hostility_calls <= calls_before_assignment_probe)
    throw std::runtime_error(
        "Move assignment did not preserve the owned hostility callback.");

  try {
    CampaignMassiveCombat invalid({});
    throw std::runtime_error("Empty hostility callback was accepted.");
  } catch (const CampaignMassiveCombatArgumentNullError &value) {
    if (value.parameter() != "hostility")
      throw std::runtime_error("Argument-null parameter differs from source.");
  }

  // A retained lookup must remain equivalent to a newly built service after
  // canonical vectors move or binding order changes. Invalid duplicate bindings
  // must still take the ordinary validation path even with a warm lookup.
  const auto evidence_row = std::ranges::find_if(fixture.at("Rows"), [](const Json &row) {
    return row.at("Name") == "advance-engagement-evidence";
  });
  if (evidence_row == fixture.at("Rows").end())
    throw std::runtime_error("Evidence cache probe source row is missing.");
  for (const bool duplicate : {false, true}) {
    auto warm_world = world(evidence_row->at("Before"));
    CampaignMassiveCombat warm([](int a, int b) { return a != b; });
    static_cast<void>(warm.advance(warm_world.state, 0.));
    auto &encounter = *warm_world.state.active_combat_encounter;
    encounter.engaged_formation_pairs.clear();
    encounter.last_observed_event_sequence = 0;
    if (duplicate) {
      encounter.vessels.back().fleet_id = encounter.vessels.front().fleet_id;
    } else {
      warm_world.state.fleets.reserve(warm_world.state.fleets.capacity() + 16);
      std::ranges::reverse(encounter.vessels);
    }
    auto fresh_world = world(world_json(warm_world));
    CampaignMassiveCombat fresh([](int a, int b) { return a != b; });
    auto invoke = [](auto &service, auto &state) {
      std::pair<std::string, std::string> failure;
      try { static_cast<void>(service.advance(state, 0.)); }
      catch (const std::exception &value) { failure = error(value); }
      return failure;
    };
    const auto warm_error = invoke(warm, warm_world.state);
    const auto fresh_error = invoke(fresh, fresh_world.state);
    if (warm_error != fresh_error || (duplicate && warm_error.first.empty()))
      throw std::runtime_error("Retained evidence lookup changed validation behavior.");
    require_equal(world_json(warm_world), world_json(fresh_world), "retained evidence lookup");
  }

  std::cout << "Campaign massive combat lifecycle parity: " << passed << "/"
            << fixture.at("RowCount") << " rows passed\n";
  if (sha256(read_bytes(fixture_path, "fixture")) != fixture_hash)
    throw std::runtime_error("Fixture changed during replay.");
  for (const auto &[path, expected] : sources)
    if (sha256(read_bytes(path, "source file")) != expected)
      throw std::runtime_error("Source changed during replay: " + path.string());
  return 0;
}

} // namespace gate094

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: campaign_massive_lifecycle_tests <fixture.json> <source-root>\n";
    return 1;
  }
  try {
    return gate094::run(fs::absolute(argv[1]), fs::absolute(argv[2]));
  } catch (const std::exception &value) {
    std::cerr << typeid(value).name() << ": " << value.what() << "\n"
              << "Working directory: " << fs::current_path() << "\n"
              << "Source root: " << fs::absolute(argv[2]) << "\n"
              << "Fixture path: " << fs::absolute(argv[1]) << "\n";
    return 1;
  }
}
