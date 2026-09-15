#include <stellar/core/ship_designs.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void check(bool condition, const std::string& message) { if (!condition) fail(message); }
void equal_number(double actual, double expected, const std::string& label) {
    const auto scale = std::max({1.0, std::abs(actual), std::abs(expected)});
    check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-11 * scale, label);
}
std::optional<std::string> optional_string(const Json& value) {
    return value.is_null() ? std::nullopt : std::optional<std::string>(value.get<std::string>());
}
ShipDesignDefinition parse_design(const Json& value) {
    const auto& p = value.at("Prerequisites");
    return {value.at("Id"), value.at("Name"), value.at("Description"),
        static_cast<FleetRole>(value.at("Role").get<int>()), value.at("IndustryCost"), value.at("StrategicSpeed"),
        value.at("MaximumLegRangeLightYears"), value.at("FuelEnduranceLightYears"), value.at("SensorRange"),
        {p.at("AllCivilizationCapabilities"), p.at("AnyCivilizationCapabilities"), p.at("RequiredConstructionProjects")},
        value.at("PopulationCostMillions"), optional_string(value.at("CombatProfileId")),
        value.at("CrewComplementIndividuals"), value.at("CreditCost"), value.at("CargoMaterialCapacity"), value.at("CargoTransferRatePerDay")};
}
void check_design(const ShipDesignDefinition& actual, const Json& expected, const std::string& label) {
    check(actual.id == expected.at("Id").get<std::string>(), label + ".Id");
    check(actual.name == expected.at("Name").get<std::string>(), label + ".Name");
    check(actual.description == expected.at("Description").get<std::string>(), label + ".Description");
    check(static_cast<int>(actual.role) == expected.at("Role").get<int>(), label + ".Role");
    equal_number(actual.industry_cost, expected.at("IndustryCost"), label + ".IndustryCost");
    equal_number(actual.strategic_speed, expected.at("StrategicSpeed"), label + ".StrategicSpeed");
    equal_number(actual.maximum_leg_range_light_years, expected.at("MaximumLegRangeLightYears"), label + ".LegRange");
    equal_number(actual.fuel_endurance_light_years, expected.at("FuelEnduranceLightYears"), label + ".FuelEndurance");
    equal_number(actual.sensor_range, expected.at("SensorRange"), label + ".SensorRange");
    const auto& p = expected.at("Prerequisites");
    check(actual.prerequisites.all_civilization_capabilities == p.at("AllCivilizationCapabilities").get<std::vector<std::string>>(), label + ".All");
    check(actual.prerequisites.any_civilization_capabilities == p.at("AnyCivilizationCapabilities").get<std::vector<std::string>>(), label + ".Any");
    check(actual.prerequisites.required_construction_projects == p.at("RequiredConstructionProjects").get<std::vector<std::string>>(), label + ".Projects");
    equal_number(actual.population_cost_millions, expected.at("PopulationCostMillions"), label + ".Population");
    check(actual.combat_profile_id == optional_string(expected.at("CombatProfileId")), label + ".Combat");
    check(actual.crew_complement_individuals == expected.at("CrewComplementIndividuals").get<int>(), label + ".Crew");
    equal_number(actual.credit_cost, expected.at("CreditCost"), label + ".Credit");
    equal_number(actual.cargo_material_capacity, expected.at("CargoMaterialCapacity"), label + ".Cargo");
    equal_number(actual.cargo_transfer_rate_per_day, expected.at("CargoTransferRatePerDay"), label + ".Transfer");
}
struct World {
    std::vector<ConstructionState> construction;
    std::vector<ShipbuildingCapabilities> capabilities;
    ShipDesignReadView read() const { return {construction, capabilities}; }
};
World parse_world(const Json& snapshot) {
    World world;
    for (const auto& item : snapshot.at("ConstructionStates")) {
        ConstructionState state; state.civilization_id = item.at("CivilizationId");
        state.completed_project_ids = item.at("CompletedProjectIds").get<std::vector<std::string>>();
        world.construction.push_back(std::move(state));
    }
    for (const auto& item : snapshot.at("Capabilities"))
        world.capabilities.push_back({item.at("CivilizationId"), item.at("CapabilityIds").get<std::vector<std::string>>()});
    return world;
}
void check_unchanged(const World& world, const Json& snapshot, const std::string& label) {
    const auto expected = parse_world(snapshot);
    check(world.construction.size() == expected.construction.size(), label + ".ConstructionCount");
    check(world.capabilities.size() == expected.capabilities.size(), label + ".CapabilityCount");
    for (size_t i = 0; i < world.construction.size(); ++i)
        check(world.construction[i].civilization_id == expected.construction[i].civilization_id &&
              world.construction[i].completed_project_ids == expected.construction[i].completed_project_ids,
              label + ".Construction");
    for (size_t i = 0; i < world.capabilities.size(); ++i)
        check(world.capabilities[i].civilization_id == expected.capabilities[i].civilization_id &&
              world.capabilities[i].capability_ids == expected.capabilities[i].capability_ids, label + ".Capabilities");
}
void check_error(const std::exception_ptr& error, const Json& expected, const std::string& name) {
    check(error != nullptr, name + ": expected error");
    try { std::rethrow_exception(error); }
    catch (const std::invalid_argument& actual) { check(expected.at("Type") == "InvalidOperationException", name + ": error category"); check(actual.what() == expected.at("Message").get<std::string>(), name + ": error message"); }
    catch (const std::out_of_range& actual) { check(expected.at("Type") == "InvalidOperationException", name + ": error category"); check(actual.what() == expected.at("Message").get<std::string>(), name + ": error message"); }
    catch (...) { fail(name + ": unexpected error category"); }
}
void run_case(const Json& test) {
    const auto name = test.at("Name").get<std::string>(); const auto kind = test.at("Kind").get<std::string>();
    if (kind == "Catalog") {
        const auto actual = ship_design_catalog(); const auto& expected = test.at("Catalog");
        check(actual.size() == expected.size(), name + ".Count");
        for (size_t i = 0; i < actual.size(); ++i) { check_design(actual[i], expected[i], name + "." + std::to_string(i)); check(find_ship_design(actual[i].id) == &actual[i], name + ".Find"); }
        check(find_ship_design("missing") == nullptr, name + ".Missing"); return;
    }
    World world = parse_world(test.at("Before")); check_unchanged(world, test.at("Before"), name + ".Before");
    const auto& args = test.at("Arguments");
    check(kind == "Resolve" || kind == "Lock" || kind == "Available" || kind == "Propulsion" || kind == "Display", name + ": unknown fixture kind");
    int civilization_id = 0;
    if (kind == "Lock" || kind == "Available" || kind == "Propulsion") civilization_id = args.at("CivilizationId").get<int>();
    const auto resolve_id = kind == "Resolve" ? optional_string(args.at("DesignId")) : std::optional<std::string>{};
    const auto role = kind == "Resolve" ? static_cast<FleetRole>(args.at("Role").get<int>()) : FleetRole::Scout;
    const auto lock_design = kind == "Lock" ? std::optional<ShipDesignDefinition>(parse_design(args.at("Design"))) : std::nullopt;
    const auto* propulsion_design = kind == "Propulsion" ? &get_ship_design(args.at("DesignId").get<std::string>()) : nullptr;
    const auto display_id = kind == "Display" ? args.at("CapabilityId").get<std::string>() : std::string{};
    std::exception_ptr error; Json result; const ShipDesignDefinition* resolved = nullptr;
    std::vector<ShipDesignDefinition> available;
    try {
        if (kind == "Resolve") {
            resolved = &resolve_fleet_ship_design(resolve_id ? std::optional<std::string_view>(*resolve_id) : std::nullopt, role);
            result = resolved->id;
        } else if (kind == "Lock") {
            const auto value = ship_design_lock_reason(world.read(), civilization_id, *lock_design);
            result = value ? Json(*value) : Json(nullptr);
        } else if (kind == "Available") {
            available = available_ship_designs(world.read(), civilization_id);
            result = Json::array(); for (const auto& design : available) result.push_back(design.id);
        } else if (kind == "Propulsion") {
            const auto value = effective_ship_propulsion(world.read(), civilization_id, *propulsion_design);
            result = {{"StrategicSpeed", value.strategic_speed}, {"MaximumLegRangeLightYears", value.maximum_leg_range_light_years}, {"FuelEnduranceLightYears", value.fuel_endurance_light_years}, {"PropulsionGeneration", value.propulsion_generation}};
        } else if (kind == "Display") result = shipbuilding_capability_display_name(display_id);
    } catch (...) { error = std::current_exception(); }
    if (!test.at("Error").is_null()) check_error(error, test.at("Error"), name); else {
        check(error == nullptr, name + ": unexpected error"); const auto& expected = test.at("Result");
        if (kind == "Resolve") { check(result == expected.at("Id").get<std::string>(), name + ".Result"); check(resolved != nullptr, name + ".Resolved"); check_design(*resolved, expected, name + ".Design"); }
        else if (kind == "Lock" || kind == "Display") check(result == expected, name + ".Result");
        else if (kind == "Available") { check(result.size() == expected.size(), name + ".Count"); for (size_t i=0;i<result.size();++i) { check(result[i] == expected[i].at("Id").get<std::string>(), name + ".Order"); check_design(available[i], expected[i], name + ".Design" + std::to_string(i)); } }
        else if (kind == "Propulsion") { equal_number(result.at("StrategicSpeed"), expected.at("StrategicSpeed"), name + ".Speed"); equal_number(result.at("MaximumLegRangeLightYears"), expected.at("MaximumLegRangeLightYears"), name + ".Range"); equal_number(result.at("FuelEnduranceLightYears"), expected.at("FuelEnduranceLightYears"), name + ".Fuel"); check(result.at("PropulsionGeneration") == expected.at("PropulsionGeneration"), name + ".Generation"); }
    }
    check_unchanged(world, test.at("After"), name + ".After");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected ship design fixture path"); std::ifstream input(argv[1]); check(input.good(), "Could not open ship design fixture");
        const auto fixture = Json::parse(input); check(fixture.at("Format") == "stellar-ship-designs-oracle-v1", "Unknown fixture format");
        for (const auto& test : fixture.at("Cases")) run_case(test);
        std::cout << "ship_designs_tests: passed " << fixture.at("Cases").size() << " cases\n";
    } catch (const std::exception& error) { std::cerr << "ship_designs_tests failed: " << error.what() << '\n'; return 1; }
    return 0;
}
