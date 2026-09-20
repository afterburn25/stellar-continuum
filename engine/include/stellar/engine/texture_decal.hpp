#pragma once
#include <stellar/engine/texture_coverage_mesh.hpp>

namespace stellar::native_map {
enum class DecalBlendProfile { Luminous, Obscuring, OpaqueCutout };
struct DecalMapping {
  double x{},y{},half_width{1},half_height{1},rotation{};
  bool mirrored{};
  [[nodiscard]] Point uv(double world_x,double world_y)const {
    const auto dx=world_x-x,dy=world_y-y,c=std::cos(rotation),s=std::sin(rotation);
    const double u=.5+(c*dx+s*dy)/(2*half_width),v=.5+(-s*dx+c*dy)/(2*half_height);
    return {static_cast<float>(mirrored?1-u:u),static_cast<float>(v)};
  }
};
inline int decal_lod_width(double projected_width,int density=1){
  // Density changes detail, never existence or coordinates. Overview stays visible.
  const auto pixels=projected_width*(density==0?.7:density==2?1.2:1.);
  return pixels<=360?256:pixels<=1000?768:2944;
}
// CPU-only worker operation. Source RGB stays intact on disk. Builds straight
// alpha with a feathered perimeter and area filtering before GPU submission.
std::shared_ptr<const RgbaImage> prepare_decal_texture(const RgbaImage&,int width,DecalBlendProfile);
template<class Coverage,class UvMapping>
TriangleMesh masked_decal_mesh(std::shared_ptr<const RgbaImage> image,UiRect bounds,UiRect viewport,Color tint,Coverage coverage,UvMapping uv){
  auto mesh=texture_coverage_mesh(std::move(image),bounds,viewport,tint,coverage);
  for(std::size_t i=0;i<mesh.vertices.size();++i){
    const auto p=uv(mesh.vertices[i]);
    if(p.x<0||p.y<0||p.x>1||p.y>1)mesh.vertex_colors[i].a=0;
    mesh.texture_coordinates[i]={std::clamp(p.x,0.f,1.f),std::clamp(p.y,0.f,1.f)};
  }
  return mesh;
}
// Safe aggregation without reordering layers: use only within a single depth.
inline bool append_decal_batch(TriangleMesh& batch,TriangleMesh&& next){
  if(batch.texture!=next.texture||batch.vertices.size()+next.vertices.size()>65536||batch.indices.size()+next.indices.size()>196608)return false;
  const auto offset=static_cast<int>(batch.vertices.size());batch.vertices.insert(batch.vertices.end(),next.vertices.begin(),next.vertices.end());batch.texture_coordinates.insert(batch.texture_coordinates.end(),next.texture_coordinates.begin(),next.texture_coordinates.end());batch.vertex_colors.insert(batch.vertex_colors.end(),next.vertex_colors.begin(),next.vertex_colors.end());for(int i:next.indices)batch.indices.push_back(i+offset);return true;
}
} // namespace stellar::native_map
