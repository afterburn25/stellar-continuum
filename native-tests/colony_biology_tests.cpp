#include <stellar/core/colony_biology.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using Json = nlohmann::json;
using namespace stellar::core;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

double number(const Json& value) {
    if (value.is_number()) return value.get<double>();
    const auto text = value.get<std::string>();
    if (text == "Infinity") return INFINITY;
    if (text == "-Infinity") return -INFINITY;
    if (text == "NaN") return NAN;
    throw std::runtime_error("Invalid named floating-point literal: " + text);
}

Json json_number(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    return value;
}

template <class T> std::optional<T> optional(const Json& value) {
    if (value.is_null()) return std::nullopt;
    return value.get<T>();
}

PlanetaryBody body(const Json& row) {
    check(row.is_array() && row.size() == 22, "Planetary body must be a compact 22-field row");
    PlanetaryBody value;
    value.id = row.at(0);
    value.system_id = row.at(1);
    value.parent_body_id = optional<int>(row.at(2));
    value.orbit_index = row.at(3);
    value.name = row.at(4);
    value.kind = row.at(5);
    value.radius_earth = number(row.at(6));
    value.mass_earth = number(row.at(7));
    value.environment = {number(row.at(8)), number(row.at(9)), number(row.at(10)), row.at(11), row.at(12),
        number(row.at(13)), row.at(14), row.at(15)};
    value.legacy_colonization_candidate = row.at(16);
    value.has_rare_resource = row.at(17);
    value.has_anomaly = row.at(18);
    value.has_pre_warp_civilization = row.at(19);
    value.orbital_eccentricity = number(row.at(20));
    value.orbital_inclination_degrees = number(row.at(21));
    return value;
}

Json body_json(const PlanetaryBody& value) {
    return {value.id, value.system_id, value.parent_body_id, value.orbit_index, value.name, value.kind,
        json_number(value.radius_earth), json_number(value.mass_earth), json_number(value.environment.gravity_g),
        json_number(value.environment.temperature_kelvin), json_number(value.environment.pressure_kpa),
        value.environment.atmosphere, value.environment.available_solvent, json_number(value.environment.radiation_hazard),
        value.environment.is_immersed_environment, value.environment.has_solid_surface,
        value.legacy_colonization_candidate, value.has_rare_resource, value.has_anomaly,
        value.has_pre_warp_civilization, json_number(value.orbital_eccentricity),
        json_number(value.orbital_inclination_degrees)};
}

SurfaceBuilding building(const Json& value) {
    SurfaceBuilding result;
    result.id = value.at("Id"); result.type_id = value.at("TypeId");
    result.x = value.at("X").get<float>(); result.z = value.at("Z").get<float>();
    result.rotation_degrees = value.at("RotationDegrees").get<float>();
    result.industry_progress = number(value.at("IndustryProgress")); result.is_complete = value.at("IsComplete");
    result.is_enabled = value.at("IsEnabled"); result.pending_upgrade_type_id = optional<std::string>(value.at("PendingUpgradeTypeId"));
    result.upgrade_days_remaining = number(value.at("UpgradeDaysRemaining")); result.operating_priority = value.at("OperatingPriority");
    result.condition = number(value.at("Condition")); result.stored_power_days = number(value.at("StoredPowerDays"));
    return result;
}

Json building_json(const SurfaceBuilding& value) {
    return {{"Id", value.id}, {"TypeId", value.type_id}, {"X", value.x}, {"Z", value.z},
        {"RotationDegrees", value.rotation_degrees}, {"IndustryProgress", json_number(value.industry_progress)},
        {"IsComplete", value.is_complete}, {"IsEnabled", value.is_enabled}, {"PendingUpgradeTypeId", value.pending_upgrade_type_id},
        {"UpgradeDaysRemaining", json_number(value.upgrade_days_remaining)}, {"OperatingPriority", value.operating_priority},
        {"Condition", json_number(value.condition)}, {"StoredPowerDays", json_number(value.stored_power_days)}};
}

Colony colony(const Json& value) {
    Colony result;
    result.id = value.at("Id"); result.civilization_id = value.at("CivilizationId"); result.system_id = value.at("SystemId");
    result.planetary_body_id = optional<int>(value.at("PlanetaryBodyId")); result.name = value.at("Name"); result.kind = value.at("Kind");
    result.population_species_id = value.at("PopulationSpeciesId"); result.population_millions = number(value.at("PopulationMillions"));
    result.infrastructure = number(value.at("Infrastructure")); result.stability = number(value.at("Stability"));
    result.stored_food_population_days_millions = number(value.at("StoredFoodPopulationDaysMillions"));
    result.stored_water_population_days_millions = number(value.at("StoredWaterPopulationDaysMillions"));
    result.stored_extracted_materials = number(value.at("StoredExtractedMaterials"));
    if (!value.at("RemainingExtractableMaterials").is_null()) result.remaining_extractable_materials = number(value.at("RemainingExtractableMaterials"));
    result.surface_hub_level = value.at("SurfaceHubLevel"); result.surface_hub_upgrade_days_remaining = number(value.at("SurfaceHubUpgradeDaysRemaining"));
    for (const auto& item : value.at("SurfaceBuildings")) result.surface_buildings.push_back(building(item));
    return result;
}

Json colony_json(const Colony& value) {
    Json buildings = Json::array();
    for (const auto& item : value.surface_buildings) buildings.push_back(building_json(item));
    return {{"Id", value.id}, {"CivilizationId", value.civilization_id}, {"SystemId", value.system_id},
        {"PlanetaryBodyId", value.planetary_body_id}, {"Name", value.name}, {"Kind", value.kind},
        {"PopulationSpeciesId", value.population_species_id}, {"PopulationMillions", json_number(value.population_millions)},
        {"Infrastructure", json_number(value.infrastructure)}, {"Stability", json_number(value.stability)},
        {"StoredFoodPopulationDaysMillions", json_number(value.stored_food_population_days_millions)},
        {"StoredWaterPopulationDaysMillions", json_number(value.stored_water_population_days_millions)},
        {"StoredExtractedMaterials", json_number(value.stored_extracted_materials)},
        {"RemainingExtractableMaterials", value.remaining_extractable_materials ? json_number(*value.remaining_extractable_materials) : Json(nullptr)},
        {"SurfaceHubLevel", value.surface_hub_level}, {"SurfaceHubUpgradeDaysRemaining", json_number(value.surface_hub_upgrade_days_remaining)},
        {"SurfaceBuildings", buildings}};
}

Json profile_json(const SpeciesBiologyProfile& value) {
    return {{"Id", value.id}, {"TypicalAdultMassKg", json_number(value.typical_adult_mass_kg)},
        {"BaselineLifespanYears", json_number(value.baseline_lifespan_years)}, {"BaselineMetabolicDemand", json_number(value.baseline_metabolic_demand)},
        {"ReproductiveMaturityYears", json_number(value.reproductive_maturity_years)}, {"TypicalOffspringPerEvent", json_number(value.typical_offspring_per_event)},
        {"MinimumInterEventYears", json_number(value.minimum_inter_event_years)}, {"DependentDevelopmentYears", json_number(value.dependent_development_years)},
        {"ReproductiveSpanYears", json_number(value.reproductive_span_years)}, {"BaselineGenerationYears", json_number(value.baseline_generation_years)},
        {"RestingMetabolicFraction", json_number(value.resting_metabolic_fraction)}, {"PeakActivityMetabolicMultiplier", json_number(value.peak_activity_metabolic_multiplier)},
        {"TypicalRestFractionOfDay", json_number(value.typical_rest_fraction_of_day)}, {"DormancyMode", value.dormancy_mode},
        {"DormancyMetabolicDemandFraction", json_number(value.dormancy_metabolic_demand_fraction)}, {"MaximumDormancyDays", json_number(value.maximum_dormancy_days)},
        {"TypicalDormancyRecoveryDays", json_number(value.typical_dormancy_recovery_days)}};
}

Json demographic_json(const SpeciesDemographicPressure& value) {
    return {{"SpeciesId", value.species_id}, {"GenerationPaceFactor", json_number(value.generation_pace_factor)},
        {"ReproductiveEventThroughputFactor", json_number(value.reproductive_event_throughput_factor)},
        {"MaturityPaceFactor", json_number(value.maturity_pace_factor)}, {"IntrinsicGrowthPaceFactor", json_number(value.intrinsic_growth_pace_factor)}};
}

Json metabolic_json(const SpeciesMetabolicEnvelope& value) {
    return {{"SpeciesId", value.species_id}, {"PopulationMillions", json_number(value.population_millions)},
        {"BaselineDemandMillions", json_number(value.baseline_demand_millions)}, {"RestingDemandMillions", json_number(value.resting_demand_millions)},
        {"PeakActivityDemandMillions", json_number(value.peak_activity_demand_millions)}, {"TypicalDayAverageDemandMillions", json_number(value.typical_day_average_demand_millions)},
        {"NaturalDormancyMode", value.natural_dormancy_mode}, {"DormantDemandMillions", json_number(value.dormant_demand_millions)},
        {"MaximumNaturalDormancyDays", json_number(value.maximum_natural_dormancy_days)}, {"TypicalDormancyRecoveryDays", json_number(value.typical_dormancy_recovery_days)},
        {"HasNaturalDormancy", value.has_natural_dormancy}};
}

Json environment_json(const SpeciesEnvironmentAssessment& value) {
    return {{"NaturalHabitability", json_number(value.natural_habitability)}, {"UnprotectedOperationalCapacity", json_number(value.unprotected_operational_capacity)},
        {"GravitySuitability", json_number(value.gravity_suitability)}, {"TemperatureSuitability", json_number(value.temperature_suitability)},
        {"PressureSuitability", json_number(value.pressure_suitability)}, {"AtmosphereSuitability", json_number(value.atmosphere_suitability)},
        {"SolventSuitability", json_number(value.solvent_suitability)}, {"ImmersionSuitability", json_number(value.immersion_suitability)},
        {"RadiationSuitability", json_number(value.radiation_suitability)}, {"LimitingFactor", value.limiting_factor},
        {"RequiresGravityMitigation", value.requires_gravity_mitigation}, {"RequiresThermalControl", value.requires_thermal_control},
        {"RequiresPressureControl", value.requires_pressure_control}, {"RequiresSealedHabitat", value.requires_sealed_habitat},
        {"RequiresArtificialBiosphere", value.requires_artificial_biosphere}, {"RequiresRadiationShielding", value.requires_radiation_shielding}};
}

Json colonization_json(const SpeciesPlanetaryColonizationAssessment& value) {
    return {{"PlanetaryBodyId", value.planetary_body_id}, {"SpeciesId", value.species_id}, {"Environment", environment_json(value.environment)},
        {"Viability", value.viability}, {"HasSolidSurface", value.has_solid_surface}, {"HasNativePreWarpCivilization", value.has_native_pre_warp_civilization},
        {"CanFoundCurrentColony", value.can_found_current_colony}};
}

Json requirements_json(const ColonyEnvironmentalSupportRequirements& value) {
    return {{"PlanetaryBodyId", value.planetary_body_id}, {"ColonizationViability", value.colonization_viability},
        {"NaturalHabitability", json_number(value.natural_habitability)}, {"UnprotectedOperationalCapacity", json_number(value.unprotected_operational_capacity)},
        {"LimitingFactor", value.limiting_factor}, {"RequiredMitigationCategories", value.required_mitigation_categories},
        {"RequiresGravityMitigation", value.requires_gravity_mitigation}, {"RequiresThermalControl", value.requires_thermal_control},
        {"RequiresPressureControl", value.requires_pressure_control}, {"RequiresSealedHabitat", value.requires_sealed_habitat},
        {"RequiresArtificialBiosphere", value.requires_artificial_biosphere}, {"RequiresRadiationShielding", value.requires_radiation_shielding},
        {"RequiresAnyEnvironmentalMitigation", value.requires_any_environmental_mitigation},
        {"UsesPrototypeHabitatSupportedFallback", value.uses_prototype_habitat_supported_fallback}};
}

Json burden_json(const ColonyHabitatSupportBurden& value) {
    return {{"ColonyId", value.colony_id}, {"CivilizationId", value.civilization_id}, {"SystemId", value.system_id}, {"SpeciesId", value.species_id},
        {"PopulationMillions", json_number(value.population_millions)}, {"TypicalDayMetabolicDemandMillions", json_number(value.typical_day_metabolic_demand_millions)},
        {"AdultBiomassMillionKg", json_number(value.adult_biomass_million_kg)}, {"Environment", value.environment ? requirements_json(*value.environment) : Json(nullptr)},
        {"UsesExactOccupiedBody", value.uses_exact_occupied_body}, {"RequiresEnvironmentalSupport", value.requires_environmental_support},
        {"GravityMitigationPopulationMillions", json_number(value.gravity_mitigation_population_millions)}, {"ThermalControlPopulationMillions", json_number(value.thermal_control_population_millions)},
        {"PressureControlPopulationMillions", json_number(value.pressure_control_population_millions)}, {"SealedHabitatPopulationMillions", json_number(value.sealed_habitat_population_millions)},
        {"ArtificialBiospherePopulationMillions", json_number(value.artificial_biosphere_population_millions)}, {"RadiationShieldingPopulationMillions", json_number(value.radiation_shielding_population_millions)}};
}

Json turnover_json(const ColonyPopulationTurnoverPressure& value) {
    return {{"ColonyId", value.colony_id}, {"SpeciesId", value.species_id}, {"IntrinsicGrowthPaceFactor", json_number(value.intrinsic_growth_pace_factor)},
        {"UsesExactOccupiedBody", value.uses_exact_occupied_body}, {"PlanetaryBodyId", value.planetary_body_id}, {"ColonizationViability", value.colonization_viability},
        {"NaturalHabitability", json_number(value.natural_habitability)}, {"NaturalEnvironmentTurnoverFactor", json_number(value.natural_environment_turnover_factor)},
        {"EffectiveGrowthPaceFactor", json_number(value.effective_growth_pace_factor)}, {"LimitingFactor", value.limiting_factor},
        {"RequiresEnvironmentalSupport", value.requires_environmental_support}, {"EnvironmentalPressureApplied", value.environmental_pressure_applied}};
}

void equal_json(const Json& actual, const Json& expected, const std::string& path) {
    if (actual.is_string() && expected.is_string() && (actual == "NaN" || actual == "Infinity" || actual == "-Infinity")) {
        check(actual == expected, path + ": named floating-point value differs"); return;
    }
    if (actual.is_number() && expected.is_number()) {
        if (actual.is_number_integer() && expected.is_number_integer()) { check(actual == expected, path + ": integer differs"); return; }
        const auto a = actual.get<double>(), e = expected.get<double>();
        check((std::isnan(a) && std::isnan(e)) || (std::isinf(a) && std::isinf(e) && std::signbit(a) == std::signbit(e)) ||
            std::abs(a - e) <= 1e-13 * std::max(1.0, std::abs(e)), path + ": expected " + expected.dump() + ", got " + actual.dump());
        return;
    }
    if (actual.is_array() && expected.is_array()) {
        check(actual.size() == expected.size(), path + ": array size");
        for (size_t index = 0; index < actual.size(); ++index) equal_json(actual[index], expected[index], path + "[" + std::to_string(index) + "]");
        return;
    }
    if (actual.is_object() && expected.is_object()) {
        check(actual.size() == expected.size(), path + ": field count");
        for (const auto& [key, value] : expected.items()) { check(actual.contains(key), path + ": missing " + key); equal_json(actual.at(key), value, path + "." + key); }
        return;
    }
    check(actual == expected, path + ": expected " + expected.dump() + ", got " + actual.dump());
}

std::string expected_native_error(const std::string& csharp_error) {
    if (csharp_error.find("cannot resolve an occupied planetary body") != std::string::npos)
        return "body";
    if (csharp_error.find("TypicalDayMetabolicDemandMillions") != std::string::npos ||
        csharp_error.find("AdultBiomassMillionKg") != std::string::npos ||
        csharp_error.find("finite") != std::string::npos || csharp_error.find("Finite") != std::string::npos)
        return "finite";
    if (csharp_error.find("population") != std::string::npos || csharp_error.find("Population") != std::string::npos)
        return "population";
    if (csharp_error.find("unknown species") != std::string::npos || csharp_error.find("Unknown species") != std::string::npos)
        return "species";
    if (csharp_error.find("non-negative") != std::string::npos) return "colony";
    throw std::runtime_error("No bounded native diagnostic mapping for C# error: " + csharp_error);
}

template <class Compute> void run_case(const Json& test, Compute compute) {
    const auto name = test.at("Name").get<std::string>();
    std::function<void()> compare;
    try {
        compare = compute(); // Fixture decoding and intended native operation only.
    } catch (const std::exception& error) {
        if (!test.contains("ExpectedError")) throw std::runtime_error(name + ": native operation threw: " + error.what());
        const auto expected = expected_native_error(test.at("ExpectedError").get<std::string>());
        check(std::string(error.what()).find(expected) != std::string::npos,
            name + ": expected native error containing '" + expected + "', got '" + error.what() + "'");
        return;
    }
    check(!test.contains("ExpectedError"), name + ": expected operation failure but succeeded");
    compare();
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected colony biology oracle path");
        std::ifstream input(argv[1]); check(input.good(), "Unable to read colony biology oracle");
        const auto fixture = Json::parse(input);
        check(fixture.at("Format") == "stellar-colony-biology-parity-v1", "Unknown colony biology oracle format");

        Json catalog = Json::array();
        for (const auto& profile : species_biology_profiles()) catalog.push_back(profile_json(profile));
        equal_json(catalog, fixture.at("Profiles"), "Profiles");
        for (const auto& expected : fixture.at("Profiles"))
            equal_json(profile_json(species_biology_profile(expected.at("Id"))), expected, "Profile lookup");

        size_t count = 0;
        for (const auto& test : fixture.at("Cases")) {
            const auto kind = test.at("Kind").get<std::string>();
            run_case(test, [&]() -> std::function<void()> {
                const auto name = test.at("Name").get<std::string>();
                if (kind == "Demographic") {
                    const auto actual = demographic_json(species_demographic_pressure(test.at("SpeciesId")));
                    return [actual, &test, name] { equal_json(actual, test.at("Expected"), name); };
                }
                if (kind == "Metabolic") {
                    const auto actual = metabolic_json(species_metabolic_envelope(test.at("SpeciesId"), number(test.at("PopulationMillions"))));
                    return [actual, &test, name] { equal_json(actual, test.at("Expected"), name); };
                }
                if (kind == "Colonization") {
                    auto value = body(test.at("Body")); const auto before = body_json(value);
                    const auto actual = colonization_json(species_colonization_assessment(test.at("SpeciesId"), value)); const auto after = body_json(value);
                    return [actual, before, after, &test, name] { equal_json(after, before, name + ".body mutation"); equal_json(actual, test.at("Expected"), name); };
                }
                if (kind == "Burden" || kind == "Turnover") {
                    auto value = colony(test.at("Colony")); const auto colony_before = colony_json(value);
                    std::vector<PlanetaryBody> bodies; for (const auto& row : test.value("Bodies", Json::array())) bodies.push_back(body(row));
                    Json bodies_before = Json::array(); for (const auto& item : bodies) bodies_before.push_back(body_json(item));
                    if (kind == "Burden") {
                        const auto actual = burden_json(colony_habitat_support(value, bodies));
                        Json bodies_after = Json::array(); for (const auto& item : bodies) bodies_after.push_back(body_json(item));
                        return [actual, value, colony_before, bodies_after, bodies_before, &test, name] { equal_json(colony_json(value), colony_before, name + ".colony mutation"); equal_json(bodies_after, bodies_before, name + ".bodies mutation"); equal_json(actual, test.at("Expected"), name); };
                    }
                    const auto actual = turnover_json(colony_population_turnover(value, bodies));
                    Json bodies_after = Json::array(); for (const auto& item : bodies) bodies_after.push_back(body_json(item));
                    return [actual, value, colony_before, bodies_after, bodies_before, &test, name] { equal_json(colony_json(value), colony_before, name + ".colony mutation"); equal_json(bodies_after, bodies_before, name + ".bodies mutation"); equal_json(actual, test.at("Expected"), name); };
                }
                throw std::runtime_error("Unknown colony biology case kind: " + kind);
            });
            ++count;
        }
        std::cout << "colony_biology_tests: passed; " << count << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "colony_biology_tests failed: " << error.what() << '\n';
        return 1;
    }
}
