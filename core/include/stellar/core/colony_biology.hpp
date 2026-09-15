#pragma once
#include <stellar/core/colony_economy.hpp>
#include <string_view>

namespace stellar::core {
// Authored biological inputs used by the current scalar-colony bridge.
// This is not the full SpeciesDefinition or a mutable adaptation/cohort simulation.
enum class NaturalDormancyMode { None, Torpor, DeepDormancy, Cryptobiosis, SyntheticStandby };
struct SpeciesBiologyProfile {
    std::string id;
    double typical_adult_mass_kg{}, baseline_lifespan_years{}, baseline_metabolic_demand{};
    double reproductive_maturity_years{}, typical_offspring_per_event{}, minimum_inter_event_years{};
    double dependent_development_years{}, reproductive_span_years{}, baseline_generation_years{};
    double resting_metabolic_fraction{}, peak_activity_metabolic_multiplier{}, typical_rest_fraction_of_day{};
    NaturalDormancyMode dormancy_mode{};
    double dormancy_metabolic_demand_fraction{}, maximum_dormancy_days{}, typical_dormancy_recovery_days{};
};
std::span<const SpeciesBiologyProfile> species_biology_profiles();
const SpeciesBiologyProfile& species_biology_profile(std::string_view id);
struct SpeciesDemographicPressure {
    std::string species_id;
    double generation_pace_factor{}, reproductive_event_throughput_factor{}, maturity_pace_factor{}, intrinsic_growth_pace_factor{};
};
SpeciesDemographicPressure species_demographic_pressure(std::string_view id);
struct SpeciesMetabolicEnvelope {
    std::string species_id;
    double population_millions{}, baseline_demand_millions{}, resting_demand_millions{}, peak_activity_demand_millions{}, typical_day_average_demand_millions{};
    NaturalDormancyMode natural_dormancy_mode{};
    double dormant_demand_millions{}, maximum_natural_dormancy_days{}, typical_dormancy_recovery_days{};
    bool has_natural_dormancy{};
};
SpeciesMetabolicEnvelope species_metabolic_envelope(std::string_view id, double population_millions);
enum class SpeciesColonizationViability { Unsuitable, HabitatSupportedFallback, NaturallyViable };
struct SpeciesPlanetaryColonizationAssessment {
    int planetary_body_id{};
    std::string species_id;
    SpeciesEnvironmentAssessment environment;
    SpeciesColonizationViability viability{};
    bool has_solid_surface{}, has_native_pre_warp_civilization{}, can_found_current_colony{};
};
SpeciesPlanetaryColonizationAssessment species_colonization_assessment(std::string_view id, const PlanetaryBody& body);
struct ColonyEnvironmentalSupportRequirements {
    int planetary_body_id{};
    SpeciesColonizationViability colonization_viability{};
    double natural_habitability{}, unprotected_operational_capacity{};
    EnvironmentalLimitingFactor limiting_factor{};
    int required_mitigation_categories{};
    bool requires_gravity_mitigation{}, requires_thermal_control{}, requires_pressure_control{};
    bool requires_sealed_habitat{}, requires_artificial_biosphere{}, requires_radiation_shielding{};
    bool requires_any_environmental_mitigation{}, uses_prototype_habitat_supported_fallback{};
};
struct ColonyHabitatSupportBurden {
    int colony_id{}, civilization_id{}, system_id{};
    std::string species_id;
    double population_millions{}, typical_day_metabolic_demand_millions{}, adult_biomass_million_kg{};
    std::optional<ColonyEnvironmentalSupportRequirements> environment;
    bool uses_exact_occupied_body{}, requires_environmental_support{};
    double gravity_mitigation_population_millions{}, thermal_control_population_millions{}, pressure_control_population_millions{};
    double sealed_habitat_population_millions{}, artificial_biosphere_population_millions{}, radiation_shielding_population_millions{};
};
struct ColonyPopulationTurnoverPressure {
    int colony_id{};
    std::string species_id;
    double intrinsic_growth_pace_factor{};
    bool uses_exact_occupied_body{};
    std::optional<int> planetary_body_id;
    std::optional<SpeciesColonizationViability> colonization_viability;
    double natural_habitability{}, natural_environment_turnover_factor{}, effective_growth_pace_factor{};
    EnvironmentalLimitingFactor limiting_factor{};
    bool requires_environmental_support{}, environmental_pressure_applied{};
};
ColonyHabitatSupportBurden colony_habitat_support(const Colony& colony, std::span<const PlanetaryBody> bodies);
ColonyPopulationTurnoverPressure colony_population_turnover(const Colony& colony, std::span<const PlanetaryBody> bodies);
} // namespace stellar::core
