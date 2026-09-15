#include <stellar/core/detail/adaptive_research_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_sha256.hpp>
#include <stellar/core/fleet_combat_intelligence.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using J = nlohmann::ordered_json;
using namespace stellar::core;

double number(const J &value) {
  if (value.is_string()) {
    const auto text = value.get<std::string>();
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (text == "Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
  }
  return value.get<double>();
}

J encoded_number(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value > 0 ? J("Infinity") : J("-Infinity");
  return value;
}

MassiveCombatLoadout loadout(const J &input) {
  MassiveCombatLoadout result;
  if (input.empty()) return result;
  result.mass_per_ship = static_cast<float>(number(input.at("MassPerShip")));
  result.acceleration = static_cast<float>(number(input.at("Acceleration")));
  result.maximum_speed = static_cast<float>(number(input.at("MaximumSpeed")));
  result.shield_per_ship = static_cast<float>(number(input.at("ShieldPerShip")));
  result.armor_per_ship = static_cast<float>(number(input.at("ArmorPerShip")));
  result.hull_per_ship = static_cast<float>(number(input.at("HullPerShip")));
  result.reactor_output_per_ship = static_cast<float>(number(input.at("ReactorOutputPerShip")));
  result.cooling_per_ship = static_cast<float>(number(input.at("CoolingPerShip")));
  result.warp_stabilization = static_cast<float>(number(input.at("WarpStabilization")));
  result.warp_spool_seconds = static_cast<float>(number(input.at("WarpSpoolSeconds")));
  result.module_slot_capacity = input.at("ModuleSlotCapacity").get<int>();
  result.maximum_module_mass = static_cast<float>(number(input.at("MaximumModuleMass")));
  for (const auto &item : input.at("Weapons")) {
    MassiveWeaponGroup value;
    value.id = item.at("Id").get<std::string>();
    value.kind = static_cast<MassiveWeaponKind>(item.at("Kind").get<int>());
    value.mounts_per_ship = item.at("MountsPerShip").get<int>();
    value.damage_per_shot = static_cast<float>(number(item.at("DamagePerShot")));
    value.shots_per_second = static_cast<float>(number(item.at("ShotsPerSecond")));
    value.range = static_cast<float>(number(item.at("Range")));
    value.accuracy = static_cast<float>(number(item.at("Accuracy")));
    value.power_per_second = static_cast<float>(number(item.at("PowerPerSecond")));
    value.heat_per_second = static_cast<float>(number(item.at("HeatPerSecond")));
    result.weapons.push_back(std::move(value));
  }
  for (const auto &item : input.at("Modules")) {
    MassiveModuleState value;
    value.id = item.at("Id").get<std::string>();
    value.kind = static_cast<MassiveModuleKind>(item.at("Kind").get<int>());
    value.installed_count = item.at("InstalledCount").get<int>();
    value.mass_each = static_cast<float>(number(item.at("MassEach")));
    value.power_per_second_each = static_cast<float>(number(item.at("PowerPerSecondEach")));
    value.heat_per_second_each = static_cast<float>(number(item.at("HeatPerSecondEach")));
    value.condition = static_cast<float>(number(item.at("Condition")));
    value.enabled = item.at("Enabled").get<bool>();
    value.effective_range = static_cast<float>(number(item.at("EffectiveRange")));
    value.field_strength = static_cast<float>(number(item.at("FieldStrength")));
    value.detection_signature = static_cast<float>(number(item.at("DetectionSignature")));
    value.slots = item.at("Slots").get<int>();
    result.modules.push_back(std::move(value));
  }
  return result;
}

FleetState fleet(const J &input) {
  FleetState result;
  result.id = input.at("Id").get<int>();
  result.civilization_id = input.at("CivilizationId").get<int>();
  result.role = static_cast<FleetRole>(input.at("Role").get<int>());
  result.is_active = input.at("IsActive").get<bool>();
  if (!input.at("CurrentSystemId").is_null())
    result.current_system_id = input.at("CurrentSystemId").get<int>();
  if (!input.at("Combat").is_null()) {
    const auto &source = input.at("Combat");
    FleetCombatState state;
    state.profile_id = source.at("ProfileId").get<std::string>();
    state.shields = number(source.at("Shields"));
    state.armor = number(source.at("Armor"));
    state.hull = number(source.at("Hull"));
    result.combat = std::move(state);
  }
  if (!input.at("TacticalLoadout").is_null())
    result.tactical_loadout = loadout(input.at("TacticalLoadout"));
  return result;
}

FleetPowerObservation observation(const J &input) {
  return {input.at("ObserverId").get<int>(), input.at("FleetId").get<int>(),
          number(input.at("Power")), number(input.at("ObservedDay")),
          input.at("Evidence").get<std::string>()};
}

struct World {
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
  std::vector<FleetPowerObservation> observations;
};

World world(const J &input) {
  World result;
  for (const auto &id : input.at("CivilizationIds")) {
    Civilization value;
    value.id = id.get<int>();
    result.civilizations.push_back(std::move(value));
  }
  for (const auto &item : input.at("Fleets")) result.fleets.push_back(fleet(item));
  for (const auto &item : input.at("CombatIntelligence"))
    result.observations.push_back(observation(item));
  return result;
}

J loadout_json(const MassiveCombatLoadout &value);

J fleet_json(const FleetState &value) {
  J combat = nullptr;
  if (value.combat)
    combat = J{{"ProfileId", value.combat->profile_id},
               {"Shields", encoded_number(value.combat->shields)},
               {"Armor", encoded_number(value.combat->armor)},
               {"Hull", encoded_number(value.combat->hull)}};
  J tactical = value.tactical_loadout ? loadout_json(*value.tactical_loadout)
                                      : J(nullptr);
  return J{{"Id", value.id},
           {"CivilizationId", value.civilization_id},
           {"Role", static_cast<int>(value.role)},
           {"CurrentSystemId", value.current_system_id ? J(*value.current_system_id) : J(nullptr)},
           {"IsActive", value.is_active},
           {"Combat", std::move(combat)},
           {"TacticalLoadout", std::move(tactical)}};
}

J observation_json(const FleetPowerObservation &value) {
  return J{{"ObserverId", value.observer_id}, {"FleetId", value.fleet_id},
           {"Power", encoded_number(value.power)},
           {"ObservedDay", encoded_number(value.observed_day)},
           {"Evidence", value.evidence}};
}

J loadout_json(const MassiveCombatLoadout &value) {
  J weapons = J::array();
  for (const auto &item : value.weapons)
    weapons.push_back(J{{"Id", item.id}, {"Kind", static_cast<int>(item.kind)},
                        {"MountsPerShip", item.mounts_per_ship},
                        {"DamagePerShot", encoded_number(item.damage_per_shot)},
                        {"ShotsPerSecond", encoded_number(item.shots_per_second)},
                        {"Range", encoded_number(item.range)},
                        {"Accuracy", encoded_number(item.accuracy)},
                        {"PowerPerSecond", encoded_number(item.power_per_second)},
                        {"HeatPerSecond", encoded_number(item.heat_per_second)}});
  J modules = J::array();
  for (const auto &item : value.modules)
    modules.push_back(J{{"Id", item.id}, {"Kind", static_cast<int>(item.kind)},
                        {"InstalledCount", item.installed_count},
                        {"MassEach", encoded_number(item.mass_each)},
                        {"PowerPerSecondEach", encoded_number(item.power_per_second_each)},
                        {"HeatPerSecondEach", encoded_number(item.heat_per_second_each)},
                        {"Condition", encoded_number(item.condition)}, {"Enabled", item.enabled},
                        {"EffectiveRange", encoded_number(item.effective_range)},
                        {"FieldStrength", encoded_number(item.field_strength)},
                        {"DetectionSignature", encoded_number(item.detection_signature)},
                        {"Slots", item.slots}});
  return J{{"MassPerShip", encoded_number(value.mass_per_ship)},
           {"Acceleration", encoded_number(value.acceleration)},
           {"MaximumSpeed", encoded_number(value.maximum_speed)},
           {"ShieldPerShip", encoded_number(value.shield_per_ship)},
           {"ArmorPerShip", encoded_number(value.armor_per_ship)},
           {"HullPerShip", encoded_number(value.hull_per_ship)},
           {"ReactorOutputPerShip", encoded_number(value.reactor_output_per_ship)},
           {"CoolingPerShip", encoded_number(value.cooling_per_ship)},
           {"WarpStabilization", encoded_number(value.warp_stabilization)},
           {"WarpSpoolSeconds", encoded_number(value.warp_spool_seconds)},
           {"ModuleSlotCapacity", value.module_slot_capacity},
           {"MaximumModuleMass", encoded_number(value.maximum_module_mass)},
           {"Weapons", std::move(weapons)}, {"Modules", std::move(modules)}};
}

J world_json(const World &value) {
  J civilizations = J::array();
  for (const auto &item : value.civilizations) civilizations.push_back(item.id);
  J fleets = J::array();
  for (const auto &item : value.fleets) fleets.push_back(fleet_json(item));
  J observations = J::array();
  for (const auto &item : value.observations) observations.push_back(observation_json(item));
  return J{{"CivilizationIds", std::move(civilizations)},
           {"Fleets", std::move(fleets)},
           {"CombatIntelligence", std::move(observations)}};
}

bool numbers_equal(const J &left, const J &right) {
  if (left.is_string() || right.is_string()) return left == right;
  const bool left_integer = left.is_number_integer() || left.is_number_unsigned();
  const bool right_integer = right.is_number_integer() || right.is_number_unsigned();
  if (left_integer && right_integer) {
    if (left.is_number_unsigned() && right.is_number_unsigned())
      return left.get<std::uint64_t>() == right.get<std::uint64_t>();
    if (left.is_number_integer() && right.is_number_integer())
      return left.get<std::int64_t>() == right.get<std::int64_t>();
    const auto &signed_value = left.is_number_integer() ? left : right;
    const auto &unsigned_value = left.is_number_unsigned() ? left : right;
    const auto signed_number = signed_value.get<std::int64_t>();
    return signed_number >= 0 &&
           static_cast<std::uint64_t>(signed_number) ==
               unsigned_value.get<std::uint64_t>();
  }
  const auto a = left.get<double>();
  const auto b = right.get<double>();
  return std::abs(a - b) <= 1e-10;
}

bool equal_json(const J &left, const J &right) {
  if (left.type() != right.type()) {
    if (left.is_number() && right.is_number()) return numbers_equal(left, right);
    return false;
  }
  if (left.is_number()) return numbers_equal(left, right);
  if (left.is_array()) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
      if (!equal_json(left[i], right[i])) return false;
    return true;
  }
  if (left.is_object()) {
    if (left.size() != right.size()) return false;
    for (auto entry = left.begin(); entry != left.end(); ++entry) {
      const auto found = right.find(entry.key());
      if (found == right.end() || !equal_json(entry.value(), *found)) return false;
    }
    return true;
  }
  return left == right;
}

J error_json(const std::exception *error, std::string type) {
  return error ? J{{"Type", std::move(type)}, {"Message", error->what()}} : J(nullptr);
}

void require_equal(const J &actual, const J &expected, const std::string &where) {
  if (!equal_json(actual, expected))
    throw std::runtime_error(where + " mismatch\nactual=" + actual.dump() +
                             "\nexpected=" + expected.dump());
}

std::string sha256_hex(std::string_view bytes) {
  const auto digest = detail::adaptive_research_sha256(
      {reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()});
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(64);
  for (const auto byte : digest) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0x0F]);
  }
  return result;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument(
          "Usage: fleet_combat_intelligence_tests <source-root> <fixture-path>");
    const auto source_root = std::filesystem::absolute(argv[1]);
    const auto fixture_path = std::filesystem::absolute(argv[2]);
    const auto source_fingerprint = [&] {
      std::string bytes;
      for (const auto *relative : {
               "src/Game/Simulation/Combat/FleetCombatPower.cs",
               "src/Game/Simulation/Combat/Massive/MassiveCombatOutcome.cs",
               "src/Game/Presentation/Main.MassiveCombat.cs"}) {
        std::ifstream source(source_root / relative, std::ios::binary);
        if (!source)
          throw std::runtime_error("Could not open source authority: " +
                                   (source_root / relative).string());
        bytes.append(std::istreambuf_iterator<char>(source),
                     std::istreambuf_iterator<char>());
      }
      return sha256_hex(bytes);
    }();
    if (source_fingerprint !=
        "904DA29FE59D531036AA95D9998395F07DB00752A1E65185B421AE0CCEE42954")
      throw std::runtime_error("Source authority fingerprint mismatch.");
    std::ifstream stream(fixture_path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open fixture: " + fixture_path.string());
    const std::string fixture_bytes((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    if (sha256_hex(fixture_bytes) !=
        "D146FDCAD77E15D20ED1505DEE40111CD9793D3E330B4C87CAA27502859FA1A6")
      throw std::runtime_error("Fixture fingerprint does not match retained source output.");
    const J fixture = J::parse(fixture_bytes);
    if (fixture.at("Schema") != 1 || fixture.at("Rows").size() != 40)
      throw std::runtime_error("Fixture schema or row count is not the retained source matrix.");
    if (fixture.at("SourceFingerprint") !=
        "904DA29FE59D531036AA95D9998395F07DB00752A1E65185B421AE0CCEE42954")
      throw std::runtime_error("Source authority fingerprint mismatch.");

    int invoked = 0;
    int source_only = 0;
    for (const auto &row : fixture.at("Rows")) {
      const auto name = row.at("Name").get<std::string>();
      const auto operation = row.at("Operation").get<std::string>();
      if (!row.at("Native").get<bool>()) {
        if (name != "observe_null_target_source_boundary" ||
            operation != "ObserveManyNull" || !row.at("Result").is_null() ||
            row.at("Error").at("Type") != "NullReferenceException" ||
            row.at("Boundary") !=
                "Native public spans contain valid typed records; null enumerable elements are outside that type boundary." ||
            row.at("Input") != row.at("InputAfter"))
          throw std::runtime_error("Unexpected source-only boundary metadata.");
        ++source_only;
        continue;
      }
      static const std::set<std::string, std::less<>> known_operations{
          "PerShip", "OwnPower", "Observed", "ObserveMany",
          "ObserveDetached", "RecordSensorContacts", "HasCombatScanner",
          "HasCombatScannerNull", "HasCombatScannerMissing"};
      if (!known_operations.contains(operation))
        throw std::runtime_error(name + " has an unknown fixture operation");
      if (row.contains("InputAfter"))
        require_equal(row.at("Input"), row.at("InputAfter"), name + " retained input");
      if (row.contains("ArgsAfter"))
        require_equal(row.at("Args"), row.at("ArgsAfter"), name + " retained arguments");

      J actual_result = nullptr;
      J actual_error = nullptr;
      std::optional<float> captured_float;
      std::optional<double> captured_double;
      std::optional<std::optional<double>> captured_observed;
      std::optional<int> captured_int;
      std::optional<bool> captured_bool;
      std::optional<World> state;
      std::optional<MassiveCombatLoadout> prepared_loadout;
      std::optional<FleetState> prepared_fleet;
      std::optional<AdaptiveResearchCivilizationState> research;
      std::vector<const FleetState *> targets;
      std::optional<FleetState> detached;
      int observer{};
      double day{};
      bool engaged{};
      bool scanning{};
      const FleetState *observed_target = nullptr;

      if (operation == "PerShip") prepared_loadout = loadout(row.at("Input"));
      else if (operation == "OwnPower") prepared_fleet = fleet(row.at("Input"));
      else if (operation == "HasCombatScanner" || operation == "HasCombatScannerNull" ||
               operation == "HasCombatScannerMissing") {
        if (operation == "HasCombatScanner") {
          research.emplace("1", "stage");
          for (const auto &capability : row.at("Input").at("Capabilities"))
            detail::AdaptiveResearchStateWriter::add_capability(
                *research, {capability.get<std::string>(), std::nullopt});
        }
      } else {
        state = world(row.at("Before"));
        observer = row.at("Args").at("ObserverId").get<int>();
        if (row.at("Args").contains("Day")) day = number(row.at("Args").at("Day"));
        if (row.at("Args").contains("Engaged")) engaged = row.at("Args").at("Engaged").get<bool>();
        if (row.at("Args").contains("Scanning")) scanning = row.at("Args").at("Scanning").get<bool>();
        if (operation == "ObserveMany")
          for (const auto &index : row.at("Args").at("TargetIndices"))
            targets.push_back(&state->fleets.at(index.get<std::size_t>()));
        else if (operation == "ObserveDetached") {
          detached = fleet(row.at("Args").at("Detached"));
          targets.push_back(&*detached);
        }
        if (operation == "Observed") {
          const auto index = row.at("Args").at("TargetIndex").get<std::size_t>();
          observed_target = &state->fleets.at(index);
        }
      }

      if (prepared_loadout)
        require_equal(loadout_json(*prepared_loadout), row.at("Input"),
                      name + " native input before");
      if (prepared_fleet)
        require_equal(fleet_json(*prepared_fleet), row.at("Input"),
                      name + " native input before");
      if (state)
        require_equal(world_json(*state), row.at("Before"),
                      name + " native world before");

      try {
        if (operation == "PerShip")
          captured_float = massive_combat_per_ship_power(*prepared_loadout);
        else if (operation == "OwnPower")
          captured_double = own_fleet_combat_power(*prepared_fleet);
        else if (operation == "Observed")
          captured_observed = observed_fleet_combat_power(
              state->observations, observer, *observed_target);
        else if (operation == "ObserveMany" || operation == "ObserveDetached") {
          observe_fleet_combat_power_many({state->civilizations, state->fleets, state->observations}, observer, targets, day, engaged, scanning);
        } else if (operation == "RecordSensorContacts") {
          captured_int = record_fleet_sensor_contacts({state->civilizations, state->fleets, state->observations}, observer, day, scanning);
        } else if (operation == "HasCombatScanner")
          captured_bool = has_combat_scanner(&*research);
        else if (operation == "HasCombatScannerNull" || operation == "HasCombatScannerMissing")
          captured_bool = has_combat_scanner(nullptr);
      } catch (const FleetCombatIntelligenceRangeError &error) {
        actual_error = error_json(&error, "ArgumentOutOfRangeException");
      } catch (const FleetCombatIntelligenceArgumentError &error) {
        actual_error = error_json(&error, "ArgumentException");
      } catch (const std::invalid_argument &error) {
        actual_error = error_json(&error, "InvalidOperationException");
      }

      if (captured_float) actual_result = encoded_number(*captured_float);
      if (captured_double) actual_result = encoded_number(*captured_double);
      if (captured_observed)
        actual_result = *captured_observed ? encoded_number(**captured_observed)
                                           : J(nullptr);
      if (captured_int) actual_result = *captured_int;
      if (captured_bool) actual_result = *captured_bool;

      require_equal(actual_result, row.at("Result"), name + " result");
      require_equal(actual_error, row.at("Error"), name + " error");
      if (state) require_equal(world_json(*state), row.at("After"), name + " world");
      if (prepared_loadout)
        require_equal(loadout_json(*prepared_loadout), row.at("Input"),
                      name + " native input after");
      if (prepared_fleet)
        require_equal(fleet_json(*prepared_fleet), row.at("Input"),
                      name + " native input after");
      ++invoked;
    }
    if (invoked != 39 || source_only != 1)
      throw std::runtime_error("Native/source-only accounting mismatch.");
    std::cout << "fleet combat intelligence source parity passed: " << invoked
              << " native rows, " << source_only << " source boundary\n";
    if (source_fingerprint != [&] {
          std::string bytes;
          for (const auto *relative : {
                   "src/Game/Simulation/Combat/FleetCombatPower.cs",
                   "src/Game/Simulation/Combat/Massive/MassiveCombatOutcome.cs",
                   "src/Game/Presentation/Main.MassiveCombat.cs"}) {
            std::ifstream source(source_root / relative, std::ios::binary);
            bytes.append(std::istreambuf_iterator<char>(source),
                         std::istreambuf_iterator<char>());
          }
          return sha256_hex(bytes);
        }())
      throw std::runtime_error("Source authority changed during replay.");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << typeid(error).name() << ": " << error.what() << '\n'
              << "cwd=" << std::filesystem::current_path().string() << '\n';
    if (argc > 1) std::cerr << "sourceRoot=" << std::filesystem::absolute(argv[1]).string() << '\n';
    if (argc > 2) std::cerr << "fixture=" << std::filesystem::absolute(argv[2]).string() << '\n';
    return 1;
  }
}
