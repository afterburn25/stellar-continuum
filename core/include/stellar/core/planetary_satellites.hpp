#pragma once
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/engine/analytic_orbit.hpp>
#include <span>

namespace stellar::core {
// Stable Sol identities and mean orbital elements. Distances are kilometres,
// angles degrees and periods days. These are analytic mean orbits, not ephemerides.
struct SolMoonDefinition {
 int id,parent,order;std::string_view name,key;
 double radius_km,gm,semimajor_km,period_days,eccentricity,inclination,node,periapsis,phase,temperature;
};
std::span<const SolMoonDefinition> sol_moon_definitions();
const SolMoonDefinition* sol_moon_definition(int body_id);
std::vector<PlanetaryBody> create_sol_major_moons();
struct SatelliteOrbit {
 stellar::engine::AnalyticOrbit relative;
 double frame_inclination{},frame_node{},parent_mass_fraction{};
 bool binary{};
 bool operator==(const SatelliteOrbit&) const = default;
};
// Reconstructs immutable version-one elements from canonical identity or saved
// procedural body properties. Rendering, inspection and tests share this rule.
SatelliteOrbit planetary_satellite_orbit(const PlanetaryBody& parent,const PlanetaryBody& moon);
std::array<double,3> satellite_relative_position(const SatelliteOrbit&,double simulation_days);
}
