#include <stellar/core/campaign_massive_combat.hpp>
#include <stellar/core/galaxy_reference_validation.hpp>

#define main gate079_unused_main
#include "massive_combat_persistence_tests.cpp"
#undef main

#include <fstream>
#include <iostream>
#include <typeinfo>

namespace gate094 {

template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt
                         : std::optional<T>{value.get<T>()};
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
                        path.contains("/Formations/"))
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

  auto zero_world = world(begin_row->at("Before"));
  auto zero_actor = std::ranges::find(zero_world.state.fleets, 1,
                                      &FleetState::id);
  if (zero_actor == zero_world.state.fleets.end() ||
      !zero_actor->tactical_vessel)
    throw std::runtime_error("Zero-identity probe source fleet is missing.");
  zero_actor->id = 0;
  zero_actor->tactical_vessel->id = 0;
  auto positive_actor = std::ranges::find(zero_world.state.fleets, 2,
                                          &FleetState::id);
  if (positive_actor == zero_world.state.fleets.end() ||
      !positive_actor->tactical_vessel)
    throw std::runtime_error("Positive-identity probe source fleet is missing.");
  positive_actor->tactical_vessel->is_flagship = true;
  CampaignMassiveCombat zero_runtime(
      [](int first_id, int second_id) { return first_id != second_id; });
  const auto zero_begin = zero_runtime.begin(
      zero_world.state, call.at("CivilizationId"), 0, number(call.at("Day")));
  if (!zero_begin.accepted || !zero_world.state.active_combat_encounter)
    throw std::runtime_error("Fleet zero could not enter massive combat.");

  auto &zero_encounter = *zero_world.state.active_combat_encounter;
  const auto zero_formation = std::ranges::find_if(
      zero_encounter.battle.formations, [](const MassiveFormationState &value) {
        return value.fleet_id == 0;
      });
  if (zero_formation == zero_encounter.battle.formations.end() ||
      zero_formation->important_vessels.size() != 1 ||
      zero_formation->important_vessels.front().id !=
          campaign_zero_fleet_vessel_id)
    throw std::runtime_error(
        "Fleet zero did not receive its reserved tactical vessel identity.");
  const auto positive_formation = std::ranges::find_if(
      zero_encounter.battle.formations, [](const MassiveFormationState &value) {
        return value.fleet_id == 2;
      });
  if (positive_formation == zero_encounter.battle.formations.end() ||
      positive_formation->important_vessels.size() != 1 ||
      positive_formation->important_vessels.front().id != 2)
    throw std::runtime_error(
        "A positive fleet identity changed during tactical binding.");

  const auto own_snapshot = zero_runtime.observe(zero_world.state, 1, false);
  const auto observed_zero = std::ranges::find_if(
      own_snapshot.formations, [](const MassiveObservedFormation &value) {
        return std::ranges::any_of(
            value.important_vessels, [](const MassiveObservedVessel &vessel) {
              return vessel.vessel_id == campaign_zero_fleet_vessel_id;
            });
      });
  if (observed_zero == own_snapshot.formations.end())
    throw std::runtime_error(
        "Fleet zero's tactical vessel identity was lost to observation.");

  auto restored_encounter = encounter(encounter_json(zero_encounter));
  validate_campaign_massive_encounter(
      restored_encounter,
      {zero_world.state.systems, zero_world.state.fleets});
  const auto restored_zero = std::ranges::find_if(
      restored_encounter.battle.formations,
      [](const MassiveFormationState &value) { return value.fleet_id == 0; });
  if (restored_zero == restored_encounter.battle.formations.end() ||
      restored_zero->important_vessels.front().id !=
          campaign_zero_fleet_vessel_id)
    throw std::runtime_error(
        "Fleet zero's tactical vessel identity did not round trip.");
  zero_world.state.active_combat_encounter = std::move(restored_encounter);
  for (auto &formation :
       zero_world.state.active_combat_encounter->battle.formations)
    formation.escaped = true;
  static_cast<void>(zero_runtime.reconcile(zero_world.state));
  zero_actor = std::ranges::find(zero_world.state.fleets, 0, &FleetState::id);
  if (zero_actor == zero_world.state.fleets.end() ||
      !zero_actor->tactical_vessel ||
      zero_actor->tactical_vessel->id != campaign_zero_fleet_vessel_id)
    throw std::runtime_error(
        "Fleet zero's tactical vessel identity was not reconciled safely.");
  zero_actor->tactical_vessel->validate();
  const std::array<FleetState, 1> persisted_fleets{*zero_actor};
  validate_galaxy_references(
      {{}, {}, {}, {}, {}, persisted_fleets, {}, nullptr});

  try {
    CampaignMassiveCombat invalid({});
    throw std::runtime_error("Empty hostility callback was accepted.");
  } catch (const CampaignMassiveCombatArgumentNullError &value) {
    if (value.parameter() != "hostility")
      throw std::runtime_error("Argument-null parameter differs from source.");
  }

  std::cout << "Campaign massive combat parity: " << passed << "/"
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
    std::cerr << "Usage: campaign_massive_combat_tests <fixture.json> <source-root>\n";
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
