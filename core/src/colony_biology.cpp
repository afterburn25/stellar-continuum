#include <stellar/core/colony_biology.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr std::string_view terran = "terran_baseline";
constexpr std::string_view pelagic = "pelagic_high_pressure";
constexpr std::string_view compact = "compact_high_gravity";
constexpr std::string_view cryogenic = "cryogenic_hydrocarbon";

const std::vector<SpeciesBiologyProfile>& profiles() {
    static const std::vector<SpeciesBiologyProfile> values = [] {
        SpeciesBiologyProfile terran_profile;
        terran_profile.id = std::string(terran); terran_profile.typical_adult_mass_kg = 70.0;
        terran_profile.baseline_lifespan_years = 82.0; terran_profile.baseline_metabolic_demand = 1.0;
        terran_profile.reproductive_maturity_years = 18.0; terran_profile.typical_offspring_per_event = 1.05;
        terran_profile.minimum_inter_event_years = 1.5; terran_profile.dependent_development_years = 16.0;
        terran_profile.reproductive_span_years = 32.0; terran_profile.baseline_generation_years = 28.0;
        terran_profile.resting_metabolic_fraction = .72; terran_profile.peak_activity_metabolic_multiplier = 2.50;
        terran_profile.typical_rest_fraction_of_day = .33; terran_profile.dormancy_mode = NaturalDormancyMode::None;
        terran_profile.dormancy_metabolic_demand_fraction = 1.0; terran_profile.maximum_dormancy_days = 0.0;
        terran_profile.typical_dormancy_recovery_days = 0.0;
        SpeciesBiologyProfile pelagic_profile;
        pelagic_profile.id = std::string(pelagic); pelagic_profile.typical_adult_mass_kg = 110.0;
        pelagic_profile.baseline_lifespan_years = 140.0; pelagic_profile.baseline_metabolic_demand = .85;
        pelagic_profile.reproductive_maturity_years = 24.0; pelagic_profile.typical_offspring_per_event = 2.0;
        pelagic_profile.minimum_inter_event_years = 3.0; pelagic_profile.dependent_development_years = 10.0;
        pelagic_profile.reproductive_span_years = 80.0; pelagic_profile.baseline_generation_years = 40.0;
        pelagic_profile.resting_metabolic_fraction = .65; pelagic_profile.peak_activity_metabolic_multiplier = 2.00;
        pelagic_profile.typical_rest_fraction_of_day = .30; pelagic_profile.dormancy_mode = NaturalDormancyMode::Torpor;
        pelagic_profile.dormancy_metabolic_demand_fraction = .45; pelagic_profile.maximum_dormancy_days = 14.0;
        pelagic_profile.typical_dormancy_recovery_days = 1.0;
        SpeciesBiologyProfile compact_profile;
        compact_profile.id = std::string(compact); compact_profile.typical_adult_mass_kg = 125.0;
        compact_profile.baseline_lifespan_years = 96.0; compact_profile.baseline_metabolic_demand = 1.20;
        compact_profile.reproductive_maturity_years = 20.0; compact_profile.typical_offspring_per_event = 1.0;
        compact_profile.minimum_inter_event_years = 2.4; compact_profile.dependent_development_years = 15.0;
        compact_profile.reproductive_span_years = 50.0; compact_profile.baseline_generation_years = 31.0;
        compact_profile.resting_metabolic_fraction = .75; compact_profile.peak_activity_metabolic_multiplier = 2.80;
        compact_profile.typical_rest_fraction_of_day = .36; compact_profile.dormancy_mode = NaturalDormancyMode::None;
        compact_profile.dormancy_metabolic_demand_fraction = 1.0; compact_profile.maximum_dormancy_days = 0.0;
        compact_profile.typical_dormancy_recovery_days = 0.0;
        SpeciesBiologyProfile cryogenic_profile;
        cryogenic_profile.id = std::string(cryogenic); cryogenic_profile.typical_adult_mass_kg = 90.0;
        cryogenic_profile.baseline_lifespan_years = 360.0; cryogenic_profile.baseline_metabolic_demand = .22;
        cryogenic_profile.reproductive_maturity_years = 55.0; cryogenic_profile.typical_offspring_per_event = 1.5;
        cryogenic_profile.minimum_inter_event_years = 8.0; cryogenic_profile.dependent_development_years = 30.0;
        cryogenic_profile.reproductive_span_years = 220.0; cryogenic_profile.baseline_generation_years = 82.0;
        cryogenic_profile.resting_metabolic_fraction = .30; cryogenic_profile.peak_activity_metabolic_multiplier = 1.70;
        cryogenic_profile.typical_rest_fraction_of_day = .45; cryogenic_profile.dormancy_mode = NaturalDormancyMode::DeepDormancy;
        cryogenic_profile.dormancy_metabolic_demand_fraction = .08; cryogenic_profile.maximum_dormancy_days = 180.0;
        cryogenic_profile.typical_dormancy_recovery_days = 7.0;
        return std::vector<SpeciesBiologyProfile>{terran_profile, pelagic_profile, compact_profile, cryogenic_profile};
    }();
    return values;
}

bool blank(std::string_view value) { return value.empty(); }
void validate_population_species(const Colony& colony) {
    if (!std::isfinite(colony.population_millions) || colony.population_millions <= 0.0)
        throw std::invalid_argument("Colony has no valid positive population to evaluate");
    if (blank(colony.population_species_id)) throw std::invalid_argument("Colony references unknown species");
}
void validate_habitat_ids(const Colony& colony) {
    if (colony.id < 0 || colony.civilization_id < 0 || colony.system_id < 0)
        throw std::invalid_argument("colony habitat-support burden IDs must be non-negative");
}
void validate_turnover_id(const Colony& colony) {
    if (colony.id < 0) throw std::invalid_argument("colony population-turnover pressure requires a non-negative colony ID");
}
void validate_burden_result(const ColonyHabitatSupportBurden& burden) {
    if (!std::isfinite(burden.typical_day_metabolic_demand_millions) || burden.typical_day_metabolic_demand_millions <= 0.0)
        throw std::invalid_argument("Typical metabolic demand must be finite and positive");
    if (!std::isfinite(burden.adult_biomass_million_kg) || burden.adult_biomass_million_kg <= 0.0)
        throw std::invalid_argument("Adult biomass must be finite and positive");
}

const PlanetaryBody* exact_body(const Colony& colony, std::span<const PlanetaryBody> bodies) {
    if (!colony.planetary_body_id) return nullptr;
    const auto found = std::find_if(bodies.begin(), bodies.end(), [&](const PlanetaryBody& body) {
        return body.id == *colony.planetary_body_id && body.system_id == colony.system_id;
    });
    if (found == bodies.end()) throw std::invalid_argument("Colony cannot resolve an occupied planetary body");
    return &*found;
}

SpeciesColonizationViability viability_for(const SpeciesEnvironmentProfile& species, const PlanetaryBody& body,
    SpeciesEnvironmentAssessment& environment) {
    const PlanetSpeciesAssessment canonical = assess_species_planet(species, body);
    environment = canonical.environment;
    const bool natural = canonical.naturally_colonizable && !body.has_pre_warp_civilization;
    const bool fallback = !natural && body.legacy_colonization_candidate && body.environment.has_solid_surface &&
        !body.has_pre_warp_civilization;
    return natural ? SpeciesColonizationViability::NaturallyViable :
        fallback ? SpeciesColonizationViability::HabitatSupportedFallback : SpeciesColonizationViability::Unsuitable;
}

int mitigation_count(const SpeciesEnvironmentAssessment& environment) {
    return static_cast<int>(environment.requires_gravity_mitigation) + static_cast<int>(environment.requires_thermal_control) +
        static_cast<int>(environment.requires_pressure_control) + static_cast<int>(environment.requires_sealed_habitat) +
        static_cast<int>(environment.requires_artificial_biosphere) + static_cast<int>(environment.requires_radiation_shielding);
}
} // namespace

std::span<const SpeciesBiologyProfile> species_biology_profiles() { return profiles(); }
const SpeciesBiologyProfile& species_biology_profile(std::string_view id) {
    const auto found = std::find_if(profiles().begin(), profiles().end(), [&](const auto& value) { return value.id == id; });
    if (found == profiles().end()) throw std::out_of_range("Unknown species ID");
    return *found;
}

SpeciesDemographicPressure species_demographic_pressure(std::string_view id) {
    const auto& species = species_biology_profile(id);
    const auto& reference = species_biology_profile(terran);
    const double generation = reference.baseline_generation_years / species.baseline_generation_years;
    const double throughput = (species.typical_offspring_per_event / species.minimum_inter_event_years) /
        (reference.typical_offspring_per_event / reference.minimum_inter_event_years);
    const double maturity = reference.reproductive_maturity_years / species.reproductive_maturity_years;
    const double pace = std::clamp(std::cbrt(generation * throughput * maturity), .20, 1.80);
    return {species.id, generation, throughput, maturity, pace};
}

SpeciesMetabolicEnvelope species_metabolic_envelope(std::string_view id, double population_millions) {
    if (!std::isfinite(population_millions) || population_millions <= 0.0)
        throw std::invalid_argument("Population must be finite and positive");
    const auto& species = species_biology_profile(id);
    const double baseline = population_millions * species.baseline_metabolic_demand;
    const double resting = baseline * species.resting_metabolic_fraction;
    const double peak = baseline * species.peak_activity_metabolic_multiplier;
    const double typical = resting * species.typical_rest_fraction_of_day + baseline * (1.0 - species.typical_rest_fraction_of_day);
    const bool dormant = species.dormancy_mode != NaturalDormancyMode::None;
    return {species.id, population_millions, baseline, resting, peak, typical, species.dormancy_mode,
        dormant ? baseline * species.dormancy_metabolic_demand_fraction : baseline,
        species.maximum_dormancy_days, species.typical_dormancy_recovery_days, dormant};
}

SpeciesPlanetaryColonizationAssessment species_colonization_assessment(std::string_view id, const PlanetaryBody& body) {
    const auto& species = species_environment_profile(std::string(id));
    SpeciesEnvironmentAssessment environment;
    const auto viability = viability_for(species, body, environment);
    return {body.id, species.id, environment, viability, body.environment.has_solid_surface,
        body.has_pre_warp_civilization, !body.has_pre_warp_civilization && viability != SpeciesColonizationViability::Unsuitable};
}

ColonyHabitatSupportBurden colony_habitat_support(const Colony& colony, std::span<const PlanetaryBody> bodies) {
    validate_population_species(colony);
    const auto& species = species_biology_profile(colony.population_species_id);
    const auto metabolic = species_metabolic_envelope(species.id, colony.population_millions);
    ColonyHabitatSupportBurden result{colony.id, colony.civilization_id, colony.system_id, species.id, colony.population_millions,
        metabolic.typical_day_average_demand_millions, colony.population_millions * species.typical_adult_mass_kg};
    if (const PlanetaryBody* body = exact_body(colony, bodies)) {
        const auto assessment = species_colonization_assessment(species.id, *body);
        const auto& e = assessment.environment;
        result.environment = ColonyEnvironmentalSupportRequirements{body->id, assessment.viability, e.natural_habitability,
            e.unprotected_operational_capacity, e.limiting_factor, mitigation_count(e), e.requires_gravity_mitigation,
            e.requires_thermal_control, e.requires_pressure_control, e.requires_sealed_habitat,
            e.requires_artificial_biosphere, e.requires_radiation_shielding};
        result.environment->requires_any_environmental_mitigation = result.environment->required_mitigation_categories > 0;
        result.environment->uses_prototype_habitat_supported_fallback =
            assessment.viability == SpeciesColonizationViability::HabitatSupportedFallback;
        result.uses_exact_occupied_body = true;
        result.requires_environmental_support = result.environment->required_mitigation_categories > 0;
        result.gravity_mitigation_population_millions = e.requires_gravity_mitigation ? colony.population_millions : 0.0;
        result.thermal_control_population_millions = e.requires_thermal_control ? colony.population_millions : 0.0;
        result.pressure_control_population_millions = e.requires_pressure_control ? colony.population_millions : 0.0;
        result.sealed_habitat_population_millions = e.requires_sealed_habitat ? colony.population_millions : 0.0;
        result.artificial_biosphere_population_millions = e.requires_artificial_biosphere ? colony.population_millions : 0.0;
        result.radiation_shielding_population_millions = e.requires_radiation_shielding ? colony.population_millions : 0.0;
    }
    validate_habitat_ids(colony);
    validate_burden_result(result);
    return result;
}

ColonyPopulationTurnoverPressure colony_population_turnover(const Colony& colony, std::span<const PlanetaryBody> bodies) {
    validate_population_species(colony);
    const auto intrinsic = species_demographic_pressure(colony.population_species_id);
    ColonyPopulationTurnoverPressure result{colony.id, intrinsic.species_id, intrinsic.intrinsic_growth_pace_factor};
    if (const PlanetaryBody* body = exact_body(colony, bodies)) {
        const auto assessment = species_colonization_assessment(intrinsic.species_id, *body);
        result.uses_exact_occupied_body = true;
        result.planetary_body_id = body->id;
        result.colonization_viability = assessment.viability;
        result.natural_habitability = assessment.environment.natural_habitability;
        result.limiting_factor = assessment.environment.limiting_factor;
        result.requires_environmental_support = mitigation_count(assessment.environment) > 0;
        result.natural_environment_turnover_factor = assessment.viability == SpeciesColonizationViability::NaturallyViable ?
            std::sqrt(std::clamp(assessment.environment.natural_habitability, 0.0, 1.0)) : 1.0;
        result.environmental_pressure_applied = assessment.viability == SpeciesColonizationViability::NaturallyViable &&
            result.natural_environment_turnover_factor < .999999999;
    } else {
        result.natural_habitability = 1.0;
        result.natural_environment_turnover_factor = 1.0;
        result.limiting_factor = EnvironmentalLimitingFactor::None;
    }
    result.effective_growth_pace_factor = result.intrinsic_growth_pace_factor * result.natural_environment_turnover_factor;
    validate_turnover_id(colony);
    return result;
}
} // namespace stellar::core
