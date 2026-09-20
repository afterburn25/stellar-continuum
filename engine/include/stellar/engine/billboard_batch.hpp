#pragma once
#include <stellar/engine/native_triangle_mesh.hpp>
#include <array>

namespace stellar::native_map {
// Reusable CPU instancing into a single indexed draw per immutable texture.
// The quaternion supplies roll/foreshortening for tumbling camera-facing cards.
inline void append_billboard(TriangleMesh& batch,Point center,float half_width,float half_height,
                             const std::array<double,4>& q,Color tint){
  if(batch.vertices.size()+4>maximum_triangle_mesh_vertices||batch.indices.size()+6>maximum_triangle_mesh_indices)
    throw std::length_error("Billboard batch budget exceeded");
  if(!std::isfinite(half_width)||!std::isfinite(half_height)||half_width<=0||half_height<=0)
    throw std::invalid_argument("Invalid billboard size");
  const auto [x,y,z,w]=q;const double norm=x*x+y*y+z*z+w*w;
  if(!std::isfinite(norm)||std::abs(norm-1)>1e-4)throw std::invalid_argument("Billboard rotation must be normalized");
  const double angle=std::atan2(2*(w*z+x*y),1-2*(y*y+z*z));
  const double squash=.35+.65*std::abs(1-2*(x*x+y*y));
  const float c=static_cast<float>(std::cos(angle)),s=static_cast<float>(std::sin(angle));
  const int offset=static_cast<int>(batch.vertices.size());
  constexpr std::array<Point,4> corners{{{-1,-1},{1,-1},{1,1},{-1,1}}};
  for(const auto p:corners){const float px=p.x*half_width,py=p.y*half_height*static_cast<float>(squash);
    batch.vertices.push_back({center.x+px*c-py*s,center.y+px*s+py*c});batch.texture_coordinates.push_back({(p.x+1)*.5f,(p.y+1)*.5f});batch.vertex_colors.push_back(tint);}
  for(int i:{0,1,2,0,2,3})batch.indices.push_back(offset+i);
}
}
