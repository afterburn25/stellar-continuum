#pragma once
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace stellar::engine::ui_skin {
using namespace stellar::native_map;
inline Color blend(Color a,Color b,float t){
  const auto channel=[&](int x,int y){return static_cast<std::uint8_t>(std::clamp(std::lround(x+(y-x)*t),0l,255l));};
  return {channel(a.r,b.r),channel(a.g,b.g),channel(a.b,b.b),channel(a.a,b.a)};
}
inline std::array<Point,8> contour(UiRect r,float cut){
  const float c=std::clamp(cut,0.f,std::max(0.f,std::min(r.width,r.height)*.5f));
  return {{{r.x+c,r.y},{r.x+r.width-c,r.y},{r.x+r.width,r.y+c},{r.x+r.width,r.y+r.height-c},
           {r.x+r.width-c,r.y+r.height},{r.x+c,r.y+r.height},{r.x,r.y+r.height-c},{r.x,r.y+c}}};
}
// Drawable-pixel vector skin. Constant bounded geometry; one gradient mesh
// and one border mesh. Optional clipping follows scrolling card content.
inline void gradient(DrawList &out,UiRect r,Color top,Color bottom,float cut=6.f,std::optional<UiRect> clip={}){
  if(r.width<=0||r.height<=0)return;
  const auto points=contour(r,cut);TriangleMesh mesh;mesh.color={255,255,255,255};mesh.clip=clip;
  mesh.vertices.assign(points.begin(),points.end());
  for(const auto p:points)mesh.vertex_colors.push_back(blend(top,bottom,(p.y-r.y)/r.height));
  for(int i=1;i<7;++i)mesh.indices.insert(mesh.indices.end(),{0,i,i+1});
  out.overlay.emplace_back(std::move(mesh));
}
inline void rim(DrawList &out,UiRect r,Color top,Color bottom,float thickness,float cut,std::optional<UiRect> clip={}){
  if(r.width<=2*thickness||r.height<=2*thickness||thickness<=0)return;
  const auto outer=contour(r,cut),inner=contour({r.x+thickness,r.y+thickness,r.width-2*thickness,r.height-2*thickness},std::max(0.f,cut-thickness));
  TriangleMesh mesh;mesh.color={255,255,255,255};mesh.clip=clip;
  mesh.vertices.assign(outer.begin(),outer.end());mesh.vertices.insert(mesh.vertices.end(),inner.begin(),inner.end());
  for(const auto p:mesh.vertices)mesh.vertex_colors.push_back(blend(top,bottom,(p.y-r.y)/r.height));
  for(int i=0;i<8;++i){const int next=(i+1)%8;mesh.indices.insert(mesh.indices.end(),{i,next,8+i,next,8+next,8+i});}
  out.overlay.emplace_back(std::move(mesh));
}
inline void surface(DrawList &out,UiRect r,float scale=1.f,bool selected=false,std::optional<UiRect> clip={}){
  const float cut=7*scale;
  gradient(out,r,{5,22,34,250},{1,10,18,249},cut,clip);
  if(selected)rim(out,r,{68,180,231,80},{12,89,124,60},4*scale,cut,clip);
  rim(out,r,selected?Color{124,226,255,255}:Color{58,126,159,235},
      selected?Color{24,135,181,255}:Color{25,65,85,230},std::max(1.f,scale),cut,clip);
  const UiRect line{r.x+cut+5*scale,r.y+2*scale,std::max(0.f,r.width-2*cut-10*scale),scale};
  gradient(out,line,{115,220,255,selected?std::uint8_t{140}:std::uint8_t{40}},{20,65,90,0},0,clip);
}
inline void control(DrawList &out,UiRect r,bool hover=false,bool active=false,bool enabled=true,float scale=1.f,std::optional<UiRect> clip={}){
  const bool lit=enabled&&(hover||active);const float cut=4*scale;
  gradient(out,r,!enabled?Color{8,20,28,235}:lit?Color{13,70,102,255}:Color{5,29,44,248},
      !enabled?Color{5,14,22,240}:lit?Color{3,30,51,255}:Color{2,14,24,248},cut,clip);
  if(lit)rim(out,r,{34,149,214,95},{3,71,103,65},4*scale,cut,clip);
  rim(out,r,!enabled?Color{41,64,79,220}:lit?Color{123,226,255,255}:Color{50,107,140,230},
      !enabled?Color{28,44,54,220}:lit?Color{31,133,188,255}:Color{19,57,78,230},std::max(1.f,scale),cut,clip);
  if(active)gradient(out,{r.x+cut,r.y+r.height-2*scale,r.width-2*cut,2*scale},{81,212,255,255},{16,111,166,255},0,clip);
}
inline void progress(DrawList &out,UiRect r,float fraction,float scale=1.f,std::optional<UiRect> clip={}){
  gradient(out,r,{7,29,44,255},{4,19,30,255},r.height*.5f,clip);
  if(fraction>0)gradient(out,{r.x,r.y,r.width*std::clamp(fraction,0.f,1.f),r.height},
      {131,225,255,255},{20,144,202,255},std::min(3*scale,r.height*.5f),clip);
}
} // namespace stellar::engine::ui_skin
