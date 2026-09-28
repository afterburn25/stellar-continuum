#include <stellar/core/planetary_satellites.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace stellar::core {
namespace {
constexpr double rad=std::numbers::pi/180.;
// JPL SSD mean satellite elements / physical parameters, reviewed 2026-09-20.
// Phases define a reproducible campaign epoch; no observing-date accuracy implied.
constexpr SolMoonDefinition moons[]{
 {9,3,0,"Moon","moon",1737.4,4902.8,384400,27.322,.0554,5.16,125.08,318.15,135.27,250},
 {11,5,0,"Io","io",1821.49,5959.91547,421800,1.762732,.004,0,0,49.1,330.9,110},
 {12,5,1,"Europa","europa",1560.8,3202.7121,671100,3.525463,.009,.5,184,45,345.4,102},
 {13,5,2,"Ganymede","ganymede",2631.2,9887.83275,1070400,7.155588,.001,.2,58.5,198.3,324.8,110},
 {14,5,3,"Callisto","callisto",2410.3,7179.2834,1882700,16.690440,.007,.3,309.1,43.8,87.4,134},
 {15,6,0,"Mimas","mimas",198.2,2.50349,186000,.942422,.020,1.6,66.2,160.4,275.3,64},
 {16,6,1,"Enceladus","enceladus",252.1,7.21037,238400,1.370218,.005,0,0,119.5,57,75},
 {17,6,2,"Tethys","tethys",531.1,41.21353,295000,1.887802,.001,1.1,273,335.3,0,86},
 {18,6,3,"Dione","dione",561.4,73.11607,377700,2.736916,.002,0,0,116,212,87},
 {19,6,4,"Rhea","rhea",763.5,153.94175,527200,4.517503,.001,.3,133.7,44.3,31.5,76},
 {20,6,5,"Titan","titan",2574.76,8978.1371,1221900,15.945448,.029,.3,78.6,78.3,11.7,94},
 {21,6,6,"Iapetus","iapetus",734.3,120.51511,3561700,79.331002,.028,7.6,86.5,254.5,74.8,110},
 {22,7,0,"Miranda","miranda",235.8,4.3,129846,1.413479,.001,4.4,100.9,154.8,73,60},
 {23,7,1,"Ariel","ariel",578.9,83.5,190929,2.520379,.001,0,0,9.6,193.5,60},
 {24,7,2,"Umbriel","umbriel",584.7,85.1,265986,4.144177,.004,.1,174.8,183.4,253,60},
 {25,7,3,"Titania","titania",788.9,226.9,436298,8.705869,.002,.1,29.5,184,68.1,70},
 {26,7,4,"Oberon","oberon",761.4,205.3,583511,13.463237,.002,.1,76.8,132.2,143.6,70},
 {27,8,0,"Triton","triton",1352.6,1428.49546,354800,5.876994,0,157.3,178.1,0,63,38},
 {28,10,0,"Charon","charon",606,106.1,19600,6.387222,0,0,0,0,304.1,53}
};
}
std::span<const SolMoonDefinition> sol_moon_definitions(){return moons;}
const SolMoonDefinition* sol_moon_definition(int id){for(const auto& m:moons)if(m.id==id)return &m;return nullptr;}
std::vector<PlanetaryBody> create_sol_major_moons(){
 std::vector<PlanetaryBody> out;
 for(const auto& m:moons){if(m.id==moon_body_id)continue;PlanetaryBody b;
  b.id=m.id;b.system_id=sol_system_id;b.parent_body_id=m.parent;b.orbit_index=m.order;b.name=m.name;b.kind=PlanetaryBodyKind::Moon;
  b.radius_earth=m.radius_km/6371.;b.mass_earth=m.gm/398600.436;
  b.environment={m.gm/(m.radius_km*m.radius_km)*1000/9.80665,m.temperature,0,PlanetaryAtmosphereRegime::Vacuum,PlanetarySolventRegime::None,.22,false,true};
  if(m.id==20){b.environment.pressure_kpa=146.7;b.environment.atmosphere=PlanetaryAtmosphereRegime::Reducing;b.environment.available_solvent=PlanetarySolventRegime::Hydrocarbon;}
  if(m.id==27){b.environment.pressure_kpa=.0014;b.environment.atmosphere=PlanetaryAtmosphereRegime::Inert;}
  b.orbital_eccentricity=m.eccentricity;b.orbital_inclination_degrees=m.inclination;validate_planetary_body(b);out.push_back(std::move(b));
 }return out;
}
SatelliteOrbit planetary_satellite_orbit(const PlanetaryBody& parent,const PlanetaryBody& moon){
 if(moon.kind!=PlanetaryBodyKind::Moon||moon.parent_body_id!=parent.id||parent.system_id!=moon.system_id||parent.kind==PlanetaryBodyKind::Moon)
  throw std::invalid_argument("Satellite requires its primary parent in the same system");
 validate_planetary_body(parent);validate_planetary_body(moon);
 SatelliteOrbit result;result.parent_mass_fraction=moon.mass_earth/(parent.mass_earth+moon.mass_earth);
 if(moon.system_id==sol_system_id){if(const auto* m=sol_moon_definition(moon.id)){
  if(parent.id!=m->parent||moon.name!=m->name)throw std::invalid_argument("Reserved Sol satellite identity mismatch");
  result.relative={m->semimajor_km,m->eccentricity,m->inclination*rad,m->node*rad,m->periapsis*rad,m->phase*rad,2*std::numbers::pi/m->period_days};
  const double tilt=parent.id==5?3.13:parent.id==6?(m->id==21?14.8:26.73):parent.id==7?97.77:parent.id==8?28.32:parent.id==10?122.53:0;
  result.frame_inclination=tilt*rad;result.binary=parent.id==pluto_body_id;return result;
 }}
 const double radius=parent.radius_earth*6371.*(6.+moon.orbit_index*5.);
 const double rate=std::sqrt(398600.436*(parent.mass_earth+moon.mass_earth)/(radius*radius*radius))*86400.;
 const double phase=std::fmod(static_cast<double>(moon.id)*2.399963229728653,2*std::numbers::pi);
 result.relative={radius,std::min(.9,moon.orbital_eccentricity),moon.orbital_inclination_degrees*rad,0,0,phase,rate};
 if(parent.appearance){result.frame_inclination=parent.appearance->axial_tilt_radians;result.frame_node=parent.appearance->axis_node_radians;}
 return result;
}
std::array<double,3> satellite_relative_position(const SatelliteOrbit& o,double days){
 return stellar::engine::framed_orbit_position(o.relative,o.frame_inclination,o.frame_node,days);
}
}
