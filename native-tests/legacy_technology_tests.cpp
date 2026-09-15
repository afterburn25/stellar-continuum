#include <stellar/core/legacy_technology.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::runtime_error(message);
}
void check(bool condition, const std::string &message) {
  if (!condition)
    fail(message);
}
double number(const Json &value) {
  if (value.is_number())
    return value.get<double>();
  const auto text = value.get<std::string>();
  if (text == "NaN")
    return std::numeric_limits<double>::quiet_NaN();
  if (text == "Infinity")
    return std::numeric_limits<double>::infinity();
  if (text == "-Infinity")
    return -std::numeric_limits<double>::infinity();
  fail("unsupported named number");
}
Json encode_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}
template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}
template <class T> Json encode_optional(const std::optional<T> &value) {
  return value ? Json(*value) : Json(nullptr);
}
void equal_json(const Json &actual, const Json &expected,
                const std::string &label) {
  if (expected.is_number() ||
      (expected.is_string() && (expected == "NaN" || expected == "Infinity" ||
                                expected == "-Infinity"))) {
    const auto a = number(actual);
    const auto e = number(expected);
    if (std::isnan(e)) {
      check(std::isnan(a), label + ": expected NaN");
      return;
    }
    if (std::isinf(e)) {
      check(a == e, label + ": infinity mismatch");
      return;
    }
    const auto scale = std::max({1.0, std::abs(a), std::abs(e)});
    check(std::isfinite(a) && std::abs(a - e) <= 1e-12 * scale,
          label + ": number mismatch");
    return;
  }
  if (expected.is_array()) {
    check(actual.is_array() && actual.size() == expected.size(),
          label + ": array shape");
    for (std::size_t index = 0; index < expected.size(); ++index)
      equal_json(actual[index], expected[index],
                 label + "[" + std::to_string(index) + "]");
    return;
  }
  if (expected.is_object()) {
    check(actual.is_object() && actual.size() == expected.size(),
          label + ": object shape");
    for (const auto &[key, value] : expected.items()) {
      check(actual.contains(key), label + ": missing " + key);
      equal_json(actual.at(key), value, label + "." + key);
    }
    return;
  }
  check(actual == expected, label + ": value mismatch; actual " +
                                actual.dump() + ", expected " +
                                expected.dump());
}

TechnologyDefinition parse_definition(const Json &value) {
  return {value.at("Id").get<std::string>(),
          value.at("Name").get<std::string>(),
          value.at("Description").get<std::string>(),
          number(value.at("ResearchCost")),
          value.at("Prerequisites").get<std::vector<std::string>>(),
          value.at("RequiredProjects").get<std::vector<std::string>>(),
          static_cast<TechnologyCategory>(value.at("Category").get<int>())};
}
Json encode_definition(const TechnologyDefinition &value) {
  return {{"Id", value.id},
          {"Name", value.name},
          {"Description", value.description},
          {"ResearchCost", encode_number(value.research_cost)},
          {"Prerequisites", value.prerequisites},
          {"RequiredProjects", value.required_projects},
          {"Category", static_cast<int>(value.category)}};
}
Json encode_definitions(std::span<const TechnologyDefinition> values) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(encode_definition(value));
  return result;
}

TechnologyState parse_state(const Json &value) {
  TechnologyState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  for (const auto &id :
       value.at("CompletedTechnologyIds").get<std::vector<std::string>>())
    result.completed_technology_ids.insert(id);
  result.active_research_id =
      optional<std::string>(value.at("ActiveResearchId"));
  result.active_research_progress = number(value.at("ActiveResearchProgress"));
  return result;
}
Json encode_state(const TechnologyState &value) {
  auto completed = Json::array();
  for (const auto &id : value.completed_technology_ids.values())
    completed.push_back(id);
  return {{"CivilizationId", value.civilization_id},
          {"CompletedTechnologyIds", std::move(completed)},
          {"ActiveResearchId", encode_optional(value.active_research_id)},
          {"ActiveResearchProgress",
           encode_number(value.active_research_progress)}};
}
Json encode_states(std::span<const TechnologyState> values) {
  auto result = Json::array();
  for (const auto &value : values)
    result.push_back(encode_state(value));
  return result;
}

ConstructionState parse_construction(const Json &value) {
  ConstructionState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.completed_project_ids =
      value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id = optional<std::string>(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  for (const auto &queued : value.at("QueuedProjects"))
    result.queued_projects.push_back(
        {queued.at("ProjectId").get<std::string>(),
         number(queued.at("AuthorizationCredits"))});
  return result;
}
Json encode_construction(const ConstructionState &value) {
  auto queued = Json::array();
  for (const auto &entry : value.queued_projects)
    queued.push_back(
        {{"ProjectId", entry.project_id},
         {"AuthorizationCredits", encode_number(entry.authorization_credits)}});
  return {
      {"CivilizationId", value.civilization_id},
      {"CompletedProjectIds", value.completed_project_ids},
      {"ActiveProjectId", encode_optional(value.active_project_id)},
      {"ActiveProjectProgress", encode_number(value.active_project_progress)},
      {"ActiveProjectAuthorizationCredits",
       encode_number(value.active_project_authorization_credits)},
      {"QueuedProjects", std::move(queued)}};
}

Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id").get<int>();
  result.name = value.at("Name").get<std::string>();
  result.home_system_id = value.at("HomeSystemId").get<int>();
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound").get<bool>()};
  result.is_player = value.at("IsPlayer").get<bool>();
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient").get<bool>();
  result.expansion_allowed = value.at("ExpansionAllowed").get<bool>();
  result.neutral_unless_provoked =
      value.at("NeutralUnlessProvoked").get<bool>();
  result.species_id = value.at("SpeciesId").get<std::string>();
  for (const auto &[office, character] :
       value.at("Leadership").at("Offices").items()) {
    CivilizationOffice entry;
    entry.office = office;
    entry.character.id = character.at("Id").get<std::string>();
    entry.character.display_name =
        character.at("DisplayName").get<std::string>();
    entry.character.voice_profile_id =
        optional<std::string>(character.at("VoiceProfileId"));
    entry.character.portrait = optional<std::string>(character.at("Portrait"));
    result.leadership.push_back(std::move(entry));
  }
  return result;
}
std::vector<Civilization> parse_civilizations(const Json &value) {
  check(value.is_array(), "Civilizations must be an array");
  std::vector<Civilization> result;
  for (const auto &entry : value)
    result.push_back(parse_civilization(entry));
  return result;
}
Json encode_civilizations(std::span<const Civilization> values) {
  auto result = Json::array();
  for (const auto &value : values) {
    Json offices = Json::object();
    for (const auto &entry : value.leadership)
      offices[entry.office] = {
          {"Id", entry.character.id},
          {"DisplayName", entry.character.display_name},
          {"VoiceProfileId", encode_optional(entry.character.voice_profile_id)},
          {"Portrait", encode_optional(entry.character.portrait)}};
    result.push_back(
        {{"Id", value.id},
         {"Name", value.name},
         {"HomeSystemId", value.home_system_id},
         {"Archetype", static_cast<int>(value.archetype)},
         {"Traits",
          {{"Aggression", encode_number(value.traits.aggression)},
           {"Territoriality", encode_number(value.traits.territoriality)},
           {"Greed", encode_number(value.traits.greed)},
           {"ScientificCuriosity",
            encode_number(value.traits.scientific_curiosity)},
           {"RiskTolerance", encode_number(value.traits.risk_tolerance)},
           {"SurvivalPriority", encode_number(value.traits.survival_priority)},
           {"HonorBound", value.traits.honor_bound}}},
         {"IsPlayer", value.is_player},
         {"DevelopmentStage", static_cast<int>(value.development_stage)},
         {"IsSeededAncient", value.is_seeded_ancient},
         {"ExpansionAllowed", value.expansion_allowed},
         {"NeutralUnlessProvoked", value.neutral_unless_provoked},
         {"SpeciesId", value.species_id},
         {"Leadership", {{"Offices", std::move(offices)}}}});
  }
  return result;
}

struct ErrorInfo {
  std::string type;
  std::string message;
};
ErrorInfo classify(const std::exception_ptr &error) {
  if (!error)
    return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::runtime_error &actual) {
    return {"InvalidOperationException", actual.what()};
  } catch (const std::exception &actual) {
    return {"UnexpectedNativeException", actual.what()};
  }
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Get" || kind == "Available" || kind == "Seed",
        name + ": unknown kind");
  const auto &arguments = test.at("Arguments");
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  const auto expected_error_message =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Message").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "InvalidOperationException",
        name + ": unsupported expected error");

  std::string id;
  std::optional<TechnologyState> state;
  std::optional<ConstructionState> construction;
  std::vector<Civilization> civilizations;
  if (kind == "Get")
    id = arguments.at("Id").get<std::string>();
  if (kind == "Available") {
    state = parse_state(arguments.at("State"));
    construction = parse_construction(arguments.at("Construction"));
    equal_json(arguments.at("State"), test.at("BeforeState"),
               name + ": state snapshot");
    equal_json(arguments.at("Construction"), test.at("BeforeConstruction"),
               name + ": construction snapshot");
    equal_json(test.at("BeforeState"), test.at("AfterState"),
               name + ": C# state mutation");
    equal_json(test.at("BeforeConstruction"), test.at("AfterConstruction"),
               name + ": C# construction mutation");
  }
  if (kind == "Seed") {
    civilizations = parse_civilizations(arguments.at("Civilizations"));
    equal_json(arguments.at("Civilizations"), test.at("BeforeCivilizations"),
               name + ": civilization snapshot");
    equal_json(test.at("BeforeCivilizations"), test.at("AfterCivilizations"),
               name + ": C# input mutation");
  }

  const TechnologyDefinition *actual_definition = nullptr;
  std::optional<std::vector<TechnologyDefinition>> actual_definitions;
  std::optional<std::vector<TechnologyState>> actual_states;
  std::exception_ptr operation_error;
  try {
    if (kind == "Get")
      actual_definition = &get_legacy_technology(id);
    else if (kind == "Available")
      actual_definitions = available_legacy_technologies(*state, *construction);
    else
      actual_states = seed_legacy_technologies(civilizations);
  } catch (...) {
    operation_error = std::current_exception();
  }

  std::optional<Json> actual_result;
  if (!operation_error) {
    if (actual_definition)
      actual_result = encode_definition(*actual_definition);
    else if (actual_definitions)
      actual_result = encode_definitions(*actual_definitions);
    else if (actual_states)
      actual_result = encode_states(*actual_states);
  }
  const auto actual_error = classify(operation_error);
  if (!expected_error_type.empty()) {
    check(actual_error.type == expected_error_type, name + ": error category");
    check(actual_error.message == expected_error_message,
          name + ": error message");
    check(!actual_result, name + ": errored result");
  } else {
    check(actual_error.type.empty(),
          name + ": unexpected error " + actual_error.message);
    check(actual_result.has_value(), name + ": missing result");
    equal_json(*actual_result, test.at("Result"), name + ": result");
  }

  if (kind == "Available") {
    equal_json(encode_state(*state), test.at("AfterState"),
               name + ": native state after");
    equal_json(encode_construction(*construction), test.at("AfterConstruction"),
               name + ": native construction after");
  }
  if (kind == "Seed")
    equal_json(encode_civilizations(civilizations),
               test.at("AfterCivilizations"),
               name + ": native civilizations after");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: legacy_technology_tests <fixture.json>\n";
    return 2;
  }
  try {
    std::ifstream input(argv[1]);
    check(input.good(), "fixture could not be opened");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format") == "stellar-legacy-technology-oracle-v1",
          "fixture format");
    equal_json(encode_definitions(legacy_technology_catalog()),
               fixture.at("Catalog"), "catalog");
    const auto &cases = fixture.at("Cases");
    check(cases.is_array() && !cases.empty(), "fixture cases");
    for (const auto &test : cases)
      run_case(test);
    std::cout << "legacy_technology_tests: passed " << cases.size()
              << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "legacy_technology_tests: " << error.what() << '\n';
    return 1;
  }
}
