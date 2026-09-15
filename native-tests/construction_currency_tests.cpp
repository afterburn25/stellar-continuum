#include <stellar/core/construction_state.hpp>
#include <stellar/core/detail/legacy_number_format.hpp>
#include <stellar/core/sovereign_currency.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace stellar::core;
using Json = nlohmann::json;
namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
double value(const std::string& text) {
    if (text == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (text == "+Infinity") return std::numeric_limits<double>::infinity();
    if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
    return std::stod(text);
}
Civilization civilization(int id, bool ancient = false) {
    Civilization result; result.id = id; result.species_id = "terran_baseline"; result.is_seeded_ancient = ancient; return result;
}
void verify_state_helpers(const Json& seed) {
    const std::vector<Civilization> civilizations{civilization(9), civilization(4, true)};
    const auto before = civilizations;
    const auto seeded = seed_construction(civilizations);
    require(civilizations[0].id == before[0].id && civilizations[0].is_seeded_ancient == before[0].is_seeded_ancient &&
        civilizations[1].id == before[1].id && civilizations[1].is_seeded_ancient == before[1].is_seeded_ancient,
        "construction seed mutated its civilization input");
    require(seeded.size() == 2 && seeded[0].civilization_id == 9 && seeded[0].completed_project_ids.empty(), "normal construction seed changed");
    require(seeded[1].civilization_id == 4 && seeded[1].completed_project_ids == std::vector<std::string>{
        "research_network", "industrial_automation", "orbital_launch_complex", "orbital_shipyard", "asteroid_resource_network", "warp_test_facility"}, "ancient seed did not retain all registry IDs in order");
    const auto& expected_states = seed.at("After");
    require(expected_states.size() == seeded.size(), "C# seed state count changed");
    for (std::size_t index = 0; index < seeded.size(); ++index) {
        const auto& expected = expected_states[index]; const auto& actual = seeded[index];
        require(actual.civilization_id == expected.at("CivilizationId") &&
            actual.completed_project_ids == expected.at("CompletedProjectIds").get<std::vector<std::string>>() &&
            !actual.active_project_id && expected.at("ActiveProjectId").is_null() &&
            actual.active_project_progress == expected.at("ActiveProjectProgress").get<double>() &&
            actual.active_project_authorization_credits == expected.at("ActiveProjectAuthorizationCredits").get<double>() &&
            actual.queued_projects.empty() && expected.at("QueuedProjects").empty(), "seeded construction fields differ from C# oracle");
    }
    std::vector<CivilizationConstructionCapabilities> caps{{7, {"Fusion", "orbital_industry"}}, {7, {"warp_field_control"}}, {8, {"orbital_industry"}}};
    ConstructionReadView view{{}, {}, {}, {}, {}, caps};
    require(construction_has_capability(view, 7, "orbital_industry"), "first resolved capability set was ignored");
    require(!construction_has_capability(view, 7, "warp_field_control"), "capability lookup skipped first matching set");
    require(!construction_has_capability(view, 7, "fusion") && !construction_has_capability(view, 99, "orbital_industry"), "capability lookup lost case sensitivity or missing-state behavior");
    std::vector<ConstructionState> states{{3, {"a", "b", "a", "c"}}, {4, {"x", "x"}}};
    const auto projection = economic_construction_projection(states);
    require(projection.size() == 2 && projection[0].civilization_id == 3 && projection[0].completed_project_ids == std::vector<std::string>{"a", "b", "c"} && projection[1].completed_project_ids == std::vector<std::string>{"x"}, "economic projection was not stable deduplicated completed IDs");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected construction/currency fixture path");
        std::ifstream input(argv[1]);
        require(input.good(), "Could not open construction/currency fixture");
        const Json fixture = Json::parse(input);
        require(fixture.at("Format").get<std::string>() == "stellar-construction-currency-oracle-v1", "Unknown construction/currency fixture format");
        for (const auto& test : fixture.at("CurrencyCases")) {
            const auto currency = sovereign_currency_for_species(test.at("SpeciesId").get<std::string>());
            const auto& expected = test.at("Currency");
            require(currency.name == expected.at("Name").get<std::string>() && currency.code == expected.at("Code").get<std::string>() &&
                currency.symbol == expected.at("Symbol").get<std::string>() && currency.local_units_per_budget_unit == expected.at("LocalUnitsPerBudgetUnit").get<double>(), "currency definition changed");
            const double budget = value(test.at("BudgetUnits").get<std::string>());
            const auto formatted = currency.format(budget); const auto expected_format = test.at("Format").get<std::string>();
            require(formatted == expected_format, "currency format mismatch for " + test.at("SpeciesId").get<std::string>() + " / " + test.at("BudgetUnits").get<std::string>() +
                " (actual " + formatted.substr(0, 48) + ", expected " + expected_format.substr(0, 48) + ")");
            require(currency.format(budget, false) == test.at("FormatWithoutCode").get<std::string>(), "currency code option mismatch");
            require(currency.format_rate(budget) == test.at("Rate").get<std::string>(), "currency rate mismatch");
        }
        for (const auto& test : fixture.at("FixedOneDecimalCases")) {
            require(detail::legacy_custom_fixed(value(test.at("Value").get<std::string>()), 1, 1) == test.at("Format").get<std::string>(),
                "C# 0.0 formatter mismatch for " + test.at("Value").get<std::string>());
        }
        for (const auto& test : fixture.at("FixedPaddingCases")) {
            const auto input = value(test.at("Value").get<std::string>());
            require(detail::legacy_custom_fixed(input, 2, 2) == test.at("FixedTwo").get<std::string>(), "C# 0.00 formatter padding mismatch");
            require(detail::legacy_custom_fixed(input, 3, 3) == test.at("FixedThree").get<std::string>(), "C# 0.000 formatter padding mismatch");
        }
        bool invalid_bounds = false; try { (void)detail::legacy_custom_fixed(1.0, 2, 1); }
        catch (const std::invalid_argument& error) { invalid_bounds = std::string(error.what()) == "Custom fixed fraction digits are invalid."; }
        require(invalid_bounds, "formatter accepted invalid fraction bounds");
        auto second = civilization(4); second.species_id = "compact_high_gravity";
        const std::vector<Civilization> lookup{civilization(4), second};
        const auto lookup_currency = sovereign_currency_for_civilization(lookup, 4);
        const auto& expected_lookup = fixture.at("CivilizationLookup");
        require(lookup_currency.name == expected_lookup.at("Name").get<std::string>() && lookup_currency.code == expected_lookup.at("Code").get<std::string>() &&
            lookup_currency.symbol == expected_lookup.at("Symbol").get<std::string>() && lookup_currency.local_units_per_budget_unit == expected_lookup.at("LocalUnitsPerBudgetUnit").get<double>(), "civilization currency did not use first matching civilization");
        bool rejected = false; try { (void)sovereign_currency_for_civilization(lookup, 99); } catch (const std::out_of_range& error) { rejected = std::string(error.what()) == "Civilization 99 does not exist."; }
        require(rejected, "missing civilization did not preserve bounded C# diagnostic");
        verify_state_helpers(fixture.at("Seed"));
        return 0;
    } catch (const std::exception& error) { std::cerr << "construction_currency_tests failed: " << error.what() << '\n'; }
    catch (...) { std::cerr << "construction_currency_tests failed: unknown exception\n"; }
    return 1;
}
