#include <stellar/core/knowledge.hpp>

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
#include <variant>
#include <vector>

using Json = nlohmann::ordered_json;
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
  fail("Unsupported named number " + text);
}

Json encoded_number(double value) {
  if (std::isnan(value))
    return "NaN";
  if (value == std::numeric_limits<double>::infinity())
    return "Infinity";
  if (value == -std::numeric_limits<double>::infinity())
    return "-Infinity";
  return value;
}

void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (expected.is_number() ||
      (expected.is_string() &&
       (expected == "NaN" || expected == "Infinity" ||
        expected == "-Infinity"))) {
    const auto actual_number = number(actual);
    const auto expected_number = number(expected);
    if (std::isnan(expected_number)) {
      check(std::isnan(actual_number), field + ": expected NaN");
      return;
    }
    if (std::isinf(expected_number)) {
      check(actual_number == expected_number, field + ": infinity mismatch");
      return;
    }
    const auto scale =
        std::max({1.0, std::abs(actual_number), std::abs(expected_number)});
    check(std::isfinite(actual_number) &&
              std::abs(actual_number - expected_number) <= 1e-9 * scale,
          field + ": number mismatch");
    return;
  }
  if (expected.is_array()) {
    check(actual.is_array() && actual.size() == expected.size(),
          field + ": array shape mismatch");
    for (std::size_t index = 0; index < expected.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (expected.is_object()) {
    check(actual.is_object() && actual.size() == expected.size(),
          field + ": object shape mismatch");
    auto actual_item = actual.begin();
    for (auto expected_item = expected.begin(); expected_item != expected.end();
         ++expected_item, ++actual_item) {
      check(actual_item.key() == expected_item.key(),
            field + ": object key/order mismatch");
      equal_json(actual_item.value(), expected_item.value(),
                 field + "." + expected_item.key());
    }
    return;
  }
  check(actual == expected, field + ": value mismatch");
}

StellarSystem parse_system(const Json &value) {
  StellarSystem result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  const auto &position = value.at("Position");
  result.position.x = static_cast<float>(number(position.at("X")));
  result.position.y = static_cast<float>(number(position.at("Y")));
  if (value.contains("GalacticDepthLightYears"))
    result.position.depth_light_years =
        number(value.at("GalacticDepthLightYears"));
  check(value.at("Archetype") == 0, "Unknown fixture star archetype");
  result.has_habitable_world = value.at("HasHabitableWorld");
  result.has_anomaly = value.at("HasAnomaly");
  result.has_rare_resource = value.at("HasRareResource");
  result.has_pre_warp_civilization = value.at("HasPreWarpCivilization");
  return result;
}

std::vector<StellarSystem> parse_systems(const Json &values) {
  std::vector<StellarSystem> result;
  for (const auto &value : values)
    result.push_back(parse_system(value));
  return result;
}

Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.home_system_id = value.at("HomeSystemId");
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  result.is_player = value.at("IsPlayer");
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient");
  result.expansion_allowed = value.at("ExpansionAllowed");
  result.neutral_unless_provoked = value.at("NeutralUnlessProvoked");
  result.species_id = value.at("SpeciesId");
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound")};
  check(value.at("Leadership").at("Offices").empty(),
        "Fixture leadership is outside the knowledge boundary");
  return result;
}

std::vector<Civilization> parse_civilizations(const Json &values) {
  std::vector<Civilization> result;
  for (const auto &value : values)
    result.push_back(parse_civilization(value));
  return result;
}

enum class Operation {
  reveal_system,
  record_reconnaissance,
  advance_system_survey,
  mark_system_fully_surveyed,
  reveal_civilization,
  unlock_galactic_core_access,
  record_galactic_core_exploration,
  reveal_within_sensor_range,
  queries,
  snapshot,
  create_initial
};

struct Command {
  Operation operation{};
  Json source;
  std::optional<int> civilization_id, system_id, target_civilization_id;
  std::optional<double> progress;
  std::optional<float> sensor_range;
  std::vector<StellarSystem> systems;
  std::vector<Civilization> civilizations;
};

Command parse_command(const Json &value) {
  Command result;
  result.source = value;
  const auto operation = value.at("Operation").get<std::string>();
  if (operation == "RevealSystem")
    result.operation = Operation::reveal_system;
  else if (operation == "RecordReconnaissance")
    result.operation = Operation::record_reconnaissance;
  else if (operation == "AdvanceSystemSurvey")
    result.operation = Operation::advance_system_survey;
  else if (operation == "MarkSystemFullySurveyed")
    result.operation = Operation::mark_system_fully_surveyed;
  else if (operation == "RevealCivilization")
    result.operation = Operation::reveal_civilization;
  else if (operation == "UnlockGalacticCoreAccess")
    result.operation = Operation::unlock_galactic_core_access;
  else if (operation == "RecordGalacticCoreExploration")
    result.operation = Operation::record_galactic_core_exploration;
  else if (operation == "RevealWithinSensorRange")
    result.operation = Operation::reveal_within_sensor_range;
  else if (operation == "Queries")
    result.operation = Operation::queries;
  else if (operation == "Snapshot")
    result.operation = Operation::snapshot;
  else if (operation == "CreateInitial")
    result.operation = Operation::create_initial;
  else
    fail("Unknown fixture command " + operation);

  if (value.contains("CivilizationId"))
    result.civilization_id = value.at("CivilizationId");
  if (value.contains("SystemId"))
    result.system_id = value.at("SystemId");
  if (value.contains("TargetCivilizationId"))
    result.target_civilization_id = value.at("TargetCivilizationId");
  if (value.contains("Progress"))
    result.progress = number(value.at("Progress"));
  if (value.contains("SensorRange"))
    result.sensor_range = static_cast<float>(number(value.at("SensorRange")));
  if (value.contains("Systems"))
    result.systems = parse_systems(value.at("Systems"));
  if (value.contains("Civilizations"))
    result.civilizations = parse_civilizations(value.at("Civilizations"));
  const auto require = [&](bool condition, const char *field) {
    check(condition, "Command " + operation + " is missing " + field);
  };
  switch (result.operation) {
  case Operation::reveal_system:
  case Operation::mark_system_fully_surveyed:
    require(result.civilization_id.has_value(), "CivilizationId");
    require(result.system_id.has_value(), "SystemId");
    break;
  case Operation::record_reconnaissance:
  case Operation::advance_system_survey:
    require(result.civilization_id.has_value(), "CivilizationId");
    require(result.system_id.has_value(), "SystemId");
    require(result.progress.has_value(), "Progress");
    break;
  case Operation::reveal_civilization:
    require(result.civilization_id.has_value(), "CivilizationId");
    require(result.target_civilization_id.has_value(),
            "TargetCivilizationId");
    break;
  case Operation::unlock_galactic_core_access:
  case Operation::record_galactic_core_exploration:
    require(result.civilization_id.has_value(), "CivilizationId");
    break;
  case Operation::reveal_within_sensor_range:
    require(result.civilization_id.has_value(), "CivilizationId");
    require(result.system_id.has_value(), "SystemId");
    require(result.sensor_range.has_value(), "SensorRange");
    require(value.contains("Systems"), "Systems");
    break;
  case Operation::queries:
    require(result.civilization_id.has_value(), "CivilizationId");
    require(result.system_id.has_value(), "SystemId");
    require(result.target_civilization_id.has_value(),
            "TargetCivilizationId");
    break;
  case Operation::create_initial:
    require(result.sensor_range.has_value(), "SensorRange");
    require(value.contains("Systems"), "Systems");
    require(value.contains("Civilizations"), "Civilizations");
    break;
  case Operation::snapshot:
    break;
  }
  return result;
}

std::vector<Command> parse_commands(const Json &values) {
  std::vector<Command> result;
  for (const auto &value : values)
    result.push_back(parse_command(value));
  return result;
}

Json encode_surveys(const CivilizationKnowledgeState &knowledge, int observer) {
  Json result = Json::array();
  for (const auto &survey : knowledge.system_survey_knowledge(observer))
    result.push_back({{"SystemId", survey.system_id},
                      {"Level", static_cast<int>(survey.level)},
                      {"Progress", encoded_number(survey.progress)}});
  return result;
}

Json observe(const CivilizationKnowledgeState &knowledge,
             const std::vector<int> &observer_universe,
             const std::vector<int> &system_universe) {
  Json observers = Json::array();
  for (const int observer : observer_universe) {
    Json query_systems = Json::array();
    for (const int system : system_universe)
      query_systems.push_back(
          {{"SystemId", system},
           {"Known", knowledge.is_system_known(observer, system)},
           {"FullySurveyed",
            knowledge.is_system_fully_surveyed(observer, system)},
           {"Level",
            static_cast<int>(knowledge.system_survey_level(observer, system))},
           {"Progress",
            encoded_number(knowledge.system_survey_progress(observer, system))}});
    observers.push_back(
        {{"CivilizationId", observer},
         {"KnownSystems", knowledge.known_systems(observer)},
         {"KnownCivilizations", knowledge.known_civilizations(observer)},
         {"Surveys", encode_surveys(knowledge, observer)},
         {"QuerySystems", std::move(query_systems)},
         {"CoreAccess", knowledge.has_galactic_core_access(observer)},
         {"CoreDiscovered", knowledge.is_galactic_core_discovered(observer)}});
  }
  return {{"Observers", std::move(observers)},
          {"CoreObservers", knowledge.galactic_core_observers()}};
}

Json encode_snapshot(const KnowledgeSnapshot &snapshot) {
  Json systems = Json::object();
  for (const auto &entry : snapshot.systems)
    systems[std::to_string(entry.observer_id)] = entry.values;
  Json civilizations = Json::object();
  for (const auto &entry : snapshot.civilizations)
    civilizations[std::to_string(entry.observer_id)] = entry.values;
  return {{"Systems", std::move(systems)},
          {"Civilizations", std::move(civilizations)}};
}

struct QueryResult {
  bool known{}, fully_surveyed{}, contact{};
  SystemSurveyLevel level{};
  double progress{};
  std::vector<int> systems, contacts;
};
using Outcome = std::variant<std::monostate, bool, int, QueryResult,
                             KnowledgeSnapshot, CivilizationKnowledgeState>;

struct Error {
  std::string type, message;
};

Error classify(const std::exception_ptr &error) {
  if (!error)
    return {};
  try {
    std::rethrow_exception(error);
  } catch (const std::out_of_range &value) {
    return {"ArgumentOutOfRangeException", value.what()};
  } catch (const std::invalid_argument &value) {
    return {"ArgumentException", value.what()};
  } catch (const std::exception &value) {
    return {"InvalidOperationException", value.what()};
  }
}

struct CommandExecution {
  Outcome outcome;
  std::exception_ptr error;
};

CommandExecution invoke(CivilizationKnowledgeState &knowledge,
                        const Command &command) {
  CommandExecution execution;
  try {
    switch (command.operation) {
    case Operation::reveal_system:
      execution.outcome = knowledge.reveal_system(*command.civilization_id,
                                                  *command.system_id);
      break;
    case Operation::record_reconnaissance:
      execution.outcome = knowledge.record_reconnaissance(
          *command.civilization_id, *command.system_id, *command.progress);
      break;
    case Operation::advance_system_survey:
      execution.outcome = knowledge.advance_system_survey(
          *command.civilization_id, *command.system_id, *command.progress);
      break;
    case Operation::mark_system_fully_surveyed:
      execution.outcome = knowledge.mark_system_fully_surveyed(
          *command.civilization_id, *command.system_id);
      break;
    case Operation::reveal_civilization:
      execution.outcome = knowledge.reveal_civilization(
          *command.civilization_id, *command.target_civilization_id);
      break;
    case Operation::unlock_galactic_core_access:
      knowledge.unlock_galactic_core_access(*command.civilization_id);
      execution.outcome = std::monostate{};
      break;
    case Operation::record_galactic_core_exploration:
      execution.outcome = knowledge.record_galactic_core_exploration(
          *command.civilization_id);
      break;
    case Operation::reveal_within_sensor_range:
      execution.outcome = knowledge.reveal_within_sensor_range(
          *command.civilization_id, *command.system_id, command.systems,
          *command.sensor_range);
      break;
    case Operation::queries:
      execution.outcome = QueryResult{
          knowledge.is_system_known(*command.civilization_id,
                                    *command.system_id),
          knowledge.is_system_fully_surveyed(*command.civilization_id,
                                             *command.system_id),
          knowledge.is_civilization_known(*command.civilization_id,
                                          *command.target_civilization_id),
          knowledge.system_survey_level(*command.civilization_id,
                                        *command.system_id),
          knowledge.system_survey_progress(*command.civilization_id,
                                           *command.system_id),
          knowledge.known_systems(*command.civilization_id),
          knowledge.known_civilizations(*command.civilization_id)};
      break;
    case Operation::snapshot:
      execution.outcome = knowledge.snapshot();
      break;
    case Operation::create_initial:
      execution.outcome = CivilizationKnowledgeState::create_initial(
          command.systems, command.civilizations, *command.sensor_range);
      break;
    }
  } catch (...) {
    execution.error = std::current_exception();
  }
  return execution;
}

Json encode_outcome(const Outcome &outcome,
                    const std::vector<int> &observer_universe,
                    const std::vector<int> &system_universe) {
  if (std::holds_alternative<std::monostate>(outcome))
    return nullptr;
  if (const auto *value = std::get_if<bool>(&outcome))
    return *value;
  if (const auto *value = std::get_if<int>(&outcome))
    return *value;
  if (const auto *value = std::get_if<QueryResult>(&outcome))
    return {{"Known", value->known},
            {"FullySurveyed", value->fully_surveyed},
            {"Level", static_cast<int>(value->level)},
            {"Progress", encoded_number(value->progress)},
            {"Systems", value->systems},
            {"Contact", value->contact},
            {"Contacts", value->contacts}};
  if (const auto *value = std::get_if<KnowledgeSnapshot>(&outcome))
    return encode_snapshot(*value);
  return observe(std::get<CivilizationKnowledgeState>(outcome),
                 observer_universe, system_universe);
}

Json freeze_execution(const Command &command, const CommandExecution &execution,
                      const std::vector<int> &observer_universe,
                      const std::vector<int> &system_universe) {
  Json result{{"Command", command.source},
              {"Result", encode_outcome(execution.outcome, observer_universe,
                                        system_universe)}};
  const auto error = classify(execution.error);
  if (!error.type.empty())
    result["Error"] = {{"Type", error.type}, {"Message", error.message}};
  return result;
}

void run_case(const Json &test, const std::vector<int> &observer_universe,
              const std::vector<int> &system_universe) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  static const std::vector<std::string> known_kinds{
      "Queries",          "RevealSystem",     "Reconnaissance",
      "AdvanceSystemSurvey", "MarkSystemFullySurveyed",
      "RevealCivilization", "CoreExplore", "CoreUnlock",
      "CoreSequence", "SensorReveal", "CreateInitial", "Snapshot"};
  check(std::ranges::find(known_kinds, kind) != known_kinds.end(),
        name + ": unknown case kind " + kind);
  const auto setup_commands =
      parse_commands(test.at("Arguments").at("SetupCommands"));
  const auto commands = parse_commands(test.at("Arguments").at("Commands"));
  const auto expected_setup = test.at("SetupResults");
  const auto expected_results = test.at("Result");
  const auto expected_before = test.at("Before");
  const auto expected_after = test.at("After");
  check(setup_commands.size() == expected_setup.size(),
        name + ": setup result count");
  check(commands.size() == expected_results.size(),
        name + ": result count");

  CivilizationKnowledgeState knowledge;
  Json actual_setup = Json::array();
  for (const auto &command : setup_commands) {
    const auto execution = invoke(knowledge, command);
    actual_setup.push_back(freeze_execution(command, execution,
                                            observer_universe,
                                            system_universe));
  }
  equal_json(actual_setup, expected_setup, name + ".SetupResults");
  equal_json(observe(knowledge, observer_universe, system_universe),
             expected_before, name + ".Before");

  Json actual_results = Json::array();
  for (const auto &command : commands) {
    const auto execution = invoke(knowledge, command);
    actual_results.push_back(freeze_execution(command, execution,
                                              observer_universe,
                                              system_universe));
  }
  equal_json(actual_results, expected_results, name + ".Result");
  equal_json(observe(knowledge, observer_universe, system_universe),
             expected_after, name + ".After");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected knowledge fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open knowledge fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format") == "stellar-knowledge-oracle-v2",
          "Unknown knowledge fixture format");
    const auto observer_universe =
        fixture.at("ObserverUniverse").get<std::vector<int>>();
    const auto system_universe =
        fixture.at("SystemUniverse").get<std::vector<int>>();
    for (const auto &test : fixture.at("Cases"))
      run_case(test, observer_universe, system_universe);
    std::cout << "knowledge_tests: passed " << fixture.at("Cases").size()
              << " actual-C# cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "knowledge_tests failed: " << error.what() << '\n';
    return 1;
  }
}
