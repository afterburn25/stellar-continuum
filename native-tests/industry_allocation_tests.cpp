#include <stellar/core/industry_allocation.hpp>

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
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void check(bool condition, const std::string& message) { if (!condition) fail(message); }

double number(const Json& value) {
    if (value.is_number()) return value.get<double>();
    const auto text = value.get<std::string>();
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (text == "Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
    fail("Unsupported named floating-point value: " + text);
}

void check_number(double actual, double expected, const std::string& field) {
    if (std::isnan(expected)) { check(std::isnan(actual), field + ": expected NaN"); return; }
    if (std::isinf(expected)) { check(actual == expected, field + ": infinity mismatch"); return; }
    const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
    check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-12 * scale,
        field + ": numeric mismatch");
}

std::optional<IndustryPriority> priority(const Json& value) {
    if (value.is_null()) return std::nullopt;
    return static_cast<IndustryPriority>(value.get<int>());
}

CivilizationEconomy parse_economy(const Json& value) {
    CivilizationEconomy economy;
    economy.civilization_id = value.at("CivilizationId").get<int>();
    economy.credits = number(value.at("Credits"));
    economy.industry = number(value.at("Industry"));
    economy.science = number(value.at("Science"));
    economy.last_credits_per_second = number(value.at("LastCreditsPerSecond"));
    economy.last_industry_per_second = number(value.at("LastIndustryPerSecond"));
    economy.last_science_per_second = number(value.at("LastSciencePerSecond"));
    economy.last_research_spending_per_day = number(value.at("LastResearchSpendingPerDay"));
    economy.last_research_funding_fraction = number(value.at("LastResearchFundingFraction"));
    economy.operating_arrears = number(value.at("OperatingArrears"));
    economy.last_base_operations_funding_fraction = number(value.at("LastBaseOperationsFundingFraction"));
    economy.industry_priority = priority(value.at("IndustryPriority"));
    return economy;
}

std::vector<CivilizationEconomy> parse_economies(const Json& values) {
    std::vector<CivilizationEconomy> economies;
    economies.reserve(values.size());
    for (const auto& value : values) economies.push_back(parse_economy(value));
    return economies;
}

void check_economy(const CivilizationEconomy& actual, const Json& expected, const std::string& label) {
    check(actual.civilization_id == expected.at("CivilizationId").get<int>(), label + ".CivilizationId");
    check_number(actual.credits, number(expected.at("Credits")), label + ".Credits");
    check_number(actual.industry, number(expected.at("Industry")), label + ".Industry");
    check_number(actual.science, number(expected.at("Science")), label + ".Science");
    check_number(actual.last_credits_per_second, number(expected.at("LastCreditsPerSecond")), label + ".LastCreditsPerSecond");
    check_number(actual.last_industry_per_second, number(expected.at("LastIndustryPerSecond")), label + ".LastIndustryPerSecond");
    check_number(actual.last_science_per_second, number(expected.at("LastSciencePerSecond")), label + ".LastSciencePerSecond");
    check_number(actual.last_research_spending_per_day, number(expected.at("LastResearchSpendingPerDay")), label + ".LastResearchSpendingPerDay");
    check_number(actual.last_research_funding_fraction, number(expected.at("LastResearchFundingFraction")), label + ".LastResearchFundingFraction");
    check_number(actual.operating_arrears, number(expected.at("OperatingArrears")), label + ".OperatingArrears");
    check_number(actual.last_base_operations_funding_fraction,
        number(expected.at("LastBaseOperationsFundingFraction")), label + ".LastBaseOperationsFundingFraction");
    check(actual.industry_priority == priority(expected.at("IndustryPriority")), label + ".IndustryPriority");
}

void check_economies(const std::vector<CivilizationEconomy>& actual, const Json& expected,
    const std::string& label) {
    check(actual.size() == expected.size(), label + ": economy count mismatch");
    for (std::size_t index = 0; index < actual.size(); ++index)
        check_economy(actual[index], expected[index], label + "[" + std::to_string(index) + "]");
}

struct ExpectedError { std::string category; std::string message; };

ExpectedError expected_error(const std::string& oracle_message) {
    if (oracle_message.find("AvailableIndustry") != std::string::npos)
        return {"out_of_range", "AvailableIndustry must be finite and non-negative."};
    if (oracle_message.find("ConstructionDemand") != std::string::npos)
        return {"out_of_range", "ConstructionDemand must be finite and non-negative."};
    if (oracle_message.find("ShipbuildingDemand") != std::string::npos)
        return {"out_of_range", "ShipbuildingDemand must be finite and non-negative."};
    if (oracle_message.find("Construction weight must") != std::string::npos)
        return {"out_of_range", "Construction weight must be finite and greater than zero."};
    if (oracle_message.find("Shipbuilding weight must") != std::string::npos)
        return {"out_of_range", "Shipbuilding weight must be finite and greater than zero."};
    if (oracle_message == "Unknown persisted industry priority.")
        return {"invalid_argument", oracle_message};
    fail("Unrecognized oracle error: " + oracle_message);
}

std::string exception_category(const std::exception& error) {
    if (dynamic_cast<const std::out_of_range*>(&error)) return "out_of_range";
    if (dynamic_cast<const std::invalid_argument*>(&error)) return "invalid_argument";
    return "unexpected";
}

void check_native_error(const std::exception_ptr& error, const ExpectedError& expected,
    const std::string& name) {
    check(error != nullptr, name + ": expected native operation to throw");
    try { std::rethrow_exception(error); }
    catch (const std::exception& native_error) {
        const auto category = exception_category(native_error);
        check(category == expected.category, name + ": wrong exception category: " + category);
        check(native_error.what() == expected.message,
            name + ": wrong exception field/message: " + native_error.what());
    }
}

void run_allocation(const Json& test, const std::string& name) {
    const auto& context_json = test.at("Context");
    const auto& weights_json = test.at("Weights");
    const IndustryAllocationContext context{context_json.at("CivilizationId").get<int>(),
        number(context_json.at("AvailableIndustry")), number(context_json.at("ConstructionDemand")),
        number(context_json.at("ShipbuildingDemand"))};
    const IndustryPriorityWeights weights{number(weights_json.at("ConstructionWeight")),
        number(weights_json.at("ShipbuildingWeight"))};
    const auto error_expectation = test.contains("ExpectedError")
        ? std::optional<ExpectedError>(expected_error(test.at("ExpectedError").get<std::string>())) : std::nullopt;

    std::optional<CivilizationIndustryAllocation> actual;
    std::exception_ptr operation_error;
    try { actual = allocate_industry(context, weights); }
    catch (...) { operation_error = std::current_exception(); }
    if (error_expectation) { check_native_error(operation_error, *error_expectation, name); return; }
    check(operation_error == nullptr, name + ": unexpected native exception");
    check(actual.has_value(), name + ": allocation result missing");

    const auto& expected = test.at("Expected");
    check(actual->civilization_id == expected.at("CivilizationId").get<int>(), name + ".CivilizationId");
    check_number(actual->available_industry, number(expected.at("AvailableIndustry")), name + ".AvailableIndustry");
    check_number(actual->construction_demand, number(expected.at("ConstructionDemand")), name + ".ConstructionDemand");
    check_number(actual->shipbuilding_demand, number(expected.at("ShipbuildingDemand")), name + ".ShipbuildingDemand");
    check_number(actual->construction_weight, number(expected.at("ConstructionWeight")), name + ".ConstructionWeight");
    check_number(actual->shipbuilding_weight, number(expected.at("ShipbuildingWeight")), name + ".ShipbuildingWeight");
    check_number(actual->construction_allocated, number(expected.at("ConstructionAllocated")), name + ".ConstructionAllocated");
    check_number(actual->shipbuilding_allocated, number(expected.at("ShipbuildingAllocated")), name + ".ShipbuildingAllocated");
    check_number(actual->total_allocated, number(expected.at("TotalAllocated")), name + ".TotalAllocated");
}

void run_weights(const Json& test, const std::string& name) {
    auto economies = parse_economies(test.at("Economies"));
    const auto& fallback_json = test.at("Fallback");
    const IndustryPriorityWeights fallback{number(fallback_json.at("ConstructionWeight")),
        number(fallback_json.at("ShipbuildingWeight"))};
    const auto civilization_id = test.at("CivilizationId").get<int>();
    const auto error_expectation = test.contains("ExpectedError")
        ? std::optional<ExpectedError>(expected_error(test.at("ExpectedError").get<std::string>())) : std::nullopt;

    std::optional<IndustryPriorityWeights> actual;
    std::exception_ptr operation_error;
    try { actual = campaign_industry_weights(economies, civilization_id, fallback); }
    catch (...) { operation_error = std::current_exception(); }
    check_economies(economies, test.at("Economies"), name + ".After");
    if (error_expectation) { check_native_error(operation_error, *error_expectation, name); return; }
    check(operation_error == nullptr, name + ": unexpected native exception");
    check(actual.has_value(), name + ": weights result missing");
    const auto& expected = test.at("Expected");
    check_number(actual->construction_weight, number(expected.at("ConstructionWeight")), name + ".ConstructionWeight");
    check_number(actual->shipbuilding_weight, number(expected.at("ShipbuildingWeight")), name + ".ShipbuildingWeight");
}

void run_command(const Json& test, const std::string& name) {
    auto economies = parse_economies(test.at("Economies"));
    const auto actor = test.at("Actor").get<int>();
    const auto target = test.at("Target").get<int>();
    const auto requested_priority = static_cast<IndustryPriority>(test.at("Priority").get<int>());
    const auto& before_economies = test.at("Before").at("Economies");
    const auto& after_economies = test.at("After");
    const auto expected_accepted = test.at("Expected").at("Accepted").get<bool>();
    const auto expected_message = test.at("Expected").at("Message").get<std::string>();

    check_economies(economies, before_economies, name + ".Before");
    const auto actual = set_industry_priority(economies, actor, target, requested_priority);
    check(actual.accepted == expected_accepted, name + ".Accepted");
    check(actual.message == expected_message, name + ".Message");
    check_economies(economies, after_economies, name + ".After");
}

void run_funding(const Json& test, const std::string& name) {
    auto economies = parse_economies(test.at("Economies"));
    const auto civilization_id = test.at("CivilizationId").get<int>();
    const auto expected = number(test.at("Expected"));
    const auto actual = civilization_operating_funding(economies, civilization_id);
    check_number(actual, expected, name + ".Expected");
    check_economies(economies, test.at("Economies"), name + ".After");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected industry allocation fixture path");
        std::ifstream input(argv[1]);
        check(input.good(), "Could not open industry allocation fixture");
        const auto fixture = Json::parse(input);
        check(fixture.at("Format") == "stellar-industry-allocation-parity-v1", "Unexpected fixture format");
        check(fixture.at("Cases").is_array(), "Fixture Cases must be an array");
        for (const auto& test : fixture.at("Cases")) {
            const auto name = test.at("Name").get<std::string>();
            const auto kind = test.at("Kind").get<std::string>();
            if (kind == "Allocation") run_allocation(test, name);
            else if (kind == "Weights") run_weights(test, name);
            else if (kind == "Command") run_command(test, name);
            else if (kind == "Funding") run_funding(test, name);
            else fail(name + ": unknown case kind " + kind);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
