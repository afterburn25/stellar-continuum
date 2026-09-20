#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace stellar::engine {
enum class OrganicShape { Cloud, Filament, Cluster, Shell, Crescent, Band };
struct OrganicRegion {
  double x{}, y{}, extent_x{1}, extent_y{1}, rotation{}, roughness{.2};
  std::uint64_t seed{};
  OrganicShape shape{OrganicShape::Cloud};
  bool operator==(const OrganicRegion&) const = default;
};
struct RegionSample {
  double overlap{}, density{}, edge_distance{}, center_distance{};
};
inline double region_noise(std::uint64_t seed, double x, double y) {
  const auto hash=[seed](std::int64_t ix,std::int64_t iy){
    auto v=seed^(static_cast<std::uint64_t>(ix)*0x9e3779b97f4a7c15ULL)^(
      static_cast<std::uint64_t>(iy)*0xbf58476d1ce4e5b9ULL);
    v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;v=(v^(v>>27))*0x94d049bb133111ebULL;
    return static_cast<double>((v^(v>>31))>>11)*0x1.0p-53;
  };
  const auto ix=static_cast<std::int64_t>(std::floor(x)),iy=static_cast<std::int64_t>(std::floor(y));
  auto fx=x-std::floor(x),fy=y-std::floor(y);fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
  return std::lerp(std::lerp(hash(ix,iy),hash(ix+1,iy),fx),std::lerp(hash(ix,iy+1),hash(ix+1,iy+1),fx),fy);
}
inline void validate_organic_region(const OrganicRegion& s) {
  if(!std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.extent_x)||!std::isfinite(s.extent_y)||
     !std::isfinite(s.rotation)||!std::isfinite(s.roughness)||std::abs(s.x)>1e7||std::abs(s.y)>1e7||
     s.extent_x<=0||s.extent_y<=0||s.extent_x>1e5||s.extent_y>1e5||s.roughness<0||s.roughness>.45||
     static_cast<unsigned>(s.shape)>static_cast<unsigned>(OrganicShape::Band))
    throw std::invalid_argument("Invalid organic spatial region");
}
// Signed distance is measured along the center-to-query ray, in world units.
// Both simulation overlap and presentation use this same bounded silhouette.
inline RegionSample sample_region(const OrganicRegion& s,double x,double y,bool broad_phase=true) {
  const auto dx=x-s.x,dy=y-s.y,distance=std::hypot(dx,dy);
  const auto bound=std::max(s.extent_x,s.extent_y);
  if(!std::isfinite(distance)||(broad_phase&&distance>bound)) return {0,0,bound-distance,distance};
  const auto c=std::cos(s.rotation),sn=std::sin(s.rotation);
  const auto u=(c*dx+sn*dy)/s.extent_x,v=(-sn*dx+c*dy)/s.extent_y;
  const auto a=std::atan2(v,u),r=std::hypot(u,v);
  const auto phase=static_cast<double>(s.seed&65535)*.0000958738;
  const auto outline=1-s.roughness*(.5+.27*std::sin(a*3+phase)+.23*std::sin(a*7-phase));
  const auto radial_scale=r>1e-9?distance/r:std::min(s.extent_x,s.extent_y);
  auto edge=(outline-r)*radial_scale;
  if(edge<=0)return {0,0,edge,distance};
  // Remnant shells have an inner boundary as well as the outer silhouette.
  // A point in the hollow centre is outside, even though it is inside the bounds.
  if(s.shape==OrganicShape::Shell||s.shape==OrganicShape::Crescent){
    edge=std::min(edge,(r-.36)*radial_scale);
    if(edge<=0)return {0,0,edge,distance};
  }
  auto overlap=std::clamp((outline-r)/.48,0.,1.);
  overlap=overlap*overlap*(3-2*overlap);
  double texture=.34+.40*region_noise(s.seed,u*3+10,v*3+10)+.18*region_noise(s.seed^73,u*9+10,v*9+10)+.08*region_noise(s.seed^919,u*23+10,v*23+10);
  if(s.shape==OrganicShape::Shell||s.shape==OrganicShape::Crescent){
    const auto inner=std::clamp((r-.36)/.16,0.,1.);overlap*=inner*inner*(3-2*inner);
    texture*=.4+.6*std::pow(std::sin(a*9+phase)*.5+.5,2);
    if(s.shape==OrganicShape::Crescent)texture*=.1+.9*std::pow(std::cos(a-phase)*.5+.5,2);
  }else if(s.shape==OrganicShape::Filament||s.shape==OrganicShape::Band){
    texture*=.3+.7*std::exp(-std::pow(v*3+.4*std::sin(u*8+phase),2));
  }else if(s.shape==OrganicShape::Cluster)texture*=.4+.6*region_noise(s.seed^789,u*5,v*5);
  return {overlap,std::clamp(overlap*texture,0.,1.),edge,distance};
}
} // namespace stellar::engine
