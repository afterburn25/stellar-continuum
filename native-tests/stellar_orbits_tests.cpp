#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/campaign_foundation_persistence.hpp>
#include "../core/src/stellar_object_json.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace stellar::core;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main()try{
 auto primary=generate_stellar_physics(17,StellarObjectType::GYellowStar);primary.mass_solar=1;primary.luminosity_solar=1;
 StellarSystem source;source.id=72;source.name="Triple";source.primary=StellarClass::GYellowDwarf;source.secondary=StellarClass::MRedDwarf;source.tertiary=StellarClass::KOrangeDwarf;source.stellar_object=primary;
 std::vector<PlanetaryBody> originals;for(int i=0;i<6;++i){PlanetaryBody b;b.id=100+i;b.system_id=72;b.orbit_index=i;b.name="Planet";b.orbital_eccentricity=.04;b.stellar_exposure=stellar_planet_exposure(primary,.8*std::pow(1.8,i));originals.push_back(b);}
 std::vector<StellarSystem> systems{source};auto planets=originals;initialize_stellar_orbits(8192,systems,planets);const auto& s=systems[0];const auto& a=*s.stellar_orbits;
 for(std::size_t i=0;i<planets.size();++i)check(planets[i].stellar_exposure==originals[i].stellar_exposure&&planetary_stellar_host(s,planets[i].id)==0,"Migration changed established planetary orbit or host");
 const double ma=primary.mass_solar,mb=a.companions[0].mass_solar,mc=a.companions[1].mass_solar;
 for(double day:{0.,12.,300.,36525.}){const auto p=stellar_positions(s,day);for(int k=0;k<3;++k){check(std::abs(ma*p[0][k]+mb*p[1][k]+mc*p[2][k])<1e-8,"Barycentre is not mass weighted");check(std::abs(ma*(p[0][k]-p[3][k])+mb*(p[1][k]-p[3][k]))<1e-8,"Inner mass ratio is wrong");}}
 const auto initial=stellar_positions(s,0),later=stellar_positions(s,36525);check(initial!=later,"Stars do not orbit");
 const double period=2*std::numbers::pi/a.relative_orbits[0].angular_speed;
 check(std::abs(period/365.25-std::sqrt(std::pow(a.relative_orbits[0].radius,3)/(ma+mb)))<1e-8,"Period fails Kepler's third law");
 const auto old=a;initialize_stellar_orbits(8192,systems,planets);check(*systems[0].stellar_orbits==old,"Initialization is not idempotent");
 const auto dto=capture_stellar_systems(systems);check(*restore_stellar_systems(dto)[0].stellar_orbits==old,"System DTO loses stellar dynamics");
 nlohmann::json json=old;const auto decoded=json.get<StellarOrbitArchitecture>();check(decoded==old,"JSON loses stellar dynamics");
 auto bad=systems;bad[0].stellar_orbits->relative_orbits[0].angular_speed*=2;bool rejected=false;try{validate_stellar_orbits(bad[0]);}catch(const std::exception&){rejected=true;}check(rejected,"Accepted period inconsistent with masses");
 auto fractional=json;fractional["planets"][0]["host"]=.5;rejected=false;try{(void)fractional.get<StellarOrbitArchitecture>();}catch(const std::exception&){rejected=true;}check(rejected,"Fractional stellar host was truncated");
 int pair_planets=0,secondary_planets=0,tertiary_planets=0;
 for(int seed=1;seed<=60;++seed){auto fresh=std::vector<StellarSystem>{source};auto local=originals;initialize_stellar_orbits(seed,fresh,local,true);const auto& v=fresh[0];const auto& binary=v.stellar_orbits->relative_orbits[0];
  const double m1=v.stellar_object->mass_solar,m2=v.stellar_orbits->companions[0].mass_solar,mu=m2/(m1+m2);
  for(const auto& b:local){const int host=planetary_stellar_host(v,b.id);const double r=b.stellar_exposure->orbit_au;
   if(host==3){++pair_planets;check(r*(1-b.orbital_eccentricity)>circumbinary_limit(binary.radius,binary.eccentricity,mu),"P-type planet entered unstable binary region");}
   else if(host<2){secondary_planets+=host==1;check(r*(1+b.orbital_eccentricity)<circumstellar_limit(binary.radius,binary.eccentricity,host==0?mu:1-mu),"S-type planet escaped its stable host region");}else ++tertiary_planets;
   check(stellar_host_accepts_orbit(v,host,r,b.orbital_eccentricity),"Planet violates hierarchy stability margins");
   check(std::abs(b.stellar_exposure->incident_flux-originals[b.id-100].stellar_exposure->incident_flux)<1e-8,"Host assignment changed planet equilibrium climate");
   check(stellar_planet_position(v,b,0)!=stellar_planet_position(v,b,3),"Planet does not orbit its host");
  }
 }
 check(pair_planets>0&&secondary_planets>0&&tertiary_planets>0,"Generation omitted individual-star or circumbinary planets");
 validate_stellar_orbit_catalog(systems,planets);
 auto orphan=systems;orphan[0].stellar_orbits->planets.push_back({999999,0});rejected=false;
 try{validate_stellar_orbit_catalog(orphan,planets);}catch(const std::exception&){rejected=true;}check(rejected,"Accepted missing stellar-bound body");
 planets[0].stellar_exposure=stellar_planet_exposure(primary,1e5);reconcile_stellar_orbit_clearance(systems[0],planets);
 check(stellar_host_accepts_orbit(systems[0],0,1e5,.04),"Expanded planet family escaped its host region");
 const auto expanded=systems[0].stellar_orbits;reconcile_stellar_orbit_clearance(systems[0],planets);check(expanded==systems[0].stellar_orbits,"Clearance repair is not idempotent");
 std::cout<<"Mass-weighted binary/triple Kepler orbits, stable S/P hosts, preserved migration and save round trip passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
