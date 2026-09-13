#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <stellar/core/galaxy_catalog.hpp>

namespace stellar::core {

enum class PlanetaryBodyKind { Planet, Moon, DwarfPlanet };
enum class PlanetaryAtmosphereRegime { Vacuum, OxygenNitrogen, OxygenRich, CarbonDioxideRich, Reducing, Inert, Other };
enum class PlanetarySolventRegime { None, Water, Ammonia, Hydrocarbon, Other };

struct PlanetaryEnvironment {
    double gravity_g{};
    double temperature_kelvin{};
    double pressure_kpa{};
    PlanetaryAtmosphereRegime atmosphere{};
    PlanetarySolventRegime available_solvent{};
    double radiation_hazard{};
    bool is_immersed_environment{};
    bool has_solid_surface{};
};

struct PlanetaryBody {
    int id{};
    int system_id{};
    std::optional<int> parent_body_id;
    int orbit_index{};
    std::string name;
    PlanetaryBodyKind kind{};
    double radius_earth{};
    double mass_earth{};
    PlanetaryEnvironment environment;
    bool legacy_colonization_candidate{};
    bool has_rare_resource{};
    bool has_anomaly{};
    bool has_pre_warp_civilization{};
    double orbital_eccentricity{};
    double orbital_inclination_degrees{};
};

inline constexpr std::string_view sol_catalog_preset_id = "sol-v1";
inline constexpr int sol_system_id = 0;
inline constexpr int earth_body_id = 3;
inline constexpr int moon_body_id = 9;
inline constexpr int pluto_body_id = 10;
inline constexpr double pluto_orbital_eccentricity = 0.2444;
inline constexpr double pluto_orbital_inclination_degrees = 17.16;

void validate_planetary_body(const PlanetaryBody& body);
std::vector<PlanetaryBody> generate_planetary_catalog(std::int64_t seed,std::span<const StellarSystem> systems);
std::vector<PlanetaryBody> apply_environmental_diversity(std::int64_t seed,std::span<const StellarSystem> systems,std::span<const PlanetaryBody> bodies);
std::vector<PlanetaryBody> create_sol_catalog(const StellarSystem& system);
std::vector<PlanetaryBody> upgrade_saved_sol_catalog(
    std::span<const PlanetaryBody> bodies, std::span<const StellarSystem> systems);

} // namespace stellar::core
