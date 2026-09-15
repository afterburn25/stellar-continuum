#include <stellar/core/legacy_research.hpp>

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

template <class T> std::optional<T> optional(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}

void equal_json(const Json &actual, const Json &expected,
                const std::string &field) {
  if (actual.is_number() && expected.is_number()) {
    const auto first = actual.get<double>();
    const auto second = expected.get<double>();
    const auto scale = std::max({1.0, std::abs(first), std::abs(second)});
    check(std::abs(first - second) <= 1e-12 * scale,
          field + ": number mismatch");
    return;
  }
  if (actual.is_array() && expected.is_array()) {
    check(actual.size() == expected.size(), field + ": array size mismatch");
    for (std::size_t index = 0; index < actual.size(); ++index)
      equal_json(actual[index], expected[index],
                 field + "[" + std::to_string(index) + "]");
    return;
  }
  if (actual.is_object() && expected.is_object()) {
    check(actual.size() == expected.size(), field + ": object size mismatch");
    for (const auto &[key, value] : expected.items()) {
      check(actual.contains(key), field + ": missing key " + key);
      equal_json(actual.at(key), value, field + "." + key);
    }
    return;
  }
  check(actual == expected,
        field + ": mismatch; actual=" + actual.dump() +
            ", expected=" + expected.dump());
}

Civilization parse_civilization(const Json &value) {
  Civilization result;
  result.id = value.at("Id");
  result.name = value.at("Name");
  result.home_system_id = value.at("HomeSystemId");
  result.archetype =
      static_cast<CivilizationArchetype>(value.at("Archetype").get<int>());
  const auto &traits = value.at("Traits");
  result.traits = {number(traits.at("Aggression")),
                   number(traits.at("Territoriality")),
                   number(traits.at("Greed")),
                   number(traits.at("ScientificCuriosity")),
                   number(traits.at("RiskTolerance")),
                   number(traits.at("SurvivalPriority")),
                   traits.at("HonorBound")};
  result.is_player = value.at("IsPlayer");
  result.development_stage = static_cast<CivilizationDevelopmentStage>(
      value.at("DevelopmentStage").get<int>());
  result.is_seeded_ancient = value.at("IsSeededAncient");
  result.expansion_allowed = value.at("ExpansionAllowed");
  result.neutral_unless_provoked = value.at("NeutralUnlessProvoked");
  result.species_id = value.at("SpeciesId");
  for (const auto &office : value.at("Leadership")) {
    const auto &character = office.at("Character");
    result.leadership.push_back(
        {office.at("Office"),
         {character.at("Id"), character.at("DisplayName"),
          optional<std::string>(character.at("VoiceProfileId")),
          optional<std::string>(character.at("Portrait"))}});
  }
  return result;
}

Json encode_civilization(const Civilization &value) {
  Json leadership = Json::array();
  for (const auto &office : value.leadership)
    leadership.push_back(
        {{"Office", office.office},
         {"Character",
          {{"Id", office.character.id},
           {"DisplayName", office.character.display_name},
           {"VoiceProfileId", office.character.voice_profile_id},
           {"Portrait", office.character.portrait}}}});
  return {{"Id", value.id},
          {"Name", value.name},
          {"HomeSystemId", value.home_system_id},
          {"Archetype", value.archetype},
          {"Traits",
           {{"Aggression", encoded_number(value.traits.aggression)},
            {"Territoriality", encoded_number(value.traits.territoriality)},
            {"Greed", encoded_number(value.traits.greed)},
            {"ScientificCuriosity",
             encoded_number(value.traits.scientific_curiosity)},
            {"RiskTolerance", encoded_number(value.traits.risk_tolerance)},
            {"SurvivalPriority",
             encoded_number(value.traits.survival_priority)},
            {"HonorBound", value.traits.honor_bound}}},
          {"IsPlayer", value.is_player},
          {"DevelopmentStage", value.development_stage},
          {"IsSeededAncient", value.is_seeded_ancient},
          {"ExpansionAllowed", value.expansion_allowed},
          {"NeutralUnlessProvoked", value.neutral_unless_provoked},
          {"SpeciesId", value.species_id},
          {"Leadership", std::move(leadership)}};
}

TechnologyState parse_technology(const Json &value) {
  TechnologyState result;
  result.civilization_id = value.at("CivilizationId");
  for (const auto &id : value.at("CompletedTechnologyIds"))
    check(result.completed_technology_ids.insert(id.get<std::string>()),
          "Fixture completed technology IDs must be unique");
  result.active_research_id =
      optional<std::string>(value.at("ActiveResearchId"));
  result.active_research_progress = number(value.at("ActiveResearchProgress"));
  return result;
}

Json encode_technology(const TechnologyState &value) {
  return {{"CivilizationId", value.civilization_id},
          {"CompletedTechnologyIds", value.completed_technology_ids.values()},
          {"ActiveResearchId", value.active_research_id},
          {"ActiveResearchProgress",
           encoded_number(value.active_research_progress)}};
}

ConstructionState parse_construction(const Json &value) {
  ConstructionState result;
  result.civilization_id = value.at("CivilizationId");
  result.completed_project_ids =
      value.at("CompletedProjectIds").get<std::vector<std::string>>();
  result.active_project_id = optional<std::string>(value.at("ActiveProjectId"));
  result.active_project_progress = number(value.at("ActiveProjectProgress"));
  result.active_project_authorization_credits =
      number(value.at("ActiveProjectAuthorizationCredits"));
  for (const auto &queued : value.at("QueuedProjects"))
    result.queued_projects.push_back(
        {queued.at("ProjectId"), number(queued.at("AuthorizationCredits"))});
  return result;
}

Json encode_construction(const ConstructionState &value) {
  Json queued = Json::array();
  for (const auto &entry : value.queued_projects)
    queued.push_back({{"ProjectId", entry.project_id},
                      {"AuthorizationCredits",
                       encoded_number(entry.authorization_credits)}});
  return {{"CivilizationId", value.civilization_id},
          {"CompletedProjectIds", value.completed_project_ids},
          {"ActiveProjectId", value.active_project_id},
          {"ActiveProjectProgress",
           encoded_number(value.active_project_progress)},
          {"ActiveProjectAuthorizationCredits",
           encoded_number(value.active_project_authorization_credits)},
          {"QueuedProjects", std::move(queued)}};
}

CivilizationEconomy parse_economy(const Json &value) {
  CivilizationEconomy result;
  result.civilization_id = value.at("CivilizationId");
  result.credits = number(value.at("Credits"));
  result.industry = number(value.at("Industry"));
  result.science = number(value.at("Science"));
  result.last_credits_per_second = number(value.at("LastCreditsPerSecond"));
  result.last_industry_per_second = number(value.at("LastIndustryPerSecond"));
  result.last_science_per_second = number(value.at("LastSciencePerSecond"));
  result.last_research_spending_per_day =
      number(value.at("LastResearchSpendingPerDay"));
  result.last_research_funding_fraction =
      number(value.at("LastResearchFundingFraction"));
  result.operating_arrears = number(value.at("OperatingArrears"));
  result.last_base_operations_funding_fraction =
      number(value.at("LastBaseOperationsFundingFraction"));
  if (!value.at("IndustryPriority").is_null())
    result.industry_priority =
        static_cast<IndustryPriority>(value.at("IndustryPriority").get<int>());
  return result;
}

Json encode_economy(const CivilizationEconomy &value) {
  return {{"CivilizationId", value.civilization_id},
          {"Credits", encoded_number(value.credits)},
          {"Industry", encoded_number(value.industry)},
          {"Science", encoded_number(value.science)},
          {"LastCreditsPerSecond",
           encoded_number(value.last_credits_per_second)},
          {"LastIndustryPerSecond",
           encoded_number(value.last_industry_per_second)},
          {"LastSciencePerSecond",
           encoded_number(value.last_science_per_second)},
          {"LastResearchSpendingPerDay",
           encoded_number(value.last_research_spending_per_day)},
          {"LastResearchFundingFraction",
           encoded_number(value.last_research_funding_fraction)},
          {"OperatingArrears", encoded_number(value.operating_arrears)},
          {"LastBaseOperationsFundingFraction",
           encoded_number(value.last_base_operations_funding_fraction)},
          {"IndustryPriority", value.industry_priority
                                   ? Json(*value.industry_priority)
                                   : Json(nullptr)}};
}

struct World {
  std::vector<Civilization> civilizations;
  std::vector<TechnologyState> technologies;
  std::vector<ConstructionState> construction;
  std::vector<CivilizationEconomy> economies;

  LegacyResearchWorldView view() {
    return {civilizations, technologies, construction, economies};
  }
};

World parse_world(const Json &value) {
  World result;
  for (const auto &entry : value.at("Civilizations"))
    result.civilizations.push_back(parse_civilization(entry));
  for (const auto &entry : value.at("Technologies"))
    result.technologies.push_back(parse_technology(entry));
  for (const auto &entry : value.at("Construction"))
    result.construction.push_back(parse_construction(entry));
  for (const auto &entry : value.at("Economies"))
    result.economies.push_back(parse_economy(entry));
  return result;
}

Json encode_world(const World &world) {
  Json civilizations = Json::array();
  Json technologies = Json::array();
  Json construction = Json::array();
  Json economies = Json::array();
  for (const auto &value : world.civilizations)
    civilizations.push_back(encode_civilization(value));
  for (const auto &value : world.technologies)
    technologies.push_back(encode_technology(value));
  for (const auto &value : world.construction)
    construction.push_back(encode_construction(value));
  for (const auto &value : world.economies)
    economies.push_back(encode_economy(value));
  return {{"Civilizations", std::move(civilizations)},
          {"Technologies", std::move(technologies)},
          {"Construction", std::move(construction)},
          {"Economies", std::move(economies)}};
}

Json encode_event(const LegacyResearchEvent &value) {
  return {{"CivilizationId", value.civilization_id},
          {"TechnologyId", value.technology_id},
          {"Message", value.message}};
}

Json encode_order(const LegacyResearchOrderResult &value) {
  return {{"Accepted", value.accepted}, {"Message", value.message}};
}

struct ErrorInfo {
  std::string type;
  std::string message;
};
ErrorInfo classify(const std::exception &error) {
  if (dynamic_cast<const std::invalid_argument *>(&error))
    return {"ArgumentException", error.what()};
  if (dynamic_cast<const std::runtime_error *>(&error))
    return {"InvalidOperationException", error.what()};
  return {"UnexpectedNativeException", error.what()};
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "StartResearch" || kind == "Advance" ||
            kind == "AdvanceForCivilization",
        name + ": unknown command kind");
  const auto arguments = test.at("Arguments");
  const auto input_world = arguments.at("World");
  const auto civilization_id = optional<int>(arguments.at("CivilizationId"));
  const auto technology_id = kind == "StartResearch"
                                 ? optional<std::string>(arguments.at("TechnologyId"))
                                 : std::optional<std::string>{};
  const auto expected_before = test.at("Before");
  const auto expected_after = test.at("After");
  const auto expected_result = test.at("Result");
  const auto expected_error = test.at("Error");
  check(input_world == expected_before,
        name + ": Arguments.World differs from Before");
  if (kind == "StartResearch" || kind == "AdvanceForCivilization")
    check(civilization_id.has_value(), name + ": missing civilization ID");
  if (!expected_error.is_null()) {
    check(expected_result.is_null(), name + ": error result must be null");
    check(expected_error.at("Type") == "InvalidOperationException",
          name + ": unsupported source error category");
  } else if (kind == "StartResearch") {
    check(expected_result.is_object(), name + ": order result must be object");
  } else {
    check(expected_result.is_array(), name + ": events must be array");
  }

  auto world = parse_world(input_world);
  equal_json(encode_world(world), expected_before, name + ".Before");
  LegacyResearchSimulation simulation;
  std::optional<LegacyResearchOrderResult> order;
  std::optional<std::vector<LegacyResearchEvent>> events;
  std::optional<ErrorInfo> error;
  try {
    if (kind == "StartResearch")
      order = simulation.start_research(
          world.view(), *civilization_id,
          technology_id ? std::optional<std::string_view>(*technology_id)
                        : std::nullopt);
    else if (kind == "Advance")
      events = simulation.advance(world.view());
    else
      events = simulation.advance_for_civilization(world.view(),
                                                    *civilization_id);
  } catch (const std::exception &caught) {
    error = classify(caught);
  }

  check(error.has_value() == !expected_error.is_null(),
        name + ": error presence mismatch" +
            (error ? " (" + error->type + ": " + error->message + ")" : ""));
  if (error) {
    check(error->type == expected_error.at("Type").get<std::string>(),
          name + ": error type mismatch");
    check(error->message == expected_error.at("Message").get<std::string>(),
          name + ": error message mismatch: " + error->message);
  } else if (order) {
    equal_json(encode_order(*order), expected_result, name + ".Result");
  } else {
    Json encoded = Json::array();
    for (const auto &event : *events)
      encoded.push_back(encode_event(event));
    equal_json(encoded, expected_result, name + ".Result");
  }
  equal_json(encode_world(world), expected_after, name + ".After");
}

void check_source_only_null(const Json &values) {
  check(values.size() == 3, "Expected three null-galaxy observations");
  for (const auto &value : values) {
    const auto command = value.at("Command").get<std::string>();
    check(command == "Advance" || command == "AdvanceForCivilization" ||
              command == "StartResearch",
          "Unknown source-only command");
    check(value.at("Error").at("Type") == "NullReferenceException",
          command + ": null boundary type mismatch");
    check(value.at("Error").at("Message") ==
              "Object reference not set to an instance of an object.",
          command + ": null boundary message mismatch");
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: legacy_research_tests <fixture.json>");
    std::ifstream stream(argv[1]);
    check(stream.good(), "Could not open fixture");
    const auto fixture = Json::parse(stream);
    check(fixture.at("Format") == "stellar-legacy-research-oracle-v1",
          "Unsupported fixture format");
    check(fixture.at("NativeBoundary") ==
              "Typed native spans cannot represent a null galaxy.",
          "Native boundary metadata mismatch");
    check_source_only_null(fixture.at("SourceOnlyNullGalaxy"));
    std::size_t passed = 0;
    for (const auto &test : fixture.at("Cases")) {
      run_case(test);
      ++passed;
    }
    std::cout << "legacy research parity passed " << passed
              << " native actual-C# cases; retained 3 source-only null-galaxy "
                 "observations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "legacy research parity failed: " << error.what() << '\n';
    return 1;
  }
}
