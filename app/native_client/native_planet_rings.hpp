#pragma once
#include "native_planet_surface_assets.hpp"
#include <stellar/engine/native_geometry3d.hpp>
#include <span>

namespace stellar::native_system_ui {
using namespace stellar::native_map;
struct PlanetRingBand {float inner,outer;Color color;};
// Artistic radial profiles matched to the supplied portraits. They are visual
// geometry, not simulated particles or inferred physical measurements.
inline constexpr auto saturn_ring_bands=std::to_array<PlanetRingBand>({
  PlanetRingBand{1.23f,1.30f,{109,102,90,90}}, {1.30f,1.38f,{132,123,108,110}},
  {1.38f,1.45f,{94,88,78,140}}, {1.45f,1.51f,{159,148,128,190}},
  {1.52f,1.58f,{204,189,160,245}}, {1.58f,1.64f,{230,214,185,255}},
  {1.64f,1.68f,{194,181,159,255}}, {1.68f,1.74f,{237,219,188,255}},
  {1.74f,1.78f,{210,192,163,255}}, {1.78f,1.85f,{244,225,192,255}},
  {1.85f,1.91f,{224,206,179,255}}, {1.91f,1.95f,{197,184,163,255}},
  {2.015f,2.06f,{157,150,137,235}}, {2.06f,2.13f,{194,184,164,245}},
  {2.13f,2.19f,{211,199,177,245}}, {2.20f,2.25f,{172,164,151,230}},
  {2.25f,2.30f,{190,180,160,235}}, {2.325f,2.335f,{219,205,181,190}}
});
inline constexpr auto uranus_ring_bands=std::to_array<PlanetRingBand>({
  PlanetRingBand{1.38f,1.388f,{86,105,107,110}}, {1.48f,1.490f,{102,119,120,145}},
  {1.56f,1.567f,{115,130,130,150}}, {1.64f,1.651f,{106,121,122,160}},
  {1.70f,1.710f,{143,150,148,175}}, {1.76f,1.773f,{104,119,121,165}},
  {1.82f,1.835f,{149,157,156,180}}, {1.89f,1.910f,{122,139,139,190}},
  {1.94f,1.975f,{153,164,162,215}}
});
inline std::span<const PlanetRingBand> planet_ring_bands(std::string_view key){
  if(key=="saturn")return saturn_ring_bands;if(key=="uranus")return uranus_ring_bands;return {};
}
inline float planet_ring_extent(std::string_view key){const auto bands=planet_ring_bands(key);return bands.empty()?1.f:bands.back().outer;}
inline const std::vector<MeshInstance3D>& planet_ring_instances(std::string_view key){
  const auto build=[](std::span<const PlanetRingBand> bands){std::vector<MeshInstance3D> result;
    for(const auto& band:bands){Material3D material;material.tint=band.color;material.ambient=.68f;material.diffuse=.32f;material.double_sided=true;material.transparent=band.color.a<255;
      result.push_back({annulus_mesh(band.inner,band.outer),{},{},1,material});}return result;};
  static const auto saturn=build(saturn_ring_bands),uranus=build(uranus_ring_bands);
  static const std::vector<MeshInstance3D> empty;
  return key=="saturn"?saturn:key=="uranus"?uranus:empty;
}
// Reuse the same annuli and colors for the two depth halves around a 2D disc.
inline void append_planet_rings(DrawList& out,std::string_view key,Point center,float radius,UiRect clip,bool front){
  const auto pose=planet_presentation_pose(key);
  const float cp=std::cos(pose.pitch),sp=std::sin(pose.pitch),cr=std::cos(pose.roll),sr=std::sin(pose.roll);
  TriangleMesh mesh;mesh.clip=clip;mesh.color={255,255,255,255};
  for(const auto& ring:planet_ring_instances(key)){
    const auto start=static_cast<int>(mesh.vertices.size());
    for(const auto& v:ring.mesh->vertices()){const float x=v.position.x,y=-v.position.z*sp;
      mesh.vertices.push_back({center.x+radius*(x*cr-y*sr),center.y-radius*(x*sr+y*cr)});mesh.vertex_colors.push_back(ring.material.tint);}
    const auto& indices=ring.mesh->indices();const auto& vertices=ring.mesh->vertices();
    for(std::size_t i=0;i<indices.size();i+=3){const float depth=(vertices[indices[i]].position.z+vertices[indices[i+1]].position.z+vertices[indices[i+2]].position.z)*cp;
      if((depth>=0)==front)for(int n=0;n<3;++n)mesh.indices.push_back(start+static_cast<int>(indices[i+n]));}
  }
  if(!mesh.indices.empty())out.world.emplace_back(std::move(mesh));
}
}
