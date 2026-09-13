#pragma once
#include <stellar/core/planetary_catalog.hpp>
#include <span>
#include <string>
#include <vector>

namespace stellar::core {
enum class BiochemicalBasis { CarbonWater, CarbonAmmonia, CarbonHydrocarbon, SiliconChemistry, Synthetic };
enum class SpeciesAtmosphere { OxygenNitrogen, OxygenRich, CarbonDioxideRich, Reducing, Inert, Vacuum, Other };
enum class SpeciesSolvent { Water, Ammonia, Hydrocarbon, Other, None };
enum class EnvironmentalLimitingFactor { None, Gravity, Temperature, Pressure, Atmosphere, Solvent, Immersion, Radiation };
enum class SettlementSuitability { Incompatible, Marginal, NaturallyViable, Comfortable };
struct ToleranceBand { double preferred{},comfortable_deviation{},survivable_deviation{}; };
// Environmental projection of the preserved species definition, not a replacement for its demographics/morphology.
struct SpeciesEnvironmentProfile {
    std::string id,display_name;
    BiochemicalBasis biochemistry{};
    ToleranceBand gravity_g,temperature_kelvin,pressure_kpa;
    SpeciesAtmosphere preferred_atmosphere{};
    SpeciesSolvent biological_solvent{};
    bool requires_immersion{},can_operate_in_vacuum_unprotected{};
    double radiation_tolerance{};
    std::vector<SpeciesAtmosphere> breathable_atmospheres;
    std::vector<SpeciesSolvent> compatible_solvents;
};
struct HabitatEnvironment {
    double gravity_g{},temperature_kelvin{},pressure_kpa{};
    SpeciesAtmosphere atmosphere{};
    SpeciesSolvent available_solvent{};
    double radiation_hazard{};
    bool is_immersed{};
};
struct PopulationAdaptation {
    std::string species_id;
    double gravity_preference_shift_g{},gravity_tolerance_bonus_g{};
    double temperature_preference_shift_kelvin{},temperature_tolerance_bonus_kelvin{};
    double pressure_preference_shift_kpa{},pressure_tolerance_bonus_kpa{};
    double radiation_tolerance_bonus{},acclimatization{};
};
struct SpeciesEnvironmentAssessment {
    double natural_habitability{},unprotected_operational_capacity{};
    double gravity_suitability{},temperature_suitability{},pressure_suitability{},atmosphere_suitability{},
        solvent_suitability{},immersion_suitability{},radiation_suitability{};
    EnvironmentalLimitingFactor limiting_factor{};
    bool requires_gravity_mitigation{},requires_thermal_control{},requires_pressure_control{},
        requires_sealed_habitat{},requires_artificial_biosphere{},requires_radiation_shielding{};
};
struct PlanetSpeciesAssessment {
    int planetary_body_id{};
    std::string species_id;
    SpeciesEnvironmentAssessment environment;
    SettlementSuitability suitability{};
    bool has_physical_settlement_site{},naturally_colonizable{};
};
std::span<const SpeciesEnvironmentProfile> species_environment_profiles();
const SpeciesEnvironmentProfile& species_environment_profile(const std::string& id);
double evaluate_tolerance_band(const ToleranceBand& band,double value);
HabitatEnvironment to_species_habitat(const PlanetaryEnvironment& environment);
SpeciesEnvironmentAssessment evaluate_species_environment(const SpeciesEnvironmentProfile& species,
    const HabitatEnvironment& habitat,const PopulationAdaptation* adaptation=nullptr);
PlanetSpeciesAssessment assess_species_planet(const SpeciesEnvironmentProfile& species,
    const PlanetaryBody& body,const PopulationAdaptation* adaptation=nullptr);
std::string assign_species(std::int64_t campaign_seed,int civilization_id,bool canonical_start);
struct SpeciesHomeworldAssignment {
    int civilization_id{};
    std::string species_id;
    int system_id{},planetary_body_id{};
    double natural_habitability{};
    SettlementSuitability suitability{};
};
// Normal natural-home planner only. Nearby-expansion fallback remains an explicit subsequent migration gate.
std::vector<SpeciesHomeworldAssignment> plan_species_homeworlds(std::span<const StellarSystem> systems,
    std::span<const PlanetaryBody> bodies,std::span<const std::string> species_ids);
}
