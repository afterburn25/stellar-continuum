#pragma once
#include <stellar/engine/native_scene3d.hpp>
#include <cmath>
#include <numbers>
namespace stellar::native_map {
inline Vec3 spherical_surface_normal(double latitude,double longitude){return {static_cast<float>(std::cos(latitude)*std::sin(longitude)),static_cast<float>(std::sin(latitude)),static_cast<float>(std::cos(latitude)*std::cos(longitude))};}
// A ribbon's local +Y is radial; its +X spans the surface tangent.
inline Quaternion spherical_surface_rotation(double latitude,double longitude,double orientation){
 auto q=compose_rotation(rotation_axis_angle({0,1,0},static_cast<float>(longitude)),rotation_axis_angle({1,0,0},static_cast<float>(std::numbers::pi/2-latitude)));
 return compose_rotation(q,rotation_axis_angle({0,1,0},static_cast<float>(orientation)));
}
inline bool sphere_occludes_orthographic(Vec3 p,Vec3 center,float radius){
 const float x=p.x-center.x,y=p.y-center.y,z=p.z-center.z,d=radius*radius-x*x-y*y;
 return d>0&&z<std::sqrt(d)-.0001f;
}
// Immutable closed proxy for ray-integrated plasma; unlike a ribbon, side views
// have a real cross section. Winding and normals point outward on all six faces.
inline std::shared_ptr<const Mesh3D> surface_emission_volume(float depth){
 if(!std::isfinite(depth)||depth<=0||depth>.75f)throw std::invalid_argument("Invalid emission volume depth");
 std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
 const auto face=[&](Vec3 c,Vec3 u,Vec3 v,Vec3 n){
  const auto base=static_cast<std::uint32_t>(vertices.size());
  for(const auto corner:std::array<Point,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})
   vertices.push_back({{c.x+u.x*corner.x+v.x*corner.y,c.y+u.y*corner.x+v.y*corner.y,c.z+u.z*corner.x+v.z*corner.y},n,{(corner.x+1)*.5f,(1-corner.y)*.5f}});
  indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
 };
 face({0,.28f,depth},{.5f,0,0},{0,.5f,0},{0,0,1});
 face({0,.28f,-depth},{-.5f,0,0},{0,.5f,0},{0,0,-1});
 face({.5f,.28f,0},{0,0,-depth},{0,.5f,0},{1,0,0});
 face({-.5f,.28f,0},{0,0,depth},{0,.5f,0},{-1,0,0});
 face({0,.78f,0},{.5f,0,0},{0,0,-depth},{0,1,0});
 face({0,-.22f,0},{.5f,0,0},{0,0,depth},{0,-1,0});
 return Mesh3D::create(std::move(vertices),std::move(indices));
}
inline std::shared_ptr<const Mesh3D> curved_surface_ribbon(int segments){
 if(segments<2||segments>64)throw std::invalid_argument("Invalid ribbon detail");
 std::vector<Vertex3D> vertices;std::vector<std::uint32_t> indices;
 for(int y=0;y<=segments;++y)for(int x=0;x<=segments;++x){const float u=static_cast<float>(x)/segments,v=static_cast<float>(y)/segments;
  vertices.push_back({{u-.5f,.78f-v,.12f*std::sin(u*std::numbers::pi_v<float>)*std::sin(v*std::numbers::pi_v<float>)},{0,0,1},{u,v}});
  if(x<segments&&y<segments){const auto a=static_cast<std::uint32_t>(y*(segments+1)+x),b=a+segments+1;indices.insert(indices.end(),{a,b,a+1,a+1,b,b+1});}
 }
 return Mesh3D::create(std::move(vertices),std::move(indices));
}
}
