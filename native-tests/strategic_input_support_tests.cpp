#include <stellar/core/strategic_input_support.hpp>
#include <nlohmann/json.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }
void check(bool condition, const std::string &message) { if (!condition) fail(message); }

template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}

double number(const Json &value) {
  if (value.is_number()) return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity") return std::numeric_limits<double>::infinity();
  if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
  fail("invalid named number");
}

bool same_number(double left, const Json &right) {
  const double expected = number(right);
  return (std::isnan(left) && std::isnan(expected)) ||
         std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(expected);
}

Json encoded_number(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value > 0 ? Json("Infinity") : Json("-Infinity");
  if (value == 0.0 && std::signbit(value)) return "-0";
  return value;
}

struct Inputs {
  std::vector<Civilization> civilizations;
  std::vector<FleetState> fleets;
  std::vector<TechnologyState> technologies;
};

Inputs parse_inputs(const Json &input) {
  Inputs result;
  for (const auto &value : input.at("Civilizations")) {
    Civilization civilization;
    civilization.id = value.at("Id");
    result.civilizations.push_back(std::move(civilization));
  }
  for (const auto &value : input.at("Fleets")) {
    FleetState fleet;
    fleet.id = value.at("Id");
    fleet.civilization_id = value.at("CivilizationId");
    fleet.role = static_cast<FleetRole>(value.at("Role").get<int>());
    fleet.current_system_id = optional<int>(value.at("CurrentSystemId"));
    fleet.is_active = value.at("IsActive");
    if (!value.at("Combat").is_null()) {
      const auto &source = value.at("Combat");
      FleetCombatState combat;
      combat.profile_id = source.at("ProfileId");
      combat.shields = number(source.at("Shields"));
      combat.armor = number(source.at("Armor"));
      combat.hull = number(source.at("Hull"));
      combat.weapon_cooldown_remaining_days = number(source.at("WeaponCooldownRemainingDays"));
      combat.order = static_cast<MilitaryOrderType>(source.at("Order").get<int>());
      combat.target_fleet_id = optional<int>(source.at("TargetFleetId"));
      combat.defend_system_id = optional<int>(source.at("DefendSystemId"));
      combat.retreat_progress_days = number(source.at("RetreatProgressDays"));
      combat.retreat_started = source.at("RetreatStarted");
      combat.is_disengaged = source.at("IsDisengaged");
      combat.disengaged_system_id = optional<int>(source.at("DisengagedSystemId"));
      fleet.combat = std::move(combat);
    }
    result.fleets.push_back(std::move(fleet));
  }
  for (const auto &value : input.at("Technologies")) {
    TechnologyState technology;
    technology.civilization_id = value.at("CivilizationId");
    for (const auto &id : value.at("CompletedTechnologyIds"))
      technology.completed_technology_ids.insert(id.get<std::string>());
    technology.active_research_id = optional<std::string>(value.at("ActiveResearchId"));
    technology.active_research_progress = number(value.at("ActiveResearchProgress"));
    result.technologies.push_back(std::move(technology));
  }
  return result;
}

Json input_fingerprint(const Inputs &input) {
  Json result;
  result["Civilizations"] = Json::array();
  for (const auto &civilization : input.civilizations) result["Civilizations"].push_back(civilization.id);
  result["Fleets"] = Json::array();
  for (const auto &fleet : input.fleets) {
    Json combat = nullptr;
    if (fleet.combat) combat = Json::array({fleet.combat->profile_id, encoded_number(fleet.combat->shields),
      encoded_number(fleet.combat->armor), encoded_number(fleet.combat->hull), encoded_number(fleet.combat->weapon_cooldown_remaining_days),
      static_cast<int>(fleet.combat->order), fleet.combat->target_fleet_id,
      fleet.combat->defend_system_id, encoded_number(fleet.combat->retreat_progress_days),
      fleet.combat->retreat_started, fleet.combat->is_disengaged,
      fleet.combat->disengaged_system_id});
    result["Fleets"].push_back(Json::array({fleet.id, fleet.civilization_id,
      static_cast<int>(fleet.role), fleet.current_system_id, fleet.is_active, combat}));
  }
  result["Technologies"] = Json::array();
  for (const auto &technology : input.technologies) {
    Json ids = Json::array();
    for (const auto &id : technology.completed_technology_ids.values()) ids.push_back(id);
    result["Technologies"].push_back(Json::array({technology.civilization_id, ids,
      technology.active_research_id, encoded_number(technology.active_research_progress)}));
  }
  return result;
}

void check_summary(const CombatReadinessSummary &actual, const Json &expected,
                   const std::string &name) {
  const std::pair<const char *, int> integers[] = {
      {"CivilizationId", actual.civilization_id}, {"ActiveVessels", actual.active_vessels},
      {"ActiveArmedVessels", actual.active_armed_vessels},
      {"CombatEffectiveArmedVessels", actual.combat_effective_armed_vessels},
      {"DamagedVessels", actual.damaged_vessels}, {"HullDamagedVessels", actual.hull_damaged_vessels},
      {"RetreatingVessels", actual.retreating_vessels}, {"DisengagedVessels", actual.disengaged_vessels}};
  for (const auto &[field, value] : integers)
    check(expected.at(field).is_number_integer() && expected.at(field).get<int>() == value, name + ": " + field);
  const std::pair<const char *, double> numbers[] = {
      {"CurrentDurability", actual.current_durability}, {"MaximumDurability", actual.maximum_durability},
      {"CurrentArmedStrength", actual.current_armed_strength}, {"MaximumArmedStrength", actual.maximum_armed_strength},
      {"CombatEffectiveArmedStrength", actual.combat_effective_armed_strength}, {"TotalRepairDeficit", actual.total_repair_deficit},
      {"DurabilityRatio", actual.durability_ratio()}, {"ArmedStrengthRatio", actual.armed_strength_ratio()}};
  for (const auto &[field, value] : numbers) check(same_number(value, expected.at(field)), name + ": " + field);
}

struct Error { std::string type, message; };
} // namespace

int main(int argc, char **argv) try {
  check(argc == 2, "usage: strategic_input_support_tests <fixture>");
  std::ifstream stream(argv[1]); check(stream.good(), "fixture unavailable");
  Json root; stream >> root;
  check(root.at("Format") == "stellar-strategic-input-support-oracle-v1", "fixture format");
  check(root.at("ReadinessCaseCount").get<std::size_t>() == root.at("ReadinessCases").size(), "readiness count");
  check(root.at("CapabilityCaseCount").get<std::size_t>() == root.at("CapabilityCases").size(), "capability count");

  for (const auto &test : root.at("ReadinessCases")) {
    const auto name = test.at("Name").get<std::string>();
    check(test.at("Input") == test.at("Before") && test.at("Before") == test.at("After"), name + ": source input mutation");
    auto inputs = parse_inputs(test.at("Input"));
    const auto before = input_fingerprint(inputs);
    const int civilization_id = test.at("CivilizationId").get<int>();
    std::optional<CombatReadinessSummary> result; std::optional<Error> error;
    try { result = combat_readiness({inputs.civilizations, inputs.fleets}, civilization_id); }
    catch (const std::runtime_error &caught) { error = Error{"InvalidOperationException", caught.what()}; }
    catch (const std::exception &caught) { error = Error{"UnexpectedNativeException", caught.what()}; }
    const auto after = input_fingerprint(inputs);
    check(after == before, name + ": native input mutation");
    if (test.at("Error").is_null()) {
      check(!error && result, name + ": unexpected native error");
      check_summary(*result, test.at("Result"), name);
    } else {
      check(error && !result, name + ": expected error");
      check(error->type == test.at("Error").at("Type").get<std::string>(), name + ": error type");
      check(error->message == test.at("Error").at("Message").get<std::string>(), name + ": error message");
    }
  }

  for (const auto &test : root.at("CapabilityCases")) {
    const auto name = test.at("Name").get<std::string>();
    check(test.at("Input") == test.at("Before") && test.at("Before") == test.at("After"), name + ": source input mutation");
    auto inputs = parse_inputs(test.at("Input")); const auto before = input_fingerprint(inputs);
    const auto kind = test.at("Kind").get<std::string>();
    check(kind == "construction" || kind == "shipbuilding", name + ": fixture capability kind");
    const int civilization_id = test.at("CivilizationId").get<int>();
    const auto capability_id = test.at("CapabilityId").get<std::string>();
    std::optional<bool> result; std::optional<Error> error;
    try {
      result = kind == "construction"
          ? prototype_construction_has_capability(inputs.technologies, civilization_id, capability_id)
          : prototype_shipbuilding_has_capability(inputs.technologies, civilization_id, capability_id);
    } catch (const std::exception &caught) { error = Error{"UnexpectedNativeException", caught.what()}; }
    const auto after = input_fingerprint(inputs); check(after == before, name + ": native input mutation");
    check(test.at("Error").is_null() && !error && result.has_value() && *result == test.at("Result").get<bool>(), name + ": result");
  }

  check(root.at("ReadinessCases").size() == 56 && root.at("CapabilityCases").size() == 22 && root.at("SourceOnlyNull").size() == 3, "coverage counts");
  for (const auto &item : root.at("SourceOnlyNull")) check(item.at("Boundary") == "source-only-null" && item.at("Error").is_object(), "source-only metadata");
  std::cout << "validated 56 combat readiness cases, 22 prototype capability cases, and 3 source-only null observations\n";
} catch (const std::exception &error) {
  std::cerr << "FAIL: " << error.what() << '\n'; return 1;
}
