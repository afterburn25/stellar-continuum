#include <stellar/core/species_environment.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace stellar::core {
namespace {
constexpr std::string_view kTerran = "terran_baseline";
constexpr std::string_view kPelagic = "pelagic_high_pressure";
constexpr std::string_view kCompact = "compact_high_gravity";
constexpr std::string_view kCryogenic = "cryogenic_hydrocarbon";

const std::vector<SpeciesEnvironmentProfile>& profiles() {
    static const std::vector<SpeciesEnvironmentProfile> values = {
        {std::string(kTerran), "Terran Baseline", BiochemicalBasis::CarbonWater,
            {1.00, .15, .55}, {288.0, 15.0, 55.0}, {101.3, 35.0, 85.0},
            SpeciesAtmosphere::OxygenNitrogen, SpeciesSolvent::Water, false, false, .10,
            {SpeciesAtmosphere::OxygenNitrogen, SpeciesAtmosphere::OxygenRich}, {SpeciesSolvent::Water}},
        {std::string(kPelagic), "Pelagic High-Pressure", BiochemicalBasis::CarbonWater,
            {.85, .22, .62}, {282.0, 12.0, 42.0}, {350.0, 125.0, 300.0},
            SpeciesAtmosphere::OxygenNitrogen, SpeciesSolvent::Water, true, false, .18,
            {SpeciesAtmosphere::OxygenNitrogen, SpeciesAtmosphere::OxygenRich}, {SpeciesSolvent::Water}},
        {std::string(kCompact), "Compact High-Gravity", BiochemicalBasis::CarbonWater,
            {1.75, .30, .90}, {300.0, 16.0, 52.0}, {160.0, 60.0, 140.0},
            SpeciesAtmosphere::OxygenRich, SpeciesSolvent::Water, false, false, .26,
            {SpeciesAtmosphere::OxygenRich, SpeciesAtmosphere::OxygenNitrogen}, {SpeciesSolvent::Water}},
        {std::string(kCryogenic), "Cryogenic Hydrocarbon", BiochemicalBasis::CarbonHydrocarbon,
            {.14, .08, .25}, {94.0, 12.0, 35.0}, {150.0, 65.0, 135.0},
            SpeciesAtmosphere::Reducing, SpeciesSolvent::Hydrocarbon, false, false, .38,
            {SpeciesAtmosphere::Reducing}, {SpeciesSolvent::Hydrocarbon}},
    };
    return values;
}

[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
bool blank(const std::string& value) {
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char value) { return std::isspace(value) != 0; });
}

void validate_band(const ToleranceBand& band) {
    if (!std::isfinite(band.preferred)) invalid("Tolerance preferred value must be finite");
    if (!std::isfinite(band.comfortable_deviation) || band.comfortable_deviation < 0.0)
        invalid("Tolerance comfortable deviation must be finite and non-negative");
    if (!std::isfinite(band.survivable_deviation) || band.survivable_deviation <= band.comfortable_deviation)
        invalid("Tolerance survivable deviation must be finite and greater than comfortable deviation");
}

void validate_species(const SpeciesEnvironmentProfile& species) {
    if (blank(species.id) || blank(species.display_name)) invalid("Species environment profile requires an identity and display name");
    validate_band(species.gravity_g); validate_band(species.temperature_kelvin); validate_band(species.pressure_kpa);
    if (species.gravity_g.preferred < 0.0 || species.temperature_kelvin.preferred <= 0.0 || species.pressure_kpa.preferred < 0.0)
        invalid("Species environmental preferences are invalid");
    if (!std::isfinite(species.radiation_tolerance) || species.radiation_tolerance < 0.0 || species.radiation_tolerance > 1.0)
        invalid("Species radiation tolerance must be between 0 and 1");
    if (species.biochemistry != BiochemicalBasis::Synthetic &&
        (species.breathable_atmospheres.empty() || species.compatible_solvents.empty()))
        invalid("Biological species require breathable atmospheres and compatible solvents");
}

void validate_habitat(const HabitatEnvironment& habitat) {
    if (!std::isfinite(habitat.gravity_g) || habitat.gravity_g < 0.0 || !std::isfinite(habitat.temperature_kelvin) ||
        habitat.temperature_kelvin <= 0.0 || !std::isfinite(habitat.pressure_kpa) || habitat.pressure_kpa < 0.0 ||
        !std::isfinite(habitat.radiation_hazard) || habitat.radiation_hazard < 0.0 || habitat.radiation_hazard > 1.0)
        invalid("Habitat environment is invalid");
}
void validate_planetary_environment(const PlanetaryEnvironment& environment) {
    if (!std::isfinite(environment.gravity_g) || environment.gravity_g < 0.0 ||
        !std::isfinite(environment.temperature_kelvin) || environment.temperature_kelvin <= 0.0 ||
        !std::isfinite(environment.pressure_kpa) || environment.pressure_kpa < 0.0 ||
        !std::isfinite(environment.radiation_hazard) || environment.radiation_hazard < 0.0 || environment.radiation_hazard > 1.0)
        invalid("Planetary environment is invalid");
}

void validate_adaptation(const PopulationAdaptation& adaptation) {
    if (blank(adaptation.species_id)) invalid("Population adaptation must reference a species");
    const std::array<double, 3> shifts = {adaptation.gravity_preference_shift_g,
        adaptation.temperature_preference_shift_kelvin, adaptation.pressure_preference_shift_kpa};
    for (const double value : shifts) if (!std::isfinite(value)) invalid("Population adaptation preference shift must be finite");
    const std::array<double, 4> bonuses = {adaptation.gravity_tolerance_bonus_g,
        adaptation.temperature_tolerance_bonus_kelvin, adaptation.pressure_tolerance_bonus_kpa,
        adaptation.radiation_tolerance_bonus};
    for (const double value : bonuses) if (!std::isfinite(value) || value < 0.0) invalid("Population adaptation bonus must be finite and non-negative");
    if (!std::isfinite(adaptation.acclimatization) || adaptation.acclimatization < 0.0 || adaptation.acclimatization > 1.0)
        invalid("Acclimatization must be between 0 and 1");
}

ToleranceBand shift_and_widen(ToleranceBand band, double shift, double bonus) {
    return {band.preferred + shift, band.comfortable_deviation + bonus, band.survivable_deviation + bonus};
}
double acclimatize(double score, double acclimatization) {
    if (score <= 0.0 || score >= 1.0 || acclimatization <= 0.0) return score;
    return std::clamp(score + ((1.0 - score) * acclimatization * .20), 0.0, 1.0);
}
bool contains(const std::vector<SpeciesAtmosphere>& values, SpeciesAtmosphere value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
bool contains(const std::vector<SpeciesSolvent>& values, SpeciesSolvent value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
double atmosphere_score(const SpeciesEnvironmentProfile& species, const HabitatEnvironment& habitat) {
    if (species.biochemistry == BiochemicalBasis::Synthetic) return 1.0;
    if (habitat.atmosphere == SpeciesAtmosphere::Vacuum) return species.can_operate_in_vacuum_unprotected ? 1.0 : 0.0;
    return contains(species.breathable_atmospheres, habitat.atmosphere) ? 1.0 : 0.0;
}
double solvent_score(const SpeciesEnvironmentProfile& species, const HabitatEnvironment& habitat) {
    return species.biochemistry == BiochemicalBasis::Synthetic || contains(species.compatible_solvents, habitat.available_solvent) ? 1.0 : 0.0;
}
double radiation_score(const SpeciesEnvironmentProfile& species, const HabitatEnvironment& habitat, const PopulationAdaptation& adaptation) {
    const double tolerance = std::clamp(species.radiation_tolerance + adaptation.radiation_tolerance_bonus, 0.0, 1.0);
    if (habitat.radiation_hazard <= tolerance || tolerance >= 1.0) return 1.0;
    return acclimatize(std::clamp(1.0 - ((habitat.radiation_hazard - tolerance) / (1.0 - tolerance)), 0.0, 1.0),
        adaptation.acclimatization);
}

SpeciesAtmosphere map_atmosphere(PlanetaryAtmosphereRegime value) {
    switch (value) {
    case PlanetaryAtmosphereRegime::Vacuum: return SpeciesAtmosphere::Vacuum;
    case PlanetaryAtmosphereRegime::OxygenNitrogen: return SpeciesAtmosphere::OxygenNitrogen;
    case PlanetaryAtmosphereRegime::OxygenRich: return SpeciesAtmosphere::OxygenRich;
    case PlanetaryAtmosphereRegime::CarbonDioxideRich: return SpeciesAtmosphere::CarbonDioxideRich;
    case PlanetaryAtmosphereRegime::Reducing: return SpeciesAtmosphere::Reducing;
    case PlanetaryAtmosphereRegime::Inert: return SpeciesAtmosphere::Inert;
    case PlanetaryAtmosphereRegime::Other: return SpeciesAtmosphere::Other;
    }
    invalid("Planetary atmosphere regime is invalid");
}
SpeciesSolvent map_solvent(PlanetarySolventRegime value) {
    switch (value) {
    case PlanetarySolventRegime::None: return SpeciesSolvent::None;
    case PlanetarySolventRegime::Water: return SpeciesSolvent::Water;
    case PlanetarySolventRegime::Ammonia: return SpeciesSolvent::Ammonia;
    case PlanetarySolventRegime::Hydrocarbon: return SpeciesSolvent::Hydrocarbon;
    case PlanetarySolventRegime::Other: return SpeciesSolvent::Other;
    }
    invalid("Planetary solvent regime is invalid");
}
} // namespace

std::span<const SpeciesEnvironmentProfile> species_environment_profiles() { return profiles(); }
const SpeciesEnvironmentProfile& species_environment_profile(const std::string& id) {
    const auto found = std::find_if(profiles().begin(), profiles().end(), [&id](const auto& profile) { return profile.id == id; });
    if (found == profiles().end()) throw std::out_of_range("Unknown species ID");
    return *found;
}
double evaluate_tolerance_band(const ToleranceBand& band, double value) {
    const double deviation = std::abs(value - band.preferred);
    if (deviation <= band.comfortable_deviation) return 1.0;
    if (deviation >= band.survivable_deviation) return 0.0;
    return 1.0 - ((deviation - band.comfortable_deviation) / (band.survivable_deviation - band.comfortable_deviation));
}
HabitatEnvironment to_species_habitat(const PlanetaryEnvironment& environment) {
    validate_planetary_environment(environment);
    HabitatEnvironment habitat{environment.gravity_g, environment.temperature_kelvin, environment.pressure_kpa,
        map_atmosphere(environment.atmosphere), map_solvent(environment.available_solvent), environment.radiation_hazard,
        environment.is_immersed_environment};
    validate_habitat(habitat);
    return habitat;
}
SpeciesEnvironmentAssessment evaluate_species_environment(const SpeciesEnvironmentProfile& species,
    const HabitatEnvironment& habitat, const PopulationAdaptation* adaptation) {
    validate_species(species); validate_habitat(habitat);
    const PopulationAdaptation default_adaptation{species.id};
    const PopulationAdaptation& current = adaptation ? *adaptation : default_adaptation;
    validate_adaptation(current);
    if (species.id != current.species_id) invalid("Population adaptation cannot be applied to a different species");
    const double gravity = acclimatize(evaluate_tolerance_band(shift_and_widen(species.gravity_g,
        current.gravity_preference_shift_g, current.gravity_tolerance_bonus_g), habitat.gravity_g), current.acclimatization);
    const double temperature = acclimatize(evaluate_tolerance_band(shift_and_widen(species.temperature_kelvin,
        current.temperature_preference_shift_kelvin, current.temperature_tolerance_bonus_kelvin), habitat.temperature_kelvin), current.acclimatization);
    const double pressure = acclimatize(evaluate_tolerance_band(shift_and_widen(species.pressure_kpa,
        current.pressure_preference_shift_kpa, current.pressure_tolerance_bonus_kpa), habitat.pressure_kpa), current.acclimatization);
    const double atmosphere = atmosphere_score(species, habitat);
    const double solvent = solvent_score(species, habitat);
    const double immersion = !species.requires_immersion || habitat.is_immersed ? 1.0 : 0.0;
    const double radiation = radiation_score(species, habitat, current);
    const std::array<std::pair<EnvironmentalLimitingFactor, double>, 7> factors = {{{EnvironmentalLimitingFactor::Gravity, gravity},
        {EnvironmentalLimitingFactor::Temperature, temperature}, {EnvironmentalLimitingFactor::Pressure, pressure},
        {EnvironmentalLimitingFactor::Atmosphere, atmosphere}, {EnvironmentalLimitingFactor::Solvent, solvent},
        {EnvironmentalLimitingFactor::Immersion, immersion}, {EnvironmentalLimitingFactor::Radiation, radiation}}};
    EnvironmentalLimitingFactor limiting = EnvironmentalLimitingFactor::None;
    double natural = 1.0;
    for (const auto [factor, score] : factors) if (score < natural) { natural = score; limiting = factor; }
    double product = 1.0;
    for (const auto [factor, score] : factors) { (void)factor; product *= std::clamp(score, 0.0, 1.0); }
    const double capacity = std::pow(product, 1.0 / static_cast<double>(factors.size()));
    return {natural, capacity, gravity, temperature, pressure, atmosphere, solvent, immersion, radiation, limiting,
        gravity < .95, temperature < .95, pressure < .95, atmosphere < .95, solvent < .95 || immersion < .95, radiation < .95};
}
PlanetSpeciesAssessment assess_species_planet(const SpeciesEnvironmentProfile& species, const PlanetaryBody& body,
    const PopulationAdaptation* adaptation) {
    validate_species(species);
    validate_planetary_body(body);
    const SpeciesEnvironmentAssessment environment = evaluate_species_environment(species, to_species_habitat(body.environment), adaptation);
    const bool site = species.requires_immersion ? body.environment.is_immersed_environment : body.environment.has_solid_surface;
    const bool colonizable = site && environment.natural_habitability >= .20;
    const SettlementSuitability suitability = !site || environment.natural_habitability <= 0.0 ? SettlementSuitability::Incompatible :
        environment.natural_habitability < .20 ? SettlementSuitability::Marginal : environment.natural_habitability < .95 ?
            SettlementSuitability::NaturallyViable : SettlementSuitability::Comfortable;
    return {body.id, species.id, environment, suitability, site, colonizable};
}
} // namespace stellar::core
