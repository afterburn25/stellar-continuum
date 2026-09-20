#pragma once
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace stellar::engine {
// Unit-agnostic, immutable Kepler elements. Time is measured in caller units.
struct AnalyticOrbit {
  double radius{}, eccentricity{}, inclination{}, ascending_node{}, periapsis{},
      phase{}, angular_speed{};
  bool operator==(const AnalyticOrbit&) const = default;
};
inline void validate_orbit(const AnalyticOrbit& o) {
  for (double v : {o.radius,o.eccentricity,o.inclination,o.ascending_node,o.periapsis,o.phase,o.angular_speed})
    if (!std::isfinite(v)) throw std::invalid_argument("Non-finite analytic orbit");
  if (o.radius<=0 || o.eccentricity<0 || o.eccentricity>=.95)
    throw std::invalid_argument("Invalid analytic orbit radius/eccentricity");
}
inline std::array<double,3> analytic_orbit_position(const AnalyticOrbit& o,double elapsed) {
  validate_orbit(o);
  if (!std::isfinite(elapsed)||!std::isfinite(elapsed*o.angular_speed)) throw std::invalid_argument("Invalid orbital time");
  const double m=std::remainder(o.phase+std::remainder(elapsed*o.angular_speed,2*std::numbers::pi),2*std::numbers::pi);
  double e=m;
  for (int i=0;i<12;++i) e-=(e-o.eccentricity*std::sin(e)-m)/(1-o.eccentricity*std::cos(e));
  const double x=o.radius*(std::cos(e)-o.eccentricity),y=o.radius*std::sqrt(1-o.eccentricity*o.eccentricity)*std::sin(e);
  const double p=x*std::cos(o.periapsis)-y*std::sin(o.periapsis),q=x*std::sin(o.periapsis)+y*std::cos(o.periapsis);
  return {p*std::cos(o.ascending_node)-q*std::cos(o.inclination)*std::sin(o.ascending_node),
          p*std::sin(o.ascending_node)+q*std::cos(o.inclination)*std::cos(o.ascending_node),q*std::sin(o.inclination)};
}
// Rotate a nested orbit from its parent's reference plane into the outer frame.
inline std::array<double,3> framed_orbit_position(const AnalyticOrbit& orbit,double inclination,double node,double elapsed){
  if(!std::isfinite(inclination)||!std::isfinite(node))throw std::invalid_argument("Invalid orbital frame");
  const auto p=analytic_orbit_position(orbit,elapsed);
  const double y=p[1]*std::cos(inclination)-p[2]*std::sin(inclination),z=p[1]*std::sin(inclination)+p[2]*std::cos(inclination);
  return {p[0]*std::cos(node)-y*std::sin(node),p[0]*std::sin(node)+y*std::cos(node),z};
}
struct AnalyticSpin {
  std::array<double,3> axis{0,0,1};
  double phase{},rate{},wobble{},precession{};
  bool operator==(const AnalyticSpin&) const = default;
};
// Quaternion x,y,z,w, with a deterministic secondary end-over-end precession.
inline std::array<double,4> analytic_spin_rotation(const AnalyticSpin& s,double elapsed) {
  if (!std::isfinite(elapsed)) throw std::invalid_argument("Invalid spin time");
  for(double v:{s.axis[0],s.axis[1],s.axis[2],s.phase,s.rate,s.wobble,s.precession})
    if(!std::isfinite(v))throw std::invalid_argument("Invalid spin parameter");
  const double norm=std::hypot(s.axis[0],s.axis[1],s.axis[2]);
  if(norm<1e-12||!std::isfinite(norm)||!std::isfinite(s.phase+elapsed*s.rate)||!std::isfinite(s.phase+elapsed*s.precession))throw std::invalid_argument("Invalid spin axis/time range");
  const double a=std::remainder(s.phase+elapsed*s.rate,2*std::numbers::pi)*.5;
  const double b=s.wobble*std::sin(s.phase+elapsed*s.precession)*.5;
  const double x=s.axis[0]/norm*std::sin(a),y=s.axis[1]/norm*std::sin(a),z=s.axis[2]/norm*std::sin(a),w=std::cos(a);
  return {std::cos(b)*x+std::sin(b)*w,std::cos(b)*y-std::sin(b)*z,
          std::cos(b)*z+std::sin(b)*y,std::cos(b)*w-std::sin(b)*x};
}
// Monotonic presentation transform; never use display coordinates in physics.
inline double stretched_orbit_radius(double radius,double unit,double exponent) {
  if(!std::isfinite(radius)||radius<0||!std::isfinite(unit)||unit<=0||!std::isfinite(exponent)||exponent<=0||exponent>1)
    throw std::invalid_argument("Invalid orbital presentation transform");
  return unit*std::pow(radius,exponent);
}
}
