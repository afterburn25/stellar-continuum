#pragma once
#include <stellar/engine/native_scene3d.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace stellar::native_map {
// Rectilinear source plate projected onto a unit celestial dome. Translation and
// system zoom are deliberately absent. A 2D system camera exposes roll only;
// callers with a 3D sky camera may pass its orientation to the scene separately.
// The isotropic crop covers the viewport even after roll, without wrapping,
// rectangular edges, duplicate boundary stars or equirectangular poles.
inline std::shared_ptr<const Mesh3D> celestial_plate_mesh(float source_aspect,float view_aspect,
    float roll=0,Point crop={},bool mirror=false){
 if(!std::isfinite(source_aspect)||!std::isfinite(view_aspect)||source_aspect<=0||view_aspect<=0||view_aspect>8||!std::isfinite(roll)||!std::isfinite(crop.x)||!std::isfinite(crop.y)||std::abs(crop.x)>.1f||std::abs(crop.y)>.1f)
   throw std::invalid_argument("Invalid celestial plate projection");
 constexpr int cols=48,rows=32;constexpr float tangent=.577350269f;
 const float cs=std::cos(roll),sn=std::sin(roll);
 const float fit=std::max((std::abs(cs)*view_aspect+std::abs(sn))/source_aspect,std::abs(sn)*view_aspect+std::abs(cs))*1.015f/(1-2*std::max(std::abs(crop.x),std::abs(crop.y)));
 std::vector<Vertex3D> v;std::vector<std::uint32_t> ix;
 for(int y=0;y<=rows;++y)for(int x=0;x<=cols;++x){
  const float px=(2.f*x/cols-1)*view_aspect,py=1-2.f*y/rows;
  const float length=std::sqrt(1+tangent*tangent*(px*px+py*py));
  const Vec3 p{px*tangent/length,py*tangent/length,-1/length};
  const float u=(cs*px-sn*py)/(fit*source_aspect)*.5f+.5f+crop.x;
  const float w=-(sn*px+cs*py)/fit*.5f+.5f+crop.y;
  v.push_back({p,{-p.x,-p.y,-p.z},{mirror?1-u:u,w}});
  if(x<cols&&y<rows){const auto a=static_cast<std::uint32_t>(y*(cols+1)+x),b=a+1,c=a+cols+1;ix.insert(ix.end(),{a,c,b,b,c,c+1});}
 }
 return Mesh3D::create(std::move(v),std::move(ix));
}
// Area filtering retains point-source energy at lower quality levels. Full
// quality samples at source resolution. This never alters the approved file.
inline std::shared_ptr<const RgbaImage> prepare_celestial_plate(const RgbaImage& source,int max_width,int density){
 if(max_width<64||max_width>4096||density<0||density>2)throw std::invalid_argument("Invalid celestial plate quality");
 const int w=std::min(source.width(),max_width),h=std::max(1,static_cast<int>(std::lround(double(source.height())*w/source.width())));
 std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const double x0=double(x)*source.width()/w,x1=double(x+1)*source.width()/w,y0=double(y)*source.height()/h,y1=double(y+1)*source.height()/h;
  std::array<double,3> color{};
  for(int sy=static_cast<int>(y0);sy<std::min(source.height(),static_cast<int>(std::ceil(y1)));++sy)
   for(int sx=static_cast<int>(x0);sx<std::min(source.width(),static_cast<int>(std::ceil(x1)));++sx){
    const double weight=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)))/((x1-x0)*(y1-y0));
    for(int k=0;k<3;++k)color[k]+=source.pixels()[(static_cast<std::size_t>(sy)*source.width()+sx)*4+k]*weight;
   }
  const double peak=*std::max_element(color.begin(),color.end());
  const double visibility=density==0?std::clamp(peak/32.,0.,1.):density==2?1.15:1.;
  const auto at=(static_cast<std::size_t>(y)*w+x)*4;
  for(int k=0;k<3;++k)pixels[at+k]=static_cast<std::uint8_t>(std::clamp(std::lround(color[k]*visibility),0l,255l));pixels[at+3]=255;
 }
 return RgbaImage::create(w,h,std::move(pixels));
}
}
