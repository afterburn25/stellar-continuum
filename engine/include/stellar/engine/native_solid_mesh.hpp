#pragma once
#include <stellar/engine/native_scene3d.hpp>
#include <cmath>
#include <functional>

namespace stellar::native_map {
// A closed, star-shaped solid defined on unit directions. Unlike radial terrain,
// this supports arbitrary elongation. The caller owns the shape and its units.
// Derivative normals remain valid after deformation, including at the UV poles.
inline std::shared_ptr<const Mesh3D> directional_solid_mesh(
    const std::function<Vec3(Vec3)>& surface,int columns=64,int rows=32) {
  if(!surface)throw std::invalid_argument("Missing solid surface");
  const auto sphere=Mesh3D::uv_sphere(columns,rows);
  auto vertices=sphere->vertices();
  const auto unit=[](Vec3 p){const float r=std::hypot(p.x,p.y,p.z);
    if(!std::isfinite(r)||r<1e-8f)throw std::invalid_argument("Degenerate solid surface derivative");
    return Vec3{p.x/r,p.y/r,p.z/r};};
  const auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
  const auto point=[&](Vec3 n){const auto p=surface(unit(n));
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.x*n.x+p.y*n.y+p.z*n.z<=0)
      throw std::invalid_argument("Solid surface must be finite and face outward");
    return p;};
  for(auto& v:vertices){
    const auto n=v.normal;
    const auto t=unit(cross(std::abs(n.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0},n));
    const auto b=cross(n,t);
    const auto derivative=[&](Vec3 axis){constexpr float d=.001f;
      const auto a=point({n.x+d*axis.x,n.y+d*axis.y,n.z+d*axis.z});
      const auto b=point({n.x-d*axis.x,n.y-d*axis.y,n.z-d*axis.z});
      return Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    v.position=point(n);v.normal=unit(cross(derivative(t),derivative(b)));
  }
  return Mesh3D::create(std::move(vertices),sphere->indices());
}
}
