#pragma once
#include "native_system_view.hpp"
#include <stellar/engine/native_scene3d.hpp>
#include <stellar/engine/native_geometry3d.hpp>
namespace stellar::native_planets {
using namespace stellar::native_map;
struct Lighting{Vec3 direction{.42f,.2f,.87f},color{1,1,1};float intensity{1};std::array<DirectionalLight3D,2> additional{};
 std::optional<Quaternion> locked_rotation;
 // Physical blocker relative to the receiving body's centre, in body radii.
 std::optional<AnalyticShadow3D> eclipse;
};
inline Vec3 rotate_light(Quaternion q,Vec3 v){const Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};}
inline Vec3 satellite_scene_vector(const std::array<double,3>& p){return {static_cast<float>(p[0]),static_cast<float>(-p[1]),static_cast<float>(p[2])};}
inline Quaternion satellite_locked_rotation(stellar::core::SatelliteOrbit orbit,double days,bool parent=false){
  const auto p=stellar::core::satellite_relative_position(orbit,days);
  orbit.relative.eccentricity=0;orbit.relative.periapsis=0;orbit.relative.phase=0;
  const auto a=satellite_scene_vector(stellar::core::satellite_relative_position(orbit,0));
  orbit.relative.phase=std::numbers::pi/2;
  const auto b=satellite_scene_vector(stellar::core::satellite_relative_position(orbit,0));
  const Vec3 pole{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
  auto forward=satellite_scene_vector(p);const float sign=parent?1.f:-1.f;
  forward.x*=sign;forward.y*=sign;forward.z*=sign;
  return rotation_frame(forward,pole);
}
inline Lighting system_lighting(const stellar::native_system::NativeSystemSnapshot& snapshot,const stellar::native_system::NativeSystemBody& body){
  using namespace stellar::core;Lighting light;
  // Synchronous orientation is independent of stellar survey/lighting data.
  if(body.satellite_orbit)light.locked_rotation=satellite_locked_rotation(*body.satellite_orbit,snapshot.simulation_days);
  else for(const auto& moon:snapshot.bodies)if(moon.parent_body_id==body.id&&moon.satellite_orbit&&moon.satellite_orbit->binary)
    light.locked_rotation=satellite_locked_rotation(*moon.satellite_orbit,snapshot.simulation_days,true);
  if(!snapshot.stellar_object)return light;
  const auto system=stellar::native_system::snapshot_stellar_system(snapshot);const auto positions=stellar_positions(system,snapshot.simulation_days);
  const auto* primary=&body;
  // Resolve satellites through the same immutable mean elements as the chart.
  if(body.parent_body_id){const auto i=std::ranges::find(snapshot.bodies,*body.parent_body_id,&stellar::native_system::NativeSystemBody::id);if(i!=snapshot.bodies.end())primary=&*i;}
  if(!primary->stellar_orbit)return light;
  auto location=stellar::engine::analytic_orbit_position(*primary->stellar_orbit,snapshot.simulation_days);
  for(int axis=0;axis<3;++axis)location[axis]+=positions[primary->stellar_host][axis];
  if(body.satellite_orbit){const auto p=satellite_relative_position(*body.satellite_orbit,snapshot.simulation_days);for(int axis=0;axis<3;++axis)location[axis]+=p[axis]/149597870.7;}
  const auto count=system.stellar_orbits?system.stellar_orbits->companions.size()+1:1;
  std::array<DirectionalLight3D,3> sources;double total=0;
  for(std::size_t i=0;i<count;++i){const auto& star=i?system.stellar_orbits->companions[i-1]:*system.stellar_object;
    const double x=positions[i][0]-location[0],y=positions[i][1]-location[1],z=positions[i][2]-location[2],distance=std::max(1e-8,std::hypot(x,y,z));
    sources[i].direction={static_cast<float>(x/distance),static_cast<float>(-y/distance),static_cast<float>(z/distance)};
    sources[i].color=blackbody_light_color(std::clamp(star.effective_temperature_kelvin,100.,100000.));
    sources[i].intensity=static_cast<float>(star.luminosity_solar/(distance*distance));total+=sources[i].intensity;
  }
  // A common photographic exposure preserves each star's inverse-square share.
  const double exposure=std::clamp(std::pow(std::max(1e-8,total),.12),.75,1.3)/std::max(1e-8,total);
  for(auto& s:sources)s.intensity=static_cast<float>(s.intensity*exposure);
  light.direction=sources[0].direction;light.color=sources[0].color;light.intensity=sources[0].intensity;
  for(int i=0;i<2;++i)light.additional[i]=sources[i+1];
  const auto blocker=[&](const std::array<double,3>& relative,double radius_km){
    const auto p=satellite_scene_vector(relative);const double receiver=body.radius_earth*6371.;
    const double along=p.x*light.direction.x+p.y*light.direction.y+p.z*light.direction.z;
    const double miss=std::hypot(p.x-along*light.direction.x,p.y-along*light.direction.y,p.z-along*light.direction.z);
    if(along<=0||miss>radius_km+receiver)return;
    AnalyticShadow3D shadow;shadow.position={p.x/receiver,p.y/receiver,p.z/receiver};shadow.scale=static_cast<float>(radius_km/receiver);
    if(!light.eclipse||shadow.scale>light.eclipse->scale)light.eclipse=shadow;
  };
  if(body.satellite_orbit){auto p=satellite_relative_position(*body.satellite_orbit,snapshot.simulation_days);for(auto& x:p)x=-x;blocker(p,primary->radius_earth*6371.);}
  else for(const auto& moon:snapshot.bodies)if(moon.parent_body_id==body.id&&moon.satellite_orbit){const auto p=satellite_relative_position(*moon.satellite_orbit,snapshot.simulation_days);blocker(p,moon.radius_earth*6371.);}
  return light;
}
}
