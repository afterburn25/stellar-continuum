#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace stellar::engine {
// Units belong to the caller. Fixed simulation steps, never render frame time,
// drive this deterministic acceleration-limited kinematic model.
struct PhysicsVector3 { float x{},y{},z{}; };
inline PhysicsVector3 add(PhysicsVector3 a,PhysicsVector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline PhysicsVector3 subtract(PhysicsVector3 a,PhysicsVector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline PhysicsVector3 multiply(PhysicsVector3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float length_squared(PhysicsVector3 a){return a.x*a.x+a.y*a.y+a.z*a.z;}
inline float length(PhysicsVector3 a){return std::sqrt(length_squared(a));}
inline PhysicsVector3 normalize(PhysicsVector3 a){const auto n=length(a);if(!(n>0&&std::isfinite(n)))throw std::invalid_argument("Invalid 3D direction");return {a.x/n,a.y/n,a.z/n};}
inline bool finite(PhysicsVector3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
struct KinematicState3D { PhysicsVector3 position,velocity; };
inline KinematicState3D move_toward_velocity(KinematicState3D state,PhysicsVector3 desired,float maximum_delta_speed,float seconds){
  if(!finite(state.position)||!finite(state.velocity)||!finite(desired)||!std::isfinite(maximum_delta_speed)||maximum_delta_speed<0||!std::isfinite(seconds)||seconds<0||seconds>60)
    throw std::invalid_argument("Invalid 3D motion step");
  auto change=subtract(desired,state.velocity);const auto magnitude=length(change);
  if(!std::isfinite(magnitude))throw std::overflow_error("3D velocity exceeds supported magnitude");
  if(magnitude>maximum_delta_speed)change=multiply(normalize(change),maximum_delta_speed);
  state.velocity=add(state.velocity,change);state.position=add(state.position,multiply(state.velocity,seconds));
  if(!finite(state.position)||!finite(state.velocity))throw std::overflow_error("3D motion overflow");
  return state;
}
struct CollisionVector3 { double x{},y{},z{}; };
inline CollisionVector3 subtract(CollisionVector3 a,CollisionVector3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline double dot(CollisionVector3 a,CollisionVector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline CollisionVector3 cross(CollisionVector3 a,CollisionVector3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline CollisionVector3 multiply(CollisionVector3 a,double s){return {a.x*s,a.y*s,a.z*s};}
inline CollisionVector3 negate(CollisionVector3 a){return {-a.x,-a.y,-a.z};}
inline double length_squared(CollisionVector3 a){return dot(a,a);}
inline CollisionVector3 normalize(CollisionVector3 a){const auto n=std::sqrt(length_squared(a));if(!(n>0&&std::isfinite(n)))throw std::invalid_argument("Invalid 3D direction");return {a.x/n,a.y/n,a.z/n};}
inline void validate_collision(CollisionVector3 p){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>1e15||std::abs(p.y)>1e15||std::abs(p.z)>1e15)throw std::invalid_argument("Invalid 3D collision coordinate");}
// Segment fractions in [0,1], including initial overlap. Continuous queries
// avoid tunnelling through thin surfaces between simulation/pointer samples.
inline std::optional<double> segment_sphere(CollisionVector3 from,CollisionVector3 to,CollisionVector3 center,double radius){
  validate_collision(from);validate_collision(to);validate_collision(center);
  if(!std::isfinite(radius)||radius<0||radius>1e15)throw std::invalid_argument("Invalid sphere radius");
  auto m=subtract(from,center),d=subtract(to,from);double c=dot(m,m)-radius*radius;
  if(c<=0)return 0.;double a=dot(d,d),b=dot(m,d);if(a==0||b>0)return {};
  double discriminant=b*b-a*c;if(discriminant<0)return {};
  // This form avoids cancellation for a near surface and distant endpoint.
  const double t=c/(-b+std::sqrt(discriminant));if(t<0||t>1)return {};return t;
}
inline std::optional<double> segment_triangle(CollisionVector3 from,CollisionVector3 to,CollisionVector3 a,CollisionVector3 b,CollisionVector3 c){
  for(auto p:{from,to,a,b,c})validate_collision(p);
  const auto d=subtract(to,from),e1=subtract(b,a),e2=subtract(c,a),p=cross(d,e2);
  const double determinant=dot(e1,p),scale=std::sqrt(dot(e1,e1)*dot(p,p));
  if(scale==0||std::abs(determinant)<=scale*1e-12)return {};
  const auto s=subtract(from,a),q=cross(s,e1);const double u=dot(s,p)/determinant,v=dot(d,q)/determinant,t=dot(e2,q)/determinant;
  constexpr double edge=1e-10;if(u<-edge||v<-edge||u+v>1+edge||t<0||t>1)return {};return t;
}
// Oriented box: world center, three unit world axes, and a half-extent
// along each — the local AABB of a mesh under rotation+scale. Tighter
// than a world AABB for rotated geometry; SAT gives both the overlap
// test and the minimum translation vector for push-out.
struct ObBox3D { CollisionVector3 center,axis[3]; double half[3]{}; };
// SAT narrow-phase over the 15 candidate separating axes (6 face normals
// + 9 edge-pair cross products). Returns the minimum translation vector
// that pushes `b` out of `a` (oriented a→b), or nullopt when a
// separating axis exists (disjoint or merely touching).
inline std::optional<CollisionVector3> obb_separation(const ObBox3D &a,const ObBox3D &b){
  for(auto p:{a.center,b.center})validate_collision(p);
  for(auto ax:a.axis)validate_collision(ax);
  for(auto ax:b.axis)validate_collision(ax);
  const auto delta=subtract(b.center,a.center);
  CollisionVector3 axes[15];std::size_t count=0;
  for(int i=0;i<3;++i){axes[count++]=a.axis[i];axes[count++]=b.axis[i];}
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){
    const auto c=cross(a.axis[i],b.axis[j]);
    if(length_squared(c)>1e-18)axes[count++]=normalize(c);
  }
  double best=std::numeric_limits<double>::max();CollisionVector3 best_axis{};
  for(std::size_t k=0;k<count;++k){
    const auto L=axes[k];const double dist=dot(delta,L);
    double ra=0,rb=0;
    for(int i=0;i<3;++i){ra+=a.half[i]*std::abs(dot(a.axis[i],L));rb+=b.half[i]*std::abs(dot(b.axis[i],L));}
    const double overlap=ra+rb-std::abs(dist);
    if(overlap<=0)return {};
    if(overlap<best){best=overlap;best_axis=dist>=0?L:negate(L);}
  }
  return multiply(best_axis,best);
}
} // namespace stellar::engine
