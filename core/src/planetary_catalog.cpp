#include <stellar/core/planetary_catalog.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace stellar::core {
namespace {

[[noreturn]] void invalid(const char* message) { throw std::invalid_argument{message}; }

bool is_blank(const std::string& value) {
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool valid(PlanetaryBodyKind value) {
    return value >= PlanetaryBodyKind::Planet && value <= PlanetaryBodyKind::DwarfPlanet;
}

void validate_environment(const PlanetaryEnvironment& environment) {
    if (!std::isfinite(environment.gravity_g) || environment.gravity_g < 0.0)
        invalid("Planetary gravity must be finite and non-negative.");
    if (!std::isfinite(environment.temperature_kelvin) || environment.temperature_kelvin <= 0.0)
        invalid("Planetary temperature must be finite and above absolute zero.");
    if (!std::isfinite(environment.pressure_kpa) || environment.pressure_kpa < 0.0)
        invalid("Planetary pressure must be finite and non-negative.");
    if (!std::isfinite(environment.radiation_hazard) || environment.radiation_hazard < 0.0 || environment.radiation_hazard > 1.0)
        invalid("Planetary radiation hazard must be between 0 and 1.");
}

PlanetaryBody planet(int id, int orbit, const char* name, double radius, double mass, double gravity,
    double temperature, double pressure, PlanetaryAtmosphereRegime atmosphere, bool water = false, bool solid = true) {
    PlanetaryBody result{id, sol_system_id, std::nullopt, orbit, name, PlanetaryBodyKind::Planet, radius, mass,
        {gravity, temperature, pressure, atmosphere, water ? PlanetarySolventRegime::Water : PlanetarySolventRegime::None,
         water ? 0.06 : 0.22, false, solid}, id == earth_body_id, false, false, false, 0.0, 0.0};
    validate_planetary_body(result);
    return result;
}

PlanetaryBody pluto() {
    PlanetaryBody result{pluto_body_id, sol_system_id, std::nullopt, 8, "Pluto", PlanetaryBodyKind::DwarfPlanet,
        0.186, 0.00218, {0.063, 44, 0.001, PlanetaryAtmosphereRegime::Other, PlanetarySolventRegime::None,
        0.22, false, true}, false, false, false, false, pluto_orbital_eccentricity, pluto_orbital_inclination_degrees};
    validate_planetary_body(result);
    return result;
}

bool is_sol(const StellarSystem& system) {
    return system.catalog_preset_id && *system.catalog_preset_id == sol_catalog_preset_id;
}

} // namespace

void validate_planetary_body(const PlanetaryBody& body) {
    if (body.id < 0) invalid("Planetary body IDs must be non-negative.");
    if (body.system_id < 0) invalid("Planetary body system IDs must be non-negative.");
    if (body.orbit_index < 0) invalid("Planetary body orbit indices must be non-negative.");
    if (is_blank(body.name)) invalid("Planetary bodies require a name.");
    if (!valid(body.kind)) invalid("Planetary body kind is invalid.");
    if ((body.kind == PlanetaryBodyKind::Planet || body.kind == PlanetaryBodyKind::DwarfPlanet) && body.parent_body_id)
        invalid("Primary planetary bodies cannot have a parent body.");
    if (body.kind == PlanetaryBodyKind::Moon && !body.parent_body_id)
        invalid("Moons require a parent body.");
    if (!std::isfinite(body.radius_earth) || body.radius_earth <= 0.0)
        invalid("Planetary body radius must be finite and positive.");
    if (!std::isfinite(body.mass_earth) || body.mass_earth <= 0.0)
        invalid("Planetary body mass must be finite and positive.");
    if (!std::isfinite(body.orbital_eccentricity) || body.orbital_eccentricity < 0.0 || body.orbital_eccentricity >= 1.0)
        invalid("Planetary body orbital eccentricity must be finite and in [0, 1).");
    if (!std::isfinite(body.orbital_inclination_degrees) || body.orbital_inclination_degrees < 0.0 || body.orbital_inclination_degrees > 180.0)
        invalid("Planetary body orbital inclination must be finite and between 0 and 180 degrees.");
    validate_environment(body.environment);
}

std::vector<PlanetaryBody> create_sol_catalog(const StellarSystem& system) {
    if (!is_sol(system) || system.id != sol_system_id)
        throw std::invalid_argument{"The Sol v1 catalog requires its reserved system identity."};
    std::vector<PlanetaryBody> result{
        planet(1, 0, "Mercury", 0.383, 0.0553, 0.38, 440, 0, PlanetaryAtmosphereRegime::Vacuum),
        planet(2, 1, "Venus", 0.950, 0.815, 0.907, 735, 9200, PlanetaryAtmosphereRegime::CarbonDioxideRich),
        planet(3, 2, "Earth", 1.0, 1.0, 1.0, 288, 101.3, PlanetaryAtmosphereRegime::OxygenNitrogen, true),
        planet(4, 3, "Mars", 0.532, 0.107, 0.377, 210, 0.61, PlanetaryAtmosphereRegime::CarbonDioxideRich),
        planet(5, 4, "Jupiter", 10.86, 317.8, 2.364, 120, 100, PlanetaryAtmosphereRegime::Reducing, false, false),
        planet(6, 5, "Saturn", 9.0, 95.15, 0.916, 88, 100, PlanetaryAtmosphereRegime::Reducing, false, false),
        planet(7, 6, "Uranus", 3.97, 14.54, 0.886, 60, 100, PlanetaryAtmosphereRegime::Reducing, false, false),
        planet(8, 7, "Neptune", 3.86, 17.15, 1.14, 55, 100, PlanetaryAtmosphereRegime::Reducing, false, false),
        PlanetaryBody{moon_body_id, sol_system_id, earth_body_id, 0, "Moon", PlanetaryBodyKind::Moon, 0.273, 0.0123,
            {0.165, 250, 0, PlanetaryAtmosphereRegime::Vacuum, PlanetarySolventRegime::None, 0.22, false, true},
            false, false, false, false, 0.0, 0.0},
        pluto(),
    };
    for (const auto& body : result) validate_planetary_body(body);
    return result;
}

std::vector<PlanetaryBody> upgrade_saved_sol_catalog(std::span<const PlanetaryBody> bodies,
    std::span<const StellarSystem> systems) {
    const StellarSystem* sol = nullptr;
    for (const auto& system : systems) {
        if (!is_sol(system)) continue;
        if (sol != nullptr) throw std::invalid_argument{"Sequence contains more than one matching element."};
        sol = &system;
    }
    if (sol == nullptr) return {bodies.begin(), bodies.end()};
    if (sol->id != sol_system_id)
        throw std::invalid_argument{"The Sol v1 catalog has an invalid reserved system identity."};

    const auto existing_pluto = std::find_if(bodies.begin(), bodies.end(), [](const PlanetaryBody& body) { return body.id == pluto_body_id; });
    if (existing_pluto != bodies.end()) {
        if (existing_pluto->system_id != sol_system_id || existing_pluto->name != "Pluto" || existing_pluto->kind != PlanetaryBodyKind::DwarfPlanet)
            throw std::invalid_argument{"Reserved Pluto body ID 10 is occupied by a different body."};
        return {bodies.begin(), bodies.end()};
    }

    struct Identity { int id; const char* name; PlanetaryBodyKind kind; };
    constexpr Identity legacy[] = {{1, "Mercury", PlanetaryBodyKind::Planet}, {2, "Venus", PlanetaryBodyKind::Planet},
        {3, "Earth", PlanetaryBodyKind::Planet}, {4, "Mars", PlanetaryBodyKind::Planet}, {5, "Jupiter", PlanetaryBodyKind::Planet},
        {6, "Saturn", PlanetaryBodyKind::Planet}, {7, "Uranus", PlanetaryBodyKind::Planet}, {8, "Neptune", PlanetaryBodyKind::Planet},
        {moon_body_id, "Moon", PlanetaryBodyKind::Moon}};
    for (const auto& expected : legacy) {
        const bool found = std::any_of(bodies.begin(), bodies.end(), [&](const PlanetaryBody& body) {
            return body.id == expected.id && body.system_id == sol_system_id && body.name == expected.name && body.kind == expected.kind;
        });
        if (!found) throw std::invalid_argument{"The saved Sol v1 catalog is missing a legacy canonical body identity."};
    }
    std::vector<PlanetaryBody> result{bodies.begin(), bodies.end()};
    result.push_back(pluto());
    return result;
}

} // namespace stellar::core
