#include <stellar/core/planetary_catalog.hpp>

#include <cmath>
#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace stellar::core;

namespace {
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error{std::string{message}}; }
template <typename F> void expect_throw(F&& call, std::string_view message) {
    try { call(); } catch (const std::exception&) { return; }
    throw std::runtime_error{std::string{message}};
}

StellarSystem sol() { return {sol_system_id, "Sol", {}, {}, {}, {}, std::string{sol_catalog_preset_id}, {}}; }

void canonical_catalog_has_physical_invariants() {
    const auto bodies = create_sol_catalog(sol());
    check(bodies.size() == 10, "Sol needs eight planets, Moon and Pluto");
    constexpr std::string_view names[] = {"Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
    for (int index = 0; index != 8; ++index) {
        const auto& body = bodies[static_cast<std::size_t>(index)];
        check(body.id == index + 1 && body.orbit_index == index && body.name == names[index] && body.kind == PlanetaryBodyKind::Planet,
            "planet identity or orbit changed");
        validate_planetary_body(body);
    }
    const auto& earth = bodies[2];
    check(earth.legacy_colonization_candidate && earth.environment.atmosphere == PlanetaryAtmosphereRegime::OxygenNitrogen &&
        earth.environment.available_solvent == PlanetarySolventRegime::Water && earth.environment.radiation_hazard == 0.06 &&
        earth.orbital_eccentricity == 0.0 && earth.orbital_inclination_degrees == 0.0,
        "Earth environment changed");
    for (std::size_t index = 4; index != 8; ++index)
        check(!bodies[index].environment.has_solid_surface && !bodies[index].legacy_colonization_candidate, "giant became terrestrial");
    const auto& moon = bodies[8];
    check(moon.id == moon_body_id && moon.parent_body_id == earth_body_id && moon.kind == PlanetaryBodyKind::Moon && moon.name == "Moon",
        "Moon identity changed");
    check(moon.orbital_eccentricity == 0.0 && moon.orbital_inclination_degrees == 0.0, "Moon orbital defaults changed");
    const auto& pluto = bodies[9];
    check(pluto.id == pluto_body_id && !pluto.parent_body_id && pluto.kind == PlanetaryBodyKind::DwarfPlanet && pluto.orbit_index == 8 &&
        std::abs(pluto.orbital_eccentricity - pluto_orbital_eccentricity) < 1e-12 &&
        std::abs(pluto.orbital_inclination_degrees - pluto_orbital_inclination_degrees) < 1e-12, "Pluto orbit changed");
    expect_throw([] { create_sol_catalog(StellarSystem{}); }, "non-Sol catalog was accepted");
    auto wrong_id = sol(); wrong_id.id = 1;
    expect_throw([&] { create_sol_catalog(wrong_id); }, "wrong reserved Sol id was accepted");
}

void validation_rejects_invalid_physical_values() {
    auto body = create_sol_catalog(sol()).front();
    body.radius_earth = 0; expect_throw([&] { validate_planetary_body(body); }, "zero radius accepted");
    body = create_sol_catalog(sol()).front(); body.orbital_eccentricity = 1; expect_throw([&] { validate_planetary_body(body); }, "eccentricity one accepted");
    body = create_sol_catalog(sol()).front(); body.orbital_inclination_degrees = 181; expect_throw([&] { validate_planetary_body(body); }, "inclination above 180 accepted");
    body = create_sol_catalog(sol()).front(); body.environment.temperature_kelvin = std::numeric_limits<double>::quiet_NaN();
    expect_throw([&] { validate_planetary_body(body); }, "NaN temperature accepted");
    body = create_sol_catalog(sol()).front(); body.environment.radiation_hazard = 1.01;
    expect_throw([&] { validate_planetary_body(body); }, "radiation above one accepted");
    body = create_sol_catalog(sol()).front(); body.kind = PlanetaryBodyKind::Moon;
    expect_throw([&] { validate_planetary_body(body); }, "parentless moon accepted");
}

void upgrade_behavior() {
    const auto system = sol();
    const auto complete = create_sol_catalog(system);
    std::vector<PlanetaryBody> legacy{complete.begin(), complete.end() - 1};
    legacy[2].mass_earth = 42.5; // Saved records must be retained verbatim, not reconstructed.
    const auto upgraded = upgrade_saved_sol_catalog(legacy, std::span{&system, 1});
    check(upgraded.size() == 10 && std::equal(legacy.begin(), legacy.end(), upgraded.begin(), [](const auto& a, const auto& b) {
        return a.id == b.id && a.system_id == b.system_id && a.name == b.name && a.kind == b.kind && a.mass_earth == b.mass_earth;
    }),
        "upgrade altered legacy records or their order");
    const auto idempotent = upgrade_saved_sol_catalog(upgraded, std::span{&system, 1});
    check(idempotent.size() == upgraded.size() && idempotent.back().id == pluto_body_id, "upgrade was not idempotent");
    auto malformed = legacy; malformed.erase(malformed.begin());
    expect_throw([&] { upgrade_saved_sol_catalog(malformed, std::span{&system, 1}); }, "missing legacy Sol body accepted");
    auto conflicting = legacy; conflicting.push_back(PlanetaryBody{pluto_body_id, 99, {}, 0, "Elsewhere", PlanetaryBodyKind::Planet,
        1, 1, {1, 1, 0, PlanetaryAtmosphereRegime::Vacuum, PlanetarySolventRegime::None, 0, false, true}});
    expect_throw([&] { upgrade_saved_sol_catalog(conflicting, std::span{&system, 1}); }, "reserved Pluto collision accepted");
    auto invalid_system = system; invalid_system.id = 1;
    expect_throw([&] { upgrade_saved_sol_catalog(legacy, std::span{&invalid_system, 1}); }, "invalid Sol system id accepted");
    StellarSystem ordinary{7, "Sol", {}, {}, {}, {}, {}, {}};
    const auto untouched = upgrade_saved_sol_catalog(legacy, std::span{&ordinary, 1});
    check(untouched.size() == legacy.size() && untouched[0].name == legacy[0].name, "display name incorrectly triggered upgrade");
}
}

int main() {
    try {
        canonical_catalog_has_physical_invariants();
        validation_rejects_invalid_physical_values();
        upgrade_behavior();
        std::cout << "sol_catalog_tests: passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "sol_catalog_tests failed: " << error.what() << '\n'; }
    return 1;
}
