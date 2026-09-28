#pragma once
#include <stellar/core/stellar_object.hpp>
#include <stellar/engine/analytic_orbit.hpp>
#include <span>
namespace stellar::core {
// AU, solar masses and simulation days. Host IDs are stable: A=0, B=1,
// C=2, inner AB barycentre=3. Triples are hierarchical Jacobi orbits.
struct StellarOrbitBinding { int body_id{},host{}; bool operator==(const StellarOrbitBinding&) const = default; };
struct StellarOrbitArchitecture {
  int version{1};
  std::vector<StellarPhysicalProperties> companions;
  std::vector<stellar::engine::AnalyticOrbit> relative_orbits;
  std::vector<StellarOrbitBinding> planets;
  int belt_host{};
  bool operator==(const StellarOrbitArchitecture&) const = default;
};
struct StellarSystem;struct PlanetaryBody;
using StellarPosition=std::array<double,3>;
// Analytic restricted orbit model; no per-frame galaxy-wide integration.
double kepler_rate(double semimajor_au,double mass_solar);
double circumstellar_limit(double binary_au,double eccentricity,double companion_mass_fraction);
double circumbinary_limit(double binary_au,double eccentricity,double secondary_mass_fraction);
void validate_stellar_orbits(const StellarSystem&);
void validate_stellar_orbit_catalog(std::span<const StellarSystem>,std::span<const PlanetaryBody>);
void reconcile_stellar_orbit_clearance(StellarSystem&,std::span<const PlanetaryBody>);
bool stellar_host_accepts_orbit(const StellarSystem&,int host,double semimajor_au,double eccentricity);
void initialize_stellar_orbits(std::int64_t,std::vector<StellarSystem>&,std::vector<PlanetaryBody>&,bool fresh=false);
std::array<StellarPosition,4> stellar_positions(const StellarSystem&,double days);
int planetary_stellar_host(const StellarSystem&,int body_id);
StellarPhysicalProperties stellar_host_physics(const StellarSystem&,int host);
stellar::engine::AnalyticOrbit planetary_stellar_orbit(const StellarSystem&,const PlanetaryBody&);
StellarPosition stellar_planet_position(const StellarSystem&,const PlanetaryBody&,double days);
std::string stellar_host_name(int host);
}
