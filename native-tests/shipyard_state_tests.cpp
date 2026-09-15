#include <stellar/core/shipyard_state.hpp>

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
  fail("Unsupported named number: " + text);
}

void check_number(double actual, double expected, const std::string &field) {
  if (std::isnan(expected)) {
    check(std::isnan(actual), field + ": expected NaN");
    return;
  }
  check(actual == expected, field + ": number mismatch");
}

template <typename T> std::optional<T> optional_value(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<T>(value.get<T>());
}

template <typename T>
void check_optional(const std::optional<T> &actual, const Json &expected,
                    const std::string &field) {
  check(actual.has_value() != expected.is_null(), field + ": presence");
  if (actual)
    check(*actual == expected.get<T>(), field + ": value");
}

ShipBuildOrderState parse_order(const Json &value, const std::string &field) {
  // C# required reference records cannot be represented by the native value
  // vector. Reject null explicitly at this import boundary instead of dropping
  // it.
  check(!value.is_null(), field + ": null queued build cannot be imported");
  ShipBuildOrderState result;
  result.order_id = value.at("OrderId").get<std::string>();
  result.design_id = value.at("DesignId").get<std::string>();
  result.authorization_credits = number(value.at("AuthorizationCredits"));
  result.reserved_population_millions =
      number(value.at("ReservedPopulationMillions"));
  result.reserved_population_species_id =
      optional_value<std::string>(value.at("ReservedPopulationSpeciesId"));
  result.reserved_population_source_colony_id =
      optional_value<int>(value.at("ReservedPopulationSourceColonyId"));
  return result;
}

ShipyardState parse_state(const Json &value, const std::string &field) {
  ShipyardState result;
  result.civilization_id = value.at("CivilizationId").get<int>();
  result.next_order_sequence =
      value.at("NextOrderSequence").get<std::int64_t>();
  result.active_design_id =
      optional_value<std::string>(value.at("ActiveDesignId"));
  result.active_order_id =
      optional_value<std::string>(value.at("ActiveOrderId"));
  result.active_build_progress = number(value.at("ActiveBuildProgress"));
  result.active_authorization_credits =
      number(value.at("ActiveAuthorizationCredits"));
  result.reserved_population_millions =
      number(value.at("ReservedPopulationMillions"));
  result.reserved_population_species_id =
      optional_value<std::string>(value.at("ReservedPopulationSpeciesId"));
  result.reserved_population_source_colony_id =
      optional_value<int>(value.at("ReservedPopulationSourceColonyId"));
  const auto &queue = value.at("QueuedBuilds");
  check(queue.is_array(), field + ": queued builds must be an array");
  for (std::size_t index = 0; index < queue.size(); ++index)
    result.queued_builds.push_back(parse_order(
        queue[index], field + ".QueuedBuilds[" + std::to_string(index) + "]"));
  return result;
}

void check_order(const ShipBuildOrderState &actual, const Json &expected,
                 const std::string &field) {
  check(actual.order_id == expected.at("OrderId").get<std::string>(),
        field + ".OrderId");
  check(actual.design_id == expected.at("DesignId").get<std::string>(),
        field + ".DesignId");
  check_number(actual.authorization_credits,
               number(expected.at("AuthorizationCredits")),
               field + ".AuthorizationCredits");
  check_number(actual.reserved_population_millions,
               number(expected.at("ReservedPopulationMillions")),
               field + ".ReservedPopulationMillions");
  check_optional(actual.reserved_population_species_id,
                 expected.at("ReservedPopulationSpeciesId"),
                 field + ".ReservedPopulationSpeciesId");
  check_optional(actual.reserved_population_source_colony_id,
                 expected.at("ReservedPopulationSourceColonyId"),
                 field + ".ReservedPopulationSourceColonyId");
}

void check_state(const ShipyardState &actual, const Json &expected,
                 const std::string &field, bool includes_pending = false) {
  check(actual.civilization_id == expected.at("CivilizationId").get<int>(),
        field + ".CivilizationId");
  check(actual.next_order_sequence ==
            expected.at("NextOrderSequence").get<std::int64_t>(),
        field + ".NextOrderSequence");
  check_optional(actual.active_design_id, expected.at("ActiveDesignId"),
                 field + ".ActiveDesignId");
  check_optional(actual.active_order_id, expected.at("ActiveOrderId"),
                 field + ".ActiveOrderId");
  check_number(actual.active_build_progress,
               number(expected.at("ActiveBuildProgress")),
               field + ".ActiveBuildProgress");
  check_number(actual.active_authorization_credits,
               number(expected.at("ActiveAuthorizationCredits")),
               field + ".ActiveAuthorizationCredits");
  check_number(actual.reserved_population_millions,
               number(expected.at("ReservedPopulationMillions")),
               field + ".ReservedPopulationMillions");
  check_optional(actual.reserved_population_species_id,
                 expected.at("ReservedPopulationSpeciesId"),
                 field + ".ReservedPopulationSpeciesId");
  check_optional(actual.reserved_population_source_colony_id,
                 expected.at("ReservedPopulationSourceColonyId"),
                 field + ".ReservedPopulationSourceColonyId");
  const auto &queue = expected.at("QueuedBuilds");
  check(actual.queued_builds.size() == queue.size(),
        field + ".QueuedBuilds count");
  for (std::size_t index = 0; index < actual.queued_builds.size(); ++index)
    check_order(actual.queued_builds[index], queue[index],
                field + ".QueuedBuilds[" + std::to_string(index) + "]");
  if (includes_pending)
    check(actual.pending_build_count() ==
              expected.at("PendingBuildCount").get<int>(),
          field + ".PendingBuildCount");
}

Civilization parse_civilization(const Json &value, const std::string &field) {
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
  const auto &offices = value.at("Leadership").at("Offices");
  check(offices.is_object() && offices.empty(),
        field + ": unsupported nonempty leadership import");
  return result;
}

void check_civilization(const Civilization &actual, const Json &expected,
                        const std::string &field) {
  check(actual.id == expected.at("Id").get<int>() &&
            actual.name == expected.at("Name").get<std::string>() &&
            actual.home_system_id == expected.at("HomeSystemId").get<int>() &&
            static_cast<int>(actual.archetype) ==
                expected.at("Archetype").get<int>(),
        field + ": identity");
  const auto &traits = expected.at("Traits");
  check_number(actual.traits.aggression, number(traits.at("Aggression")),
               field + ".Traits.Aggression");
  check_number(actual.traits.territoriality,
               number(traits.at("Territoriality")),
               field + ".Traits.Territoriality");
  check_number(actual.traits.greed, number(traits.at("Greed")),
               field + ".Traits.Greed");
  check_number(actual.traits.scientific_curiosity,
               number(traits.at("ScientificCuriosity")),
               field + ".Traits.ScientificCuriosity");
  check_number(actual.traits.risk_tolerance, number(traits.at("RiskTolerance")),
               field + ".Traits.RiskTolerance");
  check_number(actual.traits.survival_priority,
               number(traits.at("SurvivalPriority")),
               field + ".Traits.SurvivalPriority");
  check(actual.traits.honor_bound == traits.at("HonorBound").get<bool>() &&
            actual.is_player == expected.at("IsPlayer").get<bool>() &&
            static_cast<int>(actual.development_stage) ==
                expected.at("DevelopmentStage").get<int>() &&
            actual.is_seeded_ancient ==
                expected.at("IsSeededAncient").get<bool>() &&
            actual.expansion_allowed ==
                expected.at("ExpansionAllowed").get<bool>() &&
            actual.neutral_unless_provoked ==
                expected.at("NeutralUnlessProvoked").get<bool>() &&
            actual.species_id == expected.at("SpeciesId").get<std::string>() &&
            actual.leadership.empty(),
        field + ": remaining fields");
}

void check_error(const std::exception_ptr &error, const Json &expected,
                 const std::string &name) {
  check(error != nullptr, name + ": expected error");
  std::string actual_type;
  std::string actual_message;
  try {
    std::rethrow_exception(error);
  } catch (const std::invalid_argument &actual) {
    actual_type = "InvalidOperationException";
    actual_message = actual.what();
  } catch (const std::exception &actual) {
    actual_type = "UnexpectedNativeException";
    actual_message = actual.what();
  }
  check(actual_type == expected.at("Type").get<std::string>(),
        name + ": error category");
  check(actual_message == expected.at("Message").get<std::string>(),
        name + ": error message");
}

void run_case(const Json &test) {
  const auto name = test.at("Name").get<std::string>();
  const auto kind = test.at("Kind").get<std::string>();
  check(kind == "Format" || kind == "Parse" || kind == "ValidId" ||
            kind == "State" || kind == "Guard" || kind == "Seed",
        name + ": unknown kind");
  const auto &arguments = test.at("Arguments");

  const auto order_id =
      (kind == "Parse" || kind == "ValidId")
          ? optional_value<std::string>(arguments.at("OrderId"))
          : std::nullopt;
  const auto civilization_id = (kind == "Format" || kind == "Parse")
                                   ? arguments.at("CivilizationId").get<int>()
                                   : 0;
  const auto sequence =
      kind == "Format" ? arguments.at("Sequence").get<std::int64_t>() : 0;
  std::optional<ShipyardState> state;
  if (kind == "State" || kind == "Guard")
    state = parse_state(arguments, name + ".Arguments");
  std::vector<Civilization> civilizations;
  if (kind == "Seed") {
    const auto &values = arguments.at("Civilizations");
    check(values.is_array(), name + ": civilizations must be an array");
    for (std::size_t index = 0; index < values.size(); ++index)
      civilizations.push_back(
          parse_civilization(values[index], name + ".Civilizations[" +
                                                std::to_string(index) + "]"));
  }
  const auto expected_error_type =
      test.at("Error").is_null()
          ? std::string{}
          : test.at("Error").at("Type").get<std::string>();
  check(expected_error_type.empty() ||
            expected_error_type == "InvalidOperationException",
        name + ": unsupported expected error category");
  if (state)
    check_state(*state, test.at("Before"), name + ".Before");

  std::optional<std::string> string_result;
  std::optional<bool> bool_result;
  std::int64_t parsed_sequence = 917;
  std::optional<int> count_result;
  std::optional<std::vector<ShipyardState>> seed_result;
  std::exception_ptr operation_error;
  try {
    if (kind == "Format")
      string_result = format_shipyard_order_id(civilization_id, sequence);
    else if (kind == "Parse")
      bool_result = try_read_canonical_shipyard_sequence(
          order_id ? std::optional<std::string_view>(*order_id) : std::nullopt,
          civilization_id, parsed_sequence);
    else if (kind == "ValidId")
      bool_result = is_valid_persisted_shipyard_order_id(
          order_id ? std::optional<std::string_view>(*order_id) : std::nullopt);
    else if (kind == "State")
      count_result = state->pending_build_count();
    else if (kind == "Guard") {
      validate_shipyard_population_persistence_safety(*state);
      count_result = static_cast<int>(state->queued_builds.size());
    } else if (kind == "Seed")
      seed_result = seed_shipyards(civilizations);
  } catch (...) {
    operation_error = std::current_exception();
  }

  if (!test.at("Error").is_null()) {
    check_error(operation_error, test.at("Error"), name);
    check(test.at("Result").is_null(), name + ": errored result must be null");
  } else {
    check(operation_error == nullptr, name + ": unexpected error");
    const auto &result = test.at("Result");
    if (kind == "Format")
      check(*string_result == result.get<std::string>(), name + ": format");
    else if (kind == "Parse") {
      check(*bool_result == result.at("Ok").get<bool>(), name + ": parse flag");
      check(parsed_sequence == result.at("Sequence").get<std::int64_t>(),
            name + ": parsed sequence");
    } else if (kind == "ValidId")
      check(*bool_result == result.get<bool>(), name + ": validity");
    else if (kind == "State") {
      check(*count_result == result.at("PendingBuildCount").get<int>(),
            name + ": pending count");
      check_state(*state, result, name + ".Result", true);
    } else if (kind == "Guard")
      check(*count_result == result.get<int>(), name + ": queue count");
    else {
      check(seed_result->size() == result.size(), name + ": seeded count");
      for (std::size_t index = 0; index < seed_result->size(); ++index)
        check_state((*seed_result)[index], result[index],
                    name + ".Result[" + std::to_string(index) + "]", true);
    }
  }

  if (state)
    check_state(*state, test.at("After"), name + ".After");
  if (kind == "Seed")
    for (std::size_t index = 0; index < civilizations.size(); ++index)
      check_civilization(civilizations[index],
                         arguments.at("Civilizations")[index],
                         name + ".Input[" + std::to_string(index) + "]");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Expected shipyard fixture path");
    std::ifstream input(argv[1]);
    check(input.good(), "Could not open shipyard fixture");
    const auto fixture = Json::parse(input);
    check(fixture.at("Format").get<std::string>() ==
              "stellar-shipyard-state-oracle-v2",
          "Unknown shipyard fixture format");
    for (const auto &test : fixture.at("Cases"))
      run_case(test);
    std::cout << "shipyard_state_tests: passed " << fixture.at("Cases").size()
              << " cases\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "shipyard_state_tests failed: " << error.what() << '\n';
    return 1;
  }
}
