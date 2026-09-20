#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cmath>

namespace stellar::native_map {
// A world-space coverage mask with a screen-space texture. Camera zoom changes
// coverage geometry, never the texel scale. Reuses the existing textured mesh
// renderer, including clipping and independent vertex alpha.
template<class Coverage>
TriangleMesh texture_coverage_mesh(std::shared_ptr<const RgbaImage> texture,
    UiRect bounds,UiRect viewport,Color color,Coverage coverage) {
  TriangleMesh mesh;mesh.texture=std::move(texture);mesh.color=color;mesh.clip=viewport;
  const float left=std::max(bounds.x,viewport.x),top=std::max(bounds.y,viewport.y);
  const float right=std::min(bounds.x+bounds.width,viewport.x+viewport.width);
  const float bottom=std::min(bounds.y+bounds.height,viewport.y+viewport.height);
  if(right<=left||bottom<=top)return mesh;
  const auto step=std::clamp(std::min(bounds.width,bounds.height)/12.f,8.f,48.f);
  const int columns=std::max(2,static_cast<int>(std::ceil((right-left)/step)));
  const int rows=std::max(2,static_cast<int>(std::ceil((bottom-top)/step)));
  for(int y=0;y<=rows;++y)for(int x=0;x<=columns;++x){
    const Point p{std::lerp(left,right,x/static_cast<float>(columns)),std::lerp(top,bottom,y/static_cast<float>(rows))};
    auto c=color;c.a=static_cast<std::uint8_t>(std::clamp(std::lround(color.a*std::clamp(coverage(p),0.,1.)),0l,255l));
    mesh.vertices.push_back(p);mesh.vertex_colors.push_back(c);
    mesh.texture_coordinates.push_back({(p.x-viewport.x)/viewport.width,(p.y-viewport.y)/viewport.height});
  }
  for(int y=0;y<rows;++y)for(int x=0;x<columns;++x){const int a=y*(columns+1)+x,b=a+1,c=a+columns+1,d=c+1;mesh.indices.insert(mesh.indices.end(),{a,b,c,b,d,c});}
  return mesh;
}
} // namespace stellar::native_map
